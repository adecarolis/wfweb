#ifndef rtHandler_H
#define rtHandler_H

#include <QObject>
#include <QByteArray>
#include <QThread>
#include <QMutex>

#ifndef Q_OS_LINUX
#include "RtAudio.h"
#else
#include "rtaudio/RtAudio.h"
#endif


#include <QAudioFormat>
#include <QTime>
#include <QMap>
#include <QTimer>


/* wfview Packet types */
#include "packettypes.h"

/* Logarithmic taper for volume control */
#include "audiotaper.h"

#include "audiohandler.h"
/* Audio converter class*/
#include "audioconverter.h"

#include <QDebug>


class rtHandler : public audioHandler
{
    Q_OBJECT

public:
    rtHandler(QObject* parent = 0);
    ~rtHandler();

    int getLatency();

    using audioHandler::getNextAudioChunk;
    void getNextAudioChunk(QByteArray& data);
    quint16 getAmplitude();

public slots:
    bool init(audioSetup setup);
    void changeLatency(const quint16 newSize);
    void setVolume(unsigned char volume);
    void convertedInput(audioPacket audio);
    void convertedOutput(audioPacket audio);
    void incomingAudio(const audioPacket data);


private slots:

signals:
    void audioMessage(QString message);
    void sendLatency(quint16 newSize);
    void haveAudioData(const audioPacket& data);
    void haveLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over);
    void setupConverter(QAudioFormat in, codecType codecIn, QAudioFormat out, codecType codecOut, quint8 opus, quint8 resamp);
    void sendToConverter(audioPacket audio);

private:


    int readData(void* outputBuffer, void* inputBuffer, unsigned int nFrames, double streamTime, RtAudioStreamStatus status);

    static int staticRead(void* outputBuffer, void* inputBuffer, unsigned int nFrames, double streamTime, RtAudioStreamStatus status, void* userData) {
        return static_cast<rtHandler*>(userData)->readData(outputBuffer, inputBuffer, nFrames, streamTime, status);
    }


    int writeData(void* outputBuffer, void* inputBuffer, unsigned int nFrames, double streamTime, RtAudioStreamStatus status);

    static int staticWrite(void* outputBuffer, void* inputBuffer, unsigned int nFrames, double streamTime, RtAudioStreamStatus status, void* userData) {
        return static_cast<rtHandler*>(userData)->writeData(outputBuffer, inputBuffer, nFrames, streamTime, status);
    }


    bool            isInitialized = false;

    RtAudio* audio = Q_NULLPTR;
    int audioDevice = 0;
    RtAudio::StreamParameters aParams;
    RtAudio::StreamOptions options;
    RtAudio::DeviceInfo info;

    quint16         audioLatency;
    unsigned int    chunkSize;

    quint32         lastSeq;
    quint32         lastSentSeq = 0;

    quint16 currentLatency;
    float amplitude = 0.0;
    qreal volume = 1.0;

    audioSetup setup;
    QAudioFormat     radioFormat;
    QAudioFormat     nativeFormat;
    audioConverter* converter = Q_NULLPTR;
    QThread* converterThread = Q_NULLPTR;
    QByteArray arrayBuffer;    
    bool            isUnderrun = false;
    bool            isOverrun = false;
    QMutex          audioMutex;
    int             retryConnectCount = 0;
};

#endif // rtHandler_H
