#ifndef RXAUDIOHANDLER_H
#define RXAUDIOHANDLER_H

#include <QObject>

#include <QtMultimedia/QAudioOutput>
#include <QMutexLocker>
#include <QIODevice>

#include <QDebug>

class rxAudioHandler : public QObject
{
    Q_OBJECT

public:
    rxAudioHandler();
    ~rxAudioHandler();


public slots:
    void process();
    void setup(const QAudioFormat format, const int bufferSize);

    void incomingAudio(const QByteArray data);
    void changeBufferSize(const int newSize);
    void getBufferSize();

signals:
    void audioMessage(QString message);
    void sendBufferSize(int newSize);
    void sendAudioBufferSize(int newSize);


private:
    QAudioOutput* audio;
    QAudioFormat format;
    QIODevice* device;
    int bufferSize;
    QMutex mutex;


};

#endif // RXAUDIOHANDLER_H
