#ifndef AUDIOCONVERTER_H
#define AUDIOCONVERTER_H
#include <QObject>
#include <QByteArray>
#include <QTime>
#include <QMap>
#include <QDebug>
#include <QAudioFormat>

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
#include <QAudioDeviceInfo>
#include <QAudioInput>
#include <QAudioOutput>
#else
#include <QMediaDevices>
#include <QAudioDevice>
#include <QAudioSource>
#include <QAudioSink>
#endif

/* Opus and Eigen */
#ifdef Q_OS_WIN
#include "opus.h"
#include <Eigen/Eigen>
#else
#include "opus/opus.h"
#include <eigen3/Eigen/Eigen>
#endif

#include "wfviewtypes.h"

#include "resampler/speex_resampler.h"

#include "packettypes.h"

struct audioPacket {
    quint32 seq;
    QTime time;
    quint16 sent;
    QByteArray data;
    quint8 guid[GUIDLEN];
    float amplitudePeak;
    float amplitudeRMS;
    qreal volume = 1.0;
};

struct audioSetup {
    audioType type;
    QString name;
    quint16 latency;
    quint8 codec;
    bool ulaw = false;
    bool isinput;
    quint32 sampleRate;
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QAudioDeviceInfo port;
#else
    QAudioDevice port;
#endif
    int portInt;
    quint8 resampleQuality;
    unsigned char localAFgain;
    quint16 blockSize = 20; // Each 'block' of audio is 20ms long by default.
    quint8 guid[GUIDLEN];
};

class audioConverter : public QObject
{
    Q_OBJECT

public:
    explicit audioConverter(QObject* parent = nullptr);;
    ~audioConverter();

public slots:
    bool init(QAudioFormat inFormat, codecType inCodec, QAudioFormat outFormat, codecType outCodec, quint8 opusComplexity, quint8 resampleQuality);
    bool convert(audioPacket audio);

signals:
    void converted(audioPacket audio);

protected:
    QAudioFormat inFormat;
    QAudioFormat outFormat;
    OpusEncoder* opusEncoder = Q_NULLPTR;
    OpusDecoder* opusDecoder = Q_NULLPTR;
    SpeexResamplerState* resampler = Q_NULLPTR;
    quint8 opusComplexity;
    quint8 resampleQuality = 4;
    double resampleRatio=1.0; // Default resample ratio is 1:1
    quint32 lastAudioSequence;
    codecType       inCodec;
    codecType       outCodec;
};


// Various audio handling functions declared inline

typedef Eigen::Matrix<quint8, Eigen::Dynamic, 1> VectorXuint8;
typedef Eigen::Matrix<qint8, Eigen::Dynamic, 1> VectorXint8;
typedef Eigen::Matrix<qint16, Eigen::Dynamic, 1> VectorXint16;
typedef Eigen::Matrix<qint32, Eigen::Dynamic, 1> VectorXint32;

static inline QAudioFormat toQAudioFormat(quint8 codec, quint32 sampleRate)
{
    QAudioFormat format;

	/*
	0x01 uLaw 1ch 8bit
	0x02 PCM 1ch 8bit
	0x04 PCM 1ch 16bit
	0x08 PCM 2ch 8bit
	0x10 PCM 2ch 16bit
	0x20 uLaw 2ch 8bit
	0x40 Opus 1ch
	0x80 Opus 2ch
	*/
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setCodec("audio/pcm");
#endif

    format.setSampleRate(sampleRate);

	if (codec == 0x01 || codec == 0x20) {
		/* Set sample to be what is expected by the encoder and the output of the decoder */
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        format.setSampleSize(16);
        format.setSampleType(QAudioFormat::SignedInt);
        format.setCodec("audio/PCMU");
#else
        format.setSampleFormat(QAudioFormat::Int16);
#endif
    }

    if (codec == 0x02 || codec == 0x08) {
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        format.setSampleSize(8);
        format.setSampleType(QAudioFormat::UnSignedInt);
#else
        format.setSampleFormat(QAudioFormat::UInt8);
#endif
    }
	if (codec == 0x08 || codec == 0x10 || codec == 0x20 || codec == 0x80) {
		format.setChannelCount(2);
    } else {
        format.setChannelCount(1);
    }

	if (codec == 0x04 || codec == 0x10) {
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        format.setSampleSize(16);
        format.setSampleType(QAudioFormat::SignedInt);
#else
        format.setSampleFormat(QAudioFormat::Int16);
#endif
	}

    if (codec == 0x40 || codec == 0x80) {
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        format.setSampleSize(32);
        format.setSampleType(QAudioFormat::Float);
        format.setCodec("audio/opus");
#else
        format.setSampleFormat(QAudioFormat::Float);
#endif
	}

	return format;
}

#endif
