#include "udpaudio.h"
#include "logcategories.h"

// Audio stream
udpAudio::udpAudio(QHostAddress local, QHostAddress ip, quint16 audioPort, quint16 lport, audioSetup rxSetup, audioSetup txSetup)
{
    qInfo(logUdp()) << "Starting udpAudio";
    this->localIP = local;
    this->port = audioPort;
    this->radioIP = ip;
    this->rxSetup = rxSetup;
    this->txSetup = txSetup;

    if (txSetup.sampleRate == 0) {
        enableTx = false;
    }

    init(lport); // Perform connection

    QUdpSocket::connect(udp, &QUdpSocket::readyRead, this, &udpAudio::dataReceived);
 
    startAudio();

    watchdogTimer = new QTimer();
    connect(watchdogTimer, &QTimer::timeout, this, &udpAudio::watchdog);
    watchdogTimer->start(WATCHDOG_PERIOD);

    areYouThereTimer = new QTimer();
    connect(areYouThereTimer, &QTimer::timeout, this, std::bind(&udpBase::sendControl, this, false, 0x03, 0));
    areYouThereTimer->start(AREYOUTHERE_PERIOD);
}

udpAudio::~udpAudio()
{

    if (pingTimer != Q_NULLPTR)
    {
        qDebug(logUdp()) << "Stopping pingTimer";
        pingTimer->stop();
        delete pingTimer;
        pingTimer = Q_NULLPTR;
    }

    if (idleTimer != Q_NULLPTR)
    {
        qDebug(logUdp()) << "Stopping idleTimer";
        idleTimer->stop();
        delete idleTimer;
        idleTimer = Q_NULLPTR;
    }

    if (watchdogTimer != Q_NULLPTR)
    {
        qDebug(logUdp()) << "Stopping watchdogTimer";
        watchdogTimer->stop();
        delete watchdogTimer;
        watchdogTimer = Q_NULLPTR;
    }

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
    qDebug(logUdp()) << "udpHandler successfully closed";
}

void udpAudio::watchdog()
{
    static bool alerted = false;
    if (lastReceived.msecsTo(QTime::currentTime()) > 2000)
    {
        if (!alerted) {
            /* Just log it at the moment, maybe try signalling the control channel that it needs to
                try requesting civ/audio again? */

            qInfo(logUdp()) << " Audio Watchdog: no audio data received for 2s, restart required?";
            alerted = true;
            if (rxAudioThread != Q_NULLPTR) {
                qDebug(logUdp()) << "Stopping rxaudio thread";
                rxAudioThread->quit();
                rxAudioThread->wait();
                rxAudioThread = Q_NULLPTR;
                rxaudio = Q_NULLPTR;
            }

            if (txAudioThread != Q_NULLPTR) {
                qDebug(logUdp()) << "Stopping txaudio thread";
                txAudioThread->quit();
                txAudioThread->wait();
                txAudioThread = Q_NULLPTR;
                txaudio = Q_NULLPTR;
            }            
        }
    }
    else
    {
        alerted = false;
    }
}


void udpAudio::sendTxAudio()
{
    if (txaudio == Q_NULLPTR) {
        return;
    }

}

void udpAudio::receiveAudioData(audioPacket audio) {
    // I really can't see how this could be possible but a quick sanity check!
    if (txaudio == Q_NULLPTR) {
        return;
    }
    if (audio.data.length() > 0) {
        int counter = 1;
        int len = 0;

        while (len < audio.data.length()) {
            QByteArray partial = audio.data.mid(len, 1364);
            audio_packet p;
            memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
            p.len = (quint32)sizeof(p) + partial.length();
            p.sentid = myId;
            p.rcvdid = remoteId;
            if (partial.length() == 0xa0) {
                p.ident = 0x9781;
            }
            else {
                p.ident = 0x0080; // TX audio is always this?
            }
            p.datalen = (quint16)qToBigEndian((quint16)partial.length());
            p.sendseq = (quint16)qToBigEndian((quint16)sendAudioSeq); // THIS IS BIG ENDIAN!
            QByteArray tx = QByteArray::fromRawData((const char*)p.packet, sizeof(p));
            tx.append(partial);
            len = len + partial.length();
            //qInfo(logUdp()) << "Sending audio packet length: " << tx.length();
            sendTrackedPacket(tx);
            sendAudioSeq++;
            counter++;
        }
    }
}

void udpAudio::changeLatency(quint16 value)
{
    emit haveChangeLatency(value);
}

void udpAudio::setVolume(unsigned char value)
{
    emit haveSetVolume(value);
}

void udpAudio::getRxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over) {

    emit haveRxLevels(amplitudePeak, amplitudeRMS, latency, current, under, over);
}

void udpAudio::getTxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over) {
    emit haveTxLevels(amplitudePeak, amplitudeRMS, latency, current, under, over);
}

void udpAudio::dataReceived()
{

    while (udp->hasPendingDatagrams()) {
        QNetworkDatagram datagram = udp->receiveDatagram();
        //qInfo(logUdp()) << "Received: " << datagram.data().mid(0,10);
        QByteArray r = datagram.data();

        switch (r.length())
        {
        case (16): // Response to control packet handled in udpBase
        {
            //control_packet_t in = (control_packet_t)r.constData();
            break;
        }
        default:
        {
            /* Audio packets start as follows:
                    PCM 16bit and PCM8/uLAW stereo: 0x44,0x02 for first packet and 0x6c,0x05 for second.
                    uLAW 8bit/PCM 8bit 0xd8,0x03 for all packets
                    PCM 16bit stereo 0x6c,0x05 first & second 0x70,0x04 third


            */
            control_packet_t in = (control_packet_t)r.constData();

            if (in->type != 0x01 && in->len >= 0x20) {
                if (in->seq == 0)
                {
                    // Seq number has rolled over.
                    seqPrefix++;
                }

                // 0xac is the smallest possible audio packet.
                lastReceived = QTime::currentTime();
                audioPacket tempAudio;
                tempAudio.seq = (quint32)seqPrefix << 16 | in->seq;
                tempAudio.time = lastReceived;
                tempAudio.sent = 0;
                tempAudio.data = r.mid(0x18);
                // Prefer signal/slot to forward audio as it is thread/safe
                // Need to do more testing but latency appears fine.
                //rxaudio->incomingAudio(tempAudio);
                if (rxAudioThread == Q_NULLPTR)
                {
                    startAudio();
                }
                emit haveAudioData(tempAudio);
            }
            break;
        }
        }

        udpBase::dataReceived(r); // Call parent function to process the rest.
        r.clear();
        datagram.clear();
    }
}

void udpAudio::startAudio() {

    if (rxSetup.type == qtAudio) {
        rxaudio = new audioHandler();
    }
    else if (rxSetup.type == portAudio) {
        rxaudio = new paHandler();
    }
    else if (rxSetup.type == rtAudio) {
        rxaudio = new rtHandler();
    }
    else
    {
        qCritical(logAudio()) << "Unsupported Receive Audio Handler selected!";
    }

    rxAudioThread = new QThread(this);
    rxAudioThread->setObjectName("rxAudio()");

    rxaudio->moveToThread(rxAudioThread);

    rxAudioThread->start(QThread::TimeCriticalPriority);

    connect(this, SIGNAL(setupRxAudio(audioSetup)), rxaudio, SLOT(init(audioSetup)));

    // signal/slot not currently used.
    connect(this, SIGNAL(haveAudioData(audioPacket)), rxaudio, SLOT(incomingAudio(audioPacket)));
    connect(this, SIGNAL(haveChangeLatency(quint16)), rxaudio, SLOT(changeLatency(quint16)));
    connect(this, SIGNAL(haveSetVolume(unsigned char)), rxaudio, SLOT(setVolume(unsigned char)));
    connect(rxaudio, SIGNAL(haveLevels(quint16, quint16, quint16, quint16, bool, bool)), this, SLOT(getRxLevels(quint16, quint16, quint16, quint16, bool, bool)));
    connect(rxAudioThread, SIGNAL(finished()), rxaudio, SLOT(deleteLater()));


    sendControl(false, 0x03, 0x00); // First connect packet

    pingTimer = new QTimer();
    connect(pingTimer, &QTimer::timeout, this, &udpBase::sendPing);
    pingTimer->start(PING_PERIOD); // send ping packets every 100ms

    if (enableTx) {
        if (txSetup.type == qtAudio) {
            txaudio = new audioHandler();
        }
        else if (txSetup.type == portAudio) {
            txaudio = new paHandler();
        }
        else if (txSetup.type == rtAudio) {
            txaudio = new rtHandler();
        }
        else
        {
            qCritical(logAudio()) << "Unsupported Transmit Audio Handler selected!";
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
    }

    emit setupRxAudio(rxSetup);

}
