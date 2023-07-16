#include "udpserver.h"
#include "logcategories.h"

#define STALE_CONNECTION 15
#define LOCK_PERIOD 10 // time to attempt to lock Mutex in ms
#define AUDIO_SEND_PERIOD 20 //
udpServer::udpServer(SERVERCONFIG* config, QObject* parent) :
    QObject(parent),
    config(config)
{
    qInfo(logUdpServer()) << "Starting udp server";
}

void udpServer::init()
{

    for (RIGCONFIG* rig : config->rigs)
    {
        qDebug(logUdpServer()) << "CIV:" << rig->civAddr;
        qDebug(logUdpServer()) << "Model:" << rig->modelName;
        qDebug(logUdpServer()) << "Name:" << rig->rigName;
        qDebug(logUdpServer()) << "CIV:" << rig->civAddr;
        qDebug(logUdpServer()).noquote() << QString("GUID: {%1%2%3%4-%5%6-%7%8-%9%10-%11%12%13%14%15%16}")
            .arg(rig->guid[0], 2, 16, QLatin1Char('0'))
            .arg(rig->guid[1], 2, 16, QLatin1Char('0'))
            .arg(rig->guid[2], 2, 16, QLatin1Char('0'))
            .arg(rig->guid[3], 2, 16, QLatin1Char('0'))
            .arg(rig->guid[4], 2, 16, QLatin1Char('0'))
            .arg(rig->guid[5], 2, 16, QLatin1Char('0'))
            .arg(rig->guid[6], 2, 16, QLatin1Char('0'))
            .arg(rig->guid[7], 2, 16, QLatin1Char('0'))
            .arg(rig->guid[8], 2, 16, QLatin1Char('0'))
            .arg(rig->guid[9], 2, 16, QLatin1Char('0'))
            .arg(rig->guid[10], 2, 16, QLatin1Char('0'))
            .arg(rig->guid[11], 2, 16, QLatin1Char('0'))
            .arg(rig->guid[12], 2, 16, QLatin1Char('0'))
            .arg(rig->guid[13], 2, 16, QLatin1Char('0'))
            .arg(rig->guid[14], 2, 16, QLatin1Char('0'))
            .arg(rig->guid[15], 2, 16, QLatin1Char('0'))
            ;
    }
    srand(time(NULL)); // Generate random 
    //timeStarted.start();
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

    QString macTemp;
    foreach(QNetworkInterface netInterface, QNetworkInterface::allInterfaces())
    {
        // Return only the first non-loopback MAC Address
        if (!(netInterface.flags() & QNetworkInterface::IsLoopBack)) {
            macTemp = netInterface.hardwareAddress();
        }
    }

    memcpy(&macAddress, macTemp.toLocal8Bit(), 6);
    memcpy(&macAddress, QByteArrayLiteral("\x00\x90\xc7").constData(), 3);

    uint32_t addr = localIP.toIPv4Address();

    qInfo(logUdpServer()) << "My IP Address:" << QHostAddress(addr).toString();


    controlId = (addr >> 8 & 0xff) << 24 | (addr & 0xff) << 16 | (config->controlPort & 0xffff);
    civId = (addr >> 8 & 0xff) << 24 | (addr & 0xff) << 16 | (config->civPort & 0xffff);
    audioId = (addr >> 8 & 0xff) << 24 | (addr & 0xff) << 16 | (config->audioPort & 0xffff);

    qInfo(logUdpServer()) << "Server Binding Control to: " << config->controlPort;
    udpControl = new QUdpSocket(this);
    udpControl->bind(config->controlPort);
    QUdpSocket::connect(udpControl, &QUdpSocket::readyRead, this, &udpServer::controlReceived);

    qInfo(logUdpServer()) << "Server Binding CIV to: " << config->civPort;
    udpCiv = new QUdpSocket(this);
    udpCiv->bind(config->civPort);
    QUdpSocket::connect(udpCiv, &QUdpSocket::readyRead, this, &udpServer::civReceived);

    qInfo(logUdpServer()) << "Server Binding Audio to: " << config->audioPort;
    udpAudio = new QUdpSocket(this);
    udpAudio->bind(config->audioPort);
    QUdpSocket::connect(udpAudio, &QUdpSocket::readyRead, this, &udpServer::audioReceived);

    wdTimer = new QTimer();
    connect(wdTimer, &QTimer::timeout, this, &udpServer::watchdog);
    wdTimer->start(500);
}

udpServer::~udpServer()
{
    qInfo(logUdpServer()) << "Closing udpServer";

    foreach(CLIENT * client, controlClients)
    {
        deleteConnection(&controlClients, client);

    }
    foreach(CLIENT * client, civClients)
    {
        deleteConnection(&civClients, client);
    }

    foreach(CLIENT * client, audioClients)
    {
        deleteConnection(&audioClients, client);
    }

    // Now all connections are deleted, close and delete the sockets
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
    status.message = QString("");
    emit haveNetworkStatus(status);
}


void udpServer::receiveRigCaps(rigCapabilities caps)
{
    for (RIGCONFIG* rig: config->rigs) {
        if (!memcmp(rig->guid, caps.guid, GUIDLEN) || config->rigs.size()==1) {
            // Matching rig, fill-in missing details
            rig->rigAvailable = true;
            rig->modelName = caps.modelName;
            rig->civAddr = caps.civ;
            if (rig->rigName == "<NONE>") {
                rig->rigName = caps.modelName;
            }
        }
    }
}


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
            current->civPort = config->civPort;
            current->audioPort = config->audioPort;
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

            current->retransmitTimer = new QTimer();
            connect(current->retransmitTimer, &QTimer::timeout, this, std::bind(&udpServer::sendRetransmitRequest, this, current));
            current->retransmitTimer->start(RETRANSMIT_PERIOD);

            qInfo(logUdpServer()) << current->ipAddress.toString() << ": New Control connection created";


            if (connMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
            {
                // Quick hack to replace the GUID with a MAC address. 
                if (config->rigs.size() == 1) {
                    memset(config->rigs.first()->guid, 0, GUIDLEN);
                    config->rigs.first()->commoncap = (quint16)0x8010;
                    memcpy(config->rigs.first()->macaddress, macAddress, 6);
                    memcpy(current->guid, config->rigs.first()->guid, GUIDLEN);
                }
                controlClients.append(current);
                connMutex.unlock();
            }
            else {
                qInfo(logUdpServer()) << "Unable to lock connMutex()";
            }
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

                if (current->audioClient != Q_NULLPTR) {
                    deleteConnection(&audioClients, current->audioClient);
                }
                if (current->civClient != Q_NULLPTR) {
                    deleteConnection(&civClients, current->civClient);
                }
                deleteConnection(&controlClients, current);
                return;
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
                    // A Reply to our ping!
                    if (in->seq == current->pingSeq) {
                        current->pingSeq++;
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
            memcpy(current->guid, in->guid, GUIDLEN);
            if (in->requesttype == 0x02 && in->requestreply == 0x01) {
                // Request for new token
                qInfo(logUdpServer()) << current->ipAddress.toString() << ": Received create token request";
                sendCapabilities(current);
                for (RIGCONFIG* radio : config->rigs) {
                    sendConnectionInfo(current, radio->guid);
                }
            }
            else if (in->requesttype == 0x01 && in->requestreply == 0x01) {
                // Token disconnect
                qInfo(logUdpServer()) << current->ipAddress.toString() << ": Received token disconnect request";
                sendTokenResponse(current, in->requesttype);
            }
            else if (in->requesttype == 0x04 && in->requestreply == 0x01) {
                // Disconnect audio/civ
                sendTokenResponse(current, in->requesttype);
                current->isStreaming = false;
                for (RIGCONFIG* radio : config->rigs) {
                    if (!memcmp(radio->guid, current->guid, GUIDLEN) || config->rigs.size() == 1)
                    {
                        sendConnectionInfo(current, radio->guid);
                    }
                }
            }
            else {
                qInfo(logUdpServer()) << current->ipAddress.toString() << ": Received token request";
                sendTokenResponse(current, in->requesttype);
            }
            break;
        }
        case (LOGIN_SIZE):
        {
            login_packet_t in = (login_packet_t)r.constData();
            qInfo(logUdpServer()) << current->ipAddress.toString() << ": Received 'login'";
            foreach(SERVERUSER user, config->users)
            {
                QByteArray usercomp;
                passcode(user.username, usercomp);
                QByteArray passcomp;
                passcode(user.password, passcomp);
                if (!user.username.trimmed().isEmpty() && !user.password.trimmed().isEmpty() && !strcmp(in->username, usercomp.constData()) && 
                    (!strcmp(in->password, user.password.toUtf8()) || !strcmp(in->password, passcomp.constData())))
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
            }
            else {
                qInfo(logUdpServer()) << current->ipAddress.toString() << ": Incorrect username/password";
            }
            sendLoginResponse(current, current->isAuthenticated);
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

            memcpy(current->guid, in->guid, GUIDLEN);
            sendStatus(current);
            current->authInnerSeq = 0x00;
            sendConnectionInfo(current,in->guid);
            qInfo(logUdpServer()) << current->ipAddress.toString() << ": rxCodec:" << current->rxCodec << " txCodec:" << current->txCodec <<
                " rxSampleRate" << current->rxSampleRate <<
                " txSampleRate" << current->txSampleRate <<
                " txBufferLen" << current->txBufferLen;


            audioSetup setup;
            setup.resampleQuality = config->resampleQuality;
            for (RIGCONFIG* radio : config->rigs) {
                if ((!memcmp(radio->guid, current->guid, GUIDLEN) || config->rigs.size()==1) && radio->txaudio == Q_NULLPTR && !config->lan)
                {
                    radio->txAudioSetup.codec = current->txCodec;
                    radio->txAudioSetup.sampleRate=current->txSampleRate;
                    radio->txAudioSetup.isinput = false;
                    radio->txAudioSetup.latency = current->txBufferLen;

                    outAudio.isinput = false;


                    if (radio->txAudioSetup.type == qtAudio) {
                        radio->txaudio = new audioHandler();
                    }
                    else if (radio->txAudioSetup.type == portAudio) {
                        radio->txaudio = new paHandler();
                    }
                    else if (radio->txAudioSetup.type == rtAudio) {
                        radio->txaudio = new rtHandler();
                    }
                    else
                    {
                        qCritical(logAudio()) << "Unsupported Transmit Audio Handler selected!";
                    }


                    if (radio->txaudio != Q_NULLPTR) {
                        radio->txAudioThread = new QThread(this);
                        radio->txAudioThread->setObjectName("txAudio()");


                        radio->txaudio->moveToThread(radio->txAudioThread);

                        radio->txAudioThread->start(QThread::TimeCriticalPriority);

                        connect(this, SIGNAL(setupTxAudio(audioSetup)), radio->txaudio, SLOT(init(audioSetup)));
                        connect(radio->txAudioThread, SIGNAL(finished()), radio->txaudio, SLOT(deleteLater()));

                        // Not sure how we make this work in QT5.9?
#if (QT_VERSION >= QT_VERSION_CHECK(5,10,0))
                        QMetaObject::invokeMethod(radio->txaudio, [=]() {
                            radio->txaudio->init(radio->txAudioSetup);
                        }, Qt::QueuedConnection);
#else
                        emit setupTxAudio(radio->txAudioSetup);
                        #warning "QT 5.9 is not fully supported multiple rigs will NOT work!"
#endif
                        hasTxAudio = datagram.senderAddress();

                        connect(this, SIGNAL(haveAudioData(audioPacket)), radio->txaudio, SLOT(incomingAudio(audioPacket)));
                    }
                }
                if ((!memcmp(radio->guid, current->guid, GUIDLEN) || config->rigs.size() == 1) && radio->rxaudio == Q_NULLPTR && !config->lan)
                {
                    radio->rxAudioSetup.codec = current->rxCodec;
                    radio->rxAudioSetup.sampleRate=current->rxSampleRate;
                    radio->rxAudioSetup.latency = current->txBufferLen;
                    radio->rxAudioSetup.isinput = true;
                    memcpy(radio->rxAudioSetup.guid, radio->guid, GUIDLEN);

                    //radio->rxaudio = new audioHandler();
                    if (radio->rxAudioSetup.type == qtAudio) {
                        radio->rxaudio = new audioHandler();
                    }
                    else if (radio->rxAudioSetup.type == portAudio) {
                        radio->rxaudio = new paHandler();
                    }
                    else if (radio->rxAudioSetup.type == rtAudio) {
                        radio->rxaudio = new rtHandler();
                    }
                    else
                    {
                        qCritical(logAudio()) << "Unsupported Receive Audio Handler selected!";
                    }

                    if (radio->rxaudio != Q_NULLPTR)
                    {

                        radio->rxAudioThread = new QThread(this);
                        radio->rxAudioThread->setObjectName("rxAudio()");

                        radio->rxaudio->moveToThread(radio->rxAudioThread);

                        radio->rxAudioThread->start(QThread::TimeCriticalPriority);

                        connect(radio->rxAudioThread, SIGNAL(finished()), radio->rxaudio, SLOT(deleteLater()));
                        connect(radio->rxaudio, SIGNAL(haveAudioData(audioPacket)), this, SLOT(receiveAudioData(audioPacket)));

#if (QT_VERSION >= QT_VERSION_CHECK(5,10,0))
                        QMetaObject::invokeMethod(radio->rxaudio, [=]() {
                            radio->rxaudio->init(radio->rxAudioSetup);
                        }, Qt::QueuedConnection);
#else
                        //#warning "QT 5.9 is not fully supported multiple rigs will NOT work!"
                        connect(this, SIGNAL(setupRxAudio(audioSetup)), radio->rxaudio, SLOT(init(audioSetup)));
                        setupRxAudio(radio->rxAudioSetup);
#endif

                    }
                }

            }

            break;
        }
        default:
        {
            break;
        }
        }

        // Connection "may" have been deleted so check before calling common function.
        if (current != Q_NULLPTR) {
            commonReceived(&controlClients, current, r);
        }
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
            foreach(CLIENT* client, controlClients)
            {
                if (client != Q_NULLPTR)
                {
                    if (client->ipAddress == datagram.senderAddress() && client->isAuthenticated && client->civClient == Q_NULLPTR)
                    {
                        current->controlClient = client;
                        client->civClient = current;
                        memcpy(current->guid, client->guid, GUIDLEN);
                    }
                }
            }

            if (current->controlClient == Q_NULLPTR || !current->controlClient->isAuthenticated)
            {
                // There is no current controlClient that matches this civClient 
                delete current;
                return;
            }

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

            current->retransmitTimer = new QTimer();
            connect(current->retransmitTimer, &QTimer::timeout, this, std::bind(&udpServer::sendRetransmitRequest, this, current));
            current->retransmitTimer->start(RETRANSMIT_PERIOD);

            qInfo(logUdpServer()) << current->ipAddress.toString() << "(" << current->type << "): New connection created";
            if (connMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
            {
                civClients.append(current);
                connMutex.unlock();
            }
            else {
                qInfo(logUdpServer()) << "Unable to lock connMutex()";
            }


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
                    // A Reply to our ping!
                    if (in->seq == current->pingSeq) {
                        current->pingSeq++;
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
                            qInfo(logUdpServer()) << current->ipAddress.toString() << ": Detected remote CI-V:" << QString("0x%1").arg(current->civId,0,16);
                        }
                        else if (current->civId != 0 && r.length() > lastFE + 2 && (quint8)r[lastFE+2] != 0xE1 && (quint8)r[lastFE + 2] != current->civId)
                        {
                            current->civId = (quint8)r[lastFE + 2];
                            qDebug(logUdpServer()) << current->ipAddress.toString() << ": Detected different remote CI-V:" << QString("0x%1").arg(current->civId,0,16);
                            qInfo(logUdpServer()) << current->ipAddress.toString() << ": Detected different remote CI-V:" << QString("0x%1").arg(current->civId,0,16);
                        } else if (r.length() > lastFE+2 && (quint8)r[lastFE+2] != 0xE1) {
                            qDebug(logUdpServer()) << current->ipAddress.toString() << ": Detected invalid remote CI-V:" << QString("0x%1").arg((quint8)r[lastFE+2],0,16);
			            }

                        for (RIGCONFIG* radio : config->rigs) {
                            if (!memcmp(radio->guid, current->guid, sizeof(radio->guid)) || config->rigs.size()==1)
                            {
                                // Only send to the rig that it belongs to!
                                //qDebug(logUdpServer()) << "Sending data" << r.mid(0x15);
#if (QT_VERSION >= QT_VERSION_CHECK(5,10,0))
                                QMetaObject::invokeMethod(radio->rig, [=]() {
                                    radio->rig->dataFromServer(r.mid(0x15));;
                                }, Qt::DirectConnection);
#else
                                #warning "QT 5.9 is not fully supported, multiple rigs will NOT work!"
                                emit haveDataFromServer(r.mid(0x15));
#endif

                            }
                        }

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
            foreach(CLIENT* client, controlClients)
            {
                if (client != Q_NULLPTR)
                {
                    if (client->ipAddress == datagram.senderAddress() && client->isAuthenticated && client->audioClient == Q_NULLPTR)
                    {
                        current->controlClient = client;
                        client->audioClient = current;
                        memcpy(current->guid, client->guid, GUIDLEN);
                    }
                }
            }

            if (current->controlClient == Q_NULLPTR || !current->controlClient->isAuthenticated)
            {
                // There is no current controlClient that matches this audioClient 
                delete current;
                return;
            }

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
            current->pingTimer->start(PING_PERIOD);

            current->retransmitTimer = new QTimer();
            connect(current->retransmitTimer, &QTimer::timeout, this, std::bind(&udpServer::sendRetransmitRequest, this, current));
            current->retransmitTimer->start(RETRANSMIT_PERIOD);
            current->seqPrefix = 0;
            qInfo(logUdpServer()) << current->ipAddress.toString() << "(" << current->type << "): New connection created";
            if (connMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
            {
                audioClients.append(current);
                connMutex.unlock();
            }
            else {
                qInfo(logUdpServer()) << "Unable to lock connMutex()";
            }

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
                    // A Reply to our ping!
                    if (in->seq == current->pingSeq) {
                        current->pingSeq++;
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

            if (in->type != 0x01) { 
                // Opus packets can be smaller than this! && in->len >= 0xAC) {
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
                    //txaudio->incomingAudio(tempAudio);

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
            qInfo(logUdpServer()) << current->ipAddress.toString() << "(" << current->type << "): Received 'Are you there'";
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
        } // This is a single packet retransmit request
        else if (in->type == 0x01 && in->len == 0x10)
        {
            auto match = current->txSeqBuf.find(in->seq);

            if (match != current->txSeqBuf.end() && match->retransmitCount < 5) {
                // Found matching entry?
                // Don't constantly retransmit the same packet, give-up eventually
                qInfo(logUdpServer()) << current->ipAddress.toString() << "(" << current->type << "): Sending (single packet) retransmit of " << QString("0x%1").arg(match->seqNum, 0, 16);
                match->retransmitCount++;
                if (udpMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
                {
                    current->socket->writeDatagram(match->data, current->ipAddress, current->port);
                    udpMutex.unlock();
                }
                else {
                    qInfo(logUdpServer()) << "Unable to lock udpMutex()";
                }

            }
            else {
                // Just send an idle!
                qInfo(logUdpServer()) << current->ipAddress.toString() << "(" << current->type << 
                    "): Requested (single) packet " << QString("0x%1").arg(in->seq, 0, 16) << 
                    "not found, have " << QString("0x%1").arg(current->txSeqBuf.firstKey(), 0, 16) <<
                    "to" << QString("0x%1").arg(current->txSeqBuf.lastKey(), 0, 16);
                sendControl(current, 0x00, in->seq);
            }
        }
        break;
    }
    default:
    {
        //break
    }
    }

    // The packet is at least 0x10 in length so safe to cast it to control_packet for processing
    control_packet_t in = (control_packet_t)r.constData();

    if (in->type == 0x01 && in->len != 0x10)
    {
        for (quint16 i = 0x10; i < r.length(); i = i + 2)
        {
            quint16 seq = (quint8)r[i] | (quint8)r[i + 1] << 8;
            auto match = current->txSeqBuf.find(seq);
            if (match == current->txSeqBuf.end()) {
                qInfo(logUdpServer()) << current->ipAddress.toString() << "(" << current->type << 
                    "): Requested (multiple) packet " << QString("0x%1").arg(seq,0,16) << 
                    "not found, have " << QString("0x%1").arg(current->txSeqBuf.firstKey(), 0, 16) <<
                    "to" << QString("0x%1").arg(current->txSeqBuf.lastKey(), 0, 16);

                // Just send idle packet.
                sendControl(current, 0, in->seq);
            }
            else if (match->seqNum != 0x00)
            {
                // Found matching entry?
                // Send "untracked" as it has already been sent once.
                qInfo(logUdpServer()) << current->ipAddress.toString() << "(" << current->type << "): Sending (multiple packet) retransmit of " << QString("0x%1").arg(match->seqNum,0,16);
                match->retransmitCount++;
                if (udpMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
                {
                    current->socket->writeDatagram(match->data, current->ipAddress, current->port);
                    udpMutex.unlock();
                }
                else {
                    qInfo(logUdpServer()) << "Unable to lock udpMutex()";
                }
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
            if (current->rxMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
            {
                current->rxSeqBuf.insert(in->seq, QTime::currentTime());
                current->rxMutex.unlock();
            }
            else {
                qInfo(logUdpServer()) << "Unable to lock rxMutex()";
            }

        }
        else
        {
            
            if (in->seq < current->rxSeqBuf.firstKey() || in->seq - current->rxSeqBuf.lastKey() > MAX_MISSING)
            {
                qInfo(logUdpServer()) << current->ipAddress.toString() << "(" << current->type << "Large seq number gap detected, previous highest: " <<
                    QString("0x%1").arg(current->rxSeqBuf.lastKey(), 0, 16) << " current: " << QString("0x%1").arg(in->seq, 0, 16);
                // Looks like it has rolled over so clear buffer and start again.
                if (current->rxMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
                {
                    current->rxSeqBuf.clear();
                    current->rxMutex.unlock();
                }
                else {
                    qInfo(logUdpServer()) << "Unable to lock rxMutex()";
                }

                if (current->missMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
                {
                    current->rxMissing.clear();
                    current->missMutex.unlock();
                }
                else {
                    qInfo(logUdpServer()) << "Unable to lock missMutex()";
                }

                return;
            }

            if (!current->rxSeqBuf.contains(in->seq))
            {
                if (current->rxMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
                {
                    // Add incoming packet to the received buffer and if it is in the missing buffer, remove it.
                    //int missCounter = 0;
                    if (in->seq > current->rxSeqBuf.lastKey() + 1) {
                        qInfo(logUdpServer()) << current->ipAddress.toString() << "(" << current->type << "1 or more missing packets detected, previous: " <<
                            QString("0x%1").arg(current->rxSeqBuf.lastKey(), 0, 16) << " current: " << QString("0x%1").arg(in->seq, 0, 16);
                        // We are likely missing packets then!
                        if (current->missMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
                        {

                            for (quint16 f = current->rxSeqBuf.lastKey() + 1; f <= in->seq; f++)
                            {

                                if (current->rxSeqBuf.size() > BUFSIZE)
                                {
                                    current->rxSeqBuf.remove(current->rxSeqBuf.firstKey());
                                }
                                current->rxSeqBuf.insert(f, QTime::currentTime());

                                if (f != in->seq && !current->rxMissing.contains(f))
                                {
                                    current->rxMissing.insert(f, 0);
                                }
                            }
                            current->missMutex.unlock();
                        }
                        else {
                            qInfo(logUdpServer()) << "Unable to lock missMutex()";
                        }
                    }
                    else {

                        if (current->rxSeqBuf.size() > BUFSIZE)
                        {
                            current->rxSeqBuf.remove(current->rxSeqBuf.firstKey());
                        }
                        current->rxSeqBuf.insert(in->seq, QTime::currentTime());
                    }
                    current->rxMutex.unlock();
                }
                else {
                    qInfo(logUdpServer()) << "Unable to lock rxMutex()";
                }

            } else{
                // Check whether this is one of our missing ones!
                if (current->missMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
                {
                    auto s = current->rxMissing.find(in->seq);
                    if (s != current->rxMissing.end())
                    {
                        qInfo(logUdpServer()) << current->ipAddress.toString() << "(" << current->type << "): Missing SEQ has been received! " << QString("0x%1").arg(in->seq,0,16);
                        s = current->rxMissing.erase(s);
                    }
                    current->missMutex.unlock();
                }
                else {
                    qInfo(logUdpServer()) << "Unable to lock missMutex()";
                }
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
        s.seqNum = c->txSeq;
        s.timeSent = QTime::currentTime();
        s.retransmitCount = 0;
        s.data = QByteArray::fromRawData((const char*)p.packet, sizeof(p));
        if (c->txMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
        {
            if (c->txSeq == 0) {
                c->txSeqBuf.clear();
            }
            else
                if (c->txSeqBuf.size() > BUFSIZE)
            {
                c->txSeqBuf.remove(c->txSeqBuf.firstKey());
            }

            c->txSeqBuf.insert(c->txSeq, s);
            c->txSeq++;
            c->txMutex.unlock();
        }
        else {
            qInfo(logUdpServer()) << "Unable to lock txMutex()";
        }

        if (udpMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
        {
            c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
            udpMutex.unlock();
        }
        else {
            qInfo(logUdpServer()) << "Unable to lock udpMutex()";
        }

    }
    else {
        p.seq = seq;
        if (udpMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
        {
            c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
            udpMutex.unlock();
        }
        else {
            qInfo(logUdpServer()) << "Unable to lock udpMutex()";
        }

    }

    return;
}



void udpServer::sendPing(QList<CLIENT*>* l, CLIENT* c, quint16 seq, bool reply)
{
    Q_UNUSED(l);
    /*
    QDateTime now = QDateTime::currentDateTime();

    if (c->lastHeard.secsTo(now) > STALE_CONNECTION)
    {
        qInfo(logUdpServer()) << c->ipAddress.toString() << "(" << c->type << "): Deleting stale connection ";
        deleteConnection(l, c);
        return;
    }

    */

    //qInfo(logUdpServer()) << c->ipAddress.toString() << ": Sending Ping";

    quint32 pingTime = 0;
    if (reply) {
        pingTime = c->rxPingTime;
    }
    else {
        QTime now=QTime::currentTime();
        pingTime = (quint32)now.msecsSinceStartOfDay();
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

    if (udpMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
    {
        c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
        udpMutex.unlock();
    }
    else {
        qInfo(logUdpServer()) << "Unable to lock udpMutex()";
    }

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
    p.payloadsize = qToBigEndian((quint16)(sizeof(p) - 0x10));
    p.requesttype = 0x00;
    p.requestreply = 0x01;


    if (!allowed) {
        p.error = 0xFEFFFFFF;
        if (c->idleTimer != Q_NULLPTR)
            c->idleTimer->stop();
        if (c->pingTimer != Q_NULLPTR)
            c->pingTimer->stop();
        if (c->retransmitTimer != Q_NULLPTR)
            c->retransmitTimer->stop();
    }
    else {
        strcpy(p.connection, "WFVIEW");
        //strcpy(p.connection, "FTTH");
    }

    SEQBUFENTRY s;
    s.seqNum = c->txSeq;
    s.timeSent = QTime::currentTime();
    s.retransmitCount = 0;
    s.data = QByteArray::fromRawData((const char*)p.packet, sizeof(p));
    if (c->txMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
    {
        if (c->txSeq == 0) {
            c->txSeqBuf.clear();
        }
        else if (c->txSeqBuf.size() > BUFSIZE)
        {
            c->txSeqBuf.remove(c->txSeqBuf.firstKey());
        }

        c->txSeqBuf.insert(c->txSeq, s);
        c->txSeq++;
        c->txMutex.unlock();
    }
    else {
        qInfo(logUdpServer()) << "Unable to lock txMutex()";
    }

    if (udpMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
    {
        c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
        udpMutex.unlock();
    }
    else {
        qInfo(logUdpServer()) << "Unable to lock udpMutex()";
    }

    if (c->idleTimer != Q_NULLPTR)
        c->idleTimer->start(100);

    return;
}

void udpServer::sendCapabilities(CLIENT* c)
{
    capabilities_packet p;
    memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
    p.type = 0x00;
    p.seq = c->txSeq;
    p.sentid = c->myId;
    p.rcvdid = c->remoteId;
    p.innerseq = c->authInnerSeq;
    p.tokrequest = c->tokenRx;
    p.token = c->tokenTx;
    p.requesttype = 0x02;
    p.requestreply = 0x02;
    p.numradios = qToBigEndian((quint16)config->rigs.size());
    SEQBUFENTRY s;
    s.seqNum = p.seq;
    s.timeSent = QTime::currentTime();
    s.retransmitCount = 0;

    for (RIGCONFIG* rig : config->rigs) {
        qInfo(logUdpServer()) << c->ipAddress.toString() << "(" << c->type << "): Sending Capabilities :" << c->txSeq << "for" << rig->modelName;
        radio_cap_packet r;
        memset(r.packet, 0x0, sizeof(r)); // We can't be sure it is initialized with 0x00!
        
        memcpy(r.guid, rig->guid, GUIDLEN);

        memcpy(r.name, rig->rigName.toLocal8Bit(), sizeof(r.name));
        memcpy(r.audio, QByteArrayLiteral("ICOM_VAUDIO").constData(), 11);

        if (rig->hasWiFi && !rig->hasEthernet) {
            r.conntype = 0x0707; // 0x0707 for wifi rig->
        }
        else {
            r.conntype = 0x073f; // 0x073f for ethernet rig->
        }

        r.civ = rig->civAddr;
        r.baudrate = (quint32)qToBigEndian(rig->baudRate);
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
        if (rig->rxaudio == Q_NULLPTR) {
            r.rxsample = 0x8b01; // all rx sample frequencies supported
        }
        else {
            if (rxSampleRate == 48000) {
                r.rxsample = 0x0800; // fixed rx sample frequency
            }
            else if (rxSampleRate == 32000) {
                r.rxsample = 0x0400;
            }
            else if (rxSampleRate == 24000) {
                r.rxsample = 0x0001;
            }
            else if (rxSampleRate == 16000) {
                r.rxsample = 0x0200;
            }
            else if (rxSampleRate == 12000) {
                r.rxsample = 0x8000;
            }
        }

        if (rig->txaudio == Q_NULLPTR) {
            r.txsample = 0x8b01; // all tx sample frequencies supported
            r.enablea = 0x01; // 0x01 enables TX 24K mode?
            qInfo(logUdpServer()) << c->ipAddress.toString() << "(" << c->type << "): Client will have TX audio";
        }
        else {
            qInfo(logUdpServer()) << c->ipAddress.toString() << "(" << c->type << "): Disable tx audio for client";
            r.txsample = 0;
        }

        // I still don't know what these are?
        r.enableb = 0x01; // 0x01 doesn't seem to do anything?
        r.enablec = 0x01; // 0x01 doesn't seem to do anything?
        r.capf = 0x5001;
        r.capg = 0x0190;
        s.data.append(QByteArray::fromRawData((const char*)r.packet, sizeof(r)));
    }

    p.len = (quint32)sizeof(p)+s.data.length();
    p.payloadsize = qToBigEndian((quint16)(sizeof(p) + s.data.length() - 0x10));

    s.data.insert(0,QByteArray::fromRawData((const char*)p.packet, sizeof(p)));

    if (c->txMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
    {
        if (c->txSeq == 0) {
            c->txSeqBuf.clear();
        }
        else if (c->txSeqBuf.size() > BUFSIZE)
        {
            c->txSeqBuf.remove(c->txSeqBuf.firstKey());
        }
        c->txSeqBuf.insert(c->txSeq, s);
        c->txSeq++;
        c->txMutex.unlock();
    }
    else {
        qInfo(logUdpServer()) << "Unable to lock txMutex()";
    }

    if (udpMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
    {
        c->socket->writeDatagram((const char*)s.data, s.data.length(), c->ipAddress, c->port);
        udpMutex.unlock();
    }
    else {
        qInfo(logUdpServer()) << "Unable to lock udpMutex()";
    }


    if (c->idleTimer != Q_NULLPTR)
        c->idleTimer->start(100);

    return;
}

// When client has requested civ/audio connection, this will contain their details
// Also used to display currently connected used information.
void udpServer::sendConnectionInfo(CLIENT* c, quint8 guid[GUIDLEN])
{
    for (RIGCONFIG* radio : config->rigs) {
        if (!memcmp(guid, radio->guid, GUIDLEN) || config->rigs.size()==1)
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
            p.payloadsize = qToBigEndian((quint16)(sizeof(p) - 0x10));
            p.requesttype = 0x00;
            p.requestreply = 0x03;

            memcpy(p.guid, radio->guid, GUIDLEN);
            memcpy(p.name, radio->rigName.toLocal8Bit(), sizeof(p.name));

            if (radio->rigAvailable) {
                if (c->isStreaming) {
                    p.busy = 0x01;
                }
                else {
                    p.busy = 0x00;
                }
            }
            else
            {
                p.busy = 0x02;
            }
 
            // This is the current streaming client (should we support multiple clients?)
            if (c->isStreaming) {
                memcpy(p.computer, c->clientName.constData(), c->clientName.length());
                p.ipaddress = qToBigEndian(c->ipAddress.toIPv4Address());
 
            }


            SEQBUFENTRY s;
            s.seqNum = p.seq;
            s.timeSent = QTime::currentTime();
            s.retransmitCount = 0;
            s.data = QByteArray::fromRawData((const char*)p.packet, sizeof(p));

            if (c->txMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
            {
                if (c->txSeq == 0) {
                    c->txSeqBuf.clear();
                }
                else if (c->txSeqBuf.size() > BUFSIZE)
                {
                    c->txSeqBuf.remove(c->txSeqBuf.firstKey());
                }
                c->txSeqBuf.insert(c->txSeq, s);
                c->txSeq++;
                c->txMutex.unlock();
            }
            else {
                qInfo(logUdpServer()) << "Unable to lock txMutex()";
            }


            if (udpMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
            {
                c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
                udpMutex.unlock();
            }
            else {
                qInfo(logUdpServer()) << "Unable to lock udpMutex()";
            }

        }
    }
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
    p.payloadsize = qToBigEndian((quint16)(sizeof(p) - 0x10));

    memcpy(p.guid, c->guid, GUIDLEN);
    p.requesttype = type;
    p.requestreply = 0x02;

    SEQBUFENTRY s;
    s.seqNum = p.seq;
    s.timeSent = QTime::currentTime();
    s.retransmitCount = 0;
    s.data = QByteArray::fromRawData((const char*)p.packet, sizeof(p));

    if (c->txMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
    {
        if (c->txSeq == 0) {
            c->txSeqBuf.clear();
        }
        else if (c->txSeqBuf.size() > BUFSIZE)
        {
            c->txSeqBuf.remove(c->txSeqBuf.firstKey());
        }
        c->txSeqBuf.insert(c->txSeq, s);
        c->txSeq++;
        c->txMutex.unlock();
    }
    else {
        qInfo(logUdpServer()) << "Unable to lock txMutex()";
    }


    if (udpMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
    {
        c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
        udpMutex.unlock();
    }
    else {
        qInfo(logUdpServer()) << "Unable to lock udpMutex()";
    }


    if (c->idleTimer != Q_NULLPTR)
        c->idleTimer->start(100);

    return;
}

void udpServer::watchdog()
{
    QDateTime now = QDateTime::currentDateTime();

    foreach(CLIENT * client, audioClients)
    {
        if (client != Q_NULLPTR)
        {
            if (client->lastHeard.secsTo(now) > STALE_CONNECTION)
            {
                qInfo(logUdpServer()) << client->ipAddress.toString() << "(" << client->type << "): Deleting stale connection ";
                deleteConnection(&audioClients, client);
            }
        }
        else {
            qInfo(logUdpServer()) << "Current client is NULL!";
        }
    }

    foreach(CLIENT* client, civClients)
    {
        if (client != Q_NULLPTR)
        {
            if (client->lastHeard.secsTo(now) > STALE_CONNECTION)
            {
                qInfo(logUdpServer()) << client->ipAddress.toString() << "(" << client->type << "): Deleting stale connection ";
                deleteConnection(&civClients, client);
            }
        }
        else {
            qInfo(logUdpServer()) << "Current client is NULL!";
        }
    }

    foreach(CLIENT* client, controlClients)
    {
        if (client != Q_NULLPTR)
        {
            if (client->lastHeard.secsTo(now) > STALE_CONNECTION)
            {
                qInfo(logUdpServer()) << client->ipAddress.toString() << "(" << client->type << "): Deleting stale connection ";
                deleteConnection(&controlClients, client);
            }
        }
        else {
            qInfo(logUdpServer()) << "Current client is NULL!";
        }
    }
    if (!config->lan) {
        status.message = QString("<pre>Server connections: Control:%1 CI-V:%2 Audio:%3</pre>").arg(controlClients.size()).arg(civClients.size()).arg(audioClients.size());
        emit haveNetworkStatus(status);
    }
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
    p.payloadsize = qToBigEndian((quint16)(sizeof(p) - 0x10));
    p.requestreply = 0x02;
    p.requesttype = 0x03;
    memcpy(p.guid, c->guid, GUIDLEN); // May be MAC address OR guid.


    p.civport = qToBigEndian(c->civPort);
    p.audioport = qToBigEndian(c->audioPort);

    // Send this to reject the request to tx/rx audio/civ
    //memcpy(p + 0x30, QByteArrayLiteral("\xff\xff\xff\xfe").constData(), 4);

    SEQBUFENTRY s;
    s.seqNum = p.seq;
    s.timeSent = QTime::currentTime();
    s.retransmitCount = 0;
    s.data = QByteArray::fromRawData((const char*)p.packet, sizeof(p));
    if (c->txMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
    {
        if (c->txSeq == 0) {
            c->txSeqBuf.clear();
        }
        else if (c->txSeqBuf.size() > BUFSIZE)
        {
            c->txSeqBuf.remove(c->txSeqBuf.firstKey());
        }
        c->txSeq++;
        c->txSeqBuf.insert(c->txSeq, s);
        c->txMutex.unlock();
    }
    else {
        qInfo(logUdpServer()) << "Unable to lock txMutex()";
    }


    if (udpMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
    {
        c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
        udpMutex.unlock();
    }
    else {
        qInfo(logUdpServer()) << "Unable to lock udpMutex()";
    }

}


void udpServer::dataForServer(QByteArray d)
{
    rigCommander* sender = qobject_cast<rigCommander*>(QObject::sender());

    if (sender == Q_NULLPTR)
    {
        return;
    }

    for (CLIENT* client : civClients)
    {
        if (client == Q_NULLPTR || !client->connected)
        {
            continue;
        }
        // Use the GUID to determine which radio the response is from
        if (memcmp(sender->getGUID(), client->guid, GUIDLEN) && config->rigs.size()>1)
        {
            continue; // Rig guid doesn't match the one requested by the client.
        }

        int lastFE = d.lastIndexOf((quint8)0xfe);
        //qInfo(logUdpServer()) << "Server got CIV data from" << config->rigs.first()->rigName << "length" << d.length() << d.toHex();
        if (client->connected && d.length() > lastFE + 2 &&
                ((quint8)d[lastFE + 1] == client->civId || (quint8)d[lastFE + 2] == client->civId ||
                (quint8)d[lastFE + 1] == 0x00 || (quint8)d[lastFE + 2] == 0x00 || (quint8)d[lastFE + 1] == 0xE1 || (quint8)d[lastFE + 2] == 0xE1))
        {
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

            if (client->txMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
            {
                if (client->txSeq == 0) {
                    client->txSeqBuf.clear();
                }
                else if (client->txSeqBuf.size() > BUFSIZE)
                {
                    client->txSeqBuf.remove(client->txSeqBuf.firstKey());
                }
                client->txSeqBuf.insert(client->txSeq, s);
                client->txSeq++;
                //client->innerSeq = (qToBigEndian(qFromBigEndian(client->innerSeq) + 1));
                client->txMutex.unlock();
            }
            else {
                qInfo(logUdpServer()) << "Unable to lock txMutex()";
            }


            if (udpMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
            {
                client->socket->writeDatagram(t, client->ipAddress, client->port);
                udpMutex.unlock();
            }
            else {
                qInfo(logUdpServer()) << "Unable to lock udpMutex()";
            }
        }
        else {
            qInfo(logUdpServer()) << "Got data for different ID" << 
                QString("0x%1").arg((quint8)d[lastFE + 1],0,16) << ":" << QString("0x%1").arg((quint8)d[lastFE + 2],0,16);
        }
    }
    return;
}


void udpServer::receiveAudioData(const audioPacket& d)
{
    rigCommander* sender = qobject_cast<rigCommander*>(QObject::sender());
    quint8 guid[GUIDLEN];
    if (sender != Q_NULLPTR)
    {
        memcpy(guid, sender->getGUID(), GUIDLEN);
    }
    else {
        memcpy(guid, d.guid, GUIDLEN);
    }
    //qInfo(logUdpServer()) << "Server got:" << d.data.length();
    foreach(CLIENT * client, audioClients)
    {
        int len = 0;
        while (len < d.data.length()) {
            QByteArray partial;
            partial = d.data.mid(len, 1364);
            len = len + partial.length();
            if (client != Q_NULLPTR && client->connected && (!memcmp(client->guid, guid, GUIDLEN) || config->rigs.size()== 1)) {
                audio_packet p;
                memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
                p.len = (quint32)sizeof(p) + partial.length();
                p.sentid = client->myId;
                p.rcvdid = client->remoteId;
                p.ident = 0x0080; // audio is always this?
                p.datalen = (quint16)qToBigEndian((quint16)partial.length());
                p.sendseq = (quint16)qToBigEndian((quint16)client->sendAudioSeq); // THIS IS BIG ENDIAN!
                p.seq = client->txSeq;
                QByteArray t = QByteArray::fromRawData((const char*)p.packet, sizeof(p));
                t.append(partial);

                SEQBUFENTRY s;
                s.seqNum = p.seq;
                s.timeSent = QTime::currentTime();
                s.retransmitCount = 0;
                s.data = t;
                if (client->txMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
                {
                    if (client->txSeq == 0) {
                        client->txSeqBuf.clear();
                    }
                    else if (client->txSeqBuf.size() > BUFSIZE)
                    {
                        client->txSeqBuf.remove(client->txSeqBuf.firstKey());
                    }
                    client->txSeqBuf.insert(client->txSeq, s);
                    client->txSeq++;
                    client->sendAudioSeq++;
                    client->txMutex.unlock();
                }
                else {
                    qInfo(logUdpServer()) << "Unable to lock txMutex()";
                }


                if (udpMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
                {
                    client->socket->writeDatagram(t, client->ipAddress, client->port);
                    udpMutex.unlock();
                }
                else {
                    qInfo(logUdpServer()) << "Unable to lock udpMutex()";
                }

            }
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

    if (c->rxMissing.isEmpty()) {
        return;
    }
    else if (c->rxMissing.size() > MAX_MISSING) {
        qInfo(logUdp()) << "Too many missing packets," << c->rxMissing.size() << "flushing all buffers";
        c->rxMutex.lock();
        c->rxSeqBuf.clear();
        c->rxMutex.unlock();
        c->missMutex.lock();
        c->rxMissing.clear();
        c->missMutex.unlock();
        return;
    }


    QByteArray missingSeqs;

    //QTime missingTime = QTime::currentTime();

    if (c->missMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
    {
        auto it = c->rxMissing.begin();
        while (it != c->rxMissing.end())
        {
            if (it.key() != 0x00)
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
                    // We have tried 4 times to request this packet, time to give up!
                    qInfo(logUdp()) << this->metaObject()->className() << ": No response for missing packet" << QString("0x%1").arg(it.key(), 0, 16) << "deleting";
                    it = c->rxMissing.erase(it);
                }
            } else {
                 qInfo(logUdp()) << this->metaObject()->className() << ": found empty key in missing buffer";
                 it++;
            }
        }

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
                p.len = sizeof(p);
                p.seq = (missingSeqs[0] & 0xff) | (quint16)(missingSeqs[1] << 8);
                qInfo(logUdp()) << this->metaObject()->className() << ": sending request for missing packet : " << QString("0x%1").arg(p.seq,0,16);

                if (udpMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
                {
                    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
                    udpMutex.unlock();
                }
                else {
                    qInfo(logUdpServer()) << "Unable to lock udpMutex()";
                }

            }
            else
            {
                qInfo(logUdp()) << this->metaObject()->className() << ": sending request for multiple missing packets : " << missingSeqs.toHex();

                p.len = (quint32)sizeof(p) + missingSeqs.size();
                missingSeqs.insert(0, p.packet, sizeof(p));

                if (udpMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
                {
                    c->socket->writeDatagram(missingSeqs, c->ipAddress, c->port);
                    udpMutex.unlock();
                }
                else {
                    qInfo(logUdpServer()) << "Unable to lock udpMutex()";
                }

            }
        }
        c->missMutex.unlock();
    }
    else {
        qInfo(logUdpServer()) << "Unable to lock missMutex()";
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

    quint8 guid[GUIDLEN];
    memcpy(guid, c->guid, GUIDLEN);
    int len = l->length();

    qInfo(logUdpServer()) << "Deleting" << c->type << "connection to: " << c->ipAddress.toString() << ":" << QString::number(c->port);
    if (c->idleTimer != Q_NULLPTR) {
        c->idleTimer->stop();
        delete c->idleTimer;
    }
    if (c->pingTimer != Q_NULLPTR) {
        c->pingTimer->stop();
        delete c->pingTimer;
    }

    if (c->retransmitTimer != Q_NULLPTR) {
        c->retransmitTimer->stop();
        delete c->retransmitTimer;
    }

    if (c->rxMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
    {
        c->rxSeqBuf.clear();
        c->rxMutex.unlock();
    }
    else {
        qInfo(logUdpServer()) << "Unable to lock rxMutex()";
    }


    if (c->txMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
    {
        c->txSeqBuf.clear();
        c->txMutex.unlock();
    }
    else {
        qInfo(logUdpServer()) << "Unable to lock txMutex()";
    }


    if (c->missMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
    {
        c->rxMissing.clear();
        c->missMutex.unlock();
    }
    else {
        qInfo(logUdpServer()) << "Unable to lock missMutex()";
    }


    if (connMutex.try_lock_for(std::chrono::milliseconds(LOCK_PERIOD)))
    {
        QList<CLIENT*>::iterator it = l->begin();
        while (it != l->end()) {
            CLIENT* client = *it;
            if (client != Q_NULLPTR && client == c) {
                qInfo(logUdpServer()) << "Found" << client->type << "connection to: " << client->ipAddress.toString() << ":" << QString::number(client->port);
                it = l->erase(it);
                len--;
            }
            else {
                ++it;
            }
        }
        delete c; // Is this needed or will the erase have done it?
        c = Q_NULLPTR;
        qInfo(logUdpServer()) << "Current Number of clients connected: " << l->length();
        connMutex.unlock();
    }
    else {
        qInfo(logUdpServer()) << "Unable to lock connMutex()";
    }

    if (len == 0) {
        for (RIGCONFIG* radio : config->rigs) {
            if (!memcmp(radio->guid, guid, GUIDLEN) || config->rigs.size() == 1)
            {

                if (radio->rxAudioThread != Q_NULLPTR) {
                    radio->rxAudioThread->quit();
                    radio->rxAudioThread->wait();
                    radio->rxaudio = Q_NULLPTR;
                    radio->rxAudioThread = Q_NULLPTR;
                }

                if (radio->txAudioThread != Q_NULLPTR) {
                    radio->txAudioThread->quit();
                    radio->txAudioThread->wait();
                    radio->txaudio = Q_NULLPTR;
                    radio->txAudioThread = Q_NULLPTR;
                }
            }
        }
    }
}
