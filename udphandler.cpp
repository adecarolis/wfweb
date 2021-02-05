// Copyright 2021 Phil Taylor M0VSE
// This code is heavily based on "Kappanhang" by HA2NON, ES1AKOS and W6EL!

#include "udphandler.h"


udpHandler::udpHandler(QHostAddress ip, int cport, int sport, int aport,QString username, QString password)
{
    qDebug() << "Starting udpHandler";
    radioIP = ip;
    this->port = cport;
    this->aport = aport;
    this->sport = sport;
    this->username = username;
    this->password = password;
    
    // Convoluted way to find the external IP address, there must be a better way????
    QString localhostname = QHostInfo::localHostName();
    QList<QHostAddress> hostList = QHostInfo::fromName(localhostname).addresses();
    foreach(const QHostAddress & address, hostList) 
    {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && address.isLoopback() == false) 
        {
            localIP = QHostAddress(address.toString());
        }
    }

    init(); // Perform connection
    QUdpSocket::connect(udp, &QUdpSocket::readyRead, this, &udpHandler::DataReceived);

    connect(&reauthTimer, &QTimer::timeout, this, QOverload<>::of(&udpHandler::ReAuth));

    udpBase::SendPacketConnect(); // First connect packet

}

udpHandler::~udpHandler()
{
    if (isAuthenticated)
    {
        if (audio != nullptr)
            delete audio;
        if (serial != nullptr)
            delete serial;

        qDebug() << "Sending De-Auth packet to radio";
        SendPacketAuth(0x01);

    }
}

void udpHandler::ReAuth()
{
    qDebug() << "Performing ReAuth";
    SendPacketAuth(0x05);
}

void udpHandler::receiveFromSerialStream(QByteArray data)
{
    emit haveDataFromPort(data);
}

void udpHandler::receiveDataFromUserToRig(QByteArray data)
{
    if (serial != nullptr)
        serial->Send(data);
}

void udpHandler::DataReceived()
{
    while (udp->hasPendingDatagrams()) {
        lastReceived = time(0);
        QNetworkDatagram datagram = udp->receiveDatagram();
        //qDebug() << "Received: " << datagram.data();
        QByteArray r = datagram.data();

        switch (r.length())
        {
        case (16): // Response to pkt0
            if (r.mid(0, 8) == QByteArrayLiteral("\x10\x00\x00\x00\x06\x00\x01\x00"))
            {
                // Update remoteSID
                if (!sentPacketLogin) {
                    remoteSID = qFromBigEndian<quint32>(r.mid(8, 4));
                    SendPacketLogin(); // second login packet
                    sentPacketLogin = true;
                }
            }
            break;
        case (64): // Response to Auth packet?
            if (r.mid(0, 6) == QByteArrayLiteral("\x40\x00\x00\x00\x00\x00"))
            {
                if (r[21] == (char)0x05)
                {
                    // Request serial and audio!
                    gotAuthOK = true;
                    if (!serialAndAudioOpened)
                        SendRequestSerialAndAudio();
                }
            }
            break;
        case (80):  // Status packet
            if (r.mid(0, 6) == QByteArrayLiteral("\x50\x00\x00\x00\x00\x00"))
            {
                if (r.mid(48, 3) == QByteArrayLiteral("\xff\xff\xff"))
                    if (!serialAndAudioOpened) {
                        emit haveNetworkError(radioIP.toString(), "Auth failed, try rebooting the radio.");
                        qDebug() << "Auth failed, try rebooting the radio.";
                    }
                if (r.mid(48, 3) == QByteArrayLiteral("\x00\x00\x00") && r[64] == (char)0x01) {
                    emit haveNetworkError(radioIP.toString(), "Got radio disconnected.");
                    qDebug() << "Got radio disconnected.";
                }
            }
            break;

        case(96): // Response to Login packet.
            if (r.mid(0, 6) == QByteArrayLiteral("\x60\x00\x00\x00\x00\x00"))
                //if (r.mid(0, 8) == QByteArrayLiteral("\x60\x00\x00\x00\x00\x00\x01\x00"))
            {
                if (r.mid(48, 4) == QByteArrayLiteral("\xff\xff\xff\xfe"))
                {
                    emit haveNetworkError(radioIP.toString(), "Invalid Username/Password");
                    qDebug() << "Invalid Username/Password";

                }
                else if (!isAuthenticated)
                {
                    emit haveNetworkError(radioIP.toString(), "Radio Login OK!");
                    qDebug() << "Login OK!";

                    authID[0] = r[26];
                    authID[1] = r[27];
                    authID[2] = r[28];
                    authID[3] = r[29];
                    authID[4] = r[30];
                    authID[5] = r[31];

                    pkt7Timer = new QTimer(this);
                    connect(pkt7Timer, &QTimer::timeout, this, &udpBase::SendPkt7Idle);
                    pkt7Timer->start(3000); // send pkt7 idle packets every 3 seconds

                    SendPacketAuth(0x02);

                    pkt0Timer = new QTimer(this);
                    connect(pkt0Timer, &QTimer::timeout, this, std::bind(&udpBase::SendPkt0Idle,this,true,0));
                    pkt0Timer->start(100);

                    SendPacketAuth(0x05);

                    reauthTimer.start(reauthInterval);

                    isAuthenticated = true;
                }

            }
            break;
        case (144):
            if (!serialAndAudioOpened && r.mid(0, 6) == QByteArrayLiteral("\x90\x00\x00\x00\x00\x00") && r[96] == (char)0x01) {
                devname = parseNullTerminatedString(r, 64);
                qDebug() << "Got serial and audio request success, device name: " << devname;

                // Stuff can change in the meantime because of a previous login...
                remoteSID = qFromBigEndian<quint32>(r.mid(8, 4));
                localSID = qFromBigEndian<quint32>(r.mid(12, 4));
                authID[0] = r[26];
                authID[1] = r[27];
                authID[2] = r[28];
                authID[3] = r[29];
                authID[4] = r[30];
                authID[5] = r[31];

                serial = new udpSerial(localIP, radioIP, sport);
                audio = new udpAudio(localIP, radioIP, aport);

                QObject::connect(serial, SIGNAL(Receive(QByteArray)), this, SLOT(receiveFromSerialStream(QByteArray)));

                serialAndAudioOpened = true;
            }
            break;

        case (168):

            if (r.mid(0, 6) == QByteArrayLiteral("\xa8\x00\x00\x00\x00\x00")) {
                a8replyID[0] = r[66];
                a8replyID[1] = r[67];
                a8replyID[2] = r[68];
                a8replyID[3] = r[69];
                a8replyID[4] = r[70];
                a8replyID[5] = r[71];
                a8replyID[6] = r[72];
                a8replyID[7] = r[73];
                a8replyID[8] = r[74];
                a8replyID[9] = r[75];
                a8replyID[10] = r[76];
                a8replyID[11] = r[77];
                a8replyID[12] = r[78];
                a8replyID[13] = r[79];
                a8replyID[14] = r[80];
                a8replyID[15] = r[81];
                gotA8ReplyID = true;
            }
            break;
        }
        udpBase::DataReceived(r); // Call parent function to process the rest.
        r.clear();
        datagram.clear();

    }

    return;
}



qint64 udpHandler::SendRequestSerialAndAudio()
{

    unsigned char* usernameEncoded = Passcode(username);
    int txSeqBufLengthMs = 100;
    int audioSampleRate = 48000;
    int udpSerialPort = 50002;
    int udpAudioPort = 50003;

    const unsigned char p[] = {
        0x90, 0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00,
        static_cast<unsigned char>(localSID >> 24 & 0xff), static_cast<unsigned char>(localSID >> 16 & 0xff), static_cast<unsigned char>(localSID >> 8 & 0xff), static_cast<unsigned char>(localSID & 0xff),
        static_cast<unsigned char>(remoteSID >> 24 & 0xff), static_cast<unsigned char>(remoteSID >> 16 & 0xff), static_cast<unsigned char>(remoteSID >> 8 & 0xff), static_cast<unsigned char>(remoteSID & 0xff),
        0x00, 0x00, 0x00, 0x80, 0x01, 0x03, 0x00, static_cast<unsigned char>(authInnerSendSeq & 0xff), static_cast<const unsigned char>(authInnerSendSeq >> 8 & 0xff),
        0x00, static_cast<unsigned char>(authID[0]), static_cast<unsigned char>(authID[1]), static_cast<unsigned char>(authID[2]),
        static_cast<unsigned char>(authID[3]), static_cast<unsigned char>(authID[4]), static_cast<unsigned char>(authID[5]),
        static_cast<unsigned char>(a8replyID[0]), static_cast<unsigned char>(a8replyID[1]), static_cast<unsigned char>(a8replyID[2]), static_cast<unsigned char>(a8replyID[3]),
        static_cast<unsigned char>(a8replyID[4]), static_cast<unsigned char>(a8replyID[5]), static_cast<unsigned char>(a8replyID[6]), static_cast<unsigned char>(a8replyID[7]),
        static_cast<unsigned char>(a8replyID[8]), static_cast<unsigned char>(a8replyID[9]), static_cast<unsigned char>(a8replyID[10]), static_cast<unsigned char>(a8replyID[11]),
        static_cast<unsigned char>(a8replyID[12]), static_cast<unsigned char>(a8replyID[13]), static_cast<unsigned char>(a8replyID[14]), static_cast<unsigned char>(a8replyID[15]),
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x49, 0x43, 0x2d, 0x37, 0x30, 0x35, 0x00, 0x00, // IC-705 in plain text
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        usernameEncoded[0], usernameEncoded[1], usernameEncoded[2], usernameEncoded[3],
        usernameEncoded[4], usernameEncoded[5], usernameEncoded[6], usernameEncoded[7],
        usernameEncoded[8], usernameEncoded[9], usernameEncoded[10], usernameEncoded[11],
        usernameEncoded[12], usernameEncoded[13], usernameEncoded[14], usernameEncoded[15],
        0x01, 0x01, 0x04, 0x04, 0x00, 0x00, static_cast<unsigned char>(audioSampleRate >> 8 & 0xff), static_cast<unsigned char>(audioSampleRate & 0xff),
        0x00, 0x00, static_cast<unsigned char>(audioSampleRate >> 8 & 0xff), static_cast<unsigned char>(audioSampleRate & 0xff),
        0x00, 0x00, static_cast<unsigned char>(udpSerialPort >> 8 & 0xff), static_cast<unsigned char>(udpSerialPort & 0xff),
        0x00, 0x00, static_cast<unsigned char>(udpAudioPort >> 8 & 0xff), static_cast<unsigned char>(udpAudioPort & 0xff), 0x00, 0x00,
        static_cast<unsigned char>(txSeqBufLengthMs >> 8 & 0xff), static_cast<unsigned char>(txSeqBufLengthMs & 0xff), 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    authInnerSendSeq++;

    delete[] usernameEncoded;

    return SendTrackedPacket(QByteArray::fromRawData((const char*)p, sizeof(p)));
}


qint64 udpHandler::SendPacketLogin() // Only used on control stream.
{

    uint16_t authStartID = rand() | rand() << 8;
    unsigned char* usernameEncoded = Passcode(username);
    unsigned char* passwordEncoded = Passcode(password);

    const unsigned char p[] = {
        0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        static_cast<unsigned char>(localSID >> 24 & 0xff), static_cast<unsigned char>(localSID >> 16 & 0xff), static_cast<unsigned char>(localSID >> 8 & 0xff), static_cast<unsigned char>(localSID & 0xff),
        static_cast<unsigned char>(remoteSID >> 24 & 0xff), static_cast<unsigned char>(remoteSID >> 16 & 0xff), static_cast<unsigned char>(remoteSID >> 8 & 0xff), static_cast<unsigned char>(remoteSID & 0xff),
        0x00, 0x00, 0x00, 0x70, 0x01, 0x00, 0x00, static_cast<unsigned char>(authInnerSendSeq & 0xff), static_cast<unsigned char>(authInnerSendSeq >> 8 & 0xff),
        0x00, static_cast<unsigned char>(authStartID & 0xff), static_cast<unsigned char>(authStartID >> 8 & 0xff), 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        usernameEncoded[0], usernameEncoded[1], usernameEncoded[2], usernameEncoded[3],
        usernameEncoded[4], usernameEncoded[5], usernameEncoded[6], usernameEncoded[7],
        usernameEncoded[8], usernameEncoded[9], usernameEncoded[10], usernameEncoded[11],
        usernameEncoded[12], usernameEncoded[13], usernameEncoded[14], usernameEncoded[15],
        passwordEncoded[0], passwordEncoded[1], passwordEncoded[2], passwordEncoded[3],
        passwordEncoded[4], passwordEncoded[5], passwordEncoded[6], passwordEncoded[7],
        passwordEncoded[8], passwordEncoded[9], passwordEncoded[10], passwordEncoded[11],
        passwordEncoded[12], passwordEncoded[13], passwordEncoded[14], passwordEncoded[15],
        0x69, 0x63, 0x6f, 0x6d, 0x2d, 0x70, 0x63, 0x00, // icom-pc in plain text
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };



    delete[] usernameEncoded;
    delete[] passwordEncoded;

    authInnerSendSeq++;
    return SendTrackedPacket(QByteArray::fromRawData((const char*)p, sizeof(p)));
}


qint64 udpHandler::SendPacketAuth(uint8_t magic)
{

    const unsigned char p[] = {
        0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x00,
        static_cast<unsigned char>(localSID >> 24 & 0xff), static_cast<unsigned char>(localSID >> 16 & 0xff), static_cast<unsigned char>(localSID >> 8 & 0xff), static_cast<unsigned char>(localSID & 0xff),
        static_cast<unsigned char>(remoteSID >> 24 & 0xff), static_cast<unsigned char>(remoteSID >> 16 & 0xff), static_cast<unsigned char>(remoteSID >> 8 & 0xff), static_cast<unsigned char>(remoteSID & 0xff),
        0x00, 0x00, 0x00, 0x30, 0x01, static_cast<unsigned char>(magic), 0x00, static_cast<unsigned char>(authInnerSendSeq & 0xff), static_cast<unsigned char>((authInnerSendSeq) >> 8 & 0xff), 0x00,
        static_cast<unsigned char>(authID[0]), static_cast<unsigned char>(authID[1]), static_cast<unsigned char>(authID[2]), 
        static_cast<unsigned char>(authID[3]), static_cast<unsigned char>(authID[4]), static_cast<unsigned char>(authID[5]),
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    authInnerSendSeq++;
    return SendTrackedPacket(QByteArray::fromRawData((const char *)p, sizeof(p)));
}


// (pseudo) serial class
udpSerial::udpSerial(QHostAddress local, QHostAddress ip, int sport) {
    qDebug() << "Starting udpSerial";
    localIP = local;
    port = sport;
    radioIP = ip;

    init(); // Perform connection

    QUdpSocket::connect(udp, &QUdpSocket::readyRead, this, &udpSerial::DataReceived);
    SendPacketConnect(); // First connect packet
}


int udpSerial::Send(QByteArray d)
{
    // qDebug() << "Sending: (" << d.length() << ") " << d;

    uint16_t l = d.length();
    const unsigned char p[] = { static_cast<unsigned char>(0x15 + l), 0x00, 0x00, 0x00, 0x00, 0x00,0x00,0x00,
        static_cast<unsigned char>(localSID >> 24 & 0xff), static_cast<unsigned char>(localSID >> 16 & 0xff), static_cast<unsigned char>(localSID >> 8 & 0xff), static_cast<unsigned char>(localSID & 0xff),
        static_cast<unsigned char>(remoteSID >> 24 & 0xff), static_cast<unsigned char>(remoteSID >> 16 & 0xff), static_cast<unsigned char>(remoteSID >> 8 & 0xff), static_cast<unsigned char>(remoteSID & 0xff),
        0xc1, static_cast<unsigned char>(l), 0x00, static_cast<unsigned char>(sendSeqB >> 8 & 0xff),static_cast<unsigned char>(sendSeqB & 0xff)
    };
    QByteArray t = QByteArray::fromRawData((const char*)p, sizeof(p));
    t.append(d);
    SendTrackedPacket(t);
    sendSeqB++;
    return 1;
}



void udpSerial::SendIdle()
{
    const unsigned char p[] = { 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        static_cast<unsigned char>(localSID >> 24 & 0xff), static_cast<unsigned char>(localSID >> 16 & 0xff), static_cast<unsigned char>(localSID >> 8 & 0xff), static_cast<unsigned char>(localSID & 0xff),
        static_cast<unsigned char>(remoteSID >> 24 & 0xff), static_cast<unsigned char>(remoteSID >> 16 & 0xff), static_cast<unsigned char>(remoteSID >> 8 & 0xff), static_cast<unsigned char>(remoteSID & 0xff)
    };

    SendTrackedPacket(QByteArray::fromRawData((const char*)p, sizeof(p)));
}

void udpSerial::SendPeriodic()
{
    const unsigned char p[] = { 0x15, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
        static_cast<unsigned char>(localSID >> 24 & 0xff), static_cast<unsigned char>(localSID >> 16 & 0xff), static_cast<unsigned char>(localSID >> 8 & 0xff), static_cast<unsigned char>(localSID & 0xff),
        static_cast<unsigned char>(remoteSID >> 24 & 0xff), static_cast<unsigned char>(remoteSID >> 16 & 0xff), static_cast<unsigned char>(remoteSID >> 8 & 0xff), static_cast<unsigned char>(remoteSID & 0xff)
    };

    SendTrackedPacket(QByteArray::fromRawData((const char*)p, sizeof(p)));

}

qint64 udpSerial::SendPacketOpenClose(bool close)
{
    uint8_t magic = 0x05;

    if (close)
        magic = 0x00;

    const unsigned char p[] = {
        0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        static_cast<unsigned char>(localSID >> 24 & 0xff), static_cast<unsigned char>(localSID >> 16 & 0xff), static_cast<unsigned char>(localSID >> 8 & 0xff), static_cast<unsigned char>(localSID & 0xff),
        static_cast<unsigned char>(remoteSID >> 24 & 0xff), static_cast<unsigned char>(remoteSID >> 16 & 0xff), static_cast<unsigned char>(remoteSID >> 8 & 0xff), static_cast<unsigned char>(remoteSID & 0xff),
        0xc0, 0x01, 0x00, static_cast<const unsigned char>(sendSeqB >> 8 & 0xff), static_cast<const unsigned char>(sendSeqB & 0xff), magic
    };

    sendSeqB++;

    return SendTrackedPacket(QByteArray::fromRawData((const char*)p, sizeof(p)));
}



void udpSerial::DataReceived()
{
    while (udp->hasPendingDatagrams()) {
        QNetworkDatagram datagram = udp->receiveDatagram();
        //qDebug() << "Received: " << datagram.data();
        QByteArray r = datagram.data();

        switch (r.length())
        {
        case (16): // Response to pkt0
            if (r.mid(0, 8) == QByteArrayLiteral("\x10\x00\x00\x00\x06\x00\x01\x00"))
            {
                // Update remoteSID
                remoteSID = qFromBigEndian<quint32>(r.mid(8, 4));

                if (!periodicRunning) {
                    SendPacketOpenClose(false); // First connect packet
                    pkt7Timer = new QTimer(this);
                    connect(pkt7Timer, &QTimer::timeout, this, &udpBase::SendPkt7Idle);
                    pkt7Timer->start(3000); // send pkt7 idle packets every 3 seconds

                    pkt0Timer = new QTimer(this);
                    connect(pkt0Timer, &QTimer::timeout, this, std::bind(&udpBase::SendPkt0Idle,this,true,0));
                    pkt0Timer->start(100);

                    periodicRunning = true;

                }

            }
            break;
        default:
            if (r.length() > 21) {
                // We should probably check for missing packets?
                //uint16_t gotSeq = qFromLittleEndian<quint16>(r.mid(6, 2));
                quint8 temp = r[0] - 0x15;
                
                if ((quint8)r[16] == 0xc1 && (quint8)r[17] == temp)
                    emit Receive(r.mid(21)); 
                    
            }
            break;

        }
        udpBase::DataReceived(r); // Call parent function to process the rest.
        r.clear();
        datagram.clear();

    }
}


// Audio stream
udpAudio::udpAudio(QHostAddress local, QHostAddress ip, int aport)
{
    qDebug() << "Starting udpAudio";
    localIP = local;
    port = aport;
    radioIP = ip;

    init(); // Perform connection

    QUdpSocket::connect(udp, &QUdpSocket::readyRead, this, &udpAudio::DataReceived);
    SendPacketConnect(); // First connect packet


    // Init audio
    format.setSampleRate(48000);
    format.setChannelCount(1);
    format.setSampleSize(16);
    format.setCodec("audio/pcm");
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::SignedInt);
    QAudioDeviceInfo info(QAudioDeviceInfo::defaultOutputDevice());
    if (info.isFormatSupported(format))
    {
        qDebug() << "Audio format supported";
    }
    else
    {
        qDebug() << "Audio format not supported!";
        if(info.isNull())
        {
            qDebug() << "No device was found. You probably need to install libqt5multimedia5-plugins.";
        } else {

            qDebug() << "Devices found: ";
            const auto deviceInfos = QAudioDeviceInfo::availableDevices(QAudio::AudioOutput);
            for (const QAudioDeviceInfo &deviceInfo : deviceInfos)
            {
                qDebug() << "Device name: " << deviceInfo.deviceName();
                qDebug() << deviceInfo.deviceName();
                qDebug() << "is null (probably not good):";
                qDebug() << deviceInfo.isNull();
                qDebug() << "channel count:";
                qDebug() << deviceInfo.supportedChannelCounts();
                qDebug() << "byte order:";
                qDebug() << deviceInfo.supportedByteOrders();
                qDebug() << "supported codecs:";
                qDebug() << deviceInfo.supportedCodecs();
                qDebug() << "sample rates:";
                qDebug() << deviceInfo.supportedSampleRates();
                qDebug() << "sample sizes:";
                qDebug() << deviceInfo.supportedSampleSizes();
                qDebug() << "sample types:";
                qDebug() << deviceInfo.supportedSampleTypes();
            }
        }
        qDebug() << "----- done with audio info -----";
    }

    buffer = new QBuffer();
    buffer->open(QIODevice::ReadWrite);
    audio = new QAudioOutput(format);
    audio->setBufferSize(10000); // TODO: add preference, maybe UI too. 20210205: connection was wifi --> cellular --> internet --> rig
    buffer->seek(0);
    audio->start(buffer);

}


void udpAudio::DataReceived()
{
    while (udp->hasPendingDatagrams()) {
        QNetworkDatagram datagram = udp->receiveDatagram();
        //qDebug() << "Received: " << datagram.data();
        QByteArray r = datagram.data();

        switch (r.length())
        {
        case (16): // Response to pkt0
            if (r.mid(0, 8) == QByteArrayLiteral("\x10\x00\x00\x00\x06\x00\x01\x00"))
            {
                // Update remoteSID in case it has changed.
                remoteSID = qFromBigEndian<quint32>(r.mid(8, 4));
                if (!periodicRunning) {
                    periodicRunning = true;
                    pkt7Timer = new QTimer(this);
                    connect(pkt7Timer, &QTimer::timeout, this, &udpBase::SendPkt7Idle);
                    pkt7Timer->start(3000); // send pkt7 idle packets every 3 seconds
                }
            }
            break;

        default:
            if (r.length() >= 580 && (r.mid(0, 6) == QByteArrayLiteral("\x6c\x05\x00\x00\x00\x00") || r.mid(0, 6) == QByteArrayLiteral("\x44\x02\x00\x00\x00\x00")))
            {
                // This is an audio packet!
                uint16_t gotSeq = qFromLittleEndian<quint16>(r.mid(6, 2));

                if (lastSeq > 0 && gotSeq > lastSeq + 1)
                {
                    for (uint16_t f = lastSeq; f < gotSeq; f++)
                        qDebug() << "Missing Audio Sequence: (" << r.length() << ") " << f;
                    lastSeq = gotSeq;
                }

                // Actual audio data is r[24] onwards sent in two groups.
                bool duplicate = false;
                for (uint16_t f = 0; f < seqBuf.length(); f++)
                    if (seqBuf[f].seqNum == gotSeq)
                        duplicate = true;
                if (!duplicate)
                {
                    //qDebug() << "Got Audio Sequence: (" << r.length() << ") " << gotSeq;
                    // Delete contents of buffer up to existing pos()
                    buffer->buffer().remove(0,buffer->pos());
                    // Seek to end of curent buffer
                    buffer->seek(buffer->size()); 
                    // Append to end of buffer
                    buffer->write(r.mid(24).constData(), r.mid(24).length());
                    // Seek to start of buffer.
                    buffer->seek(0);
                }
            }
            break;

        }

        udpBase::DataReceived(r); // Call parent function to process the rest.
        r.clear();
        datagram.clear();
    }
}



void udpBase::init()
{
    udp = new QUdpSocket(this);
    udp->bind(); // Bind to random port.
    localPort = udp->localPort();
    qDebug() << "UDP Stream bound to local port:" << localPort << " remote port:" << port;
    uint32_t addr = localIP.toIPv4Address();
    localSID = (addr >> 8 & 0xff) << 24 | (addr & 0xff) << 16 | (localPort & 0xffff);
}
udpBase::~udpBase()
{
    qDebug() << "Closing UDP stream :" << radioIP.toString() << ":" << port;
    SendPacketDisconnect();
    if (udp != nullptr) {
        udp->close();
        delete udp;
    }
}

// Base class!

void udpBase::DataReceived(QByteArray r)
{
    switch (r.length())
    {
    case (16): // Response to pkt0
        if (r.mid(0, 8) == QByteArrayLiteral("\x10\x00\x00\x00\x04\x00\x00\x00"))
        {
            if (!sentPacketConnect2)
            {
                remoteSID = qFromBigEndian<quint32>(r.mid(8, 4));
                SendPacketConnect2(); // second connect packet
                sentPacketConnect2 = true;
            }
        }
        else if (r.mid(0, 6) == QByteArrayLiteral("\x10\x00\x00\x00\x00\x00"))
        {   // pkt0

        }
        else if (r.mid(0, 6) == QByteArrayLiteral("\x10\x00\x00\x00\x01\x00"))
        {   // retransmit request
            // Send an idle with the requested seqnum if not found.
            uint16_t gotSeq = qFromLittleEndian<quint16>(r.mid(6, 2));
            qDebug() << "Retransmit request for (audio): " << gotSeq;
            int f;
            for (f = txSeqBuf.length() - 1; f >= 0; f--)
            {
                if (txSeqBuf[f].seqNum == gotSeq) {
                    qDebug() << "Sending requested packet (len=" << txSeqBuf[f].data.length() << ")";
                    udp->writeDatagram(txSeqBuf[f].data, radioIP, port);
                    udp->writeDatagram(txSeqBuf[f].data, radioIP, port);
                    break;
                }
            }
            if (f == 0)
            {
                // Packet was not found in buffer
                qDebug() << "Could not find requested packet, sending idle.";
                SendPkt0Idle(false, gotSeq);
            }
        }
        else if (r.mid(0, 6) == QByteArrayLiteral("\x18\x00\x00\x00\x01\x00"))
        {   // retransmit range request, can contain multiple ranges.
            for (int f = 16; f < r.length() - 4; f = f + 4)
            {
                quint16 start = qFromLittleEndian<quint16>(r.mid(f, 2));
                quint16 end = qFromLittleEndian<quint16>(r.mid(f + 2, 2));
                qDebug() << "Retransmit range request for (audio) from:" << start << " to " << end;
                for (quint16 gotSeq = start; gotSeq <= end; gotSeq++)
                {
                    int h;
                    for (h = txSeqBuf.length() - 1; h >= 0; h--)
                        if (txSeqBuf[h].seqNum == gotSeq) {
                            qDebug() << "Sending requested packet (len=" << txSeqBuf[h].data.length() << ")";
                            udp->writeDatagram(txSeqBuf[h].data, radioIP, port);
                            udp->writeDatagram(txSeqBuf[h].data, radioIP, port);
                            break;
                        }
                    if (h==0)
                    {
                        qDebug() << "Could not find requested packet, sending idle.";
                        SendPkt0Idle(false, gotSeq);
                    }
                }
            }
        }
        break;

    case (21): // pkt7, send response if request.
        if (r.mid(1, 5) == QByteArrayLiteral("\x00\x00\x00\x07\x00"))
        {
            uint16_t gotSeq = qFromLittleEndian<quint16>(r.mid(6, 2));

            if (r[16] == (char)0x00)
            {
                QMutexLocker locker(&mutex);

                const unsigned char p[] = { 0x15, 0x00, 0x00, 0x00, 0x07, 0x00,static_cast<unsigned char>(gotSeq & 0xff),static_cast<unsigned char>((gotSeq >> 8) & 0xff),
                    static_cast<unsigned char>(localSID >> 24 & 0xff), static_cast<unsigned char>(localSID >> 16 & 0xff), static_cast<unsigned char>(localSID >> 8 & 0xff), static_cast<unsigned char>(localSID & 0xff),
                    static_cast<unsigned char>(remoteSID >> 24 & 0xff), static_cast<unsigned char>(remoteSID >> 16 & 0xff), static_cast<unsigned char>(remoteSID >> 8 & 0xff), static_cast<unsigned char>(remoteSID & 0xff),
                    0x01,static_cast<unsigned char>(r[17]),static_cast<unsigned char>(r[18]),static_cast<unsigned char>(r[19]),static_cast<unsigned char>(r[20])
                };

                udp->writeDatagram(QByteArray::fromRawData((const char *)p, sizeof(p)), radioIP, port);

            }
        }
        break;
    default:
        break;

    }

}

// Send periodic idle packets (every 100ms)
void udpBase::SendPkt0Idle(bool tracked=true,quint16 seq=0)
{
    unsigned char p[] = { 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x00,
        static_cast<unsigned char>(localSID >> 24 & 0xff), static_cast<unsigned char>(localSID >> 16 & 0xff), static_cast<unsigned char>(localSID >> 8 & 0xff), static_cast<unsigned char>(localSID & 0xff),
        static_cast<unsigned char>(remoteSID >> 24 & 0xff), static_cast<unsigned char>(remoteSID >> 16 & 0xff), static_cast<unsigned char>(remoteSID >> 8 & 0xff), static_cast<unsigned char>(remoteSID & 0xff)
    };

    if (!tracked) {
        p[6] = seq & 0xff;
        p[7] = (seq >> 8) & 0xff;
        udp->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), radioIP, port);
    }
    else {
        SendTrackedPacket(QByteArray::fromRawData((const char*)p, sizeof(p)));
    }
    return;
}

// Send periodic idle packets (every 3000ms)
void udpBase::SendPkt7Idle()
{


    QMutexLocker locker(&mutex);
    //qDebug() << this->metaObject()->className()  << " tx buffer size:" << txSeqBuf.length();

    const unsigned char p[] = { 0x15, 0x00, 0x00, 0x00, 0x07, 0x00, static_cast<unsigned char>(pkt7SendSeq & 0xff),static_cast<unsigned char>(pkt7SendSeq >> 8 & 0xff),
        static_cast<unsigned char>(localSID >> 24 & 0xff), static_cast<unsigned char>(localSID >> 16 & 0xff), static_cast<unsigned char>(localSID >> 8 & 0xff), static_cast<unsigned char>(localSID & 0xff),
        static_cast<unsigned char>(remoteSID >> 24 & 0xff), static_cast<unsigned char>(remoteSID >> 16 & 0xff), static_cast<unsigned char>(remoteSID >> 8 & 0xff), static_cast<unsigned char>(remoteSID & 0xff),
        static_cast<unsigned char>(rand()),static_cast<unsigned char>(innerSendSeq & 0xff),static_cast<unsigned char>(innerSendSeq >> 8 & 0xff), 0x06
    };

    lastPacket7Sent = time(0);
    udp->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), radioIP, port);
    pkt7SendSeq++;
    return;
}


qint64 udpBase::SendTrackedPacket(QByteArray d)
{
    QMutexLocker locker(&mutex);
    // As the radio can request retransmission of these packets, store them in a buffer (eventually!)
    d[6] = sendSeq & 0xff;
    d[7] = (sendSeq >> 8) & 0xff;
    SEQBUFENTRY s;
    s.seqNum = sendSeq;
    s.timeSent = time(NULL);
    s.data = (d);
    txSeqBuf.append(s);
    PurgeOldEntries();
    sendSeq++;

    return udp->writeDatagram(d, radioIP, port);
}


void udpBase::PurgeOldEntries()
{
    for (int f = txSeqBuf.length() - 1; f >= 0; f--)
    {
        // Delete any entries older than 1 second.
        if (difftime(time(NULL),txSeqBuf[f].timeSent) > 3)
            txSeqBuf.removeAt(f);
    }

}

qint64 udpBase::SendPacketConnect()
{
    qDebug() << this->metaObject()->className() << ": Sending Connect";
    QMutexLocker locker(&mutex);
    const unsigned char p[] = { 0x10, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
        static_cast<unsigned char>(localSID >> 24 & 0xff), static_cast<unsigned char>(localSID >> 16 & 0xff), static_cast<unsigned char>(localSID >> 8 & 0xff), static_cast<unsigned char>(localSID & 0xff),
        static_cast<unsigned char>(remoteSID >> 24 & 0xff), static_cast<unsigned char>(remoteSID >> 16 & 0xff), static_cast<unsigned char>(remoteSID >> 8 & 0xff), static_cast<unsigned char>(remoteSID & 0xff)
    };

    udp->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), radioIP, port);
    return udp->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), radioIP, port);
}


qint64 udpBase::SendPacketConnect2()
{
    qDebug() << this->metaObject()->className() << ": Sending Connect2";
    QMutexLocker locker(&mutex);
    const unsigned char p[] = { 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00,
        static_cast<unsigned char>(localSID >> 24 & 0xff), static_cast<unsigned char>(localSID >> 16 & 0xff), static_cast<unsigned char>(localSID >> 8 & 0xff), static_cast<unsigned char>(localSID & 0xff),
        static_cast<unsigned char>(remoteSID >> 24 & 0xff), static_cast<unsigned char>(remoteSID >> 16 & 0xff), static_cast<unsigned char>(remoteSID >> 8 & 0xff), static_cast<unsigned char>(remoteSID & 0xff)
    };

    udp->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), radioIP, port);
    return udp->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), radioIP, port);
}

qint64 udpBase::SendPacketDisconnect() // Unmanaged packet
{
    QMutexLocker locker(&mutex);
    //qDebug() << "Sending Stream Disconnect";

    const unsigned char p[] = { 0x10, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
        static_cast<unsigned char>(localSID >> 24 & 0xff), static_cast<unsigned char>(localSID >> 16 & 0xff), static_cast<unsigned char>(localSID >> 8 & 0xff), static_cast<unsigned char>(localSID & 0xff),
        static_cast<unsigned char>(remoteSID >> 24 & 0xff), static_cast<unsigned char>(remoteSID >> 16 & 0xff), static_cast<unsigned char>(remoteSID >> 8 & 0xff), static_cast<unsigned char>(remoteSID & 0xff)
    };
    udp->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), radioIP, port);

    return udp->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), radioIP, port);
}


unsigned char* udpBase::Passcode(QString str)
{
    const unsigned char sequence[] =
    {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0x47,0x5d,0x4c,0x42,0x66,0x20,0x23,0x46,0x4e,0x57,0x45,0x3d,0x67,0x76,0x60,0x41,0x62,0x39,0x59,0x2d,0x68,0x7e,
        0x7c,0x65,0x7d,0x49,0x29,0x72,0x73,0x78,0x21,0x6e,0x5a,0x5e,0x4a,0x3e,0x71,0x2c,0x2a,0x54,0x3c,0x3a,0x63,0x4f,
        0x43,0x75,0x27,0x79,0x5b,0x35,0x70,0x48,0x6b,0x56,0x6f,0x34,0x32,0x6c,0x30,0x61,0x6d,0x7b,0x2f,0x4b,0x64,0x38,
        0x2b,0x2e,0x50,0x40,0x3f,0x55,0x33,0x37,0x25,0x77,0x24,0x26,0x74,0x6a,0x28,0x53,0x4d,0x69,0x22,0x5c,0x44,0x31,
        0x36,0x58,0x3b,0x7a,0x51,0x5f,0x52,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0

    };

    unsigned char* res = new unsigned char[16];
    memset(res, 0, 16); // Make sure res buffer is empty!
    QByteArray ba = str.toLocal8Bit();
    uchar* ascii = (uchar*)ba.constData();
    for (int i = 0; i < str.length() && i < 16; i++)
    {
        int p = ascii[i] + i;
        if (p > 126)
            p = 32 + p % 127;
        res[i] = sequence[p];
    }
    return res;
}


QString udpBase::parseNullTerminatedString(QByteArray c, int s)
{
    QString res = "";
    for (int i = s; i < c.length(); i++)
    {
        if (c[i] != '\0')
            res = res + QChar(c[i]);
        else
            break;
    }
    return res;
}
