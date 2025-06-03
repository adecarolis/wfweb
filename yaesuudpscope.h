#ifndef YAESUUDPSCOPE_H
#define YAESUUDPSCOPE_H


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
#include <QtMinMax>

// Allow easy endian-ness conversions
#include <QtEndian>

// Needed for audio
#include <QBuffer>
#include <QThread>

#include <QDebug>

#include "packettypes.h"
#include "yaesuudpbase.h"

#include "audiohandler.h"
#include "udpbase.h"
#include "udpcivdata.h"
#include "udpaudio.h"
#include "yaesuudpbase.h"


class yaesuUdpScope: public yaesuUdpBase
{
    Q_OBJECT

public:
    yaesuUdpScope(QHostAddress local, QHostAddress remote, quint16 port);

public slots:
    void incomingUdp(void* buf, size_t bufLen);

private slots:
    void sendHeartbeat();

signals:
    void haveScopeData(QByteArray d);
    void haveNetworkError(errorType);

private:
    void sendConnect();

    enum yaesuState {yaesuStart=0, yaesuConnect, yaesuCatSent,yaesuCatPending,yaesuAudioReady,yaesuAudioSent,yaesuAudioPending,yaesuScopeReady,yaesuScopeSent,yaesuConnectPending,yaesuConnected};
    yaesuState state = yaesuStart;
    quint16 catPacketId = 0;
    quint16 rxCatPacketId = 0;

};

#endif // YAESUUDPSCOPE_H
