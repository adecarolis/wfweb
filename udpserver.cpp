#include "udpserver.h"
#include "logcategories.h"

#define STALE_CONNECTION 15
udpServer::udpServer(SERVERCONFIG config, audioSetup outAudio, audioSetup inAudio) :
    config(config),
    outAudio(outAudio),
    inAudio(inAudio)
{
    qInfo(logUdpServer()) << "Starting udp server";
}

void udpServer::init()
{

    srand(time(NULL)); // Generate random key
    timeStarted.start();
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

    foreach(QNetworkInterface netInterface, QNetworkInterface::allInterfaces())
    {
        // Return only the first non-loopback MAC Address
        if (!(netInterface.flags() & QNetworkInterface::IsLoopBack)) {
            macAddress = netInterface.hardwareAddress();
        }
    }

    uint32_t addr = localIP.toIPv4Address();

    qInfo(logUdpServer()) << " My IP Address: " << QHostAddress(addr).toString() << " My MAC Address: " << macAddress;


    controlId = (addr >> 8 & 0xff) << 24 | (addr & 0xff) << 16 | (config.controlPort & 0xffff);
    civId = (addr >> 8 & 0xff) << 24 | (addr & 0xff) << 16 | (config.civPort & 0xffff);
    audioId = (addr >> 8 & 0xff) << 24 | (addr & 0xff) << 16 | (config.audioPort & 0xffff);

    qInfo(logUdpServer()) << "Server Binding Control to: " << config.controlPort;
    udpControl = new QUdpSocket(this);
    udpControl->bind(config.controlPort);
    QUdpSocket::connect(udpControl, &QUdpSocket::readyRead, this, &udpServer::controlReceived);

    qInfo(logUdpServer()) << "Server Binding CIV to: " << config.civPort;
    udpCiv = new QUdpSocket(this);
    udpCiv->bind(config.civPort);
    QUdpSocket::connect(udpCiv, &QUdpSocket::readyRead, this, &udpServer::civReceived);

    qInfo(logUdpServer()) << "Server Binding Audio to: " << config.audioPort;
    udpAudio = new QUdpSocket(this);
    udpAudio->bind(config.audioPort);
    QUdpSocket::connect(udpAudio, &QUdpSocket::readyRead, this, &udpServer::audioReceived);
}

udpServer::~udpServer()
{
    qInfo(logUdpServer()) << "Closing udpServer";

    connMutex.lock();

    foreach(CLIENT * client, controlClients)
    {
        if (client->idleTimer != Q_NULLPTR)
        {
            client->idleTimer->stop();
            delete client->idleTimer;
        }
        if (client->pingTimer != Q_NULLPTR) {
            client->pingTimer->stop();
            delete client->pingTimer;
        }
        if (client->wdTimer != Q_NULLPTR) {
            client->wdTimer->stop();
            delete client->wdTimer;
        }

        if (client->retransmitTimer != Q_NULLPTR) {
            client->retransmitTimer->stop();
            delete client->retransmitTimer;
        }

        delete client;
        controlClients.removeAll(client);
    }
    foreach(CLIENT * client, civClients)
    {
        if (client->idleTimer != Q_NULLPTR)
        {
            client->idleTimer->stop();
            delete client->idleTimer;
        }
        if (client->pingTimer != Q_NULLPTR) {
            client->pingTimer->stop();
            delete client->pingTimer;
        }
        if (client->retransmitTimer != Q_NULLPTR) {
            client->retransmitTimer->stop();
            delete client->retransmitTimer;
        }
        delete client;
        civClients.removeAll(client);
    }
    foreach(CLIENT * client, audioClients)
    {
        if (client->idleTimer != Q_NULLPTR)
        {
            client->idleTimer->stop();
            delete client->idleTimer;
        }
        if (client->pingTimer != Q_NULLPTR) {
            client->pingTimer->stop();
            delete client->pingTimer;
        }
        if (client->retransmitTimer != Q_NULLPTR) {
            client->retransmitTimer->stop();
            delete client->retransmitTimer;
        }
        delete client;
        audioClients.removeAll(client);
    }

    if (rxAudioTimer != Q_NULLPTR) {
        rxAudioTimer->stop();
        delete rxAudioTimer;
        rxAudioTimer = Q_NULLPTR;
    }

    if (rxAudioThread != Q_NULLPTR) {
        rxAudioThread->quit();
        rxAudioThread->wait();
    }

    if (txAudioThread != Q_NULLPTR) {
        txAudioThread->quit();
        txAudioThread->wait();
    }

    if (udpControl != Q_NULLPTR) {
        udpControl->close();
        delete udpControl;
    }
    if (udpCiv != Q_NULLPTR) {
        udpCiv->close();
        delete udpCiv;
    }
    if (udpAudio != Q_NULLPTR) {
        udpAudio->close();
        delete udpAudio;
    }

    connMutex.unlock();


}


void udpServer::receiveRigCaps(rigCapabilities caps)
{
    this->rigCaps = caps;
}

#define RETRANSMIT_PERIOD 100

void udpServer::controlReceived()
{
    // Received data on control port.
    while (udpControl->hasPendingDatagrams()) {
        QNetworkDatagram datagram = udpControl->receiveDatagram();
        QByteArray r = datagram.data();
        CLIENT* current = Q_NULLPTR;
        if (datagram.senderAddress().isNull() || datagram.senderPort() == 65535 || datagram.senderPort() == 0)
            return;

        foreach(CLIENT * client, controlClients)
        {
            if (client != Q_NULLPTR)
            {
                if (client->ipAddress == datagram.senderAddress() && client->port == datagram.senderPort())
                {
                    current = client;
                }
            }
        }
        if (current == Q_NULLPTR)
        {
            current = new CLIENT();
            current->type = "Control";
            current->connected = true;
            current->isAuthenticated = false;
            current->isStreaming = false;
            current->timeConnected = QDateTime::currentDateTime();
            current->ipAddress = datagram.senderAddress();
            current->port = datagram.senderPort();
            current->civPort = config.civPort;
            current->audioPort = config.audioPort;
            current->myId = controlId;
            current->remoteId = qFromLittleEndian<quint32>(r.mid(8, 4));
            current->socket = udpControl;
            current->pingSeq = (quint8)rand() << 8 | (quint8)rand();

            current->pingTimer = new QTimer();
            connect(current->pingTimer, &QTimer::timeout, this, std::bind(&udpServer::sendPing, this, &controlClients, current, (quint16)0x00, false));
            current->pingTimer->start(100);

            current->idleTimer = new QTimer();
            connect(current->idleTimer, &QTimer::timeout, this, std::bind(&udpServer::sendControl, this, current, (quint8)0x00, (quint16)0x00));
            current->idleTimer->start(100);

            current->wdTimer = new QTimer();
            connect(current->wdTimer, &QTimer::timeout, this, std::bind(&udpServer::watchdog, this, current));
            //current->wdTimer->start(1000);

            current->retransmitTimer = new QTimer();
            connect(current->retransmitTimer, &QTimer::timeout, this, std::bind(&udpServer::sendRetransmitRequest, this, current));
            current->retransmitTimer->start(RETRANSMIT_PERIOD);


            current->commonCap = 0x8010;
            qInfo(logUdpServer()) << current->ipAddress.toString() << ": New Control connection created";
            connMutex.lock();
            controlClients.append(current);
            connMutex.unlock();

        }

        current->lastHeard = QDateTime::currentDateTime();

        switch (r.length())
        {

        case (CONTROL_SIZE):
        {
            control_packet_t in = (control_packet_t)r.constData();
            if (in->type == 0x05)
            {
                qInfo(logUdpServer()) << current->ipAddress.toString() << ": Received 'disconnect' request";
                sendControl(current, 0x00, in->seq);
                //current->wdTimer->stop(); // Keep watchdog running to delete stale connection.
                deleteConnection(&controlClients, current);
            }
            break;
        }
        case (PING_SIZE):
        {
            ping_packet_t in = (ping_packet_t)r.constData();
            if (in->type == 0x07)
            {
                // It is a ping request/response

                if (in->reply == 0x00)
                {
                    current->rxPingTime = in->time;
                    sendPing(&controlClients, current, in->seq, true);
                }
                else if (in->reply == 0x01) {
                    if (in->seq == current->pingSeq || in->seq == current->pingSeq - 1)
                    {
                        // A Reply to our ping!
                        if (in->seq == current->pingSeq) {
                            current->pingSeq++;
                        }
                        else {
                            qInfo(logUdpServer()) << current->ipAddress.toString() << ": got out of sequence ping reply. Got: " << in->seq << " expecting: " << current->pingSeq;
                        }
                    }
                }
            }
            break;
        }
        case (TOKEN_SIZE):
        {
            // Token request
            token_packet_t in = (token_packet_t)r.constData();
            current->rxSeq = in->seq;
            current->authInnerSeq = in->innerseq;
            current->identa = in->identa;
            current->identb = in->identb;
            if (in->res == 0x02) {
                // Request for new token
                qInfo(logUdpServer()) << current->ipAddress.toString() << ": Received create token request";
                sendCapabilities(current);
                sendConnectionInfo(current);
            }
            else if (in->res == 0x01) {
                // Token disconnect
                qInfo(logUdpServer()) << current->ipAddress.toString() << ": Received token disconnect request";
                sendTokenResponse(current, in->res);
            }
            else if (in->res == 0x04) {
                // Disconnect audio/civ
                sendTokenResponse(current, in->res);
                current->isStreaming = false;
                sendConnectionInfo(current);
            }
            else {
                qInfo(logUdpServer()) << current->ipAddress.toString() << ": Received token request";
                sendTokenResponse(current, in->res);
            }
            break;
        }
        case (LOGIN_SIZE):
        {
            login_packet_t in = (login_packet_t)r.constData();
            qInfo(logUdpServer()) << current->ipAddress.toString() << ": Received 'login'";
            foreach(SERVERUSER user, config.users)
            {
                QByteArray usercomp;
                passcode(user.username, usercomp);
                QByteArray passcomp;
                passcode(user.password, passcomp);
                if (!strcmp(in->username, usercomp.constData()) && !strcmp(in->password, passcomp.constData()))
                {
                    current->isAuthenticated = true;
                    current->user = user;
                    break;
                }


            }
            // Generate login response
            current->rxSeq = in->seq;
            current->clientName = in->name;
            current->authInnerSeq = in->innerseq;
            current->tokenRx = in->tokrequest;
            current->tokenTx = (quint8)rand() | (quint8)rand() << 8 | (quint8)rand() << 16 | (quint8)rand() << 24;

            if (current->isAuthenticated) {
                qInfo(logUdpServer()) << current->ipAddress.toString() << ": User " << current->user.username << " login OK";
                sendLoginResponse(current, true);
            }
            else {
                qInfo(logUdpServer()) << current->ipAddress.toString() << ": Incorrect username/password";

                sendLoginResponse(current, false);
            }
            break;
        }
        case (CONNINFO_SIZE):
        {
            conninfo_packet_t in = (conninfo_packet_t)r.constData();
            qInfo(logUdpServer()) << current->ipAddress.toString() << ": Received request for radio connection";
            // Request to start audio and civ!
            current->isStreaming = true;
            current->rxSeq = in->seq;
            current->rxCodec = in->rxcodec;
            current->txCodec = in->txcodec;
            current->rxSampleRate = qFromBigEndian<quint32>(in->rxsample);
            current->txSampleRate = qFromBigEndian<quint32>(in->txsample);
            current->txBufferLen = qFromBigEndian<quint32>(in->txbuffer);
            current->authInnerSeq = in->innerseq;
            current->identa = in->identa;
            current->identb = in->identb;
            sendStatus(current);
            current->authInnerSeq = 0x00;
            sendConnectionInfo(current);
            qInfo(logUdpServer()) << current->ipAddress.toString() << ": rxCodec:" << current->rxCodec << " txCodec:" << current->txCodec <<
                " rxSampleRate" << current->rxSampleRate <<
                " txSampleRate" << current->rxSampleRate <<
                " txBufferLen" << current->txBufferLen;

            if (!config.lan) {
                // Radio is connected by USB/Serial and we assume that audio is connected as well.
                // Create audio TX/RX threads if they don't already exist (first client chooses samplerate/codec)

                audioSetup setup;
                setup.resampleQuality = config.resampleQuality;

                if (txaudio == Q_NULLPTR)
                {
                    outAudio.codec = current->txCodec;
                    outAudio.samplerate = current->txSampleRate;
                    outAudio.latency = current->txBufferLen;

                    txaudio = new audioHandler();
                    txAudioThread = new QThread(this);
                    txaudio->moveToThread(txAudioThread);

                    txAudioThread->start();

                    connect(this, SIGNAL(setupTxAudio(audioSetup)), txaudio, SLOT(init(audioSetup)));
                    connect(txAudioThread, SIGNAL(finished()), txaudio, SLOT(deleteLater()));

                    emit setupTxAudio(outAudio);
                    hasTxAudio = datagram.senderAddress();

                    connect(this, SIGNAL(haveAudioData(audioPacket)), txaudio, SLOT(incomingAudio(audioPacket)));

                }
                if (rxaudio == Q_NULLPTR)
                {
                    inAudio.codec = current->rxCodec;
                    inAudio.samplerate = current->rxSampleRate;

                    rxaudio = new audioHandler();
                    rxAudioThread = new QThread(this);
                    rxaudio->moveToThread(rxAudioThread);
                    rxAudioThread->start();

                    connect(this, SIGNAL(setupRxAudio(audioSetup)), rxaudio, SLOT(init(audioSetup)));
                    connect(rxAudioThread, SIGNAL(finished()), rxaudio, SLOT(deleteLater()));

                    emit setupRxAudio(inAudio);

                    rxAudioTimer = new QTimer();
                    rxAudioTimer->setTimerType(Qt::PreciseTimer);
                    connect(rxAudioTimer, &QTimer::timeout, this, std::bind(&udpServer::sendRxAudio, this));
                    rxAudioTimer->start(20);
                }

            }

            break;
        }
        default:
        {
            break;
        }
        }
        // Report current connections:
        emit haveNetworkStatus(QString("<pre>%1 current server connection(s)</pre>").arg(controlClients.size()));

        commonReceived(&controlClients, current, r);

    }
}


void udpServer::civReceived()
{
    while (udpCiv->hasPendingDatagrams()) {
        QNetworkDatagram datagram = udpCiv->receiveDatagram();
        QByteArray r = datagram.data();

        CLIENT* current = Q_NULLPTR;

        if (datagram.senderAddress().isNull() || datagram.senderPort() == 65535 || datagram.senderPort() == 0)
            return;

        QDateTime now = QDateTime::currentDateTime();

        bool userOK = false;
        foreach(CLIENT * client, controlClients)
        {
            if (client != Q_NULLPTR)
            {
                if (client->ipAddress == datagram.senderAddress() && client->isAuthenticated)
                {
                    userOK = true;
                }
            }
        }
        if (!userOK)
        {
            qDebug(logUdpServer()) << "user is NOT authenticated but attempted CI-V connection!";
        }

        foreach(CLIENT * client, civClients)
        {
            if (client != Q_NULLPTR)
            {
                if (client->ipAddress == datagram.senderAddress() && client->port == datagram.senderPort())
                {
                    current = client;
                }

            }
        }

        if (current == Q_NULLPTR)
        {
            current = new CLIENT();
            current->type = "CIV";
            current->civId = 0;
            current->connected = true;
            current->timeConnected = QDateTime::currentDateTime();
            current->ipAddress = datagram.senderAddress();
            current->port = datagram.senderPort();
            current->myId = civId;
            current->remoteId = qFromLittleEndian<quint32>(r.mid(8, 4));
            current->socket = udpCiv;
            current->pingSeq = (quint8)rand() << 8 | (quint8)rand();

            current->pingTimer = new QTimer();
            connect(current->pingTimer, &QTimer::timeout, this, std::bind(&udpServer::sendPing, this, &civClients, current, (quint16)0x00, false));
            current->pingTimer->start(100);

            current->idleTimer = new QTimer();
            connect(current->idleTimer, &QTimer::timeout, this, std::bind(&udpServer::sendControl, this, current, 0x00, (quint16)0x00));
            //current->idleTimer->start(100); // Start idleTimer after receiving iamready.

            current->wdTimer = new QTimer();
            connect(current->wdTimer, &QTimer::timeout, this, std::bind(&udpServer::watchdog, this, current));
            //current->wdTimer->start(1000);

            current->retransmitTimer = new QTimer();
            connect(current->retransmitTimer, &QTimer::timeout, this, std::bind(&udpServer::sendRetransmitRequest, this, current));
            current->retransmitTimer->start(RETRANSMIT_PERIOD);

            qInfo(logUdpServer()) << current->ipAddress.toString() << "(" << current->type << "): New connection created";
            connMutex.lock();
            civClients.append(current);
            connMutex.unlock();

        }

        switch (r.length())
        {
            /* case (CONTROL_SIZE):
            {
            }
            */
        case (PING_SIZE):
        {
            ping_packet_t in = (ping_packet_t)r.constData();
            if (in->type == 0x07)
            {
                // It is a ping request/response

                if (in->reply == 0x00)
                {
                    current->rxPingTime = in->time;
                    sendPing(&civClients, current, in->seq, true);
                }
                else if (in->reply == 0x01) {
                    if (in->seq == current->pingSeq || in->seq == current->pingSeq - 1)
                    {
                        // A Reply to our ping!
                        if (in->seq == current->pingSeq) {
                            current->pingSeq++;
                        }
                        else {
                            qInfo(logUdpServer()) << current->ipAddress.toString() << ": got out of sequence ping reply. Got: " << in->seq << " expecting: " << current->pingSeq;
                        }
                    }
                }
            }
            break;
        }
        default:
        {

            if (r.length() > 0x18) {
                data_packet_t in = (data_packet_t)r.constData();
                if (in->type != 0x01)
                {
                    if (quint16(in->datalen + 0x15) == (quint16)in->len)
                    {
                        // Strip all '0xFE' command preambles first:
                        int lastFE = r.lastIndexOf((char)0xfe);
                        //qInfo(logUdpServer()) << "Got:" << r.mid(lastFE);
                        if (current->civId == 0 && r.length() > lastFE + 2 && (quint8)r[lastFE+2] != 0xE1 && (quint8)r[lastFE + 2] > (quint8)0xdf && (quint8)r[lastFE + 2] < (quint8)0xef) {
                            // This is (should be) the remotes CIV id.
                            current->civId = (quint8)r[lastFE + 2];
                            qInfo(logUdpServer()) << current->ipAddress.toString() << ": Detected remote CI-V:" << hex << current->civId;
                        }
                        else if (current->civId != 0 && r.length() > lastFE + 2 && (quint8)r[lastFE+2] != 0xE1 && (quint8)r[lastFE + 2] != current->civId)
                        {
                            current->civId = (quint8)r[lastFE + 2];
                            qDebug(logUdpServer()) << current->ipAddress.toString() << ": Detected different remote CI-V:" << hex << current->civId;                            qInfo(logUdpServer()) << current->ipAddress.toString() << ": Detected different remote CI-V:" << hex << current->civId;
                        } else if (r.length() > lastFE+2 && (quint8)r[lastFE+2] != 0xE1) {
                            qDebug(logUdpServer()) << current->ipAddress.toString() << ": Detected invalid remote CI-V:" << hex << (quint8)r[lastFE+2];
			}

                        emit haveDataFromServer(r.mid(0x15));
                    }
                    else {
                        qInfo(logUdpServer()) << current->ipAddress.toString() << ": Datalen mismatch " << quint16(in->datalen + 0x15) << ":" << (quint16)in->len;

                    }
                }
            }
            //break;
        }
        }
        if (current != Q_NULLPTR) {
            udpServer::commonReceived(&civClients, current, r);
        }

    }
}

void udpServer::audioReceived()
{
    while (udpAudio->hasPendingDatagrams()) {
        QNetworkDatagram datagram = udpAudio->receiveDatagram();
        QByteArray r = datagram.data();
        CLIENT* current = Q_NULLPTR;

        if (datagram.senderAddress().isNull() || datagram.senderPort() == 65535 || datagram.senderPort() == 0)
            return;

        QDateTime now = QDateTime::currentDateTime();

        bool userOK = false;
        foreach(CLIENT * client, controlClients)
        {
            if (client != Q_NULLPTR)
            {
                if (client->ipAddress == datagram.senderAddress() && client->isAuthenticated)
                {
                    userOK = true;
                }
            }
        }
        if (!userOK)
        {
            qDebug(logUdpServer()) << "user is NOT authenticated but attempted CI-V connection!";
        }

        foreach(CLIENT * client, audioClients)
        {
            if (client != Q_NULLPTR)
            {
                if (client->ipAddress == datagram.senderAddress() && client->port == datagram.senderPort())
                {
                    current = client;
                }
            }
        }
        if (current == Q_NULLPTR)
        {
            current = new CLIENT();
            current->type = "Audio";
            current->connected = true;
            current->timeConnected = QDateTime::currentDateTime();
            current->ipAddress = datagram.senderAddress();
            current->port = datagram.senderPort();
            current->myId = audioId;
            current->remoteId = qFromLittleEndian<quint32>(r.mid(8, 4));
            current->socket = udpAudio;
            current->pingSeq = (quint8)rand() << 8 | (quint8)rand();

            current->pingTimer = new QTimer();
            connect(current->pingTimer, &QTimer::timeout, this, std::bind(&udpServer::sendPing, this, &audioClients, current, (quint16)0x00, false));
            current->pingTimer->start(100);

            current->wdTimer = new QTimer();
            connect(current->wdTimer, &QTimer::timeout, this, std::bind(&udpServer::watchdog, this, current));
            //current->wdTimer->start(1000);

            current->retransmitTimer = new QTimer();
            connect(current->retransmitTimer, &QTimer::timeout, this, std::bind(&udpServer::sendRetransmitRequest, this, current));
            current->retransmitTimer->start(RETRANSMIT_PERIOD);
            current->seqPrefix = 0;
            qInfo(logUdpServer()) << current->ipAddress.toString() << "(" << current->type << "): New connection created";
            connMutex.lock();
            audioClients.append(current);
            connMutex.unlock();

        }


        switch (r.length())
        {
        case (PING_SIZE):
        {
            ping_packet_t in = (ping_packet_t)r.constData();
            if (in->type == 0x07)
            {
                // It is a ping request/response

                if (in->reply == 0x00)
                {
                    current->rxPingTime = in->time;
                    sendPing(&audioClients, current, in->seq, true);
                }
                else if (in->reply == 0x01) {
                    if (in->seq == current->pingSeq || in->seq == current->pingSeq - 1)
                    {
                        // A Reply to our ping!
                        if (in->seq == current->pingSeq) {
                            current->pingSeq++;
                        }
                        else {
                            qInfo(logUdpServer()) << current->ipAddress.toString() << ": got out of sequence ping reply. Got: " << in->seq << " expecting: " << current->pingSeq;
                        }
                    }
                }
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

            if (in->type != 0x01 && in->len >= 0xAC) {
                if (in->seq == 0)
                {
                    // Seq number has rolled over.
                    current->seqPrefix++;
                }

                if (hasTxAudio == current->ipAddress)
                {
                    // 0xac is the smallest possible audio packet.
                    audioPacket tempAudio;
                    tempAudio.seq = (quint32)current->seqPrefix << 16 | in->seq;
                    tempAudio.time = QTime::currentTime();;
                    tempAudio.sent = 0;
                    tempAudio.data = r.mid(0x18);
                    //qInfo(logUdpServer()) << "sending tx audio " << in->seq;
                    emit haveAudioData(tempAudio);
                }
            }
            break;
        }

        }
        if (current != Q_NULLPTR) {
            udpServer::commonReceived(&audioClients, current, r);
        }
    }
}


void udpServer::commonReceived(QList<CLIENT*>* l, CLIENT* current, QByteArray r)
{
    Q_UNUSED(l); // We might need it later!
    if (current == Q_NULLPTR || r.isNull()) {
        return;
    }
    current->lastHeard = QDateTime::currentDateTime();
    if (r.length() < 0x10)
    {
        // Invalid packet
        qInfo(logUdpServer()) << current->ipAddress.toString() << "(" << current->type << "): Invalid packet received, len: " << r.length();
        return;
    }

    switch (r.length())
    {
    case (CONTROL_SIZE):
    {
        control_packet_t in = (control_packet_t)r.constData();
        if (in->type == 0x03) {
            qInfo(logUdpServer()) << current->ipAddress.toString() << "(" << current->type << "): Received 'are you there'";
            current->remoteId = in->sentid;
            sendControl(current, 0x04, in->seq);
        } // This is This is "Are you ready" in response to "I am here".
        else if (in->type == 0x06)
        {
            qInfo(logUdpServer()) << current->ipAddress.toString() << "(" << current->type << "): Received 'Are you ready'";
            current->remoteId = in->sentid;
            sendControl(current, 0x06, in->seq);
            if (current->idleTimer != Q_NULLPTR && !current->idleTimer->isActive()) {
                current->idleTimer->start(100);
            }
        } // This is a retransmit request
        else if (in->type == 0x01)
        {
            // Single packet request
            qInfo(logUdpServer()) << current->ipAddress.toString() << "(" << current->type << "): Received 'retransmit' request for " << in->seq;
            QMap<quint16, SEQBUFENTRY>::iterator match = current->txSeqBuf.find(in->seq);

            if (match != current->txSeqBuf.end() && match->retransmitCount < 5) {
                // Found matching entry?
                // Don't constantly retransmit the same packet, give-up eventually
                qInfo(logUdpServer()) << current->ipAddress.toString() << "(" << current->type << "): Sending retransmit of " << hex << match->seqNum;
                match->retransmitCount++;
                udpMutex.lock();
                current->socket->writeDatagram(match->data, current->ipAddress, current->port);
                udpMutex.unlock();
            }
            else {
                // Just send an idle!
                sendControl(current, 0x00, in->seq);
            }
        }
        break;
    }
    default:
    {
        //break;
    }
    }

    // The packet is at least 0x10 in length so safe to cast it to control_packet for processing
    control_packet_t in = (control_packet_t)r.constData();

    if (in->type == 0x01 && in->len != 0x10)
    {
        for (quint16 i = 0x10; i < r.length(); i = i + 2)
        {
            auto match = std::find_if(current->txSeqBuf.begin(), current->txSeqBuf.end(), [&cs = in->seq](SEQBUFENTRY& s) {
                return s.seqNum == cs;
            });
            if (match == current->txSeqBuf.end()) {
                qInfo(logUdpServer()) << current->ipAddress.toString() << "(" << current->type << "): Requested packet " << hex << in->seq << " not found";
                // Just send idle packet.
                sendControl(current, 0, in->seq);
            }
            else if (match->seqNum != 0x00)
            {
                // Found matching entry?
                // Send "untracked" as it has already been sent once.
                qInfo(logUdpServer()) << current->ipAddress.toString() << "(" << current->type << "): Sending retransmit of " << hex << match->seqNum;
                match->retransmitCount++;
                udpMutex.lock();
                current->socket->writeDatagram(match->data, current->ipAddress, current->port);
                udpMutex.unlock();
                match++;
            }
        }
    }
    else if (in->type == 0x00 && in->seq != 0x00)
    {
        //if (current->type == "CIV") {
        //    qInfo(logUdpServer()) << "Got:" << in->seq;
        //}
        if (current->rxSeqBuf.isEmpty())
        {
            current->rxMutex.lock();
            current->rxSeqBuf.insert(in->seq, QTime::currentTime());
            current->rxMutex.unlock();
        }
        else
        {
            
            if (in->seq < current->rxSeqBuf.firstKey())
            {
                qInfo(logUdpServer()) << current->ipAddress.toString() << "(" << current->type << "): ******* seq number may have rolled over ****** previous highest: " << hex << current->rxSeqBuf.lastKey() << " current: " << hex << in->seq;
                // Looks like it has rolled over so clear buffer and start again.
                current->rxMutex.lock();
                current->rxSeqBuf.clear();
                current->rxMutex.unlock();
                current->missMutex.lock();
                current->rxMissing.clear();
                current->missMutex.unlock();
                return;
            }

            if (!current->rxSeqBuf.contains(in->seq))
            {
                // Add incoming packet to the received buffer and if it is in the missing buffer, remove it.
                current->rxMutex.lock();
                if (current->rxSeqBuf.size() > 400)
                {
                    current->rxSeqBuf.remove(0);
                }
                current->rxSeqBuf.insert(in->seq, QTime::currentTime());
                current->rxMutex.unlock();
            } else{
                // Check whether this is one of our missing ones!
                current->missMutex.lock();
                QMap<quint16, int>::iterator s = current->rxMissing.find(in->seq);
                if (s != current->rxMissing.end())
                {
                    qInfo(logUdpServer()) << current->ipAddress.toString() << "(" << current->type << "): Missing SEQ has been received! " << hex << in->seq;
                    s = current->rxMissing.erase(s);
                }
                current->missMutex.unlock();
            }
        }
    }
}



void udpServer::sendControl(CLIENT* c, quint8 type, quint16 seq)
{

    //qInfo(logUdpServer()) << c->ipAddress.toString() << ": Sending control packet: " << type;

    control_packet p;
    memset(p.packet, 0x0, CONTROL_SIZE); // We can't be sure it is initialized with 0x00!
    p.len = sizeof(p);
    p.type = type;
    p.sentid = c->myId;
    p.rcvdid = c->remoteId;

    if (seq == 0x00)
    {
        p.seq = c->txSeq;
        SEQBUFENTRY s;
        s.seqNum = seq;
        s.timeSent = QTime::currentTime();
        s.retransmitCount = 0;
        s.data = QByteArray::fromRawData((const char*)p.packet, sizeof(p));
        c->txMutex.lock();
        c->txSeqBuf.insert(seq, s);
        c->txSeq++;
        c->txMutex.unlock();

        udpMutex.lock();
        c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
        udpMutex.unlock();
    }
    else {
        p.seq = seq;
        udpMutex.lock();
        c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
        udpMutex.unlock();
    }

    return;
}



void udpServer::sendPing(QList<CLIENT*>* l, CLIENT* c, quint16 seq, bool reply)
{
    // Also use to detect "stale" connections
    QDateTime now = QDateTime::currentDateTime();

    if (c->lastHeard.secsTo(now) > STALE_CONNECTION)
    {
        qInfo(logUdpServer()) << c->ipAddress.toString() << "(" << c->type << "): Deleting stale connection ";
        deleteConnection(l, c);
        return;
    }


    //qInfo(logUdpServer()) << c->ipAddress.toString() << ": Sending Ping";

    quint32 pingTime = 0;
    if (reply) {
        pingTime = c->rxPingTime;
    }
    else {
        pingTime = (quint32)timeStarted.msecsSinceStartOfDay();
        seq = c->pingSeq;
        // Don't increment pingseq until we receive a reply.
    }

    // First byte of pings "from" server can be either 0x00 or packet length!
    ping_packet p;
    memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
    if (reply) {
        p.len = sizeof(p);
    }
    p.type = 0x07;
    p.seq = seq;
    p.sentid = c->myId;
    p.rcvdid = c->remoteId;
    p.time = pingTime;
    p.reply = (char)reply;

    udpMutex.lock();
    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
    udpMutex.unlock();

    return;
}

void udpServer::sendLoginResponse(CLIENT* c, bool allowed)
{
    qInfo(logUdpServer()) << c->ipAddress.toString() << "(" << c->type << "): Sending Login response: " << c->txSeq;

    login_response_packet p;
    memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
    p.len = sizeof(p);
    p.type = 0x00;
    p.seq = c->txSeq;
    p.sentid = c->myId;
    p.rcvdid = c->remoteId;
    p.innerseq = c->authInnerSeq;
    p.tokrequest = c->tokenRx;
    p.token = c->tokenTx;
    p.code = 0x0250;


    if (!allowed) {
        p.error = 0xFEFFFFFF;
        if (c->idleTimer != Q_NULLPTR)
            c->idleTimer->stop();
        if (c->pingTimer != Q_NULLPTR)
            c->pingTimer->stop();
        if (c->retransmitTimer != Q_NULLPTR)
            c->retransmitTimer->stop();
        if (c->wdTimer != Q_NULLPTR)
            c->wdTimer->stop();
    }
    else {
        strcpy(p.connection, "WFVIEW");
    }

    SEQBUFENTRY s;
    s.seqNum = c->txSeq;
    s.timeSent = QTime::currentTime();
    s.retransmitCount = 0;
    s.data = QByteArray::fromRawData((const char*)p.packet, sizeof(p));
    c->txMutex.lock();
    c->txSeqBuf.insert(c->txSeq, s);
    c->txSeq++;
    c->txMutex.unlock();

    udpMutex.lock();
    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
    udpMutex.unlock();

    if (c->idleTimer != Q_NULLPTR)
        c->idleTimer->start(100);

    return;
}

void udpServer::sendCapabilities(CLIENT* c)
{
    qInfo(logUdpServer()) << c->ipAddress.toString() << "(" << c->type << "): Sending Capabilities :" << c->txSeq;

    capabilities_packet p;
    memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
    p.len = sizeof(p);
    p.type = 0x00;
    p.seq = c->txSeq;
    p.sentid = c->myId;
    p.rcvdid = c->remoteId;
    p.innerseq = c->authInnerSeq;
    p.tokrequest = c->tokenRx;
    p.token = c->tokenTx;
    p.code = 0x0298;
    p.res = 0x02;
    p.capa = 0x01;
    p.commoncap = c->commonCap;

    memcpy(p.macaddress, macAddress.toLocal8Bit(), 6);
    // IRU seems to expect an "Icom" mac address so replace the first 3 octets of our Mac with one in their range!
    memcpy(p.macaddress, QByteArrayLiteral("\x00\x90\xc7").constData(), 3);


    memcpy(p.name, rigCaps.modelName.toLocal8Bit(), rigCaps.modelName.length());
    memcpy(p.audio, QByteArrayLiteral("ICOM_VAUDIO").constData(), 11);

    if (rigCaps.hasWiFi && !rigCaps.hasEthernet) {
        p.conntype = 0x0707; // 0x0707 for wifi rig.
    }
    else {
        p.conntype = 0x073f; // 0x073f for ethernet rig.
    }

    p.civ = rigCaps.civ;
    p.baudrate = (quint32)qToBigEndian(config.baudRate);
    /*
        0x80 = 12K only
        0x40 = 44.1K only
        0x20 = 22.05K only
        0x10 = 11.025K only
        0x08 = 48K only
        0x04 = 32K only
        0x02 = 16K only
        0x01 = 8K only
    */
    if (rxaudio == Q_NULLPTR) {
        p.rxsample = 0x8b01; // all rx sample frequencies supported
    }
    else {
        if (rxSampleRate == 48000) {
            p.rxsample = 0x0800; // fixed rx sample frequency
        }
        else if (rxSampleRate == 32000) {
            p.rxsample = 0x0400;
        }
        else if (rxSampleRate == 24000) {
            p.rxsample = 0x0001;
        }
        else if (rxSampleRate == 16000) {
            p.rxsample = 0x0200;
        }
        else if (rxSampleRate == 12000) {
            p.rxsample = 0x8000;
        }
    }

    if (txaudio == Q_NULLPTR) {
        p.txsample = 0x8b01; // all tx sample frequencies supported
        p.enablea = 0x01; // 0x01 enables TX 24K mode?
        qInfo(logUdpServer()) << c->ipAddress.toString() << "(" << c->type << "): Client will have TX audio";
    }
    else {
        qInfo(logUdpServer()) << c->ipAddress.toString() << "(" << c->type << "): Disable tx audio for client";
        p.txsample = 0;
    }

    // I still don't know what these are?
    p.enableb = 0x01; // 0x01 doesn't seem to do anything?
    p.enablec = 0x01; // 0x01 doesn't seem to do anything?
    p.capf = 0x5001;
    p.capg = 0x0190;



    SEQBUFENTRY s;
    s.seqNum = p.seq;
    s.timeSent = QTime::currentTime();
    s.retransmitCount = 0;
    s.data = QByteArray::fromRawData((const char*)p.packet, sizeof(p));
    c->txMutex.lock();
    if (c->txSeqBuf.size() > 400)
    {
        c->txSeqBuf.remove(0);
    }
    c->txSeqBuf.insert(p.seq, s);
    c->txSeq++;
    c->txMutex.unlock();

    udpMutex.lock();
    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
    udpMutex.unlock();

    if (c->idleTimer != Q_NULLPTR)
        c->idleTimer->start(100);

    return;
}

// When client has requested civ/audio connection, this will contain their details
// Also used to display currently connected used information.
void udpServer::sendConnectionInfo(CLIENT* c)
{
    qInfo(logUdpServer()) << c->ipAddress.toString() << "(" << c->type << "): Sending ConnectionInfo :" << c->txSeq;
    conninfo_packet p;
    memset(p.packet, 0x0, sizeof(p));
    p.len = sizeof(p);
    p.type = 0x00;
    p.seq = c->txSeq;
    p.sentid = c->myId;
    p.rcvdid = c->remoteId;
    //p.innerseq = c->authInnerSeq; // Innerseq not used in user packet
    p.tokrequest = c->tokenRx;
    p.token = c->tokenTx;
    p.code = 0x0380;
    p.commoncap = c->commonCap;
    p.identa = c->identa;
    p.identb = c->identb;

    // 0x1a-0x1f is authid (random number?
    // memcpy(p + 0x40, QByteArrayLiteral("IC-7851").constData(), 7);

    memcpy(p.packet + 0x40, rigCaps.modelName.toLocal8Bit(), rigCaps.modelName.length());

    // This is the current streaming client (should we support multiple clients?)
    if (c->isStreaming) {
        p.busy = 0x01;
        memcpy(p.computer, c->clientName.constData(), c->clientName.length());
        p.ipaddress = qToBigEndian(c->ipAddress.toIPv4Address());
        p.identa = c->identa;
        p.identb = c->identb;
    }


    SEQBUFENTRY s;
    s.seqNum = p.seq;
    s.timeSent = QTime::currentTime();
    s.retransmitCount = 0;
    s.data = QByteArray::fromRawData((const char*)p.packet, sizeof(p));
 
    c->txMutex.lock();
    if (c->txSeqBuf.size() > 400)
    {
        c->txSeqBuf.remove(0);
    }
    c->txSeqBuf.insert(p.seq, s);
    c->txSeq++;
    c->txMutex.unlock();

    udpMutex.lock();
    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
    udpMutex.unlock();

    if (c->idleTimer != Q_NULLPTR)
        c->idleTimer->start(100);

    return;
}

void udpServer::sendTokenResponse(CLIENT* c, quint8 type)
{
    qInfo(logUdpServer()) << c->ipAddress.toString() << "(" << c->type << "): Sending Token response for type: " << type;

    token_packet p;
    memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
    p.len = sizeof(p);
    p.type = 0x00;
    p.seq = c->txSeq;
    p.sentid = c->myId;
    p.rcvdid = c->remoteId;
    p.innerseq = c->authInnerSeq;
    p.tokrequest = c->tokenRx;
    p.token = c->tokenTx;
    p.code = 0x0230;
    p.identa = c->identa;
    p.identb = c->identb;
    p.commoncap = c->commonCap;
    p.res = type;


    SEQBUFENTRY s;
    s.seqNum = p.seq;
    s.timeSent = QTime::currentTime();
    s.retransmitCount = 0;
    s.data = QByteArray::fromRawData((const char*)p.packet, sizeof(p));

    c->txMutex.lock();
    if (c->txSeqBuf.size() > 400)
    {
        c->txSeqBuf.remove(0);
    }
    c->txSeqBuf.insert(p.seq, s);
    c->txSeq++;
    c->txMutex.unlock();

    udpMutex.lock();
    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
    udpMutex.unlock();


    if (c->idleTimer != Q_NULLPTR)
        c->idleTimer->start(100);

    return;
}

#define PURGE_SECONDS 60

void udpServer::watchdog(CLIENT* c)
{
    Q_UNUSED(c);
    // Do something!

}

void udpServer::sendStatus(CLIENT* c)
{

    qInfo(logUdpServer()) << c->ipAddress.toString() << "(" << c->type << "): Sending Status";

    status_packet p;
    memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
    p.len = sizeof(p);
    p.type = 0x00;
    p.seq = c->txSeq;
    p.sentid = c->myId;
    p.rcvdid = c->remoteId;
    p.innerseq = c->authInnerSeq;
    p.tokrequest = c->tokenRx;
    p.token = c->tokenTx;
    p.code = 0x0240;
    p.res = 0x03;
    p.unknown = 0x1000;
    p.unusede = (char)0x80;
    p.identa = c->identa;
    p.identb = c->identb;

    p.civport = qToBigEndian(c->civPort);
    p.audioport = qToBigEndian(c->audioPort);

    // Send this to reject the request to tx/rx audio/civ
    //memcpy(p + 0x30, QByteArrayLiteral("\xff\xff\xff\xfe").constData(), 4);

    SEQBUFENTRY s;
    s.seqNum = p.seq;
    s.timeSent = QTime::currentTime();
    s.retransmitCount = 0;
    s.data = QByteArray::fromRawData((const char*)p.packet, sizeof(p));
    c->txMutex.lock();
    if (c->txSeqBuf.size() > 400)
    {
        c->txSeqBuf.remove(0);
    }
    c->txSeq++;
    c->txSeqBuf.insert(p.seq, s);
    c->txMutex.unlock();

    udpMutex.lock();
    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
    udpMutex.unlock();

}


void udpServer::dataForServer(QByteArray d)
{

    //qInfo(logUdpServer()) << "Server got:" << d;
    foreach(CLIENT * client, civClients)
    {
        int lastFE = d.lastIndexOf((quint8)0xfe);
        if (client != Q_NULLPTR && client->connected && d.length() > lastFE + 2 &&
		((quint8)d[lastFE + 1] == client->civId || (quint8)d[lastFE + 2] == client->civId ||
        (quint8)d[lastFE + 1] == 0x00 || (quint8)d[lastFE + 2]==0x00 || (quint8)d[lastFE + 1] == 0xE1 || (quint8)d[lastFE + 2] == 0xE1)) {
            data_packet p;
            memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
            p.len = (quint16)d.length() + sizeof(p);
            p.seq = client->txSeq;
            p.sentid = client->myId;
            p.rcvdid = client->remoteId;
            p.reply = (char)0xc1;
            p.datalen = (quint16)d.length();
            p.sendseq = client->innerSeq;
            QByteArray t = QByteArray::fromRawData((const char*)p.packet, sizeof(p));
            t.append(d);

            SEQBUFENTRY s;
            s.seqNum = p.seq;
            s.timeSent = QTime::currentTime();
            s.retransmitCount = 0;
            s.data = t;

            client->txMutex.lock();
            if (client->txSeqBuf.size() > 400)
            {
                client->txSeqBuf.remove(0);
            }
            client->txSeqBuf.insert(p.seq, s);
            client->txSeq++;
            client->innerSeq++;
            client->txMutex.unlock();

            udpMutex.lock();
            client->socket->writeDatagram(t, client->ipAddress, client->port);
            udpMutex.unlock();
        } else {
		qInfo(logUdpServer()) << "Got data for different ID" << hex << (quint8)d[lastFE+1] << ":" << hex << (quint8)d[lastFE+2];
	}
    }

    return;
}

void udpServer::sendRxAudio()
{
    QByteArray audio;
    if (rxaudio) {
        audio.clear();
        rxaudio->getNextAudioChunk(audio);
        int len = 0;
        while (len < audio.length()) {
            audioPacket partial;
            partial.data = audio.mid(len, 1364);
            receiveAudioData(partial);
            len = len + partial.data.length();
        }
    }
}



void udpServer::receiveAudioData(const audioPacket& d)
{
    //qInfo(logUdpServer()) << "Server got:" << d.data.length();
    foreach(CLIENT * client, audioClients)
    {
        if (client != Q_NULLPTR && client->connected) {
            audio_packet p;
            memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
            p.len = sizeof(p) + d.data.length();
            p.sentid = client->myId;
            p.rcvdid = client->remoteId;
            p.ident = 0x0080; // audio is always this?
            p.datalen = (quint16)qToBigEndian((quint16)d.data.length());
            p.sendseq = (quint16)qToBigEndian((quint16)client->sendAudioSeq); // THIS IS BIG ENDIAN!
            p.seq = client->txSeq;
            QByteArray t = QByteArray::fromRawData((const char*)p.packet, sizeof(p));
            t.append(d.data);

            SEQBUFENTRY s;
            s.seqNum = p.seq;
            s.timeSent = QTime::currentTime();
            s.retransmitCount = 0;
            s.data = t;
            client->txMutex.lock();
            if (client->txSeqBuf.size() > 400)
            {
                client->txSeqBuf.remove(0);
            }
            client->txSeqBuf.insert(p.seq, s);
            client->txSeq++;
            client->sendAudioSeq++;
            client->txMutex.unlock();

            udpMutex.lock();
            client->socket->writeDatagram(t, client->ipAddress, client->port);
            udpMutex.unlock();
        }
    }

    return;
}

/// <summary>
/// Find all gaps in received packets and then send requests for them.
/// This will run every 100ms so out-of-sequence packets will not trigger a retransmit request.
/// </summary>
/// <param name="c"></param>
void udpServer::sendRetransmitRequest(CLIENT* c)
{
    // Find all gaps in received packets and then send requests for them.
 // This will run every 100ms so out-of-sequence packets will not trigger a retransmit request.

    QByteArray missingSeqs;


    if (!c->rxSeqBuf.empty() && c->rxSeqBuf.size() <= c->rxSeqBuf.lastKey() - c->rxSeqBuf.firstKey())
    {
        if ((c->rxSeqBuf.lastKey() - c->rxSeqBuf.firstKey() - c->rxSeqBuf.size()) > 20)
        {
            // Too many packets to process, flush buffers and start again!
            qDebug(logUdp()) << "Too many missing packets, flushing buffer: " << c->rxSeqBuf.lastKey() << "missing=" << c->rxSeqBuf.lastKey() - c->rxSeqBuf.firstKey() - c->rxSeqBuf.size() + 1;
            c->missMutex.lock();
            c->rxMissing.clear();
            c->missMutex.unlock();

            c->rxMutex.lock();
            c->rxSeqBuf.clear();
            c->rxMutex.unlock();
        }
        else {
            // We have at least 1 missing packet!
            qDebug(logUdp()) << "Missing Seq: size=" << c->rxSeqBuf.size() << "firstKey=" << c->rxSeqBuf.firstKey() << "lastKey=" << c->rxSeqBuf.lastKey() << "missing=" << c->rxSeqBuf.lastKey() - c->rxSeqBuf.firstKey() - c->rxSeqBuf.size() + 1;
            // We are missing packets so iterate through the buffer and add the missing ones to missing packet list
            for (int i = 0; i < c->rxSeqBuf.keys().length() - 1; i++) {
                for (quint16 j = c->rxSeqBuf.keys()[i] + 1; j < c->rxSeqBuf.keys()[i + 1]; j++) {
                    auto s = c->rxMissing.find(j);
                    if (s == c->rxMissing.end())
                    {
                        // We haven't seen this missing packet before
                        qDebug(logUdp()) << this->metaObject()->className() << ": Adding to missing buffer (len=" << c->rxMissing.size() << "): " << j;

                        c->missMutex.lock();
                        c->rxMissing.insert(j, 0);
                        c->missMutex.unlock();

                        c->rxMutex.lock();
                        if (c->rxSeqBuf.size() > 400)
                        {
                            c->rxSeqBuf.remove(0);
                        }
                        c->rxSeqBuf.insert(j, QTime::currentTime()); // Add this missing packet to the rxbuffer as we now long about it.
                        c->rxMutex.unlock();
                    }
                    else {
                        if (s.value() == 4)
                        {
                            // We have tried 4 times to request this packet, time to give up!
                            c->missMutex.lock();
                            s = c->rxMissing.erase(s);
                            c->missMutex.unlock();
                        }

                    }
                }
            }
        }
    }

    c->missMutex.lock();
    for (auto it = c->rxMissing.begin(); it != c->rxMissing.end(); ++it)
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
    c->missMutex.unlock();

    if (missingSeqs.length() != 0)
    {
        control_packet p;
        memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
        p.type = 0x01;
        p.seq = 0x0000;
        p.sentid = c->myId;
        p.rcvdid = c->remoteId;
        if (missingSeqs.length() == 4) // This is just a single missing packet so send using a control.
        {
            p.seq = (missingSeqs[0] & 0xff) | (quint16)(missingSeqs[1] << 8);
            qDebug(logUdp()) << this->metaObject()->className() << ": sending request for missing packet : " << hex << p.seq;

            udpMutex.lock();
            c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
            udpMutex.unlock();
        }
        else
        {
            qDebug(logUdp()) << this->metaObject()->className() << ": sending request for multiple missing packets : " << missingSeqs.toHex();

            udpMutex.lock();
            missingSeqs.insert(0, p.packet, sizeof(p.packet));
            c->socket->writeDatagram(missingSeqs, c->ipAddress, c->port);
            udpMutex.unlock();
        }
    }


}


/// <summary>
/// This function is passed a pointer to the list of connection objects and a pointer to the object itself
/// Needs to stop and delete all timers, remove the connection from the list and delete the connection. 
/// </summary>
/// <param name="l"></param>
/// <param name="c"></param>
void udpServer::deleteConnection(QList<CLIENT*>* l, CLIENT* c)
{

    qInfo(logUdpServer()) << "Deleting connection to: " << c->ipAddress.toString() << ":" << QString::number(c->port);
    if (c->idleTimer != Q_NULLPTR) {
        c->idleTimer->stop();
        delete c->idleTimer;
    }
    if (c->pingTimer != Q_NULLPTR) {
        c->pingTimer->stop();
        delete c->pingTimer;
    }
    if (c->wdTimer != Q_NULLPTR) {
        c->wdTimer->stop();
        delete c->wdTimer;
    }

    if (c->retransmitTimer != Q_NULLPTR) {
        c->retransmitTimer->stop();
        delete c->retransmitTimer;
    }

    c->rxMutex.lock();
    c->rxSeqBuf.clear();
    c->rxMutex.unlock();

    c->txMutex.lock();
    c->txSeqBuf.clear();
    c->txMutex.unlock();

    c->missMutex.lock();
    c->rxMissing.clear();
    c->missMutex.unlock();


    connMutex.lock();
    QList<CLIENT*>::iterator it = l->begin();
    while (it != l->end()) {
        CLIENT* client = *it;
        if (client != Q_NULLPTR && client == c) {
            it = l->erase(it);
        }
        else {
            ++it;
        }
    }
    delete c; // Is this needed or will the erase have done it?
    c = Q_NULLPTR;
    qInfo(logUdpServer()) << "Current Number of clients connected: " << l->length();
    connMutex.unlock();

    if (l->length() == 0) {

        if (rxAudioTimer != Q_NULLPTR) {
            rxAudioTimer->stop();
            delete rxAudioTimer;
            rxAudioTimer = Q_NULLPTR;
        }

        if (rxAudioThread != Q_NULLPTR) {
            rxAudioThread->quit();
            rxAudioThread->wait();
            rxaudio = Q_NULLPTR;
            rxAudioThread = Q_NULLPTR;
        }

        if (txAudioThread != Q_NULLPTR) {
            txAudioThread->quit();
            txAudioThread->wait();
            txaudio = Q_NULLPTR;
            txAudioThread = Q_NULLPTR;
        }

    }
}
