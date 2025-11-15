#include "audiohandlerrtinput.h"

#include <cstring>

audioHandlerRtInput::audioHandlerRtInput(QObject* parent)
    : audioHandlerBase(parent) {}



int audioHandlerRtInput::rtCallback(void* out, void* in, unsigned nFrames, double, RtAudioStreamStatus st, void* u)
{ Q_UNUSED(out); static_cast<audioHandlerRtInput*>(u)->handleCallbackInput(in, nFrames, st); return 0; }


void audioHandlerRtInput::handleCallbackInput(const void* in, unsigned nFrames, RtAudioStreamStatus st)
{
    if (st & RTAUDIO_INPUT_OVERFLOW) isOverrun.store(true);
    if (!in) return;
    const size_t nBytes = nFrames * bytesPerFrame;
    inRB->push(static_cast<const char*>(in), nBytes);
    QMetaObject::invokeMethod(this, [this]{ onReadyRead(); }, Qt::QueuedConnection);
}


bool audioHandlerRtInput::openDevice() noexcept
{

    qDebug(logAudio()) << "Creating input RT Audio device:" << setupData.name;
#ifdef RT_EXCEPTION
    try {
        if (!rtaudio.getDeviceCount()) return false;
        RtAudio::StreamParameters inParams; inParams.deviceId=setupData.portInt; inParams.nChannels=nativeFormat.channelCount(); inParams.firstChannel=0;
        RtAudio::StreamOptions opts; opts.streamName = "audioHandlerRtInput";
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        fmt = (nativeFormat.sampleSize()==16) ? RTAUDIO_SINT16 : RTAUDIO_FLOAT32;
#else
        fmt = (nativeFormat.sampleFormat()==QAudioFormat::Int16) ? RTAUDIO_SINT16 : RTAUDIO_FLOAT32;
#endif
        bytesPerSample = (fmt==RTAUDIO_SINT16?2:4);
        bytesPerFrame = bytesPerSample * nativeFormat.channelCount();
        const unsigned sr = nativeFormat.sampleRate();
        unsigned frames = qMax(64u, static_cast<unsigned>((setupData.latency * sr)/1000));
        inRB = std::make_unique<ByteRingRt>(nativeFormat.bytesForDuration(setupData.latency*2000));


        rtaudio.openStream(nullptr, &inParams, fmt, sr, &frames, &audioHandlerRtInput::rtCallback, this, &opts);
        rtaudio.startStream();


        emit setupConverter(nativeFormat, codecType::LPCM, radioFormat, codec, 7, setupData.resampleQuality);
        connect(this, &audioHandlerBase::sendToConverter, converter, &audioConverter::convert);
        connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(onConverted(audioPacket)));
        return true;
    } catch (RtAudioError& e) { reportError(QString::fromStdString(e.getMessage())); return false; }
#else
    if (!rtaudio.getDeviceCount()) return false;
    // Optional: centralised warning handler

#if RTAUDIO_VERSION_MAJOR >= 6
    rtaudio.setErrorCallback([](RtAudioErrorType t, const std::string& m){
        qWarning() << "RtAudio error" << int(t) << ":" << QString::fromStdString(m);
    });
#endif

    RtAudio::StreamParameters inParams; inParams.deviceId=setupData.portInt; inParams.nChannels=nativeFormat.channelCount(); inParams.firstChannel=0;
    RtAudio::StreamOptions opts; opts.streamName = "audioHandlerRtInput";

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    bool useInt16 = (nativeFormat.sampleSize() == 16);
#else
    bool useInt16 = (nativeFormat.sampleFormat() == QAudioFormat::Int16);
#endif
    fmt = useInt16 ? RTAUDIO_SINT16 : RTAUDIO_FLOAT32;
    bytesPerSample = useInt16 ? 2 : 4;
    bytesPerFrame  = bytesPerSample * nativeFormat.channelCount();

    const unsigned sr = nativeFormat.sampleRate();
    unsigned frames = qMax(64u, static_cast<unsigned>((setupData.latency * sr)/1000));
    inRB = std::make_unique<ByteRing>(nativeFormat.bytesForDuration(setupData.latency*2000));

    rtaudio.openStream(nullptr, &inParams, fmt, sr, &frames, &audioHandlerRtInput::rtCallback, this, &opts);
    if (rtaudio.isStreamOpen()) rtaudio.startStream();
    if (!rtaudio.isStreamRunning()) { reportError("RtAudio input stream failed to start"); return false; }


    emit setupConverter(nativeFormat, codecType::LPCM, radioFormat, codec, 7, setupData.resampleQuality);
    connect(this, &audioHandlerBase::sendToConverter, converter, &audioConverter::convert);
    connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(onConverted(audioPacket)));
    return true;
#endif
}


void audioHandlerRtInput::closeDevice() noexcept
{
#ifdef RT_EXCEPTION
    try {
        if (rtaudio.isStreamOpen())
        {
            if (rtaudio.isStreamRunning())
            {
                rtaudio.stopStream();
            }
            rtaudio.closeStream();
        }
    } catch(...) {}
#else
    if (rtaudio.isStreamOpen()) {
        if (rtaudio.isStreamRunning())
        {
            rtaudio.stopStream();
        }
        rtaudio.closeStream();
    }
#endif
    inRB.reset();
}

void audioHandlerRtInput::onReadyRead()
{
    if (!inRB) return;

    QByteArray chunk;
    chunk.resize(static_cast<int>(inRB->size()));
    const size_t got = inRB->pop(chunk.data(), static_cast<size_t>(chunk.size()));
    if (!got) return;
    chunk.resize(static_cast<int>(got));

    tempBuf.data += chunk;

    const int bytesPerBlock = nativeFormat.bytesForDuration(setupData.blockSize * 1000);
    while (tempBuf.data.size() >= bytesPerBlock) {
        audioPacket pkt;
        pkt.time   = QTime::currentTime();
        pkt.sent   = 0;
        pkt.volume = volume;
        std::memcpy(&pkt.guid, setupData.guid, GUIDLEN);
        pkt.data   = tempBuf.data.left(bytesPerBlock);
        tempBuf.data.remove(0, bytesPerBlock);
        emit sendToConverter(pkt);
    }
}

void audioHandlerRtInput::onConverted(audioPacket audio)
{
    if (audio.data.isEmpty()) return;

    emit haveAudioData(audio);

    if (!lastReceived.isValid()) lastReceived.start();
    else                         lastReceived.restart();

    amplitude.store(audio.amplitudePeak);
    emit haveLevels(amplitudePeak(), audio.amplitudeRMS,
                    setupData.latency, currentLatency.load(),
                    isUnderrun.load(), isOverrun.load());
}


bool audioHandlerRtInput::isFormatSupported(QAudioFormat f)
{
    bool ret=true;
#ifdef RT_EXCEPTION
    try {
        RtAudio::DeviceInfo info = rtaudio.getDeviceInfo(setupData.portInt);
    }
    catch (RtAudioError e) {
        qInfo(logAudio()) << "Device exception:" << setupData.portInt << ":" << QString::fromStdString(e.getMessage());
        return false;
    }
    if (info.probed)
    {
#else
    RtAudio::DeviceInfo info = rtaudio.getDeviceInfo(setupData.portInt);
#endif

        // Is this format supported?
        // First look through supported channel numbers
        if (int(info.inputChannels) < f.channelCount())
            ret = false;

        // Now search sample rates:
        if (std::find(info.sampleRates.begin(), info.sampleRates.end(), f.sampleRate()) == info.sampleRates.end())
            ret = false;

        // Now search input format:

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        if (!(f.sampleType() == QAudioFormat::Float && info.nativeFormats & RTAUDIO_FLOAT32) &&
            !(f.sampleType() == QAudioFormat::SignedInt && f.sampleSize() == 32 && info.nativeFormats & RTAUDIO_SINT32) &&
            !(f.sampleType() == QAudioFormat::SignedInt && f.sampleSize() == 16 && info.nativeFormats & RTAUDIO_SINT16))
#else
        if (!(f.sampleFormat() == QAudioFormat::Float && info.nativeFormats & RTAUDIO_FLOAT32) &&
            !(f.sampleFormat() == QAudioFormat::Int32 && info.nativeFormats & RTAUDIO_SINT32) &&
            !(f.sampleFormat() == QAudioFormat::Int16 && info.nativeFormats & RTAUDIO_SINT16))
#endif
    {
    ret = false;
    }
#ifdef RT_EXCEPTION
    }
#endif


    return ret;
}


QAudioFormat audioHandlerRtInput::getNativeFormat()
{

    QAudioFormat native;
#ifdef RT_EXCEPTION
    try {
        RtAudio::DeviceInfo info = rtaudio.getDeviceInfo(setupData.portInt);
    }
    catch (RtAudioError e) {
        qInfo(logAudio()) << "Device exception:" << aParams.deviceId << ":" << QString::fromStdString(e.getMessage());
        return false;
    }
    if (info.probed)
    {
#else
    RtAudio::DeviceInfo info = rtaudio.getDeviceInfo(setupData.portInt);
#endif

        qDebug(logAudio()) << QString::fromStdString(info.name) << "(" << setupData.portInt << ") successfully probed";

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        native.setByteOrder(QAudioFormat::LittleEndian);
        native.setCodec("audio/pcm");
#endif

        if (info.nativeFormats == 0)
        {
            qCritical(logAudio()) << "		No natively supported data formats!";
            return QAudioFormat();
        }

        qDebug(logAudio()) << "rtaudio input channels:" << info.inputChannels << " preferred sample rate:" << info.preferredSampleRate;
        native.setChannelCount(info.inputChannels);
        native.setSampleRate(info.preferredSampleRate);

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        if (info.nativeFormats & RTAUDIO_FLOAT32) {
            native.setSampleType(QAudioFormat::Float);
            native.setSampleSize(32);
        }
        else if (info.nativeFormats & RTAUDIO_SINT32)
        {
            native.setSampleType(QAudioFormat::SignedInt);
            native.setSampleSize(32);
        }
        else if (info.nativeFormats & RTAUDIO_SINT16)
        {
            native.setSampleType(QAudioFormat::SignedInt);
            native.setSampleSize(16);
        }
        else if (info.nativeFormats & RTAUDIO_SINT24)
        {
            native.setSampleType(QAudioFormat::SignedInt);
            native.setSampleSize(24);
        }
#else
        if (info.nativeFormats & RTAUDIO_FLOAT32) {
            native.setSampleFormat(QAudioFormat::Float);
        }
        else if (info.nativeFormats & RTAUDIO_SINT32)
        {
            native.setSampleFormat(QAudioFormat::Int32);
        }
        else if (info.nativeFormats & RTAUDIO_SINT16)
        {
            native.setSampleFormat(QAudioFormat::Int16);
        }
        else if (info.nativeFormats & RTAUDIO_SINT24)
        {
            native.setSampleFormat(QAudioFormat::Unknown);
        }
#endif
        else {
            qCritical(logAudio()) << "Cannot find default sample format!";
            return QAudioFormat();
        }

#ifdef RT_EXCEPTION
    }
#endif

    return native;
}
