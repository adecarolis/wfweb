#include "audiohandlertcioutput.h"
#include "tciserver.h"

bool audioHandlerTciOutput::openDevice() noexcept
{

    emit setupConverter(radioFormat,  codec,
                        nativeFormat, codecType::LPCM,
                        7, setupData.resampleQuality);

    connect(this, &audioHandlerBase::sendToConverter, converter, &audioConverter::convert);
    connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(onConverted(audioPacket)));

    if (setupData.tci != Q_NULLPTR) {
        connect(this,SIGNAL(sendTCIAudio(audioPacket)), (tciServer*)setupData.tci, SLOT(receiveTCIAudio(audioPacket)));
    } else {
        qCritical(logAudio()) << "***** TCI NOT FOUND *****";
    }
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

    arrayBuffer.append(pkt.data);
    amplitude = pkt.amplitudePeak;
    currentLatency = 0;
    if (arrayBuffer.length() >= qsizetype(TCI_AUDIO_LENGTH * sizeof(float))) {
        pkt.data.clear();
        pkt.data = arrayBuffer.mid(0, qsizetype(TCI_AUDIO_LENGTH * sizeof(float)));
        arrayBuffer.remove(0, qsizetype(TCI_AUDIO_LENGTH * sizeof(float)));
        emit sendTCIAudio(pkt);
        emit haveLevels(pkt.amplitudePeak, quint16(pkt.amplitudeRMS*255.0f), setupData.latency, currentLatency.load(), isUnderrun.load(), isOverrun.load());

    }
}



QAudioFormat audioHandlerTciOutput::getNativeFormat()
{    
    QAudioFormat native;
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    native.setByteOrder(QAudioFormat::LittleEndian);
    native.setCodec("audio/pcm");
    native.setSampleType(QAudioFormat::Float);
    native.setSampleSize(32);
#else
    native.setSampleFormat(QAudioFormat::Float);
#endif

    native.setSampleRate(48000);
    native.setChannelCount(2);
    return native;
}

bool audioHandlerTciOutput::isFormatSupported(QAudioFormat f)
{
    return true;
}

