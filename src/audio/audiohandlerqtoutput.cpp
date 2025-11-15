// =============================
// audiohandleroutput.cpp
// =============================
#include "audiohandlerqtoutput.h"

bool audioHandlerQtOutput::openDevice() noexcept
{
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    audioOutput = new QAudioOutput(deviceInfo, nativeFormat, this);
    connect(audioOutput, SIGNAL(stateChanged(QAudio::State)), this, SLOT(stateChanged(QAudio::State)));
#else
    audioOutput = new QAudioSink(deviceInfo, nativeFormat, this);
    connect(audioOutput, &QAudioSink::stateChanged, this, &audioHandlerQtOutput::stateChanged);
#endif

    emit setupConverter(radioFormat,  codec,
                        nativeFormat, codecType::LPCM,
                        7, setupData.resampleQuality);

    connect(this, &audioHandlerBase::sendToConverter, converter, &audioConverter::convert);
    connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(onConverted(audioPacket)));

#if (defined(Q_OS_WIN) && (QT_VERSION < QT_VERSION_CHECK(6,0,0)))
    audioOutput->setBufferSize(nativeFormat.bytesForDuration(setupData.latency * 100));
#else
    audioOutput->setBufferSize(nativeFormat.bytesForDuration(setupData.latency * 1000));
#endif

    audioDevice = audioOutput->start();
    if (!audioDevice) return false;

    connect(audioOutput, SIGNAL(destroyed()), audioDevice, SLOT(deleteLater()), Qt::UniqueConnection);
    qInfo(logAudio()) << "Connected to Qt audio output device" << deviceInfo.description();
    return true;
}

void audioHandlerQtOutput::closeDevice() noexcept
{
    if (audioOutput) {
        if (audioOutput->state() != QAudio::StoppedState) audioOutput->stop();
        audioOutput->deleteLater();
        audioOutput = nullptr;
    }
    audioDevice = nullptr;
}

void audioHandlerQtOutput::incomingAudio(audioPacket packet)
{
    if (!audioDevice || packet.data.isEmpty()) return;
    packet.volume = volume;
    emit sendToConverter(packet);
}

void audioHandlerQtOutput::onConverted(audioPacket pkt)
{
    if (!audioOutput || !audioDevice || pkt.data.isEmpty()) return;
    const int nowToPktMs = pkt.time.msecsTo(QTime::currentTime());
    if (nowToPktMs > setupData.latency * 1.5) {
        return; // late frame ( more than latency * 1.5), drop
    }
    writeToOutputDevice(pkt.data, pkt.seq, pkt.amplitudePeak, pkt.amplitudeRMS);
}

void audioHandlerQtOutput::writeToOutputDevice(QByteArray data, quint32 seq, float ampPeak, float ampRms)
{
    Q_UNUSED(seq);
    if (!audioOutput || !audioDevice) return;

    qint64 buffered = audioOutput->bufferSize() - audioOutput->bytesFree();
    int devLatencyMs = static_cast<int>(nativeFormat.durationForBytes(buffered) / 1000);
    int pipelineMs   = lastReceived.isValid() ? static_cast<int>(lastReceived.elapsed()) : 0;
    int newLatency   = pipelineMs + devLatencyMs;
    int prev = currentLatency.load(std::memory_order_relaxed);
    currentLatency.store(static_cast<int>(prev * 0.8 + newLatency * 0.2), std::memory_order_relaxed);

    qint64 toWrite = data.size();
    const char* p  = data.constData();
    while (toWrite > 0) {
        qint64 written = audioDevice->write(p, toWrite);
        if (written <= 0) break;
        p       += written;
        toWrite -= written;
    }

    lastReceived.restart();
    amplitude.store(ampPeak, std::memory_order_relaxed);
    emit haveLevels(amplitudePeak(), static_cast<quint16>(ampRms * 255.0f), setupData.latency, currentLatency.load(), isUnderrun.load(), isOverrun.load());
}

QAudioFormat audioHandlerQtOutput::getNativeFormat()
{
    return setupData.port.preferredFormat();
}

bool audioHandlerQtOutput::isFormatSupported(QAudioFormat f)
{
    return setupData.port.isFormatSupported(f);
}
