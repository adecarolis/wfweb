#include "rtpaudio.h"
#include "logcategories.h"

// Audio stream
rtpAudio::rtpAudio(QString ip, quint16 port, audioSetup rxSetup, audioSetup txSetup, QObject* parent)
    : QObject{parent}, rxSetup(rxSetup), txSetup(txSetup), port(port)
{
    qInfo(logUdp()) << "Starting rtpAudio";

    if (txSetup.sampleRate == 0) {
        enableTx = false;
    } else {
        this->ip = QHostAddress(ip);
    }
}

rtpAudio::~rtpAudio()
{
    if (udp != Q_NULLPTR)
    {
        qDebug(logUdp()) << "Closing RTP connection";
        udp->close();
        delete udp;
        udp = Q_NULLPTR;
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
    debugFile.close();
}

void rtpAudio::init()
{
    udp = new QUdpSocket(this);

    if (!udp->bind(port))
    {
        // We couldn't bind to the selected port.
        qCritical(logUdp()) << "**** Unable to bind to RTP port" << port << "Cannot continue! ****";
        return;
    }
    else
    {
        QUdpSocket::connect(udp, &QUdpSocket::readyRead, this, &rtpAudio::dataReceived);
        qInfo(logUdp()) << "RTP Stream bound to local port:" << udp->localPort() << " remote port:" << port;
    }


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
#ifndef BUILD_WFSERVER
        else if (txSetup.type == tciAudio) {
            txaudio = new tciAudioHandler();
        }
#endif
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

void rtpAudio::dataReceived()
{
    QHostAddress sender;
    quint16 port;
    QByteArray d(udp->pendingDatagramSize(),0x0);
    udp->readDatagram(d.data(), d.size(), &sender, &port);
    rtp_header in;
    memcpy(in.packet,d.mid(0,12).constData(),sizeof(rtp_header));
    //qInfo(logUdp()) << "RX: version:" << in.version << "type:" << in.payloadType << "len" << d.size() << "seq" << qFromBigEndian(in.seq) << "header" << sizeof(rtp_header) << "hex" << d.mid(0,12).toHex(' ');
    if (in.payloadType == 96) {
        // We have audio data
        audioPacket tempAudio;
        tempAudio.seq = qFromBigEndian(in.seq);
        tempAudio.time = QTime::currentTime().addMSecs(0);
        tempAudio.sent = 0;
        tempAudio.data = d.mid(12);
        emit haveAudioData(tempAudio);
        packetCount++;
        size = size + tempAudio.data.size();
    }
}

void rtpAudio::changeLatency(quint16 value)
{
    emit haveChangeLatency(value);
}

void rtpAudio::setVolume(quint8 value)
{
    emit haveSetVolume(value);
}

void rtpAudio::getRxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over)
{
    emit haveRxLevels(amplitudePeak, amplitudeRMS, latency, current, under, over);
}

void rtpAudio::getTxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over)
{
    emit haveTxLevels(amplitudePeak, amplitudeRMS, latency, current, under, over);
}

void rtpAudio::receiveAudioData(audioPacket audio)
{
    // I really can't see how this could be possible but a quick sanity check!
    if (txaudio == Q_NULLPTR) {
        return;
    }
    if (audio.data.length() > 0) {
        int len = 0;

        while (len < audio.data.length()) {
            QByteArray partial = audio.data.mid(len, 640);
            rtp_header p;
            memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
            p.version = 2;
            p.padding = 0;
            p.extension = 0;
            p.csrc = 0;
            p.marker = 0;
            p.payloadType=96;
            p.seq = qToBigEndian(seq++);
            p.timestamp = 0;
            p.ssrc = quint32(0x00) << 24 | quint32(0x30) << 16 | quint32(0x39) << 8 | (quint32(0x38) & 0xff);
            QByteArray tx = QByteArray::fromRawData((const char*)p.packet, sizeof(p));
            tx.append(partial);
            len = len + partial.length();
            //qInfo(logUdp()) << "TX: " << tx.length() << "to" << ip.toString() << "port" << port << "ver" << p.version << "type" << p.payloadType << "seq" << p.seq << "ssrc" << QString::number(p.ssrc,16);
            udp->writeDatagram(tx, ip, port);
        }
    }
}
