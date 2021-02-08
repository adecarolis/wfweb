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

void rxAudioHandler::setup(const QAudioFormat format, const int bufferSize)
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

void rxAudioHandler::changeBufferSize(const int newSize)
{
    // TODO: make a way to change the buffer size.
    // possibly deleting the buffer and re-creating

    audio->setBufferSize(newSize);
}

void rxAudioHandler::getBufferSize()
{
    emit sendBufferSize(audio->bufferSize());
}

