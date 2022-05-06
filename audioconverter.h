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

#endif