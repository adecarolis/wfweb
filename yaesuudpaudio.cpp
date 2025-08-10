#include "yaesuudpaudio.h"
#include "logcategories.h"

yaesuUdpAudio::yaesuUdpAudio(QHostAddress local, QHostAddress remote, quint16 port, audioSetup rxAudio, audioSetup txAudio) :
    rxSetup(rxAudio),txSetup(txAudio)
{
    remotePort = port;
    remoteAddr = remote;
    localAddr = local;
}

yaesuUdpAudio::~yaesuUdpAudio()
{
    if (rxAudioThread != Q_NULLPTR) {
        qDebug(logUdp()) << "Stopping rxaudio thread";
        rxAudioThread->quit();
        rxAudioThread->wait();
    }

    if (txAudioThread != Q_NULLPTR) {
        qDebug(logUdp()) << "Stopping txaudio thread";
        txAudioThread->quit();
        txAudioThread->wait();
    }
    qDebug(logUdp()) << "Yaesu udpAudio successfully closed";
}

void yaesuUdpAudio::init()
{

    /* Once we have our own server, we need to add Opus/ADPCM options
        ("Opus 1ch", 64); ("Opus 2ch", 65); ("ADPCM 1ch", 128);
        Do this in init as cannot emit() from constructor
    */

    switch (this->rxSetup.codec)
    {
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
    case 4:
        audioSize = 320;
        audioChannels = 1;
    case 16:
        break;
    case 1:
        audioChannels = 1;
        audioSize = 160;
    case 32:
        if (this->rxSetup.codec==32)
            audioSize = 320;
        audioCodec = MuLaw;
        break;
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif
    default:
        qInfo(logUdp()) << "Unsupported audio codec";
        emit haveNetworkError(errorType(true, remoteAddr.toString(), "Unsupported audio codec"));

        return;
        break;
    }

    // Connect audio ready to receive it:
    if (rxSetup.type == qtAudio) {
        rxaudio = new audioHandler();
    }
    else if (rxSetup.type == portAudio) {
        rxaudio = new paHandler();
    }
    else if (rxSetup.type == rtAudio) {
        rxaudio = new rtHandler();
    }
#ifndef BUILD_WFSERVER
    else if (rxSetup.type == tciAudio) {
        rxaudio = new tciAudioHandler();
    }
#endif
    else
    {
        qCritical(logAudio()) << "Unsupported Receive Audio Handler selected!";
        return;
    }

    rxAudioThread = new QThread(this);
    rxAudioThread->setObjectName("rxAudio()");

    rxaudio->moveToThread(rxAudioThread);

    rxAudioThread->start(QThread::TimeCriticalPriority);

    connect(this, SIGNAL(setupRxAudio(audioSetup)), rxaudio, SLOT(init(audioSetup)));

    connect(this, SIGNAL(haveAudioData(audioPacket)), rxaudio, SLOT(incomingAudio(audioPacket)));
    //connect(this, SIGNAL(haveChangeLatency(quint16)), rxaudio, SLOT(changeLatency(quint16)));
    connect(this, SIGNAL(haveSetVolume(quint8)), rxaudio, SLOT(setVolume(quint8)));
    connect(rxaudio, SIGNAL(haveLevels(quint16,quint16,quint16,quint16,bool,bool)), this, SLOT(getRxLevels(quint16,quint16,quint16,quint16,bool,bool)));
    connect(rxAudioThread, SIGNAL(finished()), rxaudio, SLOT(deleteLater()));

    emit setupRxAudio(rxSetup);

    if (txSetup.type == qtAudio) {
        txaudio = new audioHandler();
    }
    else if (txSetup.type == portAudio) {
        txaudio = new paHandler();
    }
    else if (txSetup.type == rtAudio) {
        txaudio = new rtHandler();
    }
#ifndef BUILD_WFSERVER
    else if (txSetup.type == tciAudio) {
        txaudio = new tciAudioHandler();
    }
#endif
    else
    {
        qCritical(logAudio()) << "Unsupported Transmit Audio Handler selected!";
        return;
    }

    txAudioThread = new QThread(this);
    rxAudioThread->setObjectName("txAudio()");

    txaudio->moveToThread(txAudioThread);

    txAudioThread->start(QThread::TimeCriticalPriority);

    connect(this, SIGNAL(setupTxAudio(audioSetup)), txaudio, SLOT(init(audioSetup)));
    connect(txaudio, SIGNAL(haveAudioData(audioPacket)), this, SLOT(receiveAudioData(audioPacket)));
    connect(txaudio, SIGNAL(haveLevels(quint16, quint16, quint16, quint16, bool, bool)), this, SLOT(getTxLevels(quint16, quint16, quint16, quint16, bool, bool)));

    connect(txAudioThread, SIGNAL(finished()), txaudio, SLOT(deleteLater()));
    emit setupTxAudio(txSetup);




    qInfo(logUdp()) << "Sending connect packet";
    yaesuUdpBase::init();
}

void yaesuUdpAudio::incomingUdp(void* buf, size_t bufLen)
{
    // First cast buf to just the header to find the packet type;
    if (bufLen <= sizeof(yaesuFrameHeader))
    {
        qInfo(logUdp()) << "Packet too small!";
        return;
    }
    yaesuFrameHeader* h = (yaesuFrameHeader*)buf;

    switch (h->msgtype)
    {
    case R2C_ConnectReply:
        // Confirmed that we are connected.
        qInfo(logUdp()) << "Got Audio Connect reply";
        //state = yaesuAudioPending;
        yaesuC2R_AudioData d2;
        memset(&d2,0,sizeof(d2));
        d2.hdr.device=UsbAudio;
        d2.hdr.msgtype=C2R_Data;
        d2.session = session;
        d2.data.channels = this->audioChannels;
        d2.data.format=this->audioCodec; // MuLaw; //ShortLE;
        d2.data.seqNum = txPacketId++;
        d2.data.playbackLatency=this->rxSetup.latency*10;
        d2.data.recordLatency=this->txSetup.latency*10;
        d2.data.playbackVol=100;
        d2.data.recordVol=100;
        d2.data.sampleRate=this->rxSetup.sampleRate;
        d2.data.resendMode = 0x01;
        d2.data.pcmDataLen = 0;
        d2.hdr.len=sizeof(d2) - sizeof(d2.hdr) - sizeof(d2.data.pcmData) + d2.data.pcmDataLen;
        outgoing((quint8*)&d2,sizeof(d2) - sizeof(d2.data.pcmData) + d2.data.pcmDataLen);
        outgoing((quint8*)&d2,sizeof(d2) - sizeof(d2.data.pcmData) + d2.data.pcmDataLen);
        d2.data.seqNum = txPacketId++;
        d2.data.pcmDataLen = this->audioSize;
        outgoing((quint8*)&d2,sizeof(d2) - sizeof(d2.data.pcmData) + d2.data.pcmDataLen);
        outgoing((quint8*)&d2,sizeof(d2) - sizeof(d2.data.pcmData) + d2.data.pcmDataLen);
        state = yaesuConnected;
        //heartbeat.setInterval(1000);

        break;
    case R2C_Data:
    {
        // Now audio is connected, should we connect scope, or wait until we have rigCaps?
        /*
        if (state != yaesuConnected)
        {
            state = yaesuConnected;
        }
        */

        if (bufLen <= sizeof(yaesuR2C_AudioData))
        {
            yaesuR2C_AudioData* r = (yaesuR2C_AudioData*)buf;
            // Ignore duplicate packets
            if (r->data.seqNum != rxPacketId)
            {
                rxPacketId = r->data.seqNum;
                if (r->data.channels != this->audioChannels || r->data.format != (quint8)this->audioCodec || r->data.sampleRate != this->rxSetup.sampleRate)
                {
                    qInfo(logUdp ) << "RX Audio packet mismatch" << r->data.channels << "channels, " << r->data.format  << "format, " << r->data.sampleRate << "sample rate, " << r->data.pcmDataLen << "bytes";

                } else {
                    if (r->data.seqNum == 0)
                    {
                        // Seq number has rolled over.
                        seqPrefix++;
                    }
                    audioPacket tempAudio;
                    tempAudio.seq = (quint32)seqPrefix << 16 | r->data.seqNum;
                    tempAudio.time = QTime::currentTime();
                    tempAudio.sent = 0;
                    tempAudio.data = QByteArray::fromRawData((char *)r->data.pcmData,r->data.pcmDataLen);
                    emit haveAudioData(tempAudio);
                }
            }
        } else {
            qInfo(logUdp()) << "Audio packet too large!" << bufLen << "bytes";
        }

        break;
    }

    default:
        qInfo(logUdp()) << QString("Yaesu audio, unsupported packet device:%0 type:%1 len:%2").arg(h->device).arg(h->msgtype).arg(h->len);
        break;
    }
}


void yaesuUdpAudio::sendHeartbeat()
{
    yaesuC2R_HeartbeatFrame f;
    memset(&f,0,sizeof(f));
    f.hdr.device=ScuLan;
    f.hdr.msgtype=C2R_HealthCheck;
    f.hdr.len=sizeof(f)-sizeof(f.hdr);
    f.session=session;
    outgoing((quint8*)&f,sizeof(yaesuC2R_HeartbeatFrame));
}

void yaesuUdpAudio::sendConnect()
{
    yaesuResultFrame b1;
    memset(&b1,0,sizeof(b1));
    b1.hdr.device=UsbAudio;
    b1.hdr.msgtype=C2R_Connect;
    b1.hdr.len=sizeof(b1)-sizeof(b1.hdr);
    b1.result = session;
    outgoing((quint8*)&b1,sizeof(b1));
}

void yaesuUdpAudio::setVolume(quint8 vol)
{
    emit haveSetVolume(vol);
}

void yaesuUdpAudio::getRxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over)
{
    status.rxAudioLevel = amplitudePeak;
    status.rxLatency = latency;
    status.rxCurrentLatency =   qint32(current);
    status.rxUnderrun = under;
    status.rxOverrun = over;
    audioLevelsRxPeak[(audioLevelsRxPosition)%audioLevelBufferSize] = amplitudePeak;
    audioLevelsRxRMS[(audioLevelsRxPosition)%audioLevelBufferSize] = amplitudeRMS;

    if((audioLevelsRxPosition)%4 == 0)
    {
        // calculate mean and emit signal
        quint8 meanPeak = findMax(audioLevelsRxPeak);
        quint8 meanRMS = findMean(audioLevelsRxRMS);
        networkAudioLevels l;
        l.haveRxLevels = true;
        l.rxAudioPeak = meanPeak;
        l.rxAudioRMS = meanRMS;
        emit haveNetworkAudioLevels(l);
    }
    audioLevelsRxPosition++;
}

void yaesuUdpAudio::getTxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over)
{
    status.txAudioLevel = amplitudePeak;
    status.txLatency = latency;
    status.txCurrentLatency = qint32(current);
    status.txUnderrun = under;
    status.txOverrun = over;
    audioLevelsTxPeak[(audioLevelsTxPosition)%audioLevelBufferSize] = amplitudePeak;
    audioLevelsTxRMS[(audioLevelsTxPosition)%audioLevelBufferSize] = amplitudeRMS;

    if((audioLevelsTxPosition)%4 == 0)
    {
        // calculate mean and emit signal
        quint8 meanPeak = findMax(audioLevelsTxPeak);
        quint8 meanRMS = findMean(audioLevelsTxRMS);
        networkAudioLevels l;
        l.haveTxLevels = true;
        l.txAudioPeak = meanPeak;
        l.txAudioRMS = meanRMS;
        emit haveNetworkAudioLevels(l);
    }
    audioLevelsTxPosition++;
}


quint8 yaesuUdpAudio::findMean(quint8 *data)
{
    unsigned int sum=0;
    for(int p=0; p < audioLevelBufferSize; p++)
    {
        sum += data[p];
    }
    return sum / audioLevelBufferSize;
}

quint8 yaesuUdpAudio::findMax(quint8 *data)
{
    unsigned int max=0;
    for(int p=0; p < audioLevelBufferSize; p++)
    {
        if(data[p] > max)
            max = data[p];
    }
    return max;
}


void yaesuUdpAudio::receiveAudioData(audioPacket audio) {
    // I really can't see how this could be possible but a quick sanity check!
    if (txaudio == Q_NULLPTR) {
        return;
    }
    if (audio.data.length() > 0) {
        int len = 0;

        while (len < audio.data.length()) {
            QByteArray partial = audio.data.mid(len, this->audioSize);
            yaesuC2R_AudioData d2;
            memset(&d2,0,sizeof(d2));
            d2.hdr.device=UsbAudio;
            d2.hdr.msgtype=C2R_Data;
            d2.session = session;
            d2.data.channels = this->audioChannels;
            d2.data.format=this->audioCodec; // MuLaw; //ShortLE;
            d2.data.seqNum = txPacketId++;
            d2.data.playbackLatency=this->rxSetup.latency*10;
            d2.data.recordLatency=this->txSetup.latency*10;
            d2.data.playbackVol=100;
            d2.data.recordVol=100;
            d2.data.sampleRate=this->rxSetup.sampleRate;
            d2.data.resendMode = 0x01;
            d2.data.pcmDataLen = partial.size();
            d2.hdr.len=sizeof(d2) - sizeof(d2.hdr) - sizeof(d2.data.pcmData) + d2.data.pcmDataLen;
            memcpy(d2.data.pcmData,partial.constData(),partial.size());
            len = len + partial.length();
            outgoing((quint8*)&d2,sizeof(d2) - sizeof(d2.data.pcmData) + d2.data.pcmDataLen);
            outgoing((quint8*)&d2,sizeof(d2) - sizeof(d2.data.pcmData) + d2.data.pcmDataLen);
        }
    }
}
