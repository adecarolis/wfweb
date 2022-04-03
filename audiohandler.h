#ifndef AUDIOHANDLER_H
#define AUDIOHANDLER_H

#include <QObject>

#include <QByteArray>
#include <QMutex>
#include <QtEndian>
#include <QtMath>

#if defined(RTAUDIO)
#ifdef Q_OS_WIN
#include "RtAudio.h"
#else
#include "rtaudio/RtAudio.h"
#endif
#elif defined (PORTAUDIO)
#include "portaudio.h"
//#error "PORTAUDIO is not currently supported"
#else
#include <QAudioOutput>
#include <QAudioFormat>
#include <QAudioDeviceInfo>
#include <QAudioInput>
#include <QIODevice>
#endif

#include "packettypes.h"

typedef signed short  MY_TYPE;
#define FORMAT RTAUDIO_SINT16

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
#include "audiotaper.h"

#include <Eigen/Eigen>

//#include <r8bbase.h>
//#include <CDSPResampler.h>

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
    quint8 guid[GUIDLEN];
};

struct audioSetup {
    QString name;
//    quint8 bits;
//    quint8 radioChan;
//    quint16 samplerate;
    quint16 latency;
    quint8 codec;
    bool ulaw = false;
    bool isinput;
    QAudioFormat format; // Use this for all audio APIs
#if defined(RTAUDIO) || defined(PORTAUDIO)
    int port;
#else
    QAudioDeviceInfo port;
#endif
    quint8 resampleQuality;
    unsigned char localAFgain;
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
    quint16 getAmplitude();

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
    int readData(const void* inputBuffer, void* outputBuffer,
        unsigned long nFrames,
        const PaStreamCallbackTimeInfo* streamTime,
        PaStreamCallbackFlags status);
    static int staticRead(const void* inputBuffer, void* outputBuffer, unsigned long nFrames, const PaStreamCallbackTimeInfo* streamTime, PaStreamCallbackFlags status, void* userData) {
        return ((audioHandler*)userData)->readData(inputBuffer, outputBuffer, nFrames, streamTime, status);
    }    
    
    int writeData(const void* inputBuffer, void* outputBuffer,
        unsigned long nFrames,
        const PaStreamCallbackTimeInfo* streamTime,
        PaStreamCallbackFlags status);
    static int staticWrite(const void* inputBuffer, void* outputBuffer, unsigned long nFrames, const PaStreamCallbackTimeInfo* streamTime, PaStreamCallbackFlags status, void* userData) {
        return ((audioHandler*)userData)->writeData(inputBuffer, outputBuffer, nFrames, streamTime, status);
    }

#else
    qint64 readData(char* data, qint64 nBytes);
    qint64 writeData(const char* data, qint64 nBytes);
#endif

    void reinit();
    bool            isInitialized=false;
    bool            isReady = false;
    bool            audioBuffered = false;
#if defined(RTAUDIO)
    RtAudio* audio = Q_NULLPTR;
    int audioDevice = 0;
    RtAudio::StreamParameters aParams;
    RtAudio::StreamOptions options;
    RtAudio::DeviceInfo info;
#elif defined(PORTAUDIO)
    PaStream* audio = Q_NULLPTR;
    PaStreamParameters aParams;
    const PaDeviceInfo *info;
#else
    QAudioOutput* audioOutput=Q_NULLPTR;
    QAudioInput* audioInput=Q_NULLPTR;
    QIODevice* audioDevice=Q_NULLPTR;
    QAudioFormat     format;
    QAudioDeviceInfo deviceInfo;
#endif
    SpeexResamplerState* resampler = Q_NULLPTR;

    //r8b::CFixedBuffer<double>* resampBufs;
    //r8b::CPtrKeeper<r8b::CDSPResampler24*>* resamps;

    quint16         audioLatency;
    unsigned int             chunkSize;
    bool            chunkAvailable;

    quint32         lastSeq;
    quint32         lastSentSeq=0;
    qint64          elapsedMs = 0;
        
    quint16         nativeSampleRate=0;
    quint8          radioSampleBits;
    quint8          radioChannels;

    int             delayedPackets=0;

    QMap<quint32, audioPacket>audioBuffer;

    double resampleRatio;

    wilt::Ring<audioPacket> *ringBuf=Q_NULLPTR;

    volatile bool ready = false;
    audioPacket tempBuf;
    quint16 currentLatency;
    float amplitude;
    qreal volume=1.0;
    int devChannels;
    audioSetup setup;
    OpusEncoder* encoder=Q_NULLPTR;
    OpusDecoder* decoder=Q_NULLPTR;
};


// Various audio handling functions declared inline

static inline qint64 getAudioSize(qint64 timeInMs, const QAudioFormat& format)
{
    qint64 value = qint64(qCeil(format.channelCount() * (format.sampleSize() / 8) * format.sampleRate() / qreal(10000) * timeInMs));

    if (value % (format.channelCount() * (format.sampleSize() / 8)) != 0)
        value += (format.channelCount() * (format.sampleSize() / 8) - value % (format.channelCount() * (format.sampleSize() / 8)));

    return value;
}

static inline qint64 getAudioDuration(qint64 bytes, const QAudioFormat& format)
{
    return qint64(qFloor(bytes / (format.channelCount() * (format.sampleSize() / 8) * format.sampleRate() / qreal(1000))));
}

typedef Eigen::Matrix<quint8, Eigen::Dynamic, 1> VectorXuint8;
typedef Eigen::Matrix<qint8, Eigen::Dynamic, 1> VectorXint8;
typedef Eigen::Matrix<qint16, Eigen::Dynamic, 1> VectorXint16;
typedef Eigen::Matrix<qint32, Eigen::Dynamic, 1> VectorXint32;

static inline QByteArray samplesToInt(const QByteArray& data, const QAudioFormat& supported_format)
{
    QByteArray input = data;

    switch (supported_format.sampleSize())
    {
    case 8:
    {
        switch (supported_format.sampleType())
        {
        case QAudioFormat::UnSignedInt:
        {
            Eigen::Ref<Eigen::VectorXf> samples_float = Eigen::Map<Eigen::VectorXf>(reinterpret_cast<float*>(input.data()), input.size() / int(sizeof(float)));

            Eigen::VectorXf samples_int_tmp = samples_float * float(std::numeric_limits<quint8>::max());

            VectorXuint8 samples_int = samples_int_tmp.cast<quint8>();

            QByteArray raw = QByteArray(reinterpret_cast<char*>(samples_int.data()), int(samples_int.size()) * int(sizeof(quint8)));

            return raw;
        }
        case QAudioFormat::SignedInt:
        {
            Eigen::Ref<Eigen::VectorXf> samples_float = Eigen::Map<Eigen::VectorXf>(reinterpret_cast<float*>(input.data()), input.size() / int(sizeof(float)));

            Eigen::VectorXf samples_int_tmp = samples_float * float(std::numeric_limits<qint8>::max());

            VectorXint8 samples_int = samples_int_tmp.cast<qint8>();

            QByteArray raw = QByteArray(reinterpret_cast<char*>(samples_int.data()), int(samples_int.size()) * int(sizeof(qint8)));

            return raw;
        }
        default:
            break;
        }

        break;
    }
    case 16:
    {
        switch (supported_format.sampleType())
        {
        case QAudioFormat::SignedInt:
        {
            Eigen::Ref<Eigen::VectorXf> samples_float = Eigen::Map<Eigen::VectorXf>(reinterpret_cast<float*>(input.data()), input.size() / int(sizeof(float)));

            Eigen::VectorXf samples_int_tmp = samples_float * float(std::numeric_limits<qint16>::max());

            VectorXint16 samples_int = samples_int_tmp.cast<qint16>();

            QByteArray raw = QByteArray(reinterpret_cast<char*>(samples_int.data()), int(samples_int.size()) * int(sizeof(qint16)));

            return raw;
        }
        default:
            break;
        }

        break;
    }
    case 32:
    {
        switch (supported_format.sampleType())
        {
        case QAudioFormat::SignedInt:
        {
            Eigen::Ref<Eigen::VectorXf> samples_float = Eigen::Map<Eigen::VectorXf>(reinterpret_cast<float*>(input.data()), input.size() / int(sizeof(float)));

            Eigen::VectorXf samples_int_tmp = samples_float * float(std::numeric_limits<qint32>::max());

            VectorXint32 samples_int = samples_int_tmp.cast<qint32>();

            QByteArray raw = QByteArray(reinterpret_cast<char*>(samples_int.data()), int(samples_int.size()) * int(sizeof(qint32)));

            return raw;
        }
        default:
            break;
        }

        break;
    }
    default:
        break;
    }

    return QByteArray();
}

static inline QByteArray samplesToFloat(const QByteArray& data, const QAudioFormat& supported_format)
{
    QByteArray input = data;

    switch (supported_format.sampleSize())
    {
    case 8:
    {
        switch (supported_format.sampleType())
        {
        case QAudioFormat::UnSignedInt:
        {
            QByteArray raw = input;

            Eigen::Ref<VectorXuint8> samples_int = Eigen::Map<VectorXuint8>(reinterpret_cast<quint8*>(raw.data()), raw.size() / int(sizeof(quint8)));

            Eigen::VectorXf samples_float = samples_int.cast<float>() / float(std::numeric_limits<quint8>::max());

            return QByteArray(reinterpret_cast<char*>(samples_float.data()), int(samples_float.size()) * int(sizeof(float)));
        }
        case QAudioFormat::SignedInt:
        {
            QByteArray raw = input;

            Eigen::Ref<VectorXint8> samples_int = Eigen::Map<VectorXint8>(reinterpret_cast<qint8*>(raw.data()), raw.size() / int(sizeof(qint8)));

            Eigen::VectorXf samples_float = samples_int.cast<float>() / float(std::numeric_limits<qint8>::max());

            return QByteArray(reinterpret_cast<char*>(samples_float.data()), int(samples_float.size()) * int(sizeof(float)));
        }
        default:
            break;
        }

        break;
    }
    case 16:
    {
        switch (supported_format.sampleType())
        {
        case QAudioFormat::SignedInt:
        {
            QByteArray raw = input;

            Eigen::Ref<VectorXint16> samples_int = Eigen::Map<VectorXint16>(reinterpret_cast<qint16*>(raw.data()), raw.size() / int(sizeof(qint16)));

            Eigen::VectorXf samples_float = samples_int.cast<float>() / float(std::numeric_limits<qint16>::max());

            return QByteArray(reinterpret_cast<char*>(samples_float.data()), int(samples_float.size()) * int(sizeof(float)));
        }
        default:
            break;
        }

        break;
    }
    case 32:
    {
        switch (supported_format.sampleType())
        {
        case QAudioFormat::SignedInt:
        {
            QByteArray raw = input;

            Eigen::Ref<VectorXint32> samples_int = Eigen::Map<VectorXint32>(reinterpret_cast<qint32*>(raw.data()), raw.size() / int(sizeof(qint32)));

            Eigen::VectorXf samples_float = samples_int.cast<float>() / float(std::numeric_limits<qint32>::max());

            return QByteArray(reinterpret_cast<char*>(samples_float.data()), int(samples_float.size()) * int(sizeof(float)));
        }
        default:
            break;
        }

        break;
    }
    default:
        break;
    }

    return QByteArray();
}
#endif // AUDIOHANDLER_H
