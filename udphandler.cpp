// Copyright 2021 Phil Taylor M0VSE
// This code is heavily based on "Kappanhang" by HA2NON, ES1AKOS and W6EL!

#include "udphandler.h"
#include "logcategories.h"

udpHandler::udpHandler(udpPreferences prefs, audioSetup rx, audioSetup tx) :
    controlPort(prefs.controlLANPort),
    civPort(0),
    audioPort(0),
    civLocalPort(0),
    audioLocalPort(0),
    rxSetup(rx),
    txSetup(tx)
{
    this->port = this->controlPort;
    this->username = prefs.username;
    this->password = prefs.password;
    this->compName = prefs.clientName.mid(0,8) + "-wfview";

    if (prefs.waterfallFormat == 2)
    {
        splitWf = true;
    }
    else
    {
        splitWf = false;
    }
    qInfo(logUdp()) << "Starting udpHandler user:" << username << " rx latency:" << rxSetup.latency  << " tx latency:" << txSetup.latency << " rx sample rate: " << rxSetup.sampleRate <<
        " rx codec: " << rxSetup.codec << " tx sample rate: " << txSetup.sampleRate << " tx codec: " << txSetup.codec;

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
    udpBase::init(0); // Perform UDP socket initialization.

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

void udpHandler::getRxLevels(quint16 amplitude,quint16 latency,quint16 current, bool under) {
    status.rxAudioLevel = amplitude;
    status.rxLatency = latency;
    status.rxCurrentLatency = current;
    status.rxUnderrun = under;
}

void udpHandler::getTxLevels(quint16 amplitude,quint16 latency, quint16 current, bool under) {
    status.txAudioLevel = amplitude;
    status.txLatency = latency;
    status.txCurrentLatency = current;
    status.txUnderrun = under;
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
                    status.networkLatency += lastPingSentTime.msecsTo(QDateTime::currentDateTime());
                    status.networkLatency /= 2;
                    status.packetsSent = packetsSent;
                    status.packetsLost = packetsLost;
                    if (audio != Q_NULLPTR) {
                        status.packetsSent = status.packetsSent + audio->packetsSent;
                        status.packetsLost = status.packetsLost + audio->packetsLost;
                    }
                    if (civ != Q_NULLPTR) {
                        status.packetsSent = status.packetsSent + civ->packetsSent;
                        status.packetsLost = status.packetsLost + civ->packetsLost;
                    }

                    QString tempLatency;
                    if (status.rxCurrentLatency < status.rxLatency*1.2 && !status.rxUnderrun)
                    {
                        tempLatency = QString("%1 ms").arg(status.rxCurrentLatency,3);
                    }
                    else {
                        tempLatency = QString("<span style = \"color:red\">%1 ms</span>").arg(status.rxCurrentLatency,3);
                    }
                    QString txString="";
                    if (txSetup.codec == 0) {
                        txString = "(no tx)";
                    }
                    status.message = QString("<pre>%1 rx latency: %2 / rtt: %3 ms / loss: %4/%5</pre>").arg(txString).arg(tempLatency).arg(status.networkLatency, 3).arg(status.packetsLost, 3).arg(status.packetsSent, 3);

                    emit haveNetworkStatus(status);

                }
                break;
            }
            case (TOKEN_SIZE): // Response to Token request
            {
                token_packet_t in = (token_packet_t)r.constData();
                if (in->requesttype == 0x05 && in->requestreply == 0x02 && in->type != 0x01)
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
                        if (!streamOpened) {

                            civ = new udpCivData(localIP, radioIP, civPort, splitWf, civLocalPort);

                            // TX is not supported
                            if (txSampleRates < 2) {
                                txSetup.sampleRate=0;
                                txSetup.codec = 0;
                            }
                            audio = new udpAudio(localIP, radioIP, audioPort, audioLocalPort, rxSetup, txSetup);

                            QObject::connect(civ, SIGNAL(receive(QByteArray)), this, SLOT(receiveFromCivStream(QByteArray)));
                            QObject::connect(audio, SIGNAL(haveAudioData(audioPacket)), this, SLOT(receiveAudioData(audioPacket)));
                            QObject::connect(this, SIGNAL(haveChangeLatency(quint16)), audio, SLOT(changeLatency(quint16)));
                            QObject::connect(this, SIGNAL(haveSetVolume(unsigned char)), audio, SLOT(setVolume(unsigned char)));
                            QObject::connect(audio, SIGNAL(haveRxLevels(quint16, quint16, quint16,bool)), this, SLOT(getRxLevels(quint16, quint16,quint16,bool)));
                            QObject::connect(audio, SIGNAL(haveTxLevels(quint16, quint16,quint16,bool)), this, SLOT(getTxLevels(quint16, quint16,quint16,bool)));

                            streamOpened = true;
                        }
                        qInfo(logUdp()) << this->metaObject()->className() << "Got serial and audio request success, device name: " << devName;


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
                        status.message = "Invalid Username/Password";
                        qInfo(logUdp()) << this->metaObject()->className() << ": Invalid Username/Password";
                    }
                    else if (!isAuthenticated)
                    {

                        if (in->tokrequest == tokRequest)
                        {
                            status.message="Radio Login OK!";
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
                // Once connected, the server will send a conninfo packet for each radio that is connected

                conninfo_packet_t in = (conninfo_packet_t)r.constData();
                QHostAddress ip = QHostAddress(qToBigEndian(in->ipaddress));

                qInfo(logUdp()) << "Got Connection status for:" << in->name << "Busy:" << in->busy << "Computer" << in->computer << "IP" << ip.toString();

                // First we need to find this radio in our capabilities packet, there aren't many so just step through
                for (unsigned char f = 0; f < radios.size(); f++)
                {

                    if ((radios[f].commoncap == 0x8010 &&
                        radios[f].macaddress[0] == in->macaddress[0] &&
                        radios[f].macaddress[1] == in->macaddress[1] &&
                        radios[f].macaddress[2] == in->macaddress[2] &&
                        radios[f].macaddress[3] == in->macaddress[3] &&
                        radios[f].macaddress[4] == in->macaddress[4] &&
                        radios[f].macaddress[5] == in->macaddress[5]) ||
                        !memcmp(radios[f].guid,in->guid, GUIDLEN))
                    {
                        emit setRadioUsage(f, in->busy, QString(in->computer), ip.toString());
                        qDebug(logUdp()) << "Set radio usage num:" << f << in->name << "Busy:" << in->busy << "Computer" << in->computer << "IP" << ip.toString();
                    }
                }

                if (!streamOpened && radios.size()==1) {

                    qDebug(logUdp()) << "Single radio available, can I connect to it?";

                    if (in->busy)
                    {
                        if (in->ipaddress != 0x00 && strcmp(in->computer, compName.toLocal8Bit()))
                        {
                            status.message = devName + " in use by: " + in->computer + " (" + ip.toString() + ")";
                            sendControl(false, 0x00, in->seq); // Respond with an idle
                        }
                        else {
                        }
                    }
                    else if (!in->busy)
                    {
                        qDebug(logUdp()) << "Attempting to connect to radio";
                        status.message = devName + " available";
                        
                        setCurrentRadio(0);
                    }
                }
                else if (streamOpened) 
                /* If another client connects/disconnects from the server, the server will emit
                   a CONNINFO packet, send our details to confirm we still want the stream */
                {
                    //qDebug(logUdp()) << "I am already connected????";
                    // Received while stream is open.
                    //sendRequestStream();
                }
                break;
            }
    
            default:
            {
                if ((r.length() - CAPABILITIES_SIZE) % RADIO_CAP_SIZE != 0)
                {
                    // Likely a retransmit request?
                    break;
                }


                capabilities_packet_t in = (capabilities_packet_t)r.constData();
                numRadios = in->numradios;

                for (int f = CAPABILITIES_SIZE; f < r.length(); f = f + RADIO_CAP_SIZE) {
                    radio_cap_packet rad;
                    const char* tmpRad = r.constData();
                    memcpy(&rad, tmpRad+f, RADIO_CAP_SIZE);
                    radios.append(rad);
                }
                for(const radio_cap_packet &radio : radios)
                {
                    qInfo(logUdp()) << this->metaObject()->className() << "Received radio capabilities, Name:" <<
                        radio.name << " Audio:" <<
                        radio.audio << "CIV:" << QString("0x%1").arg((unsigned char)radio.civ,0, 16) <<
                        "MAC:" << radio.macaddress[0] <<
                        ":" << radio.macaddress[1] <<
                        ":" << radio.macaddress[2] <<
                        ":" << radio.macaddress[3] <<
                        ":" << radio.macaddress[4] <<
                        ":" << radio.macaddress[5] <<
                        "CAPF" << radio.capf;
                }
                emit requestRadioSelection(radios);

                break;
            }
    
        }
        udpBase::dataReceived(r); // Call parent function to process the rest.
        r.clear();
        datagram.clear();
    }
    return;
}


void udpHandler::setCurrentRadio(quint8 radio) {
    qInfo(logUdp()) << "Got Radio" << radio;
    qInfo(logUdp()) << "Find available local ports";

    /* This seems to be the only way to find some available local ports.
        The problem is we need to know the local AND remote ports but send the 
        local port to the server first and it replies with the remote ports! */
    if (civLocalPort == 0 || audioLocalPort == 0) {
        QUdpSocket* tempudp = new QUdpSocket();
        tempudp->bind(); // Bind to random port.
        civLocalPort = tempudp->localPort();
        tempudp->close();
        tempudp->bind();
        audioLocalPort = tempudp->localPort();
        tempudp->close();
        delete tempudp;
    }
    int baudrate = qFromBigEndian(radios[radio].baudrate);
    emit haveBaudRate(baudrate);

    if (radios[radio].commoncap == 0x8010) {
        memcpy(&macaddress, radios[radio].macaddress, sizeof(macaddress));
        useGuid = false;
    }
    else {
        useGuid = true;
        memcpy(&guid, radios[radio].guid, GUIDLEN);
    }

    devName =radios[radio].name;
    audioType = radios[radio].audio;
    civId = radios[radio].civ;
    rxSampleRates = radios[radio].rxsample;
    txSampleRates = radios[radio].txsample;

    sendRequestStream();
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
    p.payloadsize = qToBigEndian((quint16)(sizeof(p) - 0x10));
    p.requesttype = 0x03;
    p.requestreply = 0x01;

    if (!useGuid) {
        p.commoncap = 0x8010;
        memcpy(&p.macaddress, macaddress, 6);
    }
    else {
        memcpy(&p.guid, guid, GUIDLEN);
    }
    p.innerseq = qToBigEndian(authSeq++);
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
    p.rxsample = qToBigEndian((quint32)rxSetup.sampleRate);
    p.txsample = qToBigEndian((quint32)txSetup.sampleRate);
    p.civport = qToBigEndian((quint32)civLocalPort);
    p.audioport = qToBigEndian((quint32)audioLocalPort);
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
        status.message = "Radio not responding!";
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
    p.payloadsize = qToBigEndian((quint16)(sizeof(p) - 0x10));
    p.requesttype = 0x00;
    p.requestreply = 0x01;

    p.innerseq = qToBigEndian(authSeq++);
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
    p.payloadsize = qToBigEndian((quint16)(sizeof(p) - 0x10));
    p.requesttype = magic;
    p.requestreply = 0x01;
    p.innerseq = qToBigEndian(authSeq++);
    p.tokrequest = tokRequest;
    p.token = token;

    sendTrackedPacket(QByteArray::fromRawData((const char *)p.packet, sizeof(p)));
    // The radio should request a repeat of the token renewal packet via retransmission!
    //tokenTimer->start(100); // Set 100ms timer for retry (this will be cancelled if a response is received)
    return;
}


// Class that manages all Civ Data to/from the rig
udpCivData::udpCivData(QHostAddress local, QHostAddress ip, quint16 civPort, bool splitWf, quint16 localPort=0 )
{
    qInfo(logUdp()) << "Starting udpCivData";
    localIP = local;
    port = civPort;
    radioIP = ip;
    splitWaterfall = splitWf;

    udpBase::init(localPort); // Perform connection

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
                            // Find data length
                            int pos = r.indexOf(QByteArrayLiteral("\x27\x00\x00"))+2;
                            int len = r.mid(pos).indexOf(QByteArrayLiteral("\xfd"));
                            //splitWaterfall = false;
                            if (splitWaterfall && pos > 1 && len > 100) {
                                // We need to split waterfall data into its component parts
                                // There are only 2 types that we are currently aware of
                                int numDivisions = 0;
                                if (len == 490) // IC705, IC9700, IC7300(LAN) 
                                {
                                    numDivisions = 11;
                                }
                                else if (len == 704) // IC7610, IC7851, ICR8600
                                {
                                    numDivisions = 15;
                                }
                                else {
                                    qInfo(logUdp()) << "Unknown spectrum size" << len;
                                    break;
                                }
                                // (sequence #1) includes center/fixed mode at [05]. No pixels.
                                // "INDEX: 00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 "
                                // "DATA:  27 00 00 01 11 01 00 00 00 14 00 00 00 35 14 00 00 fd "
                                // (sequences 2-10, 50 pixels)
                                // "INDEX: 00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 "
                                // "DATA:  27 00 00 07 11 27 13 15 01 00 22 21 09 08 06 19 0e 20 23 25 2c 2d 17 27 29 16 14 1b 1b 21 27 1a 18 17 1e 21 1b 24 21 22 23 13 19 23 2f 2d 25 25 0a 0e 1e 20 1f 1a 0c fd "
                                //                  ^--^--(seq 7/11)
                                //                        ^-- start waveform data 0x00 to 0xA0, index 05 to 54
                                // (sequence #11)
                                // "INDEX: 00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 "
                                // "DATA:  27 00 00 11 11 0b 13 21 23 1a 1b 22 1e 1a 1d 13 21 1d 26 28 1f 19 1a 18 09 2c 2c 2c 1a 1b fd "

                                int divSize = (len / numDivisions)+6;
                                QByteArray wfPacket;
                                for (int i = 0; i < numDivisions; i++) {

                                    wfPacket = r.mid(pos - 6, 9); // First part of packet 

                                    wfPacket = r.mid(pos - 6, 9); // First part of packet 
                                    char tens = ((i + 1) / 10);
                                    char units = ((i + 1) - (10 * tens));
                                    wfPacket[7] = units | (tens << 4);
                                    
                                    tens = (numDivisions / 10);
                                    units = (numDivisions - (10 * tens));
                                    wfPacket[8] = units | (tens << 4);

                                    if (i == 0) {
                                        //Just send initial data, first BCD encode the max number:
                                        wfPacket.append(r.mid(pos + 3, 12));
                                    }
                                    else
                                    {
                                        wfPacket.append(r.mid((pos + 15) + ((i-1) * divSize),divSize));
                                    }
                                    if (i < numDivisions-1) {
                                        wfPacket.append('\xfd');
                                    }
                                    //printHex(wfPacket, false, true);
                                    
                                    emit receive(wfPacket);
                                    wfPacket.clear();

                                }
                                //qDebug(logUdp()) << "Waterfall packet len" << len << "Num Divisions" << numDivisions << "Division Size" << divSize;
                            }
                            else {
                                // Not waterfall data or split not enabled.
                                emit receive(r.mid(0x15));
                            }
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
udpAudio::udpAudio(QHostAddress local, QHostAddress ip, quint16 audioPort, quint16 lport, audioSetup rxSetup, audioSetup txSetup)
{
    qInfo(logUdp()) << "Starting udpAudio";
    this->localIP = local;
    this->port = audioPort;
    this->radioIP = ip;

    if (txSetup.sampleRate == 0) {
        enableTx = false;
    }

    init(lport); // Perform connection

    QUdpSocket::connect(udp, &QUdpSocket::readyRead, this, &udpAudio::dataReceived);

    rxaudio = new audioHandler();
    rxAudioThread = new QThread(this);
    rxAudioThread->setObjectName("rxAudio()");

    rxaudio->moveToThread(rxAudioThread);

    rxAudioThread->start(QThread::TimeCriticalPriority);

    connect(this, SIGNAL(setupRxAudio(audioSetup)), rxaudio, SLOT(init(audioSetup)));

    // signal/slot not currently used.
    connect(this, SIGNAL(haveAudioData(audioPacket)), rxaudio, SLOT(incomingAudio(audioPacket)));
    connect(this, SIGNAL(haveChangeLatency(quint16)), rxaudio, SLOT(changeLatency(quint16)));
    connect(this, SIGNAL(haveSetVolume(unsigned char)), rxaudio, SLOT(setVolume(unsigned char)));
    connect(rxaudio, SIGNAL(haveLevels(quint16, quint16, quint16,bool)), this, SLOT(getRxLevels(quint16, quint16, quint16,bool)));
    connect(rxAudioThread, SIGNAL(finished()), rxaudio, SLOT(deleteLater()));
    

    sendControl(false, 0x03, 0x00); // First connect packet

    pingTimer = new QTimer();
    connect(pingTimer, &QTimer::timeout, this, &udpBase::sendPing);
    pingTimer->start(PING_PERIOD); // send ping packets every 100ms

    if (enableTx) {
        txaudio = new audioHandler();
        txAudioThread = new QThread(this);
        rxAudioThread->setObjectName("txAudio()");

        txaudio->moveToThread(txAudioThread);

        txAudioThread->start(QThread::TimeCriticalPriority);

        connect(this, SIGNAL(setupTxAudio(audioSetup)), txaudio, SLOT(init(audioSetup)));
        connect(txaudio, SIGNAL(haveAudioData(audioPacket)), this, SLOT(receiveAudioData(audioPacket)));
        connect(txaudio, SIGNAL(haveLevels(quint16, quint16, quint16, bool)), this, SLOT(getTxLevels(quint16, quint16, quint16, bool)));

        connect(txAudioThread, SIGNAL(finished()), txaudio, SLOT(deleteLater()));
        emit setupTxAudio(txSetup);
    }

    emit setupRxAudio(rxSetup);

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

void udpAudio::changeLatency(quint16 value)
{
    emit haveChangeLatency(value);
}

void udpAudio::setVolume(unsigned char value)
{
    emit haveSetVolume(value);
}

void udpAudio::getRxLevels(quint16 amplitude,quint16 latency, quint16 current, bool under) {

    emit haveRxLevels(amplitude,latency, current, under);
}

void udpAudio::getTxLevels(quint16 amplitude,quint16 latency, quint16 current, bool under) {
    emit haveTxLevels(amplitude,latency, current, under);
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



void udpBase::init(quint16 lport)
{
    //timeStarted.start();
    udp = new QUdpSocket(this);
    udp->bind(lport); // Bind to random port.
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
            if (in->type == 0x01 && in->len == 0x10)
            {
                // Single packet request
                packetsLost++;
                congestion = static_cast<double>(packetsSent) / packetsLost * 100;
                txBufferMutex.lock();
                auto match = txSeqBuf.find(in->seq);
                if (match != txSeqBuf.end()) {
                    // Found matching entry?
                    // Send "untracked" as it has already been sent once.
                    // Don't constantly retransmit the same packet, give-up eventually
                    qDebug(logUdp()) << this->metaObject()->className() << ": Sending (single packet) retransmit of " << QString("0x%1").arg(match->seqNum, 0, 16);
                    match->retransmitCount++;
                    udpMutex.lock();
                    udp->writeDatagram(match->data, radioIP, port);
                    udpMutex.unlock();
                }
                else {
                    qDebug(logUdp()) << this->metaObject()->className() << ": Remote requested packet" 
                        << QString("0x%1").arg(in->seq, 0, 16) << 
                        "not found, have " << QString("0x%1").arg(txSeqBuf.firstKey(), 0, 16) <<
                        "to" << QString("0x%1").arg(txSeqBuf.lastKey(), 0, 16);
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
            auto match = txSeqBuf.find(seq);
            if (match == txSeqBuf.end()) {
                qDebug(logUdp()) << this->metaObject()->className() << ": Remote requested packet"
                    << QString("0x%1").arg(seq, 0, 16) <<
                    "not found, have " << QString("0x%1").arg(txSeqBuf.firstKey(), 0, 16) <<
                    "to" << QString("0x%1").arg(txSeqBuf.lastKey(), 0, 16);
                // Just send idle packet.
                sendControl(false, 0, seq);
            }
            else {
                // Found matching entry?
                // Send "untracked" as it has already been sent once.
                qDebug(logUdp()) << this->metaObject()->className() << ": Sending (multiple packet) retransmit of " << QString("0x%1").arg(match->seqNum,0,16);
                match->retransmitCount++;
                udpMutex.lock();
                udp->writeDatagram(match->data, radioIP, port);
                udpMutex.unlock();
                packetsLost++;
                congestion = static_cast<double>(packetsSent) / packetsLost * 100;
            }
        }
    } 
    else if (in->len != PING_SIZE && in->type == 0x00 && in->seq != 0x00)
    {
        rxBufferMutex.lock();
        if (rxSeqBuf.isEmpty()) {
            rxSeqBuf.insert(in->seq, QTime::currentTime());
        }
        else
        {
            if (in->seq < rxSeqBuf.firstKey() || in->seq - rxSeqBuf.lastKey() > MAX_MISSING)
            {
                qInfo(logUdp()) << this->metaObject()->className() << "Large seq number gap detected, previous highest: " << 
                    QString("0x%1").arg(rxSeqBuf.lastKey(),0,16) << " current: " << QString("0x%1").arg(in->seq,0,16);
                //seqPrefix++;
                // Looks like it has rolled over so clear buffer and start again.
                rxSeqBuf.clear();
                // Add this packet to the incoming buffer
                rxSeqBuf.insert(in->seq, QTime::currentTime());
                rxBufferMutex.unlock();
                missingMutex.lock();
                rxMissing.clear();
                missingMutex.unlock();
                return;
            }

            if (!rxSeqBuf.contains(in->seq))
            {
                // Add incoming packet to the received buffer and if it is in the missing buffer, remove it.

                if (in->seq > rxSeqBuf.lastKey() + 1) {
                    qInfo(logUdp()) << this->metaObject()->className() << "1 or more missing packets detected, previous: " << 
                        QString("0x%1").arg(rxSeqBuf.lastKey(),0,16) << " current: " << QString("0x%1").arg(in->seq,0,16);
                    // We are likely missing packets then!
                    missingMutex.lock();
                    //int missCounter = 0;
                    // Sanity check!
                    for (quint16 f = rxSeqBuf.lastKey() + 1; f <= in->seq; f++)
                    {
                        if (rxSeqBuf.size() > BUFSIZE)
                        {
                            rxSeqBuf.erase(rxSeqBuf.begin());
                        }
                        rxSeqBuf.insert(f, QTime::currentTime());
                        if (f != in->seq && !rxMissing.contains(f))
                        {
                            rxMissing.insert(f, 0);
                        }
                    }
                    missingMutex.unlock();
                }
                else {
                    if (rxSeqBuf.size() > BUFSIZE)
                    {
                        rxSeqBuf.erase(rxSeqBuf.begin());
                    }
                    rxSeqBuf.insert(in->seq, QTime::currentTime());

                }
            }
            else {
                // This is probably one of our missing packets!
                missingMutex.lock();
                auto s = rxMissing.find(in->seq);
                if (s != rxMissing.end())
                {
                    qInfo(logUdp()) << this->metaObject()->className() << ": Missing SEQ has been received! " << QString("0x%1").arg(in->seq,0,16);

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
    if (rxMissing.isEmpty()) {
        return;
    }
    else if (rxMissing.size() > MAX_MISSING) {
        qInfo(logUdp()) << "Too many missing packets," << rxMissing.size() << "flushing all buffers";
        missingMutex.lock();
        rxMissing.clear();
        missingMutex.unlock();

        rxBufferMutex.lock();
        rxSeqBuf.clear();
        rxBufferMutex.unlock();
        return;
    }

    QByteArray missingSeqs;

    missingMutex.lock();
    auto it = rxMissing.begin();
    while (it != rxMissing.end())
    {
        if (it.key() != 0x0)
        {
            if (it.value() < 4)
            {
                missingSeqs.append(it.key() & 0xff);
                missingSeqs.append(it.key() >> 8 & 0xff);
                missingSeqs.append(it.key() & 0xff);
                missingSeqs.append(it.key() >> 8 & 0xff);
                it.value()++;
                it++;
            }
            else {
                qInfo(logUdp()) << this->metaObject()->className() << ": No response for missing packet" << QString("0x%1").arg(it.key(), 0, 16) << "deleting";
                it = rxMissing.erase(it);
            }            
        } else {
            qInfo(logUdp()) << this->metaObject()->className() << ": found empty key in missing buffer";
            it++;
        }
    }
    missingMutex.unlock();

    if (missingSeqs.length() != 0)
    {
        control_packet p;
        memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
        p.len = sizeof(p);
        p.type = 0x01;
        p.seq = 0x0000;
        p.sentid = myId;
        p.rcvdid = remoteId;
        if (missingSeqs.length() == 4) // This is just a single missing packet so send using a control.
        {
            p.seq = (missingSeqs[0] & 0xff) | (quint16)(missingSeqs[1] << 8);
            qInfo(logUdp()) << this->metaObject()->className() << ": sending request for missing packet : " << QString("0x%1").arg(p.seq,0,16);
            udpMutex.lock();
            udp->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), radioIP, port);
            udpMutex.unlock();
        }
        else
        {
            qInfo(logUdp()) << this->metaObject()->className() << ": sending request for multiple missing packets : " << missingSeqs.toHex(':');
            missingMutex.lock();
            p.len = sizeof(p)+missingSeqs.size();
            missingSeqs.insert(0, p.packet, sizeof(p));
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
    QTime now=QTime::currentTime();
    p.time = (quint32)now.msecsSinceStartOfDay();
    lastPingSentTime = QDateTime::currentDateTime();
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
        if (txSeqBuf.size() > BUFSIZE)
        {
            txSeqBuf.erase(txSeqBuf.begin());
        }
        txSeqBuf.insert(sendSeq, s);

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

void udpBase::printHex(const QByteArray& pdata)
{
    printHex(pdata, false, true);
}

void udpBase::printHex(const QByteArray& pdata, bool printVert, bool printHoriz)
{
    qDebug(logUdp()) << "---- Begin hex dump -----:";
    QString sdata("DATA:  ");
    QString index("INDEX: ");
    QStringList strings;

    for (int i = 0; i < pdata.length(); i++)
    {
        strings << QString("[%1]: %2").arg(i, 8, 10, QChar('0')).arg((unsigned char)pdata[i], 2, 16, QChar('0'));
        sdata.append(QString("%1 ").arg((unsigned char)pdata[i], 2, 16, QChar('0')));
        index.append(QString("%1 ").arg(i, 2, 10, QChar('0')));
    }

    if (printVert)
    {
        for (int i = 0; i < strings.length(); i++)
        {
            //sdata = QString(strings.at(i));
            qDebug(logUdp()) << strings.at(i);
        }
    }

    if (printHoriz)
    {
        qDebug(logUdp()) << index;
        qDebug(logUdp()) << sdata;
    }
    qDebug(logUdp()) << "----- End hex dump -----";
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

