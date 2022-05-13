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

	if (isInitialized) {
		Pa_StopStream(audio);
		Pa_CloseStream(audio);
	}

	if (converterThread != Q_NULLPTR) {
		converterThread->quit();
		converterThread->wait();
	}

	//Pa_Terminate();
	
}

bool paHandler::init(audioSetup setup)
{
	if (isInitialized) {
		return false;
	}

	this->setup = setup;
	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "PortAudio handler starting:" << setup.name;

	if (setup.portInt==-1)
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

	aParams.suggestedLatency = (float)setup.latency/1000.0f;
	outFormat.setSampleRate(info->defaultSampleRate);
	aParams.sampleFormat = paFloat32;
	outFormat.setSampleSize(32);
	outFormat.setSampleType(QAudioFormat::Float);
	outFormat.setByteOrder(QAudioFormat::LittleEndian);
	outFormat.setCodec("audio/pcm");


	if (!setup.isinput)
	{
		this->setVolume(setup.localAFgain);
	}

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
	this->chunkSize = (outFormat.bytesForDuration(setup.blockSize*1000)/sizeof(float))*outFormat.channelCount();


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

	this->volume = audiopot[volume];

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

			//currentLatency = packet.time.msecsTo(QTime::currentTime()) + (info->outputLatency * 1000);
			currentLatency = (info->outputLatency * 1000);

		}
		/*
		currentLatency = packet.time.msecsTo(QTime::currentTime()) + (outFormat.durationForBytes(audioOutput->bufferSize() - audioOutput->bytesFree()) / 1000);

		if (audioDevice != Q_NULLPTR) {
			if (audioDevice->write(packet.data) < packet.data.size()) {
				qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Buffer full!";
				isOverrun = true;
			}
			else {
				isOverrun = false;
			}
			if (lastReceived.msecsTo(QTime::currentTime()) > 100) {
				qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Time since last audio packet" << lastReceived.msecsTo(QTime::currentTime()) << "Expected around" << setup.blockSize;
			}
			lastReceived = QTime::currentTime();
		}

		lastSentSeq = packet.seq;

		*/
		amplitude = packet.amplitude;
		emit haveLevels(getAmplitude(), setup.latency, currentLatency, false, false);
	}
}



void paHandler::convertedInput(audioPacket audio)
{
	if (audio.data.size() > 0) {
		emit haveAudioData(audio);
		amplitude = audio.amplitude;
		emit haveLevels(getAmplitude(), setup.latency, currentLatency, false,false);
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
	return amplitude;
}
