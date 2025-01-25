#ifndef RTPAUDIO_H
#define RTPAUDIO_H

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
#include <QFile>
#include <QStandardPaths>
#include <QDebug>

#include "audiohandler.h"
#include "pahandler.h"
#include "rthandler.h"
#include "tciaudiohandler.h"
#include "packettypes.h"


class rtpAudio : public QObject
{
    Q_OBJECT
public:
    explicit rtpAudio(QString ip, quint16 port, audioSetup rxSetup, audioSetup txSetup, QObject *parent = nullptr);
    ~rtpAudio();

signals:
    void haveAudioData(audioPacket data);

    void setupTxAudio(audioSetup setup);
    void setupRxAudio(audioSetup setup);

    void haveChangeLatency(quint16 value);
    void haveSetVolume(quint8 value);
    void haveRxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over);
    void haveTxLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over);

private slots:
    void init();
    void dataReceived();
    void changeLatency(quint16 value);
    void setVolume(quint8 value);
    void getRxLevels(quint16 amplitude, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over);
    void getTxLevels(quint16 amplitude, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over);
    void receiveAudioData(audioPacket audio);

private:

    QUdpSocket* udp = Q_NULLPTR;

    audioSetup rxSetup;
    audioSetup txSetup;

    audioHandler* rxaudio = Q_NULLPTR;
    QThread* rxAudioThread = Q_NULLPTR;

    audioHandler* txaudio = Q_NULLPTR;
    QThread* txAudioThread = Q_NULLPTR;

    QTimer* txAudioTimer = Q_NULLPTR;
    bool enableTx = true;

    QMutex audioMutex;

    QHostAddress ip;
    quint16 port = 0;
    int packetCount=0;
    QFile debugFile;
    quint64 size=0;
    quint16 seq=0;

signals:
};

#endif // RTPAUDIO_H
