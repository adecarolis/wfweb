#ifndef AUDIOHANDLER_H
#define AUDIOHANDLER_H

/* QT Headers */
#include <QObject>
#include <QByteArray>
#include <QMutex>
#include <QtEndian>
#include <QtMath>
#include <QThread>
#include <QTimer>
#include <QTime>
#include <QMap>
#include <QDebug>
#include <QDateTime>

/* QT Audio Headers */
#include <QAudioOutput>
#include <QAudioFormat>
#include <QAudioDeviceInfo>
#include <QAudioInput>
#include <QIODevice>

/* Current resampler code */
#include "resampler/speex_resampler.h"

/* Potential new resampler */
//#include <r8bbase.h>
//#include <CDSPResampler.h>


/* Opus */
#ifdef Q_OS_WIN
#include "opus.h"
#else
#include "opus/opus.h"
#endif

/* Eigen */
#ifndef Q_OS_WIN
#include <eigen3/Eigen/Eigen>
#else
#include <Eigen/Eigen>
#endif

/* wfview Packet types */
#include "packettypes.h"

/* Logarithmic taper for volume control */
#include "audiotaper.h"

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
    quint16 latency;
    quint8 codec;
    bool ulaw = false;
    bool isinput;
    QAudioFormat format; // Use this for all audio APIs
    QAudioDeviceInfo port;
    quint8 resampleQuality;
    unsigned char localAFgain;
    quint16 blockSize=20; // Each 'block' of audio is 20ms long by default.
    quint8 guid[GUIDLEN];
};

// For QtMultimedia, use a native QIODevice
class audioHandler : public QObject
{
    Q_OBJECT

public:
    audioHandler(QObject* parent = 0);
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

private slots:
    void stateChanged(QAudio::State state);
    void clearUnderrun();    
    void getNextAudioChunk();

signals:
    void audioMessage(QString message);
    void sendLatency(quint16 newSize);
    void haveAudioData(const audioPacket& data);
    void haveLevels(quint16 amplitude,quint16 latency,quint16 current,bool under);
private:

    bool            isUnderrun = false;
    bool            isInitialized=false;
    bool            isReady = false;
    bool            audioBuffered = false;
    QTime       lastReceived;
    QAudioOutput* audioOutput=Q_NULLPTR;
    QAudioInput* audioInput=Q_NULLPTR;
    QIODevice* audioDevice=Q_NULLPTR;
    QTimer* audioTimer = Q_NULLPTR;
    QAudioFormat     format;
    QAudioDeviceInfo deviceInfo;
    SpeexResamplerState* resampler = Q_NULLPTR;

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
    float amplitude;
    float volume=1.0;

    audioSetup setup;

    OpusEncoder* encoder=Q_NULLPTR;
    OpusDecoder* decoder=Q_NULLPTR;
    QTimer* underTimer;
};


// Various audio handling functions declared inline

typedef Eigen::Matrix<quint8, Eigen::Dynamic, 1> VectorXuint8;
typedef Eigen::Matrix<qint8, Eigen::Dynamic, 1> VectorXint8;
typedef Eigen::Matrix<qint16, Eigen::Dynamic, 1> VectorXint16;
typedef Eigen::Matrix<qint32, Eigen::Dynamic, 1> VectorXint32;

/*
static inline qint64 getAudioSize(qint64 timeInMs, const QAudioFormat& format)
{
#ifdef Q_OS_LINUX
    qint64 value = qint64(qCeil(format.channelCount() * (format.sampleSize() / 8) * format.sampleRate() / qreal(1000) * timeInMs));
#else
    qint64 value = qint64(qCeil(format.channelCount() * (format.sampleSize() / 8) * format.sampleRate() / qreal(10000) * timeInMs));
#endif


    if (value % (format.channelCount() * (format.sampleSize() / 8)) != 0)
        value += (format.channelCount() * (format.sampleSize() / 8) - value % (format.channelCount() * (format.sampleSize() / 8)));

    return value;
}

static inline qint64 getAudioDuration(qint64 bytes, const QAudioFormat& format)
{
    return qint64(qFloor(bytes / (format.channelCount() * (format.sampleSize() / 8) * format.sampleRate() / qreal(1000))));
}

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
*/
#endif // AUDIOHANDLER_H
