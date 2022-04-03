/*
	This class handles both RX and TX audio, each is created as a separate instance of the class
	but as the setup/handling if output (RX) and input (TX) devices is so similar I have combined them.
*/

#include "audiohandler.h"

#include "logcategories.h"
#include "ulaw.h"

#if defined(Q_OS_WIN) && defined(PORTAUDIO)
#include <objbase.h>
#endif


audioHandler::audioHandler(QObject* parent) 
{
	Q_UNUSED(parent)
}

audioHandler::~audioHandler()
{

	if (isInitialized) {
#if defined(RTAUDIO)

		try {
			audio->abortStream();
			audio->closeStream();
		}
		catch (RtAudioError& e) {
			qInfo(logAudio()) << "Error closing stream:" << aParams.deviceId << ":" << QString::fromStdString(e.getMessage());
		}
		delete audio;
#elif defined(PORTAUDIO)
		Pa_StopStream(audio);
		Pa_CloseStream(audio);
#else
		stop();
#endif
	}

	if (ringBuf != Q_NULLPTR) {
		delete ringBuf;
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

	if (setup.codec == 0x01 || setup.codec == 0x20) {
		setup.ulaw = true;
	}
	if (setup.codec == 0x08 || setup.codec == 0x10 || setup.codec == 0x20 || setup.codec == 0x80) {
		setup.format.setChannelCount(2);
	}
	if (setup.codec == 0x04 || setup.codec == 0x10 || setup.codec == 0x40 || setup.codec == 0x80) {
		setup.format.setSampleSize(16);
		setup.format.setSampleType(QAudioFormat::SignedInt);
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


	ringBuf = new wilt::Ring<audioPacket>(setup.latency + 1); // Should be customizable.

	tempBuf.sent = 0;

    if(!setup.isinput)
    {
        this->setVolume(setup.localAFgain);
    }

#if defined(RTAUDIO)	
#if !defined(Q_OS_MACX)
	options.flags = ((!RTAUDIO_HOG_DEVICE) | (RTAUDIO_MINIMIZE_LATENCY));
#endif

#if defined(Q_OS_LINUX)
	audio = new RtAudio(RtAudio::Api::LINUX_ALSA);
#elif defined(Q_OS_WIN)
	audio = new RtAudio(RtAudio::Api::WINDOWS_WASAPI);
#elif defined(Q_OS_MACX)
	audio = new RtAudio(RtAudio::Api::MACOSX_CORE);
#endif

	if (setup.port > 0) {
		aParams.deviceId = setup.port;
	}
	else if (setup.isinput) {
		aParams.deviceId = audio->getDefaultInputDevice();
	}
	else {
		aParams.deviceId = audio->getDefaultOutputDevice();
	}
	aParams.firstChannel = 0;

	try {
		info = audio->getDeviceInfo(aParams.deviceId);
	}
	catch (RtAudioError& e) {
		qInfo(logAudio()) << "Device error:" << aParams.deviceId << ":" << QString::fromStdString(e.getMessage());
		return isInitialized;
	}

	if (info.probed)
	{
		// Always use the "preferred" sample rate
		// We can always resample if needed
		this->nativeSampleRate = info.preferredSampleRate;

		// Per channel chunk size.
		this->chunkSize = (this->nativeSampleRate / 50);

		qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << QString::fromStdString(info.name) << "(" << aParams.deviceId << ") successfully probed";
		if (info.nativeFormats == 0)
		{
			qInfo(logAudio()) << "		No natively supported data formats!";
			return false;
		}
		else {
			qDebug(logAudio()) << "		Supported formats:" <<
				(info.nativeFormats & RTAUDIO_SINT8 ? "8-bit int," : "") <<
				(info.nativeFormats & RTAUDIO_SINT16 ? "16-bit int," : "") <<
				(info.nativeFormats & RTAUDIO_SINT24 ? "24-bit int," : "") <<
				(info.nativeFormats & RTAUDIO_SINT32 ? "32-bit int," : "") <<
				(info.nativeFormats & RTAUDIO_FLOAT32 ? "32-bit float," : "") <<
				(info.nativeFormats & RTAUDIO_FLOAT64 ? "64-bit float," : "");

			qInfo(logAudio()) << "		Preferred sample rate:" << info.preferredSampleRate;
			if (setup.isinput) {
				devChannels = info.inputChannels;
			}
			else {
				devChannels = info.outputChannels;
			}
			qInfo(logAudio()) << "		Channels:" << devChannels;
			if (devChannels > 2) {
				devChannels = 2;
			}
			aParams.nChannels = devChannels;
		}

		qInfo(logAudio()) << "		chunkSize: " << chunkSize;
		try {
			if (setup.isinput) {
				audio->openStream(NULL, &aParams, RTAUDIO_SINT16, this->nativeSampleRate, &this->chunkSize, &staticWrite, this, &options);
			}
			else {
				audio->openStream(&aParams, NULL, RTAUDIO_SINT16, this->nativeSampleRate, &this->chunkSize, &staticRead, this, &options);
			}
			audio->startStream();
			isInitialized = true;
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "device successfully opened";
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "detected latency:" << audio->getStreamLatency();
		}
		catch (RtAudioError& e) {
			qInfo(logAudio()) << "Error opening:" << QString::fromStdString(e.getMessage());
		}
	}
	else
	{
		qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << QString::fromStdString(info.name) << "(" << aParams.deviceId << ") could not be probed, check audio configuration!";
	}


#elif defined(PORTAUDIO)

	PaError err;

#ifdef Q_OS_WIN
	CoInitialize(0);
#endif

	memset(&aParams, 0,sizeof(PaStreamParameters));

	if (setup.port > 0) {
		aParams.device = setup.port;
	}
	else if (setup.isinput) {
		aParams.device = Pa_GetDefaultInputDevice();
	}
	else {
		aParams.device = Pa_GetDefaultOutputDevice();
	}
	
	info = Pa_GetDeviceInfo(aParams.device);

	aParams.channelCount = 2;
	aParams.hostApiSpecificStreamInfo = NULL;
	aParams.sampleFormat = paInt16;
	if (setup.isinput) {
		aParams.suggestedLatency = info->defaultLowInputLatency;
	}
	else {
		aParams.suggestedLatency = info->defaultLowOutputLatency;
	}

	aParams.hostApiSpecificStreamInfo = NULL; 

	// Always use the "preferred" sample rate (unless it is 44100) 
	// We can always resample if needed
	if (info->defaultSampleRate == 44100) {
		this->nativeSampleRate = 48000;
	} 
	else {
		this->nativeSampleRate = info->defaultSampleRate;
	}
	// Per channel chunk size.
	this->chunkSize = (this->nativeSampleRate / 50);

	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << info->name << "(" << aParams.device << ") successfully probed";
	if (setup.isinput) {
		devChannels = info->maxInputChannels;
	}
	else {
		devChannels = info->maxOutputChannels;
	}
	if (devChannels > 2) {
		devChannels = 2;
	}
	aParams.channelCount = devChannels;

	qInfo(logAudio()) << "		Channels:" << devChannels;
	qInfo(logAudio()) << "		chunkSize: " << chunkSize;
	qInfo(logAudio()) << "		sampleRate: " << nativeSampleRate;

	if (setup.isinput) {
		err=Pa_OpenStream(&audio, &aParams, 0, this->nativeSampleRate, this->chunkSize, paNoFlag, &audioHandler::staticWrite, (void*)this);
	}
	else {
		err=Pa_OpenStream(&audio, 0, &aParams, this->nativeSampleRate, this->chunkSize, paNoFlag, &audioHandler::staticRead, (void*)this);
	}

	if (err == paNoError) {
		err = Pa_StartStream(audio);
	}
	if (err == paNoError) {
		isInitialized = true;
		qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "device successfully opened";
	}
	else {
		qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "failed to open device" << Pa_GetErrorText(err);
	}


#else

	format.setSampleSize(16);
	format.setChannelCount(2);
	format.setSampleRate(INTERNAL_SAMPLE_RATE);
	format.setCodec("audio/pcm");
	format.setByteOrder(QAudioFormat::LittleEndian);
	format.setSampleType(QAudioFormat::SignedInt);
	if (setup.port.isNull())
	{
		qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "No audio device was found. You probably need to install libqt5multimedia-plugins.";
		return false;
	}
	else if (!setup.port.isFormatSupported(format))
	{
		qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Format not supported, choosing nearest supported format - which may not work!";
		format=setup.port.nearestFormat(format);
	}
	if (format.channelCount() > 2) {
		format.setChannelCount(2);
	}
	else if (format.channelCount() < 1)
	{
		qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "No channels found, aborting setup.";
		return false;
	}

	devChannels = format.channelCount();
	nativeSampleRate = format.sampleRate();
	// chunk size is always relative to Internal Sample Rate.
	this->chunkSize = (nativeSampleRate / 50);

	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Internal: sample rate" << format.sampleRate() << "channel count" << format.channelCount();

	// We "hopefully" now have a valid format that is supported so try connecting

	if (setup.isinput) {
		audioInput = new QAudioInput(setup.port, format, this);
		connect(audioInput, SIGNAL(notify()), SLOT(notified()));
		connect(audioInput, SIGNAL(stateChanged(QAudio::State)), SLOT(stateChanged(QAudio::State)));
		isInitialized = true;
	}
	else {
		audioOutput = new QAudioOutput(setup.port, format, this);

		audioOutput->setBufferSize(getAudioSize(setup.latency, format));

		//connect(audioOutput, SIGNAL(notify()), SLOT(notified()));
		connect(audioOutput, SIGNAL(stateChanged(QAudio::State)), SLOT(stateChanged(QAudio::State)));
		isInitialized = true;
	}

#endif
	// Setup resampler and opus if they are needed.
	int resample_error = 0;
	int opus_err = 0;
	if (setup.isinput) {
		resampler = wf_resampler_init(devChannels, nativeSampleRate, setup.format.sampleRate(), setup.resampleQuality, &resample_error);
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
		resampler = wf_resampler_init(devChannels, setup.format.sampleRate(), this->nativeSampleRate, setup.resampleQuality, &resample_error);
		if (setup.codec == 0x40 || setup.codec == 0x80) {
			// Opus codec
			decoder = opus_decoder_create(setup.format.sampleRate(), setup.format.sampleRate(), &opus_err);
			qInfo(logAudio()) << "Creating opus decoder: " << opus_strerror(opus_err);
		}
	}
	unsigned int ratioNum;
	unsigned int ratioDen;

	wf_resampler_get_ratio(resampler, &ratioNum, &ratioDen);
	resampleRatio = static_cast<double>(ratioDen) / ratioNum;
	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "wf_resampler_init() returned: " << resample_error << " resampleRatio: " << resampleRatio;

    qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "thread id" << QThread::currentThreadId();

#if !defined (RTAUDIO) && !defined(PORTAUDIO)
	if (isInitialized) {
		this->start();
	}
#endif

	return isInitialized;
}

#if !defined (RTAUDIO) && !defined(PORTAUDIO)
void audioHandler::start()
{
	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "start() running";

	if ((audioOutput == Q_NULLPTR || audioOutput->state() != QAudio::StoppedState) &&
		(audioInput == Q_NULLPTR || audioInput->state() != QAudio::StoppedState)) {
		return;
	}

	if (setup.isinput) {
#ifndef Q_OS_WIN
		this->open(QIODevice::WriteOnly);
#else
		this->open(QIODevice::WriteOnly);
		//this->open(QIODevice::WriteOnly | QIODevice::Unbuffered);
#endif
		audioInput->start(this);
	}
	else {
#ifndef Q_OS_WIN
		this->open(QIODevice::ReadOnly);
#else
		//this->open(QIODevice::ReadOnly | QIODevice::Unbuffered);
		//this->open(QIODevice::ReadOnly);
#endif
		audioDevice = audioOutput->start();
		if (!audioDevice)
		{
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Audio device failed to start()";
			return;
		}
		connect(audioOutput, &QAudioOutput::destroyed, audioDevice, &QIODevice::deleteLater, Qt::UniqueConnection);
		connect(audioDevice, &QIODevice::destroyed, this, &QAudioOutput::deleteLater, Qt::UniqueConnection);
		audioBuffered = true;

	}
}
#endif

void audioHandler::setVolume(unsigned char volume)
{

    this->volume = audiopot[volume];

	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "setVolume: " << volume << "(" << this->volume << ")";
}



/// <summary>
/// This function processes the incoming audio FROM the radio and pushes it into the playback buffer *data
/// </summary>
/// <param name="data"></param>
/// <param name="maxlen"></param>
/// <returns></returns>
#if defined(RTAUDIO)
int audioHandler::readData(void* outputBuffer, void* inputBuffer, 
	unsigned int nFrames, double streamTime, RtAudioStreamStatus status)
{
	Q_UNUSED(inputBuffer);
	Q_UNUSED(streamTime);
	if (status == RTAUDIO_OUTPUT_UNDERFLOW)
		qDebug(logAudio()) << "Underflow detected";
	int nBytes = nFrames * devChannels * 2; // This is ALWAYS 2 bytes per sample and 2 channels
	quint8* buffer = (quint8*)outputBuffer;
#elif defined(PORTAUDIO)

int audioHandler::readData(const void* inputBuffer, void* outputBuffer,
	unsigned long nFrames, const PaStreamCallbackTimeInfo * streamTime, PaStreamCallbackFlags status)
{
	Q_UNUSED(inputBuffer);
	Q_UNUSED(streamTime);
	Q_UNUSED(status);
	int nBytes = nFrames * devChannels * 2; // This is ALWAYS 2 bytes per sample and 2 channels
	quint8* buffer = (quint8*)outputBuffer;
#else
qint64 audioHandler::readData(char* buffer, qint64 nBytes)
{
#endif
	// Calculate output length, always full samples
	int sentlen = 0;
	if (!isReady) {
		isReady = true;
	}
	if (!audioBuffered) {
		memset(buffer, 0, nBytes);
#if defined(RTAUDIO)
		return 0;
#elif defined(PORTAUDIO)
		return 0;
#else
		return nBytes;
#endif
	}
	audioPacket packet;
	if (ringBuf->size()>0)
	{
		// Output buffer is ALWAYS 16 bit.
		while (sentlen < nBytes)
		{
			if (!ringBuf->try_read(packet))
			{
				qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "buffer is empty, sentlen:" << sentlen << " nBytes:" << nBytes ;
				break;
			}
			//qDebug(logAudio()) << "Packet size:" << packet.data.length() << "nBytes (requested)" << nBytes << "remaining" << nBytes-sentlen;
			currentLatency = packet.time.msecsTo(QTime::currentTime());

			// This shouldn't be required but if we did output a partial packet
			// This will add the remaining packet data to the output buffer.
			if (tempBuf.sent != tempBuf.data.length())
			{
				int send = qMin((int)nBytes - sentlen, tempBuf.data.length() - tempBuf.sent);
				memcpy(buffer + sentlen, tempBuf.data.constData() + tempBuf.sent, send);
				tempBuf.sent = tempBuf.sent + send;
				sentlen = sentlen + send;
				if (tempBuf.sent != tempBuf.data.length())
				{
					// We still don't have enough buffer space for this?
					break;
				}
				//qDebug(logAudio()) << "Adding partial:" << send;
			}

			if (currentLatency > setup.latency) {
				qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Packet " << hex << packet.seq <<
					" arrived too late (increase output latency!) " <<
					dec << packet.time.msecsTo(QTime::currentTime()) << "ms";
				delayedPackets++;
			}

			int send = qMin((int)nBytes - sentlen, packet.data.length());
			memcpy(buffer + sentlen, packet.data.constData(), send);
			sentlen = sentlen + send;
			if (send < packet.data.length())
			{
				//qDebug(logAudio()) << "Asking for partial, sent:" << send << "packet length" << packet.data.length();
				tempBuf = packet;
				tempBuf.sent = tempBuf.sent + send;
				lastSeq = packet.seq;
				break;
			}

			
			if (packet.seq <= lastSeq) {
				qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Duplicate/early audio packet: " << hex << lastSeq << " got " << hex << packet.seq;
			}
			else if (packet.seq != lastSeq + 1) {
				qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Missing audio packet(s) from: " << hex << lastSeq + 1 << " to " << hex << packet.seq - 1;
			}
			
			lastSeq = packet.seq;
		}
	}

    // fill the rest of the buffer with silence
    if (nBytes > sentlen) {
		qDebug(logAudio()) << "looking for: " << nBytes << " got: " << sentlen;
		memset(buffer + sentlen, 0, nBytes - sentlen);
	}

	if (delayedPackets > 10) {
		qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Too many delayed packets, flushing buffer";
		//while (ringBuf->try_read(packet)); // Empty buffer
		delayedPackets = 0;
		//audioBuffered = false;
	}

#if defined(RTAUDIO)
	return 0;
#elif defined(PORTAUDIO)
	return 0;
#else
	return nBytes;
#endif
}

#if defined(RTAUDIO)
int audioHandler::writeData(void* outputBuffer, void* inputBuffer, 
	unsigned int nFrames, double streamTime, RtAudioStreamStatus status)
{
	Q_UNUSED(outputBuffer);
	Q_UNUSED(streamTime);
	Q_UNUSED(status);
	int nBytes = nFrames * devChannels * 2; // This is ALWAYS 2 bytes per sample and 2 channels
	const char* data = (const char*)inputBuffer;
#elif defined(PORTAUDIO)
int audioHandler::writeData(const void* inputBuffer, void* outputBuffer,
	unsigned long nFrames, const PaStreamCallbackTimeInfo * streamTime,
	PaStreamCallbackFlags status)
{
	Q_UNUSED(outputBuffer);
	Q_UNUSED(streamTime);
	Q_UNUSED(status);
	int nBytes = nFrames * devChannels * 2; // This is ALWAYS 2 bytes per sample and 2 channels
    const char* data = (const char*)inputBuffer;
#else
qint64 audioHandler::writeData(const char* data, qint64 nBytes)
{
#endif
	if (!isReady) {
		isReady = true;
	}
	int sentlen = 0;
	//qDebug(logAudio()) << "nFrames" << nFrames << "nBytes" << nBytes;
	int chunkBytes = chunkSize * devChannels * 2;
	while (sentlen < nBytes) {
		if (tempBuf.sent != chunkBytes)
		{
			int send = qMin((int)(nBytes - sentlen), chunkBytes - tempBuf.sent);
			tempBuf.data.append(QByteArray::fromRawData(data + sentlen, send));
			sentlen = sentlen + send;
			tempBuf.seq = lastSentSeq;
			tempBuf.time = QTime::currentTime();
			tempBuf.sent = tempBuf.sent + send;
		}
		else {
			if (!ringBuf->try_write(tempBuf))
			{
				qDebug(logAudio()) <<  (setup.isinput ? "Input" : "Output") << " audio buffer full!";
				break;
			} 
			tempBuf.data.clear();
			tempBuf.sent = 0;
			lastSentSeq++;
		}
	}

	//qDebug(logAudio()) << "sentlen" << sentlen;
#if defined(RTAUDIO)
	return 0;
#elif defined(PORTAUDIO)
	return 0;
#else
	return nBytes;
#endif
}

void audioHandler::incomingAudio(audioPacket inPacket)
{
	// No point buffering audio until stream is actually running.
	// Regardless of the radio stream format, the buffered audio will ALWAYS be
	// 16bit sample interleaved stereo 48K (or whatever the native sample rate is)

	audioPacket livePacket = inPacket;

	if (setup.codec == 0x40 || setup.codec == 0x80) {
		/* Opus data */
		unsigned char* in = (unsigned char*)inPacket.data.data();

		/* Decode the frame. */
		QByteArray outPacket((setup.format.sampleRate() / 50) *  sizeof(qint16) * setup.format.channelCount(), (char)0xff); // Preset the output buffer size.
		qint16* out = (qint16*)outPacket.data();
		int nSamples = opus_packet_get_nb_samples(in, livePacket.data.size(),setup.format.sampleRate());
		if (nSamples == -1) {
			// No opus data yet?
			return;
		} 
		else if (nSamples != setup.format.sampleRate() / 50)
		{
			qInfo(logAudio()) << "Opus nSamples=" << nSamples << " expected:" << (setup.format.sampleRate() / 50);
			return;
		}
		if (livePacket.seq > lastSentSeq + 1) {
			nSamples = opus_decode(decoder, in, livePacket.data.size(), out, (setup.format.sampleRate() / 50), 1);
		}
		else {
			nSamples = opus_decode(decoder, in, livePacket.data.size(), out, (setup.format.sampleRate() / 50), 0);
		}
		if (nSamples < 0)
		{
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Opus decode failed:" << opus_strerror(nSamples) << "packet size" << livePacket.data.length();
			return;
		}
		else {
			if (int(nSamples * sizeof(qint16) * setup.format.channelCount()) != outPacket.size())
			{
				qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Opus decoder mismatch: nBytes:" << nSamples * sizeof(qint16) * setup.format.channelCount() << "outPacket:" << outPacket.size();
				outPacket.resize(nSamples * sizeof(qint16) * setup.format.channelCount());
			}
			//qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Opus decoded" << livePacket.data.size() << "bytes, into" << outPacket.length() << "bytes";
			livePacket.data.clear();
			livePacket.data = outPacket; // Replace incoming data with converted.
		}
	}

	// Process uLaw 
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
		setup.format.setSampleSize(16);
		setup.format.setSampleType(QAudioFormat::SignedInt);
		// Buffer now contains 16bit signed samples.
	}



	if (!livePacket.data.isEmpty()) {

		Eigen::VectorXf samplesF;
		if (setup.format.sampleSize() == 16)
		{
			VectorXint16 samplesI = Eigen::Map<VectorXint16>(reinterpret_cast<qint16*>(livePacket.data.data()), livePacket.data.size() / int(sizeof(qint16)));
			samplesF = samplesI.cast<float>();
		}
		else
		{
			VectorXuint8 samplesI = Eigen::Map<VectorXuint8>(reinterpret_cast<quint8*>(livePacket.data.data()), livePacket.data.size() / int(sizeof(quint8)));
			samplesF = samplesI.cast<float>() / float(std::numeric_limits<quint8>::max());;
		}

		// Set the max amplitude found in the vector
		amplitude = samplesF.array().abs().maxCoeff();

		// Set the volume
		samplesF *= volume;

		// Convert mono to stereo
		if (setup.format.channelCount() == 1) {
			Eigen::VectorXf samplesTemp(samplesF.size() * 2);
			Eigen::Map<Eigen::VectorXf, 0, Eigen::InnerStride<2> >(samplesTemp.data(), samplesF.size()) = samplesF;
			Eigen::Map<Eigen::VectorXf, 0, Eigen::InnerStride<2> >(samplesTemp.data() + 1, samplesF.size()) = samplesF;
			samplesF = samplesTemp;
		}


		if (format.sampleType() == QAudioFormat::SignedInt)
		{
			VectorXint16 samplesI = samplesF.cast<qint16>();
			livePacket.data = QByteArray(reinterpret_cast<char*>(samplesI.data()), int(samplesI.size()) * int(sizeof(qint16)));
		}
		else
		{
			livePacket.data = QByteArray(reinterpret_cast<char*>(samplesF.data()), int(samplesF.size()) * int(sizeof(float)));
		}


		if (resampleRatio != 1.0) {

			// We need to resample
			// We have a stereo 16bit stream.
			quint32 outFrames = ((livePacket.data.length() / 2 / devChannels) * resampleRatio);
			quint32 inFrames = (livePacket.data.length() / 2 / devChannels);
			QByteArray outPacket(outFrames * 4, (char)0xff); // Preset the output buffer size.

			const qint16* in = (qint16*)livePacket.data.constData();
			qint16* out = (qint16*)outPacket.data();

			int err = 0;
			err = wf_resampler_process_interleaved_int(resampler, in, &inFrames, out, &outFrames);
			if (err) {
				qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Resampler error " << err << " inFrames:" << inFrames << " outFrames:" << outFrames;
			}
			livePacket.data.clear();
			livePacket.data = outPacket; // Replace incoming data with converted.
		}


		//qDebug(logAudio()) << "Adding packet to buffer:" << livePacket.seq << ": " << livePacket.data.length();

		currentLatency = livePacket.time.msecsTo(QTime::currentTime());

		audioDevice->write(livePacket.data);

		if ((inPacket.seq > lastSentSeq + 1) && (setup.codec == 0x40 || setup.codec == 0x80)) {
			qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Attempting FEC on packet" << inPacket.seq << "as last is" << lastSentSeq;
			lastSentSeq = inPacket.seq;
			incomingAudio(inPacket); // Call myself again to run the packet a second time (FEC)
		}

		lastSentSeq = inPacket.seq;
	}

	return;
}

void audioHandler::changeLatency(const quint16 newSize)
{
	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Changing latency to: " << newSize << " from " << setup.latency;
	setup.latency = newSize;
	//delete ringBuf;
	//audioBuffered = false;
	//ringBuf = new wilt::Ring<audioPacket>(setup.latency + 1); // Should be customizable.
	if (!setup.isinput) {
		qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Current buffer size is" << audioOutput->bufferSize() << " " << getAudioDuration(audioOutput->bufferSize(), format) << "ms)";
		audioOutput->stop();
		audioOutput->setBufferSize(getAudioSize(setup.latency, format));
		audioDevice = audioOutput->start();
		connect(audioOutput, &QAudioOutput::destroyed, audioDevice, &QIODevice::deleteLater, Qt::UniqueConnection);
		connect(audioDevice, &QIODevice::destroyed, this, &QAudioOutput::deleteLater, Qt::UniqueConnection);
		qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "New buffer size is" << audioOutput->bufferSize() << " " << getAudioDuration(audioOutput->bufferSize(), format) << "ms)";
	}
}

int audioHandler::getLatency()
{
	return currentLatency;
}



void audioHandler::getNextAudioChunk(QByteArray& ret)
{
	audioPacket packet;
	packet.sent = 0;

	if (isInitialized && ringBuf != Q_NULLPTR && ringBuf->try_read(packet))
	{
		currentLatency = packet.time.msecsTo(QTime::currentTime());

		if (currentLatency > setup.latency) {
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Packet " << hex << packet.seq <<
				" arrived too late (increase latency!) " <<
				dec << packet.time.msecsTo(QTime::currentTime()) << "ms";
			delayedPackets++;
		}
		
		//qDebug(logAudio) << "Chunksize" << this->chunkSize << "Packet size" << packet.data.length();
		// Packet will arrive as stereo interleaved 16bit 48K
		if (resampleRatio != 1.0)
		{
			quint32 outFrames = ((packet.data.length() / 2 / devChannels) * resampleRatio);
			quint32 inFrames = (packet.data.length() / 2 / devChannels);
			QByteArray outPacket((int)outFrames * 2 * devChannels, (char)0xff);

			const qint16* in = (qint16*)packet.data.constData();
			qint16* out = (qint16*)outPacket.data();

			int err = 0;
			err = wf_resampler_process_interleaved_int(resampler, in, &inFrames, out, &outFrames);
			if (err) {
				qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Resampler error " << err << " inFrames:" << inFrames << " outFrames:" << outFrames;
			}
			//qInfo(logAudio()) << "Resampler run " << err << " inFrames:" << inFrames << " outFrames:" << outFrames;
			//qInfo(logAudio()) << "Resampler run inLen:" << packet->datain.length() << " outLen:" << packet->dataout.length();
			packet.data.clear();
			packet.data = outPacket; // Copy output packet back to input buffer.
		}

		//qDebug(logAudio()) << "Now resampled, length" << packet.data.length();

		int tempAmplitude = 0;
		// Do we need to convert mono to stereo?
		if (setup.format.channelCount() == 1 && devChannels > 1)
		{
			// Strip out right channel?
			QByteArray outPacket(packet.data.length()/2, (char)0xff);
			const qint16* in = (qint16*)packet.data.constData();
			qint16* out = (qint16*)outPacket.data();
			for (int f = 0; f < outPacket.length()/2; f++)
			{
				tempAmplitude = qMax(tempAmplitude, (int)(abs(*in) / 256));
				*out++ = *in++;
				in++; // Skip each even channel.
			}
			packet.data.clear();
			packet.data = outPacket; // Copy output packet back to input buffer.
		}
		
		//qDebug(logAudio()) << "Now mono, length" << packet.data.length();

		if (setup.codec == 0x40 || setup.codec == 0x80) 
		{
			//Are we using the opus codec?	
			qint16* in = (qint16*)packet.data.data();

			/* Encode the frame. */
			QByteArray outPacket(1275, (char)0xff); // Preset the output buffer size to MAXIMUM possible Opus frame size
			unsigned char* out = (unsigned char*)outPacket.data();

			int nbBytes = opus_encode(encoder, in, (setup.format.sampleRate() / 50), out, outPacket.length());
			if (nbBytes < 0)
			{
				qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Opus encode failed:" << opus_strerror(nbBytes);
				return;
			}
			else {
				outPacket.resize(nbBytes);
				packet.data.clear();
				packet.data = outPacket; // Replace incoming data with converted.
			}

		}
		else if (setup.format.sampleSize() == 8) 
		{

			// Do we need to convert 16-bit to 8-bit?
			QByteArray outPacket((int)packet.data.length() / 2, (char)0xff);
			qint16* in = (qint16*)packet.data.data();
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
				else {
					int compressedByte = (((sample + 32768) >> 8) & 0xff);
					outPacket[f] = (quint8)compressedByte;
				}
				tempAmplitude = qMax(tempAmplitude, abs(outPacket[f]));
			}
			packet.data.clear();
			packet.data = outPacket; // Copy output packet back to input buffer.
		} 
		amplitude = tempAmplitude;

		ret = packet.data;
		//qDebug(logAudio()) << "Now radio format, length" << packet.data.length();
		if (delayedPackets > 10) {
			qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Too many delayed packets, flushing buffer";
			while (ringBuf->try_read(packet)); // Empty buffer
			delayedPackets = 0;
		}

	}

	return;

}


#if !defined (RTAUDIO) && !defined(PORTAUDIO)

qint64 audioHandler::bytesAvailable() const
{
	return 0;
}

bool audioHandler::isSequential() const
{
	return true;
}

void audioHandler::notified()
{
}


void audioHandler::stateChanged(QAudio::State state)
{
	// Process the state
	switch (state)
	{
		case QAudio::IdleState:
		{
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Audio now in idle state: " << audioBuffer.size() << " packets in buffer";
			if (audioOutput != Q_NULLPTR && audioOutput->error() == QAudio::UnderrunError)
			{
				qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "buffer underrun";
				//audioOutput->suspend();
			}
			break;
		}
		case QAudio::ActiveState:
		{
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Audio now in active state: " << audioBuffer.size() << " packets in buffer";
			break;
		}
		case QAudio::SuspendedState:
		{
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Audio now in suspended state: " << audioBuffer.size() << " packets in buffer";
			break;
		}
		case QAudio::StoppedState:
		{
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Audio now in stopped state: " << audioBuffer.size() << " packets in buffer";
			break;
		}
		default: {
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Unhandled audio state: " << audioBuffer.size() << " packets in buffer";
		}
	}
}

void audioHandler::stop()
{
	if (audioOutput != Q_NULLPTR && audioOutput->state() != QAudio::StoppedState) {
		// Stop audio output
		audioOutput->stop();
		this->stop();
		this->close();
		delete audioOutput;
		audioOutput = Q_NULLPTR;
	}

	if (audioInput != Q_NULLPTR && audioInput->state() != QAudio::StoppedState) {
		// Stop audio output
		audioInput->stop();
		this->stop();
		this->close();
		delete audioInput;
		audioInput = Q_NULLPTR;
	}
	isInitialized = false;
}

#endif

quint16 audioHandler::getAmplitude()
{
	return *reinterpret_cast<quint16*>(&amplitude);
}

