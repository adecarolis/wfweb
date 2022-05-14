#ifndef AUDIOHANDLER_H
#define AUDIOHANDLER_H

/* QT Headers */
#include <QObject>
#include <QByteArray>
#include <QMutex>
#include <QtEndian>
#include <QtMath>
#include <QThread>
#include <QTime>
#include <QMap>
#include <QDebug>
#include <QTimer>

/* QT Audio Headers */
#include <QAudioOutput>
#include <QAudioFormat>
#include <QAudioDeviceInfo>
#include <QAudioInput>
#include <QIODevice>


/* wfview Packet types */
#include "packettypes.h"

/* Logarithmic taper for volume control */
#include "audiotaper.h"


/* Audio converter class*/
#include "audioconverter.h"


#define MULAW_BIAS 33
#define MULAW_MAX 0x1fff


// For QtMultimedia, use a native QIODevice
//class audioHandler : public QIODevice
class audioHandler : public QObject
{
    Q_OBJECT

public:
    explicit audioHandler(QObject* parent = nullptr);
    ~audioHandler();

    int getLatency();

    void start();
    void stop();

    quint16 getAmplitude();

public slots:
    bool init(audioSetup setup);
    void changeLatency(const quint16 newSize);
    void setVolume(unsigned char volume);
    void incomingAudio(const audioPacket data);
    void convertedInput(audioPacket audio);
    void convertedOutput(audioPacket audio);

private slots:
    void stateChanged(QAudio::State state);
    void clearUnderrun();    
    void getNextAudioChunk();

signals:
    void audioMessage(QString message);
    void sendLatency(quint16 newSize);
    void haveAudioData(const audioPacket& data);
    void haveLevels(quint16 amplitude,quint16 latency,quint16 current,bool under,bool over);
    void setupConverter(QAudioFormat in, QAudioFormat out, quint8 opus, quint8 resamp);
    void sendToConverter(audioPacket audio);


private:

    //qint64 readData(char* data, qint64 nBytes);
    //qint64 writeData(const char* data, qint64 nBytes);


    bool            isUnderrun = false;
    bool            isOverrun = false;
    bool            isInitialized=false;
    bool            isReady = false;
    bool            audioBuffered = false;

    QAudioOutput* audioOutput=Q_NULLPTR;
    QAudioInput* audioInput=Q_NULLPTR;
    QIODevice* audioDevice=Q_NULLPTR;
    QAudioFormat     inFormat;
    QAudioFormat     outFormat;
    QAudioDeviceInfo deviceInfo;

    audioConverter* converter=Q_NULLPTR;
    QThread* converterThread = Q_NULLPTR;
    QTime lastReceived;
    //r8b::CFixedBuffer<double>* resampBufs;
    //r8b::CPtrKeeper<r8b::CDSPResampler24*>* resamps;

    quint16         audioLatency;

    quint32         lastSeq;
    quint32         lastSentSeq=0;
    qint64          elapsedMs = 0;
        
    int             delayedPackets=0;

    double resampleRatio;

    volatile bool ready = false;
    audioPacket tempBuf;
    quint16 currentLatency;
    float amplitude=0.0;
    qreal volume = 1.0;

    audioSetup setup;

    OpusEncoder* encoder = Q_NULLPTR;
    OpusDecoder* decoder = Q_NULLPTR;
    QTimer* underTimer;
};


#endif // AUDIOHANDLER_H
