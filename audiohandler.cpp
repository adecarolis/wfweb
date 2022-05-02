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

	if (audioTimer != Q_NULLPTR) {
		audioTimer->stop();
		delete audioTimer;
		audioTimer = Q_NULLPTR;
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
		setup.ulaw = true;
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
	if (format.channelCount() == 1 && setup.format.channelCount() == 2) {
		format.setChannelCount(2);
		if (!setup.port.isFormatSupported(format)) {
			qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "Cannot request stereo input!";
			format.setChannelCount(1);
		}
	}

	if (format.sampleSize() == 24) {
		// We can't convert this easily so use 32 bit instead.
		format.setSampleSize(32);
		if (!setup.port.isFormatSupported(format)) {
			qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "24 bit requested and 32 bit audio not supported, try 16 bit instead";
			format.setSampleSize(16);
		}
	}

	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Internal: sample rate" << format.sampleRate() << "channel count" << format.channelCount();

	// We "hopefully" now have a valid format that is supported so try connecting

	if (setup.isinput) {
		audioInput = new QAudioInput(setup.port, format, this);
		qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Starting audio timer";
		audioTimer = new QTimer();
		audioTimer->setTimerType(Qt::PreciseTimer);
		connect(audioTimer, &QTimer::timeout, this, &audioHandler::getNextAudioChunk);

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
		connect(audioInput, &QAudioInput::destroyed, audioDevice, &QIODevice::deleteLater, Qt::UniqueConnection);
		//connect(audioDevice, &QIODevice::readyRead, this, &audioHandler::getNextAudioChunk);
		audioTimer->start(setup.blockSize);
	}
	else {
		/*	OK I don't understand what is happening here?
			On Windows, I set the buffer size and when the stream is started, the buffer size is multiplied by 10?
			this doesn't happen on Linux/
			We want the buffer sizes to be slightly more than the setup.latency value 
		*/
#ifdef Q_OS_WIN
		audioOutput->setBufferSize(format.bytesForDuration(setup.latency * 100));
#else
		audioOutput->setBufferSize(format.bytesForDuration(setup.latency * 1000));
#endif
		audioDevice = audioOutput->start();
		qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "bufferSize needed" << format.bytesForDuration((quint32)setup.latency * 1000) << "got" << audioOutput->bufferSize();
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
		audioTimer->stop();
		audioInput->stop();
	}
	audioDevice = Q_NULLPTR;
}

void audioHandler::setVolume(unsigned char volume)
{
	/*float volumeLevelLinear = float(0.5); //cut amplitude in half
	float volumeLevelDb = float(10 * (qLn(double(volumeLevelLinear)) / qLn(10)));
	float volumeLinear = (volume / float(100));
	this->volume = volumeLinear * float(qPow(10, (qreal(volumeLevelDb)) / 20));

	this->volume = qMin(this->volume, float(1));
	*/
    this->volume = audiopot[volume];
	qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "setVolume: " << volume << "(" << this->volume << ")";
}


void audioHandler::incomingAudio(audioPacket inPacket)
{


	QTime startProcessing = QTime::currentTime();

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
		setup.format.setSampleSize(16);
		setup.format.setSampleType(QAudioFormat::SignedInt);

	}


		/* Opus data */
	if (setup.codec == 0x40 || setup.codec == 0x80) {
		unsigned char* in = (unsigned char*)inPacket.data.data();

		//Decode the frame.
		int nSamples = opus_packet_get_nb_samples(in, livePacket.data.size(),setup.format.sampleRate());
		if (nSamples == -1) {
			// No opus data yet?
			return;
		}

		QByteArray outPacket(nSamples*sizeof(float)*setup.format.channelCount(), (char)0xff); // Preset the output buffer size.
		float* out = (float*)outPacket.data();

		if (livePacket.seq > lastSentSeq + 1) {
			nSamples = opus_decode_float(decoder, Q_NULLPTR,0, out, nSamples, 1);
		}
		else {
			nSamples = opus_decode_float(decoder, in, livePacket.data.size(), out, nSamples, 0);
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
		if (setup.format.sampleType() == QAudioFormat::SignedInt && setup.format.sampleSize() == 32)
		{
			Eigen::Ref<VectorXint32> samplesI = Eigen::Map<VectorXint32>(reinterpret_cast<qint32*>(livePacket.data.data()), livePacket.data.size() / int(sizeof(qint32)));
			samplesF = samplesI.cast<float>() / float(std::numeric_limits<qint32>::max());
		}
		else if (setup.format.sampleType() == QAudioFormat::SignedInt && setup.format.sampleSize() == 16)
		{
			Eigen::Ref<VectorXint16> samplesI = Eigen::Map<VectorXint16>(reinterpret_cast<qint16*>(livePacket.data.data()), livePacket.data.size() / int(sizeof(qint16)));
			samplesF = samplesI.cast<float>() / float(std::numeric_limits<qint16>::max());
		}
		else if (setup.format.sampleType() == QAudioFormat::SignedInt && setup.format.sampleSize() == 8)
		{
			Eigen::Ref<VectorXint8> samplesI = Eigen::Map<VectorXint8>(reinterpret_cast<qint8*>(livePacket.data.data()), livePacket.data.size() / int(sizeof(qint8)));
			samplesF = samplesI.cast<float>() / float(std::numeric_limits<qint8>::max());;
		}
		else if (setup.format.sampleType() == QAudioFormat::UnSignedInt && setup.format.sampleSize() == 8)
		{
			Eigen::Ref<VectorXuint8> samplesI = Eigen::Map<VectorXuint8>(reinterpret_cast<quint8*>(livePacket.data.data()), livePacket.data.size() / int(sizeof(quint8)));
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


		if (setup.format.channelCount() == 2 && format.channelCount() == 1) {
			// If we need to drop one of the audio channels, do it now 
			Eigen::VectorXf samplesTemp(samplesF.size() / 2);
			samplesTemp = Eigen::Map<Eigen::VectorXf, 0, Eigen::InnerStride<2> >(samplesF.data(), samplesF.size() / 2);
			samplesF = samplesTemp;
		}
		else if (setup.format.channelCount() == 1 && format.channelCount() == 2) {
			// Convert mono to stereo if required
			Eigen::VectorXf samplesTemp(samplesF.size() * 2);
			Eigen::Map<Eigen::VectorXf, 0, Eigen::InnerStride<2> >(samplesTemp.data(), samplesF.size()) = samplesF;
			Eigen::Map<Eigen::VectorXf, 0, Eigen::InnerStride<2> >(samplesTemp.data() + 1, samplesF.size()) = samplesF;
			samplesF = samplesTemp;
		}

		// We now have format.channelCount() (native) channels of audio.

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
		if (format.sampleType() == QAudioFormat::SignedInt && format.sampleSize() == 8)
		{
			Eigen::VectorXf samplesITemp = samplesF * float(std::numeric_limits<qint8>::max());
			VectorXint8 samplesI = samplesITemp.cast<qint8>();
			livePacket.data = QByteArray(reinterpret_cast<char*>(samplesI.data()), int(samplesI.size()) * int(sizeof(qint8)));
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

		if (audioDevice != Q_NULLPTR) {
			audioDevice->write(livePacket.data);
			if (lastReceived.msecsTo(QTime::currentTime()) > 30) {
				qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Time since last audio packet" << lastReceived.msecsTo(QTime::currentTime()) << "Expected around" << setup.blockSize << "Processing time" << startProcessing.msecsTo(QTime::currentTime());
			}
			lastReceived = QTime::currentTime();

		}
		if ((inPacket.seq > lastSentSeq + 1) && (setup.codec == 0x40 || setup.codec == 0x80)) {
			qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Attempting FEC on packet" << inPacket.seq << "as last is" << lastSentSeq;
			lastSentSeq = inPacket.seq;
			incomingAudio(inPacket); // Call myself again to run the packet a second time (FEC)
		}

		currentLatency = livePacket.time.msecsTo(QTime::currentTime()) + (format.durationForBytes(audioOutput->bufferSize() - audioOutput->bytesFree()) / 1000);
		lastSentSeq = inPacket.seq;
	}
	 else {
	 qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Received audio packet is empty?";
	}

	emit haveLevels(getAmplitude(), setup.latency, currentLatency,isUnderrun);

	return;
}


void audioHandler::getNextAudioChunk()
{
	tempBuf.data.append(audioDevice->readAll());

	if (tempBuf.data.length() < format.bytesForDuration(setup.blockSize * 1000)) {
		return;
	}
	

	audioPacket livePacket;
	livePacket.time= QTime::currentTime();
	livePacket.sent = 0;
	memcpy(&livePacket.guid, setup.guid, GUIDLEN);
	while (tempBuf.data.length() > format.bytesForDuration(setup.blockSize * 1000)) {
		QTime startProcessing = QTime::currentTime();
		livePacket.data.clear();
		livePacket.data = tempBuf.data.mid(0, format.bytesForDuration(setup.blockSize * 1000));
		tempBuf.data.remove(0, format.bytesForDuration(setup.blockSize * 1000));
		//qDebug(logAudio()) << "Got bytes " << format.bytesForDuration(setup.blockSize * 1000);

		if (livePacket.data.length() > 0)
		{
			Eigen::VectorXf samplesF;
			if (format.sampleType() == QAudioFormat::SignedInt && format.sampleSize() == 32)
			{
				Eigen::Ref<VectorXint32> samplesI = Eigen::Map<VectorXint32>(reinterpret_cast<qint32*>(livePacket.data.data()), livePacket.data.size() / int(sizeof(qint32)));
				samplesF = samplesI.cast<float>() / float(std::numeric_limits<qint32>::max());
			}
			else if (format.sampleType() == QAudioFormat::SignedInt && format.sampleSize() == 16)
			{
				Eigen::Ref<VectorXint16> samplesI = Eigen::Map<VectorXint16>(reinterpret_cast<qint16*>(livePacket.data.data()), livePacket.data.size() / int(sizeof(qint16)));
				samplesF = samplesI.cast<float>() / float(std::numeric_limits<qint16>::max());
			}
			else if (format.sampleType() == QAudioFormat::UnSignedInt && format.sampleSize() == 8)
			{
				Eigen::Ref<VectorXuint8> samplesI = Eigen::Map<VectorXuint8>(reinterpret_cast<quint8*>(livePacket.data.data()), livePacket.data.size() / int(sizeof(quint8)));
				samplesF = samplesI.cast<float>() / float(std::numeric_limits<quint8>::max());;
			}
			else if (format.sampleType() == QAudioFormat::SignedInt && format.sampleSize() == 8)
			{
				Eigen::Ref<VectorXint8> samplesI = Eigen::Map<VectorXint8>(reinterpret_cast<qint8*>(livePacket.data.data()), livePacket.data.size() / int(sizeof(qint8)));
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
				else if (format.channelCount() == 1 && setup.format.channelCount() == 2) {
					// Convert mono to stereo if required
					Eigen::VectorXf samplesTemp(samplesF.size() * 2);
					Eigen::Map<Eigen::VectorXf, 0, Eigen::InnerStride<2> >(samplesTemp.data(), samplesF.size()) = samplesF;
					Eigen::Map<Eigen::VectorXf, 0, Eigen::InnerStride<2> >(samplesTemp.data() + 1, samplesF.size()) = samplesF;
					samplesF = samplesTemp;
				}

				//qInfo(logAudio()) << "Sending audio len" << livePacket.data.length() << "remaining" << tempBuf.data.length() << "resampled" << samplesF.size();

				if (setup.codec == 0x40 || setup.codec == 0x80)
				{
					//Are we using the opus codec?	
					float* in = (float*)samplesF.data();

					/* Encode the frame. */
					QByteArray outPacket(1275, (char)0xff); // Preset the output buffer size to MAXIMUM possible Opus frame size
					unsigned char* out = (unsigned char*)outPacket.data();

					int nbBytes = opus_encode_float(encoder, in, (samplesF.size() / setup.format.channelCount()), out, outPacket.length());
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
				emit haveAudioData(livePacket);
				if (lastReceived.msecsTo(QTime::currentTime()) > 30) {
					qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Time since last audio packet" << lastReceived.msecsTo(QTime::currentTime()) << "Expected around" << setup.blockSize << "Processing time" << startProcessing.msecsTo(QTime::currentTime());
				}
				lastReceived = QTime::currentTime();

				//ret = livePacket.data;
			}
		}
		emit haveLevels(getAmplitude(), setup.latency, currentLatency, isUnderrun);
	}
	return;

}


void audioHandler::changeLatency(const quint16 newSize)
{

	setup.latency = newSize;

	if (!setup.isinput) {
		stop();
		start();
	}
	qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Configured latency: " << setup.latency << "Buffer Duration:" << format.durationForBytes(audioOutput->bufferSize()/1000) << "ms";

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
		if (!setup.isinput)
		{
			qDebug(logAudio()) << "Output Underrun detected" << "Buffer size" << audioOutput->bufferSize() << "Bytes free" << audioOutput->bytesFree();
			if (isUnderrun && audioOutput->bytesFree() > audioOutput->bufferSize()/2) {
				audioOutput->suspend();
			}
		}
		isUnderrun = true;
		if (!underTimer->isActive()) {
			underTimer->start(setup.latency/2);
		}
		
		break;
	}
	case QAudio::ActiveState:
	{
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
	if (!setup.isinput) {
		qDebug(logAudio()) << "clearUnderrun() " << "Buffer size" << audioOutput->bufferSize() << "Bytes free" << audioOutput->bytesFree();
		audioOutput->resume();
	}
}