#include "yaesuudpbase.h"

#include "logcategories.h"

// This "base" UDP class handles all connection and encoding/decoding of packets.
// Each "child" class has it's own constructor.
// use a mutex to ensure that packet buffer doesn't get overwritten.

void yaesuUdpBase::init()
{
    // Separate init() needs to bind socket after being moved to dedicated thread.
    sock.reset(new QUdpSocket(this));
    sock->bind(this->localAddr);
    qInfo(logUdp()).noquote() << QString("Created Yaesu UDP connection to %0:%1 (from %2:%3)")
                                     .arg(remoteAddr.toString()).arg(remotePort).arg(sock->localAddress().toString()).arg(sock->localPort()) ;
    connect(sock.data(), &QUdpSocket::readyRead, this, &yaesuUdpBase::incoming);

    sendConnect();

}
yaesuUdpBase::~yaesuUdpBase()
{
    if (sock != Q_NULLPTR)
        qInfo(logUdp()).noquote() << QString("Closing Yaesu UDP connection to %0:%1 (from %2:%3)")
                                     .arg(remoteAddr.toString()).arg(remotePort).arg(sock->localAddress().toString()).arg(sock->localPort()) ;
}

void yaesuUdpBase::incoming() {
    qint64 remoteLen = 0;
    qint64 localLen = 0;
    //QMutexLocker locker(&mutex);
    while (sock->hasPendingDatagrams()) {
        remoteLen = sock->readDatagram((char*) &remoteBuffer, sizeof(yaesuEncodedFrame), &remoteAddr, &remotePort);
        if (remoteLen >= 0) {
            localLen = decodePacket(&remoteBuffer, remoteLen);
            if (localLen >= 0) {
                incomingUdp(localBuffer, localLen);
            }
        }
    }
}

void yaesuUdpBase::outgoing(void *buf, size_t bufLen)
{
    //QMutexLocker locker(&mutex);
    if (remoteAddr.isNull())
        return;
    qint64 remoteLen = encodePacket((quint8*) buf, bufLen);
    if (remoteLen < 0)
        return;
    if (sock != Q_NULLPTR)
        sock->writeDatagram((char*)&remoteBuffer, remoteLen, remoteAddr, remotePort);
}

int yaesuUdpBase::decodePacket(yaesuEncodedFrame* enc, size_t encLen) {
    if (encLen - 8 > YAESU_MAX_FRAME_SIZE)
        return -1;
    size_t bufLen = encLen - 8;
    if (enc->type != 0x5a5a)
        return -2;
    if (enc->encodingType != 0x100)
        return -3;
    quint8 encKey = enc->firstKey ^ enc->secondKey;
    for (size_t i = 0; i < bufLen; i++) {
        quint8 ib = (quint8)i;
        ib = ~ib + 1;
        localBuffer[i] = encKey ^ ib ^ enc->encodedData[i];
    }
    return int(bufLen);
}

int yaesuUdpBase::encodePacket(quint8* rawData, size_t dataLen) {
    if (YAESU_MAX_FRAME_SIZE < dataLen)
        return -1;
    remoteBuffer.type = 0x5a5a;
    remoteBuffer.encodingType = 0x100;
    remoteBuffer.firstKey = rand() % 0x100;
    remoteBuffer.secondKey = rand() % 0x100;
    quint8 encKey = remoteBuffer.firstKey ^ remoteBuffer.secondKey;
    remoteBuffer.length = int(dataLen + 4);
    for (size_t i = 0; i < dataLen; i++) {
        quint8 ib = (quint8)i;
        ib = ~ib + 1;
        remoteBuffer.encodedData[i] = encKey ^ ib ^ rawData[i];
    }
    return int(dataLen + 8);
}

