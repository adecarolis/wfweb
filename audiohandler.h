#ifndef AUDIOHANDLER_H
#define AUDIOHANDLER_H

#include <QObject>

#include <QtMultimedia/QAudioOutput>
#include <QByteArray>
#include <QMutex>
#include <QtEndian>
#include <QtMath>
#include "rtaudio/RtAudio.h"

typedef signed short  MY_TYPE;
#define FORMAT RTAUDIO_SINT16
#define SCALE  32767.0

#define LOG100 4.60517018599

#include <QThread>
#include <QTimer>
#include <QTime>
#include <QMap>
#include "resampler/speex_resampler.h"
#include "ring/ring.h"


#include <QDebug>

//#define BUFFER_SIZE (32*1024)

#define INTERNAL_SAMPLE_RATE 48000

#define MULAW_BIAS 33
#define MULAW_MAX 0x1fff

struct audioPacket {
    quint32 seq;
    QTime time;
    quint16 sent;
    QByteArray data;
};

class audioHandler : public QObject
{
    Q_OBJECT

public:
    audioHandler(QObject* parent = 0);
    ~audioHandler();

    int getLatency();

    void getNextAudioChunk(QByteArray &data);

private slots:
    bool init(const quint8 bits, const quint8 channels, const quint16 samplerate, const quint16 latency, const bool isulaw, const bool isinput, int port, quint8 resampleQuality);
    void changeLatency(const quint16 newSize);
    void setVolume(unsigned char volume);
    void incomingAudio(const audioPacket data);

signals:
    void audioMessage(QString message);
    void sendLatency(quint16 newSize);
    void haveAudioData(const QByteArray& data);


private:
    int readData(void* outputBuffer, void* inputBuffer, unsigned int nFrames, double streamTime, RtAudioStreamStatus status);

    static int staticRead(void* outputBuffer, void* inputBuffer, unsigned int nFrames, double streamTime, RtAudioStreamStatus status, void* userData) {
        return static_cast<audioHandler*>(userData)->readData(outputBuffer, inputBuffer, nFrames, streamTime, status);
    }

    int writeData(void* outputBuffer, void* inputBuffer, unsigned int nFrames, double streamTime, RtAudioStreamStatus status);

    static int staticWrite(void* outputBuffer, void* inputBuffer, unsigned int nFrames, double streamTime, RtAudioStreamStatus status, void* userData) {
        return static_cast<audioHandler*>(userData)->writeData(outputBuffer, inputBuffer, nFrames, streamTime, status);
    }

    void reinit();

    bool            isInitialized;
    RtAudio audio;
    int audioDevice = 0;
    RtAudio::StreamParameters aParams;
    RtAudio::DeviceInfo info;

    SpeexResamplerState* resampler = Q_NULLPTR;

    bool            isUlaw;
    quint16         audioLatency;
    bool            isInput;   // Used to determine whether input or output audio
    unsigned int             chunkSize;
    bool            chunkAvailable;

    quint32        lastSeq;

    quint16          radioSampleRate;
    quint8           radioSampleBits;
    quint8          radioChannels;

    QMap<quint32, audioPacket>audioBuffer;

    unsigned int ratioNum;
    unsigned int ratioDen;

    wilt::Ring<audioPacket> *ringBuf=Q_NULLPTR;
    volatile bool ready = false;
    audioPacket tempBuf;
    quint16 currentLatency;
    qreal volume=1.0;
    int devChannels;
};

#endif // AUDIOHANDLER_H
