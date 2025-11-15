#include "audiohandlerpainput.h"
#include <cstring>

audioHandlerPaInput::audioHandlerPaInput(QObject* parent)
    : audioHandlerBase(parent) {}

int audioHandlerPaInput::paCallback(const void* input, void* output, unsigned long frameCount, const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags statusFlags, void* user)
{
    Q_UNUSED(output);
    static_cast<audioHandlerPaInput*>(user)->handleCallbackInput(input, frameCount, statusFlags);
    return paContinue;
}

void audioHandlerPaInput::handleCallbackInput(const void* input, unsigned long frameCount, PaStreamCallbackFlags statusFlags)
{
    if (statusFlags & paInputOverflow) isOverrun.store(true);
    if (!input) return;
    const size_t nBytes = frameCount * bytesPerFrame;
    inRB->push(static_cast<const char*>(input), nBytes);
    QMetaObject::invokeMethod(this, [this]{ onReadyRead(); }, Qt::QueuedConnection);
}

bool audioHandlerPaInput::openDevice() noexcept
{
    if (Pa_Initialize() != paNoError)
    {
        reportError("PortAudio init failed");
        return false;
    }
    PaStreamParameters in{};
    in.device = (setupData.portInt==paNoDevice) ? Pa_GetDefaultInputDevice() : setupData.portInt;

    if (in.device == paNoDevice) {
        reportError("No PortAudio input device");
        return false;
    }

    in.channelCount = nativeFormat.channelCount();


#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    in.sampleFormat = (nativeFormat.sampleSize() == 16) ? paInt16 : paFloat32;
#else
    in.sampleFormat = (nativeFormat.sampleFormat() == QAudioFormat::Int16) ? paInt16 : paFloat32;
#endif
    bytesPerSample = (in.sampleFormat == paInt16) ? 2 : 4;
    bytesPerFrame  = bytesPerSample * nativeFormat.channelCount();

    in.suggestedLatency = setupData.latency / 1000.0; in.hostApiSpecificStreamInfo=nullptr;
    inRB = std::make_unique<ByteRing>(nativeFormat.bytesForDuration(setupData.latency*2000));
    double sr = nativeFormat.sampleRate();
    if (Pa_OpenStream(&stream, &in, nullptr, sr, paFramesPerBufferUnspecified, paNoFlag, &audioHandlerPaInput::paCallback, this) != paNoError)
    {
        reportError("PortAudio open input failed");
        return false;
    }

    if (Pa_StartStream(stream) != paNoError) {
        reportError("PortAudio start input failed");
        return false;
    }

    emit setupConverter(nativeFormat, codecType::LPCM, radioFormat, codec, 7, setupData.resampleQuality);
    connect(this, &audioHandlerBase::sendToConverter, converter, &audioConverter::convert);
    connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(onConverted(audioPacket)));
    return true;
}

void audioHandlerPaInput::closeDevice() noexcept
{
    if (stream)
    {
        Pa_StopStream(stream);
        Pa_CloseStream(stream);
        stream=nullptr;
    }
    Pa_Terminate();
    inRB.reset();
}

void audioHandlerPaInput::onReadyRead()
{
    QByteArray chunk;
    chunk.resize(int(inRB->size()));
    size_t got = inRB->pop(chunk.data(), size_t(chunk.size()));
    chunk.resize(int(got));
    if (!got)
        return;
    tempBuf.data += chunk;
    const int bytesPerBlock = nativeFormat.bytesForDuration(setupData.blockSize * 1000);
    while (tempBuf.data.size() >= bytesPerBlock) {
        audioPacket pkt;
        pkt.time=QTime::currentTime();
        pkt.sent=0;
        pkt.volume=volume;
        memcpy(&pkt.guid, setupData.guid, GUIDLEN);
        pkt.data = tempBuf.data.left(bytesPerBlock); tempBuf.data.remove(0, bytesPerBlock);
        emit sendToConverter(pkt);
    }
}

void audioHandlerPaInput::onConverted(audioPacket audio)
{
    if (audio.data.isEmpty()) return;
    emit haveAudioData(audio);
    if (!lastReceived.isValid()) lastReceived.start(); else lastReceived.restart();
    amplitude.store(audio.amplitudePeak);
    emit haveLevels(amplitudePeak(), audio.amplitudeRMS, setupData.latency, currentLatency.load(), isUnderrun.load(), isOverrun.load());
}



bool audioHandlerPaInput::isFormatSupported(QAudioFormat f)
{
    bool ret=true;
    if (Pa_Initialize() != paNoError) { reportError("PortAudio init failed"); return false; }
    const PaDeviceInfo* info = Pa_GetDeviceInfo(setupData.portInt);

    // Is this format supported?
    // First look through supported channel numbers
    if (int(info->maxInputChannels) < f.channelCount())
        ret = false;
    return ret;
}


QAudioFormat audioHandlerPaInput::getNativeFormat()
{
    QAudioFormat native;
    if (Pa_Initialize() != paNoError) { reportError("PortAudio init failed"); return native; }

    const PaDeviceInfo* info = Pa_GetDeviceInfo(setupData.portInt);

    native.setChannelCount(info->maxInputChannels);
    native.setSampleRate(info->defaultSampleRate);

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    native.setSampleSize(32);
    native.setSampleType(QAudioFormat::Float);
    native.setByteOrder(QAudioFormat::LittleEndian);
    native.setCodec("audio/pcm");
#else
    native.setSampleFormat(QAudioFormat::Float);
#endif
    return native;
}
