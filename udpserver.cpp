#include "udpserver.h"

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
    udpCiv->bind(config.civPort); 
    udpAudio = new QUdpSocket(this);
    udpAudio->bind(config.audioPort); 

    QUdpSocket::connect(udpControl, &QUdpSocket::readyRead, this, &udpServer::controlReceived);
    QUdpSocket::connect(udpCiv, &QUdpSocket::readyRead, this, &udpServer::civReceived);
    QUdpSocket::connect(udpAudio, &QUdpSocket::readyRead, this, &udpServer::audioReceived);

}

udpServer::~udpServer()
{
    qDebug() << "Closing udpServer";

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

    
    foreach(CLIENT* client, controlClients)
    {
        client->idleTimer->stop();
        delete client->idleTimer;
        //delete& client; // Not sure how safe this is?
    }
    foreach(CLIENT* client, civClients)
    {
        client->idleTimer->stop();
        delete client->idleTimer;
        client->pingTimer->stop();
        delete client->pingTimer;
        //delete& client; // Not sure how safe this is?
    }
    foreach(CLIENT* client, audioClients)
    {
        client->idleTimer->stop();
        delete client->idleTimer;
        client->pingTimer->stop();
        delete client->pingTimer;
        //delete& client; // Not sure how safe this is?
    }
    
}


void udpServer::controlReceived()
{
    // Received data on control port.
    while (udpControl->hasPendingDatagrams()) {
        QNetworkDatagram datagram = udpControl->receiveDatagram();
        QByteArray r = datagram.data();
        CLIENT* current = Q_NULLPTR;
        foreach(CLIENT * client, controlClients)
        {
            if (client->ipAddress == datagram.senderAddress() && client->port == datagram.senderPort())
            {
                current = client;
            }
        }
        if (current == Q_NULLPTR)
        {
            current = new CLIENT();
            current->connected = true;
            current->timeConnected = time(NULL);
            current->ipAddress = datagram.senderAddress();
            current->port = datagram.senderPort();
            current->myId = controlId;
            current->remoteId = qFromLittleEndian<quint32>(r.mid(8, 4));
            current->socket = udpControl;
            current->innerPingSeq = (quint16)rand();
            current->pingSeq = (quint8)rand() << 8  | (quint8)rand();
            current->pingTimer = new QTimer();
            current->idleTimer = new QTimer();
            connect(current->pingTimer, &QTimer::timeout, this, std::bind(&udpServer::sendPing, this, current, (quint16)0x00, false));
            connect(current->idleTimer, &QTimer::timeout, this, std::bind(&udpServer::sendIdle, this, current, (quint16)0x00));
            current->pingTimer->start(100);
            current->idleTimer->start(100);
            qDebug() << "New connection created from :" << current->ipAddress.toString() << ":" << QString::number(current->port);
            controlClients.append(current);
        }

        current->lastHeard = time(NULL);
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
                current->idleTimer->stop();
                current->pingTimer->stop();
                delete current;
                controlClients.removeOne(current);

            }
            break;
            qDebug() << "Got 0x14 command: " << gotSeq;
        case (0x15):
            if (r.mid(1, 5) == QByteArrayLiteral("\x00\x00\x00\x07\x00"))
            {
                // It is a ping request/response

                if (r[16] == (char)0x00)
                {
                    current->rxPingSeq = qFromLittleEndian<quint32>(r.mid(0x11, 4));
                    sendPing(current, gotSeq, true);
                }
                else if (r[16] == (char)0x01) {
                    // A Reply to our ping!
                    if (gotSeq == current->pingSeq) {
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
            //current->authSeq = qFromLittleEndian<quint16>(r.mid(0x06, 2));
            if (r[0x15] == (char)0x02) {
                // Request for new token
                //current->tokenTx = (quint16)rand();
                sendIdle(current, gotSeq);
                current->authSeq++;
                sendCapabilities(current);
                current->authSeq++;
                sendConnectionInfo(current);
            }
            else if(r[0x15] == (char)0x01) {
                // De-auth request
                sendIdle(current, gotSeq);
                current->authSeq++;
            }
            else if (r.mid(0x13,3) == QByteArrayLiteral("\x30\01\x05")) {
                qDebug() << current->ipAddress.toString() << ": Received 'token renewal' request";
                sendTokenRenewal(current);
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
                current->authInnerSeq = qFromLittleEndian<quint16>(r.mid(0x17, 2));
                current->authSeq = qFromLittleEndian<quint16>(r.mid(0x06, 2));
                current->tokenRx = qFromLittleEndian<quint16>(r.mid(0x1a, 2));
                current->tokenTx = (quint32)((quint16)rand() | (quint16)rand() << 16) ;

                if (userOk) {
                    sendLoginResponse(current, true);
                }
                else {
                    qDebug() << "Username no match!";
                    sendLoginResponse(current, false);
                }

            }
            break;
        default:
            break;
        }
    }
}


void udpServer::civReceived()
{
    while (udpCiv->hasPendingDatagrams()) {
        QNetworkDatagram datagram = udpCiv->receiveDatagram();
        QByteArray r = datagram.data();
        qDebug() << "CIV Data from :" << datagram.senderAddress().toString() << ":" << QString::number(datagram.senderPort());

    }
}

void udpServer::audioReceived()
{
    while (udpAudio->hasPendingDatagrams()) {
        QNetworkDatagram datagram = udpAudio->receiveDatagram();
        QByteArray r = datagram.data();
        qDebug() << "Audio Data from :" << datagram.senderAddress().toString() << ":" << QString::number(datagram.senderPort());
    }
}


void udpServer::sendIAmHere(CLIENT* c)
{
    qDebug() << c->ipAddress.toString() << ": Sending I am here...";
    const quint8 p[] = { 0x10, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
        static_cast<quint8>(c->myId & 0xff), static_cast<quint8>(c->myId >> 8 & 0xff), static_cast<quint8>(c->myId >>16 & 0xff), static_cast<quint8>(c->myId >>24 & 0xff),
        static_cast<quint8>(c->remoteId & 0xff), static_cast<quint8>(c->remoteId >> 8 & 0xff), static_cast<quint8>(c->remoteId >> 16 & 0xff), static_cast<quint8>(c->remoteId >>24 & 0xff)
    };
    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), c->ipAddress, c->port);
    return;
}

void udpServer::sendIAmReady(CLIENT* c)
{
    qDebug() << c->ipAddress.toString() << ": Sending I am ready...";
    const quint8 p[] = { 0x10, 0x00, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00,
        static_cast<quint8>(c->myId & 0xff), static_cast<quint8>(c->myId >> 8 & 0xff), static_cast<quint8>(c->myId >>16 & 0xff), static_cast<quint8>(c->myId >>24 & 0xff),
        static_cast<quint8>(c->remoteId & 0xff), static_cast<quint8>(c->remoteId >> 8 & 0xff), static_cast<quint8>(c->remoteId >>16 & 0xff), static_cast<quint8>(c->remoteId >>24 & 0xff)
    };
    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), c->ipAddress, c->port);
    return;
}

void udpServer::sendPing(CLIENT* c, quint16 seq, bool reply)
{
    //qDebug() << c->ipAddress.toString() << ": Sending Ping";

    quint32 pingSeq = 0;
    if (reply) {
        pingSeq = c->rxPingSeq;
    }
    else {
        pingSeq = (quint32)((quint8)(rand() & 0xff)) | (quint16)c->innerPingSeq << 8 | (quint8)0x06 << 24;
        seq = c->pingSeq;
    }

    // First byte of pings "from" server is 0x00 NOT packet length!
    //
    const quint8 p[] = { 0x00, 0x00, 0x00, 0x00, 0x07, 0x00,static_cast<quint8>(seq & 0xff),static_cast<quint8>(seq >>8 & 0xff),
        static_cast<quint8>(c->myId & 0xff), static_cast<quint8>(c->myId >>8 & 0xff), static_cast<quint8>(c->myId >>16 & 0xff), static_cast<quint8>(c->myId >>24 & 0xff),
        static_cast<quint8>(c->remoteId & 0xff), static_cast<quint8>(c->remoteId >> 8 & 0xff), static_cast<quint8>(c->remoteId >> 16 & 0xff), static_cast<quint8>(c->remoteId >>24 & 0xff),
        static_cast<quint8>(reply), static_cast<quint8>(pingSeq & 0xff), static_cast<quint8>(pingSeq >> 8 & 0xff), static_cast<quint8>(pingSeq >> 16 & 0xff), static_cast<quint8>(pingSeq >>24 & 0xff)
    };
    c->innerPingSeq++;

    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), c->ipAddress, c->port);
    return;
}

void udpServer::sendIdle(CLIENT* c, quint16 seq)
{
    
    if (seq == 0x00)
    {
        seq = c->txSeq;
        c->txSeq++;
    }
    

    const quint8 p[] = { 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,static_cast<quint8>(seq & 0xff),static_cast<quint8>(seq >>8 & 0xff),
        static_cast<quint8>(c->myId & 0xff), static_cast<quint8>(c->myId >> 8 & 0xff), static_cast<quint8>(c->myId >>16 & 0xff), static_cast<quint8>(c->myId >>24 & 0xff),
        static_cast<quint8>(c->remoteId >> 24 & 0xff), static_cast<quint8>(c->remoteId >> 16 & 0xff), static_cast<quint8>(c->remoteId >> 8 & 0xff), static_cast<quint8>(c->remoteId >>24 & 0xff),
    };

    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), c->ipAddress, c->port);
    return;
}

void udpServer::sendLoginResponse(CLIENT* c,bool allowed)
{
    qDebug() << c->ipAddress.toString() << ": Sending Login response: " << c->txSeq;
    quint8 p[] = { 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, static_cast<quint8>(c->txSeq & 0xff), static_cast<quint8>(c->txSeq >> 8 & 0xff),
        static_cast<quint8>(c->myId & 0xff), static_cast<quint8>(c->myId >> 8 & 0xff), static_cast<quint8>(c->myId >> 16 & 0xff), static_cast<quint8>(c->myId >> 24 & 0xff),
        static_cast<quint8>(c->remoteId & 0xff), static_cast<quint8>(c->remoteId >> 8 & 0xff), static_cast<quint8>(c->remoteId >> 16 & 0xff), static_cast<quint8>(c->remoteId >> 24 & 0xff),
        /*0x10*/ 0x00, 0x00, 0x00, 0x50, 0x02, 0x00, 0x00,
        static_cast<quint8>(c->authInnerSeq & 0xff), static_cast<quint8>(c->authInnerSeq >> 8 & 0xff), 0x00,
        static_cast<quint8>(c->tokenRx >> 0 & 0xff), static_cast<quint8>(c->tokenRx >> 8 & 0xff),
        static_cast<quint8>(c->tokenTx & 0xff), static_cast<quint8>(c->tokenTx >> 8 & 0xff),
        static_cast<quint8>(c->tokenTx >> 16 & 0xff), static_cast<quint8>(c->tokenTx >> 24 & 0xff),
        /*0x20*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /*0x30*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /*0x40*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /*0x50*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    if (!allowed) {
        memcpy(p + 0x30, QByteArrayLiteral("\xFF\xFF\xFF\xFE").constData(), 4);
        c->idleTimer->stop();
        c->pingTimer->stop();
    }
    else {
        memcpy(p + 0x40, QByteArrayLiteral("FTTH").constData(), 4);
        //memcpy(p + 0x40, QByteArrayLiteral("WFVIEW").constData(), 6);
    }
    c->authInnerSeq++;
    c->txSeq++;
    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), c->ipAddress, c->port);
    c->idleTimer->start(100);
    return;
}

void udpServer::sendCapabilities(CLIENT* c)
{
    qDebug() << c->ipAddress.toString() << ": Sending Capabilities :" << c->txSeq;
    quint8 p[] = { 0xa8, 0x00, 0x00, 0x00, 0x00, 0x00, static_cast<quint8>(c->txSeq & 0xff), static_cast<quint8>(c->txSeq >>8 & 0xff),
        static_cast<quint8>(c->myId & 0xff), static_cast<quint8>(c->myId >> 8 & 0xff), static_cast<quint8>(c->myId >> 16 & 0xff), static_cast<quint8>(c->myId >>24 & 0xff),
        static_cast<quint8>(c->remoteId & 0xff), static_cast<quint8>(c->remoteId >> 8 & 0xff), static_cast<quint8>(c->remoteId >> 16 & 0xff), static_cast<quint8>(c->remoteId >>24 & 0xff),
        /*0x10*/ 0x00, 0x00, 0x00, 0x98, 0x02, 0x02, 0x00,
        static_cast<quint8>(c->authInnerSeq & 0xff), static_cast<quint8>(c->authInnerSeq >> 8 & 0xff), 0x00,
        static_cast<quint8>(c->tokenRx >> 0 & 0xff), static_cast<quint8>(c->tokenRx >> 8 & 0xff),
        static_cast<quint8>(c->tokenTx & 0xff), static_cast<quint8>(c->tokenTx >> 8 & 0xff),
        static_cast<quint8>(c->tokenTx >> 16 & 0xff), static_cast<quint8>(c->tokenTx >> 24 & 0xff),
        /*0x20*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /*0x30*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /*0x40*/ 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x80, 0x00, 0x00, 0x90, 0xc7, 0x00,
        /*0x50*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /*0x60*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /*0x70*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /*0x80*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /*0x90*/ 0x00, 0x00, 0x3f, 0x07, 0x00, 0x01, 0x8b, 0x01, 0x8b, 0x01, 0x01, 0x01, 0x00, 0x00, 0x4b, 0x00,
        /*0xA0*/ 0x01, 0x50, 0x00, 0x90, 0x01, 0x00, 0x00, 0x00,
    };
    // 0x42-0x51 is "replyID" need to research what this is?
    // 0x90-0xa8 contains lots of seemingly random data, radio info?

    //memcpy(p + 0x4d, QByteArrayLiteral("\x08\x7a\x55").constData(), 3); // IC7851
    //p[0x94] = (char)0x8e; // IC-7851 C-IV address

    memcpy(p + 0x4d, QByteArrayLiteral("\x0b\xe7\x64").constData(), 3); // IC9700
    p[0x94] = (char)0xa2; // IC-9700 C-IV address

    memcpy(p + 0x52, QByteArrayLiteral("IC-9700").constData(), 7);
    memcpy(p + 0x72, QByteArrayLiteral("ICOM_VAUDIO").constData(), 11);

    c->authInnerSeq++;
    c->txSeq++;
    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), c->ipAddress, c->port);
    c->idleTimer->start(100);
    return;
}

void udpServer::sendConnectionInfo(CLIENT* c)
{
    qDebug() << c->ipAddress.toString() << ": Sending ConnectionInfo :" << c->txSeq;
    quint8 p[] = { 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, static_cast<quint8>(c->txSeq & 0xff), static_cast<quint8>(c->txSeq >>8 & 0xff),
        static_cast<quint8>(c->myId & 0xff), static_cast<quint8>(c->myId >> 8 & 0xff), static_cast<quint8>(c->myId >>16 & 0xff), static_cast<quint8>(c->myId >> 24 & 0xff),
        static_cast<quint8>(c->remoteId & 0xff), static_cast<quint8>(c->remoteId >> 8 & 0xff), static_cast<quint8>(c->remoteId >> 16 & 0xff), static_cast<quint8>(c->remoteId >>24 & 0xff),
        /*0x10*/ 0x00, 0x00, 0x00, 0x80, 0x03, 0x00, 0x00,
        static_cast<quint8>(c->authInnerSeq & 0xff), static_cast<quint8>(c->authInnerSeq >> 8 & 0xff), 0x00,
        static_cast<quint8>(c->tokenRx >> 0 & 0xff), static_cast<quint8>(c->tokenRx >> 8 & 0xff),
        static_cast<quint8>(c->tokenTx & 0xff), static_cast<quint8>(c->tokenTx >> 8 & 0xff),
        static_cast<quint8>(c->tokenTx >> 16 & 0xff), static_cast<quint8>(c->tokenTx >> 24 & 0xff),
        /*0x20*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x80, 0x00, 0x00, 0x90, 0xc7, 0x08, 0x7a, 0x55,
        /*0x30*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /*0x40*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /*0x50*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /*0x60*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /*0x70*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /*0x80*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    // 0x1a-0x1f is authid (random number?
    //memcpy(p + 0x40, QByteArrayLiteral("IC-7851").constData(), 7);
    memcpy(p + 0x40, QByteArrayLiteral("IC-9700").constData(), 7);

    c->authInnerSeq++;
    c->txSeq++;
    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), c->ipAddress, c->port);
    c->idleTimer->start(100);
    return;
}

void udpServer::sendTokenRenewal(CLIENT* c)
{
    qDebug() << c->ipAddress.toString() << ": Sending Token renwal";
    quint8 p[] = { 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, static_cast<quint8>(c->txSeq & 0xff), static_cast<quint8>(c->txSeq >> 8 & 0xff),
        static_cast<quint8>(c->myId & 0xff), static_cast<quint8>(c->myId >> 8 & 0xff), static_cast<quint8>(c->myId >> 16 & 0xff), static_cast<quint8>(c->myId >> 24 & 0xff),
        static_cast<quint8>(c->remoteId & 0xff), static_cast<quint8>(c->remoteId >> 8 & 0xff), static_cast<quint8>(c->remoteId >> 16 & 0xff), static_cast<quint8>(c->remoteId >> 24 & 0xff),
        /*0x10*/ 0x00, 0x00, 0x00, 0x30, 0x02, 0x05, 0x00,
        static_cast<quint8>(c->authInnerSeq & 0xff), static_cast<quint8>(c->authInnerSeq >> 8 & 0xff), 0x00,
        static_cast<quint8>(c->tokenRx >> 0 & 0xff), static_cast<quint8>(c->tokenRx >> 8 & 0xff),
        static_cast<quint8>(c->tokenTx & 0xff), static_cast<quint8>(c->tokenTx >> 8 & 0xff),
        static_cast<quint8>(c->tokenTx >> 16 & 0xff), static_cast<quint8>(c->tokenTx >> 24 & 0xff),
        /*0x20*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /*0x30*/ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    c->authInnerSeq++;
    c->txSeq++;
    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p, sizeof(p)), c->ipAddress, c->port);
    return;
}



