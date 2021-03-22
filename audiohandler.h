#ifndef AUDIOHANDLER_H
#define AUDIOHANDLER_H

#include <QObject>

#include <QtMultimedia/QAudioOutput>
#include <QMutexLocker>
#include <QByteArray>
#include <QtEndian>
#include <QAudioFormat>
#include <QAudioDeviceInfo>
#include <QAudioOutput>
#include <QAudioInput>
#include <QIODevice>
#include <QThread>
#include <QTimer>
#include <QTime>
#include "resampler/speex_resampler.h"

#include <QDebug>

//#define BUFFER_SIZE (32*1024)

#define INTERNAL_SAMPLE_RATE 48000

struct audioPacket {
    quint32 seq;
    QTime time;
    quint16 sent;
    QByteArray datain;
    QByteArray dataout;
};


class audioHandler : public QIODevice
{
    Q_OBJECT


public:
    audioHandler(QObject* parent = 0);
    ~audioHandler();

    void getLatency();

    bool setDevice(QAudioDeviceInfo deviceInfo);

    void start();
    void setVolume(quint8 volume);
    void flush();
    void stop();

    qint64 readData(char* data, qint64 maxlen);
    qint64 writeData(const char* data, qint64 len);
    qint64 bytesAvailable() const;
    bool isSequential() const;
    void getNextAudioChunk(QByteArray &data);
    bool isChunkAvailable();
public slots:
    bool init(const quint8 bits, const quint8 channels, const quint16 samplerate, const quint16 latency, const bool isulaw, const bool isinput, QString port, quint8 resampleQuality);
    void incomingAudio(const audioPacket data);
    void changeLatency(const quint16 newSize);

private slots:
    void notified();
    void stateChanged(QAudio::State state);

signals:
    void audioMessage(QString message);
    void sendLatency(quint16 newSize);
    void haveAudioData(const QByteArray& data);


private:
    void reinit();

    QMutex          mutex;

    bool            isInitialized;
    QAudioOutput*   audioOutput;
    QAudioInput*    audioInput;
    bool            isUlaw;
    quint16         latency;
    bool            isInput;   // Used to determine whether input or output audio
    int             chunkSize;
    bool            chunkAvailable;

    quint32        lastSeq;

    QAudioFormat     format;
    QAudioDeviceInfo deviceInfo;
    quint16          radioSampleRate;
    quint8           radioSampleBits;
    quint8          radioChannels;
    QVector<audioPacket> audioBuffer;

    SpeexResamplerState* resampler;
    unsigned int ratioNum;
    unsigned int ratioDen;
};

#endif // AUDIOHANDLER_H
