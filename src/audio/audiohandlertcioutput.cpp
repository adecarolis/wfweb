#include "audiohandlertcioutput.h"

bool audioHandlerTciOutput::openDevice() noexcept
{

    emit setupConverter(radioFormat,  codec,
                        nativeFormat, codecType::LPCM,
                        7, setupData.resampleQuality);

    connect(this, &audioHandlerBase::sendToConverter, converter, &audioConverter::convert);
    connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(onConverted(audioPacket)));

    return true;
}

void audioHandlerTciOutput::closeDevice() noexcept
{
}

void audioHandlerTciOutput::incomingAudio(audioPacket packet)
{
    packet.volume = volume;
    emit sendToConverter(packet);
}

void audioHandlerTciOutput::onConverted(audioPacket pkt)
{
    const int nowToPktMs = pkt.time.msecsTo(QTime::currentTime());
    if (nowToPktMs > setupData.latency) {
        return; // late frame, drop
    }
}



QAudioFormat audioHandlerTciOutput::getNativeFormat()
{

    return QAudioFormat();
}

bool audioHandlerTciOutput::isFormatSupported(QAudioFormat f)
{
    return false;
}

