// Copyright 2021 Phil Taylor M0VSE
// This code is heavily based on "Kappanhang" by HA2NON, ES1AKOS and W6EL!

#include "udphandler.h"

udpHandler::udpHandler(QString ip, quint16 cport, quint16 sport, quint16 aport, QString username, QString password, 
                            quint16 buffer, quint16 rxsample, quint8 rxcodec, quint16 txsample, quint8 txcodec)
{
    qDebug() << "Starting udpHandler user:" << username << " buffer:" << buffer << " rx sample rate: " << rxsample << 
                        " rx codec: " << rxcodec << " tx sample rate: " << txsample << " tx codec: " << txcodec;

    // Lookup IP address

    this->port = cport;
    this->aport = aport;
    this->sport = sport;
    this->username = username;
    this->password = password;
    this->rxBufferSize = buffer;
    this->rxSampleRate = rxsample;
    this->txSampleRate = txsample;
    this->rxCodec = rxcodec;
    this->txCodec = txcodec;

    /*
    0x01 uLaw 1ch 8bit
    0x02 PCM 1ch 8bit
    0x04 PCM 1ch 16bit
    0x08 PCM 2ch 8bit
    0x10 PCM 2ch 16bit
    0x20 uLaw 2ch 8bit
    */
    this->rxCodec = rxcodec;
    this->txCodec = txcodec;


    // Try to set the IP address, if it is a hostname then perform a DNS lookup.
    if (!radioIP.setAddress(ip))
    {
        QHostInfo remote = QHostInfo::fromName(ip);
        foreach(QHostAddress addr, remote.addresses())
        {
            if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
                radioIP = addr;
                qDebug() << "Got IP Address :" << ip << ": " << addr.toString();
                break;
            }
        }
        if (radioIP.isNull())
        { 
            qDebug() << "Error obtaining IP Address for :" << ip << ": " << remote.errorString();
            return;
        }
    }
    
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
    compName = QString("wfview").toUtf8();
}

udpHandler::~udpHandler()
{
    if (isAuthenticated)
    {
        if (audio != Q_NULLPTR)
        {
            delete audio;
        }

        if (serial != Q_NULLPTR)
        {
            delete serial;
        }

        qDebug() << "Sending De-Auth packet to radio";
        SendPacketAuth(0x01);

    }
}

void udpHandler::changeBufferSize(quint16 value)
{
    emit haveChangeBufferSize(value);
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
    if (serial != Q_NULLPTR)
    {
        serial->Send(data);
    }
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
        case (21): // pkt7, 
            if (r.mid(1, 5) == QByteArrayLiteral("\x00\x00\x00\x07\x00") && r[16] == (char)0x01 && serialAndAudioOpened)
            {
                //qDebug("Got response!");
                // This is a response to our pkt7 request so measure latency (only once fully connected though.
                latency += lastPacket7Sent.msecsTo(QDateTime::currentDateTime());
                latency /= 2;
                emit haveNetworkStatus(" rtt: " + QString::number(latency) + " ms");
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
                    {
                        SendRequestSerialAndAudio();
                    }
                }
            }
            break;
        case (80):  // Status packet
            if (r.mid(0, 6) == QByteArrayLiteral("\x50\x00\x00\x00\x00\x00"))
            {
                if (r.mid(48, 3) == QByteArrayLiteral("\xff\xff\xff"))
                {
                    if (!serialAndAudioOpened)
                    {
                        emit haveNetworkError(radioIP.toString(), "Auth failed, try rebooting the radio.");
                        qDebug() << "Auth failed, try rebooting the radio.";
                    }
                }
                if (r.mid(48, 3) == QByteArrayLiteral("\x00\x00\x00") && r[64] == (char)0x01)
                {
                    emit haveNetworkError(radioIP.toString(), "Got radio disconnected.");
                    qDebug() << "Got radio disconnected.";
                }
            }
            break;

        case(96): // Response to Login packet.
            if (r.mid(0, 6) == QByteArrayLiteral("\x60\x00\x00\x00\x00\x00"))
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
                    connect(pkt0Timer, &QTimer::timeout, this, std::bind(&udpBase::SendPkt0Idle, this, true, 0));
                    pkt0Timer->start(100);

                    SendPacketAuth(0x05);

                    reauthTimer.start(reauthInterval);

                    isAuthenticated = true;
                }

            }
            break;
        case (144):
            if (!serialAndAudioOpened && r.mid(0, 6) == QByteArrayLiteral("\x90\x00\x00\x00\x00\x00") && r[0x60] == (char)0x01)
            {
                devName = parseNullTerminatedString(r, 0x40);
                QHostAddress ip = QHostAddress(qFromBigEndian<quint32>(r.mid(0x84, 4)));
                if (parseNullTerminatedString(r, 0x64) != compName) //  || ip != localIP ) // TODO: More testing of IP address detection code!
                {
                    emit haveNetworkStatus("Radio in use by: " + QString::fromUtf8(parseNullTerminatedString(r, 0x64))+" ("+ip.toString()+")");
                } 
                else
                {

                    serial = new udpSerial(localIP, radioIP, sport);
                    audio = new udpAudio(localIP, radioIP, aport,rxBufferSize,rxSampleRate, rxCodec,txSampleRate,txCodec);

                    QObject::connect(serial, SIGNAL(Receive(QByteArray)), this, SLOT(receiveFromSerialStream(QByteArray)));
                    QObject::connect(this, SIGNAL(haveChangeBufferSize(quint16)), audio, SLOT(changeBufferSize(quint16)));

                    serialAndAudioOpened = true;

                    emit haveNetworkStatus(QString::fromUtf8(devName));

                    qDebug() << "Got serial and audio request success, device name: " << QString::fromUtf8(devName);

                    // Stuff can change in the meantime because of a previous login...
                    remoteSID = qFromBigEndian<quint32>(r.mid(8, 4));
                    localSID = qFromBigEndian<quint32>(r.mid(12, 4));
                    authID[0] = r[26];
                    authID[1] = r[27];
                    authID[2] = r[28];
                    authID[3] = r[29];
                    authID[4] = r[30];
                    authID[5] = r[31];
                }
                // Is there already somebody connected to the radio?
            }
            break;

        case (168):

            if (r.mid(0, 6) == QByteArrayLiteral("\xa8\x00\x00\x00\x00\x00")) 
            {
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

    /*
        0x72 is RX audio codec
        0x73 is TX audio codec (only single channel options) 
        0x01 uLaw 1ch 8bit
        0x02 PCM 1ch 8bit
        0x04 PCM 1ch 16bit
        0x08 PCM 2ch 8bit
        0x10 PCM 2ch 16bit
        0x20 uLaw 2ch 8bit
    */

    quint8* usernameEncoded = Passcode(username);
    int txSeqBufLengthMs = 50;

    const quint8 p[] = {
        0x90, 0x00, 0x00, 0x00, 0x00, 0x00,  0x00, 0x00,
        static_cast<quint8>(localSID >> 24 & 0xff), static_cast<quint8>(localSID >> 16 & 0xff), static_cast<quint8>(localSID >> 8 & 0xff), static_cast<quint8>(localSID & 0xff),
        static_cast<quint8>(remoteSID >> 24 & 0xff), static_cast<quint8>(remoteSID >> 16 & 0xff), static_cast<quint8>(remoteSID >> 8 & 0xff), static_cast<quint8>(remoteSID & 0xff),
        0x00, 0x00, 0x00, 0x80, 0x01, 0x03, 0x00, static_cast<quint8>(authInnerSendSeq & 0xff), static_cast<quint8>(authInnerSendSeq >> 8 & 0xff),
        0x00, static_cast<quint8>(authID[0]), static_cast<quint8>(authID[1]), static_cast<quint8>(authID[2]),
        static_cast<quint8>(authID[3]), static_cast<quint8>(authID[4]), static_cast<quint8>(authID[5]),
        static_cast<quint8>(a8replyID[0]), static_cast<quint8>(a8replyID[1]), static_cast<quint8>(a8replyID[2]), static_cast<quint8>(a8replyID[3]),
        static_cast<quint8>(a8replyID[4]), static_cast<quint8>(a8replyID[5]), static_cast<quint8>(a8replyID[6]), static_cast<quint8>(a8replyID[7]),
        static_cast<quint8>(a8replyID[8]), static_cast<quint8>(a8replyID[9]), static_cast<quint8>(a8replyID[10]), static_cast<quint8>(a8replyID[11]),
        static_cast<quint8>(a8replyID[12]), static_cast<quint8>(a8replyID[13]), static_cast<quint8>(a8replyID[14]), static_cast<quint8>(a8replyID[15]),
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x49, 0x43, 0x2d, 0x37, 0x38, 0x35, 0x31, 0x00, // IC-7851 in plain text
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        usernameEncoded[0], usernameEncoded[1], usernameEncoded[2], usernameEncoded[3],
        usernameEncoded[4], usernameEncoded[5], usernameEncoded[6], usernameEncoded[7],
        usernameEncoded[8], usernameEncoded[9], usernameEncoded[10], usernameEncoded[11],
        usernameEncoded[12], usernameEncoded[13], usernameEncoded[14], usernameEncoded[15],
        0x01, 0x01, rxCodec, txCodec, 0x00, 0x00, static_cast<quint8>(rxSampleRate >> 8 & 0xff), static_cast<quint8>(rxSampleRate & 0xff),
        0x00, 0x00, static_cast<quint8>(txSampleRate >> 8 & 0xff), static_cast<quint8>(txSampleRate & 0xff),
        0x00, 0x00, static_cast<quint8>(sport >> 8 & 0xff), static_cast<quint8>(sport & 0xff),
        0x00, 0x00, static_cast<quint8>(aport >> 8 & 0xff), static_cast<quint8>(aport & 0xff), 0x00, 0x00,
        static_cast<quint8>(txSeqBufLengthMs >> 8 & 0xff), static_cast<quint8>(txSeqBufLengthMs & 0xff), 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    authInnerSendSeq++;
    delete[] usernameEncoded;

    return SendTrackedPacket(QByteArray::fromRawData((const char*)p, sizeof(p)));
}


qint64 udpHandler::SendPacketLogin() // Only used on control stream.
{

    uint16_t authStartID = rand() | rand() << 8;
    quint8* usernameEncoded = Passcode(username);
    quint8* passwordEncoded = Passcode(password);

    quint8 p[] = {
        0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        static_cast<quint8>(localSID >> 24 & 0xff), static_cast<quint8>(localSID >> 16 & 0xff), static_cast<quint8>(localSID >> 8 & 0xff), static_cast<quint8>(localSID & 0xff),
        static_cast<quint8>(remoteSID >> 24 & 0xff), static_cast<quint8>(remoteSID >> 16 & 0xff), static_cast<quint8>(remoteSID >> 8 & 0xff), static_cast<quint8>(remoteSID & 0xff),
        0x00, 0x00, 0x00, 0x70, 0x01, 0x00, 0x00, static_cast<quint8>(authInnerSendSeq & 0xff), static_cast<quint8>(authInnerSendSeq >> 8 & 0xff),
        0x00, static_cast<quint8>(authStartID & 0xff), static_cast<quint8>(authStartID >> 8 & 0xff), 0x00, 0x00, 0x00, 0x00,
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
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    
    memcpy(p + 0x60, compName.constData(), compName.length());

    delete[] usernameEncoded;
    delete[] passwordEncoded;

    authInnerSendSeq++;
    return SendTrackedPacket(QByteArray::fromRawData((const char*)p, sizeof(p)));
}


qint64 udpHandler::SendPacketAuth(uint8_t magic)
{

    const quint8 p[] = {
        0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x00,
        static_cast<quint8>(localSID >> 24 & 0xff), static_cast<quint8>(localSID >> 16 & 0xff), static_cast<quint8>(localSID >> 8 & 0xff), static_cast<quint8>(localSID & 0xff),
        static_cast<quint8>(remoteSID >> 24 & 0xff), static_cast<quint8>(remoteSID >> 16 & 0xff), static_cast<quint8>(remoteSID >> 8 & 0xff), static_cast<quint8>(remoteSID & 0xff),
        0x00, 0x00, 0x00, 0x30, 0x01, static_cast<quint8>(magic), 0x00, static_cast<quint8>(authInnerSendSeq & 0xff), static_cast<quint8>((authInnerSendSeq) >> 8 & 0xff), 0x00,
        static_cast<quint8>(authID[0]), static_cast<quint8>(authID[1]), static_cast<quint8>(authID[2]), 
        static_cast<quint8>(authID[3]), static_cast<quint8>(authID[4]), static_cast<quint8>(authID[5]),
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    authInnerSendSeq++;
    return SendTrackedPacket(QByteArray::fromRawData((const char *)p, sizeof(p)));
}


// (pseudo) serial class
udpSerial::udpSerial(QHostAddress local, QHostAddress ip, quint16 sport) 
{
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
    const quint8 p[] = { static_cast<quint8>(0x15 + l), 0x00, 0x00, 0x00, 0x00, 0x00,0x00,0x00,
        static_cast<quint8>(localSID >> 24 & 0xff), static_cast<quint8>(localSID >> 16 & 0xff), static_cast<quint8>(localSID >> 8 & 0xff), static_cast<quint8>(localSID & 0xff),
        static_cast<quint8>(remoteSID >> 24 & 0xff), static_cast<quint8>(remoteSID >> 16 & 0xff), static_cast<quint8>(remoteSID >> 8 & 0xff), static_cast<quint8>(remoteSID & 0xff),
        0xc1, static_cast<quint8>(l), 0x00, static_cast<quint8>(sendSeqB >> 8 & 0xff),static_cast<quint8>(sendSeqB & 0xff)
    };
    QByteArray t = QByteArray::fromRawData((const char*)p, sizeof(p));
    t.append(d);
    SendTrackedPacket(t);
    sendSeqB++;
    return 1;
}



void udpSerial::SendIdle()
{
    const quint8 p[] = { 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        static_cast<quint8>(localSID >> 24 & 0xff), static_cast<quint8>(localSID >> 16 & 0xff), static_cast<quint8>(localSID >> 8 & 0xff), static_cast<quint8>(localSID & 0xff),
        static_cast<quint8>(remoteSID >> 24 & 0xff), static_cast<quint8>(remoteSID >> 16 & 0xff), static_cast<quint8>(remoteSID >> 8 & 0xff), static_cast<quint8>(remoteSID & 0xff)
    };

    SendTrackedPacket(QByteArray::fromRawData((const char*)p, sizeof(p)));
}

void udpSerial::SendPeriodic()
{
    const quint8 p[] = { 0x15, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
        static_cast<quint8>(localSID >> 24 & 0xff), static_cast<quint8>(localSID >> 16 & 0xff), static_cast<quint8>(localSID >> 8 & 0xff), static_cast<quint8>(localSID & 0xff),
        static_cast<quint8>(remoteSID >> 24 & 0xff), static_cast<quint8>(remoteSID >> 16 & 0xff), static_cast<quint8>(remoteSID >> 8 & 0xff), static_cast<quint8>(remoteSID & 0xff)
    };

    SendTrackedPacket(QByteArray::fromRawData((const char*)p, sizeof(p)));

}

qint64 udpSerial::SendPacketOpenClose(bool close)
{
    uint8_t magic = 0x05;

    if (close) 
    {
        magic = 0x00;
    }

    const quint8 p[] = {
        0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        static_cast<quint8>(localSID >> 24 & 0xff), static_cast<quint8>(localSID >> 16 & 0xff), static_cast<quint8>(localSID >> 8 & 0xff), static_cast<quint8>(localSID & 0xff),
        static_cast<quint8>(remoteSID >> 24 & 0xff), static_cast<quint8>(remoteSID >> 16 & 0xff), static_cast<quint8>(remoteSID >> 8 & 0xff), static_cast<quint8>(remoteSID & 0xff),
        0xc0, 0x01, 0x00, static_cast<const quint8>(sendSeqB >> 8 & 0xff), static_cast<const quint8>(sendSeqB & 0xff),static_cast<quint8>(magic)
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
                // First check if we are missing any packets?
                uint16_t gotSeq = qFromLittleEndian<quint16>(r.mid(6, 2));
                if (lastReceivedSeq == 0 || lastReceivedSeq > gotSeq) {
                    lastReceivedSeq = gotSeq;
                }

                for (uint16_t f = lastReceivedSeq + 1; f < gotSeq; f++) {
                    // Do we need to request a retransmit?
                    qDebug() << this->metaObject()->className() << ": Missing Sequence: (" << r.length() << ") " << f;
                }

                
                lastReceivedSeq = gotSeq;

                quint8 temp = r[0] - 0x15;
                if ((quint8)r[16] == 0xc1 && (quint8)r[17] == temp)
                {
                    emit Receive(r.mid(21));
                }  
            }
            break;

        }
        udpBase::DataReceived(r); // Call parent function to process the rest.
        r.clear();
        datagram.clear();

    }
}


// Audio stream
udpAudio::udpAudio(QHostAddress local, QHostAddress ip, quint16 aport, quint16 buffer, quint16 rxsample, quint8 rxcodec, quint16 txsample, quint8 txcodec)
{
    qDebug() << "Starting udpAudio";
    this->localIP = local;
    this->port = aport;
    this->radioIP = ip;
    this->bufferSize = buffer;
    this->rxSampleRate = rxsample;
    this->txSampleRate = txsample;
    this->rxCodec = rxcodec;
    this->txCodec = txcodec;

    init(); // Perform connection

    QUdpSocket::connect(udp, &QUdpSocket::readyRead, this, &udpAudio::DataReceived);

    if (rxCodec == 0x01 || rxCodec == 0x20)
        rxIsUlawCodec = true;
    if (rxCodec == 0x08 || rxCodec == 0x10 || rxCodec == 0x20)
        rxChannelCount = 2;
    if (rxCodec == 0x02 || rxCodec == 0x8)
        rxNumSamples = 8; // uLaw is actually 16bit. 

    rxaudio = new audioHandler();
    rxAudioThread = new QThread(this);

    rxaudio->moveToThread(rxAudioThread);

    connect(this, SIGNAL(setupRxAudio(quint8, quint8, quint16, quint16, bool, bool)), rxaudio, SLOT(init(quint8, quint8, quint16, quint16, bool, bool)));
    connect(this, SIGNAL(haveAudioData(QByteArray)), rxaudio, SLOT(incomingAudio(QByteArray)));
    connect(this, SIGNAL(haveChangeBufferSize(quint16)), rxaudio, SLOT(changeBufferSize(quint16)));
    connect(rxAudioThread, SIGNAL(finished()), rxaudio, SLOT(deleteLater()));

    if (txCodec == 0x01)
        txIsUlawCodec = true;
    else if (txCodec == 0x02)
        txNumSamples = 8; // uLaw is actually 16bit. 

    txChannelCount = 1; // Only 1 channel is supported.

    txaudio = new audioHandler();
    txAudioThread = new QThread(this);

    txaudio->moveToThread(txAudioThread);

    connect(this, SIGNAL(setupTxAudio(quint8, quint8, quint16, quint16, bool, bool)), txaudio, SLOT(init(quint8, quint8, quint16, quint16, bool, bool)));
    connect(txaudio, SIGNAL(haveAudioData(QByteArray)), this, SLOT(sendTxAudio(QByteArray)));
    connect(txAudioThread, SIGNAL(finished()), txaudio, SLOT(deleteLater()));
    
    rxAudioThread->start();
    emit setupRxAudio(rxNumSamples, rxChannelCount, rxSampleRate, bufferSize, rxIsUlawCodec,false);

    txAudioThread->start();
    emit setupTxAudio(txNumSamples, txChannelCount, txSampleRate, bufferSize, txIsUlawCodec,true);

    SendPacketConnect(); // First connect packet, audio should start very soon after.
}

udpAudio::~udpAudio()
{
    if (rxAudioThread) {
        rxAudioThread->quit();
        rxAudioThread->wait();
    }
	
    if (txAudioThread) {
        txAudioThread->quit();
        txAudioThread->wait();
    }
}

void udpAudio::sendTxAudio(QByteArray audio)
{
    quint8 p[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      static_cast<quint8>(localSID >> 24 & 0xff), static_cast<quint8>(localSID >> 16 & 0xff), static_cast<quint8>(localSID >> 8 & 0xff), static_cast<quint8>(localSID & 0xff),
      static_cast<quint8>(remoteSID >> 24 & 0xff), static_cast<quint8>(remoteSID >> 16 & 0xff), static_cast<quint8>(remoteSID >> 8 & 0xff), static_cast<quint8>(remoteSID & 0xff),
      0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00
    };
    int counter = 0;
    if (((txCodec == 0x01 || txCodec == 0x02) && audio.length() != 960)  || (txCodec == 0x04 && audio.length() != 1920)) {
        qDebug() << "Unsupported TX audio length :" << audio.length() << " With codec: " << txCodec;
    }
    while (counter < audio.length())
    {
        QByteArray tx = QByteArray::fromRawData((const char*)p, sizeof(p));
        QByteArray partial = audio.mid(counter, 1364);
        tx.append(partial);
        tx[0] = static_cast<quint8>(tx.length() & 0xff);
        tx[1] = static_cast<quint8>(tx.length() >> 8 & 0xff);
        tx[18] = static_cast<quint8>(sendAudioSeq >> 8 & 0xff);
        tx[19] = static_cast<quint8>(sendAudioSeq & 0xff);
        tx[22] = static_cast<quint8>(partial.length() >> 8 & 0xff);
        tx[23] = static_cast<quint8>(partial.length() & 0xff);
        counter = counter + partial.length();
        //qDebug() << "Sending audio packet length: " << tx.length();
        SendTrackedPacket(tx);
        sendAudioSeq++;
    }

}

void udpAudio::changeBufferSize(quint16 value)
{
    emit haveChangeBufferSize(value);
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
            /* Audio packets start as follows:
                    PCM 16bit and PCM8/uLAW stereo: 0x44,0x02 for first packet and 0x6c,0x05 for second.
                    uLAW 8bit/PCM 8bit 0xd8,0x03 for all packets
                    PCM 16bit stereo 0x6c,0x05 first & second 0x70,0x04 third


            */
            if (r.mid(0, 2) == QByteArrayLiteral("\x6c\x05") || 
                r.mid(0, 2) == QByteArrayLiteral("\x44\x02") ||
                r.mid(0, 2) == QByteArrayLiteral("\xd8\x03") ||
                r.mid(0, 2) == QByteArrayLiteral("\x70\x04"))
            {
                // First check if we are missing any packets
                // Audio stream does not send periodic pkt0 so seq "should" be sequential.
                uint16_t gotSeq = qFromLittleEndian<quint16>(r.mid(6, 2));
                if (lastReceivedSeq == 0 || lastReceivedSeq > gotSeq) {
                    lastReceivedSeq = gotSeq;
                }

                for (uint16_t f = lastReceivedSeq+1 ; f < gotSeq; f++) {
                    // Do we need to request a retransmit?
                    qDebug() << this->metaObject()->className() << ": Missing Sequence: (" << r.length() << ") " << f;
                }

                lastReceivedSeq = gotSeq;

                emit haveAudioData(r.mid(24));
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
    if (udp != Q_NULLPTR) {
        SendPacketDisconnect();
        udp->close();
        delete udp;
    }
    if (pkt0Timer != Q_NULLPTR)
    {
        pkt0Timer->stop();
        delete pkt0Timer;
    }
    if (pkt7Timer != Q_NULLPTR)
    {
        pkt7Timer->stop();
        delete pkt7Timer;
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
            // Just get the seqnum and ignore the rest.
            lastReceivedSeq = qFromLittleEndian<quint16>(r.mid(6, 2));
        }
        else if (r.mid(0, 6) == QByteArrayLiteral("\x10\x00\x00\x00\x01\x00"))
        {   // retransmit request
            // Send an idle with the requested seqnum if not found.
            uint16_t gotSeq = qFromLittleEndian<quint16>(r.mid(6, 2));
            //qDebug() << "Retransmit request for "<< this->metaObject()->className() <<" : " << gotSeq;
            bool found=false;
            for (int f = txSeqBuf.length() - 1; f >= 0; f--)
            {
                if (txSeqBuf[f].seqNum == gotSeq) {
                    qDebug() << this->metaObject()->className() << ": retransmitting packet :" << gotSeq << " (len=" << txSeqBuf[f].data.length() << ")";
                    udp->writeDatagram(txSeqBuf[f].data, radioIP, port);
                    udp->writeDatagram(txSeqBuf[f].data, radioIP, port);
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                // Packet was not found in buffer
                qDebug() << this->metaObject()->className() << ": Could not find requested packet " << gotSeq << ", sending idle.";
                SendPkt0Idle(false, gotSeq);
            }
        }
        else if (r.mid(0, 6) == QByteArrayLiteral("\x18\x00\x00\x00\x01\x00"))
        {   // retransmit range request, can contain multiple ranges.
            for (int f = 16; f < r.length() - 4; f = f + 4)
            {
                quint16 start = qFromLittleEndian<quint16>(r.mid(f, 2));
                quint16 end = qFromLittleEndian<quint16>(r.mid(f + 2, 2));
                qDebug() << this->metaObject()->className() << ": Retransmit range request for:" << start << " to " << end;
                for (quint16 gotSeq = start; gotSeq <= end; gotSeq++)
                {
                    bool found=false;
                    for (int h = txSeqBuf.length() - 1; h >= 0; h--)
                        if (txSeqBuf[h].seqNum == gotSeq) {
                            qDebug() << this->metaObject()->className() << ": retransmitting packet :" << gotSeq << " (len=" << txSeqBuf[f].data.length() << ")";
                            udp->writeDatagram(txSeqBuf[h].data, radioIP, port);
                            udp->writeDatagram(txSeqBuf[h].data, radioIP, port);
                            found = true;
                            break;
                        }
                    if (!found)
                    {
                        qDebug() << this->metaObject()->className() << ": Could not find requested packet " << gotSeq << ", sending idle.";
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

                const quint8 p[] = { 0x15, 0x00, 0x00, 0x00, 0x07, 0x00,static_cast<quint8>(gotSeq & 0xff),static_cast<quint8>((gotSeq >> 8) & 0xff),
                    static_cast<quint8>(localSID >> 24 & 0xff), static_cast<quint8>(localSID >> 16 & 0xff), static_cast<quint8>(localSID >> 8 & 0xff), static_cast<quint8>(localSID & 0xff),
                    static_cast<quint8>(remoteSID >> 24 & 0xff), static_cast<quint8>(remoteSID >> 16 & 0xff), static_cast<quint8>(remoteSID >> 8 & 0xff), static_cast<quint8>(remoteSID & 0xff),
                    0x01,static_cast<quint8>(r[17]),static_cast<quint8>(r[18]),static_cast<quint8>(r[19]),static_cast<quint8>(r[20])
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
    quint8 p[] = { 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x00,
        static_cast<quint8>(localSID >> 24 & 0xff), static_cast<quint8>(localSID >> 16 & 0xff), static_cast<quint8>(localSID >> 8 & 0xff), static_cast<quint8>(localSID & 0xff),
        static_cast<quint8>(remoteSID >> 24 & 0xff), static_cast<quint8>(remoteSID >> 16 & 0xff), static_cast<quint8>(remoteSID >> 8 & 0xff), static_cast<quint8>(remoteSID & 0xff)
    };

    lastPacket0Sent = QDateTime::currentDateTime(); // Is this used?

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

    const quint8 p[] = { 0x15, 0x00, 0x00, 0x00, 0x07, 0x00, static_cast<quint8>(pkt7SendSeq & 0xff),static_cast<quint8>(pkt7SendSeq >> 8 & 0xff),
        static_cast<quint8>(localSID >> 24 & 0xff), static_cast<quint8>(localSID >> 16 & 0xff), static_cast<quint8>(localSID >> 8 & 0xff), static_cast<quint8>(localSID & 0xff),
        static_cast<quint8>(remoteSID >> 24 & 0xff), static_cast<quint8>(remoteSID >> 16 & 0xff), static_cast<quint8>(remoteSID >> 8 & 0xff), static_cast<quint8>(remoteSID & 0xff),
        0x00, static_cast<quint8>(rand()),static_cast<quint8>(innerSendSeq & 0xff),static_cast<quint8>(innerSendSeq >> 8 & 0xff), 0x06
    };
    //qDebug() << this->metaObject()->className() << ": Send pkt7: " <<  QByteArray::fromRawData((const char*)p, sizeof(p));
    lastPacket7Sent = QDateTime::currentDateTime();
    udp->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), radioIP, port);
    pkt7SendSeq++;
    innerSendSeq++;
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
        if (difftime(time(NULL), txSeqBuf[f].timeSent) > 5) // Delete anything more than 5 seconds old.
        {
            txSeqBuf.removeAt(f);
        }
    }

}

qint64 udpBase::SendPacketConnect()
{
    qDebug() << this->metaObject()->className() << ": Sending Connect";
    QMutexLocker locker(&mutex);
    const quint8 p[] = { 0x10, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
        static_cast<quint8>(localSID >> 24 & 0xff), static_cast<quint8>(localSID >> 16 & 0xff), static_cast<quint8>(localSID >> 8 & 0xff), static_cast<quint8>(localSID & 0xff),
        static_cast<quint8>(remoteSID >> 24 & 0xff), static_cast<quint8>(remoteSID >> 16 & 0xff), static_cast<quint8>(remoteSID >> 8 & 0xff), static_cast<quint8>(remoteSID & 0xff)
    };

    udp->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), radioIP, port);
    return udp->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), radioIP, port);
}


qint64 udpBase::SendPacketConnect2()
{
    qDebug() << this->metaObject()->className() << ": Sending Connect2";
    QMutexLocker locker(&mutex);
    const quint8 p[] = { 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00,
        static_cast<quint8>(localSID >> 24 & 0xff), static_cast<quint8>(localSID >> 16 & 0xff), static_cast<quint8>(localSID >> 8 & 0xff), static_cast<quint8>(localSID & 0xff),
        static_cast<quint8>(remoteSID >> 24 & 0xff), static_cast<quint8>(remoteSID >> 16 & 0xff), static_cast<quint8>(remoteSID >> 8 & 0xff), static_cast<quint8>(remoteSID & 0xff)
    };

    udp->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), radioIP, port);
    return udp->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), radioIP, port);
}

qint64 udpBase::SendPacketDisconnect() // Unmanaged packet
{
    QMutexLocker locker(&mutex);
    //qDebug() << "Sending Stream Disconnect";

    const quint8 p[] = { 0x10, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
        static_cast<quint8>(localSID >> 24 & 0xff), static_cast<quint8>(localSID >> 16 & 0xff), static_cast<quint8>(localSID >> 8 & 0xff), static_cast<quint8>(localSID & 0xff),
        static_cast<quint8>(remoteSID >> 24 & 0xff), static_cast<quint8>(remoteSID >> 16 & 0xff), static_cast<quint8>(remoteSID >> 8 & 0xff), static_cast<quint8>(remoteSID & 0xff)
    };
    udp->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), radioIP, port);

    return udp->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), radioIP, port);
}


quint8* udpBase::Passcode(QString str)
{
    const quint8 sequence[] =
    {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0x47,0x5d,0x4c,0x42,0x66,0x20,0x23,0x46,0x4e,0x57,0x45,0x3d,0x67,0x76,0x60,0x41,0x62,0x39,0x59,0x2d,0x68,0x7e,
        0x7c,0x65,0x7d,0x49,0x29,0x72,0x73,0x78,0x21,0x6e,0x5a,0x5e,0x4a,0x3e,0x71,0x2c,0x2a,0x54,0x3c,0x3a,0x63,0x4f,
        0x43,0x75,0x27,0x79,0x5b,0x35,0x70,0x48,0x6b,0x56,0x6f,0x34,0x32,0x6c,0x30,0x61,0x6d,0x7b,0x2f,0x4b,0x64,0x38,
        0x2b,0x2e,0x50,0x40,0x3f,0x55,0x33,0x37,0x25,0x77,0x24,0x26,0x74,0x6a,0x28,0x53,0x4d,0x69,0x22,0x5c,0x44,0x31,
        0x36,0x58,0x3b,0x7a,0x51,0x5f,0x52,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0

    };

    quint8* res = new quint8[16];
    memset(res, 0, 16); // Make sure res buffer is empty!
    QByteArray ba = str.toLocal8Bit();
    uchar* ascii = (uchar*)ba.constData();
    for (int i = 0; i < str.length() && i < 16; i++)
    {
        int p = ascii[i] + i;
        if (p > 126) 
        {
            p = 32 + p % 127;
        }
        res[i] = sequence[p];
    }
    return res;
}


QByteArray udpBase::parseNullTerminatedString(QByteArray c, int s)
{
    //QString res = "";
    QByteArray res;
    for (int i = s; i < c.length(); i++)
    {
        if (c[i] != '\0')
        {
            res.append(c[i]);
        }
        else
        {
            break;
        }
    }
    return res;
}
