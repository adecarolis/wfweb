#ifndef YAESUUDPBASE_H
#define YAESUUDPBASE_H

#include <QObject>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QHostInfo>
#include <QTimer>
#include <QMutex>
#include <QDateTime>
#include <QByteArray>
#include <QVector>
#include <QMap>
#include <QUuid>

// Allow easy endian-ness conversions
#include <QtEndian>

// Needed for audio
#include <QBuffer>
#include <QThread>

#include <QDebug>

#include "audioconverter.h"
#include "packettypes.h"
#include "wfviewtypes.h"
#include "icomudpbase.h"

class yaesuUdpBase : public QObject
{
    Q_OBJECT
public:

    ~yaesuUdpBase();

signals:
    void haveNetworkError(errorType);
    void haveNetworkStatus(networkStatus);

public slots:
    virtual void init();
    void incoming();
    void outgoing(void* buf, size_t bufLen);
    void setSession(quint32 s) {this->session = s;};

protected:
    virtual void sendConnect() {};
    virtual void incomingUdp(void* buf, size_t bufLen) {Q_UNUSED(buf) Q_UNUSED(bufLen)};

    QTimer* heartbeat = Q_NULLPTR;

    quint16 remotePort;
    QHostAddress remoteAddr;
    QHostAddress localAddr;

    quint32 session=0;

    enum yaesuState {yaesuStart=0, yaesuConnect, yaesuCatSent,yaesuCatPending,yaesuAudioReady,yaesuAudioSent,yaesuAudioPending,yaesuScopeReady,yaesuScopeSent,yaesuConnectPending,yaesuConnected};

    yaesuState state = yaesuStart;
    quint16 txPacketId = 0;
    quint16 rxPacketId = 0;

    QDateTime lastPingSentTime;

    quint32 packetsSent = 0;
    quint32 packetsLost = 0;


private:
    int decodePacket(yaesuEncodedFrame* enc, size_t encLen);
    int encodePacket(quint8* rawData, size_t dataLen);

    QMutex mutex;

    QScopedPointer<QUdpSocket> sock;

    yaesuEncodedFrame remoteBuffer;
    quint8 localBuffer[YAESU_MAX_FRAME_SIZE];

};

#endif // YAESUUDPBASE_H
