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


    // Set my computer name. Should this be configurable?
    compName = "wfview";

}

void udpHandler::init()
{
    udpBase::init(); // Perform UDP socket initialization.

    // Connect socket to my dataReceived function.
    QUdpSocket::connect(udp, &QUdpSocket::readyRead, this, &udpHandler::dataReceived);

    /*
        Connect various timers
    */
    tokenTimer = new QTimer();
    areYouThereTimer = new QTimer();
    pingTimer = new QTimer();
    idleTimer = new QTimer();

    connect(tokenTimer, &QTimer::timeout, this, std::bind(&udpHandler::sendToken, this, 0x05));
    connect(areYouThereTimer, &QTimer::timeout, this, QOverload<>::of(&udpHandler::sendAreYouThere));
    connect(pingTimer, &QTimer::timeout, this, &udpBase::sendPing);
    connect(idleTimer, &QTimer::timeout, this, std::bind(&udpBase::sendControl, this, true, 0, 0));

    // Start sending are you there packets - will be stopped once "I am here" received
    areYouThereTimer->start(AREYOUTHERE_PERIOD);
}

udpHandler::~udpHandler()
{
    if (streamOpened) {
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
        QByteArray r = datagram.data();

        switch (r.length())
        {
            case (CONTROL_SIZE): // control packet
            {
                control_packet_t in = (control_packet_t)r.constData();
                if (in->type == 0x04) {
                    // If timer is active, stop it as they are obviously there!
                    if (areYouThereTimer->isActive()) {
                        areYouThereTimer->stop();
                        // send ping packets every second
                        pingTimer->start(PING_PERIOD);
                        idleTimer->start(IDLE_PERIOD);
                    }
                }
                // This is "I am ready" in response to "Are you ready" so send login.
                else if (in->type == 0x06)
                {
                    qDebug() << this->metaObject()->className() << ": Received I am ready";
                    sendLogin(); // send login packet
                }
                break;
            }
            case (PING_SIZE): // ping packet
            {
                ping_packet_t in = (ping_packet_t)r.constData();
                if (in->type == 0x07 && in->reply == 0x01 && streamOpened)
                {
                    // This is a response to our ping request so measure latency
                    latency += lastPingSentTime.msecsTo(QDateTime::currentDateTime());
                    latency /= 2;
                    quint32 totalsent = packetsSent;
                    quint32 totallost = packetsLost / 2;
                    if (audio != Q_NULLPTR) {
                        totalsent = totalsent + audio->packetsSent;
                        totallost = totallost + audio->packetsLost / 2;
                    }
                    if (civ != Q_NULLPTR) {
                        totalsent = totalsent + civ->packetsSent;
                        totallost = totallost + civ->packetsLost / 2;
                    }
                    //double perclost = 1.0 * totallost / totalsent * 100.0 ;
                    emit haveNetworkStatus(" rtt: " + QString::number(latency) + " ms, loss: (" + QString::number(packetsLost) + "/" + QString::number(packetsSent) + ")");
                }
                break;
            }
            case (TOKEN_SIZE): // Response to Token request
            {
                token_packet_t in = (token_packet_t)r.constData();
                if (in->res == 0x05)
                {
                    if (in->response == 0x0000)
                    {
                        qDebug() << this->metaObject()->className() << ": Token renewal successful";
                        tokenTimer->start(TOKEN_RENEWAL);
                        gotAuthOK = true;
                        if (!streamOpened)
                        {
                            sendRequestStream();
                        }

                    }
                    else if (in->response == 0xffffffff)
                    {
                        qWarning() << this->metaObject()->className() << ": Radio rejected token renewal, performing login";
                        remoteId = in->sentid;
                        tokRequest = in->tokrequest;
                        token = in->token;
                        // Got new token response
                        sendToken(0x02); // Update it.
                    }
                    else
                    {
                        qWarning() << this->metaObject()->className() << ": Unknown response to token renewal? " << in->response;
                    }
                }
                break;
            }   
            case (STATUS_SIZE):  // Status packet
            {
                status_packet_t in = (status_packet_t)r.constData();
                if (in->error == 0x00ffffff && !streamOpened)
                {
                    emit haveNetworkError(radioIP.toString(), "Auth failed, try rebooting the radio.");
                    qDebug() << this->metaObject()->className() << ": Auth failed, try rebooting the radio.";
                }
                else if (in->error == 0x00000000 && in->disc == 0x01)
                {
                    emit haveNetworkError(radioIP.toString(), "Got radio disconnected.");
                    qDebug() << this->metaObject()->className() << ": Got radio disconnected.";
                    if (streamOpened) {
                        // Close stream connections but keep connection open to the radio.
                        if (audio != Q_NULLPTR) {
                            delete audio;
                        }

                        if (civ != Q_NULLPTR) {
                            delete civ;
                        }
                        streamOpened = false;
                    }
                }
                break;
            }
            case(LOGIN_RESPONSE_SIZE): // Response to Login packet.
            {
                login_response_packet_t in = (login_response_packet_t)r.constData();
                if (in->error == 0xfeffffff)
                {
                    emit haveNetworkStatus("Invalid Username/Password");
                    qDebug() << this->metaObject()->className() << ": Invalid Username/Password";
                }
                else if (!isAuthenticated)
                {
    
                    if (in->tokrequest == tokRequest)
                    {
                        emit haveNetworkStatus("Radio Login OK!");
                        qDebug() << this->metaObject()->className() << ": Received matching token response to our request";
                        token = in->token;
                        sendToken(0x02);
                        tokenTimer->start(TOKEN_RENEWAL); // Start token request timer
                        isAuthenticated = true;
                    }
                    else
                    {
                        qDebug() << this->metaObject()->className() << ": Token response did not match, sent:" << tokRequest << " got " << in->tokrequest;
                    }
                }
    
                if (!strcmp(in->connection, "FTTH"))
                {
                    highBandwidthConnection = true;
                }
    
                qDebug() << this->metaObject()->className() << ": Detected connection speed " << in->connection;
                break;
            }
            case (CONNINFO_SIZE):
            {
                conninfo_packet_t in = (conninfo_packet_t)r.constData();
    
                devName = in->name;
                QHostAddress ip = QHostAddress(qToBigEndian(in->ipaddress));
                if (!streamOpened && in->busy)
                {
                    if (strcmp(in->computer,compName.toLocal8Bit())) 
                    {
                        emit haveNetworkStatus(devName + " in use by: " + in->computer + " (" + ip.toString() + ")");
                        sendControl(false, 0x00, in->seq); // Respond with an idle
                    }
                    else {
                        civ = new udpCivData(localIP, radioIP, civPort);
                        audio = new udpAudio(localIP, radioIP, audioPort, rxBufferSize, rxSampleRate, rxCodec, txSampleRate, txCodec);

                        QObject::connect(civ, SIGNAL(receive(QByteArray)), this, SLOT(receiveFromCivStream(QByteArray)));
                        QObject::connect(this, SIGNAL(haveChangeBufferSize(quint16)), audio, SLOT(changeBufferSize(quint16)));

                        streamOpened = true;

                        emit haveNetworkStatus(devName);

                        qDebug() << this->metaObject()->className() << "Got serial and audio request success, device name: " << devName;

                        // Stuff can change in the meantime because of a previous login...
                        remoteId = in->sentid;
                        myId = in->rcvdid;
                        tokRequest = in->tokrequest;
                        token = in->token;
                    }
                }
                else if (!streamOpened && !in->busy)
                {
                    emit haveNetworkStatus(devName + " available");

                    identa = in->identa;
                    identb = in->identb;

                    sendRequestStream();
                }
                break;
            }
    
            case (CAPABILITIES_SIZE):
            {
                capabilities_packet_t in = (capabilities_packet_t)r.constData();
                audioType = in->audio;
                devName = in->name;
                //replyId = r.mid(0x42, 16);
                qDebug() << this->metaObject()->className() << "Received radio capabilities, Name:" <<
                    devName << " Audio:" <<
                    audioType;
    
                break;
            }
    
        }
        udpBase::dataReceived(r); // Call parent function to process the rest.
        r.clear();
        datagram.clear();

    }
    return;
}


void udpHandler::sendRequestStream()
{

    QByteArray usernameEncoded;
    passcode(username, usernameEncoded);
    int txSeqBufLengthMs = 300;

    conninfo_packet p;
    memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
    p.len = sizeof(p);
    p.sentid = myId;
    p.rcvdid = remoteId;
    p.code = 0x0180;
    p.res = 0x03;
    p.resb = 0x8010;
    p.identa = identa;
    p.identb = identb;
    p.innerseq = authInnerSendSeq;
    p.tokrequest = tokRequest;
    p.token = token;
    memcpy(&p.name, devName.toLocal8Bit().constData(), devName.length());
    p.rxenable = 1;
    p.txenable = 1;
    p.rxcodec = rxCodec;
    p.txcodec = txCodec;
    memcpy(&p.username, usernameEncoded.constData(), usernameEncoded.length());
    p.rxsample = qToBigEndian((quint32)rxSampleRate);
    p.txsample = qToBigEndian((quint32)txSampleRate);
    p.civport = qToBigEndian((quint32)civPort);
    p.audioport = qToBigEndian((quint32)audioPort);
    p.txbuffer = qToBigEndian((quint32)txSeqBufLengthMs);

    authInnerSendSeq++;

    sendTrackedPacket(QByteArray::fromRawData((const char*)p.packet, sizeof(p)));
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

void udpHandler::sendLogin() // Only used on control stream.
{

    qDebug() << this->metaObject()->className() << ": Sending login packet";

    tokRequest = static_cast<quint16>(rand() | rand() << 8); // Generate random token request.

    QByteArray usernameEncoded;
    QByteArray passwordEncoded;
    passcode(username, usernameEncoded);
    passcode(password, passwordEncoded);

    login_packet p;
    memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
    p.len = sizeof(p);
    p.sentid = myId;
    p.rcvdid = remoteId;
    p.code = 0x0170; // Not sure what this is?
    p.innerseq = authInnerSendSeq;
    p.tokrequest = tokRequest;
    memcpy(p.username, usernameEncoded.constData(), usernameEncoded.length());
    memcpy(p.password, passwordEncoded.constData(), passwordEncoded.length());
    memcpy(p.name, compName.toLocal8Bit().constData(), compName.length());

    authInnerSendSeq++;
    sendTrackedPacket(QByteArray::fromRawData((const char*)p.packet, sizeof(p)));
    return;
}

void udpHandler::sendToken(uint8_t magic)
{
    qDebug() << this->metaObject()->className() << "Sending Token request: " << magic;

    token_packet p;
    memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
    p.len = sizeof(p);
    p.sentid = myId;
    p.rcvdid = remoteId;
    p.code = 0x0130; // Not sure what this is?
    p.res = magic;
    p.innerseq = authInnerSendSeq;
    p.tokrequest = tokRequest;
    p.token = token;

    authInnerSendSeq++;
    sendTrackedPacket(QByteArray::fromRawData((const char *)p.packet, sizeof(p)));
    tokenTimer->start(100); // Set 100ms timer for retry (this will be cancelled if a response is received)
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
    pingTimer = new QTimer();
    idleTimer = new QTimer();

    connect(pingTimer, &QTimer::timeout, this, &udpBase::sendPing);
    connect(idleTimer, &QTimer::timeout, this, std::bind(&udpBase::sendControl, this, true, 0, 0));

    // send ping packets every 100 ms (maybe change to less frequent?)
    pingTimer->start(PING_PERIOD);
    // Send idle packets every 100ms, this timer will be reset everytime a non-idle packet is sent.
    idleTimer->start(IDLE_PERIOD);
}

udpCivData::~udpCivData() {
    sendOpenClose(true);
}

void udpCivData::send(QByteArray d)
{
    // qDebug() << "Sending: (" << d.length() << ") " << d;
    data_packet p;
    memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
    p.len = sizeof(p);
    p.sentid = myId;
    p.rcvdid = remoteId;
    p.reply = (char)0xc1;
    p.datalen = d.length();
    p.sendseq = qToBigEndian(sendSeqB); // THIS IS BIG ENDIAN!

    QByteArray t = QByteArray::fromRawData((const char*)p.packet, sizeof(p));
    t.append(d);
    sendTrackedPacket(t);
    sendSeqB++;
    return;
}


void udpCivData::sendOpenClose(bool close)
{
    uint8_t magic = 0x05;

    if (close) 
    {
        magic = 0x00;
    }

    openclose_packet p;
    memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
    p.len = sizeof(p);
    p.sentid = myId;
    p.rcvdid = remoteId;
    p.data = 0x01c0; // Not sure what other values are available:
    p.sendseq = qToBigEndian(sendSeqB);
    p.magic = magic;

    sendSeqB++;

    sendTrackedPacket(QByteArray::fromRawData((const char*)p.packet, sizeof(p)));
    return;
}



void udpCivData::dataReceived()
{
    while (udp->hasPendingDatagrams()) 
    {
        QNetworkDatagram datagram = udp->receiveDatagram();
        //qDebug() << "Received: " << datagram.data();
        QByteArray r = datagram.data();

        switch (r.length())
        {
            case (CONTROL_SIZE): // Control packet
            {
                control_packet_t in = (control_packet_t)r.constData();
                if (in->type == 0x06)
                {
                    // Update remoteId
                    remoteId = in->sentid;
                    sendOpenClose(false);
                }
                break;
            }
            default:
            {
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
                        emit receive(r.mid(0x15));
                    }
                }
                break;
            }
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

    pingTimer = new QTimer();
    connect(pingTimer, &QTimer::timeout, this, &udpBase::sendPing);
    pingTimer->start(PING_PERIOD); // send ping packets every 100ms

    emit setupTxAudio(txNumSamples, txChannelCount, txSampleRate, bufferSize, txIsUlawCodec, true);
    emit setupRxAudio(rxNumSamples, rxChannelCount, rxSampleRate, bufferSize, rxIsUlawCodec, false);

    txAudioTimer = new QTimer();
    txAudioTimer->setTimerType(Qt::PreciseTimer);
    connect(txAudioTimer, &QTimer::timeout, this, &udpAudio::sendTxAudio);
    txAudioTimer->start(TXAUDIO_PERIOD);
}

udpAudio::~udpAudio()
{
    if (txAudioTimer != Q_NULLPTR)
    {
        txAudioTimer->stop();
        delete txAudioTimer;
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



void udpAudio::sendTxAudio()
{

    if (txaudio->isChunkAvailable()) {
        QByteArray audio;
        txaudio->getNextAudioChunk(audio);
        int counter = 1;
        int len = 0;
 
        while (len < audio.length()) {
            QByteArray partial = audio.mid(len, 1364);
            txaudio_packet p;
            memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
            p.len = sizeof(p)+partial.length();
            p.sentid = myId;
            p.rcvdid = remoteId;
            p.ident = 0x0080; // TX audio is always this?
            p.datalen = (quint16)qToBigEndian((quint16)partial.length());
            p.sendseq = (quint16)qToBigEndian((quint16)sendAudioSeq); // THIS IS BIG ENDIAN!
            QByteArray tx = QByteArray::fromRawData((const char*)p.packet, sizeof(p));
            tx.append(partial);
            len = len + partial.length();
            //qDebug() << "Sending audio packet length: " << tx.length();
            sendTrackedPacket(tx);
            sendAudioSeq++;
            counter++;
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
        case (16): // Response to control packet handled in udpBase
            break;

        default:
        {
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

                for (uint16_t f = lastReceivedSeq + 1; f < gotSeq; f++) {
                    // Do we need to request a retransmit?
                    qDebug() << this->metaObject()->className() << ": Missing Sequence: (" << r.length() << ") " << f;
                }

                lastReceivedSeq = gotSeq;

                rxaudio->incomingAudio(r.mid(24));
            }
            break;
        }
        }

        udpBase::dataReceived(r); // Call parent function to process the rest.
        r.clear();
        datagram.clear();
    }
}



void udpBase::init()
{
    timeStarted.start();
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
    if (pingTimer != Q_NULLPTR)
    {
        pingTimer->stop();
        delete pingTimer;
    }
    if (idleTimer != Q_NULLPTR)
    {
        idleTimer->stop();
        delete idleTimer;
    }
    pingTimer = Q_NULLPTR;
    idleTimer = Q_NULLPTR;

}

// Base class!

void udpBase::dataReceived(QByteArray r)
{
    switch (r.length())
    {
        case (CONTROL_SIZE): // Empty response used for simple comms and retransmit requests.
        {
            control_packet_t in = (control_packet_t)r.constData();
            // We should check for missing packets here 
            // for now just store received seq.
            lastReceivedSeq = in->seq;
            if (in->type == 0x04) {
                qDebug() << this->metaObject()->className() << ": Received I am here";
                areYouThereCounter = 0;
                // I don't think that we will ever receive an "I am here" other than in response to "Are you there?"
                remoteId = in->sentid;
                sendControl(false, 0x06, 0x01); // Send Are you ready - untracked.
            }
            else if (in->type == 0x06)
            {
                // Just get the seqnum and ignore the rest.
            }
            else if (in->type == 0x01) // retransmit request
            { 
                // retransmit request
                // Send an idle with the requested seqnum if not found.
                bool found = false;
                for (int f = txSeqBuf.length() - 1; f >= 0; f--)
                {
                    packetsLost++;
                    if (txSeqBuf[f].seqNum == in->seq) {
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
                    sendControl(false, 0, in->seq);
                }
            }
            break;
        }
        case (PING_SIZE): // ping packet
        {
            ping_packet_t in = (ping_packet_t)r.constData();
            if (in->type == 0x07)
            {
                // It is a ping request/response
                //uint16_t gotSeq = qFromLittleEndian<quint16>(r.mid(6, 2));
                if (in->reply == 0x00)
                {   
                    ping_packet p;
                    memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
                    p.len = sizeof(p);
                    p.type = 0x07;
                    p.sentid = myId;
                    p.rcvdid = remoteId;
                    p.reply = 0x01;
                    p.seq = in->seq;
                    p.time = in->time;
                    QMutexLocker locker(&mutex);
                    udp->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), radioIP, port);
                }
                else if (r[0x10] == (char)0x01) {
                    if (in->seq == pingSendSeq)
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
        }
        case (0x18):
        {
            if (r.mid(0, 6) == QByteArrayLiteral("\x18\x00\x00\x00\x01\x00"))
            {   // retransmit range request, can contain multiple ranges.
                for (int f = 16; f < r.length() - 4; f = f + 4)
                {
                    quint16 start = qFromLittleEndian<quint16>(r.mid(f, 2));
                    quint16 end = qFromLittleEndian<quint16>(r.mid(f + 2, 2));
                    packetsLost = packetsLost + (end - start);
                    qDebug() << this->metaObject()->className() << ": Retransmit range request for:" << start << " to " << end;
                    for (quint16 gotSeq = start; gotSeq <= end; gotSeq++)
                    {
                        bool found = false;
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
        }
        default:
            break;

    }

}

// Used to send idle and other "control" style messages
void udpBase::sendControl(bool tracked=true, quint8 type=0, quint16 seq=0)
{
    control_packet p;
    memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
    p.len = sizeof(p);
    p.type = type;
    p.sentid = myId;
    p.rcvdid = remoteId;

    if (!tracked) {
        p.seq = seq;
        QMutexLocker locker(&mutex);
        udp->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), radioIP, port);
    }
    else {
        sendTrackedPacket(QByteArray::fromRawData((const char*)p.packet, sizeof(p)));
    }
    return;
}

// Send periodic ping packets
void udpBase::sendPing()
{
    ping_packet p;
    memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
    p.len = sizeof(p);
    p.type = 0x07;
    p.sentid = myId;
    p.rcvdid = remoteId;
    p.seq = pingSendSeq;
    p.time = timeStarted.msecsSinceStartOfDay();
    lastPingSentTime = QDateTime::currentDateTime();
    QMutexLocker locker(&mutex);
    udp->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), radioIP, port);
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
    if (idleTimer != Q_NULLPTR && idleTimer->isActive()) {
        idleTimer->start(IDLE_PERIOD); // Reset idle counter if it's running
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

