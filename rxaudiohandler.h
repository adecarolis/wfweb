#ifndef RXAUDIOHANDLER_H
#define RXAUDIOHANDLER_H

#include <QObject>

#include <QtMultimedia/QAudioOutput>
#include <QBuffer>

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

    void incomingAudio(const QByteArray data, const int size);
    void changeBufferSize(const int newSize);
    void getBufferSize();
    void getAudioBufferSize();

signals:
    void audioMessage(QString message);
    void sendBufferSize(int newSize);
    void sendAudioBufferSize(int newSize);


private:
    QBuffer* buffer;
    QAudioOutput* audio;
    QAudioFormat format;
    int bufferSize;


};

#endif // RXAUDIOHANDLER_H
