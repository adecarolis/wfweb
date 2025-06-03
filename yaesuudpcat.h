#ifndef YAESUUDPCAT_H
#define YAESUUDPCAT_H


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


class yaesuUdpCat: public yaesuUdpBase
{
    Q_OBJECT

public:
    yaesuUdpCat(QHostAddress local, QHostAddress remote, quint16 port);


public slots:
    void incomingUdp(void* buf, size_t bufLen);
    void sendCatDataToRig(QByteArray d);

private slots:
    void sendHeartbeat();

signals:
    void haveCatDataFromRig(QByteArray d);
    void haveNetworkError(errorType);
    void initAudio();

private:
    void sendConnect();

};

#endif // YAESUUDPCAT_H
