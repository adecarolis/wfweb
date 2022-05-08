#ifndef AUDIOCONVERTER_H
#define AUDIOCONVERTER_H
#include <QObject>
#include <QByteArray>
#include <QTime>
#include <QMap>
#include <QDebug>
#include <QAudioFormat>

/* Opus and Eigen */
#ifdef Q_OS_WIN
#include "opus.h"
#include <Eigen/Eigen>
#else
#include "opus/opus.h"
#include <eigen3/Eigen/Eigen>
#endif

#include "resampler/speex_resampler.h"

#include "packettypes.h"

struct audioPacket {
    quint32 seq;
    QTime time;
    quint16 sent;
    QByteArray data;
    quint8 guid[GUIDLEN];
    float amplitude;
    qreal volume = 1.0;
};

class audioConverter : public QObject
{
    Q_OBJECT

public:
    audioConverter();
    ~audioConverter();

public slots:
    bool init(QAudioFormat inFormat, QAudioFormat outFormat, quint8 opusComplexity, quint8 resampleQuality);
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
};


// Various audio handling functions declared inline

typedef Eigen::Matrix<quint8, Eigen::Dynamic, 1> VectorXuint8;
typedef Eigen::Matrix<qint8, Eigen::Dynamic, 1> VectorXint8;
typedef Eigen::Matrix<qint16, Eigen::Dynamic, 1> VectorXint16;
typedef Eigen::Matrix<qint32, Eigen::Dynamic, 1> VectorXint32;

static QAudioFormat toQAudioFormat(quint8 codec, quint32 sampleRate)
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

	format.setChannelCount(1);
	format.setSampleSize(8);
	format.setSampleType(QAudioFormat::UnSignedInt);
	format.setByteOrder(QAudioFormat::LittleEndian);
	format.setCodec("audio/pcm");

	if (codec == 0x01 || codec == 0x20) {
		/* Set sample to be what is expected by the encoder and the output of the decoder */
		format.setSampleSize(16);
		format.setSampleType(QAudioFormat::SignedInt);
		format.setCodec("audio/PCMU");
	}

	if (codec == 0x08 || codec == 0x10 || codec == 0x20 || codec == 0x80) {
		format.setChannelCount(2);
	}

	if (codec == 0x04 || codec == 0x10) {
		format.setSampleSize(16);
		format.setSampleType(QAudioFormat::SignedInt);
	}

	if (codec == 0x40 || codec == 0x80) {
		format.setSampleSize(32);
		format.setSampleType(QAudioFormat::Float);
		format.setCodec("audio/opus");
	}

	format.setSampleRate(sampleRate);
	return format;
}

#endif