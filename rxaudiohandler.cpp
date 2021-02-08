#include "rxaudiohandler.h"

rxAudioHandler::rxAudioHandler()
{

}

rxAudioHandler::~rxAudioHandler()
{
    audio->stop();
    delete audio;
}

void rxAudioHandler::process()
{
    qDebug() << "rxAudio Handler created.";
}

void rxAudioHandler::setup(const QAudioFormat format, const quint16 bufferSize)
{
    this->format  = format;
    this->bufferSize = bufferSize;
    audio = new QAudioOutput(format);
    audio->setBufferSize(bufferSize);
    device = audio->start();
}


void rxAudioHandler::incomingAudio(const QByteArray data)
{
    QMutexLocker locker(&mutex);
    device->write(data,data.length());
}

void rxAudioHandler::changeBufferSize(const quint16 newSize)
{
    QMutexLocker locker(&mutex);
    qDebug() << "Changing buffer size to: " << newSize << " from " << audio->bufferSize();
    audio->stop();
    audio->setBufferSize(newSize);
    device = audio->start();
}

void rxAudioHandler::getBufferSize()
{
    emit sendBufferSize(audio->bufferSize());
}

