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
#include "packettypes.h"


class rtpAudio : public QObject
{
    Q_OBJECT
public:
    explicit rtpAudio(QString ip, quint16 port, audioSetup outSetup, audioSetup inSetup, QObject *parent = nullptr);
    ~rtpAudio();

signals:
    void haveAudioData(audioPacket data);

    void setupInAudio(audioSetup setup);
    void setupOutAudio(audioSetup setup);

    void haveChangeLatency(quint16 value);
    void haveSetVolume(quint8 value);
    void haveOutLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over);
    void haveInLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over);

private slots:
    void init();
    void dataReceived();
    void changeLatency(quint16 value);
    void setVolume(quint8 value);
    void getOutLevels(quint16 amplitude, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over);
    void getInLevels(quint16 amplitude, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over);
    void receiveAudioData(audioPacket audio);

private:

    QUdpSocket* udp = Q_NULLPTR;

    audioSetup outSetup;
    audioSetup inSetup;

    audioHandlerBase* outaudio = Q_NULLPTR;
    QThread* outAudioThread = Q_NULLPTR;

    audioHandlerBase* inaudio = Q_NULLPTR;
    QThread* inAudioThread = Q_NULLPTR;

    QTimer* inAudioTimer = Q_NULLPTR;
    bool enableIn = true;
    bool enableOut = true;

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
