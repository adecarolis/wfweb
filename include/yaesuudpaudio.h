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

#define audioLevelBufferSize (4)

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
    void setVolume(quint8 vol);
    void getRxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over);
    void getTxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over);
    void receiveAudioData(audioPacket audio);

signals:
    void haveAudioData(audioPacket data);
    void setupTxAudio(audioSetup setup);
    void setupRxAudio(audioSetup setup);
    void haveChangeLatency(quint16 value);
    void haveSetVolume(quint8 value);
    void haveNetworkAudioLevels(networkAudioLevels);

private:


    void sendConnect();

    void sendCommandControlFrame(QString data);

    audioSetup rxSetup;
    audioSetup txSetup;

    yaesuAudioFormat audioCodec = ShortLE;
    quint16 audioSize = 640;
    quint8 audioChannels = 2;
    quint16 seqPrefix = 0;

    audioHandlerBase* rxaudio = Q_NULLPTR;
    QThread* rxAudioThread = Q_NULLPTR;

    audioHandlerBase* txaudio = Q_NULLPTR;
    QThread* txAudioThread = Q_NULLPTR;

    quint8 audioLevelsTxPeak[audioLevelBufferSize];
    quint8 audioLevelsRxPeak[audioLevelBufferSize];

    quint8 audioLevelsTxRMS[audioLevelBufferSize];
    quint8 audioLevelsRxRMS[audioLevelBufferSize];

    quint8 audioLevelsTxPosition = 0;
    quint8 audioLevelsRxPosition = 0;
    quint8 findMean(quint8 *d);
    quint8 findMax(quint8 *d);
    networkStatus status;


};

#endif // YAESUUDPAUDIO_H
