#include "udpserver.h"

#define STALE_CONNECTION 15

udpServer::udpServer(SERVERCONFIG config) :
    config(config)
{
    qDebug() << "Starting udp server";
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

    uint32_t addr = localIP.toIPv4Address();

    qDebug() << " Got: " << QHostAddress(addr).toString();


    controlId = (addr >> 8 & 0xff) << 24 | (addr & 0xff) << 16 | (config.controlPort & 0xffff);
    civId = (addr >> 8 & 0xff) << 24 | (addr & 0xff) << 16 | (config.civPort & 0xffff);
    audioId = (addr >> 8 & 0xff) << 24 | (addr & 0xff) << 16 | (config.audioPort & 0xffff);

    udpControl = new QUdpSocket(this);
    udpControl->bind(config.controlPort); 

    udpCiv = new QUdpSocket(this);
    udpAudio = new QUdpSocket(this);

    udpAudio->bind(config.audioPort);
    udpCiv->bind(config.civPort);

    qDebug() << "Server Binding Control to: " << config.controlPort;
    qDebug() << "Server Binding CIV to: " << config.civPort;
    qDebug() << "Server Binding Audio to: " << config.audioPort;


    QUdpSocket::connect(udpControl, &QUdpSocket::readyRead, this, &udpServer::controlReceived);
    QUdpSocket::connect(udpAudio, &QUdpSocket::readyRead, this, &udpServer::audioReceived);
    QUdpSocket::connect(udpCiv, &QUdpSocket::readyRead, this, &udpServer::civReceived);

}

udpServer::~udpServer()
{
    qDebug() << "Closing udpServer";


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
        delete client;
        audioClients.removeAll(client);
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
            current->connected = true;
            current->isStreaming = false;
            current->timeConnected = QDateTime::currentDateTime();
            current->ipAddress = datagram.senderAddress();
            current->port = datagram.senderPort();
            current->civPort = config.civPort;
            current->audioPort = config.audioPort;
            current->myId = controlId;
            current->remoteId = qFromLittleEndian<quint32>(r.mid(8, 4));
            current->socket = udpControl;
            current->innerPingSeq = (quint16)rand();
            current->pingSeq = (quint8)rand() << 8 | (quint8)rand();
            current->pingTimer = new QTimer();
            current->idleTimer = new QTimer();
            current->wdTimer = new QTimer();
            connect(current->pingTimer, &QTimer::timeout, this, std::bind(&udpServer::sendPing, this, &controlClients, current, (quint16)0x00, false));
            connect(current->idleTimer, &QTimer::timeout, this, std::bind(&udpServer::sendControl, this, current, (quint8)0x00, (quint16)0x00));
            connect(current->wdTimer, &QTimer::timeout, this, std::bind(&udpServer::sendWatchdog, this, current));
            current->pingTimer->start(100);
            current->idleTimer->start(100);
            current->wdTimer->start(10000);
            current->commonCap = 0x8010;
            qDebug() << "New Control connection created from :" << current->ipAddress.toString() << ":" << QString::number(current->port);
            controlClients.append(current);
        }

        current->lastHeard = QDateTime::currentDateTime();

        switch (r.length())
        {
            case (CONTROL_SIZE):
            {
                control_packet_t in = (control_packet_t)r.constData();
                if (in->type == 0x03)
                {
                    qDebug() << current->ipAddress.toString() << ": Received 'are you there'";
                    current->remoteId = in->sentid;
                    sendControl(current,0x04,in->seq);
                } // This is This is "Are you ready" in response to "I am here".
                else if (in->type == 0x06)
                {
                    qDebug() << current->ipAddress.toString() << ": Received 'Are you ready'";
                    current->remoteId = in->sentid;
                    sendControl(current,0x06,in->seq);
                } // This is a retransmit request
                else if (in->type == 0x01)
                {
                    // Just send an idle for now!
                    qDebug() << current->ipAddress.toString() << ": Received 'retransmit' request for " << in->seq;
                    sendControl(current,0x00, in->seq);

                } // This is a disconnect request
                else if (in->type == 0x05)
                {
                    qDebug() << current->ipAddress.toString() << ": Received 'disconnect' request";
                    sendControl(current, 0x00, in->seq);
                    //current->wdTimer->stop(); // Keep watchdog running to delete stale connection.
                    deleteConnection(&controlClients, current);
                }
                break;
            }
            case (WATCHDOG_SIZE):
            {
                //watchdog_packet_t in = (watchdog_packet_t)r.constData();
                // Watchdog packet.
                break;
            }
            case (PING_SIZE):
            {
                ping_packet_t in = (ping_packet_t)r.constData();
                if (in->type == 0x07)
                {
                    // It is a ping request/response

                    if (r[16] == (char)0x00)
                    {
                        current->rxPingTime = qFromLittleEndian<quint32>(r.mid(0x11, 4));
                        sendPing(&controlClients, current, in->seq, true);
                    }
                    else if (r[16] == (char)0x01) {
                        // A Reply to our ping!
                        if (in->seq == current->pingSeq) {
                            current->pingSeq++;
                        }
                        else {
                            qDebug() << current->ipAddress.toString() << ": Server got out of sequence ping reply. Got: " << in->seq << " expecting: " << current->pingSeq;
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
                if (in->res == 0x02) {
                    // Request for new token
                    qDebug() << current->ipAddress.toString() << ": Received create token request";
                    sendCapabilities(current);
                    sendConnectionInfo(current);
                }
                else if (in->res == 0x01) {
                    // Token disconnect
                    qDebug() << current->ipAddress.toString() << ": Received token disconnect request";
                    sendTokenResponse(current, in->res);
                }
                else {
                    qDebug() << current->ipAddress.toString() << ": Received token request";
                    sendTokenResponse(current, in->res);
                }
                break;
            }
            case (LOGIN_SIZE):
            {
                login_packet_t in = (login_packet_t)r.constData();
                qDebug() << current->ipAddress.toString() << ": Received 'login'";
                bool userOk = false;
                foreach(SERVERUSER user, config.users)
                {
                    QByteArray usercomp;
                    passcode(user.username, usercomp);
                    QByteArray passcomp;
                    passcode(user.password, passcomp);
                    if (!strcmp(in->username, usercomp.constData()) && !strcmp(in->password, passcomp.constData()))
                    {
                        userOk = true;
                        current->user = user;
                        break;
                    }
 

                }
                // Generate login response
                current->rxSeq = in->seq;
                current->clientName = in->name;
                current->authInnerSeq = in->innerseq;
                current->tokenRx = in->tokrequest;
                current->tokenTx =(quint8)rand() | (quint8)rand() << 8 | (quint8)rand() << 16 | (quint8)rand() << 24;

                if (userOk) {
                    qDebug() << current->ipAddress.toString() << ": User " << current->user.username << " login OK";
                    sendLoginResponse(current, in->seq, true);
                }
                else {
                    qDebug() << current->ipAddress.toString() << ": Incorrect username/password";

                    sendLoginResponse(current, in->seq, false);
                }
                break;
            }
            case (CONNINFO_SIZE):
            {
                conninfo_packet_t in = (conninfo_packet_t)r.constData();
                qDebug() << current->ipAddress.toString() << ": Received request for radio connection";
                // Request to start audio and civ!
                current->isStreaming = true;
                current->rxSeq = in->seq;
                current->rxCodec = in->rxcodec;
                current->txCodec = in->txcodec;
                current->rxSampleRate = qFromBigEndian<quint16>(in->rxsample);
                current->txSampleRate = qFromBigEndian<quint16>(in->txsample);
                current->txBufferLen = qFromBigEndian<quint16>(in->txbuffer);
                current->authInnerSeq = in->innerseq;
                current->connSeq = in->identb;
                sendStatus(current);
                current->authInnerSeq = 0x00; 
                sendConnectionInfo(current);

                break;
            }
            default:
            {
                qDebug() << "Unknown length packet received: " << r.length();
                break;
            }
        }
    }
}


void udpServer::civReceived()
{
    while (udpCiv->hasPendingDatagrams()) {
        QNetworkDatagram datagram = udpCiv->receiveDatagram();
        QByteArray r = datagram.data();

        CLIENT* current = Q_NULLPTR;

        qDebug() << "Got CIV data";
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
            current->connected = true;
            current->timeConnected = QDateTime::currentDateTime();
            current->ipAddress = datagram.senderAddress();
            current->port = datagram.senderPort();
            current->myId = civId;
            current->remoteId = qFromLittleEndian<quint32>(r.mid(8, 4));
            current->socket = udpCiv;
            current->innerPingSeq = (quint16)rand();
            current->pingSeq = (quint8)rand() << 8 | (quint8)rand();
            current->pingTimer = new QTimer();
            current->idleTimer = new QTimer();
            current->wdTimer = new QTimer();
            connect(current->pingTimer, &QTimer::timeout, this, std::bind(&udpServer::sendPing, this, &civClients, current, (quint16)0x00, false));
            connect(current->idleTimer, &QTimer::timeout, this, std::bind(&udpServer::sendControl, this, current,0x00, (quint16)0x00));
            current->pingTimer->start(100);
            current->idleTimer->start(100);
            qDebug() << "New CIV connection created from :" << current->ipAddress.toString() << ":" << QString::number(current->port);
            civClients.append(current);
        }

        current->lastHeard = QDateTime::currentDateTime();
        quint16 gotSeq = qFromLittleEndian<quint16>(r.mid(0x06, 2));

        switch (r.length())
        {
            case (CONTROL_SIZE):
            {
                control_packet_t in = (control_packet_t)r.constData();

                if (in->type == 0x03) {
                    qDebug() << current->ipAddress.toString() << ": Received 'are you there'";
                    current->remoteId = qFromLittleEndian<quint32>(r.mid(8, 4));
                    sendControl(current, 0x04, gotSeq);
                } // This is This is "Are you ready" in response to "I am here".
                else if (in->type == 0x06)
                {
                    qDebug() << current->ipAddress.toString() << ": Received 'Are you ready'";
                    current->remoteId = qFromLittleEndian<quint32>(r.mid(8, 4));
                    sendControl(current, 0x06, gotSeq);
                } // This is a retransmit request
                else if (in->type == 0x01)
                {
                    // Just send an idle for now, we need to be able to retransmit missing packets.
                    qDebug() << current->ipAddress.toString() << ": Received 'retransmit' request for " << gotSeq;
                    sendControl(current, 0x00, gotSeq);

                } // This is a disconnect request
                else if (in->type == 0x05)
                {
                    qDebug() << current->ipAddress.toString() << ": Received 'disconnect' request";
                    sendControl(current, 0x00, gotSeq);
                    deleteConnection(&civClients, current);

                }
                break;
            }
            case (WATCHDOG_SIZE):
            {
                // Watchdog packet.
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
                        current->rxPingTime = qFromLittleEndian<quint32>(r.mid(0x11, 4));
                        sendPing(&civClients, current, gotSeq, true);
                    }
                    else if (in->reply == 0x01) {
                        // A Reply to our ping!
                        if (gotSeq == current->pingSeq || gotSeq == current->pingSeq - 1) {
                            current->pingSeq++;
                        }
                        else {
                            qDebug() << current->ipAddress.toString() << ": Civ got out of sequence ping reply. Got: " << gotSeq << " expecting: " << current->pingSeq;
                        }
                    }
                }
                break;
            }
            default:
            {
                if (r.length() > 21) {
                    // First check if we are missing any packets?
                    quint8 temp = r[0] - 0x15;
                    if ((quint8)r[16] == 0xc1 && (quint8)r[17] == temp)
                    {
                        //qDebug() << "Got CIV from server: " << r.mid(21);
                        emit haveDataFromServer(r.mid(21));
                    }
                }


                break;
            }
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
            current->connected = true;
            current->timeConnected = QDateTime::currentDateTime();
            current->ipAddress = datagram.senderAddress();
            current->port = datagram.senderPort();
            current->myId = audioId;
            current->remoteId = qFromLittleEndian<quint32>(r.mid(8, 4));
            current->socket = udpAudio;
            current->innerPingSeq = (quint16)rand();
            current->pingSeq = (quint8)rand() << 8 | (quint8)rand();
            current->pingTimer = new QTimer();
            connect(current->pingTimer, &QTimer::timeout, this, std::bind(&udpServer::sendPing, this, &audioClients, current, (quint16)0x00, false));
            current->pingTimer->start(100);
            qDebug() << "New Audio connection created from :" << current->ipAddress.toString() << ":" << QString::number(current->port);
            audioClients.append(current);
        }

        current->lastHeard = QDateTime::currentDateTime();
        quint16 gotSeq = qFromLittleEndian<quint16>(r.mid(0x06, 2));

        switch (r.length())
        {
            case (CONTROL_SIZE):
            {
                control_packet_t in = (control_packet_t)r.constData();
                if (in->type == 0x03) {
                    qDebug() << current->ipAddress.toString() << ": Received 'are you there'";
                    current->remoteId = qFromLittleEndian<quint32>(r.mid(8, 4));
                    sendControl(current, 0x04, gotSeq);
                } // This is This is "Are you ready" in response to "I am here".
                else if (in->type == 0x06)
                {
                    qDebug() << current->ipAddress.toString() << ": Received 'Are you ready'";
                    current->remoteId = qFromLittleEndian<quint32>(r.mid(8, 4));
                    sendControl(current, 0x06, gotSeq);
                } // This is a retransmit request
                else if (in->type == 0x01)
                {
                    // Just send an idle for now!
                    qDebug() << current->ipAddress.toString() << ": Received 'retransmit' request for " << gotSeq;
                    sendControl(current, 0x00, gotSeq);

                } // This is a disconnect request
                else if (in->type == 0x05)
                {
                    qDebug() << current->ipAddress.toString() << ": Received 'disconnect' request";
                    sendControl(current, 0x00, gotSeq);
                    deleteConnection(&audioClients, current);
                }
                break;
            }
            case (WATCHDOG_SIZE):
            {
                // Watchdog packet.
                break;
            }
            case (0x15):
            {
                ping_packet_t in = (ping_packet_t)r.constData();
                if (in->type == 0x07)
                {
                    // It is a ping request/response

                    if (in->reply == 0x00)
                    {
                        current->rxPingTime = qFromLittleEndian<quint32>(r.mid(0x11, 4));
                        sendPing(&audioClients, current, gotSeq, true);
                    } 
                    else if (in->reply == 0x01) {
                        if (gotSeq == current->pingSeq || gotSeq == current->pingSeq - 1)
                        {
                            // A Reply to our ping!
                            if (gotSeq == current->pingSeq) {
                                current->pingSeq++;
                            }
                            else {
                                qDebug() << current->ipAddress.toString() << ": Civ got out of sequence ping reply. Got: " << gotSeq << " expecting: " << current->pingSeq;
                            }
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

    }
}




void udpServer::sendControl(CLIENT* c, quint8 type, quint16 seq)
{
    if (seq == 0x00)
    {
        seq = c->txSeq;
        c->txSeq++;
    }

    //qDebug() << c->ipAddress.toString() << ": Sending control packet: " << type;
    control_packet p;
    memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
    p.len = sizeof(p);
    p.type = type;
    p.seq = seq;
    p.sentid = c->myId;
    p.rcvdid = c->remoteId;

    QMutexLocker locker(&mutex);
    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
    return;
}



void udpServer::sendPing(QList<CLIENT*> *l,CLIENT* c, quint16 seq, bool reply)
{
    QMutexLocker locker(&mutex);
    // Also use to detect "stale" connections
    QDateTime now = QDateTime::currentDateTime();

    if (c->lastHeard.secsTo(now) > STALE_CONNECTION)
    {
        qDebug() << "Deleting stale connection " << c->ipAddress.toString();
        deleteConnection(l, c);
        return;
    }


    //qDebug() << c->ipAddress.toString() << ": Sending Ping";

    quint32 pingTime = 0;
    quint8 pingReply = 0;
    if (reply) {
        pingTime = c->rxPingTime;
        pingReply = 1;
    }
    else {
        pingTime = (quint32)timeStarted.msecsSinceStartOfDay();
        seq = c->pingSeq;
    }

    // First byte of pings "from" server can be either 0x00 or packet length!
    ping_packet p;
    memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
    p.len = sizeof(p);
    p.type = 0x07;
    p.seq = seq;
    p.sentid = c->myId;
    p.rcvdid = c->remoteId;
    p.time = pingTime;
    p.reply = pingReply;

    c->innerPingSeq++;

    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
    return;
}

void udpServer::sendLoginResponse(CLIENT* c,quint16 seq, bool allowed)
{
    qDebug() << c->ipAddress.toString() << ": Sending Login response: " << c->txSeq;

    login_response_packet p;
    memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
    p.len = sizeof(p);
    p.type = 0x00;
    p.seq = seq;
    p.sentid = c->myId;
    p.rcvdid = c->remoteId;
    p.innerseq = c->authInnerSeq;
    p.tokrequest = c->tokenRx;
    p.token = c->tokenTx;
    p.code = 0x0250;


    if (!allowed) {
        p.error = 0xFEFFFFFF;
        c->idleTimer->stop();
        c->pingTimer->stop();
        c->wdTimer->stop();
    }
    else {
        strcpy(p.connection,"WFVIEW");
    }
    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
    c->idleTimer->start(100);
    c->txSeq++;
    return;
}

void udpServer::sendCapabilities(CLIENT* c)
{
    qDebug() << c->ipAddress.toString() << ": Sending Capabilities :" << c->txSeq;

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
    p.capc = (char)0x90;

    memcpy(p.packet + 0x4d, QByteArrayLiteral("\x90\xc7\x0b\xe7").constData(), 4); // IC9700
    memcpy(p.packet + 0x92, QByteArrayLiteral("\x3f\x07\x00\x01\x8b\x01\x8b\x01\x01\x01\x00\x00\x4b").constData(), 13);
    memcpy(p.packet + 0xa0, QByteArrayLiteral("\x01\x50\x00\x90\x01").constData(), 5);

    p.packet[0x94] = rigciv;
    p.packet[0x51] = (char)0x64;
    memcpy(p.packet + 0x52, rigname.toLocal8Bit(), rigname.length());
    memcpy(p.packet + 0x72, QByteArrayLiteral("ICOM_VAUDIO").constData(), 11);

    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
    c->idleTimer->start(100);
    c->txSeq++;
    return;
}

// When client has requested civ/audio connection, this will contain their details
// Also used to display currently connected used information.
void udpServer::sendConnectionInfo(CLIENT* c)
{
    qDebug() << c->ipAddress.toString() << ": Sending ConnectionInfo :" << c->txSeq;
    conninfo_packet p;
    memset(p.packet, 0x0, sizeof(p));
    p.len = sizeof(p);
    p.type = 0x00;
    p.seq = c->txSeq;
    p.sentid = c->myId;
    p.rcvdid = c->remoteId;
    p.innerseq = c->authInnerSeq;
    p.tokrequest = c->tokenRx;
    p.token = c->tokenTx;
    p.code = 0x0380;
    p.commoncap = c->commonCap;
    p.identa = (char)0x90;
    p.identb = 0x64e70bc7;

    // 0x1a-0x1f is authid (random number?
    // memcpy(p + 0x40, QByteArrayLiteral("IC-7851").constData(), 7);

    memcpy(p.packet + 0x40, rigname.toLocal8Bit(), rigname.length());

    // This is the current streaming client (should we support multiple clients?)
    if (c->isStreaming) {
        p.busy = 0x01;
        memcpy(p.computer, c->clientName.constData(), c->clientName.length());
        p.ipaddress = qToBigEndian(c->ipAddress.toIPv4Address());
        p.identb = c->connSeq;
    }
    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
    c->idleTimer->start(100);
    c->txSeq++;
    return;
}

void udpServer::sendTokenResponse(CLIENT* c, quint8 type)
{
    qDebug() << c->ipAddress.toString() << ": Sending Token response for type: " << type;

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
    p.res = type;

    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
    c->idleTimer->start(100);
    c->txSeq++;
    return;
}

void udpServer::sendWatchdog(CLIENT* c)
{
    QMutexLocker locker(&mutex);

    QDateTime now = QDateTime::currentDateTime();

    qint32 deciSeconds = (qint32)c->timeConnected.msecsTo(now) / 100;

    watchdog_packet p;
    memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
    p.len = sizeof(p);
    p.type = 0x00;
    p.seq = c->txSeq;
    p.sentid = c->myId;
    p.rcvdid = c->remoteId;
    p.secondsa = deciSeconds;
    p.secondsb = deciSeconds + 1;

    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
    return;


}

void udpServer::sendStatus(CLIENT* c)
{
    QMutexLocker locker(&mutex);

    qDebug() << c->ipAddress.toString() << ": Sending Status";

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
    p.value[0] = (char)0x90;

    qToLittleEndian(c->connSeq, p.packet + 0x2c);

    p.civport=qToBigEndian(c->civPort);
    p.audioport=qToBigEndian(c->audioPort);

    // Send this to reject the request to tx/rx audio/civ
    //memcpy(p + 0x30, QByteArrayLiteral("\xff\xff\xff\xfe").constData(), 4);


    c->txSeq++;
    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);


}


void udpServer::dataForServer(QByteArray d)
{

    //qDebug() << "Server got:" << d;
    foreach(CLIENT * client, civClients)
    {
        if (client != Q_NULLPTR && client->connected) {
            data_packet p;
            memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
            p.seq = client->txSeq;
            p.sentid = client->myId;
            p.rcvdid = client->remoteId;
            p.reply = (char)0xc1;
            p.len = (quint16)d.length();
            p.sendseq = client->connSeq;
            p.len = (quint16)d.length() + sizeof(p);
            QByteArray t = QByteArray::fromRawData((const char*)p.packet, sizeof(p));
            t.append(d);

            QMutexLocker locker(&mutex);
            client->connSeq++;
            client->txSeq++;
            client->socket->writeDatagram(t, client->ipAddress, client->port);
        }
    }
    
    return;
}


// This function is passed a pointer to the list of connection objects and a pointer to the object itself
// Needs to stop and delete all timers, remove the connection from the list and delete the connection.
void udpServer::deleteConnection(QList<CLIENT*> *l, CLIENT* c)
{
    qDebug() << "Deleting connection to: " << c->ipAddress.toString() << ":" << QString::number(c->port);
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
    qDebug() << "Current Number of clients connected: " << l->length();
}
