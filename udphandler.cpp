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
            audio = Q_NULLPTR;
        }

        if (civ != Q_NULLPTR) {
            delete civ;
            civ = Q_NULLPTR;
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

void udpHandler::getRxLevels(quint16 amplitudePeak, quint16 amplitudeRMS,quint16 latency,quint16 current, bool under, bool over) {
    status.rxAudioLevel = amplitudePeak;
    status.rxLatency = latency;
    status.rxCurrentLatency = current;
    status.rxUnderrun = under;
    status.rxOverrun = over;
    audioLevelsRxPeak[(audioLevelsRxPosition)%audioLevelBufferSize] = amplitudePeak;
    audioLevelsRxRMS[(audioLevelsRxPosition)%audioLevelBufferSize] = amplitudeRMS;

    if((audioLevelsRxPosition)%4 == 0)
    {
        // calculate mean and emit signal
        unsigned char meanPeak = findMax(audioLevelsRxPeak);
        unsigned char meanRMS = findMean(audioLevelsRxRMS);
        networkAudioLevels l;
        l.haveRxLevels = true;
        l.rxAudioPeak = meanPeak;
        l.rxAudioRMS = meanRMS;
        emit haveNetworkAudioLevels(l);
    }
    audioLevelsRxPosition++;
}

void udpHandler::getTxLevels(quint16 amplitudePeak, quint16 amplitudeRMS ,quint16 latency, quint16 current, bool under, bool over) {
    status.txAudioLevel = amplitudePeak;
    status.txLatency = latency;
    status.txCurrentLatency = current;
    status.txUnderrun = under;
    status.txOverrun = over;
    audioLevelsTxPeak[(audioLevelsTxPosition)%audioLevelBufferSize] = amplitudePeak;
    audioLevelsTxRMS[(audioLevelsTxPosition)%audioLevelBufferSize] = amplitudeRMS;

    if((audioLevelsTxPosition)%4 == 0)
    {
        // calculate mean and emit signal
        unsigned char meanPeak = findMax(audioLevelsTxPeak);
        unsigned char meanRMS = findMean(audioLevelsTxRMS);
        networkAudioLevels l;
        l.haveTxLevels = true;
        l.txAudioPeak = meanPeak;
        l.txAudioRMS = meanRMS;
        emit haveNetworkAudioLevels(l);
    }
    audioLevelsTxPosition++;
}

unsigned char udpHandler::findMean(unsigned char *data)
{
    unsigned int sum=0;
    for(int p=0; p < audioLevelBufferSize; p++)
    {
        sum += data[p];
    }
    return sum / audioLevelBufferSize;
}

unsigned char udpHandler::findMax(unsigned char *data)
{
    unsigned int max=0;
    for(int p=0; p < audioLevelBufferSize; p++)
    {
       if(data[p] > max)
           max = data[p];
    }
    return max;
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
                    if (status.rxCurrentLatency <= status.rxLatency && !status.rxUnderrun && !status.rxOverrun)
                    {
                        tempLatency = QString("%1 ms").arg(status.rxCurrentLatency,3);
                    }
                    else if (status.rxUnderrun){
                        tempLatency = QString("<span style = \"color:red\">%1 ms</span>").arg(status.rxCurrentLatency,3);
                    }
                    else if (status.rxOverrun){
                        tempLatency = QString("<span style = \"color:orange\">%1 ms</span>").arg(status.rxCurrentLatency,3);
                    } else
                    {
                        tempLatency = QString("<span style = \"color:green\">%1 ms</span>").arg(status.rxCurrentLatency,3);
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
                        emit haveNetworkError(errorType(true, radioIP.toString(), "Connection failed\ntry rebooting the radio."));
                        qInfo(logUdp()) << this->metaObject()->className() << ": Connection failed, wait a few minutes or reboot the radio.";
                    }
                    else if (in->error == 0x00000000 && in->disc == 0x01)
                    {
                        emit haveNetworkError(errorType(radioIP.toString(), "Got radio disconnected."));
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
                            QObject::connect(civ, SIGNAL(receive(QByteArray)), this, SLOT(receiveFromCivStream(QByteArray)));

                            // TX is not supported
                            if (txSampleRates < 2) {
                                txSetup.sampleRate=0;
                                txSetup.codec = 0;
                            }
                            streamOpened = true;
                        }
                        if (audio == Q_NULLPTR) {
                            audio = new udpAudio(localIP, radioIP, audioPort, audioLocalPort, rxSetup, txSetup);

                            QObject::connect(audio, SIGNAL(haveAudioData(audioPacket)), this, SLOT(receiveAudioData(audioPacket)));
                            QObject::connect(this, SIGNAL(haveChangeLatency(quint16)), audio, SLOT(changeLatency(quint16)));
                            QObject::connect(this, SIGNAL(haveSetVolume(unsigned char)), audio, SLOT(setVolume(unsigned char)));
                            QObject::connect(audio, SIGNAL(haveRxLevels(quint16, quint16, quint16, quint16, bool, bool)), this, SLOT(getRxLevels(quint16, quint16, quint16, quint16, bool, bool)));
                            QObject::connect(audio, SIGNAL(haveTxLevels(quint16, quint16, quint16, quint16, bool, bool)), this, SLOT(getTxLevels(quint16, quint16, quint16, quint16, bool, bool)));
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
                            emit haveNetworkError(errorType(QString("UDP"), QString("Opus codec not supported, forcing LPCM16")));
                            if (rxSetup.codec >= 0x40)
                                rxSetup.codec = 0x04;
                            if (txSetup.codec >= 0x40)
                                txSetup.codec = 0x04;
                        }
                    }


                    if (in->error == 0xfeffffff)
                    {
                        emit haveNetworkError(errorType(true, radioIP.toString(), "Invalid Username/Password"));
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
                    qInfo(logUdp()) << this->metaObject()->className() << QString("Received radio capabilities, Name: %1, Audio: %2, CIV: %3, MAC: %4:%5:%6:%7:%8:%9 CAPF: %10")
                        .arg(rad.name).arg(rad.audio).arg((unsigned char)rad.civ, 2, 16, QChar('0'))
                        .arg(rad.macaddress[0], 2, 16, QChar('0')).arg(rad.macaddress[1], 2, 16, QChar('0'))
                        .arg(rad.macaddress[2], 2, 16, QChar('0')).arg(rad.macaddress[3], 2, 16, QChar('0'))
                        .arg(rad.macaddress[4], 2, 16, QChar('0')).arg(rad.macaddress[5], 2, 16, QChar('0')).arg(rad.capf, 4, 16, QChar('0'));
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

    // If we are currently connected to a different radio, disconnect first
    if (audio != Q_NULLPTR) {
        delete audio;
        audio = Q_NULLPTR;
    }

    if (civ != Q_NULLPTR) {
        delete civ;
        civ = Q_NULLPTR;
    }

    streamOpened = false;

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
    p.resetcap = qToBigEndian((quint16)0x0798);
    p.token = token;

    sendTrackedPacket(QByteArray::fromRawData((const char *)p.packet, sizeof(p)));
    // The radio should request a repeat of the token renewal packet via retransmission!
    //tokenTimer->start(100); // Set 100ms timer for retry (this will be cancelled if a response is received)
    return;
}

