#include "audiohandlertciinput.h"

bool audioHandlerTciInput::openDevice() noexcept
{

    emit setupConverter(nativeFormat, codecType::LPCM,
                        radioFormat,  codec,
                        7, setupData.resampleQuality);

    connect(this, &audioHandlerBase::sendToConverter, converter, &audioConverter::convert);
    connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(onConverted(audioPacket)));

    return true;
}

void audioHandlerTciInput::closeDevice() noexcept
{
}

void audioHandlerTciInput::onReadyRead()
{

    const int bytesPerBlock = nativeFormat.bytesForDuration(setupData.blockSize * 1000);
    while (tempBuf.data.size() >= bytesPerBlock) {
        audioPacket pkt;
        pkt.time   = QTime::currentTime();
        pkt.sent   = 0;
        pkt.volume = volume;
        memcpy(&pkt.guid, setupData.guid, GUIDLEN);
        pkt.data   = tempBuf.data.left(bytesPerBlock);
        tempBuf.data.remove(0, bytesPerBlock);
        emit sendToConverter(pkt);
    }
}

void audioHandlerTciInput::onConverted(audioPacket audio)
{
    if (audio.data.isEmpty()) return;

    emit haveAudioData(audio);

    if (lastReceived.isValid() && lastReceived.elapsed() > 100) {
        qDebug(logAudio()) << role() << "Time since last audio packet" << lastReceived.elapsed()
        << "Expected around" << setupData.blockSize;
    }
    if (!lastReceived.isValid()) lastReceived.start(); else lastReceived.restart();

    amplitude.store(audio.amplitudePeak, std::memory_order_relaxed);
    emit haveLevels(amplitudePeak(), audio.amplitudeRMS, setupData.latency,
                    static_cast<quint16>(latencyMs()), isUnderrun.load(), isOverrun.load());
}


QAudioFormat audioHandlerTciInput::getNativeFormat()
{

    return QAudioFormat();
}

bool audioHandlerTciInput::isFormatSupported(QAudioFormat f)
{
    return false;
}
