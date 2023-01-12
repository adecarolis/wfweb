#include "rthandler.h"

#include "logcategories.h"

#if defined(Q_OS_WIN)
#include <objbase.h>
#endif

#undef RT_EXCEPTION

rtHandler::rtHandler(QObject* parent)
{
	Q_UNUSED(parent)
}

rtHandler::~rtHandler()
{

	if (converterThread != Q_NULLPTR) {
		converterThread->quit();
		converterThread->wait();
	}

	if (isInitialized) {

#ifdef RT_EXCEPTION
		try {
#endif
			audio->abortStream();
			audio->closeStream();
#ifdef RT_EXCEPTION
		}
        catch (RtAudioError& e) {
			qInfo(logAudio()) << "Error closing stream:" << aParams.deviceId << ":" << QString::fromStdString(e.getMessage());
		}
#endif
		delete audio;

	}
	
}

bool rtHandler::init(audioSetup setup)
{
	if (isInitialized) {
		return false;
	}

	this->setup = setup;
	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "RTAudio handler starting:" << setup.name;

	if (setup.portInt==-1)
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

	codecType codec = LPCM;
	if (setup.codec == 0x01 || setup.codec == 0x20)
		codec = PCMU;
	else if (setup.codec == 0x40 || setup.codec == 0x40)
		codec = OPUS;

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

#ifdef RT_EXCEPTION
	try {
#endif
		info = audio->getDeviceInfo(aParams.deviceId);
#ifdef RT_EXCEPTION
	}
    catch (RtAudioError e) {
		qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Device exception:" << aParams.deviceId << ":" << QString::fromStdString(e.getMessage());
		goto errorHandler;
	}
#endif
#ifdef RT_EXCEPTION
	if (info.probed)
	{
#endif
		qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << QString::fromStdString(info.name) << "(" << aParams.deviceId << ") successfully probed";

		RtAudioFormat sampleFormat;

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
		nativeFormat.setByteOrder(QAudioFormat::LittleEndian);
		nativeFormat.setCodec("audio/pcm");
#endif

		if (info.nativeFormats == 0)
		{
			qCritical(logAudio()) << "		No natively supported data formats!";
			goto errorHandler;
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
				nativeFormat.setChannelCount(info.inputChannels);
			}
			else {
				nativeFormat.setChannelCount(info.outputChannels);
			}
			
			qInfo(logAudio()) << "		Channels:" << nativeFormat.channelCount();
			
			if (nativeFormat.channelCount() > 2) {
				nativeFormat.setChannelCount(2);
			}
			else if (nativeFormat.channelCount() < 1)
			{
				qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "No channels found, aborting setup.";
				goto errorHandler;
			}

			if (nativeFormat.channelCount() == 1 && radioFormat.channelCount() == 2) {
				nativeFormat.setChannelCount(2);
			}

			aParams.nChannels = nativeFormat.channelCount();


			nativeFormat.setSampleRate(info.preferredSampleRate);

			if (nativeFormat.sampleRate() < 44100) {
				nativeFormat.setSampleRate(48000);				
			}

			if (info.nativeFormats & RTAUDIO_FLOAT32) {
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
				nativeFormat.setSampleType(QAudioFormat::Float);
				nativeFormat.setSampleSize(32);
#else
				nativeFormat.setSampleFormat(QAudioFormat::Float);
#endif
				sampleFormat = RTAUDIO_FLOAT32;
			}
			else if (info.nativeFormats & RTAUDIO_SINT32) {
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
				nativeFormat.setSampleType(QAudioFormat::SignedInt);
				nativeFormat.setSampleSize(32);
#else
				nativeFormat.setSampleFormat(QAudioFormat::Int32);
#endif
				sampleFormat = RTAUDIO_SINT32;
			}
			else if (info.nativeFormats & RTAUDIO_SINT16) {
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
				nativeFormat.setSampleType(QAudioFormat::SignedInt);
				nativeFormat.setSampleSize(16);
#else
				nativeFormat.setSampleFormat(QAudioFormat::Int16);
#endif
				sampleFormat = RTAUDIO_SINT16;
			}
			else {
				qCritical(logAudio()) << "Cannot find supported sample format!";
				goto errorHandler;
			}
		}

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
		qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Selected format: SampleSize" << nativeFormat.sampleSize() << "Channel Count" << nativeFormat.channelCount() <<
			"Sample Rate" << nativeFormat.sampleRate() << "Codec" << nativeFormat.codec() << "Sample Type" << nativeFormat.sampleType();
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


		// Per channel chunk size.
		this->chunkSize = nativeFormat.framesForDuration(setup.blockSize * 1000);

#ifdef RT_EXCEPTION
		try {
#endif
			if (setup.isinput) {
				audio->openStream(NULL, &aParams, sampleFormat, nativeFormat.sampleRate(), &this->chunkSize, &staticWrite, this, &options);
				emit setupConverter(nativeFormat, codecType::LPCM, radioFormat, codec, 7, setup.resampleQuality);
				connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(convertedInput(audioPacket)));
			}
			else {
				audio->openStream(&aParams, NULL, sampleFormat, nativeFormat.sampleRate(), &this->chunkSize, &staticRead, this , &options);
				emit setupConverter(radioFormat, codec, nativeFormat, codecType::LPCM, 7, setup.resampleQuality);
				connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(convertedOutput(audioPacket)));
			}
			audio->startStream();
			isInitialized = true;
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "device successfully opened";
			qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "detected latency:" << audio->getStreamLatency();
#ifdef RT_EXCEPTION
		}
		catch (RtAudioError& e) {
			qInfo(logAudio()) << "Error opening:" << QString::fromStdString(e.getMessage());
			// Try again?
			goto errorHandler;
		}
#endif

#ifdef RT_EXCEPTION
	}
	else
	{
		qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << QString::fromStdString(info.name) << "(" << aParams.deviceId << ") could not be probed, check audio configuration!";
		goto errorHandler;
	}
#endif
	this->setVolume(setup.localAFgain);

	
	return isInitialized;

errorHandler:
	if (retryConnectCount < 10) {
		qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "*** Attempting to reconnect to audio device in 500ms";
		QTimer::singleShot(500, this, std::bind(&rtHandler::init, this, setup));
		retryConnectCount++;
	}
	else
	{
		qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "*** Retry count exceeded, giving up!";
	}
	return false;

}


void rtHandler::setVolume(unsigned char volume)
{
	this->volume = audiopot[volume];
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
	int nBytes = nFrames * nativeFormat.bytesPerFrame();
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
	packet.data.append((char*)inputBuffer, nFrames * nativeFormat.bytesPerFrame());
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
	amplitude = packet.amplitudePeak;
	currentLatency = packet.time.msecsTo(QTime::currentTime()) + (nativeFormat.durationForBytes(audio->getStreamLatency() * nativeFormat.bytesPerFrame()) / 1000);
	emit haveLevels(getAmplitude(), packet.amplitudeRMS, setup.latency, currentLatency, isUnderrun, isOverrun);
}



void rtHandler::convertedInput(audioPacket packet)
{
	if (packet.data.size() > 0) {
		emit haveAudioData(packet);
		amplitude = packet.amplitudePeak;
		currentLatency = packet.time.msecsTo(QTime::currentTime()) + (nativeFormat.durationForBytes(audio->getStreamLatency() * nativeFormat.bytesPerFrame()) / 1000);
		emit haveLevels(getAmplitude(), static_cast<quint16>(packet.amplitudeRMS * 255.0), setup.latency, currentLatency, isUnderrun, isOverrun);
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
