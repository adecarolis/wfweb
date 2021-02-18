// Copyright 2021 Phil Taylor M0VSE
// This code is heavily based on "Kappanhang" by HA2NON, ES1AKOS and W6EL!

#include "udphandler.h"

udpHandler::udpHandler(QString ip, quint16 controlPort, quint16 civPort, quint16 audioPort, QString username, QString password, 
                            quint16 buffer, quint16 rxsample, quint8 rxcodec, quint16 txsample, quint8 txcodec) :
    controlPort(controlPort),
    civPort(civPort),
    audioPort(audioPort)
{

    this->port = this->controlPort;
    this->username = username;
    this->password = password;
    this->rxBufferSize = buffer;
    this->rxSampleRate = rxsample;
    this->txSampleRate = txsample;
    this->rxCodec = rxcodec;
    this->txCodec = txcodec;

    qDebug() << "Starting udpHandler user:" << username << " buffer:" << buffer << " rx sample rate: " << rxsample <<
        " rx codec: " << rxcodec << " tx sample rate: " << txsample << " tx codec: " << txcodec;

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

    udpBase::init(); // Perform UDP socket initialization.

    // Connect socket to my dataReceived function.
    QUdpSocket::connect(udp, &QUdpSocket::readyRead, this, &udpHandler::dataReceived);

    /*
        Connect various timers
    */
    connect(&tokenTimer, &QTimer::timeout, this, std::bind(&udpHandler::sendToken, this, 0x05));
    connect(&areYouThereTimer, &QTimer::timeout, this, QOverload<>::of(&udpHandler::sendAreYouThere));
    connect(&pingTimer, &QTimer::timeout, this, &udpBase::sendPing);
    connect(&idleTimer, &QTimer::timeout, this, std::bind(&udpBase::sendControl, this, true, 0, 0));

    // Start sending are you there packets - will be stopped once "I am here" received
    areYouThereTimer.start(AREYOUTHERE_PERIOD);

    // Set my computer name. Should this be configurable?
    compName = QString("wfview").toUtf8();

}

udpHandler::~udpHandler()
{
    if (isAuthenticated) {
        if (audio != Q_NULLPTR) {
            delete audio;
        }

        if (civ != Q_NULLPTR) {
            delete civ;
        }

        qDebug() << "Sending token removal packet";
        sendToken(0x01);
    }
}

void udpHandler::changeBufferSize(quint16 value)
{
    emit haveChangeBufferSize(value);
}

void udpHandler::receiveFromCivStream(QByteArray data)
{
    emit haveDataFromPort(data);
}

void udpHandler::receiveDataFromUserToRig(QByteArray data)
{
    if (civ != Q_NULLPTR)
    {
        civ->send(data);
    }
}

void udpHandler::dataReceived()
{
    while (udp->hasPendingDatagrams()) {
        lastReceived = time(0);
        QNetworkDatagram datagram = udp->receiveDatagram();
        //qDebug() << "Received: " << datagram.data();
        QByteArray r = datagram.data();

        switch (r.length())
        {
        case (16):
            if (r.mid(0, 8) == QByteArrayLiteral("\x10\x00\x00\x00\x04\x00\x00\x00")) {
                // If timer is active, stop it as they are obviously there!
                if (areYouThereTimer.isActive()) {
                    areYouThereTimer.stop();
                    // send ping packets every second
                    pingTimer.start(PING_PERIOD);
                    idleTimer.start(IDLE_PERIOD);
                }
            }
            // This is "I am ready" in response to "Are you ready" so send login.
            else if (r.mid(0, 8) == QByteArrayLiteral("\x10\x00\x00\x00\x06\x00\x01\x00"))
            {
                qDebug() << this->metaObject()->className() << ": Received I am ready";
                remoteId = qFromLittleEndian<quint32>(r.mid(8, 4));
                sendLogin(); // second login packet
            }
            break;
        case (21): // pkt7, 
            if (r.mid(1, 5) == QByteArrayLiteral("\x00\x00\x00\x07\x00") && r[16] == (char)0x01 && serialAndAudioOpened)
            {
                // This is a response to our ping request so measure latency
                latency += lastPingSentTime.msecsTo(QDateTime::currentDateTime());
                latency /= 2;
                quint32 totalsent = packetsSent;
                quint32 totallost = packetsLost/2;
                if (audio != Q_NULLPTR) {
                    totalsent = totalsent + audio->packetsSent;
                    totallost = totallost + audio->packetsLost/2;
                }
                if (civ != Q_NULLPTR) {
                    totalsent = totalsent + civ->packetsSent;
                    totallost = totallost + civ->packetsLost/2;
                }
                //double perclost = 1.0 * totallost / totalsent * 100.0 ;

                emit haveNetworkStatus(" rtt: " + QString::number(latency) + " ms, loss: (" +QString::number(packetsLost)+ "/"+ QString::number(packetsSent) +")");
            }
            break;
        case (64): // Response to Auth packet?
            if (r.mid(0, 6) == QByteArrayLiteral("\x40\x00\x00\x00\x00\x00") && r[21] == (char)0x05) {
                if (r.mid(0x30, 4) == QByteArrayLiteral("\x00\x00\x00\x00")) {
                    qDebug() << this->metaObject()->className() << ": Token renewal successful";
                    tokenTimer.start(TOKEN_RENEWAL);
                    gotAuthOK = true;
                    if (!serialAndAudioOpened)
                    {
                        sendRequestSerialAndAudio();
                    }

                }
                else if (r.mid(0x30, 4) == QByteArrayLiteral("\xff\xff\xff\xff"))
                {
                    qWarning() << this->metaObject()->className() << ": Radio rejected token renewal, performing login";
                    remoteId = qFromLittleEndian<quint32>(r.mid(8, 4));
                    isAuthenticated = false;
                    sendLogin(); // Try sending login packet (didn't seem to work?)
                }
                else
                {
                    qWarning() << this->metaObject()->className() << ": Unknown response to token renewal? " << r.mid(0x30,4);
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
                        qDebug() << this->metaObject()->className() << ": Auth failed, try rebooting the radio.";
                    }
                }
                if (r.mid(48, 3) == QByteArrayLiteral("\x00\x00\x00") && r[64] == (char)0x01)
                {
                    emit haveNetworkError(radioIP.toString(), "Got radio disconnected.");
                    qDebug() << this->metaObject()->className() << ": Got radio disconnected.";
                }
            }
            break;

        case(96): // Response to Login packet.
            if (r.mid(0, 6) == QByteArrayLiteral("\x60\x00\x00\x00\x00\x00"))
            {
                if (r.mid(48, 4) == QByteArrayLiteral("\xff\xff\xff\xfe"))
                {
                    emit haveNetworkStatus("Invalid Username/Password");
                    qDebug() << this->metaObject()->className() << ": Invalid Username/Password";

                }
                else if (!isAuthenticated)
                {
                    emit haveNetworkStatus("Radio Login OK!");
                    qDebug() << this->metaObject()->className() << ": Received Login OK";

                    authId = r.mid(0x1a, 6);
                    sendToken(0x02);
                    tokenTimer.start(TOKEN_RENEWAL); // Start token request timer

                    isAuthenticated = true;
                }

                if (r.mid(0x40, 4) == QByteArrayLiteral("FTTH"))
                {
                    highBandwidthConnection = true;
                }
                qDebug() << this->metaObject()->className() << ": Detected connection speed " << QString::fromUtf8(parseNullTerminatedString(r, 0x40));

            }
            break;
        case (144):
            if (!serialAndAudioOpened && r.mid(0, 6) == QByteArrayLiteral("\x90\x00\x00\x00\x00\x00") && r[0x60] == (char)0x00)
            {
                devName = parseNullTerminatedString(r, 0x40);
                QHostAddress ip = QHostAddress(qFromBigEndian<quint32>(r.mid(0x84, 4)));
                if (!ip.isEqual(QHostAddress("0.0.0.0")) && parseNullTerminatedString(r, 0x64) != compName) //  || ip != localIP ) // TODO: More testing of IP address detection code!
                {
                    emit haveNetworkStatus(QString::fromUtf8(devName) + "in use by: " + QString::fromUtf8(parseNullTerminatedString(r, 0x64)) + " (" + ip.toString() + ")");
                }
                else {
                    emit haveNetworkStatus(QString::fromUtf8(devName) + " available");
                    sendRequestSerialAndAudio();
                }
            }
            else if (!serialAndAudioOpened && r.mid(0, 6) == QByteArrayLiteral("\x90\x00\x00\x00\x00\x00") && r[0x60] == (char)0x01)
            {
                    devName = parseNullTerminatedString(r, 0x40);

                    civ = new udpCivData(localIP, radioIP, civPort);
                    audio = new udpAudio(localIP, radioIP, audioPort,rxBufferSize,rxSampleRate, rxCodec,txSampleRate,txCodec);

                    QObject::connect(civ, SIGNAL(receive(QByteArray)), this, SLOT(receiveFromCivStream(QByteArray)));
                    QObject::connect(this, SIGNAL(haveChangeBufferSize(quint16)), audio, SLOT(changeBufferSize(quint16)));

                    serialAndAudioOpened = true;

                    emit haveNetworkStatus(QString::fromUtf8(devName));

                    qDebug() << this->metaObject()->className() << "Got serial and audio request success, device name: " << QString::fromUtf8(devName);

                    // Stuff can change in the meantime because of a previous login...
                    remoteId = qFromLittleEndian<quint32>(r.mid(0x08, 4));
                    myId = qFromLittleEndian<quint32>(r.mid(0x0c, 4));
                    authId = r.mid(0x1a, 6);
                    // Is there already somebody connected to the radio?
            }
            break;

        case (168):
            audioType = parseNullTerminatedString(r, 0x72);
            devName = parseNullTerminatedString(r, 0x52);
            replyId = r.mid(0x42, 16);
            qDebug() << this->metaObject()->className() << "Received radio capabilities, Name:" <<
                QString::fromUtf8(devName) << " Audio:" <<
                QString::fromUtf8(audioType);

            break;
        }
        udpBase::dataReceived(r); // Call parent function to process the rest.
        r.clear();
        datagram.clear();

    }

    return;
}


#define SERIALAUDIO_SIZE 0x90
void udpHandler::sendRequestSerialAndAudio()
{
    QByteArray usernameEncoded;
    passcode(username, usernameEncoded);
    int txSeqBufLengthMs = 50;

    quint8 p[SERIALAUDIO_SIZE];
    memset(p, 0x0, sizeof(p));
    qToLittleEndian(sizeof(p), p + 0x00);
    qToLittleEndian(myId, p + 0x08);
    qToLittleEndian(remoteId, p + 0x0c);
    memcpy(p + 0x13, QByteArrayLiteral("\x80\x01\x03").constData(), 3);
    qToLittleEndian(authInnerSendSeq, p + 0x16);
    memcpy(p + 0x1a, authId.constData(), authId.length());
    memcpy(p + 0x20, replyId.constData(), replyId.length());
    memcpy(p + 0x40, devName.constData(), devName.length());
    memcpy(p + 0x60, usernameEncoded.constData(), usernameEncoded.length());
    memcpy(p + 0x70, QByteArrayLiteral("\x01\x01").constData(), 2);
    p[0x72] = rxCodec;
    p[0x73] = txCodec;
    qToBigEndian(rxSampleRate, p + 0x76);
    qToBigEndian(txSampleRate, p + 0x7a);
    qToBigEndian(civPort, p + 0x7e);
    qToBigEndian(audioPort, p + 0x82);
    qToBigEndian(txSeqBufLengthMs, p + 0x86);
    p[0x88] = 0x01;

    authInnerSendSeq++;

    sendTrackedPacket(QByteArray::fromRawData((const char*)p, sizeof(p)));
    return;
}

void udpHandler::sendAreYouThere()
{
    if (areYouThereCounter == 20)
    {
        qDebug() << this->metaObject()->className() << ": Radio not responding.";
        emit haveNetworkStatus("Radio not responding!");
    }
    qDebug() << this->metaObject()->className() << ": Sending Are You There...";

    areYouThereCounter++;
    udpBase::sendControl(false,0x03,0x00);
}

#define LOGIN_SIZE 0x80
void udpHandler::sendLogin() // Only used on control stream.
{

    qDebug() << this->metaObject()->className() << ": Sending login packet";

    uint16_t authStartID = rand() | rand() << 8;
    //uint16_t authStartID = 0x7866;
    QByteArray usernameEncoded;
    QByteArray passwordEncoded;
    passcode(username, usernameEncoded);
    passcode(password, passwordEncoded);

    quint8 p[LOGIN_SIZE];
    memset(p, 0x0, sizeof(p));
    qToLittleEndian(sizeof(p), p + 0x00);
    qToLittleEndian(myId, p + 0x08);
    qToLittleEndian(remoteId, p + 0x0c);
    memcpy(p + 0x13, QByteArrayLiteral("\x70\x01").constData(), 2);
    qToLittleEndian(authInnerSendSeq, p + 0x17);
    qToLittleEndian(authStartID, p + 0x20);
    memcpy(p + 0x40, usernameEncoded.constData(), usernameEncoded.length());
    memcpy(p + 0x50, passwordEncoded.constData(), passwordEncoded.length());
    memcpy(p + 0x60, compName.constData(), compName.length());

    authInnerSendSeq++;
    sendTrackedPacket(QByteArray::fromRawData((const char*)p, sizeof(p)));
    return;
}

#define TOKEN_SIZE 0x40
void udpHandler::sendToken(uint8_t magic)
{
    qDebug() << this->metaObject()->className() << "Sending Token request: " << magic;

    quint8 p[TOKEN_SIZE];
    memset(p, 0x0, sizeof(p));
    qToLittleEndian(sizeof(p), p + 0x00);
    qToLittleEndian(myId, p + 0x08);
    qToLittleEndian(remoteId, p + 0x0c);
    memcpy(p + 0x13, QByteArrayLiteral("\x30\x01").constData(), 2);
    qToLittleEndian(magic, p + 0x15);
    qToLittleEndian(authInnerSendSeq, p + 0x17);
    memcpy(p + 0x1a, authId.constData(), authId.length());

    authInnerSendSeq++;
    sendTrackedPacket(QByteArray::fromRawData((const char *)p, sizeof(p)));
    tokenTimer.start(100); // Set 100ms timer for retry (this will be cancelled if a response is received)
    return;
}


// Class that manages all Civ Data to/from the rig
udpCivData::udpCivData(QHostAddress local, QHostAddress ip, quint16 civPort) 
{
    qDebug() << "Starting udpCivData";
    localIP = local;
    port = civPort;
    radioIP = ip;

    udpBase::init(); // Perform connection

    QUdpSocket::connect(udp, &QUdpSocket::readyRead, this, &udpCivData::dataReceived);

    sendControl(false, 0x03, 0x00); // First connect packet

    /*
        Connect various timers
    */
    connect(&pingTimer, &QTimer::timeout, this, &udpBase::sendPing);
    connect(&idleTimer, &QTimer::timeout, this, std::bind(&udpBase::sendControl, this, true, 0, 0));

    // send ping packets every 100 ms (maybe change to less frequent?)
    pingTimer.start(PING_PERIOD);
    // Send idle packets every 100ms, this timer will be reset everytime a non-idle packet is sent.
    idleTimer.start(IDLE_PERIOD);
}

udpCivData::~udpCivData() {
    sendOpenClose(true);
}

#define CIV_SIZE 0x15
void udpCivData::send(QByteArray d)
{
    // qDebug() << "Sending: (" << d.length() << ") " << d;

    quint8 p[CIV_SIZE];
    memset(p, 0x0, sizeof(p));
    qToLittleEndian((quint16)sizeof(p) + d.length(), p + 0x00);
    qToLittleEndian(myId, p + 0x08);
    qToLittleEndian(remoteId, p + 0x0c);
    p[0x10] = 0xc1;
    qToLittleEndian((quint16)d.length(), p + 0x11);
    qToLittleEndian(sendSeqB, p + 0x13);

    QByteArray t = QByteArray::fromRawData((const char*)p, sizeof(p));
    t.append(d);
    sendTrackedPacket(t);
    sendSeqB++;
    return;
}


#define OPENCLOSE_SIZE 0x16
void udpCivData::sendOpenClose(bool close)
{
    uint8_t magic = 0x05;

    if (close) 
    {
        magic = 0x00;
    }

    quint8 p[OPENCLOSE_SIZE];
    memset(p, 0x0, sizeof(p));
    qToLittleEndian((quint16)sizeof(p), p + 0x00);
    qToLittleEndian(myId, p + 0x08);
    qToLittleEndian(remoteId, p + 0x0c);
    memcpy(p + 0x10, QByteArrayLiteral("\xc0\x01").constData(), 2);
    qToLittleEndian(sendSeqB, p + 0x13);
    p[0x15] = magic;

    sendSeqB++;

    sendTrackedPacket(QByteArray::fromRawData((const char*)p, sizeof(p)));
    return;
}



void udpCivData::dataReceived()
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
                // Update remoteId
                remoteId = qFromLittleEndian<quint32>(r.mid(8, 4));
                sendOpenClose(false);
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
                    emit receive(r.mid(21));
                }  
            }
            break;

        }
        udpBase::dataReceived(r); // Call parent function to process the rest.
        r.clear();
        datagram.clear();

    }
}


// Audio stream
udpAudio::udpAudio(QHostAddress local, QHostAddress ip, quint16 audioPort, quint16 buffer, quint16 rxsample, quint8 rxcodec, quint16 txsample, quint8 txcodec)
{
    qDebug() << "Starting udpAudio";
    this->localIP = local;
    this->port = audioPort;
    this->radioIP = ip;
    this->bufferSize = buffer;
    this->rxSampleRate = rxsample;
    this->txSampleRate = txsample;
    this->rxCodec = rxcodec;
    this->txCodec = txcodec;

    init(); // Perform connection

    QUdpSocket::connect(udp, &QUdpSocket::readyRead, this, &udpAudio::dataReceived);

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

    if (rxCodec == 0x01 || rxCodec == 0x20) {
        rxIsUlawCodec = true;
    }
    if (rxCodec == 0x08 || rxCodec == 0x10 || rxCodec == 0x20) {
        rxChannelCount = 2;
    }
    if (rxCodec == 0x04 || rxCodec == 0x10) {
        rxNumSamples = 16;
    }

    rxaudio = new audioHandler();
    rxAudioThread = new QThread(this);

    rxaudio->moveToThread(rxAudioThread);

    connect(this, SIGNAL(setupRxAudio(quint8, quint8, quint16, quint16, bool, bool)), rxaudio, SLOT(init(quint8, quint8, quint16, quint16, bool, bool)));
    //connect(this, SIGNAL(haveAudioData(QByteArray)), rxaudio, SLOT(incomingAudio(QByteArray)));
    connect(this, SIGNAL(haveChangeBufferSize(quint16)), rxaudio, SLOT(changeBufferSize(quint16)));
    connect(rxAudioThread, SIGNAL(finished()), rxaudio, SLOT(deleteLater()));

    if (txCodec == 0x01)
        txIsUlawCodec = true;
    else if (txCodec == 0x04)
        txNumSamples = 16;

    txChannelCount = 1; // Only 1 channel is supported.

    txaudio = new audioHandler();
    txAudioThread = new QThread(this);

    txaudio->moveToThread(txAudioThread);

    connect(this, SIGNAL(setupTxAudio(quint8, quint8, quint16, quint16, bool, bool)), txaudio, SLOT(init(quint8, quint8, quint16, quint16, bool, bool)));
    //connect(txaudio, SIGNAL(haveAudioData(QByteArray)), this, SLOT(sendTxAudio(QByteArray)));
    connect(txAudioThread, SIGNAL(finished()), txaudio, SLOT(deleteLater()));
    
    rxAudioThread->start();

    txAudioThread->start();

    sendControl(false, 0x03, 0x00); // First connect packet

    connect(&pingTimer, &QTimer::timeout, this, &udpBase::sendPing);
    pingTimer.start(PING_PERIOD); // send ping packets every 100ms

    connect(&txAudioTimer, &QTimer::timeout, this, &udpAudio::sendTxAudio);
    txAudioTimer.start(TXAUDIO_PERIOD);

    emit setupTxAudio(txNumSamples, txChannelCount, txSampleRate, bufferSize, txIsUlawCodec, true);
    emit setupRxAudio(rxNumSamples, rxChannelCount, rxSampleRate, bufferSize, rxIsUlawCodec, false);


}

udpAudio::~udpAudio()
{
    if (txAudioTimer.isActive())
    {
        txAudioTimer.stop();
    }

    if (rxAudioThread) {
        rxAudioThread->quit();
        rxAudioThread->wait();
    }

    if (txAudioThread) {
        txAudioThread->quit();
        txAudioThread->wait();
    }
}



#define TXAUDIO_SIZE 0x18
void udpAudio::sendTxAudio()
{
    quint8 p[TXAUDIO_SIZE];
    memset(p, 0x0, sizeof(p));
    qToLittleEndian(myId, p + 0x08);
    qToLittleEndian(remoteId, p + 0x0c);
    p[0x10] = 0x80;

    if (txaudio->chunkAvailable) {
        QByteArray audio;
        txaudio->getNextAudioChunk(audio);
        int counter = 0;
        while (counter < audio.length()) {
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
            sendTrackedPacket(tx);
            sendAudioSeq++;
        }
    }
}

void udpAudio::changeBufferSize(quint16 value)
{
    emit haveChangeBufferSize(value);
}



void udpAudio::dataReceived()
{
    while (udp->hasPendingDatagrams()) {
        QNetworkDatagram datagram = udp->receiveDatagram();
        //qDebug() << "Received: " << datagram.data();
        QByteArray r = datagram.data();

        switch (r.length())
        {
        case (16): // Response to idle packet handled in udpBase
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
                // First check if we are missing any packets as seq should be sequential.
                uint16_t gotSeq = qFromLittleEndian<quint16>(r.mid(6, 2));
                if (lastReceivedSeq == 0 || lastReceivedSeq > gotSeq) {
                    lastReceivedSeq = gotSeq;
                }

                for (uint16_t f = lastReceivedSeq+1 ; f < gotSeq; f++) {
                    // Do we need to request a retransmit?
                    qDebug() << this->metaObject()->className() << ": Missing Sequence: (" << r.length() << ") " << f;
                }

                lastReceivedSeq = gotSeq;

                rxaudio->incomingAudio(r.mid(24));
            }
            break;
        }

        udpBase::dataReceived(r); // Call parent function to process the rest.
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
    myId = (addr >> 8 & 0xff) << 24 | (addr & 0xff) << 16 | (localPort & 0xffff);
}

udpBase::~udpBase()
{
    qDebug() << "Closing UDP stream :" << radioIP.toString() << ":" << port;
    if (udp != Q_NULLPTR) {
        sendControl(false, 0x05, 0x00); // Send disconnect
        udp->close();
        delete udp;
    }
    if (pingTimer.isActive())
    {
        pingTimer.stop();
    }
    if (idleTimer.isActive())
    {
        idleTimer.stop();
    }
}

// Base class!

void udpBase::dataReceived(QByteArray r)
{
    switch (r.length())
    {
    case (16): // Empty response used for simple comms and retransmit requests.
        if (r.mid(0, 8) == QByteArrayLiteral("\x10\x00\x00\x00\x04\x00\x00\x00")) {
            // If timer is active, stop it as they are obviously there!
            qDebug() << this->metaObject()->className() << ": Received I am here";
            areYouThereCounter = 0;
            // I don't think that we will ever receive an "I am here" other than in response to "Are you there?"
            remoteId = qFromLittleEndian<quint32>(r.mid(8, 4));
            sendControl(false,0x06,0x01); // Send Are you ready - untracked.
        } 
        else if (r.mid(0, 6) == QByteArrayLiteral("\x10\x00\x00\x00\x00\x00")) {   
            // Just get the seqnum and ignore the rest.
            lastReceivedSeq = qFromLittleEndian<quint16>(r.mid(6, 2));
        }
        else if (r.mid(0, 6) == QByteArrayLiteral("\x10\x00\x00\x00\x01\x00")) {
            // retransmit request
            // Send an idle with the requested seqnum if not found.
            uint16_t gotSeq = qFromLittleEndian<quint16>(r.mid(6, 2));
            bool found=false;
            for (int f = txSeqBuf.length() - 1; f >= 0; f--)
            {
                packetsLost++;
                if (txSeqBuf[f].seqNum == gotSeq) {
                    //qDebug() << this->metaObject()->className() << ": retransmitting packet :" << gotSeq << " (len=" << txSeqBuf[f].data.length() << ")";
                    QMutexLocker locker(&mutex);
                    udp->writeDatagram(txSeqBuf[f].data, radioIP, port);
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                // Packet was not found in buffer
                //qDebug() << this->metaObject()->className() << ": Could not find requested packet " << gotSeq << ", sending idle.";
                sendControl(false, 0, gotSeq);
            }
        }
        else if (r.mid(0, 6) == QByteArrayLiteral("\x18\x00\x00\x00\x01\x00"))
        {   // retransmit range request, can contain multiple ranges.
            for (int f = 16; f < r.length() - 4; f = f + 4)
            {
                quint16 start = qFromLittleEndian<quint16>(r.mid(f, 2));
                quint16 end = qFromLittleEndian<quint16>(r.mid(f + 2, 2));
                packetsLost=packetsLost + (end-start);
                qDebug() << this->metaObject()->className() << ": Retransmit range request for:" << start << " to " << end;
                for (quint16 gotSeq = start; gotSeq <= end; gotSeq++)
                {
                    bool found=false;
                    for (int h = txSeqBuf.length() - 1; h >= 0; h--)
                        if (txSeqBuf[h].seqNum == gotSeq) {
                            //qDebug() << this->metaObject()->className() << ": retransmitting packet :" << gotSeq << " (len=" << txSeqBuf[f].data.length() << ")";
                            QMutexLocker locker(&mutex);
                            udp->writeDatagram(txSeqBuf[h].data, radioIP, port);
                            found = true;
                            break;
                        }
                    if (!found)
                    {
                        //qDebug() << this->metaObject()->className() << ": Could not find requested packet " << gotSeq << ", sending idle.";
                        sendControl(false, 0, gotSeq);
                    }
                }
            }
        }
        break;

    case (21): 
        if (r.mid(1, 5) == QByteArrayLiteral("\x00\x00\x00\x07\x00"))
        {
            // It is a ping request/response
            uint16_t gotSeq = qFromLittleEndian<quint16>(r.mid(6, 2));

            if (r[0x10] == (char)0x00)
            {
                quint8 p[0x15];
                memset(p, 0x0, sizeof(p));
                qToLittleEndian(sizeof(p), p + 0x00);
                p[0x04] = 0x07;
                qToLittleEndian(myId, p + 0x08);
                qToLittleEndian(remoteId, p + 0x0c);
                p[0x10] = (char)0x01;
                p[0x11] = (char)r[0x11];
                p[0x12] = (char)r[0x12];
                p[0x13] = (char)r[0x13];
                p[0x14] = (char)r[0x14];

                QMutexLocker locker(&mutex);
                udp->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), radioIP, port);
            }
            else if (r[0x10] == (char)0x01) {
                if (gotSeq == pingSendSeq)
                {
                    // This is response to OUR request so increment counter
                    pingSendSeq++;
                }
                else {
                    // Not sure what to do here, need to spend more time with the protocol but try sending ping with same seq next time?
                    //qDebug() << "Received out-of-sequence ping response. Sent:" << pingSendSeq << " received " << gotSeq;
                }

            }
            else {
                qDebug() << "Unhandled response to ping. I have never seen this! 0x10=" << r[16];
            }

        }
        break;
    default:
        break;

    }

}

#define CONTROL_SIZE 0x10
// Used to send idle and other "control" style messages
void udpBase::sendControl(bool tracked=true, quint8 id=0, quint16 seq=0)
{
    quint8 p[CONTROL_SIZE];
    memset(p, 0x0, sizeof(p));
    qToLittleEndian((quint16)sizeof(p), p + 0x00);
    qToLittleEndian(myId, p + 0x08);
    qToLittleEndian(remoteId, p + 0x0c);
    p[0x04] = id;

    lastControlPacketSentTime = QDateTime::currentDateTime(); // Is this used?
    if (!tracked) {
        qToLittleEndian(seq, p + 0x06);
        QMutexLocker locker(&mutex);
        udp->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), radioIP, port);
    }
    else {
        sendTrackedPacket(QByteArray::fromRawData((const char*)p, sizeof(p)));
    }
    if (idleTimer.isActive()) {
        idleTimer.start(IDLE_PERIOD); // Reset idle counter if it's running
    }
    return;
}

#define PING_SIZE 0x15
// Send periodic ping packets
void udpBase::sendPing()
{
    quint32 pingSeq = (quint32)((quint8)(rand() & 0xff)) | (quint16)innerSendSeq << 8 | (quint8)0x06 << 24;

    quint8 p[PING_SIZE];
    memset(p, 0x0, sizeof(p));
    qToLittleEndian((quint16)sizeof(p), p + 0x00);
    qToLittleEndian(myId, p + 0x08);
    qToLittleEndian(remoteId, p + 0x0c);
    qToLittleEndian(pingSeq, p + 0x11);

    lastPingSentTime = QDateTime::currentDateTime();
    QMutexLocker locker(&mutex);
    udp->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), radioIP, port);
    innerSendSeq++;
    return;
}


void udpBase::sendTrackedPacket(QByteArray d)
{
    // As the radio can request retransmission of these packets, store them in a buffer (eventually!)
    d[6] = sendSeq & 0xff;
    d[7] = (sendSeq >> 8) & 0xff;
    SEQBUFENTRY s;
    s.seqNum = sendSeq;
    s.timeSent = time(NULL);
    s.data = (d);
    txSeqBuf.append(s);
    purgeOldEntries(); // Delete entries older than PURGE_SECONDS seconds (currently 5)
    sendSeq++;

    QMutexLocker locker(&mutex);
    udp->writeDatagram(d, radioIP, port);
    if (idleTimer.isActive()) {
        idleTimer.start(IDLE_PERIOD); // Reset idle counter if it's running
    }
    packetsSent++;
    return;
}

void udpBase::purgeOldEntries()
{
    for (int f = txSeqBuf.length() - 1; f >= 0; f--)
    {
        if (difftime(time(NULL), txSeqBuf[f].timeSent) > PURGE_SECONDS)
        {
            txSeqBuf.removeAt(f);
        }
    }
}

/// <summary>
/// passcode function used to generate secure (ish) code
/// </summary>
/// <param name="str"></param>
/// <returns>pointer to encoded username or password</returns>
void passcode(QString in, QByteArray& out)
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
    QByteArray ba = in.toLocal8Bit();
    uchar* ascii = (uchar*)ba.constData();
    for (int i = 0; i < in.length() && i < 16; i++)
    {
        int p = ascii[i] + i;
        if (p > 126) 
        {
            p = 32 + p % 127;
        }
        out.append(sequence[p]);
    }
    return;
}

/// <summary>
/// returns a QByteArray of a null terminated string
/// </summary>
/// <param name="c"></param>
/// <param name="s"></param>
/// <returns></returns>
QByteArray parseNullTerminatedString(QByteArray c, int s)
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

