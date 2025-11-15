#include "audiohandlerpaoutput.h"
#include <cstring>

audioHandlerPaOutput::audioHandlerPaOutput(QObject* parent)
    : audioHandlerBase(parent) {}

int audioHandlerPaOutput::paCallback(const void* input, void* output, unsigned long frameCount, const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags statusFlags, void* user)
{
    Q_UNUSED(input);
    static_cast<audioHandlerPaOutput*>(user)->handleCallbackOutput(output, frameCount, statusFlags);
    return paContinue;
}

void audioHandlerPaOutput::handleCallbackOutput(void* output, unsigned long frameCount, PaStreamCallbackFlags statusFlags)
{
    if (statusFlags & paOutputUnderflow) isUnderrun.store(true);
    const size_t nBytes = frameCount * bytesPerFrame;
    size_t got = outRB->pop(static_cast<char*>(output), nBytes);
    if (got < nBytes) memset(static_cast<char*>(output)+got, 0, nBytes-got);
    int ringMs = int((outRB->size()/bytesPerFrame) * 1000.0 / nativeFormat.sampleRate());
    int prev=currentLatency.load(); currentLatency.store(int(prev*0.8 + ringMs*0.2));
}

bool audioHandlerPaOutput::openDevice() noexcept
{
    if (Pa_Initialize() != paNoError) { reportError("PortAudio init failed"); return false; }
    PaStreamParameters out{};
    out.device = (setupData.portInt==paNoDevice) ? Pa_GetDefaultOutputDevice() : setupData.portInt;
    if (out.device == paNoDevice) { reportError("No PortAudio output device"); return false; }
    out.channelCount = nativeFormat.channelCount();
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    out.sampleFormat = (nativeFormat.sampleSize() == 16) ? paInt16 : paFloat32;
#else
    out.sampleFormat = (nativeFormat.sampleFormat() == QAudioFormat::Int16) ? paInt16 : paFloat32;
#endif
    bytesPerSample = (out.sampleFormat == paInt16) ? 2 : 4;
    bytesPerFrame  = bytesPerSample * nativeFormat.channelCount();

    out.suggestedLatency = setupData.latency / 1000.0; out.hostApiSpecificStreamInfo=nullptr;
    outRB = std::make_unique<ByteRing>(nativeFormat.bytesForDuration(setupData.latency*2000));
    double sr = nativeFormat.sampleRate();

    if (Pa_OpenStream(&stream, nullptr, &out, sr, paFramesPerBufferUnspecified, paNoFlag, &audioHandlerPaOutput::paCallback, this) != paNoError)
    {
        reportError("PortAudio open output failed");
        return false;
    }
    if (Pa_StartStream(stream) != paNoError)
    {
        reportError("PortAudio start output failed");
        return false;
    }

    emit setupConverter(radioFormat, codec, nativeFormat, codecType::LPCM, 7, setupData.resampleQuality);
    connect(this, &audioHandlerBase::sendToConverter, converter, &audioConverter::convert);
    connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(onConverted(audioPacket)));
    return true;
}

void audioHandlerPaOutput::closeDevice() noexcept
{
    if (stream)
    {
        Pa_StopStream(stream);
        Pa_CloseStream(stream);
        stream=nullptr;
    }
    Pa_Terminate();
    outRB.reset();
}

void audioHandlerPaOutput::incomingAudio(audioPacket packet)
{
    if (packet.data.isEmpty())
        return;
    packet.volume=volume;
    emit sendToConverter(packet);
}

void audioHandlerPaOutput::onConverted(audioPacket pkt)
{
    if (pkt.data.isEmpty()) return;
    const int age = pkt.time.msecsTo(QTime::currentTime());
    if (age > setupData.latency * 1.5)
        return;
    outRB->push(pkt.data.constData(), size_t(pkt.data.size()));
    lastReceived.restart(); amplitude.store(pkt.amplitudePeak);
    emit haveLevels(amplitudePeak(), quint16(pkt.amplitudeRMS*255.0f), setupData.latency, currentLatency.load(), isUnderrun.load(), isOverrun.load());
}

bool audioHandlerPaOutput::isFormatSupported(QAudioFormat f)
{
    bool ret=true;
    if (Pa_Initialize() != paNoError) { reportError("PortAudio init failed"); return false; }

    const PaDeviceInfo* info = Pa_GetDeviceInfo(setupData.portInt);

    // Is this format supported?
    // First look through supported channel numbers
    if (int(info->maxOutputChannels) < f.channelCount())
        ret = false;
    return ret;
}


QAudioFormat audioHandlerPaOutput::getNativeFormat()
{
    QAudioFormat native;
    if (Pa_Initialize() != paNoError) { reportError("PortAudio init failed"); return native; }

    const PaDeviceInfo* info = Pa_GetDeviceInfo(setupData.portInt);

    native.setChannelCount(info->maxOutputChannels);
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

