#include "audiohandlerrtoutput.h"
#include <cstring>

audioHandlerRtOutput::audioHandlerRtOutput(QObject* parent)
    : audioHandlerBase(parent) {}

int audioHandlerRtOutput::rtCallback(void* out, void* in, unsigned nFrames, double, RtAudioStreamStatus st, void* u)
{ Q_UNUSED(in); static_cast<audioHandlerRtOutput*>(u)->handleCallbackOutput(out, nFrames, st); return 0; }


void audioHandlerRtOutput::handleCallbackOutput(void* out, unsigned nFrames, RtAudioStreamStatus st)
{
    if (st & RTAUDIO_OUTPUT_UNDERFLOW) isUnderrun.store(true);
    const size_t nBytes = nFrames * bytesPerFrame;
    size_t got = outRB->pop(static_cast<char*>(out), nBytes);
    if (got < nBytes) memset(static_cast<char*>(out)+got, 0, nBytes-got);
    int ringMs = int((outRB->size()/bytesPerFrame) * 1000.0 / nativeFormat.sampleRate());
    int prev=currentLatency.load(); currentLatency.store(int(prev*0.8 + ringMs*0.2));
}

bool audioHandlerRtOutput::openDevice() noexcept
{
    qDebug(logAudio()) << "Creating output RT Audio device:" << setupData.name;

#ifdef RT_EXCEPTION
    try {
        if (!rtaudio.getDeviceCount()) return false;
        RtAudio::StreamParameters outParams; outParams.deviceId=setupData.portInt; outParams.nChannels=nativeFormat.channelCount(); outParams.firstChannel=0;
        RtAudio::StreamOptions opts; opts.streamName = "audioHandlerRtOutput";
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        fmt = (nativeFormat.sampleSize() == 16) ? RTAUDIO_SINT16 : RTAUDIO_FLOAT32;
#else
        fmt = (nativeFormat.sampleFormat()==QAudioFormat::Int16) ? RTAUDIO_SINT16 : RTAUDIO_FLOAT32;
#endif
        bytesPerSample = (fmt==RTAUDIO_SINT16?2:4);
        bytesPerFrame = bytesPerSample * nativeFormat.channelCount();
        const unsigned sr = nativeFormat.sampleRate();
        unsigned frames = qMax(64u, static_cast<unsigned>((setupData.latency * sr)/1000));
        outRB = std::make_unique<ByteRingRt>(nativeFormat.bytesForDuration(setupData.latency*2000));


        rtaudio.openStream(&outParams, nullptr, fmt, sr, &frames, &audioHandlerRtOutput::rtCallback, this, &opts);
        rtaudio.startStream();


        emit setupConverter(radioFormat, codec, nativeFormat, codecType::LPCM, 7, setupData.resampleQuality);
        connect(this, &audioHandlerBase::sendToConverter, converter, &audioConverter::convert);
        connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(onConverted(audioPacket)));
        return true;
    } catch (RtAudioError& e) { reportError(QString::fromStdString(e.getMessage())); return false; }
#else
    if (!rtaudio.getDeviceCount()) return false;

#if RTAUDIO_VERSION_MAJOR >= 6
    rtaudio.setErrorCallback([](RtAudioErrorType t, const std::string& m){
        qWarning() << "RtAudio error" << int(t) << ":" << QString::fromStdString(m);
    });
#endif
    RtAudio::StreamParameters outParams; outParams.deviceId=setupData.portInt; outParams.nChannels=nativeFormat.channelCount(); outParams.firstChannel=0;
    RtAudio::StreamOptions opts; opts.streamName = "audioHandlerRtOutput";
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    bool useInt16 = (nativeFormat.sampleSize() == 16);
#else
    bool useInt16 = (nativeFormat.sampleFormat() == QAudioFormat::Int16);
#endif
    fmt = useInt16 ? RTAUDIO_SINT16 : RTAUDIO_FLOAT32;
    bytesPerSample = useInt16 ? 2 : 4;
    bytesPerFrame  = bytesPerSample * nativeFormat.channelCount();
    const unsigned sr = nativeFormat.sampleRate();
    unsigned frames = qMax(64u, static_cast<unsigned>((setupData.latency * sr)/1000));
    outRB = std::make_unique<ByteRing>(nativeFormat.bytesForDuration(setupData.latency*2000));

    rtaudio.openStream(&outParams, nullptr, fmt, sr, &frames, &audioHandlerRtOutput::rtCallback, this, &opts);
    if (rtaudio.isStreamOpen()) rtaudio.startStream();
    if (!rtaudio.isStreamRunning()) { reportError("RtAudio output stream failed to start"); return false; }

    emit setupConverter(radioFormat, codec, nativeFormat, codecType::LPCM, 7, setupData.resampleQuality);
    connect(this, &audioHandlerBase::sendToConverter, converter, &audioConverter::convert);
    connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(onConverted(audioPacket)));
    return true;
#endif
}



void audioHandlerRtOutput::closeDevice() noexcept
{
#ifdef RT_EXCEPTION
    try {
        if (rtaudio.isStreamOpen())
        {
            if (rtaudio.isStreamRunning())
            {
                rtaudio.stopStream();
            }
            rtaudio.closeStream();
        }
    }
    catch(...) {}
#else
    if (rtaudio.isStreamOpen()) {
        if (rtaudio.isStreamRunning())
        {
            rtaudio.stopStream();
        }
        rtaudio.closeStream();
    }
#endif
    outRB.reset();
}


void audioHandlerRtOutput::incomingAudio(audioPacket packet)
{ if (packet.data.isEmpty()) return; packet.volume = volume; emit sendToConverter(packet); }


void audioHandlerRtOutput::onConverted(audioPacket pkt)
{
    if (pkt.data.isEmpty()) return;
    const int age = pkt.time.msecsTo(QTime::currentTime()); if (age > setupData.latency) return;
    outRB->push(pkt.data.constData(), size_t(pkt.data.size()));
    lastReceived.restart(); amplitude.store(pkt.amplitudePeak);
    emit haveLevels(amplitudePeak(), quint16(pkt.amplitudeRMS*255.0f), setupData.latency, currentLatency.load(), isUnderrun.load(), isOverrun.load());
}

bool audioHandlerRtOutput::isFormatSupported(QAudioFormat f)
{
    bool ret=true;
#ifdef RT_EXCEPTION
    try {
        RtAudio::DeviceInfo info = rtaudio.getDeviceInfo(setupData.portInt);
    }
    catch (RtAudioError e) {
        qInfo(logAudio()) << "Device exception:" << aParams.deviceId << ":" << QString::fromStdString(e.getMessage());
        return false;
    }
    if (info.probed)
    {
#else
    RtAudio::DeviceInfo info = rtaudio.getDeviceInfo(setupData.portInt);
#endif

        // Is this format supported?
        // First look through supported channel numbers
        if (int(info.outputChannels) < f.channelCount())
            ret = false;

        // Now search sample rates:
        if (std::find(info.sampleRates.begin(), info.sampleRates.end(), f.sampleRate()) == info.sampleRates.end())
            ret = false;

        // Now search output format:

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        if (!(f.sampleType() == QAudioFormat::Float && info.nativeFormats & RTAUDIO_FLOAT32) &&
            !(f.sampleType() == QAudioFormat::SignedInt && f.sampleSize() == 32 && info.nativeFormats & RTAUDIO_SINT32) &&
            !(f.sampleType() == QAudioFormat::SignedInt && f.sampleSize() == 16 && info.nativeFormats & RTAUDIO_SINT16))
#else
    if (!(f.sampleFormat() == QAudioFormat::Float && info.nativeFormats & RTAUDIO_FLOAT32) &&
        !(f.sampleFormat() == QAudioFormat::Int32 && info.nativeFormats & RTAUDIO_SINT32) &&
        !(f.sampleFormat() == QAudioFormat::Int16 && info.nativeFormats & RTAUDIO_SINT16))
#endif
        {
            ret = false;
        }
#ifdef RT_EXCEPTION
    }
#endif
    return ret;
}


QAudioFormat audioHandlerRtOutput::getNativeFormat()
    {

        QAudioFormat native;
#ifdef RT_EXCEPTION
        try {
            RtAudio::DeviceInfo info = rtaudio.getDeviceInfo(setupData.portInt);
        }
        catch (RtAudioError e) {
            qInfo(logAudio()) << "Device exception:" << setupData.portInt << ":" << QString::fromStdString(e.getMessage());
            return false;
        }
        if (info.probed)
        {
#else
    RtAudio::DeviceInfo info = rtaudio.getDeviceInfo(setupData.portInt);
#endif

            qDebug(logAudio()) << QString::fromStdString(info.name) << "(" << setupData.portInt << ") successfully probed";

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
            native.setByteOrder(QAudioFormat::LittleEndian);
            native.setCodec("audio/pcm");
#endif

            if (info.nativeFormats == 0)
            {
                qCritical(logAudio()) << "rtaudio output, no natively supported data formats!";
                return QAudioFormat();
            }

            qDebug(logAudio()) << "rtaudio output channels:" << info.outputChannels << " preferred sample rate:" << info.preferredSampleRate;
            native.setChannelCount(info.outputChannels);
            native.setSampleRate(info.preferredSampleRate);

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
            if (info.nativeFormats & RTAUDIO_FLOAT32) {
                native.setSampleType(QAudioFormat::Float);
                native.setSampleSize(32);
            }
            else if (info.nativeFormats & RTAUDIO_SINT32)
            {
                native.setSampleType(QAudioFormat::SignedInt);
                native.setSampleSize(32);
            }
            else if (info.nativeFormats & RTAUDIO_SINT16)
            {
                native.setSampleType(QAudioFormat::SignedInt);
                native.setSampleSize(16);
            }
            else if (info.nativeFormats & RTAUDIO_SINT24)
            {
                native.setSampleType(QAudioFormat::SignedInt);
                native.setSampleSize(24);
            }
#else
        if (info.nativeFormats & RTAUDIO_FLOAT32) {
            native.setSampleFormat(QAudioFormat::Float);
        }
        else if (info.nativeFormats & RTAUDIO_SINT32)
        {
            native.setSampleFormat(QAudioFormat::Int32);
        }
        else if (info.nativeFormats & RTAUDIO_SINT16)
        {
            native.setSampleFormat(QAudioFormat::Int16);
        }
        else if (info.nativeFormats & RTAUDIO_SINT24)
        {
            native.setSampleFormat(QAudioFormat::Unknown);
        }
#endif
        else {
            qCritical(logAudio()) << "Cannot find default sample format!";
            return QAudioFormat();
        }

#ifdef RT_EXCEPTION
        }
#endif

    return native;
}
