#include "audioconverter.h"
#include "logcategories.h"
#include "ulaw.h"

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
		//opus_encoder_ctl(opusEncoder, OPUS_SET_LSB_DEPTH(16));
		//opus_encoder_ctl(opusEncoder, OPUS_SET_INBAND_FEC(1));
		//opus_encoder_ctl(opusEncoder, OPUS_SET_DTX(1));
		//opus_encoder_ctl(opusEncoder, OPUS_SET_PACKET_LOSS_PERC(5));
		opus_encoder_ctl(opusEncoder, OPUS_SET_COMPLEXITY(opusComplexity)); // Reduce complexity to maybe lower CPU?
		qInfo(logAudioConverter()) << "Creating opus encoder: " << opus_strerror(opus_err);
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

	if (resampler != Q_NULLPTR) {
		speex_resampler_destroy(resampler);
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
            unsigned char* in = (unsigned char*)audio.data.data();

            //Decode the frame.
            int nSamples = opus_packet_get_nb_samples(in, audio.data.size(), inFormat.sampleRate());
            if (nSamples == -1) {
                // No opus data yet?
                return false;
            }
            QByteArray outPacket(nSamples * sizeof(float) * inFormat.channelCount(), (char)0xff); // Preset the output buffer size.
            float* out = (float*)outPacket.data();

            int ret = opus_decode_float(opusDecoder, in, audio.data.size(), out, nSamples, 0);
            if (ret != nSamples)
            {
                qDebug(logAudio()) << "opus_decode_float: returned:" << ret << "samples, expected:" << nSamples;
            }
            audio.data.clear();
            audio.data = outPacket; // Replace incoming data with converted.
        }
        else if (inCodec == PCMU)
        {
            // Current packet is "technically" 8bit so need to create a new buffer that is 16bit
            QByteArray outPacket((int)audio.data.length() * 2, (char)0xff);
            qint16* out = (qint16*)outPacket.data();
            for (int f = 0; f < audio.data.length(); f++)
            {
                *out++ = ulaw_decode[(quint8)audio.data[f]];
            }
            audio.data.clear();
            audio.data = outPacket; // Replace incoming data with converted.
            // Make sure that sample size/type is set correctly
        }

        Eigen::VectorXf samplesF;
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        if (inFormat.sampleType() == QAudioFormat::SignedInt && inFormat.sampleSize() == 32)
#else
        if (inFormat.sampleFormat() == QAudioFormat::Int32)
#endif
        {
            Eigen::Ref<VectorXint32> samplesI = Eigen::Map<VectorXint32>(reinterpret_cast<qint32*>(audio.data.data()), audio.data.size() / int(sizeof(qint32)));
            samplesF = samplesI.cast<float>() / float(std::numeric_limits<qint32>::max());
        }
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        else if (inFormat.sampleType() == QAudioFormat::SignedInt && inFormat.sampleSize() == 16)
#else
        else if (inFormat.sampleFormat() == QAudioFormat::Int16)
#endif
        {
            Eigen::Ref<VectorXint16> samplesI = Eigen::Map<VectorXint16>(reinterpret_cast<qint16*>(audio.data.data()), audio.data.size() / int(sizeof(qint16)));
            samplesF = samplesI.cast<float>() / float(std::numeric_limits<qint16>::max());
        }
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        else if (inFormat.sampleType() == QAudioFormat::UnSignedInt && inFormat.sampleSize() == 8)
#else
        else if (inFormat.sampleFormat() == QAudioFormat::UInt8)
#endif
        {
            Eigen::Ref<VectorXuint8> samplesI = Eigen::Map<VectorXuint8>(reinterpret_cast<quint8*>(audio.data.data()), audio.data.size() / int(sizeof(quint8)));
            samplesF = samplesI.cast<float>() / float(std::numeric_limits<quint8>::max());;
        }
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        else if (inFormat.sampleType() == QAudioFormat::Float) 
#else
        else if (inFormat.sampleFormat() == QAudioFormat::Float)
#endif
        { 

            samplesF = Eigen::Map<Eigen::VectorXf>(reinterpret_cast<float*>(audio.data.data()), audio.data.size() / int(sizeof(float)));
        }
        else
        {
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        qInfo(logAudio()) << "Unsupported Input Sample Type:" << inFormat.sampleType() << "Size:" << inFormat.sampleSize();
#else
        qInfo(logAudio()) << "Unsupported Input Sample Format:" << inFormat.sampleFormat();
#endif
        }

        if (samplesF.size() > 0)

        {
            audio.amplitudePeak = samplesF.array().abs().maxCoeff();
            //audio.amplitudeRMS = samplesF.array().abs().mean(); // zero for tx audio
            //audio.amplitudeRMS = samplesF.norm() / sqrt(samplesF.size()); // too high values. Zero for tx audio.
            //audio.amplitudeRMS = samplesF.squaredNorm(); // tx not zero. Values higher than peak sometimes
            //audio.amplitudeRMS = samplesF.norm(); // too small values. also too small on TX
            //audio.amplitudeRMS = samplesF.blueNorm(); // scale same as norm, too small.


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
                QByteArray outPacket(outFrames * outFormat.channelCount() * sizeof(float), (char)0xff); // Preset the output buffer size.
                const float* in = (float*)samplesF.data();
                float* out = (float*)outPacket.data();

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
                samplesF = Eigen::Map<Eigen::VectorXf>(reinterpret_cast<float*>(outPacket.data()), outPacket.size() / int(sizeof(float)));
            }

            /*
                If output is Opus so encode it now, don't do any more conversion on the output of Opus.
            */

            if (outCodec == OPUS)
            {
                float* in = (float*)samplesF.data();
                QByteArray outPacket(1275, (char)0xff); // Preset the output buffer size to MAXIMUM possible Opus frame size
                unsigned char* out = (unsigned char*)outPacket.data();

                int nbBytes = opus_encode_float(opusEncoder, in, (samplesF.size() / outFormat.channelCount()), out, outPacket.length());
                if (nbBytes < 0)
                {
                    qInfo(logAudioConverter()) << "Opus encode failed:" << opus_strerror(nbBytes) << "Num Samples:" << samplesF.size();
                    return false;
                }
                else {
                    outPacket.resize(nbBytes);
                    audio.data.clear();
                    audio.data = outPacket; // Copy output packet back to input buffer.
                    //samplesF = Eigen::Map<Eigen::VectorXf>(reinterpret_cast<float*>(outPacket.data()), outPacket.size() / int(sizeof(float)));
                }

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
                    Eigen::VectorXf samplesITemp = samplesF * float(std::numeric_limits<qint8>::max());                    
                    samplesITemp.array() += 127;
                    VectorXuint8 samplesI = samplesITemp.cast<quint8>();
                    audio.data = QByteArray(reinterpret_cast<char*>(samplesI.data()), int(samplesI.size()) * int(sizeof(quint8)));
                }
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
                else if (outFormat.sampleType() == QAudioFormat::SignedInt && outFormat.sampleSize() == 16)
#else
                else if (outFormat.sampleFormat() == QAudioFormat::Int16)
#endif
                {
                    Eigen::VectorXf samplesITemp = samplesF * float(std::numeric_limits<qint16>::max());
                    VectorXint16 samplesI = samplesITemp.cast<qint16>();
                    audio.data = QByteArray(reinterpret_cast<char*>(samplesI.data()), int(samplesI.size()) * int(sizeof(qint16)));
                }
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
                else if (outFormat.sampleType() == QAudioFormat::SignedInt && outFormat.sampleSize() == 32)
#else
                else if (outFormat.sampleFormat() == QAudioFormat::Int32)
#endif
                {
                    Eigen::VectorXf samplesITemp = samplesF * float(std::numeric_limits<qint32>::max());
                    VectorXint32 samplesI = samplesITemp.cast<qint32>();
                    audio.data = QByteArray(reinterpret_cast<char*>(samplesI.data()), int(samplesI.size()) * int(sizeof(qint32)));
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
                    As we currently don't have a float based uLaw encoder, this must be done
                    after all other conversion has taken place.
                */
                if (outCodec == PCMU)
                {
                    QByteArray outPacket((int)audio.data.length() / 2, (char)0xff);
                    qint16* in = (qint16*)audio.data.data();
                    for (int f = 0; f < outPacket.length(); f++)
                    {
                        qint16 sample = *in++;
                        int sign = (sample >> 8) & 0x80;
                        if (sign)
                            sample = (short)-sample;
                        if (sample > cClip)
                            sample = cClip;
                        sample = (short)(sample + cBias);
                        int exponent = (int)MuLawCompressTable[(sample >> 7) & 0xFF];
                        int mantissa = (sample >> (exponent + 3)) & 0x0F;
                        int compressedByte = ~(sign | (exponent << 4) | mantissa);
                        outPacket[f] = (quint8)compressedByte;
                    }
                    audio.data.clear();
                    audio.data = outPacket; // Copy output packet back to input buffer.

                }
            }
        }
        else 
        {
            qDebug(logAudioConverter) << "Detected empty packet";
        }
    }

	emit converted(audio);
	return true;
}

