#ifndef YAESUUDPCONTROL_H
#define YAESUUDPCONTROL_H


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

#include "packettypes.h"
#include "yaesuudpbase.h"

#include "audiohandler.h"
#include "icomudpbase.h"
#include "icomudpcivdata.h"
#include "icomudpaudio.h"
#include "yaesuudpbase.h"


class yaesuUdpControl: public yaesuUdpBase
{
    Q_OBJECT

public:
    yaesuUdpControl(udpPreferences prefs, QHostAddress local, QHostAddress remote);
    ~yaesuUdpControl();

public slots:
    void incomingUdp(void* buf, size_t bufLen);

private slots:
    void sendHeartbeat();
protected:

signals:
    void initCat();
    void haveSession(quint32 s);

private:

    void sendConnect();
    void sendDisconnect();

    networkStatus status;

    udpPreferences prefs;

    enum yaesuState {yaesuStart=0,yaesuConnect,yaesuCatSent,yaesuAudioSent,yaesuScopeSent,yaesuReady};
    yaesuState state = yaesuStart;
};

#endif // YAESUUDPCONTROL_H
