/*
	This class handles both RX and TX audio, each is created as a separate instance of the class
	but as the setup/handling if output (RX) and input (TX) devices is so similar I have combined them.
*/

#include "audiohandler.h"

#include "logcategories.h"
#include "ulaw.h"



audioHandler::audioHandler(QObject* parent) : QObject(parent)
{
	Q_UNUSED(parent)
}

audioHandler::~audioHandler()
{

	if (converterThread != Q_NULLPTR) {
		converterThread->quit();
		converterThread->wait();
	}

	if (isInitialized) {
		stop();
	}

	if (audioInput != Q_NULLPTR) {
		delete audioInput;
		audioInput = Q_NULLPTR;
	}
	if (audioOutput != Q_NULLPTR) {
		delete audioOutput;
		audioOutput = Q_NULLPTR;
	}

}bool audioHandler::init(audioSetup setup) 
{
	if (isInitialized) {
		return false;
	}

	this->setup = setup;
	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "audio handler starting:" << setup.name;
	if (setup.port.isNull())
	{
#ifdef Q_OS_LINUX
		qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "No audio device was found. You probably need to install libqt5multimedia-plugins.";
#else
		qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "Audio device is NULL, please check device selection in settings.";
#endif
		return false;
	}

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

	radioFormat = toQAudioFormat(setup.codec, setup.sampleRate);
	codec = LPCM;
	if (setup.codec == 0x01 || setup.codec == 0x20)
		codec = PCMU;
	else if (setup.codec == 0x40 || setup.codec == 0x40)
		codec = OPUS;


	nativeFormat = setup.port.preferredFormat();

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
	qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Preferred Format: SampleSize" << nativeFormat.sampleSize() << "Channel Count" << nativeFormat.channelCount() <<
		"Sample Rate" << nativeFormat.sampleRate() << "Codec" << codec << "Sample Type" << nativeFormat.sampleType();
#else
	qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Preferred Format: SampleFormat" << nativeFormat.sampleFormat() << "Channel Count" << nativeFormat.channelCount() <<
		"Sample Rate" << nativeFormat.sampleRate();
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
		if (!setup.port.isFormatSupported(nativeFormat)) {
            qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Cannot request stereo reverting to mono";
			nativeFormat.setChannelCount(1);
		}
	}

    if (nativeFormat.sampleRate() < 48000) {
        int tempRate=nativeFormat.sampleRate();
        nativeFormat.setSampleRate(48000);
        if (!setup.port.isFormatSupported(nativeFormat)) {
            qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "Cannot request 48K, reverting to "<< tempRate;
            nativeFormat.setSampleRate(tempRate);
        }
    }

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))

	if (nativeFormat.sampleType() == QAudioFormat::UnSignedInt && nativeFormat.sampleSize() == 8) {
		nativeFormat.setSampleType(QAudioFormat::SignedInt);
		nativeFormat.setSampleSize(16);

		if (!setup.port.isFormatSupported(nativeFormat)) {
			qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "Cannot request 16bit Signed samples, reverting to 8bit Unsigned";
			nativeFormat.setSampleType(QAudioFormat::UnSignedInt);
			nativeFormat.setSampleSize(8);
		}
	}
#else
	if (nativeFormat.sampleFormat() == QAudioFormat::UInt8) {
		nativeFormat.setSampleFormat(QAudioFormat::Int16);

		if (!setup.port.isFormatSupported(nativeFormat)) {
			qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "Cannot request 16bit Signed samples, reverting to 8bit Unsigned";
			nativeFormat.setSampleFormat(QAudioFormat::UInt8);
		}
	}
#endif

	/*
    if (nativeFormat.sampleType()==QAudioFormat::SignedInt) {
        nativeFormat.setSampleType(QAudioFormat::Float);
        nativeFormat.setSampleSize(32);
        if (!setup.port.isFormatSupported(nativeFormat)) {
            qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "Attempt to select 32bit Float failed, reverting to SignedInt";
            nativeFormat.setSampleType(QAudioFormat::SignedInt);
            nativeFormat.setSampleSize(16);
        }

    }
	*/

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))

	if (nativeFormat.sampleSize() == 24) {
		// We can't convert this easily so use 32 bit instead.
		nativeFormat.setSampleSize(32);
		if (!setup.port.isFormatSupported(nativeFormat)) {
			qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "24 bit requested and 32 bit audio not supported, try 16 bit instead";
			nativeFormat.setSampleSize(16);
		}
	}
	

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

	connect(this, SIGNAL(setupConverter(QAudioFormat,codecType,QAudioFormat,codecType,quint8,quint8)), converter, SLOT(init(QAudioFormat,codecType,QAudioFormat,codecType,quint8,quint8)));
	connect(converterThread, SIGNAL(finished()), converter, SLOT(deleteLater()));
	connect(this, SIGNAL(sendToConverter(audioPacket)), converter, SLOT(convert(audioPacket)));
	converterThread->start(QThread::TimeCriticalPriority);

	if (setup.isinput) {

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
		audioInput = new QAudioInput(setup.port, nativeFormat, this);
#else
		audioInput = new QAudioSource(setup.port, nativeFormat, this);
#endif
		connect(audioInput, SIGNAL(stateChanged(QAudio::State)), SLOT(stateChanged(QAudio::State)));
		emit setupConverter(nativeFormat, codecType::LPCM, radioFormat, codec, 7, setup.resampleQuality);
		connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(convertedInput(audioPacket)));
	}
	else {

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
		audioOutput = new QAudioOutput(setup.port, nativeFormat, this);
#else
		audioOutput = new QAudioSink(setup.port, nativeFormat, this);
#endif

		connect(audioOutput, SIGNAL(stateChanged(QAudio::State)), SLOT(stateChanged(QAudio::State)));
		emit setupConverter(radioFormat, codec, nativeFormat, codecType::LPCM, 7, setup.resampleQuality);
		connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(convertedOutput(audioPacket)));
	}



	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "thread id" << QThread::currentThreadId();

	underTimer = new QTimer();
	underTimer->setSingleShot(true);
	connect(underTimer, SIGNAL(timeout()), this, SLOT(clearUnderrun()));

	this->setVolume(setup.localAFgain);

	this->start();

	return true;
}

void audioHandler::start()
{
	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "start() running";

	if (setup.isinput) {
		//this->open(QIODevice::WriteOnly);
		//audioInput->start(this);
#if (defined(Q_OS_WIN) && (QT_VERSION < QT_VERSION_CHECK(6,0,0)))
		audioInput->setBufferSize(nativeFormat.bytesForDuration(setup.latency * 100));
#else
		audioInput->setBufferSize(nativeFormat.bytesForDuration(setup.latency * 1000));
#endif
		audioDevice = audioInput->start();
		connect(audioInput, SIGNAL(destroyed()), audioDevice, SLOT(deleteLater()), Qt::UniqueConnection);
		connect(audioDevice, SIGNAL(readyRead()), this, SLOT(getNextAudioChunk()), Qt::UniqueConnection);
		//audioInput->setNotifyInterval(setup.blockSize/2);
		//connect(audioInput, SIGNAL(notify()), this, SLOT(getNextAudioChunk()), Qt::UniqueConnection);
	}
	else {
		// Buffer size must be set before audio is started.
#if (defined(Q_OS_WIN) && (QT_VERSION < QT_VERSION_CHECK(6,0,0)))
		audioOutput->setBufferSize(nativeFormat.bytesForDuration(setup.latency * 100));
#else
		audioOutput->setBufferSize(nativeFormat.bytesForDuration(setup.latency * 1000));
#endif
		audioDevice = audioOutput->start();
		connect(audioOutput, SIGNAL(destroyed()), audioDevice, SLOT(deleteLater()), Qt::UniqueConnection);
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
}


void audioHandler::incomingAudio(audioPacket packet)
{

    if (audioDevice != Q_NULLPTR && packet.data.size() > 0) {
		packet.volume = volume;

		emit sendToConverter(packet);
	}

	return;
}

void audioHandler::convertedOutput(audioPacket packet) {
	
    if (packet.data.size() > 0 ) {

        currentLatency = packet.time.msecsTo(QTime::currentTime()) + (nativeFormat.durationForBytes(audioOutput->bufferSize() - audioOutput->bytesFree()) / 1000);
        if (audioDevice != Q_NULLPTR) {
            if (audioDevice->write(packet.data) < packet.data.size()) {
                    qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Buffer full!";
                    isOverrun=true;
            } else {
                isOverrun = false;
            }
            if (lastReceived.msecsTo(QTime::currentTime()) > 100) {
                qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Time since last audio packet" << lastReceived.msecsTo(QTime::currentTime()) << "Expected around" << setup.blockSize;
            }
            lastReceived = QTime::currentTime();
        }
        /*if ((packet.seq > lastSentSeq + 1) && (setup.codec == 0x40 || setup.codec == 0x80)) {
            qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Attempting FEC on packet" << packet.seq << "as last is" << lastSentSeq;
            lastSentSeq = packet.seq;
            incomingAudio(packet); // Call myself again to run the packet a second time (FEC)
        }
        */
        lastSentSeq = packet.seq;
        amplitude = packet.amplitudePeak;
        emit haveLevels(getAmplitude(), static_cast<quint16>(packet.amplitudeRMS * 255.0), setup.latency, currentLatency, isUnderrun, isOverrun);
    }
}

void audioHandler::getNextAudioChunk()
{
    if (audioDevice) {
        tempBuf.data.append(audioDevice->readAll());
    }
    if (tempBuf.data.length() >= nativeFormat.bytesForDuration(setup.blockSize * 1000)) {
		audioPacket packet;
		packet.time = QTime::currentTime();
		packet.sent = 0;
		packet.volume = volume;
		memcpy(&packet.guid, setup.guid, GUIDLEN);
		//QTime startProcessing = QTime::currentTime();
		packet.data.clear();
		packet.data = tempBuf.data.mid(0, nativeFormat.bytesForDuration(setup.blockSize * 1000));
		tempBuf.data.remove(0, nativeFormat.bytesForDuration(setup.blockSize * 1000));

		emit sendToConverter(packet);
	}

    /* If there is still enough data in the buffer, call myself again in 20ms */
    if (tempBuf.data.length() >= nativeFormat.bytesForDuration(setup.blockSize * 1000)) {
        QTimer::singleShot(setup.blockSize, this, &audioHandler::getNextAudioChunk);
    }

	return;
}


void audioHandler::convertedInput(audioPacket audio) 
{
    if (audio.data.size() > 0) {
        emit haveAudioData(audio);
        if (lastReceived.msecsTo(QTime::currentTime()) > 100) {
            qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Time since last audio packet" << lastReceived.msecsTo(QTime::currentTime()) << "Expected around" << setup.blockSize ;
        }
        lastReceived = QTime::currentTime();
        amplitude = audio.amplitudePeak;
        emit haveLevels(getAmplitude(), audio.amplitudeRMS, setup.latency, currentLatency, isUnderrun, isOverrun);
    }
}

void audioHandler::changeLatency(const quint16 newSize)
{

	setup.latency = newSize;

	if (!setup.isinput) {
		stop();
		start();
	}
	qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Configured latency: " << setup.latency << "Buffer Duration:" << nativeFormat.durationForBytes(audioOutput->bufferSize())/1000 << "ms";

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
}
