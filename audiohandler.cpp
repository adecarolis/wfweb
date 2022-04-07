/*
	This class handles both RX and TX audio, each is created as a separate instance of the class
	but as the setup/handling if output (RX) and input (TX) devices is so similar I have combined them.
*/

#include "audiohandler.h"

#include "logcategories.h"
#include "ulaw.h"



audioHandler::audioHandler(QObject* parent) 
{
	Q_UNUSED(parent)
}

audioHandler::~audioHandler()
{

	if (isInitialized) {
		stop();
	}

	if (audioInput != Q_NULLPTR) {
		audioInput = Q_NULLPTR;
		delete audioInput;
	}
	if (audioOutput != Q_NULLPTR) {
		delete audioOutput;
		audioOutput = Q_NULLPTR;
	}


	if (resampler != Q_NULLPTR) {
		speex_resampler_destroy(resampler);
		qDebug(logAudio()) << "Resampler closed";
	}
	if (encoder != Q_NULLPTR) {
		qInfo(logAudio()) << "Destroying opus encoder";
		opus_encoder_destroy(encoder);
	}
	if (decoder != Q_NULLPTR) {
		qInfo(logAudio()) << "Destroying opus decoder";
		opus_decoder_destroy(decoder);
	}
}

bool audioHandler::init(audioSetup setupIn) 
{
	if (isInitialized) {
		return false;
	}
	/*
	0x01 uLaw 1ch 8bit
	0x02 PCM 1ch 8bit
	0x04 PCM 1ch 16bit
	0x08 PCM 2ch 8bit
	0x10 PCM 2ch 16bit
	0x20 uLaw 2ch 8bit
	*/

	setup = setupIn;
	setup.format.setChannelCount(1);
	setup.format.setSampleSize(8);
	setup.format.setSampleType(QAudioFormat::UnSignedInt);
	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "audio handler starting:" << setup.name;

	if (setup.port.isNull())
	{
		qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "No audio device was found. You probably need to install libqt5multimedia-plugins.";
		return false;
	}

	if (setup.codec == 0x01 || setup.codec == 0x20) {
		/* Althought uLaw is 8bit unsigned, it is 16bit signed once decoded*/
		setup.ulaw = true;
		setup.format.setSampleSize(16);
		setup.format.setSampleType(QAudioFormat::SignedInt);

	}
	if (setup.codec == 0x08 || setup.codec == 0x10 || setup.codec == 0x20 || setup.codec == 0x80) {
		setup.format.setChannelCount(2);
	}

	if (setup.codec == 0x04 || setup.codec == 0x10) {
		setup.format.setSampleSize(16);
		setup.format.setSampleType(QAudioFormat::SignedInt);
	}

	if (setup.codec == 0x40 || setup.codec == 0x80) {
		setup.format.setSampleType(QAudioFormat::Float);
	}

	qDebug(logAudio()) << "Creating" << (setup.isinput ? "Input" : "Output") << "audio device:" << setup.name <<
		", bits" << setup.format.sampleSize() <<
		", codec" << setup.codec <<
		", latency" << setup.latency <<
		", localAFGain" << setup.localAFgain <<
		", radioChan" << setup.format.channelCount() <<
		", resampleQuality" << setup.resampleQuality <<
		", samplerate" << setup.format.sampleRate() <<
		", uLaw" << setup.ulaw;


	tempBuf.sent = 0;

    if(!setup.isinput)
    {
        this->setVolume(setup.localAFgain);
    }

	format = setup.port.preferredFormat();
	qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Preferred Format: SampleSize" << format.sampleSize() << "Channel Count" << format.channelCount() <<
		"Sample Rate" << format.sampleRate() << "Codec" << format.codec() << "Sample Type" << format.sampleType();
	if (format.channelCount() > 2) {
		format.setChannelCount(2);
	}
	else if (format.channelCount() < 1)
	{
		qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "No channels found, aborting setup.";
		return false;
	}

	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Internal: sample rate" << format.sampleRate() << "channel count" << format.channelCount();

	// We "hopefully" now have a valid format that is supported so try connecting

	if (setup.isinput) {
		audioInput = new QAudioInput(setup.port, format, this);
		connect(audioInput, SIGNAL(stateChanged(QAudio::State)), SLOT(stateChanged(QAudio::State)));
	}
	else {
		audioOutput = new QAudioOutput(setup.port, format, this);
		connect(audioOutput, SIGNAL(stateChanged(QAudio::State)), SLOT(stateChanged(QAudio::State)));
	}

	// Setup resampler and opus if they are needed.
	int resample_error = 0;
	int opus_err = 0;
	if (setup.isinput) {
		resampler = wf_resampler_init(format.channelCount(), format.sampleRate(), setup.format.sampleRate(), setup.resampleQuality, &resample_error);
		if (setup.codec == 0x40 || setup.codec == 0x80) {
			// Opus codec
			encoder = opus_encoder_create(setup.format.sampleRate(), setup.format.channelCount(), OPUS_APPLICATION_AUDIO, &opus_err);
			opus_encoder_ctl(encoder, OPUS_SET_LSB_DEPTH(16));
			opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
			opus_encoder_ctl(encoder, OPUS_SET_DTX(1));
			opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(5));
			qInfo(logAudio()) << "Creating opus encoder: " << opus_strerror(opus_err);
		}
	}
	else {

		//resampBufs = new r8b::CFixedBuffer<double>[format.channelCount()];
		//resamps = new r8b::CPtrKeeper<r8b::CDSPResampler24*>[format.channelCount()];
		resampler = wf_resampler_init(format.channelCount(), setup.format.sampleRate(), format.sampleRate(), setup.resampleQuality, &resample_error);
		if (setup.codec == 0x40 || setup.codec == 0x80) {
			// Opus codec
			decoder = opus_decoder_create(setup.format.sampleRate(), setup.format.channelCount(), &opus_err);
			qInfo(logAudio()) << "Creating opus decoder: " << opus_strerror(opus_err);
		}
	}
	unsigned int ratioNum;
	unsigned int ratioDen;

	wf_resampler_get_ratio(resampler, &ratioNum, &ratioDen);
	resampleRatio = static_cast<double>(ratioDen) / ratioNum;
	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "wf_resampler_init() returned: " << resample_error << " resampleRatio: " << resampleRatio;

    qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "thread id" << QThread::currentThreadId();

	underTimer = new QTimer();
	underTimer->setSingleShot(true);
	connect(underTimer, &QTimer::timeout, this, &audioHandler::clearUnderrun);

	this->start();

	return true;
}

void audioHandler::start()
{
	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "start() running";

	if (setup.isinput) {
		audioDevice = audioInput->start();
		connect(audioInput, &QAudioOutput::destroyed, audioDevice, &QIODevice::deleteLater, Qt::UniqueConnection);
	}
	else {
		// Buffer size must be set before audio is started.
		audioOutput->setBufferSize(getAudioSize(setup.latency, format));
		audioDevice = audioOutput->start();
		connect(audioOutput, &QAudioOutput::destroyed, audioDevice, &QIODevice::deleteLater, Qt::UniqueConnection);
	}
	if (!audioDevice) {
		qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Audio device failed to start()";
		return;
	}

}


void audioHandler::stop()
{
	qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "stop() running";

	if (audioOutput != Q_NULLPTR && audioOutput->state() != QAudio::StoppedState) {
		// Stop audio output
		audioOutput->stop();
	}

	if (audioInput != Q_NULLPTR && audioInput->state() != QAudio::StoppedState) {
		// Stop audio output
		audioInput->stop();
	}
	audioDevice = Q_NULLPTR;
}

void audioHandler::setVolume(unsigned char volume)
{
    this->volume = audiopot[volume];
	qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "setVolume: " << volume << "(" << this->volume << ")";
}


void audioHandler::incomingAudio(audioPacket inPacket)
{

	audioPacket livePacket = inPacket;
	// Process uLaw.
	if (setup.ulaw)
	{
		// Current packet is 8bit so need to create a new buffer that is 16bit 
		QByteArray outPacket((int)livePacket.data.length() * 2, (char)0xff);
		qint16* out = (qint16*)outPacket.data();
		for (int f = 0; f < livePacket.data.length(); f++)
		{
			*out++ = ulaw_decode[(quint8)livePacket.data[f]];
		}
		livePacket.data.clear();
		livePacket.data = outPacket; // Replace incoming data with converted.
		// Buffer now contains 16bit signed samples.
	}


		/* Opus data */
	if (setup.codec == 0x40 || setup.codec == 0x80) {
		unsigned char* in = (unsigned char*)inPacket.data.data();

		//Decode the frame.
		QByteArray outPacket((960) *  sizeof(float) * setup.format.channelCount(), (char)0xff); // Preset the output buffer size.
		float* out = (float*)outPacket.data();
		int nSamples = opus_packet_get_nb_samples(in, livePacket.data.size(),setup.format.sampleRate());
		if (nSamples == -1) {
			// No opus data yet?
			return;
		}
		else if (nSamples != 960)
		{
			qDebug(logAudio()) << "Opus nSamples=" << nSamples << " expected:" << 960;
			//return;
		}
		if (livePacket.seq > lastSentSeq + 1) {
			nSamples = opus_decode_float(decoder, Q_NULLPTR,0, out, 960, 1);
		}
		else {
			nSamples = opus_decode_float(decoder, in, livePacket.data.size(), out, 960, 0);
		}
		if (nSamples < 0)
		{
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Opus decode failed:" << opus_strerror(nSamples) << "packet size" << livePacket.data.length();
			return;
		}
		else {
			if (int(nSamples * sizeof(float) * setup.format.channelCount()) != outPacket.size())
			{
				qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Opus decoder mismatch: nBytes:" << nSamples * sizeof(float) * setup.format.channelCount() << "outPacket:" << outPacket.size();
				outPacket.resize(nSamples * sizeof(float) * setup.format.channelCount());
			}
			//qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Opus decoded" << livePacket.data.size() << "bytes, into" << outPacket.length() << "bytes";
			livePacket.data.clear();
			livePacket.data = outPacket; // Replace incoming data with converted.
		}
		setup.format.setSampleType(QAudioFormat::Float);
	}


	if (!livePacket.data.isEmpty()) {

		Eigen::VectorXf samplesF;
		if (setup.format.sampleType() == QAudioFormat::SignedInt && setup.format.sampleSize() == 16)
		{
			VectorXint16 samplesI = Eigen::Map<VectorXint16>(reinterpret_cast<qint16*>(livePacket.data.data()), livePacket.data.size() / int(sizeof(qint16)));
			samplesF = samplesI.cast<float>() / float(std::numeric_limits<qint16>::max());
		}
		else if (setup.format.sampleType() == QAudioFormat::UnSignedInt && setup.format.sampleSize() == 8)
		{
			VectorXuint8 samplesI = Eigen::Map<VectorXuint8>(reinterpret_cast<quint8*>(livePacket.data.data()), livePacket.data.size() / int(sizeof(quint8)));
			samplesF = samplesI.cast<float>() / float(std::numeric_limits<quint8>::max());;
		}
		else if (setup.format.sampleType() == QAudioFormat::Float) {
			samplesF = Eigen::Map<Eigen::VectorXf>(reinterpret_cast<float*>(livePacket.data.data()), livePacket.data.size() / int(sizeof(float)));
		}
		else
		{ 
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Unsupported Sample Type:" << format.sampleType();
		}


		/* samplesF is now an Eigen Vector of the current samples in float format */


		// Set the max amplitude found in the vector
		// Should it be before or after volume is applied?

		amplitude = samplesF.array().abs().maxCoeff();
		// Set the volume
		samplesF *= volume;

		// Convert mono to stereo if required
		if (setup.format.channelCount() == 1) {
			Eigen::VectorXf samplesTemp(samplesF.size() * 2);
			Eigen::Map<Eigen::VectorXf, 0, Eigen::InnerStride<2> >(samplesTemp.data(), samplesF.size()) = samplesF;
			Eigen::Map<Eigen::VectorXf, 0, Eigen::InnerStride<2> >(samplesTemp.data() + 1, samplesF.size()) = samplesF;
			samplesF = samplesTemp;
		}


		if (resampleRatio != 1.0) {

			// We need to resample
			// We have a stereo 16bit stream.
			quint32 outFrames = ((samplesF.size() / format.channelCount()) * resampleRatio);
			quint32 inFrames = (samplesF.size() / format.channelCount());
			QByteArray outPacket(outFrames * format.channelCount() * sizeof(float), (char)0xff); // Preset the output buffer size.
			const float* in = (float*)samplesF.data();
			float* out = (float*)outPacket.data();

			int err = 0;
			if (format.channelCount() == 1) {
				err = wf_resampler_process_float(resampler, 0, in, &inFrames, out, &outFrames);
			}
			else {
				err = wf_resampler_process_interleaved_float(resampler, in, &inFrames, out, &outFrames);
			}

			if (err) {
				qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Resampler error " << err << " inFrames:" << inFrames << " outFrames:" << outFrames;
			}
			samplesF = Eigen::Map<Eigen::VectorXf>(reinterpret_cast<float*>(outPacket.data()), outPacket.size() / int(sizeof(float)));
		}

		if (format.sampleType() == QAudioFormat::UnSignedInt && format.sampleSize() == 8)
		{
			Eigen::VectorXf samplesITemp = samplesF * float(std::numeric_limits<quint8>::max());
			VectorXuint8 samplesI = samplesITemp.cast<quint8>();
			livePacket.data = QByteArray(reinterpret_cast<char*>(samplesI.data()), int(samplesI.size()) * int(sizeof(quint8)));
		}
		if (format.sampleType() == QAudioFormat::SignedInt && format.sampleSize() == 16)
		{
			Eigen::VectorXf samplesITemp = samplesF * float(std::numeric_limits<qint16>::max());
			VectorXint16 samplesI = samplesITemp.cast<qint16>();
			livePacket.data = QByteArray(reinterpret_cast<char*>(samplesI.data()), int(samplesI.size()) * int(sizeof(qint16)));
		}
		else if (format.sampleType() == QAudioFormat::SignedInt && format.sampleSize() == 32)
		{
			Eigen::VectorXf samplesITemp = samplesF * float(std::numeric_limits<qint32>::max());
			VectorXint32 samplesI = samplesITemp.cast<qint32>();
			livePacket.data = QByteArray(reinterpret_cast<char*>(samplesI.data()), int(samplesI.size()) * int(sizeof(qint32)));
		}
		else if (format.sampleType() == QAudioFormat::Float)
		{
			livePacket.data = QByteArray(reinterpret_cast<char*>(samplesF.data()), int(samplesF.size()) * int(sizeof(float)));
		}
		else {
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Unsupported Sample Type:" << format.sampleType();
		}

		currentLatency = livePacket.time.msecsTo(QTime::currentTime()) + getAudioDuration(audioOutput->bufferSize()-audioOutput->bytesFree(),format);
		if (audioDevice != Q_NULLPTR) {
			audioDevice->write(livePacket.data);
		}
		if ((inPacket.seq > lastSentSeq + 1) && (setup.codec == 0x40 || setup.codec == 0x80)) {
			qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Attempting FEC on packet" << inPacket.seq << "as last is" << lastSentSeq;
			lastSentSeq = inPacket.seq;
			incomingAudio(inPacket); // Call myself again to run the packet a second time (FEC)
		}

		lastSentSeq = inPacket.seq;
	}

	emit haveLevels(getAmplitude(), setup.latency, currentLatency,isUnderrun);

	return;
}


void audioHandler::getNextAudioChunk(QByteArray& ret)
{
	audioPacket livePacket;
	livePacket.sent = 0;
	// Don't start sending until we have setup.latency of audio buffered
	if (audioDevice->bytesAvailable() < format.bytesForDuration((setup.latency*1000)))
	{
		return;
	}
	if (audioDevice != Q_NULLPTR) {
		livePacket.data = audioDevice->read(format.bytesForDuration(20000)); // 20000uS is 20ms in NATIVE format.
		if (livePacket.data.length() > 0)
		{
			Eigen::VectorXf samplesF;
			if (format.sampleType() == QAudioFormat::SignedInt && format.sampleSize() == 32)
			{
				VectorXint32 samplesI = Eigen::Map<VectorXint32>(reinterpret_cast<qint32*>(livePacket.data.data()), livePacket.data.size() / int(sizeof(qint32)));
				samplesF = samplesI.cast<float>() / float(std::numeric_limits<qint32>::max());
			}
			else if (format.sampleType() == QAudioFormat::SignedInt && format.sampleSize() == 16)
			{
				VectorXint16 samplesI = Eigen::Map<VectorXint16>(reinterpret_cast<qint16*>(livePacket.data.data()), livePacket.data.size() / int(sizeof(qint16)));
				samplesF = samplesI.cast<float>() / float(std::numeric_limits<qint16>::max());
			}
			else if (format.sampleType() == QAudioFormat::UnSignedInt && format.sampleSize() == 8)
			{
				VectorXuint8 samplesI = Eigen::Map<VectorXuint8>(reinterpret_cast<quint8*>(livePacket.data.data()), livePacket.data.size() / int(sizeof(quint8)));
				samplesF = samplesI.cast<float>() / float(std::numeric_limits<quint8>::max());;
			}
			else if (format.sampleType() == QAudioFormat::SignedInt && format.sampleSize() == 8)
			{
				VectorXint8 samplesI = Eigen::Map<VectorXint8>(reinterpret_cast<qint8*>(livePacket.data.data()), livePacket.data.size() / int(sizeof(qint8)));
				samplesF = samplesI.cast<float>() / float(std::numeric_limits<qint8>::max());;
			}
			else if (format.sampleType() == QAudioFormat::Float)
			{
				samplesF = Eigen::Map<Eigen::VectorXf>(reinterpret_cast<float*>(livePacket.data.data()), livePacket.data.size() / int(sizeof(float)));
			}
			else {
				qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Unsupported Sample Type:" << format.sampleType() << format.sampleSize();
			}

			/* samplesF is now an Eigen Vector of the current samples in float format */

			// Set the max amplitude found in the vector
			if (samplesF.size() > 0) {
				amplitude = samplesF.array().abs().maxCoeff();
				// Channel count should now match the device that audio is going to (rig)

				if (resampleRatio != 1.0) {

					// We need to resample
					// We have a stereo 16bit stream.
					quint32 outFrames = ((samplesF.size() / format.channelCount()) * resampleRatio);
					quint32 inFrames = (samplesF.size() / format.channelCount());

					QByteArray outPacket(outFrames * format.channelCount() * sizeof(float), (char)0xff); // Preset the output buffer size.
					const float* in = (float*)samplesF.data();
					float* out = (float*)outPacket.data();

					int err = 0;
					if (format.channelCount() == 1) {
						err = wf_resampler_process_float(resampler, 0, in, &inFrames, out, &outFrames);
					}
					else {
						err = wf_resampler_process_interleaved_float(resampler, in, &inFrames, out, &outFrames);
					}
					if (err) {
						qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Resampler error " << err << " inFrames:" << inFrames << " outFrames:" << outFrames;
					}
					samplesF = Eigen::Map<Eigen::VectorXf>(reinterpret_cast<float*>(outPacket.data()), outPacket.size() / int(sizeof(float)));
				}

				// If we need to drop one of the audio channels, do it now 
				if (format.channelCount() == 2 && setup.format.channelCount() == 1) {
					Eigen::VectorXf samplesTemp(samplesF.size() / 2);
					samplesTemp = Eigen::Map<Eigen::VectorXf, 0, Eigen::InnerStride<2> >(samplesF.data(), samplesF.size() / 2);
					samplesF = samplesTemp;
				}


				if (setup.codec == 0x40 || setup.codec == 0x80)
				{
					//Are we using the opus codec?	
					float* in = (float*)samplesF.data();

					/* Encode the frame. */
					QByteArray outPacket(1275, (char)0xff); // Preset the output buffer size to MAXIMUM possible Opus frame size
					unsigned char* out = (unsigned char*)outPacket.data();

					int nbBytes = opus_encode_float(encoder, in, (samplesF.size()/setup.format.channelCount()), out, outPacket.length());
					if (nbBytes < 0)
					{
						qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Opus encode failed:" << opus_strerror(nbBytes) << "Num Samples:" << samplesF.size();
						return;
					}
					else {
						outPacket.resize(nbBytes);
						samplesF = Eigen::Map<Eigen::VectorXf>(reinterpret_cast<float*>(outPacket.data()), outPacket.size() / int(sizeof(float)));
					}
				}


				if (setup.format.sampleType() == QAudioFormat::SignedInt && setup.format.sampleSize() == 8)
				{
					Eigen::VectorXf samplesITemp = samplesF * float(std::numeric_limits<qint8>::max());
					VectorXint8 samplesI = samplesITemp.cast<qint8>();
					livePacket.data = QByteArray(reinterpret_cast<char*>(samplesI.data()), int(samplesI.size()) * int(sizeof(qint8)));
				}
				else if (setup.format.sampleType() == QAudioFormat::UnSignedInt && setup.format.sampleSize() == 8)
				{
					Eigen::VectorXf samplesITemp = samplesF * float(std::numeric_limits<quint8>::max());
					VectorXuint8 samplesI = samplesITemp.cast<quint8>();
					livePacket.data = QByteArray(reinterpret_cast<char*>(samplesI.data()), int(samplesI.size()) * int(sizeof(quint8)));
				}
				else if (setup.format.sampleType() == QAudioFormat::SignedInt && setup.format.sampleSize() == 16)
				{
					Eigen::VectorXf samplesITemp = samplesF * float(std::numeric_limits<qint16>::max());
					VectorXint16 samplesI = samplesITemp.cast<qint16>();
					livePacket.data = QByteArray(reinterpret_cast<char*>(samplesI.data()), int(samplesI.size()) * int(sizeof(qint16)));
				}
				else if (setup.format.sampleType() == QAudioFormat::SignedInt && setup.format.sampleSize() == 32)
				{
					Eigen::VectorXf samplesITemp = samplesF * float(std::numeric_limits<qint32>::max());
					VectorXint32 samplesI = samplesITemp.cast<qint32>();
					livePacket.data = QByteArray(reinterpret_cast<char*>(samplesI.data()), int(samplesI.size()) * int(sizeof(qint32)));
				}
				else if (setup.format.sampleType() == QAudioFormat::Float)
				{
					livePacket.data = QByteArray(reinterpret_cast<char*>(samplesF.data()), int(samplesF.size()) * int(sizeof(float)));
				}
				else {
					qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Unsupported Sample Type:" << format.sampleType();
				}

				/* Need to find a floating point uLaw encoder!*/
				if (setup.ulaw)
				{
					QByteArray outPacket((int)livePacket.data.length() / 2, (char)0xff);
					qint16* in = (qint16*)livePacket.data.data();
					for (int f = 0; f < outPacket.length(); f++)
					{
						qint16 sample = *in++;
						if (setup.ulaw) {
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
					}
					livePacket.data.clear();
					livePacket.data = outPacket; // Copy output packet back to input buffer.
				}
				ret = livePacket.data;
			}
		}
	}

	emit haveLevels(getAmplitude(), setup.latency, currentLatency, isUnderrun);

	return;

}


void audioHandler::changeLatency(const quint16 newSize)
{

	setup.latency = newSize;

	if (!setup.isinput) {
		stop();
		start();
	}
	qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Configured latency: " << setup.latency << "Buffer Duration:" << getAudioDuration(audioOutput->bufferSize(), format) << "ms";

}

int audioHandler::getLatency()
{
	return currentLatency;
}



quint16 audioHandler::getAmplitude()
{
	return static_cast<quint16>(amplitude * 255.0);
}



void audioHandler::stateChanged(QAudio::State state)
{
	// Process the state
	switch (state)
	{
	case QAudio::IdleState:
	{
		isUnderrun = true;
		if (underTimer->isActive()) {
			underTimer->stop();
		}
		break;
	}
	case QAudio::ActiveState:
	{
		//qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Audio started!";
		if (!underTimer->isActive()) {
			underTimer->start(500);
		}
		break;
	}
	case QAudio::SuspendedState:
	{
		break;
	}
	case QAudio::StoppedState:
	{
		break;
	}
	default: {
	}
	    break;
	}
}

void audioHandler::clearUnderrun()
{
	isUnderrun = false;
	underTimer->stop();
}