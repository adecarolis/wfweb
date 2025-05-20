#include "rtpaudio.h"
#include "logcategories.h"

// Audio stream
rtpAudio::rtpAudio(QString ip, quint16 port, audioSetup outSetup, audioSetup inSetup, QObject* parent)
    : QObject{parent}, outSetup(outSetup), inSetup(inSetup), port(port)
{
    qInfo(logUdp()) << "Starting rtpAudio";

    if (inSetup.sampleRate == 0) {
        enableIn = false;
    } else {
        this->ip = QHostAddress(ip);
    }

    if (outSetup.sampleRate == 0) {
        enableOut = false;
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
    if (outAudioThread != Q_NULLPTR) {
        qDebug(logUdp()) << "Stopping outaudio thread";
        outAudioThread->quit();
        outAudioThread->wait();
    }

    if (inAudioThread != Q_NULLPTR) {
        qDebug(logUdp()) << "Stopping inaudio thread";
        inAudioThread->quit();
        inAudioThread->wait();
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

    if (enableOut)
    {
        if (outSetup.type == qtAudio) {
            outaudio = new audioHandler();
        }
        else if (outSetup.type == portAudio) {
            outaudio = new paHandler();
        }
        else if (outSetup.type == rtAudio) {
            outaudio = new rtHandler();
        }
#ifndef BUILD_WFSERVER
        else if (outSetup.type == tciAudio) {
            outaudio = new tciAudioHandler();
        }
#endif
        else
        {
            qCritical(logAudio()) << "Unsupported Receive Audio Handler selected!";
        }

        outAudioThread = new QThread(this);
        outAudioThread->setObjectName("outAudio()");

        outaudio->moveToThread(outAudioThread);

        connect(this, SIGNAL(setupOutAudio(audioSetup)), outaudio, SLOT(init(audioSetup)));

        // signal/slot not currently used.
        connect(this, SIGNAL(haveAudioData(audioPacket)), outaudio, SLOT(incomingAudio(audioPacket)));
        connect(this, SIGNAL(haveChangeLatency(quint16)), outaudio, SLOT(changeLatency(quint16)));
        connect(this, SIGNAL(haveSetVolume(quint8)), outaudio, SLOT(setVolume(quint8)));
        connect(outaudio, SIGNAL(haveLevels(quint16, quint16, quint16, quint16, bool, bool)), this, SLOT(getOutLevels(quint16, quint16, quint16, quint16, bool, bool)));
        connect(outAudioThread, SIGNAL(finished()), outaudio, SLOT(deleteLater()));

        outAudioThread->start(QThread::TimeCriticalPriority);

        emit setupOutAudio(outSetup);
    }

    if (enableIn) {
        if (inSetup.type == qtAudio) {
            inaudio = new audioHandler();
        }
        else if (inSetup.type == portAudio) {
            inaudio = new paHandler();
        }
        else if (inSetup.type == rtAudio) {
            inaudio = new rtHandler();
        }
#ifndef BUILD_WFSERVER
        else if (inSetup.type == tciAudio) {
            inaudio = new tciAudioHandler();
        }
#endif
        else
        {
            qCritical(logAudio()) << "Unsupported Transmit Audio Handler selected!";
        }

        inAudioThread = new QThread(this);
        inAudioThread->setObjectName("inAudio()");

        inaudio->moveToThread(inAudioThread);

        connect(this, SIGNAL(setupInAudio(audioSetup)), inaudio, SLOT(init(audioSetup)));
        connect(inaudio, SIGNAL(haveAudioData(audioPacket)), this, SLOT(receiveAudioData(audioPacket)));
        connect(inaudio, SIGNAL(haveLevels(quint16, quint16, quint16, quint16, bool, bool)), this, SLOT(getInLevels(quint16, quint16, quint16, quint16, bool, bool)));

        connect(inAudioThread, SIGNAL(finished()), inaudio, SLOT(deleteLater()));

        inAudioThread->start(QThread::TimeCriticalPriority);

        emit setupInAudio(inSetup);
    }
}

void rtpAudio::dataReceived()
{
    QHostAddress sender;
    quint16 port;
    QByteArray d(udp->pendingDatagramSize(),0x0);
    udp->readDatagram(d.data(), d.size(), &sender, &port);
    rtp_header in;
    memcpy(in.packet,d.mid(0,12).constData(),sizeof(rtp_header));
    //qInfo(logUdp()) << "Audio out: version:" << in.version << "type:" << in.payloadType << "len" << d.size() << "seq" << qFromBigEndian(in.seq) << "header" << sizeof(rtp_header) << "hex" << d.mid(0,12).toHex(' ');
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

void rtpAudio::getOutLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over)
{
    emit haveOutLevels(amplitudePeak, amplitudeRMS, latency, current, under, over);
}

void rtpAudio::getInLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over)
{
    emit haveInLevels(amplitudePeak, amplitudeRMS, latency, current, under, over);
}

void rtpAudio::receiveAudioData(audioPacket audio)
{
    // I really can't see how this could be possible but a quick sanity check!
    if (inaudio == Q_NULLPTR) {
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
            QByteArray in = QByteArray::fromRawData((const char*)p.packet, sizeof(p));
            in.append(partial);
            len = len + partial.length();
            //qInfo(logUdp()) << "Audio In: " << in.length() << "to" << ip.toString() << "port" << port << "ver" << p.version << "type" << p.payloadType << "seq" << p.seq << "ssrc" << QString::number(p.ssrc,16);
            udp->writeDatagram(in, ip, port);
        }
    }
}
