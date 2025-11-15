#include "audioconverter.h"
#include "logcategories.h"
#include "adpcm/adpcm-lib.h"

audioConverter::audioConverter(QObject* parent) : QObject(parent)
{
}

bool audioConverter::init(QAudioFormat inFormat, codecType inCodec, QAudioFormat outFormat, codecType outCodec, quint8 opusComplexity, quint8 resampleQuality)
{

    this->inFormat = inFormat;
    this->inCodec = inCodec;
    this->outFormat = outFormat;
    this->outCodec = outCodec;
    this->opusComplexity = opusComplexity;
    this->resampleQuality = resampleQuality;

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    qInfo(logAudioConverter) << "Starting audioConverter() Input:" << inFormat.channelCount() << "Channels of" << inCodec << inFormat.sampleRate() << inFormat.sampleType() << inFormat.sampleSize() <<
        "Output:" << outFormat.channelCount() << "Channels of" << outCodec << outFormat.sampleRate() << outFormat.sampleType() << outFormat.sampleSize();

    if (inFormat.byteOrder() != outFormat.byteOrder()) {
        qInfo(logAudioConverter) << "Byteorder mismatch in:" << inFormat.byteOrder() << "out:" << outFormat.byteOrder();
    }
#else
    qInfo(logAudioConverter) << "Starting audioConverter() Input:" << inFormat.channelCount() << "Channels of" << inCodec << inFormat.sampleRate() << inFormat.sampleFormat() <<
        "Output:" << outFormat.channelCount() << "Channels of" << outCodec << outFormat.sampleRate() << outFormat.sampleFormat();

#endif


    if (inCodec == OPUS)
    {
        // Create instance of opus decoder
        int opus_err = 0;
        opusDecoder = opus_decoder_create(inFormat.sampleRate(), inFormat.channelCount(), &opus_err);
        qInfo(logAudioConverter()) << "Creating opus decoder: " << opus_strerror(opus_err);
    }

    if (outCodec == OPUS)
    {
        // Create instance of opus encoder
        int opus_err = 0;
        opusEncoder = opus_encoder_create(outFormat.sampleRate(), outFormat.channelCount(), OPUS_APPLICATION_AUDIO, &opus_err);
        if (opusEncoder) {
            opus_encoder_ctl(opusEncoder, OPUS_SET_BITRATE(64000));
            opus_encoder_ctl(opusEncoder, OPUS_SET_VBR(1));
            opus_encoder_ctl(opusEncoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
            opus_encoder_ctl(opusEncoder, OPUS_SET_COMPLEXITY(opusComplexity)); // Reduce complexity to maybe lower CPU?
        }

        qInfo(logAudioConverter()) << "Creating opus encoder: " << opus_strerror(opus_err);
    }

    if (outCodec == ADPCM)
    {
        adpcmContext = adpcm_create_context(outFormat.channelCount(),outFormat.sampleRate(),1,1);
    }

    if (inFormat.sampleRate() != outFormat.sampleRate())
    {
        int resampleError = 0;
        unsigned int ratioNum;
        unsigned int ratioDen;
        // Sample rate conversion required.
        resampler = wf_resampler_init(outFormat.channelCount(), inFormat.sampleRate(), outFormat.sampleRate(), resampleQuality, &resampleError);
        wf_resampler_get_ratio(resampler, &ratioNum, &ratioDen);
        resampleRatio = static_cast<double>(ratioDen) / ratioNum;
        qInfo(logAudioConverter()) << "wf_resampler_init() returned: " << resampleError << " resampleRatio: " << resampleRatio;
    }
    return true;
}

audioConverter::~audioConverter()
{

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    qInfo(logAudioConverter) << "Closing audioConverter() Input:" << inFormat.channelCount() << "Channels of" << inCodec << inFormat.sampleRate() << inFormat.sampleType() << inFormat.sampleSize() <<
        "Output:" << outFormat.channelCount() << "Channels of" << outCodec << outFormat.sampleRate() << outFormat.sampleType() << outFormat.sampleSize();
#else
    qInfo(logAudioConverter) << "Closing audioConverter() Input:" << inFormat.channelCount() << "Channels of" << inCodec << inFormat.sampleRate() << inFormat.sampleFormat() <<
        "Output:" << outFormat.channelCount() << "Channels of" << outCodec << outFormat.sampleRate() << outFormat.sampleFormat();
#endif

    if (opusEncoder != Q_NULLPTR) {
        qInfo(logAudioConverter()) << "Destroying opus encoder";
        opus_encoder_destroy(opusEncoder);
    }

    if (opusDecoder != Q_NULLPTR) {
        qInfo(logAudioConverter()) << "Destroying opus decoder";
        opus_decoder_destroy(opusDecoder);
    }

    if (adpcmContext != Q_NULLPTR) {
        qDebug(logAudioConverter()) << "adpcm context closed";
        adpcm_free_context(adpcmContext);
    }

    if (resampler != Q_NULLPTR) {
        wf_resampler_destroy(resampler);
        resampler = Q_NULLPTR;
        qDebug(logAudioConverter()) << "Resampler closed";
    }

}

bool audioConverter::convert(audioPacket audio)
{

    // If inFormat and outFormat are identical, just emit the data back (removed as it doesn't then process amplitude)
    if (audio.data.size() > 0)
    {

        if (inCodec == OPUS)
        {
            quint8* in = (quint8*)audio.data.data();

            //Decode the frame.
            int nSamples = opus_packet_get_nb_samples(in, audio.data.size(), inFormat.sampleRate());
            if (nSamples == -1) {
                // No opus data yet?
                return false;
            }
            scratchIn.resize(nSamples * sizeof(float) * inFormat.channelCount()); // Preset the output buffer size.
            float* out = (float*)scratchIn.data();

            int ret = opus_decode_float(opusDecoder, in, audio.data.size(), out, nSamples, 0);
            if (ret != nSamples)
            {
                qDebug(logAudio()) << "opus_decode_float: returned:" << ret << "samples, expected:" << nSamples;
            }
            audio.data.swap(scratchIn); // Replace incoming data with converted.
        }
        else if (inCodec == PCMU)
        {
            // Current packet is "technically" 8bit so need to create a new buffer to hold the float value
            // Bit-exact G.711 μ-law → float32 [-1, 1]
            // audio.data holds μ-law bytes (0..255)
            scratchIn.resize(audio.data.size() * int(sizeof(float)));

            const quint8* in  = reinterpret_cast<const quint8*>(audio.data.constData());
            float* out = reinterpret_cast<float*>(scratchIn.data());

            for (int i = 0; i < audio.data.size(); ++i) {
                // μ-law bytes are bit-inverted on the wire
                const quint8 u = ~in[i];

                // Extract sign, exponent, mantissa
                const int sign = (u & 0x80) ? -1 : 1;
                const int exp  = (u >> 4) & 0x07;       // 3 bits
                const int mant = (u & 0x0F);            // 4 bits

                // Reconstruct 16-bit PCM per G.711: ((mant<<3)+0x84) << exp) - 0x84
                int t = ((mant << 3) + 0x84) << exp;    // 0x84 = 132 (bias)
                int s = (t - 0x84) * sign;              // signed 16-bit range

                // Scale to float; use 32768.0f so full-scale maps symmetrically
                out[i] = qBound(-1.0f, s / 32768.0f, 1.0f);
            }

            audio.data.swap(scratchIn); // Replace incoming data with decoded floats
        }
        else if (inCodec == ADPCM)
        {
            scratchIn.resize((int)((audio.data.length()-4) * 4));
            qint16* out = (qint16*)scratchIn.data();
            quint8* in = (quint8*)audio.data.data();
            int samples = adpcm_decode_block(out,in,audio.data.size(),1);
            if (samples != scratchIn.size()/2) {
                qInfo() << "Sample size mismatch, audio packet in:" << audio.data.length() << "out:" << scratchIn.length() << "samples:" <<samples;
            }
            audio.data.swap(scratchIn); // Replace incoming data with converted.
        }

        Eigen::VectorXf samplesF;
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        if (inFormat.sampleType() == QAudioFormat::SignedInt && inFormat.sampleSize() == 32)
#else
        if (inFormat.sampleFormat() == QAudioFormat::Int32)
#endif
        {
            const int n = audio.data.size() / int(sizeof(qint32));
            MapI32Un s32(reinterpret_cast<qint32*>(audio.data.data()), n);
            samplesF = s32.cast<float>() / float(std::numeric_limits<qint32>::max());
        }
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        else if (inFormat.sampleType() == QAudioFormat::SignedInt && inFormat.sampleSize() == 16)
#else
        else if (inFormat.sampleFormat() == QAudioFormat::Int16)
#endif
        {
            const int n = audio.data.size() / int(sizeof(qint16));
            MapI16Un s16(reinterpret_cast<qint16*>(audio.data.data()), n);
            samplesF = s16.cast<float>() / float(std::numeric_limits<qint16>::max());
        }
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        else if (inFormat.sampleType() == QAudioFormat::UnSignedInt && inFormat.sampleSize() == 8)
#else
        else if (inFormat.sampleFormat() == QAudioFormat::UInt8)
#endif
        {
            const int count = audio.data.size() / int(sizeof(quint8));
            MapU8Un u8(reinterpret_cast<quint8*>(audio.data.data()), count);
            scratchF.resize(count);
            // Convert [0..255] → [-1..1]
            scratchF = (u8.cast<float>().array() - 128.0f) / 127.0f;
            samplesF = scratchF;
        }
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        else if (inFormat.sampleType() == QAudioFormat::Float)
#else
        else if (inFormat.sampleFormat() == QAudioFormat::Float)
#endif
        {
            const int n = audio.data.size() / int(sizeof(float));
            MapFUn f(reinterpret_cast<float*>(audio.data.data()), n);
            samplesF = f;
        }
        else
        {
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
            qInfo(logAudio()) << "Unsupported Input Sample Type:" << inFormat.sampleType() << "Size:" << inFormat.sampleSize();
#else
            qInfo(logAudio()) << "Unsupported Input Sample Format:" << inFormat.sampleFormat();
#endif
        }


        //qDebug(logAudioConverter()) << "samplesF count =" << samplesF.size()
        //                            << "ch=" << outFormat.channelCount()
        //                            << "sr=" << outFormat.sampleRate();

        if (samplesF.size() > 0)

        {
            // samplesF is currently raw samples as received from the radio:
            emit floatAudio(samplesF);

            audio.amplitudePeak = samplesF.array().abs().maxCoeff();
            audio.amplitudeRMS = std::sqrt((samplesF.array() * samplesF.array()).mean());

            // Set the volume
            samplesF *= audio.volume;

            /*
                samplesF is now an Eigen Vector of the current samples in float format
                The next step is to convert to the correct number of channels in outFormat.channelCount()
            */


            if (inFormat.channelCount() == 2 && outFormat.channelCount() == 1) {
                // If we need to drop one of the audio channels, do it now
                Eigen::VectorXf samplesTemp(samplesF.size() / 2);
                samplesTemp = Eigen::Map<Eigen::VectorXf, 0, Eigen::InnerStride<2> >(samplesF.data(), samplesF.size() / 2);
                samplesF = samplesTemp;
            }
            else if (inFormat.channelCount() == 1 && outFormat.channelCount() == 2) {
                // Convert mono to stereo if required
                Eigen::VectorXf samplesTemp(samplesF.size() * 2);
                Eigen::Map<Eigen::VectorXf, 0, Eigen::InnerStride<2> >(samplesTemp.data(), samplesF.size()) = samplesF;
                Eigen::Map<Eigen::VectorXf, 0, Eigen::InnerStride<2> >(samplesTemp.data() + 1, samplesF.size()) = samplesF;
                samplesF = samplesTemp;
            }

            /*
                Next step is to resample (if needed)
            */

            if (resampler != Q_NULLPTR && resampleRatio != 1.0)
            {
                quint32 outFrames = ((samplesF.size() / outFormat.channelCount()) * resampleRatio);
                quint32 inFrames = (samplesF.size() / outFormat.channelCount());
                scratchOut.resize(outFrames * outFormat.channelCount() * sizeof(float)); // Preset the output buffer size.
                const float* in = (float*)samplesF.data();
                float* out = (float*)scratchOut.data();

                int err = 0;
                if (outFormat.channelCount() == 1) {
                    err = wf_resampler_process_float(resampler, 0, in, &inFrames, out, &outFrames);
                }
                else {
                    err = wf_resampler_process_interleaved_float(resampler, in, &inFrames, out, &outFrames);
                }

                if (err) {
                    qInfo(logAudioConverter()) << "Resampler error " << err << " inFrames:" << inFrames << " outFrames:" << outFrames;
                }
                samplesF = Eigen::Map<Eigen::VectorXf>(reinterpret_cast<float*>(scratchOut.data()), scratchOut.size() / int(sizeof(float)));
            }

            /*
                We now have samplesF which contains float samples, and can apply any encoding needed.
            */

            if (outCodec == OPUS)
            {
                float* in = (float*)samplesF.data();
                scratchOut.resize(1275); // Preset the output buffer size to MAXIMUM possible Opus frame size
                quint8* out = (quint8*)scratchOut.data();

                int nbBytes = opus_encode_float(opusEncoder, in, (samplesF.size() / outFormat.channelCount()), out, scratchOut.length());
                if (nbBytes < 0)
                {
                    qInfo(logAudioConverter()) << "Opus encode failed:" << opus_strerror(nbBytes) << "Num Samples:" << samplesF.size();
                    return false;
                }
                else {
                    scratchOut.resize(nbBytes);
                    audio.data.swap(scratchOut); // Copy output packet back to input buffer.
                    //samplesF = Eigen::Map<Eigen::VectorXf>(reinterpret_cast<float*>(outPacket.data()), outPacket.size() / int(sizeof(float)));
                }

            }
            else if (outCodec == PCMU)
            {
                float* in = reinterpret_cast<float*>(samplesF.data());
                const int n = samplesF.size();

                scratchOut.resize(n); // Qt5: one-arg resize
                quint8* dst = reinterpret_cast<quint8*>(scratchOut.data());

                // ITU G.711 μ-law constants
                constexpr int BIAS = 0x84;      // 132
                constexpr int CLIP = 32635;
                static const int seg_end[8] = { 0x1F, 0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF };

                for (int i = 0; i < n; ++i) {
                    // clamp float and convert to 16-bit linear
                    float xf = in[i];
                    if (xf >  1.0f) xf =  1.0f;
                    if (xf < -1.0f) xf = -1.0f;
                    int s = int(std::lrintf(xf * 32767.0f));
                    if (s >  32767) s =  32767;
                    if (s < -32768) s = -32768;

                    // μ-law companding (bit-exact)
                    int pcm  = s;
                    int mask;
                    if (pcm < 0) { pcm = BIAS - pcm; mask = 0x7F; }
                    else         { pcm = BIAS + pcm; mask = 0xFF; }
                    const int maxp = BIAS + CLIP;
                    if (pcm > maxp) pcm = maxp;

                    int seg = 0;
                    while (seg < 8 && pcm > seg_end[seg]) ++seg;
                    if (seg > 7) seg = 7;

                    const int mant = (pcm >> (seg + 3)) & 0x0F;
                    const quint8 u = quint8(((seg << 4) | mant) ^ mask); // invert with sign-dependent mask

                    dst[i] = u;
                }

                audio.data.swap(scratchOut);

            }
            else {


                /*
                    Now convert back into the output format required
                */
                audio.data.clear();

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
                if (outFormat.sampleType() == QAudioFormat::UnSignedInt && outFormat.sampleSize() == 8)
#else
                if (outFormat.sampleFormat() == QAudioFormat::UInt8)
#endif
                {
                    const Eigen::VectorXf f = samplesF.array().max(-1.0f).min(1.0f);
                    Eigen::VectorXf scaled = (f.array() * 127.0f) + 128.0f;
                    VectorXuint8 u8 = scaled.array().round().cast<quint8>();
                    scratchOut.resize(int(u8.size() * sizeof(quint8)));
                    std::memcpy(scratchOut.data(), u8.data(), size_t(u8.size())); // bytes == elements
                    audio.data.swap(scratchOut);  // O(1) swap; scratchOut now holds old audio.data
                }
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
                else if (outFormat.sampleType() == QAudioFormat::SignedInt && outFormat.sampleSize() == 16)
#else
                else if (outFormat.sampleFormat() == QAudioFormat::Int16)
#endif
                {
                    const Eigen::VectorXf f = samplesF.array().max(-1.0f).min(1.0f);
                    VectorXint16 s16 = (f * float(std::numeric_limits<qint16>::max())).array().round().cast<qint16>();
                    scratchOut.resize(int(s16.size() * sizeof(qint16)));
                    std::memcpy(scratchOut.data(), s16.data(), size_t(s16.size()) * sizeof(qint16));
                    audio.data.swap(scratchOut);

                }
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
                else if (outFormat.sampleType() == QAudioFormat::SignedInt && outFormat.sampleSize() == 32)
#else
                else if (outFormat.sampleFormat() == QAudioFormat::Int32)
#endif
                {
                    const Eigen::VectorXf f = samplesF.array().max(-1.0f).min(1.0f);
                    VectorXint32 s32 = (f * float(std::numeric_limits<qint32>::max())).array().round().cast<qint32>();
                    scratchOut.resize(int(s32.size() * sizeof(qint32)));
                    std::memcpy(scratchOut.data(), s32.data(), size_t(s32.size()) * sizeof(qint32));
                    audio.data.swap(scratchOut);
                }
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
                else if (outFormat.sampleType() == QAudioFormat::Float)
#else
                else if (outFormat.sampleFormat() == QAudioFormat::Float)
#endif
                {
                    audio.data = QByteArray(reinterpret_cast<char*>(samplesF.data()), int(samplesF.size()) * int(sizeof(float)));
                }
                else {
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
                    qInfo(logAudio()) << "Unsupported Output Sample Type:" << outFormat.sampleType() << "Size:" << outFormat.sampleSize();
#else
                    qInfo(logAudio()) << "Unsupported Output Sample Type:" << outFormat.sampleFormat();
#endif
                }

                /*
                    As we currently don't have a float based adpcm encoder, this must be done
                    after all other conversion has taken place.
                */

                if (outCodec == ADPCM)
                {
                    // Clamp float to [-1,1] and convert to S16
                    const int ch = outFormat.channelCount();
                    const Eigen::VectorXf f = samplesF.array().max(-1.0f).min(1.0f);
                    VectorXint16 s16 = (f * float(std::numeric_limits<qint16>::max()))
                                           .array().round().cast<qint16>();

                    scratchOut.resize((int)(s16.size() * sizeof(qint16) / 4) + 4); // ~4:1 + hdr
                    qint16* in  = const_cast<qint16*>(s16.data());
                    quint8* out = reinterpret_cast<quint8*>(scratchOut.data());
                    size_t outSize = 0;
                    adpcm_encode_block(adpcmContext, out, &outSize, in, s16.size());
                    scratchOut.resize(int(outSize));
                    audio.data.swap(scratchOut);
                }
            }
        }
        else
        {
            qDebug(logAudioConverter) << "Detected empty packet";
        }
    }
    //qDebug(logAudioConverter()) << "encoded bytes =" << audio.data.size()
    //                            << "codec=" << int(outCodec);
    emit converted(audio);
    return true;
}

