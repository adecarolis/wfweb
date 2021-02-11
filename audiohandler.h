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

#include <QDebug>

//#define BUFFER_SIZE (32*1024)

class audioHandler : public QIODevice
{
    Q_OBJECT

public:
    audioHandler(QObject* parent = 0);
    ~audioHandler();

    void getBufferSize();

    bool setDevice(QAudioDeviceInfo deviceInfo);

    void start();
    void setVolume(float volume);
    void flush();
    void stop();

    qint64 readData(char* data, qint64 maxlen);
    qint64 writeData(const char* data, qint64 len);
    qint64 bytesAvailable() const;
    bool isSequential() const;

public slots:
    bool init(const quint8 bits, const quint8 channels, const quint16 samplerate, const quint16 bufferSize, const bool isulaw, const bool isinput);
    void incomingAudio(const QByteArray& data);
    void changeBufferSize(const quint16 newSize);

private slots:
    void notified();
    void stateChanged(QAudio::State state);

signals:
    void audioMessage(QString message);
    void sendBufferSize(quint16 newSize);
    void haveAudioData(const QByteArray& data);

private:
    void reinit();

    int             bufferSize;
    QMutex          mutex;
    bool            isUlaw;
    bool            isInput;   // Used to determine whether input or output audio

    bool             isInitialized;
    QByteArray       buffer;
    QAudioFormat     format;
    QAudioDeviceInfo deviceInfo;
    QAudioOutput*    audioOutput = Q_NULLPTR;
    QAudioInput*    audioInput = Q_NULLPTR;
    float            volume;

};

#endif // AUDIOHANDLER_H
