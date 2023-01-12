#include "udpcivdata.h"
#include "logcategories.h"

// Class that manages all Civ Data to/from the rig
udpCivData::udpCivData(QHostAddress local, QHostAddress ip, quint16 civPort, bool splitWf, quint16 localPort = 0)
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
    p.len = (quint32)sizeof(p) + d.length();
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
                        int pos = r.indexOf(QByteArrayLiteral("\x27\x00\x00")) + 2;
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

                            int divSize = (len / numDivisions) + 6;
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
                                    wfPacket.append(r.mid((pos + 15) + ((i - 1) * divSize), divSize));
                                }
                                if (i < numDivisions - 1) {
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
