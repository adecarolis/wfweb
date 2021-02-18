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
            connect(current->idleTimer, &QTimer::timeout, this, std::bind(&udpServer::sendIdle, this, current, (quint16)0x00));
            connect(current->wdTimer, &QTimer::timeout, this, std::bind(&udpServer::sendWatchdog, this, controlClients, current));
            current->pingTimer->start(100);
            current->idleTimer->start(100);
            current->wdTimer->start(10000);
            qDebug() << "New Control connection created from :" << current->ipAddress.toString() << ":" << QString::number(current->port);
            controlClients.append(current);
        }

        current->lastHeard = QDateTime::currentDateTime();
        quint16 gotSeq = qFromLittleEndian<quint16>(r.mid(0x06, 2));

        switch (r.length())
        {
        case (0x10):
            if (r.mid(0, 8) == QByteArrayLiteral("\x10\x00\x00\x00\x03\x00\x00\x00")) {
                qDebug() << current->ipAddress.toString() << ": Received 'are you there'";
                current->remoteId = qFromLittleEndian<quint32>(r.mid(8, 4));
                sendIAmHere(current);
            } // This is This is "Are you ready" in response to "I am here".
            else if (r.mid(0, 8) == QByteArrayLiteral("\x10\x00\x00\x00\x06\x00\x01\x00"))
            {
                qDebug() << current->ipAddress.toString() << ": Received 'Are you ready'";
                current->remoteId = qFromLittleEndian<quint32>(r.mid(8, 4));
                sendIAmReady(current);
            } // This is a retransmit request
            else if (r.mid(0, 6) == QByteArrayLiteral("\x10\x00\x00\x00\x01\x00"))
            {
                // Just send an idle for now!
                qDebug() << current->ipAddress.toString() << ": Received 'retransmit' request for " << gotSeq;
                sendIdle(current, gotSeq);

            } // This is a disconnect request
            else if (r.mid(0, 6) == QByteArrayLiteral("\x10\x00\x00\x00\x05\x00"))
            {
                qDebug() << current->ipAddress.toString() << ": Received 'disconnect' request";
                sendIdle(current, gotSeq);
                //current->wdTimer->stop(); // Keep watchdog running to delete stale connection.
                deleteConnection(&controlClients, current);
            }
            break;
        case (0x14):
            // Watchdog packet.
            break;
        case (0x15):
            if (r.mid(1, 5) == QByteArrayLiteral("\x00\x00\x00\x07\x00"))
            {
                // It is a ping request/response

                if (r[16] == (char)0x00)
                {
                    current->rxPingSeq = qFromLittleEndian<quint32>(r.mid(0x11, 4));
                    sendPing(&controlClients, current, gotSeq, true);
                }
                else if (r[16] == (char)0x01) {
                    // A Reply to our ping!
                    if (gotSeq == current->pingSeq || gotSeq == current->pingSeq - 1) {
                        current->pingSeq++;
                    }
                    else {
                        qDebug() << current->ipAddress.toString() << ": Server got out of sequence ping reply. Got: " << gotSeq << " expecting: " << current->pingSeq;
                    }
                }
            }
            break;
        case (0x40):
            // Token request
            current->authInnerSeq = qFromLittleEndian<quint32>(r.mid(0x16, 4));
            if (r[0x15] == (char)0x02) {
                // Request for new token
                //current->tokenTx = (quint16)rand();
                qDebug() << current->ipAddress.toString() << ": Received create token request";
                sendCapabilities(current);
                current->authInnerSeq = 0x00;
                sendConnectionInfo(current);
            }
            else {
                qDebug() << current->ipAddress.toString() << ": Received token request";
                sendTokenResponse(current, r[0x15]);
            }
            break;
        case (0x80):
            if (r.mid(0, 8) == QByteArrayLiteral("\x80\x00\x00\x00\x00\x00\x01\x00"))
            {
                qDebug() << current->ipAddress.toString() << ": Received 'login'";
                bool userOk = false;
                foreach(SERVERUSER user, config.users)
                {
                    QByteArray usercomp;
                    passcode(user.username, usercomp);
                    QByteArray passcomp;
                    passcode(user.password, passcomp);
                    if (r.mid(0x40, usercomp.length()) == usercomp && r.mid(0x50, passcomp.length()) == passcomp)
                    {
                        userOk = true;
                        break;
                    }

                }

                // Generate login response
                current->clientName = parseNullTerminatedString(r, 0x60);
                current->authInnerSeq = qFromLittleEndian<quint32>(r.mid(0x16, 4));
                current->tokenRx = qFromLittleEndian<quint16>(r.mid(0x1a, 2));
                current->tokenTx = (quint32)((quint16)rand() | (quint16)rand() << 16);

                if (userOk) {
                    sendLoginResponse(current, gotSeq, true);
                }
                else {
                    qDebug() << "Username no match!";
                    sendLoginResponse(current, gotSeq, false);
                }

            }
            break;
        case 0x90:
            qDebug() << current->ipAddress.toString() << ": Received request for radio connection";
            // Request to start audio and civ!
            current->isStreaming = true;
            current->rxCodec = r[0x72];
            current->txCodec = r[0x73];
            current->rxSampleRate = qFromBigEndian<quint16>(r.mid(0x76, 2));
            current->txSampleRate = qFromBigEndian<quint16>(r.mid(0x7a, 2));
            //current->civPort = qFromBigEndian<quint16>(r.mid(0x7e, 2)); // Ignore port sent from client and tell it which to use
            //current->audioPort = qFromBigEndian<quint16>(r.mid(0x82, 2));
            current->txBufferLen = qFromBigEndian<quint16>(r.mid(0x86, 2));
            current->authInnerSeq = qFromLittleEndian<quint32>(r.mid(0x16, 4));
            current->connSeq = qFromLittleEndian<quint32>(r.mid(0x2c, 4));
            sendStatus(current);
            sendConnectionInfo(current);

            break;

        default:
            qDebug() << "Unknown length packet received: " << r.length();
            break;
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
            connect(current->idleTimer, &QTimer::timeout, this, std::bind(&udpServer::sendIdle, this, current, (quint16)0x00));
            current->pingTimer->start(100);
            current->idleTimer->start(100);
            qDebug() << "New CIV connection created from :" << current->ipAddress.toString() << ":" << QString::number(current->port);
            civClients.append(current);
        }

        current->lastHeard = QDateTime::currentDateTime();
        quint16 gotSeq = qFromLittleEndian<quint16>(r.mid(0x06, 2));

        switch (r.length())
        {
        case (0x10):
            if (r.mid(0, 8) == QByteArrayLiteral("\x10\x00\x00\x00\x03\x00\x00\x00")) {
                qDebug() << current->ipAddress.toString() << ": Received 'are you there'";
                current->remoteId = qFromLittleEndian<quint32>(r.mid(8, 4));
                sendIAmHere(current);
            } // This is This is "Are you ready" in response to "I am here".
            else if (r.mid(0, 8) == QByteArrayLiteral("\x10\x00\x00\x00\x06\x00\x01\x00"))
            {
                qDebug() << current->ipAddress.toString() << ": Received 'Are you ready'";
                current->remoteId = qFromLittleEndian<quint32>(r.mid(8, 4));
                sendIAmReady(current);
            } // This is a retransmit request
            else if (r.mid(0, 6) == QByteArrayLiteral("\x10\x00\x00\x00\x01\x00"))
            {
                // Just send an idle for now!
                qDebug() << current->ipAddress.toString() << ": Received 'retransmit' request for " << gotSeq;
                sendIdle(current, gotSeq);

            } // This is a disconnect request
            else if (r.mid(0, 6) == QByteArrayLiteral("\x10\x00\x00\x00\x05\x00"))
            {
                qDebug() << current->ipAddress.toString() << ": Received 'disconnect' request";
                sendIdle(current, gotSeq);
                deleteConnection(&civClients, current);

            }
            break;
        case (0x14):
            // Watchdog packet.
            break;
        case (0x15):
            if (r.mid(1, 5) == QByteArrayLiteral("\x00\x00\x00\x07\x00"))
            {
                // It is a ping request/response

                if (r[16] == (char)0x00)
                {
                    current->rxPingSeq = qFromLittleEndian<quint32>(r.mid(0x11, 4));
                    sendPing(&civClients, current, gotSeq, true);
                }
                else if (r[16] == (char)0x01) {
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
        default:
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
        case (0x10):
            if (r.mid(0, 8) == QByteArrayLiteral("\x10\x00\x00\x00\x03\x00\x00\x00")) {
                qDebug() << current->ipAddress.toString() << ": Received 'are you there'";
                current->remoteId = qFromLittleEndian<quint32>(r.mid(8, 4));
                sendIAmHere(current);
            } // This is This is "Are you ready" in response to "I am here".
            else if (r.mid(0, 8) == QByteArrayLiteral("\x10\x00\x00\x00\x06\x00\x01\x00"))
            {
                qDebug() << current->ipAddress.toString() << ": Received 'Are you ready'";
                current->remoteId = qFromLittleEndian<quint32>(r.mid(8, 4));
                sendIAmReady(current);
            } // This is a retransmit request
            else if (r.mid(0, 6) == QByteArrayLiteral("\x10\x00\x00\x00\x01\x00"))
            {
                // Just send an idle for now!
                qDebug() << current->ipAddress.toString() << ": Received 'retransmit' request for " << gotSeq;
                sendIdle(current, gotSeq);

            } // This is a disconnect request
            else if (r.mid(0, 6) == QByteArrayLiteral("\x10\x00\x00\x00\x05\x00"))
            {
                qDebug() << current->ipAddress.toString() << ": Received 'disconnect' request";
                sendIdle(current, gotSeq);
                deleteConnection(&audioClients, current);
            }
            break;
        case (0x14):
            // Watchdog packet.
            break;
        case (0x15):
            if (r.mid(1, 5) == QByteArrayLiteral("\x00\x00\x00\x07\x00"))
            {
                // It is a ping request/response

                if (r[16] == (char)0x00)
                {
                    current->rxPingSeq = qFromLittleEndian<quint32>(r.mid(0x11, 4));
                    sendPing(&audioClients, current, gotSeq, true);
                }
                if (gotSeq == current->pingSeq || gotSeq == current->pingSeq - 1) {
                    // A Reply to our ping!
                    if (gotSeq == current->pingSeq) {
                        current->pingSeq++;
                    }
                    else {
                        qDebug() << current->ipAddress.toString() << ": Civ got out of sequence ping reply. Got: " << gotSeq << " expecting: " << current->pingSeq;
                    }
                }
            }
            break;
        default:
            break;
        }

    }
}





#define IDLE_SIZE 0x10

void udpServer::sendIAmHere(CLIENT* c)
{
    qDebug() << c->ipAddress.toString() << ": Sending I am here...";

    quint8 p[IDLE_SIZE];
    memset(p, 0x0, sizeof(p));
    qToLittleEndian(sizeof(p), p + 0x00);
    p[0x04] = (char)0x04;
    qToLittleEndian(c->txSeq, p + 0x06);
    qToLittleEndian(c->myId, p + 0x08);
    qToLittleEndian(c->remoteId, p + 0x0c);

    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), c->ipAddress, c->port);
    c->txSeq++;
    return;
}

void udpServer::sendIAmReady(CLIENT* c)
{
    qDebug() << c->ipAddress.toString() << ": Sending I am ready...";

    quint8 p[IDLE_SIZE];
    memset(p, 0x0, sizeof(p));
    qToLittleEndian(sizeof(p), p + 0x00);
    p[0x04] = (char)0x06;
    qToLittleEndian(c->txSeq, p + 0x06);
    qToLittleEndian(c->myId, p + 0x08);
    qToLittleEndian(c->remoteId, p + 0x0c);

    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), c->ipAddress, c->port);
    c->txSeq++;
    return;
}

void udpServer::sendIdle(CLIENT* c, quint16 seq)
{
    QMutexLocker locker(&mutex);

    if (seq == 0x00)
    {
        seq = c->txSeq;
        c->txSeq++;
    }

    quint8 p[IDLE_SIZE];
    memset(p, 0x0, sizeof(p));
    qToLittleEndian(sizeof(p), p + 0x00);
    qToLittleEndian(seq, p + 0x06);
    qToLittleEndian(c->myId, p + 0x08);
    qToLittleEndian(c->remoteId, p + 0x0c);

    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), c->ipAddress, c->port);
    return;
}




#define PING_SIZE 0x15
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

    quint32 pingSeq = 0;
    quint8 pingReply = 0;
    if (reply) {
        pingSeq = c->rxPingSeq;
        pingReply = 1;
    }
    else {
        pingSeq = (quint32)((quint8)(rand() & 0xff)) | (quint16)c->innerPingSeq << 8 | (quint8)0x06 << 24;
        seq = c->pingSeq;
    }

    // First byte of pings "from" server can be either 0x00 or packet length!

    quint8 p[PING_SIZE];
    memset(p, 0x0, sizeof(p));
    if (!reply) {
        qToLittleEndian(sizeof(p), p + 0x00);
    }
    p[0x04] = (char)0x07;
    qToLittleEndian(seq, p + 0x06);
    qToLittleEndian(c->myId, p + 0x08);
    qToLittleEndian(c->remoteId, p + 0x0c);
    qToLittleEndian(pingSeq, p + 0x11);
    p[0x10] = pingReply;

    c->innerPingSeq++;

    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), c->ipAddress, c->port);
    return;
}

#define LOGINRESP_SIZE 0x60
void udpServer::sendLoginResponse(CLIENT* c,quint16 seq, bool allowed)
{
    qDebug() << c->ipAddress.toString() << ": Sending Login response: " << c->txSeq;

    quint8 p[LOGINRESP_SIZE];
    memset(p, 0x0, sizeof(p));
    qToLittleEndian(sizeof(p), p + 0x00);
    qToLittleEndian(seq, p + 0x06);
    qToLittleEndian(c->myId, p + 0x08);
    qToLittleEndian(c->remoteId, p + 0x0c);
    memcpy(p + 0x13, QByteArrayLiteral("\x50\x02\00").constData(), 3);
    qToLittleEndian(c->authInnerSeq, p + 0x16);
    qToLittleEndian(c->tokenRx, p + 0x1a);
    qToLittleEndian(c->tokenTx, p + 0x1c);

    if (!allowed) {
        memcpy(p + 0x30, QByteArrayLiteral("\xFF\xFF\xFF\xFE").constData(), 4);
        c->idleTimer->stop();
        c->pingTimer->stop();
        c->wdTimer->stop();
    }
    else {
        //memcpy(p + 0x40, QByteArrayLiteral("FTTH").constData(), 4);
        memcpy(p + 0x40, QByteArrayLiteral("WFVIEW").constData(), 6);
    }
    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), c->ipAddress, c->port);
    c->idleTimer->start(100);
    return;
}

#define CAP_SIZE 0xa8
void udpServer::sendCapabilities(CLIENT* c)
{
    qDebug() << c->ipAddress.toString() << ": Sending Capabilities :" << c->txSeq;

    quint8 p[CAP_SIZE];
    memset(p, 0x0, sizeof(p));
    qToLittleEndian(sizeof(p), p + 0x00);
    qToLittleEndian(c->txSeq, p + 0x06);
    qToLittleEndian(c->myId, p + 0x08);
    qToLittleEndian(c->remoteId, p + 0x0c);
    memcpy(p + 0x13, QByteArrayLiteral("\x98\x02\02").constData(), 3);
    qToLittleEndian(c->authInnerSeq, p + 0x16);
    qToLittleEndian(c->tokenRx, p + 0x1a);
    qToLittleEndian(c->tokenTx, p + 0x1c);
    p[0x41] = (char)0x01;
    p[0x49] = (char)0x10;
    p[0x4a] = (char)0x80;
    memcpy(p + 0x49, QByteArrayLiteral("\x10\x80").constData(), 2);
    memcpy(p + 0x4d, QByteArrayLiteral("\x90\xc7\x0b\xe7").constData(), 4); // IC9700
    memcpy(p + 0x92, QByteArrayLiteral("\x3f\x07\x00\x01\x8b\x01\x8b\x01\x01\x01\x00\x00\x4b").constData(), 13);
    memcpy(p + 0xa0, QByteArrayLiteral("\x01\x50\x00\x90\x01").constData(), 5);

    p[0x94] = rigciv;
    p[0x51] = (char)0x64;
    memcpy(p + 0x52, rigname.toLocal8Bit(), rigname.length());
    memcpy(p + 0x72, QByteArrayLiteral("ICOM_VAUDIO").constData(), 11);

    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), c->ipAddress, c->port);
    c->idleTimer->start(100);
    c->txSeq++;
    return;
}

// When client has requested civ/audio connection, this will contain their details
// Also used to display currently connected used information.
#define CONNINFO_SIZE 0x90
void udpServer::sendConnectionInfo(CLIENT* c)
{
    qDebug() << c->ipAddress.toString() << ": Sending ConnectionInfo :" << c->txSeq;
    quint8 p[CONNINFO_SIZE];
    memset(p, 0x0, sizeof(p));
    qToLittleEndian(sizeof(p), p + 0x00);
    qToLittleEndian(c->txSeq, p + 0x06);
    qToLittleEndian(c->myId, p + 0x08);
    qToLittleEndian(c->remoteId, p + 0x0c);
    p[0x13] = (char)0x80;
    p[0x14] = (char)0x03;
    qToLittleEndian(c->authInnerSeq, p + 0x16);
    qToLittleEndian(c->tokenRx, p + 0x1a);
    qToLittleEndian(c->tokenTx, p + 0x1c);
    p[0x27] = (char)0x10;
    p[0x28] = (char)0x80;
    memcpy(p + 0x2b, QByteArrayLiteral("\x90\xc7\x0b\xe7\x64").constData(), 5);  // THIS SHOULD BE DYNAMIC?

    // 0x1a-0x1f is authid (random number?
    // memcpy(p + 0x40, QByteArrayLiteral("IC-7851").constData(), 7);

    memcpy(p + 0x40, rigname.toLocal8Bit(), rigname.length());

    // This is the current streaming client (should we support multiple clients?)
    if (c->isStreaming) {
        p[0x60] = (char)0x01;
        memcpy(p + 0x64, c->clientName.constData(), c->clientName.length());
        qToBigEndian(c->ipAddress.toIPv4Address(), p + 0x84);
        qToLittleEndian(c->connSeq, p + 0x2c);

    }
    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), c->ipAddress, c->port);
    c->idleTimer->start(100);
    c->txSeq++;
    return;
}

#define TOKEN_SIZE 0x40
void udpServer::sendTokenResponse(CLIENT* c, quint8 type)
{
    qDebug() << c->ipAddress.toString() << ": Sending Token response for type: " << type;

    quint8 p[TOKEN_SIZE];
    memset(p, 0x0, sizeof(p));
    qToLittleEndian(sizeof(p), p + 0x00);
    qToLittleEndian(c->txSeq, p + 0x06);
    qToLittleEndian(c->myId, p + 0x08);
    qToLittleEndian(c->remoteId, p + 0x0c);
    p[0x13] = (char)0x30;
    p[0x14] = (char)0x02;
    p[0x15] = (char)type;
    qToLittleEndian(c->authInnerSeq, p + 0x16);
    qToLittleEndian(c->tokenRx, p + 0x1a);
    qToLittleEndian(c->tokenTx, p + 0x1c);

    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), c->ipAddress, c->port);
    c->idleTimer->start(100);
    c->txSeq++;
    return;
}

#define WATCHDOG_SIZE 0x14
void udpServer::sendWatchdog(QList<CLIENT*> l,CLIENT* c)
{
    QMutexLocker locker(&mutex);

    QDateTime now = QDateTime::currentDateTime();

    qint32 deciSeconds = (qint32)c->timeConnected.msecsTo(now) / 100;

    quint8 p[WATCHDOG_SIZE];
    memset(p, 0x0, sizeof(p));
    qToLittleEndian(sizeof(p), p + 0x00);
    p[0x04] = (char)0x01;
    qToLittleEndian(c->myId, p + 0x08);
    qToLittleEndian(c->remoteId, p + 0x0c);
    qToLittleEndian(deciSeconds, p + 0x10);
    qToLittleEndian(deciSeconds+1, p + 0x12);

    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), c->ipAddress, c->port);
    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), c->ipAddress, c->port);
    return;


}
#define STATUS_SIZE 0x50

void udpServer::sendStatus(CLIENT* c)
{
    QMutexLocker locker(&mutex);

    qDebug() << c->ipAddress.toString() << ": Sending Status";

    quint8 p[STATUS_SIZE];
    memset(p, 0x0, sizeof(p));
    qToLittleEndian(sizeof(p), p + 0x00);
    qToLittleEndian(c->txSeq, p + 0x06);
    qToLittleEndian(c->myId, p + 0x08);
    qToLittleEndian(c->remoteId, p + 0x0c);
    p[0x13] = (char)0x40;
    p[0x14] = (char)0x02;
    p[0x15] = (char)0x03;
    qToLittleEndian(c->authInnerSeq, p + 0x16);
    qToLittleEndian(c->tokenRx, p + 0x1a);
    qToLittleEndian(c->tokenTx, p + 0x1c);
    p[0x27] = (char)0x10;
    p[0x28] = (char)0x80;
    p[0x2b] = (char)0x90;
    qToLittleEndian(c->connSeq, p + 0x2c);

    qToBigEndian(c->civPort, p + 0x42);
    qToBigEndian(c->audioPort, p + 0x46);

    // Send this to reject the request to tx/rx audio/civ
    //memcpy(p + 0x30, QByteArrayLiteral("\xff\xff\xff\xfe").constData(), 4);


    c->txSeq++;
    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), c->ipAddress, c->port);


}


#define SEND_SIZE 0x15
void udpServer::dataForServer(QByteArray d)
{

    //qDebug() << "Server got:" << d;
    foreach(CLIENT * client, civClients)
    {
        if (client != Q_NULLPTR && client->connected) {
            quint8 p[SEND_SIZE];
            memset(p, 0x0, sizeof(p));
            qToLittleEndian(client->txSeq, p + 0x06);
            qToLittleEndian(client->myId, p + 0x08);
            qToLittleEndian(client->remoteId, p + 0x0c);

            QByteArray t = QByteArray::fromRawData((const char*)p, sizeof(p));
            p[0x10] = (char)0xc1;
            qToLittleEndian((quint16)t.length(), p + 0x11);
            qToLittleEndian(client->connSeq, p + 0x12);
            qToLittleEndian((quint16)sizeof(p) + t.length()+d.length(), p + 0x00);

            t.append(QByteArray::fromRawData((const char*)p, sizeof(p)));
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
