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

	inFormat = toQAudioFormat(setup.codec, setup.sampleRate);

	qDebug(logAudio()) << "Creating" << (setup.isinput ? "Input" : "Output") << "audio device:" << setup.name <<
		", bits" << inFormat.sampleSize() <<
		", codec" << setup.codec <<
		", latency" << setup.latency <<
		", localAFGain" << setup.localAFgain <<
		", radioChan" << inFormat.channelCount() <<
		", resampleQuality" << setup.resampleQuality <<
		", samplerate" << inFormat.sampleRate() <<
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

	memset(&aParams, 0, sizeof(PaStreamParameters));

	aParams.device = setup.portInt;
	info = Pa_GetDeviceInfo(aParams.device);

	qDebug(logAudio()) << "PortAudio" << (setup.isinput ? "Input" : "Output") << setup.portInt << "Input Channels" << info->maxInputChannels << "Output Channels" << info->maxOutputChannels;


	if (setup.isinput) {
		outFormat.setChannelCount(info->maxInputChannels);
	}
	else {
		outFormat.setChannelCount(info->maxOutputChannels);
	}

	aParams.suggestedLatency = (float)setup.latency / 1000.0f;
	outFormat.setSampleRate(info->defaultSampleRate);
	aParams.sampleFormat = paFloat32;
	outFormat.setSampleSize(32);
	outFormat.setSampleType(QAudioFormat::Float);
	outFormat.setByteOrder(QAudioFormat::LittleEndian);
	outFormat.setCodec("audio/pcm");


	if (outFormat.channelCount() > 2) {
		outFormat.setChannelCount(2);
	}
	else if (outFormat.channelCount() < 1)
	{
		qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "No channels found, aborting setup.";
		return false;
	}

	if (inFormat.channelCount() < outFormat.channelCount()) {
		outFormat.setChannelCount(inFormat.channelCount());
	}

	aParams.channelCount = outFormat.channelCount();

	if (outFormat.sampleRate() < 44100) {
		outFormat.setSampleRate(48000);
	}


	qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Selected format: SampleSize" << outFormat.sampleSize() << "Channel Count" << outFormat.channelCount() <<
		"Sample Rate" << outFormat.sampleRate() << "Codec" << outFormat.codec() << "Sample Type" << outFormat.sampleType();

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

	connect(this, SIGNAL(setupConverter(QAudioFormat, QAudioFormat, quint8, quint8)), converter, SLOT(init(QAudioFormat, QAudioFormat, quint8, quint8)));
	connect(converterThread, SIGNAL(finished()), converter, SLOT(deleteLater()));
	connect(this, SIGNAL(sendToConverter(audioPacket)), converter, SLOT(convert(audioPacket)));
	converterThread->start(QThread::TimeCriticalPriority);

	aParams.hostApiSpecificStreamInfo = NULL;

	// Per channel chunk size.
	this->chunkSize = (outFormat.bytesForDuration(setup.blockSize * 1000) / sizeof(float)) * outFormat.channelCount();

	// Check the format is supported


	if (setup.isinput) {
		err = Pa_IsFormatSupported(&aParams, NULL, outFormat.sampleRate());
	}
	else
	{
		err = Pa_IsFormatSupported(NULL,&aParams, outFormat.sampleRate());
	}

	if (err != paNoError) {
		if (err == paInvalidChannelCount)
		{
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Unsupported channel count" << aParams.channelCount;
			if (aParams.channelCount == 2) {
				aParams.channelCount = 1;
				outFormat.setChannelCount(1);
			}
			else {
				aParams.channelCount = 2;
				outFormat.setChannelCount(2);
			}
		}
		else if (err == paInvalidSampleRate)
		{
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Unsupported sample rate" << outFormat.sampleRate();
			outFormat.setSampleRate(44100);
		}
		else if (err == paSampleFormatNotSupported)
		{
			aParams.sampleFormat = paInt16;
			outFormat.setSampleType(QAudioFormat::SignedInt);
			outFormat.setSampleSize(16);
		}

		if (setup.isinput) {
			err = Pa_IsFormatSupported(&aParams, NULL, outFormat.sampleRate());
		}
		else
		{
			err = Pa_IsFormatSupported(NULL, &aParams, outFormat.sampleRate());
		}
		if (err != paNoError) {
			qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "Cannot find suitable format, aborting:" << Pa_GetErrorText(err);
			return false;
		}
	}

	if (setup.isinput) {

		err = Pa_OpenStream(&audio, &aParams, 0, outFormat.sampleRate(), this->chunkSize, paNoFlag, &paHandler::staticWrite, (void*)this);
		emit setupConverter(outFormat, inFormat, 7, setup.resampleQuality);
		connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(convertedInput(audioPacket)));
	}
	else {
		err = Pa_OpenStream(&audio, 0, &aParams, outFormat.sampleRate(), this->chunkSize, paNoFlag, NULL, NULL);
		emit setupConverter(inFormat, outFormat, 7, setup.resampleQuality);
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

	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "setVolume: " << volume << "(" << this->volume << ")";
}

void paHandler::incomingAudio(audioPacket packet)
{
	packet.volume = volume;
	emit sendToConverter(packet);
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
	packet.data.append((char*)inputBuffer, nFrames*inFormat.channelCount()*sizeof(float));
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
			PaError err = Pa_WriteStream(audio, (char*)packet.data.data(), packet.data.size() / sizeof(float) / outFormat.channelCount());

			if (err != paNoError) {
				qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Error writing audio!";
			}
			const PaStreamInfo* info = Pa_GetStreamInfo(audio);
			currentLatency = packet.time.msecsTo(QTime::currentTime()) + (info->outputLatency * 1000);
		}

		amplitude = packet.amplitude;
		emit haveLevels(getAmplitude(), setup.latency, currentLatency, isUnderrun, isOverrun);
	}
}



void paHandler::convertedInput(audioPacket packet)
{
	if (packet.data.size() > 0) {
		emit haveAudioData(packet);
		amplitude = packet.amplitude;
		const PaStreamInfo* info = Pa_GetStreamInfo(audio);
		currentLatency = packet.time.msecsTo(QTime::currentTime()) + (info->inputLatency * 1000);
		emit haveLevels(getAmplitude(), setup.latency, currentLatency, isUnderrun, isOverrun);
	}
}



void paHandler::changeLatency(const quint16 newSize)
{
	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Changing latency to: " << newSize << " from " << setup.latency;
}

int paHandler::getLatency()
{
	return currentLatency;
}


quint16 paHandler::getAmplitude()
{
	return static_cast<quint16>(amplitude * 255.0);
}
