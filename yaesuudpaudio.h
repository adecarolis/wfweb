#ifndef YAESUUDPAUDIO_H
#define YAESUUDPAUDIO_H


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


class yaesuUdpAudio: public yaesuUdpBase
{
    Q_OBJECT

public:
    yaesuUdpAudio(QHostAddress local, QHostAddress remote, quint16 port, audioSetup rxAudio, audioSetup txAudio);
    ~yaesuUdpAudio();


public slots:
    void init();
    void incomingUdp(void* buf, size_t bufLen);

private slots:
    void sendHeartbeat();

signals:
    void haveAudioData(audioPacket data);
    void setupTxAudio(audioSetup setup);
    void setupRxAudio(audioSetup setup);

    void haveNetworkError(errorType);

    void haveChangeLatency(quint16 value);
    void haveSetVolume(quint8 value);
    void haveRxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over);
    void haveTxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over);

private:


    void sendConnect();

    void sendCommandControlFrame(QString data);

    audioSetup rxSetup;
    audioSetup txSetup;

    yaesuAudioFormat audioCodec = ShortLE;
    quint16 audioSize = 640;
    quint8 audioChannels = 2;
    quint16 seqPrefix = 0;

    audioHandler* rxaudio = Q_NULLPTR;
    QThread* rxAudioThread = Q_NULLPTR;

    audioHandler* txaudio = Q_NULLPTR;
    QThread* txAudioThread = Q_NULLPTR;


};

#endif // YAESUUDPAUDIO_H
