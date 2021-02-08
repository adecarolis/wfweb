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
    void setup(const QAudioFormat format, const quint16 bufferSize);

    void incomingAudio(const QByteArray data);
    void changeBufferSize(const quint16 newSize);
    void getBufferSize();

signals:
    void audioMessage(QString message);
    void sendBufferSize(quint16 newSize);


private:
    QAudioOutput* audio;
    QAudioFormat format;
    QIODevice* device;
    int bufferSize;
    QMutex mutex;


};

#endif // RXAUDIOHANDLER_H
