// =============================
// audiohandler_base.cpp (fixed names)
// =============================
#include "audiohandlerbase.h"

audioHandlerBase::audioHandlerBase(QObject* parent)
    : QObject(parent)
{
}

audioHandlerBase::~audioHandlerBase()
{
}


void audioHandlerBase::dispose()
{
    bool expected = false;
    if (!disposed.compare_exchange_strong(expected, true)) return; // already disposed

    // Ensure we run on this object's thread (prevent races with audio callbacks)
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this]{ dispose(); },
                                  Qt::BlockingQueuedConnection);
        return;
    }

    QMutexLocker lock(&devMutex);
    closeDevice();

    if (underTimer) {
        underTimer->stop();
        underTimer->deleteLater();
        underTimer=nullptr;
    }

    if (converterThread) {
        disconnect(this, nullptr, nullptr, nullptr);
        disconnect(converterThread, nullptr, nullptr, nullptr);
        converterThread->quit();
        converterThread->wait();
        converterThread->deleteLater();
        converterThread = nullptr;
    }
}

void audioHandlerBase::reportError(const QString& msg)
{
    qCritical(logAudio()).noquote() << role() << msg;
    emit audioMessage(QString("%1: %2").arg(role(), msg));
}

bool audioHandlerBase::negotiateFormat(int minSampleRate)
{
    nativeFormat = getNativeFormat();

    if (nativeFormat.channelCount() < 1) {
        reportError("Cannot initialize audio device, no channels found");
        return false;
    }

    if (nativeFormat.channelCount() > 2) nativeFormat.setChannelCount(2);

    if (nativeFormat.channelCount() == 1 && radioFormat.channelCount() == 2) {
        nativeFormat.setChannelCount(2);


#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        if (!isFormatSupported(nativeFormat)) nativeFormat.setChannelCount(1);
#else
        if (!isFormatSupported(nativeFormat)) nativeFormat.setChannelCount(1);
#endif
    }

    if (nativeFormat.sampleRate() < minSampleRate) {
        const int prev = nativeFormat.sampleRate();
        nativeFormat.setSampleRate(minSampleRate);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        if (!isFormatSupported(nativeFormat)) nativeFormat.setSampleRate(prev);
#else
        if (!isFormatSupported(nativeFormat)) nativeFormat.setSampleRate(prev);
#endif
    }

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    if (nativeFormat.sampleType() == QAudioFormat::UnSignedInt && nativeFormat.sampleSize() == 8) {
        nativeFormat.setSampleType(QAudioFormat::SignedInt);
        nativeFormat.setSampleSize(16);
        if (!isFormatSupported(nativeFormat)) {
            nativeFormat.setSampleType(QAudioFormat::UnSignedInt);
            nativeFormat.setSampleSize(8);
        }
    }
    if (nativeFormat.sampleSize() == 24) {
        nativeFormat.setSampleSize(32);
        if (!isFormatSupported(nativeFormat)) nativeFormat.setSampleSize(16);
    }
#else
    if (nativeFormat.sampleFormat() == QAudioFormat::UInt8) {
        nativeFormat.setSampleFormat(QAudioFormat::Int16);
        if (!isFormatSupported(nativeFormat)) nativeFormat.setSampleFormat(QAudioFormat::UInt8);
    }
#endif

    qDebug(logAudio()) << role() << "Selected format: ch=" << nativeFormat.channelCount()
                       << " rate=" << nativeFormat.sampleRate();
    return true;
}

bool audioHandlerBase::init(const audioSetup& setup)
{
    QMutexLocker lock(&devMutex);
    if (initialized) return true;

    setupData = setup;
    radioFormat = toQAudioFormat(setup.codec, setup.sampleRate);

    codec = codecType::LPCM;
    if (setup.codec == 0x01 || setup.codec == 0x20)      codec = codecType::PCMU;
    else if (setup.codec == 0x40 || setup.codec == 0x41) codec = codecType::OPUS;
    else if (setup.codec == 0x80)                        codec = codecType::ADPCM;

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    if (setup.port.isNull() && setup.portInt == -1) {
        reportError("No audio device was found (install Qt Multimedia plugins?)");
        return false;
    }
    deviceInfo = setup.port;
#else
    if (setup.port.isNull() && setup.port.description().isEmpty() && setup.portInt == -1) {
        reportError("Audio device is NULL, check device selection in settings");
        return false;
    } else {
        deviceInfo = setup.port;
    }
#endif

    if (!negotiateFormat(48000)) return false;

    converter   = new audioConverter();
    converterThread  = new QThread(this);
    converterThread->setObjectName(role() == "Input" ? "audioConvIn()" : "audioConvOut()");
    converter->moveToThread(converterThread);

    connect(this, &audioHandlerBase::setupConverter, converter, &audioConverter::init);
    connect(converterThread, &QThread::finished, converter, &QObject::deleteLater);

    converterThread->start(QThread::TimeCriticalPriority);

    underTimer = new QTimer(this);
    underTimer->setSingleShot(true);
    connect(underTimer, &QTimer::timeout, this, &audioHandlerBase::clearUnderrun);

    setVolume(setup.localAFgain);

    if (!openDevice()) {
        reportError("Failed to open device");
        return false;
    }

    initialized = true;
    qInfo(logAudio()) << role() << "thread id" << QThread::currentThreadId();
    return true;
}

void audioHandlerBase::start() {
    QMutexLocker lock(&devMutex);
}

void audioHandlerBase::stop()  {
    QMutexLocker lock(&devMutex);
    closeDevice();
}

void audioHandlerBase::setVolume(quint8 volumeIdx) {
    volume = audiopot[volumeIdx];
    //qInfo(logAudio) << "Setting audio volume:" << volume;
}

void audioHandlerBase::changeLatency(quint16 newLatencyMs) {
    QMutexLocker lock(&devMutex);
    setupData.latency = newLatencyMs;
}

void audioHandlerBase::stateChanged(QAudio::State state)
{
    switch (state) {
    case QAudio::IdleState:
        isUnderrun.store(true, std::memory_order_relaxed);
        if (underTimer->isActive()) underTimer->stop();
        break;
    case QAudio::ActiveState:
        if (!underTimer->isActive()) underTimer->start(500);
        break;
    default: break;
    }
}

void audioHandlerBase::clearUnderrun() { isUnderrun.store(false, std::memory_order_relaxed); }


