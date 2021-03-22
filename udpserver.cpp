#include "udpserver.h"
#include "logcategories.h"

#define STALE_CONNECTION 15

udpServer::udpServer(SERVERCONFIG config) :
    config(config)
{
    qDebug(logUdpServer()) << "Starting udp server";
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

    qDebug(logUdpServer()) << " My IP Address: " << QHostAddress(addr).toString() << " My MAC Address: " << macAddress;


    controlId = (addr >> 8 & 0xff) << 24 | (addr & 0xff) << 16 | (config.controlPort & 0xffff);
    civId = (addr >> 8 & 0xff) << 24 | (addr & 0xff) << 16 | (config.civPort & 0xffff);
    audioId = (addr >> 8 & 0xff) << 24 | (addr & 0xff) << 16 | (config.audioPort & 0xffff);

    qDebug(logUdpServer()) << "Server Binding Control to: " << config.controlPort;
    udpControl = new QUdpSocket(this);
    udpControl->bind(config.controlPort); 
    QUdpSocket::connect(udpControl, &QUdpSocket::readyRead, this, &udpServer::controlReceived);

    qDebug(logUdpServer()) << "Server Binding CIV to: " << config.civPort;
    udpCiv = new QUdpSocket(this);
    udpCiv->bind(config.civPort);
    QUdpSocket::connect(udpCiv, &QUdpSocket::readyRead, this, &udpServer::civReceived);

    qDebug(logUdpServer()) << "Server Binding Audio to: " << config.audioPort;
    udpAudio = new QUdpSocket(this);
    udpAudio->bind(config.audioPort);
    QUdpSocket::connect(udpAudio, &QUdpSocket::readyRead, this, &udpServer::audioReceived);

}

udpServer::~udpServer()
{
    qDebug(logUdpServer()) << "Closing udpServer";


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
            current->pingSeq = (quint8)rand() << 8 | (quint8)rand();

            current->pingTimer = new QTimer();
            connect(current->pingTimer, &QTimer::timeout, this, std::bind(&udpServer::sendPing, this, &controlClients, current, (quint16)0x00, false));
            current->pingTimer->start(100);

            current->idleTimer = new QTimer();
            connect(current->idleTimer, &QTimer::timeout, this, std::bind(&udpServer::sendControl, this, current, (quint8)0x00, (quint16)0x00));
            current->idleTimer->start(100);

            current->wdTimer = new QTimer();
            connect(current->wdTimer, &QTimer::timeout, this, std::bind(&udpServer::watchdog, this, current));
            current->wdTimer->start(1000);

            current->retransmitTimer = new QTimer();
            connect(current->retransmitTimer, &QTimer::timeout, this, std::bind(&udpServer::sendRetransmitRequest, this, current));
            current->retransmitTimer->start(RETRANSMIT_PERIOD);


            current->commonCap = 0x8010;
            qDebug(logUdpServer()) << "New Control connection created from :" << current->ipAddress.toString() << ":" << QString::number(current->port);
            controlClients.append(current);
        }

        current->lastHeard = QDateTime::currentDateTime();

        switch (r.length())
        {
            
            case (CONTROL_SIZE):
            {
                control_packet_t in = (control_packet_t)r.constData();
                if (in->type == 0x05)
                {
                    qDebug(logUdpServer()) << current->ipAddress.toString() << ": Received 'disconnect' request";
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
                                qDebug(logUdpServer()) << current->ipAddress.toString() << ": got out of sequence ping reply. Got: " << in->seq << " expecting: " << current->pingSeq;
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
                    qDebug(logUdpServer()) << current->ipAddress.toString() << ": Received create token request";
                    sendCapabilities(current);
                    sendConnectionInfo(current);
                }
                else if (in->res == 0x01) {
                    // Token disconnect
                    qDebug(logUdpServer()) << current->ipAddress.toString() << ": Received token disconnect request";
                    sendTokenResponse(current, in->res);
                }
                else if (in->res == 0x04) {
                    // Disconnect audio/civ
                    sendTokenResponse(current, in->res);
                    current->isStreaming = false;
                    sendConnectionInfo(current);
                }
                else {
                    qDebug(logUdpServer()) << current->ipAddress.toString() << ": Received token request";
                    sendTokenResponse(current, in->res);
                }
                break;
            }
            case (LOGIN_SIZE):
            {
                login_packet_t in = (login_packet_t)r.constData();
                qDebug(logUdpServer()) << current->ipAddress.toString() << ": Received 'login'";
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
                    qDebug(logUdpServer()) << current->ipAddress.toString() << ": User " << current->user.username << " login OK";
                    sendLoginResponse(current, true);
                }
                else {
                    qDebug(logUdpServer()) << current->ipAddress.toString() << ": Incorrect username/password";

                    sendLoginResponse(current, false);
                }
                break;
            }
            case (CONNINFO_SIZE):
            {
                conninfo_packet_t in = (conninfo_packet_t)r.constData();
                qDebug(logUdpServer()) << current->ipAddress.toString() << ": Received request for radio connection";
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
                qDebug(logUdpServer()) << "rxCodec:" << current->rxCodec << " txCodec:" << current->txCodec <<
                    " rxSampleRate" << current->rxSampleRate <<
                    " txSampleRate" << current->rxSampleRate <<
                    " txBufferLen" << current->txBufferLen;

                if (!config.lan) {
                    // Radio is connected by USB/Serial and we assume that audio is connected as well.
                    // Create audio TX/RX threads if they don't already exist (first client chooses samplerate/codec)
                    if (txaudio == Q_NULLPTR)
                    {
                        bool uLaw = false;
                        quint8 channels = 1;
                        quint8 samples = 8;
                        txSampleRate = current->txSampleRate;
                        txCodec = current->txCodec;

                        if (current->txCodec == 0x01 || current->txCodec == 0x20) {
                            uLaw = true;
                        }
                        if (current->txCodec == 0x08 || current->txCodec == 0x10 || current->txCodec == 0x20) {
                            channels = 2;
                        }
                        if (current->txCodec == 0x04 || current->txCodec == 0x10) {
                            samples = 16;
                        }


                        txaudio = new audioHandler();
                        txAudioThread = new QThread(this);
                        txaudio->moveToThread(txAudioThread);

                        txAudioThread->start();

                        connect(this, SIGNAL(setupTxAudio(quint8, quint8, quint16, quint16, bool, bool, QString, quint8)), txaudio, SLOT(init(quint8, quint8, quint16, quint16, bool, bool, QString, quint8)));
                        connect(txAudioThread, SIGNAL(finished()), txaudio, SLOT(deleteLater()));

                        emit setupTxAudio(samples, channels, current->txSampleRate, in->txbuffer, uLaw, false, config.audioOutput, config.resampleQuality);
                        hasTxAudio=datagram.senderAddress();

                        connect(this, SIGNAL(haveAudioData(audioPacket)), txaudio, SLOT(incomingAudio(audioPacket)));

                    }
                    if (rxaudio == Q_NULLPTR)
                    {
                        bool uLaw = false;
                        quint8 channels = 1;
                        quint8 samples = 8;
                        rxSampleRate = current->rxSampleRate;
                        rxCodec = current->rxCodec;

                        if (current->rxCodec == 0x01 || current->rxCodec == 0x20) {
                            uLaw = true;
                        }
                        if (current->rxCodec == 0x08 || current->rxCodec == 0x10 || current->rxCodec == 0x20) {
                            channels = 2;
                        }
                        if (current->rxCodec == 0x04 || current->rxCodec == 0x10) {
                            samples = 16;
                        }


                        rxaudio = new audioHandler();
                        rxAudioThread = new QThread(this);
                        rxaudio->moveToThread(rxAudioThread);
                        rxAudioThread->start();

                        connect(this, SIGNAL(setupRxAudio(quint8, quint8, quint16, quint16, bool, bool, QString, quint8)), rxaudio, SLOT(init(quint8, quint8, quint16, quint16, bool, bool, QString, quint8)));
                        connect(rxAudioThread, SIGNAL(finished()), txaudio, SLOT(deleteLater()));

                        emit setupRxAudio(samples, channels, current->rxSampleRate, 150, uLaw, true, config.audioInput, config.resampleQuality);

                        rxAudioTimer = new QTimer();
                        connect(rxAudioTimer, &QTimer::timeout, this, std::bind(&udpServer::sendRxAudio, this));
                        rxAudioTimer->start(10);
                    }

                }

                break;
            }
            default:
            {
                break;
            }
        }
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
            current->pingSeq = (quint8)rand() << 8 | (quint8)rand();

            current->pingTimer = new QTimer();
            connect(current->pingTimer, &QTimer::timeout, this, std::bind(&udpServer::sendPing, this, &civClients, current, (quint16)0x00, false));
            current->pingTimer->start(100);

            current->idleTimer = new QTimer();
            connect(current->idleTimer, &QTimer::timeout, this, std::bind(&udpServer::sendControl, this, current, 0x00, (quint16)0x00));
            current->idleTimer->start(100);

            current->wdTimer = new QTimer();
            connect(current->wdTimer, &QTimer::timeout, this, std::bind(&udpServer::watchdog, this, current));
            current->wdTimer->start(1000);

            current->retransmitTimer = new QTimer();
            connect(current->retransmitTimer, &QTimer::timeout, this, std::bind(&udpServer::sendRetransmitRequest, this, current));
            current->retransmitTimer->start(RETRANSMIT_PERIOD);

            qDebug(logUdpServer()) << "New CIV connection created from :" << current->ipAddress.toString() << ":" << QString::number(current->port);
            civClients.append(current);
        }

        switch (r.length())
        {
            case (CONTROL_SIZE):
            {
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
                                qDebug(logUdpServer()) << current->ipAddress.toString() << ": got out of sequence ping reply. Got: " << in->seq << " expecting: " << current->pingSeq;
                            }
                        }
                    }
                }
                break;
            }
            default:
            {

                if (r.length() > 0x15) {
                    data_packet_t in = (data_packet_t)r.constData();
                    if (in->type != 0x01) 
                    {
                        if (quint16(in->datalen + 0x15) == (quint16)in->len)
                        {
                            emit haveDataFromServer(r.mid(0x15));
                        }
                    }
                }
                break;
            }
        }
        commonReceived(&civClients, current, r);

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
            current->pingSeq = (quint8)rand() << 8 | (quint8)rand();
            
            current->pingTimer = new QTimer();
            connect(current->pingTimer, &QTimer::timeout, this, std::bind(&udpServer::sendPing, this, &audioClients, current, (quint16)0x00, false));
            current->pingTimer->start(100);

            current->wdTimer = new QTimer();
            connect(current->wdTimer, &QTimer::timeout, this, std::bind(&udpServer::watchdog, this, current));
            current->wdTimer->start(1000);

            current->retransmitTimer = new QTimer();
            connect(current->retransmitTimer, &QTimer::timeout, this, std::bind(&udpServer::sendRetransmitRequest, this, current));
            current->retransmitTimer->start(RETRANSMIT_PERIOD);
            current->seqPrefix = 0;
            qDebug(logUdpServer()) << "New Audio connection created from :" << current->ipAddress.toString() << ":" << QString::number(current->port);
            audioClients.append(current);
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
                                qDebug(logUdpServer()) << current->ipAddress.toString() << ": got out of sequence ping reply. Got: " << in->seq << " expecting: " << current->pingSeq;
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

                    // 0xac is the smallest possible audio packet.
                    audioPacket tempAudio;
                    tempAudio.seq = (quint32)current->seqPrefix << 16 | in->seq;
                    tempAudio.time = QTime::currentTime();;
                    tempAudio.sent = 0;
                    tempAudio.datain = r.mid(0x18);
                    // Prefer signal/slot to forward audio as it is thread/safe
                    // Need to do more testing but latency appears fine.
                    //if (hasTxAudio == datagram.senderAddress())
                    //{
                        qDebug(logUdpServer()) << "sending tx audio " << in->seq;
                        emit haveAudioData(tempAudio);
                    //}
                    //rxaudio->incomingAudio(tempAudio);
                }
                break;
            }

        }
        commonReceived(&audioClients, current, r);

    }
}


void udpServer::commonReceived(QList<CLIENT*>* l,CLIENT* current, QByteArray r)
{
    Q_UNUSED(l); // We might need it later!

    current->lastHeard = QDateTime::currentDateTime();
    if (r.length() < 0x10)
    {
        // Invalid packet
        qDebug(logUdpServer()) << current->ipAddress.toString() << ": Invalid packet received, len: " << r.length();
        return;
    }

    switch (r.length())
    {
        case (CONTROL_SIZE):
        {
            control_packet_t in = (control_packet_t)r.constData();
            if (in->type == 0x03) {
                qDebug(logUdpServer()) << current->ipAddress.toString() << ": Received 'are you there'";
                current->remoteId = in->sentid;
                sendControl(current, 0x04, in->seq);
            } // This is This is "Are you ready" in response to "I am here".
            else if (in->type == 0x06)
            {
                qDebug(logUdpServer()) << current->ipAddress.toString() << ": Received 'Are you ready'";
                current->remoteId = in->sentid;
                sendControl(current, 0x06, in->seq);
            } // This is a retransmit request
            else if (in->type == 0x01)
            {
                // Single packet request
                qDebug(logUdpServer()) << current->ipAddress.toString() << ": Received 'retransmit' request for " << in->seq;

                auto match = std::find_if(current->txSeqBuf.begin(), current->txSeqBuf.end(), [&cs = in->seq](SEQBUFENTRY& s) {
                    return s.seqNum == cs;
                });

                if (match != current->txSeqBuf.end() && match->retransmitCount < 5) {
                    // Found matching entry?
                    // Don't constantly retransmit the same packet, give-up eventually
                    QMutexLocker locker(&mutex);
                    qDebug(logUdpServer()) << this->metaObject()->className() << ": Sending retransmit of " << hex << match->seqNum;
                    match->retransmitCount++;
                    current->socket->writeDatagram(match->data, current->ipAddress, current->port);
                } else {
                    // Just send an idle!
                    sendControl(current, 0x00, in->seq);
                }
            }
            break;
        }
        default:
        {
            break;
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
                qDebug(logUdpServer()) << current->ipAddress.toString() << ":" << current->port << ": Requested packet " << hex << in->seq << " not found";
                // Just send idle packet.
                sendControl(current, 0, in->seq);
            }
            else if (match->seqNum != 0x00)
            {
                // Found matching entry?
                // Send "untracked" as it has already been sent once.
                QMutexLocker locker(&mutex);
                qDebug(logUdpServer()) << current->ipAddress.toString() << ":" << current->port << ": Sending retransmit of " << hex << match->seqNum;
                match->retransmitCount++;
                current->socket->writeDatagram(match->data, current->ipAddress, current->port);
                match++;
            }
        }
    }
    else if (in->len != PING_SIZE && in->type == 0x00 && in->seq != 0x00)
    {
        QMutexLocker locker(&mutex);
        if (current->rxSeqBuf.isEmpty()) 
        {
            current->rxSeqBuf.append(in->seq);
        }
        else
        {
            std::sort(current->rxSeqBuf.begin(), current->rxSeqBuf.end());
            if (in->seq < current->rxSeqBuf.front())
            {
                qDebug(logUdpServer()) << current->ipAddress.toString() << ": ******* seq number may have rolled over ****** previous highest: " << hex << current->rxSeqBuf.back() << " current: " << hex << in->seq;
                // Looks like it has rolled over so clear buffer and start again.
                current->rxSeqBuf.clear();
                return;
            }

            if (!current->rxSeqBuf.contains(in->seq))
            {
                // Add incoming packet to the received buffer and if it is in the missing buffer, remove it.
                current->rxSeqBuf.append(in->seq);
                // Check whether this is one of our missing ones!
                auto s = std::find_if(current->rxMissing.begin(), current->rxMissing.end(), [&cs = in->seq](SEQBUFENTRY& s) { return s.seqNum == cs; });
                if (s != current->rxMissing.end())
                {
                    qDebug(logUdpServer()) << current->ipAddress.toString() << ": Missing SEQ has been received! " << hex << in->seq;
                    s = current->rxMissing.erase(s);
                }
            }
        }
    }
}



void udpServer::sendControl(CLIENT* c, quint8 type, quint16 seq)
{

    //qDebug(logUdpServer()) << c->ipAddress.toString() << ": Sending control packet: " << type;
    QMutexLocker locker(&mutex);

    control_packet p;
    memset(p.packet, 0x0, CONTROL_SIZE); // We can't be sure it is initialized with 0x00!
    p.len = sizeof(p);
    p.type = type;
    p.sentid = c->myId;
    p.rcvdid = c->remoteId;

    if (seq == 0x00)
    {
        p.seq = c->txSeq;
        c->txSeqBuf.append(SEQBUFENTRY());
        c->txSeqBuf.last().seqNum = seq;
        c->txSeqBuf.last().timeSent = QTime::currentTime();
        c->txSeqBuf.last().retransmitCount = 0;
        c->txSeqBuf.last().data = QByteArray::fromRawData((const char*)p.packet, sizeof(p));
        c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
        c->txSeq++;
        if (c->idleTimer != Q_NULLPTR)
            c->idleTimer->start(100);
    }
    else {
        p.seq = seq;
        c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
    }

    return;
}



void udpServer::sendPing(QList<CLIENT*> *l,CLIENT* c, quint16 seq, bool reply)
{
    QMutexLocker locker(&mutex);
    // Also use to detect "stale" connections
    QDateTime now = QDateTime::currentDateTime();

    if (c->lastHeard.secsTo(now) > STALE_CONNECTION)
    {
        qDebug(logUdpServer()) << "Deleting stale connection " << c->ipAddress.toString();
        deleteConnection(l, c);
        return;
    }


    //qDebug(logUdpServer()) << c->ipAddress.toString() << ": Sending Ping";

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

    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);

    return;
}

void udpServer::sendLoginResponse(CLIENT* c, bool allowed)
{
    qDebug(logUdpServer()) << c->ipAddress.toString() << ": Sending Login response: " << c->txSeq;

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
        strcpy(p.connection,"WFVIEW");
    }

    QMutexLocker locker(&mutex);
    c->txSeqBuf.append(SEQBUFENTRY());
    c->txSeqBuf.last().seqNum = c->txSeq;
    c->txSeqBuf.last().timeSent = QTime::currentTime();
    c->txSeqBuf.last().retransmitCount = 0;
    c->txSeqBuf.last().data = QByteArray::fromRawData((const char*)p.packet, sizeof(p));

    c->socket->writeDatagram(c->txSeqBuf.last().data, c->ipAddress, c->port);
    c->txSeq++;
    if (c->idleTimer != Q_NULLPTR)
        c->idleTimer->start(100);

    return;
}

void udpServer::sendCapabilities(CLIENT* c)
{
    qDebug(logUdpServer()) << c->ipAddress.toString() << ": Sending Capabilities :" << c->txSeq;

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
    p.baudrate = (quint32)qToBigEndian(19200);
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
    }
    else {
        p.enablea = 0x00; // 0x01 enables TX 24K mode?
        if (txSampleRate == 48000) {
            p.txsample = 0x0800; // fixed tx sample frequency
        }
        else if (txSampleRate == 32000) {
            p.txsample = 0x0400;
        }
        else if (txSampleRate == 24000) {
            p.txsample = 0x0000;
            p.enablea = 0x01;
        }
        else if (txSampleRate == 16000) {
            p.txsample = 0x0200;
        }
        else if (txSampleRate == 12000) {
            p.txsample = 0x8000;
        }
    }

    // I still don't know what these are?
    p.enableb = 0x01; // 0x01 doesn't seem to do anything?
    p.enablec = 0x01; // 0x01 doesn't seem to do anything?
    p.capf = 0x5001; 
    p.capg = 0x0190;

    QMutexLocker locker(&mutex);
    c->txSeqBuf.append(SEQBUFENTRY());
    c->txSeqBuf.last().seqNum = p.seq;
    c->txSeqBuf.last().timeSent = QTime::currentTime();
    c->txSeqBuf.last().retransmitCount = 0;
    c->txSeqBuf.last().data = QByteArray::fromRawData((const char*)p.packet, sizeof(p));

    c->socket->writeDatagram(c->txSeqBuf.last().data, c->ipAddress, c->port);

    if (c->idleTimer != Q_NULLPTR)
        c->idleTimer->start(100);

    c->txSeq++;
    return;
}

// When client has requested civ/audio connection, this will contain their details
// Also used to display currently connected used information.
void udpServer::sendConnectionInfo(CLIENT* c)
{
    qDebug(logUdpServer()) << c->ipAddress.toString() << ": Sending ConnectionInfo :" << c->txSeq;
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

    QMutexLocker locker(&mutex);
    c->txSeqBuf.append(SEQBUFENTRY());
    c->txSeqBuf.last().seqNum = p.seq;
    c->txSeqBuf.last().timeSent = QTime::currentTime();
    c->txSeqBuf.last().retransmitCount = 0;
    c->txSeqBuf.last().data = QByteArray::fromRawData((const char*)p.packet, sizeof(p));

    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);

    if (c->idleTimer != Q_NULLPTR)
        c->idleTimer->start(100);

    c->txSeq++;
    return;
}

void udpServer::sendTokenResponse(CLIENT* c, quint8 type)
{
    qDebug(logUdpServer()) << c->ipAddress.toString() << ": Sending Token response for type: " << type;

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

    QMutexLocker locker(&mutex);
    c->txSeqBuf.append(SEQBUFENTRY());
    c->txSeqBuf.last().seqNum = p.seq;
    c->txSeqBuf.last().timeSent = QTime::currentTime();
    c->txSeqBuf.last().retransmitCount = 0;
    c->txSeqBuf.last().data = QByteArray::fromRawData((const char*)p.packet, sizeof(p));

    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);

    if (c->idleTimer != Q_NULLPTR)
        c->idleTimer->start(100);

    c->txSeq++;
    return;
}

#define PURGE_SECONDS 60

void udpServer::watchdog(CLIENT* c)
{

    //qDebug(logUdpServer()) << c->ipAddress.toString() << ":" << c->port << ":Buffers tx:"<< c->txSeqBuf.length() << " rx:" << c->rxSeqBuf.length();
    QMutexLocker locker(&mutex);
    // Erase old entries from the tx packet buffer. Keep the first 100 sent packets as we seem to get asked for these?
    if (!c->txSeqBuf.isEmpty())
    {
        c->txSeqBuf.erase(std::remove_if(c->txSeqBuf.begin(), c->txSeqBuf.end(), [](const SEQBUFENTRY& v)
        { return v.timeSent.secsTo(QTime::currentTime()) > PURGE_SECONDS; }), c->txSeqBuf.end());
    }

    // Erase old entries from the missing packets buffer
    if (!c->rxMissing.isEmpty()) {
        c->rxMissing.erase(std::remove_if(c->rxMissing.begin(), c->rxMissing.end(), [](const SEQBUFENTRY& v)
        { return v.timeSent.secsTo(QTime::currentTime()) > PURGE_SECONDS; }), c->rxMissing.end());
    }

    if (!c->rxSeqBuf.isEmpty()) {
        std::sort(c->rxSeqBuf.begin(), c->rxSeqBuf.end());

        if (c->rxSeqBuf.length() > 400)
        {
            c->rxSeqBuf.remove(0, 200);
        }
    }
}

void udpServer::sendStatus(CLIENT* c)
{

    qDebug(logUdpServer()) << c->ipAddress.toString() << ": Sending Status";

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

    p.civport=qToBigEndian(c->civPort);
    p.audioport=qToBigEndian(c->audioPort);

    // Send this to reject the request to tx/rx audio/civ
    //memcpy(p + 0x30, QByteArrayLiteral("\xff\xff\xff\xfe").constData(), 4);


    c->txSeq++;
    QMutexLocker locker(&mutex);
    c->txSeqBuf.append(SEQBUFENTRY());
    c->txSeqBuf.last().seqNum = p.seq;
    c->txSeqBuf.last().timeSent = QTime::currentTime();
    c->txSeqBuf.last().retransmitCount = 0;
    c->txSeqBuf.last().data = QByteArray::fromRawData((const char*)p.packet, sizeof(p));

    c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);

}


void udpServer::dataForServer(QByteArray d)
{

    //qDebug(logUdpServer()) << "Server got:" << d;
    foreach(CLIENT * client, civClients)
    {
        if (client != Q_NULLPTR && client->connected) {
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
            QMutexLocker locker(&mutex);
            client->txSeqBuf.append(SEQBUFENTRY());
            client->txSeqBuf.last().seqNum = p.seq;
            client->txSeqBuf.last().timeSent = QTime::currentTime();
            client->txSeqBuf.last().retransmitCount = 0;
            client->txSeqBuf.last().data = t;

            client->socket->writeDatagram(t, client->ipAddress, client->port);

            client->txSeq++;
            client->innerSeq++;
        }
    }

    return;
}

void udpServer::sendRxAudio()
{
    if (rxaudio && rxaudio->isChunkAvailable()) {
        QByteArray audio;
        rxaudio->getNextAudioChunk(audio);
        int len = 0;

        while (len < audio.length()) {
            audioPacket partial;
            partial.datain = audio.mid(len, 1364);
            receiveAudioData(partial);
            len = len + partial.datain.length();
        }
    }
}



void udpServer::receiveAudioData(const audioPacket &d)
{
    //qDebug(logUdpServer()) << "Server got:" << d.data.length();
    foreach(CLIENT * client, audioClients)
    {
        if (client != Q_NULLPTR && client->connected) {
            audio_packet p;
            memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
            p.len = sizeof(p) + d.datain.length();
            p.sentid = client->myId;
            p.rcvdid = client->remoteId;
            p.ident = 0x0080; // audio is always this?
            p.datalen = (quint16)qToBigEndian((quint16)d.datain.length());
            p.sendseq = (quint16)qToBigEndian((quint16)client->sendAudioSeq); // THIS IS BIG ENDIAN!
            p.seq = client->txSeq;
            QByteArray t = QByteArray::fromRawData((const char*)p.packet, sizeof(p));
            t.append(d.datain);
            QMutexLocker locker(&mutex);
            client->txSeqBuf.append(SEQBUFENTRY());
            client->txSeqBuf.last().seqNum = p.seq;
            client->txSeqBuf.last().timeSent = QTime::currentTime();
            client->txSeqBuf.last().retransmitCount = 0;
            client->txSeqBuf.last().data = t;
            client->socket->writeDatagram(t, client->ipAddress, client->port);

            client->txSeq++;
            client->sendAudioSeq++;
        }
    }

    return;
}

/// <summary>
/// Find all gaps in received packets and then send requests for them.
/// This will run every 100ms so out-of-sequence packets will not trigger a retransmit request.
/// </summary>
/// <param name="c"></param>
void udpServer::sendRetransmitRequest(CLIENT *c)
{

    QMutexLocker locker(&mutex);

    QByteArray missingSeqs;

    auto i = std::adjacent_find(c->rxSeqBuf.begin(), c->rxSeqBuf.end(), [](quint16 l, quint16 r) {return l + 1 < r; });
    while (i != c->rxSeqBuf.end())
    {
        if (i + 1 != c->rxSeqBuf.end())
        {
            if (*(i + 1) - *i < 30)
            {
                for (quint16 j = *i + 1; j < *(i + 1); j++)
                {
                    qDebug(logUdpServer()) << this->metaObject()->className() << ": Found missing seq between " << *i << " : " << *(i + 1) << " (" << j << ")";
                    auto s = std::find_if(c->rxMissing.begin(), c->rxMissing.end(), [&cs = j](SEQBUFENTRY& s) { return s.seqNum == cs; });
                    if (s == c->rxMissing.end())
                    {
                        // We haven't seen this missing packet before
                        qDebug(logUdpServer()) << this->metaObject()->className() << ": Adding to missing buffer (len="<< c->rxMissing.length() << "): " << j;
                        c->rxMissing.append(SEQBUFENTRY());
                        c->rxMissing.last().seqNum = j;
                        c->rxMissing.last().retransmitCount = 0;
                        c->rxMissing.last().timeSent = QTime::currentTime();
                        //packetsLost++;
                    }
                    else {
                        if (s->retransmitCount == 4 && j != 0)
                        {
                            // We have tried 4 times to request this packet, time to give up!
                            s = c->rxMissing.erase(s);
                            c->rxSeqBuf.append(j); // Final thing is to add to received buffer!
                        }

                    }
                }
            }
            else {
                qDebug(logUdpServer()) << c->ipAddress.toString() << ": Too many missing, flushing buffers";
                c->rxSeqBuf.clear();
                missingSeqs.clear();
                break;
            }
        }
        i++;
    }


    for (auto it = c->rxMissing.begin(); it != c->rxMissing.end(); ++it)
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
        p.sentid = c->myId;
        p.rcvdid = c->remoteId;
        if (missingSeqs.length() == 4) // This is just a single missing packet so send using a control.
        {
            p.seq = (missingSeqs[0] & 0xff) | (quint16)(missingSeqs[1] << 8);
            qDebug(logUdpServer()) << c->ipAddress.toString() << ": sending request for missing packet : " << hex << p.seq;
            c->socket->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), c->ipAddress, c->port);
        }
        else
        {
            qDebug(logUdpServer()) <<  c->ipAddress.toString() << ": sending request for multiple missing packets : " << missingSeqs.toHex();
            missingSeqs.insert(0, p.packet, sizeof(p.packet));
            c->socket->writeDatagram(missingSeqs, c->ipAddress, c->port);
        }
    }
}


/// <summary>
/// This function is passed a pointer to the list of connection objects and a pointer to the object itself
/// Needs to stop and delete all timers, remove the connection from the list and delete the connection. 
/// </summary>
/// <param name="l"></param>
/// <param name="c"></param>
void udpServer::deleteConnection(QList<CLIENT*> *l, CLIENT* c)
{
    qDebug(logUdpServer()) << "Deleting connection to: " << c->ipAddress.toString() << ":" << QString::number(c->port);
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
    qDebug(logUdpServer()) << "Current Number of clients connected: " << l->length();

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
