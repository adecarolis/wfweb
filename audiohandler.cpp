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

	if (converterThread != Q_NULLPTR) {
		converterThread->quit();
		converterThread->wait();
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
		qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "No audio device was found. You probably need to install libqt5multimedia-plugins.";
		return false;
	}

	qDebug(logAudio()) << "Creating" << (setup.isinput ? "Input" : "Output") << "audio device:" << setup.name <<
		", bits" << inFormat.sampleSize() <<
		", codec" << setup.codec <<
		", latency" << setup.latency <<
		", localAFGain" << setup.localAFgain <<
		", radioChan" << inFormat.channelCount() <<
		", resampleQuality" << setup.resampleQuality <<
		", samplerate" << inFormat.sampleRate() <<
		", uLaw" << setup.ulaw;

	inFormat = toQAudioFormat(setup.codec, setup.sampleRate);

    if(!setup.isinput)
    {
        this->setVolume(setup.localAFgain);
    }

	outFormat = setup.port.preferredFormat();
	qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Preferred Format: SampleSize" << outFormat.sampleSize() << "Channel Count" << outFormat.channelCount() <<
		"Sample Rate" << outFormat.sampleRate() << "Codec" << outFormat.codec() << "Sample Type" << outFormat.sampleType();
	if (outFormat.channelCount() > 2) {
		outFormat.setChannelCount(2);
	}
	else if (outFormat.channelCount() < 1)
	{
		qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "No channels found, aborting setup.";
		return false;
	}

    if (outFormat.channelCount() == 1 && inFormat.channelCount() == 2) {
		outFormat.setChannelCount(2);
		if (!setup.port.isFormatSupported(outFormat)) {
            qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Cannot request stereo reverting to mono";
			outFormat.setChannelCount(1);
		}
	}

    if (outFormat.sampleRate() < 48000) {
        int tempRate=outFormat.sampleRate();
        outFormat.setSampleRate(48000);
        if (!setup.port.isFormatSupported(outFormat)) {
            qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "Cannot request 48K, reverting to "<< tempRate;
            outFormat.setSampleRate(tempRate);
        }
    }

    if (outFormat.sampleType() == QAudioFormat::UnSignedInt && outFormat.sampleSize()==8) {
        outFormat.setSampleType(QAudioFormat::SignedInt);
        outFormat.setSampleSize(16);

        if (!setup.port.isFormatSupported(outFormat)) {
            qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "Cannot request 16bit Signed samples, reverting to 8bit Unsigned";
            outFormat.setSampleType(QAudioFormat::UnSignedInt);
            outFormat.setSampleSize(8);
        }
    }


    /*

    if (outFormat.sampleType()==QAudioFormat::SignedInt) {
        outFormat.setSampleType(QAudioFormat::Float);
        outFormat.setSampleSize(32);
        if (!setup.port.isFormatSupported(outFormat)) {
            qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "Attempt to select 32bit Float failed, reverting to SignedInt";
            outFormat.setSampleType(QAudioFormat::SignedInt);
            outFormat.setSampleSize(16);
        }

    }
	*/

	if (outFormat.sampleSize() == 24) {
		// We can't convert this easily so use 32 bit instead.
		outFormat.setSampleSize(32);
		if (!setup.port.isFormatSupported(outFormat)) {
			qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "24 bit requested and 32 bit audio not supported, try 16 bit instead";
			outFormat.setSampleSize(16);
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

	connect(this, SIGNAL(setupConverter(QAudioFormat,QAudioFormat,quint8,quint8)), converter, SLOT(init(QAudioFormat,QAudioFormat,quint8,quint8)));
	connect(converterThread, SIGNAL(finished()), converter, SLOT(deleteLater()));
	connect(this, SIGNAL(sendToConverter(audioPacket)), converter, SLOT(convert(audioPacket)));
	converterThread->start(QThread::TimeCriticalPriority);

	if (setup.isinput) {
		audioInput = new QAudioInput(setup.port, outFormat, this);
		connect(audioInput, SIGNAL(stateChanged(QAudio::State)), SLOT(stateChanged(QAudio::State)));
		emit setupConverter(outFormat, inFormat, 7, setup.resampleQuality);
		connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(convertedInput(audioPacket)));
	}
	else {
		audioOutput = new QAudioOutput(setup.port, outFormat, this);
		connect(audioOutput, SIGNAL(stateChanged(QAudio::State)), SLOT(stateChanged(QAudio::State)));
		emit setupConverter(inFormat, outFormat, 7, setup.resampleQuality);
		connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(convertedOutput(audioPacket)));
	}



	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "thread id" << QThread::currentThreadId();

	underTimer = new QTimer();
	underTimer->setSingleShot(true);
	connect(underTimer, SIGNAL(timeout()), this, SLOT(clearUnderrun()));

	this->start();

	return true;
}

void audioHandler::start()
{
	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "start() running";

	if (setup.isinput) {
		//this->open(QIODevice::WriteOnly);
		//audioInput->start(this);
#ifdef Q_OS_WIN
		audioInput->setBufferSize(inFormat.bytesForDuration(setup.latency * 100));
#else
		audioInput->setBufferSize(inFormat.bytesForDuration(setup.latency * 1000));
#endif
		audioDevice = audioInput->start();
		connect(audioInput, SIGNAL(destroyed()), audioDevice, SLOT(deleteLater()), Qt::UniqueConnection);
		connect(audioDevice, SIGNAL(readyRead()), this, SLOT(getNextAudioChunk()), Qt::UniqueConnection);
		//audioInput->setNotifyInterval(setup.blockSize/2);
		//connect(audioInput, SIGNAL(notify()), this, SLOT(getNextAudioChunk()), Qt::UniqueConnection);
	}
	else {
		// Buffer size must be set before audio is started.
#ifdef Q_OS_WIN
		audioOutput->setBufferSize(outFormat.bytesForDuration(setup.latency * 100));
#else
		audioOutput->setBufferSize(outFormat.bytesForDuration(setup.latency * 1000));
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
	qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "setVolume: " << volume << "(" << this->volume << ")";
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

        currentLatency = packet.time.msecsTo(QTime::currentTime()) + (outFormat.durationForBytes(audioOutput->bufferSize() - audioOutput->bytesFree()) / 1000);
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
        emit haveLevels(getAmplitude(), setup.latency, currentLatency, isUnderrun, isOverrun);

        amplitude = packet.amplitude;
    }
}

void audioHandler::getNextAudioChunk()
{
    if (audioDevice) {
        tempBuf.data.append(audioDevice->readAll());
    }
    if (tempBuf.data.length() >= outFormat.bytesForDuration(setup.blockSize * 1000)) {
		audioPacket packet;
		packet.time = QTime::currentTime();
		packet.sent = 0;
		packet.volume = volume;
		memcpy(&packet.guid, setup.guid, GUIDLEN);
		//QTime startProcessing = QTime::currentTime();
		packet.data.clear();
		packet.data = tempBuf.data.mid(0, outFormat.bytesForDuration(setup.blockSize * 1000));
		tempBuf.data.remove(0, outFormat.bytesForDuration(setup.blockSize * 1000));

		emit sendToConverter(packet);
	}

    /* If there is still enough data in the buffer, call myself again in 20ms */
    if (tempBuf.data.length() >= outFormat.bytesForDuration(setup.blockSize * 1000)) {
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
        amplitude = audio.amplitude;
        emit haveLevels(getAmplitude(), setup.latency, currentLatency, isUnderrun, isOverrun);
    }
}

void audioHandler::changeLatency(const quint16 newSize)
{

	setup.latency = newSize;

	if (!setup.isinput) {
		stop();
		start();
	}
	qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Configured latency: " << setup.latency << "Buffer Duration:" << outFormat.durationForBytes(audioOutput->bufferSize())/1000 << "ms";

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
