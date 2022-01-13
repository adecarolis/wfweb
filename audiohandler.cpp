/*
	This class handles both RX and TX audio, each is created as a seperate instance of the class
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
	setup.radioChan = 1;
	setup.bits = 8;

	if (setup.codec == 0x01 || setup.codec == 0x20) {
		setup.ulaw = true;
	}
	if (setup.codec == 0x08 || setup.codec == 0x10 || setup.codec == 0x20 || setup.codec == 0x80) {
		setup.radioChan = 2;
	}
	if (setup.codec == 0x04 || setup.codec == 0x10 || setup.codec == 0x40 || setup.codec == 0x80) {
		setup.bits = 16;
	}

	ringBuf = new wilt::Ring<audioPacket>(setupIn.latency / 8 + 1); // Should be customizable.

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

#ifdef Q_OS_MAC
        audioOutput->setBufferSize(chunkSize*4);
#endif

		connect(audioOutput, SIGNAL(notify()), SLOT(notified()));
		connect(audioOutput, SIGNAL(stateChanged(QAudio::State)), SLOT(stateChanged(QAudio::State)));
		isInitialized = true;
	}

#endif
	// Setup resampler and opus if they are needed.
	int resample_error = 0;
	int opus_err = 0;
	if (setup.isinput) {
		resampler = wf_resampler_init(devChannels, nativeSampleRate, setup.samplerate, setup.resampleQuality, &resample_error);
		if (setup.codec == 0x40 || setup.codec == 0x80) {
			// Opus codec
			encoder = opus_encoder_create(setup.samplerate, setup.radioChan, OPUS_APPLICATION_AUDIO, &opus_err);
			opus_encoder_ctl(encoder, OPUS_SET_LSB_DEPTH(16));
			opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
			opus_encoder_ctl(encoder, OPUS_SET_DTX(1));
			opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(5));
			qInfo(logAudio()) << "Creating opus encoder: " << opus_strerror(opus_err);
		}
	}
	else {
		resampler = wf_resampler_init(devChannels, setup.samplerate, this->nativeSampleRate, setup.resampleQuality, &resample_error);
		if (setup.codec == 0x40 || setup.codec == 0x80) {
			// Opus codec
			decoder = opus_decoder_create(setup.samplerate, setup.radioChan, &opus_err);
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
		this->open(QIODevice::WriteOnly | QIODevice::Unbuffered);
#endif
		audioInput->start(this);
	}
	else {
#ifndef Q_OS_WIN
		this->open(QIODevice::ReadOnly);
#else
		this->open(QIODevice::ReadOnly | QIODevice::Unbuffered);
#endif
		audioOutput->start(this);
	}
}
#endif

void audioHandler::setVolume(unsigned char volume)
{
    //this->volume = (qreal)volume/255.0;
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
	if (ringBuf->size()>0)
	{
		// Output buffer is ALWAYS 16 bit.
		//qDebug(logAudio()) << "Read: nFrames" << nFrames << "nBytes" << nBytes;
		while (sentlen < nBytes)
		{
			audioPacket packet;
			if (!ringBuf->try_read(packet))
			{
				qDebug(logAudio()) << "No more data available but buffer is not full! sentlen:" << sentlen << " nBytes:" << nBytes ;
				break;
			}
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
				while (currentLatency > setup.latency/2) {
					if (!ringBuf->try_read(packet)) {
						break;
					}
					currentLatency = packet.time.msecsTo(QTime::currentTime());
				}
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

			/*
			if (packet.seq <= lastSeq) {
				qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Duplicate/early audio packet: " << hex << lastSeq << " got " << hex << packet.seq;
			}
			else if (packet.seq != lastSeq + 1) {
				qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Missing audio packet(s) from: " << hex << lastSeq + 1 << " to " << hex << packet.seq - 1;
			}
			*/
			lastSeq = packet.seq;
		}
	}
    //qDebug(logAudio()) << "looking for: " << nBytes << " got: " << sentlen;

    // fill the rest of the buffer with silence
    if (nBytes > sentlen) {
        memset(buffer+sentlen,0,nBytes-sentlen);
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

	if (!isInitialized && !isReady)
	{
		qDebug(logAudio()) << "Packet received when stream was not ready";
		return;
	}

	audioPacket livePacket = inPacket;

	if (setup.codec == 0x40 || setup.codec == 0x80) {
		unsigned char* in = (unsigned char*)inPacket.data.data();

		/* Decode the frame. */
		QByteArray outPacket((setup.samplerate / 50) *  sizeof(qint16) * setup.radioChan, (char)0xff); // Preset the output buffer size.
		qint16* out = (qint16*)outPacket.data();
		int nSamples = opus_packet_get_nb_samples(in, livePacket.data.size(),setup.samplerate);
		if (nSamples == -1) {
			// No opus data yet?
			return;
		} 
		else if (nSamples != setup.samplerate / 50)
		{
			qInfo(logAudio()) << "Opus nSamples=" << nSamples << " expected:" << (setup.samplerate / 50);
			return;
		}
		if (livePacket.seq > lastSentSeq + 1) {
			nSamples = opus_decode(decoder, in, livePacket.data.size(), out, (setup.samplerate / 50), 1);
		}
		else {
			nSamples = opus_decode(decoder, in, livePacket.data.size(), out, (setup.samplerate / 50), 0);
		}
		if (nSamples < 0)
		{
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Opus decode failed:" << opus_strerror(nSamples) << "packet size" << livePacket.data.length();
			return;
		}
		else {
			if (int(nSamples * sizeof(qint16) * setup.radioChan) != outPacket.size())
			{
				qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Opus decoder mismatch: nBytes:" << nSamples * sizeof(qint16) * setup.radioChan << "outPacket:" << outPacket.size();
				outPacket.resize(nSamples * sizeof(qint16) * setup.radioChan);
			}
			//qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Opus decoded" << livePacket.data.size() << "bytes, into" << outPacket.length() << "bytes";
			livePacket.data.clear();
			livePacket.data = outPacket; // Replace incoming data with converted.
		}
	}

    //qDebug(logAudio()) << "Got" << setup.bits << "bits, length" << livePacket.data.length();
	// Incoming data is 8bits?
	if (setup.bits == 8)
	{
		// Current packet is 8bit so need to create a new buffer that is 16bit 
		QByteArray outPacket((int)livePacket.data.length() * 2 * (devChannels / setup.radioChan), (char)0xff);
		qint16* out = (qint16*)outPacket.data();
		for (int f = 0; f < livePacket.data.length(); f++)
		{
			int samp = (quint8)livePacket.data[f];
			for (int g = setup.radioChan; g <= devChannels; g++)
			{
				if (setup.ulaw)
					*out++ = ulaw_decode[samp] * this->volume;
				else
					*out++ = (qint16)((samp - 128) << 8) * this->volume;
			}
		}
		livePacket.data.clear();
		livePacket.data = outPacket; // Replace incoming data with converted.
	}
	else 
	{
		// This is already a 16bit stream, do we need to convert to stereo?
		if (setup.radioChan == 1 && devChannels > 1) {
			// Yes
			QByteArray outPacket(livePacket.data.length() * 2, (char)0xff); // Preset the output buffer size.
			qint16* in = (qint16*)livePacket.data.data();
			qint16* out = (qint16*)outPacket.data();
			for (int f = 0; f < livePacket.data.length() / 2; f++)
			{
				*out++ = (qint16)*in * this->volume;
				*out++ = (qint16)*in++ * this->volume;
			}
			livePacket.data.clear();
			livePacket.data = outPacket; // Replace incoming data with converted.
		}
		else 
		{
			// We already have the same number of channels so just update volume.
			qint16* in = (qint16*)livePacket.data.data();
			for (int f = 0; f < livePacket.data.length() / 2; f++)
			{
                *in = *in * this->volume;
                in++;
			}
		}

	}

	/*	We now have an array of 16bit samples in the NATIVE samplerate of the radio
		If the radio sample rate is below 48000, we need to resample.
		*/
    //qDebug(logAudio()) << "Now 16 bit stereo, length" << livePacket.data.length();

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

	if (!ringBuf->try_write(livePacket))
	{
		qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Buffer full! capacity:" << ringBuf->capacity() << "length" << ringBuf->size();
	}
	if ((inPacket.seq > lastSentSeq + 1) && (setup.codec == 0x40 || setup.codec == 0x80)) {
		qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Attempting FEC on packet" << inPacket.seq << "as last is"<<lastSentSeq ;
		lastSentSeq = inPacket.seq;
		incomingAudio(inPacket); // Call myself again to run the packet a second time (FEC)
	}
	lastSentSeq = inPacket.seq;
	return;
}

void audioHandler::changeLatency(const quint16 newSize)
{
	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Changing latency to: " << newSize << " from " << setup.latency;
	setup.latency = newSize;
	delete ringBuf;
	ringBuf = new wilt::Ring<audioPacket>(setup.latency / 8 + 1); // Should be customizable.
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
		//	if (!ringBuf->try_read(packet))
		//		break;
		//	currentLatency = packet.time.msecsTo(QTime::currentTime());
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

		// Do we need to convert mono to stereo?
		if (setup.radioChan == 1 && devChannels > 1)
		{
			// Strip out right channel?
			QByteArray outPacket(packet.data.length()/2, (char)0xff);
			const qint16* in = (qint16*)packet.data.constData();
			qint16* out = (qint16*)outPacket.data();
			for (int f = 0; f < outPacket.length()/2; f++)
			{
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

			int nbBytes = opus_encode(encoder, in, (setup.samplerate / 50), out, outPacket.length());
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
		else if (setup.bits == 8) 
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
			}
			packet.data.clear();
			packet.data = outPacket; // Copy output packet back to input buffer.
		} 

		ret = packet.data;
		//qDebug(logAudio()) << "Now radio format, length" << packet.data.length();
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


