#ifndef PAHANDLER_H
#define PAHANDLER_H

#include <QObject>
#include <QByteArray>
#include <QThread>

#include "portaudio.h"

#include <QAudioFormat>
#include <QTime>
#include <QMap>


/* wfview Packet types */
#include "packettypes.h"

/* Logarithmic taper for volume control */
#include "audiotaper.h"

#include "audiohandler.h"

/* Audio converter class*/
#include "audioconverter.h"

#include <QDebug>


class paHandler : public audioHandler
{
    Q_OBJECT

public:
    paHandler(QObject* parent = 0);
    ~paHandler();

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

    int writeData(const void* inputBuffer, void* outputBuffer,
        unsigned long nFrames,
        const PaStreamCallbackTimeInfo* streamTime,
        PaStreamCallbackFlags status);
    static int staticWrite(const void* inputBuffer, void* outputBuffer, unsigned long nFrames, const PaStreamCallbackTimeInfo* streamTime, PaStreamCallbackFlags status, void* userData) {
        return ((paHandler*)userData)->writeData(inputBuffer, outputBuffer, nFrames, streamTime, status);
    }

    bool            isInitialized = false;
    PaStream* audio = Q_NULLPTR;
    PaStreamParameters aParams;
    const PaDeviceInfo* info;

    quint16         audioLatency;
    unsigned int    chunkSize;

    quint32         lastSeq;
    quint32         lastSentSeq = 0;

    quint16 currentLatency;
    float amplitude=0.0;
    qreal volume = 1.0;

    audioSetup setup;
    QAudioFormat     radioFormat;
    QAudioFormat     nativeFormat;
    audioConverter* converter = Q_NULLPTR;
    QThread* converterThread = Q_NULLPTR;
    bool            isUnderrun = false;
    bool            isOverrun = false;
    int latencyAllowance = 0;
};

#endif // PAHANDLER_H
