// Copyright 2021 Phil Taylor M0VSE
// This code is heavily based on "Kappanhang" by HA2NON, ES1AKOS and W6EL!

#include "udphandler.h"
#include "logcategories.h"
udpHandler::udpHandler(QString ip, quint16 controlPort, quint16 civPort, quint16 audioPort, QString username, QString password, 
                            quint16 rxlatency, quint16 txlatency, quint16 rxsample, quint8 rxcodec, quint16 txsample, quint8 txcodec) :
    controlPort(controlPort),
    civPort(civPort),
    audioPort(audioPort)
{

    this->port = this->controlPort;
    this->username = username;
    this->password = password;
    this->rxLatency = rxlatency;
    this->txLatency = txlatency;
    this->rxSampleRate = rxsample;
    this->txSampleRate = txsample;
    this->rxCodec = rxcodec;
    this->txCodec = txcodec;

    qDebug(logUdp()) << "Starting udpHandler user:" << username << " rx latency:" << rxLatency  << " tx latency:" << txLatency << " rx sample rate: " << rxsample <<
        " rx codec: " << rxcodec << " tx sample rate: " << txsample << " tx codec: " << txcodec;

    // Try to set the IP address, if it is a hostname then perform a DNS lookup.
    if (!radioIP.setAddress(ip))
    {
        QHostInfo remote = QHostInfo::fromName(ip);
        foreach(QHostAddress addr, remote.addresses())
        {
            if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
                radioIP = addr;
                qDebug(logUdp()) << "Got IP Address :" << ip << ": " << addr.toString();
                break;
            }
        }
        if (radioIP.isNull())
        { 
            qDebug(logUdp()) << "Error obtaining IP Address for :" << ip << ": " << remote.errorString();
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
    connect(areYouThereTimer, &QTimer::timeout, this, std::bind(&udpBase::sendControl, this, false, 0x03, 0));
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
        qDebug(logUdp()) << "Sending token removal packet";
        sendToken(0x01);
        if (tokenTimer != Q_NULLPTR)
        {
            tokenTimer->stop();
            delete tokenTimer;
        }
        if (watchdogTimer != Q_NULLPTR)
        {
            watchdogTimer->stop();
            delete watchdogTimer;
        }


    }
}


void udpHandler::changeLatency(quint16 value)
{
    emit haveChangeLatency(value);
}

void udpHandler::receiveFromCivStream(QByteArray data)
{
    emit haveDataFromPort(data);
}

void udpHandler::receiveAudioData(const AUDIOPACKET &data)
{
    emit haveAudioData(data);
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
        lastReceived = QTime::currentTime();
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
                        // send ping packets every second
                        areYouThereTimer->stop();
                        pingTimer->start(PING_PERIOD);
                        idleTimer->start(IDLE_PERIOD);
                    }
                }
                // This is "I am ready" in response to "Are you ready" so send login.
                else if (in->type == 0x06)
                {
                    qDebug(logUdp()) << this->metaObject()->className() << ": Received I am ready";
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
                    quint32 totallost = packetsLost;
                    if (audio != Q_NULLPTR) {
                        totalsent = totalsent + audio->packetsSent;
                        totallost = totallost + audio->packetsLost;
                    }
                    if (civ != Q_NULLPTR) {
                        totalsent = totalsent + civ->packetsSent;
                        totallost = totallost + civ->packetsLost;
                    }

                    emit haveNetworkStatus(" rtt: " + QString::number(latency) + " ms, loss: (" + QString::number(totallost) + "/" + QString::number(totalsent) + ")");
                }
                break;
            }
            case (TOKEN_SIZE): // Response to Token request
            {
                token_packet_t in = (token_packet_t)r.constData();
                if (in->res == 0x05 && in->type != 0x01)
                {
                    if (in->response == 0x0000)
                    {
                        qDebug(logUdp()) << this->metaObject()->className() << ": Token renewal successful";
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
                if (in->type != 0x01) {
                    if (in->error == 0x00ffffff && !streamOpened)
                    {
                        emit haveNetworkError(radioIP.toString(), "Auth failed, try rebooting the radio.");
                        qDebug(logUdp()) << this->metaObject()->className() << ": Auth failed, try rebooting the radio.";
                    }
                    else if (in->error == 0x00000000 && in->disc == 0x01)
                    {
                        emit haveNetworkError(radioIP.toString(), "Got radio disconnected.");
                        qDebug(logUdp()) << this->metaObject()->className() << ": Got radio disconnected.";
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
                }
                break;
            }
            case(LOGIN_RESPONSE_SIZE): // Response to Login packet.
            {
                login_response_packet_t in = (login_response_packet_t)r.constData();
                if (in->type != 0x01) {
                    if (in->error == 0xfeffffff)
                    {
                        emit haveNetworkStatus("Invalid Username/Password");
                        qDebug(logUdp()) << this->metaObject()->className() << ": Invalid Username/Password";
                    }
                    else if (!isAuthenticated)
                    {

                        if (in->tokrequest == tokRequest)
                        {
                            emit haveNetworkStatus("Radio Login OK!");
                            qDebug(logUdp()) << this->metaObject()->className() << ": Received matching token response to our request";
                            token = in->token;
                            sendToken(0x02);
                            tokenTimer->start(TOKEN_RENEWAL); // Start token request timer
                            isAuthenticated = true;
                        }
                        else
                        {
                            qDebug(logUdp()) << this->metaObject()->className() << ": Token response did not match, sent:" << tokRequest << " got " << in->tokrequest;
                        }
                    }

                    if (!strcmp(in->connection, "FTTH"))
                    {
                        highBandwidthConnection = true;
                    }

                    qDebug(logUdp()) << this->metaObject()->className() << ": Detected connection speed " << in->connection;
                }
                break;
            }
            case (CONNINFO_SIZE):
            {
                conninfo_packet_t in = (conninfo_packet_t)r.constData();
                if (in->type != 0x01) {
                    devName = in->name;
                    QHostAddress ip = QHostAddress(qToBigEndian(in->ipaddress));
                    if (!streamOpened && in->busy)
                    {
                        if (in->ipaddress != 0x00 && strcmp(in->computer, compName.toLocal8Bit()))
                        {
                            emit haveNetworkStatus(devName + " in use by: " + in->computer + " (" + ip.toString() + ")");
                            sendControl(false, 0x00, in->seq); // Respond with an idle
                        }
                        else {
                            civ = new udpCivData(localIP, radioIP, civPort);
                            audio = new udpAudio(localIP, radioIP, audioPort, rxLatency, txLatency, rxSampleRate, rxCodec, txSampleRate, txCodec);

                            QObject::connect(civ, SIGNAL(receive(QByteArray)), this, SLOT(receiveFromCivStream(QByteArray)));
                            QObject::connect(audio, SIGNAL(haveAudioData(AUDIOPACKET)), this, SLOT(receiveAudioData(AUDIOPACKET)));
                            QObject::connect(this, SIGNAL(haveChangeLatency(quint16)), audio, SLOT(changeLatency(quint16)));


                            streamOpened = true;

                            emit haveNetworkStatus(devName);

                            qDebug(logUdp()) << this->metaObject()->className() << "Got serial and audio request success, device name: " << devName;

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
                    else if (streamOpened) 
                    /* If another client connects/disconnects from the server, the server will emit 
                        a CONNINFO packet, send our details to confirm we still want the stream */
                    {
                        // Received while stream is open.
                        sendRequestStream();
                    }
                }
                break;
            }
    
            case (CAPABILITIES_SIZE):
            {
                capabilities_packet_t in = (capabilities_packet_t)r.constData();
                if (in->type != 0x01)
                {
                    audioType = in->audio;
                    devName = in->name;
                    //replyId = r.mid(0x42, 16);
                    qDebug(logUdp()) << this->metaObject()->className() << "Received radio capabilities, Name:" <<
                        devName << " Audio:" <<
                        audioType;
                }
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

    conninfo_packet p;
    memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
    p.len = sizeof(p);
    p.sentid = myId;
    p.rcvdid = remoteId;
    p.code = 0x0180;
    p.res = 0x03;
    p.commoncap = 0x8010;
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
    p.txbuffer = qToBigEndian((quint32)txLatency);

    authInnerSendSeq++;

    sendTrackedPacket(QByteArray::fromRawData((const char*)p.packet, sizeof(p)));
    return;
}

void udpHandler::sendAreYouThere()
{
    if (areYouThereCounter == 20)
    {
        qDebug(logUdp()) << this->metaObject()->className() << ": Radio not responding.";
        emit haveNetworkStatus("Radio not responding!");
    }
    qDebug(logUdp()) << this->metaObject()->className() << ": Sending Are You There...";

    areYouThereCounter++;
    udpBase::sendControl(false,0x03,0x00);
}

void udpHandler::sendLogin() // Only used on control stream.
{

    qDebug(logUdp()) << this->metaObject()->className() << ": Sending login packet";

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
    qDebug(logUdp()) << this->metaObject()->className() << "Sending Token request: " << magic;

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
    // The radio should request a repeat of the token renewal packet via retransmission!
    //tokenTimer->start(100); // Set 100ms timer for retry (this will be cancelled if a response is received)
    return;
}


// Class that manages all Civ Data to/from the rig
udpCivData::udpCivData(QHostAddress local, QHostAddress ip, quint16 civPort) 
{
    qDebug(logUdp()) << "Starting udpCivData";
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
    areYouThereTimer = new QTimer();
    startCivDataTimer = new QTimer();
    watchdogTimer = new QTimer();

    connect(pingTimer, &QTimer::timeout, this, &udpBase::sendPing);
    connect(watchdogTimer, &QTimer::timeout, this, &udpCivData::watchdog);
    connect(idleTimer, &QTimer::timeout, this, std::bind(&udpBase::sendControl, this, true, 0, 0));
    connect(startCivDataTimer, &QTimer::timeout, this, std::bind(&udpCivData::sendOpenClose, this, false));
    connect(areYouThereTimer, &QTimer::timeout, this, std::bind(&udpBase::sendControl, this, false, 0x03, 0));
    watchdogTimer->start(WATCHDOG_PERIOD);
    // Start sending are you there packets - will be stopped once "I am here" received
    // send ping packets every 100 ms (maybe change to less frequent?)
    pingTimer->start(PING_PERIOD);
    // Send idle packets every 100ms, this timer will be reset everytime a non-idle packet is sent.
    idleTimer->start(IDLE_PERIOD);
    areYouThereTimer->start(AREYOUTHERE_PERIOD);
}

udpCivData::~udpCivData() 
{
    sendOpenClose(true);
    if (startCivDataTimer != Q_NULLPTR)
    {
        startCivDataTimer->stop();
        delete startCivDataTimer;
        startCivDataTimer = Q_NULLPTR;
    }
}

void udpCivData::watchdog()
{
    static bool alerted = false;
    if (lastReceived.msecsTo(QTime::currentTime()) > 500)
    {
        if (!alerted) {
            qDebug(logUdp()) << " CIV Watchdog: no CIV data received for 500ms, requesting data start.";
            if (startCivDataTimer != Q_NULLPTR)
            {
                startCivDataTimer->start(100);
            }
            alerted = true;
        }
    }
    else
    {
        alerted = false;
    }
}

void udpCivData::send(QByteArray d)
{
    // qDebug(logUdp()) << "Sending: (" << d.length() << ") " << d;
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
    uint8_t magic = 0x04;

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
        //qDebug(logUdp()) << "Received: " << datagram.data();
        QByteArray r = datagram.data();

        switch (r.length())
        {
            case (CONTROL_SIZE): // Control packet
            {
                control_packet_t in = (control_packet_t)r.constData();
                if (in->type == 0x04)
                {
                    areYouThereTimer->stop();
                }
                else if (in->type == 0x06)
                {
                    // Update remoteId
                    remoteId = in->sentid;
                    // Manually send a CIV start request and start the timer if it isn't received.
                    // The timer will be stopped as soon as valid CIV data is received.
                    sendOpenClose(false);
                    if (startCivDataTimer != Q_NULLPTR) {
                        startCivDataTimer->start(100);
                    }
                }
                break;
            }
            default:
            {
                if (r.length() > 21) {
                    data_packet_t in = (data_packet_t)r.constData();
                    if (in->type != 0x01) {
                        // Process this packet, any re-transmit requests will happen later.
                        //uint16_t gotSeq = qFromLittleEndian<quint16>(r.mid(6, 2));
                        // We have received some Civ data so stop sending Start packets!
                        if (startCivDataTimer != Q_NULLPTR) {
                            startCivDataTimer->stop();
                        }
                        lastReceived = QTime::currentTime();
                        if (quint16(in->datalen + 0x15) == (quint16)in->len)
                        {
                            emit receive(r.mid(0x15));
                        }

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
udpAudio::udpAudio(QHostAddress local, QHostAddress ip, quint16 audioPort, quint16 rxlatency, quint16 txlatency, quint16 rxsample, quint8 rxcodec, quint16 txsample, quint8 txcodec)
{
    qDebug(logUdp()) << "Starting udpAudio";
    this->localIP = local;
    this->port = audioPort;
    this->radioIP = ip;
    this->rxLatency = rxlatency;
    this->txLatency = txlatency;
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

    qRegisterMetaType<AUDIOPACKET>();
    connect(this, SIGNAL(haveAudioData(AUDIOPACKET)), rxaudio, SLOT(incomingAudio(AUDIOPACKET)));
    connect(this, SIGNAL(haveChangeLatency(quint16)), rxaudio, SLOT(changeLatency(quint16)));
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
    connect(txAudioThread, SIGNAL(finished()), txaudio, SLOT(deleteLater()));
    
    rxAudioThread->start();

    txAudioThread->start();

    sendControl(false, 0x03, 0x00); // First connect packet

    pingTimer = new QTimer();
    connect(pingTimer, &QTimer::timeout, this, &udpBase::sendPing);
    pingTimer->start(PING_PERIOD); // send ping packets every 100ms

    emit setupTxAudio(txNumSamples, txChannelCount, txSampleRate, txLatency, txIsUlawCodec, true);
    emit setupRxAudio(rxNumSamples, rxChannelCount, rxSampleRate, txLatency, rxIsUlawCodec, false);

    watchdogTimer = new QTimer();
    connect(watchdogTimer, &QTimer::timeout, this, &udpAudio::watchdog);
    watchdogTimer->start(WATCHDOG_PERIOD);

    txAudioTimer = new QTimer();
    txAudioTimer->setTimerType(Qt::PreciseTimer);
    connect(txAudioTimer, &QTimer::timeout, this, &udpAudio::sendTxAudio);

    areYouThereTimer = new QTimer();
    connect(areYouThereTimer, &QTimer::timeout, this, std::bind(&udpBase::sendControl, this, false, 0x03, 0));
    areYouThereTimer->start(AREYOUTHERE_PERIOD);
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

void udpAudio::watchdog()
{
    static bool alerted = false;
    if (lastReceived.msecsTo(QTime::currentTime()) > 500)
    {
        if (!alerted) {
            /* Just log it at the moment, maybe try signalling the control channel that it needs to 
                try requesting civ/audio again? */

            qDebug(logUdp()) << " Audio Watchdog: no audio data received for 500ms, restart required";
            alerted = true;
        }
    }
    else
    {
        alerted = false;
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
            audio_packet p;
            memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
            p.len = sizeof(p) + partial.length();
            p.sentid = myId;
            p.rcvdid = remoteId;
            p.ident = 0x0080; // TX audio is always this?
            p.datalen = (quint16)qToBigEndian((quint16)partial.length());
            p.sendseq = (quint16)qToBigEndian((quint16)sendAudioSeq); // THIS IS BIG ENDIAN!
            QByteArray tx = QByteArray::fromRawData((const char*)p.packet, sizeof(p));
            tx.append(partial);
            len = len + partial.length();
            //qDebug(logUdp()) << "Sending audio packet length: " << tx.length();
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



void udpAudio::dataReceived()
{
    while (udp->hasPendingDatagrams()) {
        QNetworkDatagram datagram = udp->receiveDatagram();
        //qDebug(logUdp()) << "Received: " << datagram.data();
        QByteArray r = datagram.data();

        switch (r.length())
        {
            case (16): // Response to control packet handled in udpBase
            {
                control_packet_t in = (control_packet_t)r.constData();
                if (in->type == 0x04)
                {
                    txAudioTimer->start(TXAUDIO_PERIOD);
                }

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
                if (in->type != 0x01) {
                    if (r.mid(0, 2) == QByteArrayLiteral("\x6c\x05") ||
                        r.mid(0, 2) == QByteArrayLiteral("\x44\x02") ||
                        r.mid(0, 2) == QByteArrayLiteral("\xd8\x03") ||
                        r.mid(0, 2) == QByteArrayLiteral("\x70\x04"))
                    {
                        lastReceived = QTime::currentTime();
                        AUDIOPACKET tempAudio;
                        tempAudio.seq = in->seq;
                        tempAudio.time = lastReceived;
                        tempAudio.sent = 0;
                        tempAudio.data = r.mid(0x18);
                        // Prefer signal/slot to forward audio as it is thread/safe
                        // Need to do more testing but latency appears fine.
                        emit haveAudioData(tempAudio);
                        //rxaudio->incomingAudio(tempAudio);
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



void udpBase::init()
{
    timeStarted.start();
    udp = new QUdpSocket(this);
    udp->bind(); // Bind to random port.
    localPort = udp->localPort();
    qDebug(logUdp()) << "UDP Stream bound to local port:" << localPort << " remote port:" << port;
    uint32_t addr = localIP.toIPv4Address();
    myId = (addr >> 8 & 0xff) << 24 | (addr & 0xff) << 16 | (localPort & 0xffff);

    retransmitTimer = new QTimer();
    connect(retransmitTimer, &QTimer::timeout, this, &udpBase::sendRetransmitRequest);
    retransmitTimer->start(RETRANSMIT_PERIOD);

}

udpBase::~udpBase()
{
    qDebug(logUdp()) << "Closing UDP stream :" << radioIP.toString() << ":" << port;
    if (udp != Q_NULLPTR) {
        sendControl(false, 0x05, 0x00); // Send disconnect
        udp->close();
        delete udp;
    }
    if (areYouThereTimer != Q_NULLPTR)
    {
        areYouThereTimer->stop();
        delete areYouThereTimer;
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
    if (retransmitTimer != Q_NULLPTR)
    {
        retransmitTimer->stop();
        delete retransmitTimer;
    }

    pingTimer = Q_NULLPTR;
    idleTimer = Q_NULLPTR;
    areYouThereTimer = Q_NULLPTR;
    retransmitTimer = Q_NULLPTR;

}

// Base class!

void udpBase::dataReceived(QByteArray r)
{
    if (r.length() < 0x10)
    {
        return; // Packet too small do to anything with?
    }

    switch (r.length())
    {
        case (CONTROL_SIZE): // Empty response used for simple comms and retransmit requests.
        {
            control_packet_t in = (control_packet_t)r.constData();
            if (in->type == 0x01)
            {
                // Single packet request
                packetsLost++;

                auto match = std::find_if(txSeqBuf.begin(), txSeqBuf.end(), [&cs = in->seq](SEQBUFENTRY& s) {
                    return s.seqNum == cs;
                });

                if (match != txSeqBuf.end()) {
                    // Found matching entry?
                    // Send "untracked" as it has already been sent once.
                    // Don't constantly retransmit the same packet, give-up eventually
                    QMutexLocker locker(&mutex);
                    //qDebug(logUdp()) << this->metaObject()->className() << ": Sending retransmit of " << hex << match->seqNum;
                    match->retransmitCount++;
                    udp->writeDatagram(match->data, radioIP, port);
                }
            }
            if (in->type == 0x04) {
                qDebug(logUdp()) << this->metaObject()->className() << ": Received I am here";
                areYouThereCounter = 0;
                // I don't think that we will ever receive an "I am here" other than in response to "Are you there?"
                remoteId = in->sentid;
                if (areYouThereTimer != Q_NULLPTR && areYouThereTimer->isActive()) {
                    // send ping packets every second
                    areYouThereTimer->stop();
                }
                sendControl(false, 0x06, 0x01); // Send Are you ready - untracked.
            }
            else if (in->type == 0x06)
            {
                // Just get the seqnum and ignore the rest.
            }
            break;
        }
        case (PING_SIZE): // ping packet
        {
            ping_packet_t in = (ping_packet_t)r.constData();
            if (in->type == 0x07)
            {
                // It is a ping request/response
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
                else if (in->reply == 0x01) {
                    if (in->seq == pingSendSeq)
                    {
                        // This is response to OUR request so increment counter
                        pingSendSeq++;
                    }
                    else {
                        // Not sure what to do here, need to spend more time with the protocol but will try sending ping with same seq next time.
                        //qDebug(logUdp()) << this->metaObject()->className() << "Received out-of-sequence ping response. Sent:" << pingSendSeq << " received " << in->seq;
                    }
                }
                else {
                    qDebug(logUdp()) << this->metaObject()->className() << "Unhandled response to ping. I have never seen this! 0x10=" << r[16];
                }
    
            }
            break;
        }
        default:
        {

            // All packets "should" be added to the incoming buffer.
            // First check that we haven't already received it.
            

        }
        break;

    }


    // All packets except ping and retransmit requests should trigger this.
    control_packet_t in = (control_packet_t)r.constData();
    
    // This is a variable length retransmit request!
    if (in->type == 0x01 && in->len != 0x10)
    {
        for (quint16 i = 0x10; i < r.length(); i = i + 2)
        {
            quint16 seq = (quint8)r[i] | (quint8)r[i + 1] << 8;
            auto match = std::find_if(txSeqBuf.begin(), txSeqBuf.end(), [&cs = seq](SEQBUFENTRY& s) {
                return s.seqNum == cs;
            });
            if (match == txSeqBuf.end()) {
                qDebug(logUdp()) << this->metaObject()->className() << ": Requested packet " << hex << seq << " not found";
                // Just send idle packet.
                sendControl(false, 0, match->seqNum);
            }
            else {
                // Found matching entry?
                // Send "untracked" as it has already been sent once.
                //qDebug(logUdp()) << this->metaObject()->className() << ": Sending retransmit (range) of " << hex << match->seqNum;
                match->retransmitCount++;
                QMutexLocker locker(&mutex);
                udp->writeDatagram(match->data, radioIP, port);
                match++;
                packetsLost++;
            }
        }
    } 
    else if (in->len != PING_SIZE && in->type == 0x00 && in->seq != 0x00)
    {
        if (rxSeqBuf.isEmpty()) {
            QMutexLocker locker(&mutex);
            rxSeqBuf.append(in->seq);
        } 
        else
        {
            std::sort(rxSeqBuf.begin(), rxSeqBuf.end());
            if (in->seq < rxSeqBuf.front())
            {
                qDebug(logUdp()) << this->metaObject()->className() << ": ******* seq number may have rolled over ****** previous highest: " << hex << rxSeqBuf.back() << " current: " << hex << in->seq;

                // Looks like it has rolled over so clear buffer and start again.
                QMutexLocker locker(&mutex);
                rxSeqBuf.clear();
                return;
            }

            if (!rxSeqBuf.contains(in->seq))
            {
                // Add incoming packet to the received buffer and if it is in the missing buffer, remove it.
                rxSeqBuf.append(in->seq);
                // Check whether this is one of our missing ones!
                auto s = std::find_if(rxMissing.begin(), rxMissing.end(), [&cs = in->seq](SEQBUFENTRY& s) { return s.seqNum == cs; });
                if (s != rxMissing.end())
                {
                    qDebug(logUdp()) << this->metaObject()->className() << ": Missing SEQ has been received! " << hex << in->seq;
                    QMutexLocker locker(&mutex);
                    s = rxMissing.erase(s);
                }
            }

        }
    }
}



void udpBase::sendRetransmitRequest()
{
    // Find all gaps in received packets and then send requests for them.
    // This will run every 100ms so out-of-sequence packets will not trigger a retransmit request.

    QByteArray missingSeqs;

    auto i = std::adjacent_find(rxSeqBuf.begin(), rxSeqBuf.end(), [](quint16 l, quint16 r) {return l + 1 < r; });
    while (i != rxSeqBuf.end())
    {
        if (i + 1 != rxSeqBuf.end())
        {
            if (*(i + 1) - *i < 30)
            {
                for (quint16 j = *i + 1; j < *(i + 1); j++)
                {
                    //qDebug(logUdp()) << this->metaObject()->className() << ": Found missing seq between " << *i << " : " << *(i + 1) << " (" << j << ")";
                    auto s = std::find_if(rxMissing.begin(), rxMissing.end(), [&cs = j](SEQBUFENTRY& s) { return s.seqNum == cs; });
                    if (s == rxMissing.end())
                    {
                        // We haven't seen this missing packet before
                        //qDebug(logUdp()) << this->metaObject()->className() << ": Adding to missing buffer (len="<< rxMissing.length() << "): " << j;
                        SEQBUFENTRY b;
                        b.seqNum = j;
                        b.retransmitCount = 0;
                        b.timeSent = QTime::currentTime();
                        QMutexLocker locker(&mutex);
                        rxMissing.append(b);
                        packetsLost++;
                    }
                    else {
                        if (s->retransmitCount == 4)
                        {
                            // We have tried 4 times to request this packet, time to give up!
                            QMutexLocker locker(&mutex);
                            s = rxMissing.erase(s);
                            rxSeqBuf.append(j); // Final thing is to add to received buffer!
                        }

                    }
                }
            }
            else {
                qDebug(logUdp()) << this->metaObject()->className() << ": Too many missing, flushing buffers";
                QMutexLocker locker(&mutex);
                rxSeqBuf.clear();
                missingSeqs.clear();
                break;
            }
        }
        i++;
    }


    for (auto it = rxMissing.begin(); it != rxMissing.end(); ++it)
    {
        if (it->retransmitCount < 10)
        {
            missingSeqs.append(it->seqNum & 0xff);
            missingSeqs.append(it->seqNum >> 8 & 0xff);
            missingSeqs.append(it->seqNum & 0xff);
            missingSeqs.append(it->seqNum >> 8 & 0xff);
            it->retransmitCount++;
        }
    }
    if (missingSeqs.length() != 0)
    {
        control_packet p;
        memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
        p.type = 0x01;
        p.seq = 0x0000;
        p.sentid = myId;
        p.rcvdid = remoteId;
        if (missingSeqs.length() == 4) // This is just a single missing packet so send using a control.
        {
            QMutexLocker locker(&mutex);
            p.seq = (missingSeqs[0] & 0xff) | (quint16)(missingSeqs[1] << 8);
            qDebug(logUdp()) << this->metaObject()->className() << ": sending request for missing packet : " << hex << p.seq;
            udp->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), radioIP, port);
        }
        else
        {
            QMutexLocker locker(&mutex);
            qDebug(logUdp()) << this->metaObject()->className() << ": sending request for multiple missing packets : " << missingSeqs.toHex();
            missingSeqs.insert(0, p.packet, sizeof(p.packet));
            udp->writeDatagram(missingSeqs, radioIP, port);
        }
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
    return;
}

void udpBase::sendRetransmitRange(quint16 first, quint16 second, quint16 third,quint16 fourth)
{
    retransmit_range_packet p;
    memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
    p.len = sizeof(p);
    p.type = 0x00;
    p.sentid = myId;
    p.rcvdid = remoteId;
    p.first = first;
    p.second = second;
    p.third = third;
    p.fourth = fourth;
    QMutexLocker locker(&mutex);
    udp->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), radioIP, port);
    return;
}


void udpBase::sendTrackedPacket(QByteArray d)
{
    // As the radio can request retransmission of these packets, store them in a buffer (eventually!)
    d[6] = sendSeq & 0xff;
    d[7] = (sendSeq >> 8) & 0xff;
    SEQBUFENTRY s;
    s.seqNum = sendSeq;
    s.timeSent = QTime::currentTime();
    s.retransmitCount = 0;
    s.data = d;
    QMutexLocker locker(&mutex);
    txSeqBuf.append(s);
    purgeOldEntries(); // Delete entries older than PURGE_SECONDS seconds (currently 5)
    sendSeq++;

    udp->writeDatagram(d, radioIP, port);
    if (idleTimer != Q_NULLPTR && idleTimer->isActive()) {
        idleTimer->start(IDLE_PERIOD); // Reset idle counter if it's running
    }
    packetsSent++;
    return;
}


/// <summary>
/// Once a packet has reached PURGE_SECONDS old (currently 10) then it is not likely to be any use.
/// </summary>
void udpBase::purgeOldEntries()
{
    // Erase old entries from the tx packet buffer
    if (!txSeqBuf.isEmpty())
    {
        txSeqBuf.erase(std::remove_if(txSeqBuf.begin(), txSeqBuf.end(), [](const SEQBUFENTRY& v)
        { return v.timeSent.secsTo(QTime::currentTime()) > PURGE_SECONDS; }), txSeqBuf.end());
    }

    // Erase old entries from the missing packets buffer
    if (!rxMissing.isEmpty()) {
        rxMissing.erase(std::remove_if(rxMissing.begin(), rxMissing.end(), [](const SEQBUFENTRY& v)
        { return v.timeSent.secsTo(QTime::currentTime()) > PURGE_SECONDS; }), rxMissing.end());
    }

    if (!rxSeqBuf.isEmpty()) {
        std::sort(rxSeqBuf.begin(), rxSeqBuf.end());

        if (rxSeqBuf.length() > 400)
        {
            rxSeqBuf.remove(0, 200);
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

