#include "pahandler.h"

#include "logcategories.h"

#if defined(Q_OS_WIN)
#include <objbase.h>
#endif


paHandler::paHandler(QObject* parent)
{
	Q_UNUSED(parent)
}

paHandler::~paHandler()
{

	if (converterThread != Q_NULLPTR) {
		converterThread->quit();
		converterThread->wait();
	}

	if (isInitialized) {
		Pa_StopStream(audio);
		Pa_CloseStream(audio);
	}
	
}

bool paHandler::init(audioSetup setup)
{
	if (isInitialized) {
		return false;
	}

	this->setup = setup;
	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "PortAudio handler starting:" << setup.name;

	if (setup.portInt == -1)
	{
		qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "No audio device was found.";
		return false;
	}

	radioFormat = toQAudioFormat(setup.codec, setup.sampleRate);

	qDebug(logAudio()) << "Creating" << (setup.isinput ? "Input" : "Output") << "audio device:" << setup.name <<
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
		", bits" << radioFormat.sampleSize() <<
#else
		", format" << radioFormat.sampleFormat() <<
#endif
		", codec" << setup.codec <<
		", latency" << setup.latency <<
		", localAFGain" << setup.localAFgain <<
		", radioChan" << radioFormat.channelCount() <<
		", resampleQuality" << setup.resampleQuality <<
		", samplerate" << radioFormat.sampleRate() <<
		", uLaw" << setup.ulaw;

	PaError err;
#ifdef Q_OS_WIN
	CoInitialize(0);
#endif

	//err = Pa_Initialize();
	//if (err != paNoError)
	//{
	//	qDebug(logAudio()) << "Portaudio initialized";
	//}


	codecType codec = LPCM;
	if (setup.codec == 0x01 || setup.codec == 0x20)
		codec = PCMU;
	else if (setup.codec == 0x40 || setup.codec == 0x40)
		codec = OPUS;

	memset(&aParams, 0, sizeof(PaStreamParameters));

	aParams.device = setup.portInt;
	info = Pa_GetDeviceInfo(aParams.device);

	qDebug(logAudio()) << "PortAudio" << (setup.isinput ? "Input" : "Output") << setup.portInt << "Input Channels" << info->maxInputChannels << "Output Channels" << info->maxOutputChannels;


	if (setup.isinput) {
		nativeFormat.setChannelCount(info->maxInputChannels);
	}
	else {
		nativeFormat.setChannelCount(info->maxOutputChannels);
	}

	aParams.suggestedLatency = (float)setup.latency / 1000.0f;
	nativeFormat.setSampleRate(info->defaultSampleRate);
	aParams.sampleFormat = paFloat32;

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
	nativeFormat.setSampleSize(32);
	nativeFormat.setSampleType(QAudioFormat::Float);
	nativeFormat.setByteOrder(QAudioFormat::LittleEndian);
	nativeFormat.setCodec("audio/pcm");
#else
	nativeFormat.setSampleFormat(QAudioFormat::Float);
#endif

	if (nativeFormat.channelCount() > 2) {
		nativeFormat.setChannelCount(2);
	}
	else if (nativeFormat.channelCount() < 1)
	{
		qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "No channels found, aborting setup.";
		return false;
	}

	if (nativeFormat.channelCount() == 1 && radioFormat.channelCount() == 2) {
		nativeFormat.setChannelCount(2);
	}

	aParams.channelCount = nativeFormat.channelCount();

	if (nativeFormat.sampleRate() < 44100) {
		nativeFormat.setSampleRate(48000);
	}


#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
	qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Selected format: SampleSize" << nativeFormat.sampleSize() << "Channel Count" << nativeFormat.channelCount() <<
		"Sample Rate" << nativeFormat.sampleRate() << "Codec" << codec << "Sample Type" << nativeFormat.sampleType();
#else
	qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Selected format: SampleFormat" << nativeFormat.sampleFormat() << "Channel Count" << nativeFormat.channelCount() <<
		"Sample Rate" << nativeFormat.sampleRate() << "Codec" << codec;
#endif

	// We "hopefully" now have a valid format that is supported so try connecting

	converter = new audioConverter();
	converterThread = new QThread(this);
	if (setup.isinput) {
		converterThread->setObjectName("audioConvIn()");
	}
	else {
		converterThread->setObjectName("audioConvOut()");
	}
	converter->moveToThread(converterThread);

	connect(this, SIGNAL(setupConverter(QAudioFormat, codecType, QAudioFormat, codecType, quint8, quint8)), converter, SLOT(init(QAudioFormat, codecType, QAudioFormat, codecType, quint8, quint8)));
	connect(converterThread, SIGNAL(finished()), converter, SLOT(deleteLater()));
	connect(this, SIGNAL(sendToConverter(audioPacket)), converter, SLOT(convert(audioPacket)));
	converterThread->start(QThread::TimeCriticalPriority);

	aParams.hostApiSpecificStreamInfo = NULL;

	// Per channel chunk size.
	this->chunkSize = nativeFormat.framesForDuration(setup.blockSize * 1000);

	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Chunk size" << this->chunkSize;
	// Check the format is supported



	err = -1;
	int errCount = 0;
	while (err != paNoError) {
		if (setup.isinput) {
			err = Pa_IsFormatSupported(&aParams, NULL, nativeFormat.sampleRate());
		}
		else
		{
			err = Pa_IsFormatSupported(NULL, &aParams, nativeFormat.sampleRate());
		}
		if (err == paInvalidChannelCount)
		{
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Unsupported channel count" << aParams.channelCount;
			if (aParams.channelCount == 2) {
				aParams.channelCount = 1;
				nativeFormat.setChannelCount(1);
			}
			else {
				aParams.channelCount = 2;
				nativeFormat.setChannelCount(2);
			}
		}
		if (err == paInvalidSampleRate)
		{
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Unsupported sample rate" << nativeFormat.sampleRate();
			nativeFormat.setSampleRate(44100);
		}
		if (err == paSampleFormatNotSupported)
		{
			aParams.sampleFormat = paInt16;
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Unsupported sample Format" << nativeFormat.sampleType();
			nativeFormat.setSampleType(QAudioFormat::SignedInt);
			nativeFormat.setSampleSize(16);
#else
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Unsupported sample Format" << nativeFormat.sampleFormat();
			nativeFormat.setSampleFormat(QAudioFormat::Int16);
#endif
		}

		errCount++;
		if (errCount > 5) {
			qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "Cannot find suitable format, aborting:" << Pa_GetErrorText(err);
			return false;
		}
	}

	if (setup.isinput) {

		err = Pa_OpenStream(&audio, &aParams, 0, nativeFormat.sampleRate(), this->chunkSize, paNoFlag, &paHandler::staticWrite, (void*)this);
		emit setupConverter(nativeFormat, codecType::LPCM, radioFormat, codec, 7, setup.resampleQuality);
		connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(convertedInput(audioPacket)));
	}
	else {
		err = Pa_OpenStream(&audio, 0, &aParams, nativeFormat.sampleRate(), this->chunkSize, paNoFlag, NULL, NULL);
		emit setupConverter(radioFormat, codec, nativeFormat, codecType::LPCM, 7, setup.resampleQuality);
		connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(convertedOutput(audioPacket)));
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

	this->setVolume(setup.localAFgain);

	return isInitialized;
}


void paHandler::setVolume(unsigned char volume)
{
#ifdef Q_OS_WIN
	this->volume = audiopot[volume] * 5;
#else
	this->volume = audiopot[volume];
#endif
}

void paHandler::incomingAudio(audioPacket packet)
{
	packet.volume = volume;
	if (Pa_IsStreamActive(audio) == 1) {
		emit sendToConverter(packet);
	}
	else
	{
		Pa_StartStream(audio);
	}
	return;
}



int paHandler::writeData(const void* inputBuffer, void* outputBuffer,
	unsigned long nFrames, const PaStreamCallbackTimeInfo * streamTime,
	PaStreamCallbackFlags status)
{
	Q_UNUSED(outputBuffer);
	Q_UNUSED(streamTime);
	Q_UNUSED(status);
	audioPacket packet;
	packet.time = QTime::currentTime();
	packet.sent = 0;
	packet.volume = volume;
	memcpy(&packet.guid, setup.guid, GUIDLEN);
	packet.data.append((char*)inputBuffer, nFrames*nativeFormat.bytesPerFrame());
	emit sendToConverter(packet);

	if (status == paInputUnderflow) {
		isUnderrun = true;
	}
	else if (status == paInputOverflow) {
		isOverrun = true;
	}
	else
	{
		isUnderrun = false;
		isOverrun = false;
	}

	return paContinue;
}


void paHandler::convertedOutput(audioPacket packet) {

	if (packet.data.size() > 0) {

		if (Pa_IsStreamActive(audio) == 1) {
			PaError err = Pa_WriteStream(audio, (char*)packet.data.data(), packet.data.size() / nativeFormat.bytesPerFrame());

			if (err != paNoError) {
				qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Error writing audio!";
			}
			const PaStreamInfo* info = Pa_GetStreamInfo(audio);
			currentLatency = packet.time.msecsTo(QTime::currentTime()) + (info->outputLatency * 1000);
		}

		amplitude = packet.amplitudePeak;
        emit haveLevels(getAmplitude(), static_cast<quint16>(packet.amplitudeRMS * 255.0), setup.latency, currentLatency, isUnderrun, isOverrun);
	}
}



void paHandler::convertedInput(audioPacket packet)
{
	if (packet.data.size() > 0) {
		emit haveAudioData(packet);
		amplitude = packet.amplitudePeak;
		const PaStreamInfo* info = Pa_GetStreamInfo(audio);
		currentLatency = packet.time.msecsTo(QTime::currentTime()) + (info->inputLatency * 1000);
        emit haveLevels(getAmplitude(), packet.amplitudeRMS, setup.latency, currentLatency, isUnderrun, isOverrun);
	}
}



void paHandler::changeLatency(const quint16 newSize)
{
	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Changing latency to: " << newSize << " from " << setup.latency;
	setup.latency = newSize;
	latencyAllowance = 0;
}

int paHandler::getLatency()
{
	return currentLatency;
}


quint16 paHandler::getAmplitude()
{
	return static_cast<quint16>(amplitude * 255.0);
}
