#include "rthandler.h"

#include "logcategories.h"

#if defined(Q_OS_WIN)
#include <objbase.h>
#endif


rtHandler::rtHandler(QObject* parent)
{
	Q_UNUSED(parent)
}

rtHandler::~rtHandler()
{

	if (isInitialized) {
		try {
			audio->abortStream();
			audio->closeStream();
		}
		catch (RtAudioError& e) {
			qInfo(logAudio()) << "Error closing stream:" << aParams.deviceId << ":" << QString::fromStdString(e.getMessage());
		}
		delete audio;

	}

	if (converterThread != Q_NULLPTR) {
		converterThread->quit();
		converterThread->wait();
	}
	
}

bool rtHandler::init(audioSetup setup)
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

#if !defined(Q_OS_MACX)
	options.flags = ((!RTAUDIO_HOG_DEVICE) | (RTAUDIO_MINIMIZE_LATENCY));
	//options.flags = RTAUDIO_MINIMIZE_LATENCY;
#endif

#if defined(Q_OS_LINUX)
	audio = new RtAudio(RtAudio::Api::LINUX_ALSA);
#elif defined(Q_OS_WIN)
	audio = new RtAudio(RtAudio::Api::WINDOWS_WASAPI);
#elif defined(Q_OS_MACX)
	audio = new RtAudio(RtAudio::Api::MACOSX_CORE);
#endif

	options.numberOfBuffers = int(setup.latency/setup.blockSize);

	if (setup.portInt > 0) {
		aParams.deviceId = setup.portInt;
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
		qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << QString::fromStdString(info.name) << "(" << aParams.deviceId << ") successfully probed";

		RtAudioFormat sampleFormat;
		outFormat.setByteOrder(QAudioFormat::LittleEndian);
		outFormat.setCodec("audio/pcm");

		if (info.nativeFormats == 0)
		{
			qCritical(logAudio()) << "		No natively supported data formats!";
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
				outFormat.setChannelCount(info.inputChannels);
			}
			else {
				outFormat.setChannelCount(info.outputChannels);
			}
			
			qInfo(logAudio()) << "		Channels:" << outFormat.channelCount();
			
			if (outFormat.channelCount() > 2) {
				outFormat.setChannelCount(2);
			}
			else if (outFormat.channelCount() < 1)
			{
				qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "No channels found, aborting setup.";
				return false;
			}

			aParams.nChannels = outFormat.channelCount();


			outFormat.setSampleRate(info.preferredSampleRate);

			if (outFormat.sampleRate() < 44100) {
				outFormat.setSampleRate(48000);				
			}

			if (info.nativeFormats & RTAUDIO_FLOAT32) {
				outFormat.setSampleType(QAudioFormat::Float);
				outFormat.setSampleSize(32);
				sampleFormat = RTAUDIO_FLOAT32;
			}
			else if (info.nativeFormats & RTAUDIO_SINT32) {
				outFormat.setSampleType(QAudioFormat::SignedInt);
				outFormat.setSampleSize(32);
				sampleFormat = RTAUDIO_SINT32;
			}
			else if (info.nativeFormats & RTAUDIO_SINT16) {
				outFormat.setSampleType(QAudioFormat::SignedInt);
				outFormat.setSampleSize(16);
				sampleFormat = RTAUDIO_SINT16;
			}
			else {
				qCritical(logAudio()) << "Cannot find supported sample format!";
				return false;
			}
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


		// Per channel chunk size.
		this->chunkSize = (outFormat.bytesForDuration(setup.blockSize * 1000) / (outFormat.sampleSize()/8) / outFormat.channelCount());

		try {
			if (setup.isinput) {
				audio->openStream(NULL, &aParams, sampleFormat, outFormat.sampleRate(), &this->chunkSize, &staticWrite, this, &options);
				emit setupConverter(outFormat, inFormat, 7, setup.resampleQuality);
				connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(convertedInput(audioPacket)));
			}
			else {
				audio->openStream(&aParams, NULL, sampleFormat, outFormat.sampleRate(), &this->chunkSize, &staticRead, this , &options);
				emit setupConverter(inFormat, outFormat, 7, setup.resampleQuality);
				connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(convertedOutput(audioPacket)));
			}
			audio->startStream();
			isInitialized = true;
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "device successfully opened";
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "detected latency:" << audio->getStreamLatency();
		}
		catch (RtAudioError& e) {
			qInfo(logAudio()) << "Error opening:" << QString::fromStdString(e.getMessage());
			// Try again?
			if (retryConnectCount < 10) {
				QTimer::singleShot(500, this, std::bind(&rtHandler::init, this, setup));
				retryConnectCount++;
				return false;
			}
		}
	}
	else
	{
		qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << QString::fromStdString(info.name) << "(" << aParams.deviceId << ") could not be probed, check audio configuration!";
	}

	this->setVolume(setup.localAFgain);

	
	return isInitialized;
}


void rtHandler::setVolume(unsigned char volume)
{

	this->volume = audiopot[volume];

	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "setVolume: " << volume << "(" << this->volume << ")";
}

void rtHandler::incomingAudio(audioPacket packet)
{
	packet.volume = volume;
	emit sendToConverter(packet);
	return;
}

int rtHandler::readData(void* outputBuffer, void* inputBuffer,
	unsigned int nFrames, double streamTime, RtAudioStreamStatus status)
{
	Q_UNUSED(inputBuffer);
	Q_UNUSED(streamTime);
	int nBytes = nFrames * outFormat.channelCount() * (outFormat.sampleSize()/8); 

	//lastSentSeq = packet.seq;
	if (arrayBuffer.length() >= nBytes) {
		if (audioMutex.tryLock(0)) {
			std::memcpy(outputBuffer, arrayBuffer.constData(), nBytes);
			arrayBuffer.remove(0, nBytes);
			audioMutex.unlock();
		}
	}

	if (status == RTAUDIO_INPUT_OVERFLOW) {
		isUnderrun = true;
	}
	else if (status == RTAUDIO_OUTPUT_UNDERFLOW) {
		isOverrun = true;
	}
	else
	{
		isUnderrun = false;
		isOverrun = false;
	}
	return 0;
}


int rtHandler::writeData(void* outputBuffer, void* inputBuffer,
	unsigned int nFrames, double streamTime, RtAudioStreamStatus status)
{
	Q_UNUSED(outputBuffer);
	Q_UNUSED(streamTime);
	Q_UNUSED(status);
	audioPacket packet;
	packet.time = QTime::currentTime();
	packet.sent = 0;
	packet.volume = volume;
	memcpy(&packet.guid, setup.guid, GUIDLEN);
	packet.data.append((char*)inputBuffer, nFrames  *outFormat.channelCount() * (outFormat.sampleSize()/8));
	emit sendToConverter(packet);
	if (status == RTAUDIO_INPUT_OVERFLOW) {
		isUnderrun = true;
	}
	else if (status == RTAUDIO_OUTPUT_UNDERFLOW) {
		isOverrun = true;
	}
	else
	{
		isUnderrun = false;
		isOverrun = false;
	}

	return 0;
}


void rtHandler::convertedOutput(audioPacket packet) 
{
	audioMutex.lock();
	arrayBuffer.append(packet.data);
	audioMutex.unlock();
	amplitude = packet.amplitude;
	currentLatency = packet.time.msecsTo(QTime::currentTime()) + (outFormat.durationForBytes(audio->getStreamLatency() * (outFormat.sampleSize() / 8) * outFormat.channelCount())/1000);
	emit haveLevels(getAmplitude(), setup.latency, currentLatency, isUnderrun, isOverrun);
}



void rtHandler::convertedInput(audioPacket packet)
{
	if (packet.data.size() > 0) {
		emit haveAudioData(packet);
		amplitude = packet.amplitude;
		currentLatency = packet.time.msecsTo(QTime::currentTime()) + (outFormat.durationForBytes(audio->getStreamLatency() * (outFormat.sampleSize() / 8) * outFormat.channelCount())/1000);
		emit haveLevels(getAmplitude(), setup.latency, currentLatency, isUnderrun, isOverrun);
	}
}



void rtHandler::changeLatency(const quint16 newSize)
{
	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Changing latency to: " << newSize << " from " << setup.latency;
}

int rtHandler::getLatency()
{
	return currentLatency;
}


quint16 rtHandler::getAmplitude()
{
	return static_cast<quint16>(amplitude * 255.0);
}
