#include "icomudpaudio.h"
#include "logcategories.h"

// Audio stream
icomUdpAudio::icomUdpAudio(QHostAddress local, QHostAddress ip, quint16 audioPort, quint16 lport, audioSetup rxSetup, audioSetup txSetup)
{
    qInfo(logUdp()) << "Starting icomUdpAudio";
    this->localIP = local;
    this->port = audioPort;
    this->radioIP = ip;
    this->rxSetup = rxSetup;
    this->txSetup = txSetup;

    if (txSetup.sampleRate == 0) {
        enableTx = false;
    }

    init(lport); // Perform connection

    QUdpSocket::connect(udp, &QUdpSocket::readyRead, this, &icomUdpAudio::dataReceived);
 
    startAudio();

    watchdogTimer = new QTimer(this);
    connect(watchdogTimer, &QTimer::timeout, this, &icomUdpAudio::watchdog);
    watchdogTimer->start(WATCHDOG_PERIOD);

    areYouThereTimer = new QTimer(this);
    connect(areYouThereTimer, &QTimer::timeout, this, std::bind(&icomUdpBase::sendControl, this, false, 0x03, 0));
    areYouThereTimer->start(AREYOUTHERE_PERIOD);
}

icomUdpAudio::~icomUdpAudio()
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
    qDebug(logUdp()) << "icomUdpHandler successfully closed";
}

void icomUdpAudio::watchdog()
{
    static bool alerted = false;
    if (lastReceived.msecsTo(QTime::currentTime()) > 30000)
    {
        if (!alerted) {
            /* Just log it at the moment, maybe try signalling the control channel that it needs to
                try requesting civ/audio again? */

            qInfo(logUdp()) << " Audio Watchdog: no audio data received for 30s, restart required? last:" << lastReceived  ;
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


void icomUdpAudio::sendTxAudio()
{
    if (txaudio == Q_NULLPTR) {
        return;
    }

}

void icomUdpAudio::receiveAudioData(audioPacket audio) {
    // I really can't see how this could be possible but a quick sanity check!
    if (txaudio == Q_NULLPTR) {
        return;
    }
    if (audio.data.length() > 0) {
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
        }
    }
}

void icomUdpAudio::changeLatency(quint16 value)
{
    emit haveChangeLatency(value);
}

void icomUdpAudio::setVolume(quint8 value)
{
    emit haveSetVolume(value);
}

void icomUdpAudio::getRxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over) {

    emit haveRxLevels(amplitudePeak, amplitudeRMS, latency, current, under, over);
}

void icomUdpAudio::getTxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over) {
    emit haveTxLevels(amplitudePeak, amplitudeRMS, latency, current, under, over);
}

void icomUdpAudio::dataReceived()
{

    while (udp->hasPendingDatagrams()) {
        QNetworkDatagram datagram = udp->receiveDatagram();
        //qInfo(logUdp()) << "Received: " << datagram.data().mid(0,10);
        QByteArray r = datagram.data();

        switch (r.length())
        {
        case (16): // Response to control packet handled in icomUdpBase
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
            static int latencyCounter = 0;

            if (in->type != 0x01 && in->len >= 0x20) {
                if (in->seq == 0)
                {
                    // Seq number has rolled over.
                    seqPrefix++;
                }

                if (timeDifference < rxSetup.latency)
                {
                    if (rxAudioThread == Q_NULLPTR)
                    {
                        startAudio();
                    }

                    audioPacket tempAudio;
                    tempAudio.seq = (quint32)seqPrefix << 16 | in->seq;
                    tempAudio.time = QTime::currentTime().addMSecs(timeDifference);;
                    tempAudio.sent = 0;
                    tempAudio.data = r.mid(0x18);
                    emit haveAudioData(tempAudio);
                }

                if (abs(timeDifference) > rxSetup.latency) {
                    qDebug(logUdp()) << "Audio timestamp is" << rxSetup.latency << "ms, away";
                    latencyCounter++;
                }

                if (latencyCounter > 5)
                {
                    radioTime = QTime(); // Invalidate radioTime to force it to be reset.
                    latencyCounter=0;
                }
                lastReceived=QTime::currentTime();
            }
            break;
        }
        }

        icomUdpBase::dataReceived(r); // Call parent function to process the rest.
        r.clear();
        datagram.clear();
    }
}

void icomUdpAudio::startAudio() {

    if (rxSetup.type == qtAudio) {
        rxaudio = new audioHandlerQtOutput();
    }
    else if (rxSetup.type == portAudio) {
        rxaudio = new audioHandlerPaOutput();
    }
    else if (rxSetup.type == rtAudio) {
        rxaudio = new audioHandlerRtOutput();
    }
#ifndef BUILD_WFSERVER
    else if (rxSetup.type == tciAudio) {
        rxaudio = new audioHandlerTciOutput();
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

    // signal/slot not currently used.
    connect(this, SIGNAL(haveAudioData(audioPacket)), rxaudio, SLOT(incomingAudio(audioPacket)));
    connect(this, SIGNAL(haveChangeLatency(quint16)), rxaudio, SLOT(changeLatency(quint16)));
    connect(this, SIGNAL(haveSetVolume(quint8)), rxaudio, SLOT(setVolume(quint8)));
    connect(rxaudio, SIGNAL(haveLevels(quint16, quint16, quint16, quint16, bool, bool)), this, SLOT(getRxLevels(quint16, quint16, quint16, quint16, bool, bool)));
    connect(rxAudioThread, SIGNAL(finished()), rxaudio, SLOT(deleteLater()));

    connect(rxAudioThread, SIGNAL(finished()), rxaudio, SLOT(deleteLater()));

    sendControl(false, 0x03, 0x00); // First connect packet

    pingTimer = new QTimer(this);
    connect(pingTimer, &QTimer::timeout, this, &icomUdpBase::sendPing);
    pingTimer->start(PING_PERIOD); // send ping packets every 100ms

    if (enableTx) {
        if (txSetup.type == qtAudio) {
            txaudio = new audioHandlerQtInput();
        }
        else if (txSetup.type == portAudio) {
            txaudio = new audioHandlerPaInput();
        }
        else if (txSetup.type == rtAudio) {
            txaudio = new audioHandlerRtInput();
        }
#ifndef BUILD_WFSERVER
        else if (txSetup.type == tciAudio) {
            txaudio = new audioHandlerTciInput();
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
    }

    emit setupRxAudio(rxSetup);

}

