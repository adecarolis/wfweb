// Copyright 2021 Phil Taylor M0VSE
// This code is heavily based on "Kappanhang" by HA2NON, ES1AKOS and W6EL!

#include "udphandler.h"
#include "logcategories.h"

udpHandler::udpHandler(udpPreferences prefs, audioSetup rx, audioSetup tx) :
    controlPort(prefs.controlLANPort),
    civPort(0),
    audioPort(0),
    rxSetup(rx),
    txSetup(tx)
{


    this->port = this->controlPort;
    this->username = prefs.username;
    this->password = prefs.password;
    this->compName = prefs.clientName.mid(0,8) + "-wfview";

    qInfo(logUdp()) << "Starting udpHandler user:" << username << " rx latency:" << rxSetup.latency  << " tx latency:" << txSetup.latency << " rx sample rate: " << rxSetup.samplerate <<
        " rx codec: " << rxSetup.codec << " tx sample rate: " << txSetup.samplerate << " tx codec: " << txSetup.codec;

    // Try to set the IP address, if it is a hostname then perform a DNS lookup.
    if (!radioIP.setAddress(prefs.ipAddress))
    {
        QHostInfo remote = QHostInfo::fromName(prefs.ipAddress);
        foreach(QHostAddress addr, remote.addresses())
        {
            if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
                radioIP = addr;
                qInfo(logUdp()) << "Got IP Address :" << prefs.ipAddress << ": " << addr.toString();
                break;
            }
        }
        if (radioIP.isNull())
        { 
            qInfo(logUdp()) << "Error obtaining IP Address for :" << prefs.ipAddress << ": " << remote.errorString();
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
        qInfo(logUdp()) << "Sending token removal packet";
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

void udpHandler::setVolume(unsigned char value)
{
    emit haveSetVolume(value);
}

void udpHandler::receiveFromCivStream(QByteArray data)
{
    emit haveDataFromPort(data);
}

void udpHandler::receiveAudioData(const audioPacket &data)
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
                    qInfo(logUdp()) << this->metaObject()->className() << ": Received I am here from: " <<datagram.senderAddress();

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
                    qInfo(logUdp()) << this->metaObject()->className() << ": Received I am ready";
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

                    QString tempLatency;
                    if (rxSetup.latency > audio->audioLatency)
                    {
                        tempLatency = QString("%1 ms").arg(audio->audioLatency,3);
                    }
                    else {
                        tempLatency = QString("<span style = \"color:red\">%1 ms</span>").arg(audio->audioLatency,3);
                    }
                    QString txString="";
                    if (txSetup.codec == 0) {
                        txString = "(no tx)";
                    }
                    emit haveNetworkStatus(QString("<pre>%1 rx latency: %2 / rtt: %3 ms / loss: %4/%5</pre>").arg(txString).arg(tempLatency).arg(latency, 3).arg(totallost, 3).arg(totalsent, 3));
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
                        streamOpened = false;
                        sendRequestStream();
                        // Got new token response
                        //sendToken(0x02); // Update it.
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
                    if (in->error == 0xffffffff && !streamOpened)
                    {
                        emit haveNetworkError(radioIP.toString(), "Connection failed, wait a few minutes or reboot the radio.");
                        qInfo(logUdp()) << this->metaObject()->className() << ": Connection failed, wait a few minutes or reboot the radio.";
                    }
                    else if (in->error == 0x00000000 && in->disc == 0x01)
                    {
                        emit haveNetworkError(radioIP.toString(), "Got radio disconnected.");
                        qInfo(logUdp()) << this->metaObject()->className() << ": Got radio disconnected.";
                        if (streamOpened) {
                            // Close stream connections but keep connection open to the radio.
                            if (audio != Q_NULLPTR) {
                                delete audio;
                                audio = Q_NULLPTR;
                            }

                            if (civ != Q_NULLPTR) {
                                delete civ;
                                civ = Q_NULLPTR;
                            }
                            
                            streamOpened = false;
                        }
                    }
                    else {
                        civPort = qFromBigEndian(in->civport);
                        audioPort = qFromBigEndian(in->audioport);
                    }
                }
                break;
            }
            case(LOGIN_RESPONSE_SIZE): // Response to Login packet.
            {
                login_response_packet_t in = (login_response_packet_t)r.constData();
                if (in->type != 0x01) {

                    connectionType = in->connection;
                    qInfo(logUdp()) << "Got connection type:" << connectionType;
                    if (connectionType == "FTTH")
                    {
                        highBandwidthConnection = true;
                    }

                    if (connectionType != "WFVIEW") // NOT WFVIEW
                    {
                        if (rxSetup.codec >= 0x40 || txSetup.codec >= 0x40)
                        {
                            emit haveNetworkError(QString("UDP"), QString("Opus codec not supported, forcing LPCM16"));
                            if (rxSetup.codec >= 0x40)
                                rxSetup.codec = 0x04;
                            if (txSetup.codec >= 0x40)
                                txSetup.codec = 0x04;
                        }
                    }


                    if (in->error == 0xfeffffff)
                    {
                        emit haveNetworkStatus("Invalid Username/Password");
                        qInfo(logUdp()) << this->metaObject()->className() << ": Invalid Username/Password";
                    }
                    else if (!isAuthenticated)
                    {

                        if (in->tokrequest == tokRequest)
                        {
                            emit haveNetworkStatus("Radio Login OK!");
                            qInfo(logUdp()) << this->metaObject()->className() << ": Received matching token response to our request";
                            token = in->token;
                            sendToken(0x02);
                            tokenTimer->start(TOKEN_RENEWAL); // Start token request timer
                            isAuthenticated = true;
                        }
                        else
                        {
                            qInfo(logUdp()) << this->metaObject()->className() << ": Token response did not match, sent:" << tokRequest << " got " << in->tokrequest;
                        }
                    }

                    qInfo(logUdp()) << this->metaObject()->className() << ": Detected connection speed " << in->connection;
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
    
                            // TX is not supported
                            if (txSampleRates <2 ) { 
                                txSetup.samplerate = 0;
                                txSetup.codec = 0;
                            }

                            audio = new udpAudio(localIP, radioIP, audioPort, rxSetup, txSetup);

                            QObject::connect(civ, SIGNAL(receive(QByteArray)), this, SLOT(receiveFromCivStream(QByteArray)));
                            QObject::connect(audio, SIGNAL(haveAudioData(audioPacket)), this, SLOT(receiveAudioData(audioPacket)));
                            QObject::connect(this, SIGNAL(haveChangeLatency(quint16)), audio, SLOT(changeLatency(quint16)));
                            QObject::connect(this, SIGNAL(haveSetVolume(unsigned char)), audio, SLOT(setVolume(unsigned char)));

                            streamOpened = true;

                            emit haveNetworkStatus(devName);

                            qInfo(logUdp()) << this->metaObject()->className() << "Got serial and audio request success, device name: " << devName;

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
                    civId = in->civ;
                    rxSampleRates = in->rxsample;
                    txSampleRates = in->txsample;
                    emit haveBaudRate(qFromBigEndian(in->baudrate));
                    //replyId = r.mid(0x42, 16);
                    qInfo(logUdp()) << this->metaObject()->className() << "Received radio capabilities, Name:" <<
                        devName << " Audio:" <<
                        audioType << "CIV:" << hex << civId; 

                    if (txSampleRates < 2)
                    {
                        // TX not supported
                        qInfo(logUdp()) << this->metaObject()->className() << "TX audio is disabled";
                    }
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
    p.innerseq = authSeq++;
    p.tokrequest = tokRequest;
    p.token = token;
    memcpy(&p.name, devName.toLocal8Bit().constData(), devName.length());
    p.rxenable = 1;
    if (this->txSampleRates > 1) {
        p.txenable = 1;
        p.txcodec = txSetup.codec;
    }
    p.rxcodec = rxSetup.codec;
    memcpy(&p.username, usernameEncoded.constData(), usernameEncoded.length());
    p.rxsample = qToBigEndian((quint32)rxSetup.samplerate);
    p.txsample = qToBigEndian((quint32)txSetup.samplerate);
    p.civport = qToBigEndian((quint32)civPort);
    p.audioport = qToBigEndian((quint32)audioPort);
    p.txbuffer = qToBigEndian((quint32)txSetup.latency);
    p.convert = 1;
    sendTrackedPacket(QByteArray::fromRawData((const char*)p.packet, sizeof(p)));
    return;
}

void udpHandler::sendAreYouThere()
{
    if (areYouThereCounter == 20)
    {
        qInfo(logUdp()) << this->metaObject()->className() << ": Radio not responding.";
        emit haveNetworkStatus("Radio not responding!");
    }
    qInfo(logUdp()) << this->metaObject()->className() << ": Sending Are You There...";

    areYouThereCounter++;
    udpBase::sendControl(false,0x03,0x00);
}

void udpHandler::sendLogin() // Only used on control stream.
{

    qInfo(logUdp()) << this->metaObject()->className() << ": Sending login packet";

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
    p.innerseq = authSeq++;
    p.tokrequest = tokRequest;
    memcpy(p.username, usernameEncoded.constData(), usernameEncoded.length());
    memcpy(p.password, passwordEncoded.constData(), passwordEncoded.length());
    memcpy(p.name, compName.toLocal8Bit().constData(), compName.length());

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
    p.innerseq = authSeq++;
    p.tokrequest = tokRequest;
    p.token = token;

    sendTrackedPacket(QByteArray::fromRawData((const char *)p.packet, sizeof(p)));
    // The radio should request a repeat of the token renewal packet via retransmission!
    //tokenTimer->start(100); // Set 100ms timer for retry (this will be cancelled if a response is received)
    return;
}


// Class that manages all Civ Data to/from the rig
udpCivData::udpCivData(QHostAddress local, QHostAddress ip, quint16 civPort) 
{
    qInfo(logUdp()) << "Starting udpCivData";
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
    // Send idle packets every 100ms, this timer will be reset every time a non-idle packet is sent.
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
    if (pingTimer != Q_NULLPTR)
    {
        pingTimer->stop();
        delete pingTimer;
        pingTimer = Q_NULLPTR;
    }
    if (idleTimer != Q_NULLPTR)
    {
        idleTimer->stop();
        delete idleTimer;
        idleTimer = Q_NULLPTR;
    }
    if (watchdogTimer != Q_NULLPTR)
    {
        watchdogTimer->stop();
        delete watchdogTimer;
        watchdogTimer = Q_NULLPTR;
    }
}

void udpCivData::watchdog()
{
    static bool alerted = false;
    if (lastReceived.msecsTo(QTime::currentTime()) > 2000)
    {
        if (!alerted) {
            qInfo(logUdp()) << " CIV Watchdog: no CIV data received for 2s, requesting data start.";
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
    //qInfo(logUdp()) << "Sending: (" << d.length() << ") " << d;
    data_packet p;
    memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
    p.len = sizeof(p)+d.length();
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
        //qInfo(logUdp()) << "Received: " << datagram.data();
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
                            //if (r.mid(0x15).length() != 157)
                               emit receive(r.mid(0x15));
                            //qDebug(logUdp()) << "Got incoming CIV datagram" << r.mid(0x15).length();
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
udpAudio::udpAudio(QHostAddress local, QHostAddress ip, quint16 audioPort, audioSetup rxSetup, audioSetup txSetup)
{
    qInfo(logUdp()) << "Starting udpAudio";
    this->localIP = local;
    this->port = audioPort;
    this->radioIP = ip;

    if (txSetup.samplerate == 0) {
        enableTx = false;
    }

    init(); // Perform connection

    QUdpSocket::connect(udp, &QUdpSocket::readyRead, this, &udpAudio::dataReceived);

    rxaudio = new audioHandler();
    rxAudioThread = new QThread(this);

    rxaudio->moveToThread(rxAudioThread);

    rxAudioThread->start(QThread::TimeCriticalPriority);

    connect(this, SIGNAL(setupRxAudio(audioSetup)), rxaudio, SLOT(init(audioSetup)));

    // signal/slot not currently used.
    connect(this, SIGNAL(haveAudioData(audioPacket)), rxaudio, SLOT(incomingAudio(audioPacket)));
    connect(this, SIGNAL(haveChangeLatency(quint16)), rxaudio, SLOT(changeLatency(quint16)));
    connect(this, SIGNAL(haveSetVolume(unsigned char)), rxaudio, SLOT(setVolume(unsigned char)));
    connect(rxAudioThread, SIGNAL(finished()), rxaudio, SLOT(deleteLater()));
    
    txSetup.radioChan = 1;

    txaudio = new audioHandler();
    txAudioThread = new QThread(this);

    txaudio->moveToThread(txAudioThread);
    
    txAudioThread->start(QThread::TimeCriticalPriority);

    connect(this, SIGNAL(setupTxAudio(audioSetup)), txaudio, SLOT(init(audioSetup)));

    connect(txAudioThread, SIGNAL(finished()), txaudio, SLOT(deleteLater()));

    sendControl(false, 0x03, 0x00); // First connect packet

    pingTimer = new QTimer();
    connect(pingTimer, &QTimer::timeout, this, &udpBase::sendPing);
    pingTimer->start(PING_PERIOD); // send ping packets every 100ms

    if (enableTx) {
        emit setupTxAudio(txSetup);
    }

    emit setupRxAudio(rxSetup);

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

    if (txAudioTimer != Q_NULLPTR)
    {
        qDebug(logUdp()) << "Stopping txaudio timer";
        txAudioTimer->stop();
        delete txAudioTimer;
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
    QByteArray audio;
    if (audioMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
    {
        txaudio->getNextAudioChunk(audio);
        // Now we have the next audio chunk, we can release the mutex.
        audioMutex.unlock();

        if (audio.length() > 0) {
            int counter = 1;
            int len = 0;

            while (len < audio.length()) {
                QByteArray partial = audio.mid(len, 1364);
                audio_packet p;
                memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
                p.len = sizeof(p) + partial.length();
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
    else {
        qInfo(logUdpServer()) << "Unable to lock mutex for rxaudio";
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
                control_packet_t in = (control_packet_t)r.constData();
                if (in->type == 0x04 && enableTx)
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
                    emit haveAudioData(tempAudio);
                    audioLatency = rxaudio->getLatency();
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
    qInfo(logUdp()) << "UDP Stream bound to local port:" << localPort << " remote port:" << port;
    uint32_t addr = localIP.toIPv4Address();
    myId = (addr >> 8 & 0xff) << 24 | (addr & 0xff) << 16 | (localPort & 0xffff);

    retransmitTimer = new QTimer();
    connect(retransmitTimer, &QTimer::timeout, this, &udpBase::sendRetransmitRequest);
    retransmitTimer->start(RETRANSMIT_PERIOD);

}

udpBase::~udpBase()
{
    qInfo(logUdp()) << "Closing UDP stream :" << radioIP.toString() << ":" << port;
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
                congestion ++;
                txBufferMutex.lock();
                QMap<quint16,SEQBUFENTRY>::iterator match = txSeqBuf.find(in->seq);
                if (match != txSeqBuf.end()) {
                    // Found matching entry?
                    // Send "untracked" as it has already been sent once.
                    // Don't constantly retransmit the same packet, give-up eventually
                    qDebug(logUdp()) << this->metaObject()->className() << ": Sending retransmit of " << hex << match->seqNum;
                    match->retransmitCount++;
                    udpMutex.lock();
                    udp->writeDatagram(match->data, radioIP, port);
                    udpMutex.unlock();
                }
                txBufferMutex.unlock();
            }
            if (in->type == 0x04) {
                qInfo(logUdp()) << this->metaObject()->className() << ": Received I am here ";
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
                    udpMutex.lock();
                    udp->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), radioIP, port);
                    udpMutex.unlock();
                }
                else if (in->reply == 0x01) {
                    if (in->seq == pingSendSeq)
                    {
                        // This is response to OUR request so increment counter
                        pingSendSeq++;
                    }
                }
                else {
                    qInfo(logUdp()) << this->metaObject()->className() << "Unhandled response to ping. I have never seen this! 0x10=" << r[16];
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
            QMap<quint16, SEQBUFENTRY>::iterator match = txSeqBuf.find(seq);
            if (match == txSeqBuf.end()) {
                qDebug(logUdp()) << this->metaObject()->className() << ": Requested packet " << hex << seq << " not found";
                // Just send idle packet.
                sendControl(false, 0, seq);
            }
            else {
                // Found matching entry?
                // Send "untracked" as it has already been sent once.
                qDebug(logUdp()) << this->metaObject()->className() << ": Sending retransmit (range) of " << hex << match->seqNum;
                match->retransmitCount++;
                udpMutex.lock();
                udp->writeDatagram(match->data, radioIP, port);
                udpMutex.unlock();
                match++;
                packetsLost++;
                congestion++;
            }
        }
    } 
    else if (in->len != PING_SIZE && in->type == 0x00 && in->seq != 0x00)
    {
        rxBufferMutex.lock();
        if (rxSeqBuf.isEmpty()) {
            if (rxSeqBuf.size() > BUFSIZE)
            {
                rxSeqBuf.erase(rxSeqBuf.begin());
            }
            rxSeqBuf.insert(in->seq, QTime::currentTime());
        } 
        else
        {
            //std::sort(rxSeqBuf.begin(), rxSeqBuf.end());
            if (in->seq < rxSeqBuf.firstKey())
            {
                qInfo(logUdp()) << this->metaObject()->className() << ": ******* seq number has rolled over ****** previous highest: " << hex << rxSeqBuf.lastKey() << " current: " << hex << in->seq;
                //seqPrefix++;
                // Looks like it has rolled over so clear buffer and start again.
                rxSeqBuf.clear();
                rxMissing.clear();
                rxBufferMutex.unlock();
                return;
            }

            if (!rxSeqBuf.contains(in->seq))
            {
                // Add incoming packet to the received buffer and if it is in the missing buffer, remove it.
                rxSeqBuf.insert(in->seq, QTime::currentTime());
                if (rxSeqBuf.size() > BUFSIZE)
                {
                    rxSeqBuf.erase(rxSeqBuf.begin());
                }
            }
            else {
                // This is probably one of our missing packets!
                missingMutex.lock();
                QMap<quint16,int>::iterator s = rxMissing.find(in->seq);
                if (s != rxMissing.end())
                {
                    qDebug(logUdp()) << this->metaObject()->className() << ": Missing SEQ has been received! " << hex << in->seq;
                    s = rxMissing.erase(s);
                }
                missingMutex.unlock();

            }

        }
        rxBufferMutex.unlock();

    }
}

bool missing(quint16 i, quint16 j) {
    return (i + 1 != j);
}

void udpBase::sendRetransmitRequest()
{
    // Find all gaps in received packets and then send requests for them.
    // This will run every 100ms so out-of-sequence packets will not trigger a retransmit request.

    QByteArray missingSeqs;

    rxBufferMutex.lock();
    if (!rxSeqBuf.empty() && rxSeqBuf.size() <= rxSeqBuf.lastKey() - rxSeqBuf.firstKey())
    {   
        if ((rxSeqBuf.lastKey() - rxSeqBuf.firstKey() - rxSeqBuf.size()) > 20)
        {
            // Too many packets to process, flush buffers and start again!
            qDebug(logUdp()) << "Too many missing packets, flushing buffer: " << rxSeqBuf.lastKey() << "missing=" << rxSeqBuf.lastKey() - rxSeqBuf.firstKey() - rxSeqBuf.size() + 1;
            rxMissing.clear();
            missingMutex.lock();
            rxSeqBuf.clear();
            missingMutex.unlock();
        }
        else {
            // We have at least 1 missing packet!
            qDebug(logUdp()) << "Missing Seq: size=" << rxSeqBuf.size() << "firstKey=" << rxSeqBuf.firstKey() << "lastKey=" << rxSeqBuf.lastKey() << "missing=" << rxSeqBuf.lastKey() - rxSeqBuf.firstKey() - rxSeqBuf.size() + 1;
            // We are missing packets so iterate through the buffer and add the missing ones to missing packet list
            missingMutex.lock();
            auto i = std::adjacent_find(rxSeqBuf.keys().begin(), rxSeqBuf.keys().end(), [](int l, int r) {return l + 1 < r; });

            int missCounter = 0;
            while (i != rxSeqBuf.keys().end())
            {
                quint16 j = 1 + *i;
                ++i;
                if (i == rxSeqBuf.keys().end())
                {
                    continue;
                }

                missCounter++;

                if (missCounter > 20) {
                    // More than 20 packets missing, something horrific has happened!
                    qDebug(logUdp()) << this->metaObject()->className() << ": Too many missing packets, clearing buffer";
                    rxSeqBuf.clear();
                    rxMissing.clear();
                    missingMutex.unlock();
                    rxBufferMutex.unlock();
                    return;
                }

                auto s = rxMissing.find(j);
                if (s == rxMissing.end())
                {
                    // We haven't seen this missing packet before
                    qDebug(logUdp()) << this->metaObject()->className() << ": Adding to missing buffer (len=" << rxMissing.size() << "): " << j;
                    if (rxMissing.size() > 25)
                    {
                        rxMissing.erase(rxMissing.begin());
                    }
                    rxMissing.insert(j, 0);
                    if (rxSeqBuf.size() > BUFSIZE)
                    {
                        rxSeqBuf.erase(rxSeqBuf.begin());
                    }
                    rxSeqBuf.insert(j, QTime::currentTime()); // Add this missing packet to the rxbuffer as we now long about it.
                    packetsLost++;
                }
                else {
                    if (s.value() == 4)
                    {
                        // We have tried 4 times to request this packet, time to give up!
                        s = rxMissing.erase(s);
                    }
                }
            }
            missingMutex.unlock();
        }
    }
    rxBufferMutex.unlock();

    missingMutex.lock();
    for (auto it = rxMissing.begin(); it != rxMissing.end(); ++it)
    {
        if (it.value() < 10)
        {
            missingSeqs.append(it.key() & 0xff);
            missingSeqs.append(it.key() >> 8 & 0xff);
            missingSeqs.append(it.key() & 0xff);
            missingSeqs.append(it.key() >> 8 & 0xff);
            it.value()++;
        }
    }
    missingMutex.unlock();
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
            p.seq = (missingSeqs[0] & 0xff) | (quint16)(missingSeqs[1] << 8);
            qDebug(logUdp()) << this->metaObject()->className() << ": sending request for missing packet : " << hex << p.seq;
            udpMutex.lock();
            udp->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), radioIP, port);
            udpMutex.unlock();
        }
        else
        {
            qDebug(logUdp()) << this->metaObject()->className() << ": sending request for multiple missing packets : " << missingSeqs.toHex();
            missingMutex.lock();
            missingSeqs.insert(0, p.packet, sizeof(p.packet));
            missingMutex.unlock();

            udpMutex.lock();
            udp->writeDatagram(missingSeqs, radioIP, port);
            udpMutex.unlock();
        }
    }

}



// Used to send idle and other "control" style messages
void udpBase::sendControl(bool tracked = true, quint8 type = 0, quint16 seq = 0)
{
    control_packet p;
    memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
    p.len = sizeof(p);
    p.type = type;
    p.sentid = myId;
    p.rcvdid = remoteId;

    if (!tracked) {
        p.seq = seq;
        udpMutex.lock();
        udp->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), radioIP, port);
        udpMutex.unlock();
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
    udpMutex.lock();
    udp->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), radioIP, port);
    udpMutex.unlock();
    return;
}

void udpBase::sendRetransmitRange(quint16 first, quint16 second, quint16 third, quint16 fourth)
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
    udpMutex.lock();
    udp->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), radioIP, port);
    udpMutex.unlock();
    return;
}


void udpBase::sendTrackedPacket(QByteArray d)
{
    // As the radio can request retransmission of these packets, store them in a buffer
    d[6] = sendSeq & 0xff;
    d[7] = (sendSeq >> 8) & 0xff;
    SEQBUFENTRY s;
    s.seqNum = sendSeq;
    s.timeSent = QTime::currentTime();
    s.retransmitCount = 0;
    s.data = d;
    if (txBufferMutex.tryLock(100))
    {
   
        if (sendSeq == 0) {
            // We are either the first ever sent packet or have rolled-over so clear the buffer.
            txSeqBuf.clear();
            congestion = 0;
        }
        txSeqBuf.insert(sendSeq,s);
        if (txSeqBuf.size() > BUFSIZE)
        {
            txSeqBuf.erase(txSeqBuf.begin());
        }
        txBufferMutex.unlock();
    } else {
        qInfo(logUdp()) << this->metaObject()->className() << ": txBuffer mutex is locked";
    }
    // Stop using purgeOldEntries() as it is likely slower than just removing the earliest packet.
    //qInfo(logUdp()) << this->metaObject()->className() << "RX:" << rxSeqBuf.size() << "TX:" <<txSeqBuf.size() << "MISS:" << rxMissing.size();
    //purgeOldEntries(); // Delete entries older than PURGE_SECONDS seconds (currently 5)
    sendSeq++;

    udpMutex.lock();
    udp->writeDatagram(d, radioIP, port);
    if (congestion>10) { // Poor quality connection?
        udp->writeDatagram(d, radioIP, port);
        if (congestion>20) // Even worse so send again.
            udp->writeDatagram(d, radioIP, port);
    }
    if (idleTimer != Q_NULLPTR && idleTimer->isActive()) {
        idleTimer->start(IDLE_PERIOD); // Reset idle counter if it's running
    }
    udpMutex.unlock();
    packetsSent++;
    return;
}


/// <summary>
/// Once a packet has reached PURGE_SECONDS old (currently 10) then it is not likely to be any use.
/// </summary>
void udpBase::purgeOldEntries()
{
    // Erase old entries from the tx packet buffer
    if (txBufferMutex.tryLock(100))
    {
        if (!txSeqBuf.isEmpty())
        {
            // Loop through the earliest items in the buffer and delete if older than PURGE_SECONDS
            for (auto it = txSeqBuf.begin(); it != txSeqBuf.end();) {
                if (it.value().timeSent.secsTo(QTime::currentTime()) > PURGE_SECONDS) {
                   txSeqBuf.erase(it++);
                }
                else {
                   break;
                }
            }
        }
        txBufferMutex.unlock();

    } else {
        qInfo(logUdp()) << this->metaObject()->className() << ": txBuffer mutex is locked";
    }



    if (rxBufferMutex.tryLock(100))
    {
        if (!rxSeqBuf.isEmpty()) {
            // Loop through the earliest items in the buffer and delete if older than PURGE_SECONDS
            for (auto it = rxSeqBuf.begin(); it != rxSeqBuf.end();) {
                if (it.value().secsTo(QTime::currentTime()) > PURGE_SECONDS) {
                    rxSeqBuf.erase(it++);
                }
                else {
                   break;
                }
           }
        }
        rxBufferMutex.unlock();
    } else {
        qInfo(logUdp()) << this->metaObject()->className() << ": rxBuffer mutex is locked";
    }

    if (missingMutex.tryLock(100))
    {
        // Erase old entries from the missing packets buffer
        if (!rxMissing.isEmpty() && rxMissing.size() > 50) {
            for (size_t i = 0; i < 25; ++i) {
                rxMissing.erase(rxMissing.begin());
            }
        }
        missingMutex.unlock();
    } else {
        qInfo(logUdp()) << this->metaObject()->className() << ": missingBuffer mutex is locked";
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

