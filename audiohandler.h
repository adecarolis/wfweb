#ifndef AUDIOHANDLER_H
#define AUDIOHANDLER_H

#include <QObject>

#include <QByteArray>
#include <QMutex>
#include <QtEndian>
#include <QtMath>

#if defined(RTAUDIO)
#include "rtaudio/RtAudio.h"
#elif defined (PORTAUDIO)
#include "portaudio.h"
#error "PORTAUDIO is not currently supported"
#else
#include <QAudioOutput>
#include <QAudioFormat>
#include <QAudioDeviceInfo>
#include <QAudioInput>
#include <QIODevice>
#endif

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
#ifdef Q_OS_WIN
#include "opus.h"
#else
#include "opus/opus.h"
#endif

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

struct audioSetup {
    QString name;
    quint8 bits;
    quint8 radioChan;
    quint16 samplerate;
    quint16 latency;
    quint8 codec;
    bool ulaw;
    bool isinput;
#if defined(RTAUDIO)
    int port;
#elif defined(PORTAUDIO)
#else
    QAudioDeviceInfo port;
#endif
    quint8 resampleQuality;
};

// For QtMultimedia, use a native QIODevice
#if !defined(PORTAUDIO) && !defined(RTAUDIO)
class audioHandler : public QIODevice
#else
class audioHandler : public QObject
#endif
{
    Q_OBJECT

public:
    audioHandler(QObject* parent = 0);
    ~audioHandler();

    int getLatency();

#if !defined (RTAUDIO) && !defined(PORTAUDIO)
    bool setDevice(QAudioDeviceInfo deviceInfo);

    void start();
    void flush();
    void stop();
    qint64 bytesAvailable() const;
    bool isSequential() const;
#endif

    void getNextAudioChunk(QByteArray &data);

public slots:
    bool init(audioSetup setup);
    void changeLatency(const quint16 newSize);
    void setVolume(unsigned char volume);
    void incomingAudio(const audioPacket data);

private slots:
#if !defined (RTAUDIO) && !defined(PORTAUDIO)
    void notified();
    void stateChanged(QAudio::State state);
#endif

signals:
    void audioMessage(QString message);
    void sendLatency(quint16 newSize);
    void haveAudioData(const QByteArray& data);


private:

#if defined(RTAUDIO)
    int readData(void* outputBuffer, void* inputBuffer, unsigned int nFrames, double streamTime, RtAudioStreamStatus status);

    static int staticRead(void* outputBuffer, void* inputBuffer, unsigned int nFrames, double streamTime, RtAudioStreamStatus status, void* userData) {
        return static_cast<audioHandler*>(userData)->readData(outputBuffer, inputBuffer, nFrames, streamTime, status);
    }

    int writeData(void* outputBuffer, void* inputBuffer, unsigned int nFrames, double streamTime, RtAudioStreamStatus status);

    static int staticWrite(void* outputBuffer, void* inputBuffer, unsigned int nFrames, double streamTime, RtAudioStreamStatus status, void* userData) {
        return static_cast<audioHandler*>(userData)->writeData(outputBuffer, inputBuffer, nFrames, streamTime, status);
    }
#elif defined(PORTAUDIO)

#else
    qint64 readData(char* data, qint64 nBytes);
    qint64 writeData(const char* data, qint64 nBytes);
#endif

    void reinit();
    bool            isInitialized=false;

#if defined(RTAUDIO)
    RtAudio* audio = Q_NULLPTR;
    int audioDevice = 0;
    RtAudio::StreamParameters aParams;
    RtAudio::StreamOptions options;
    RtAudio::DeviceInfo info;
#elif defined(PORTAUDIO)
#else
    QAudioOutput* audioOutput=Q_NULLPTR;
    QAudioInput* audioInput=Q_NULLPTR;
    QAudioFormat     format;
    QAudioDeviceInfo deviceInfo;
#endif
    SpeexResamplerState* resampler = Q_NULLPTR;

    bool            isUlaw;
    quint16         audioLatency;
    bool            isInput;   // Used to determine whether input or output audio
    unsigned int             chunkSize;
    bool            chunkAvailable;

    quint32         lastSeq;
    quint32         lastSentSeq=0;
        
    quint16         radioSampleRate;
    quint16         nativeSampleRate=0;
    quint8          radioSampleBits;
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
    audioSetup setup;
    OpusEncoder* encoder=Q_NULLPTR;
    OpusDecoder* decoder=Q_NULLPTR;
};

#endif // AUDIOHANDLER_H
