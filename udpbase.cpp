#include "udpbase.h"
#include "logcategories.h"

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
                qDebug(logUdp()) << this->metaObject()->className() << ": Sending (multiple packet) retransmit of " << QString("0x%1").arg(match->seqNum, 0, 16);
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
                qDebug(logUdp()) << this->metaObject()->className() << "Large seq number gap detected, previous highest: " <<
                    QString("0x%1").arg(rxSeqBuf.lastKey(), 0, 16) << " current: " << QString("0x%1").arg(in->seq, 0, 16);
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
                    qDebug(logUdp()) << this->metaObject()->className() << "1 or more missing packets detected, previous: " <<
                        QString("0x%1").arg(rxSeqBuf.lastKey(), 0, 16) << " current: " << QString("0x%1").arg(in->seq, 0, 16);
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
                    qDebug(logUdp()) << this->metaObject()->className() << ": Missing SEQ has been received! " << QString("0x%1").arg(in->seq, 0, 16);

                    s = rxMissing.erase(s);
                }
                missingMutex.unlock();

            }

        }
        rxBufferMutex.unlock();

    }
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
        }
        else {
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
            qInfo(logUdp()) << this->metaObject()->className() << ": sending request for missing packet : " << QString("0x%1").arg(p.seq, 0, 16);
            udpMutex.lock();
            udp->writeDatagram(QByteArray::fromRawData((const char*)p.packet, sizeof(p)), radioIP, port);
            udpMutex.unlock();
        }
        else
        {
            qInfo(logUdp()) << this->metaObject()->className() << ": sending request for multiple missing packets : " << missingSeqs.toHex(':');
            missingMutex.lock();
            p.len = (quint32)sizeof(p) + missingSeqs.size();
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
    QTime now = QTime::currentTime();
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
    }
    else {
        qInfo(logUdp()) << this->metaObject()->className() << ": txBuffer mutex is locked";
    }
    // Stop using purgeOldEntries() as it is likely slower than just removing the earliest packet.
    //qInfo(logUdp()) << this->metaObject()->className() << "RX:" << rxSeqBuf.size() << "TX:" <<txSeqBuf.size() << "MISS:" << rxMissing.size();
    //purgeOldEntries(); // Delete entries older than PURGE_SECONDS seconds (currently 5)
    sendSeq++;

    udpMutex.lock();
    udp->writeDatagram(d, radioIP, port);

    /*if (congestion > 10) { // Poor quality connection?
        udp->writeDatagram(d, radioIP, port);
        if (congestion > 20) // Even worse so send again.
            udp->writeDatagram(d, radioIP, port);
    } */
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

    }
    else {
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
    }
    else {
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
    }
    else {
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


