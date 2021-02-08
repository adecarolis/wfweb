#include "rxaudiohandler.h"

rxAudioHandler::rxAudioHandler()
{

}

rxAudioHandler::~rxAudioHandler()
{
    audio->stop();
    delete audio;
    delete buffer;
}

void rxAudioHandler::process()
{
    qDebug() << "rxAudio Handler created.";
}

void rxAudioHandler::setup(const QAudioFormat format, const int bufferSize)
{
    this->format  = format;
    this->bufferSize = bufferSize;
    buffer = new QBuffer();
    buffer->open(QIODevice::ReadWrite);
    audio = new QAudioOutput(format);
    audio->setBufferSize(bufferSize);
    buffer->seek(0);
    audio->start(buffer);
}


void rxAudioHandler::incomingAudio(const QByteArray data, const int size)
{
    buffer->buffer().remove(0,buffer->pos());
    buffer->seek(buffer->size());

    buffer->write(data.constData(), size);
    buffer->seek(0);
}

void rxAudioHandler::changeBufferSize(const int newSize)
{
    // TODO: make a way to change the buffer size.
    // possibly deleting the buffer and re-creating

    audio->setBufferSize(newSize);
}

void rxAudioHandler::getBufferSize()
{
    emit sendBufferSize(buffer->size());
}


void rxAudioHandler::getAudioBufferSize()
{
    emit sendAudioBufferSize(audio->bufferSize());
}
