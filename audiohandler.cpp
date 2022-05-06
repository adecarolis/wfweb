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

	if (converterThread != Q_NULLPTR) {
		converterThread->quit();
		converterThread->wait();
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
	0x40 Opus 1ch
	0x80 Opus 2ch
	*/

	setup = setupIn;
	setup.format.setChannelCount(1);
	setup.format.setSampleSize(8);
	setup.format.setSampleType(QAudioFormat::UnSignedInt);
	setup.format.setCodec("audio/pcm");
	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "audio handler starting:" << setup.name;

	if (setup.port.isNull())
	{
		qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "No audio device was found. You probably need to install libqt5multimedia-plugins.";
		return false;
	}

	if (setup.codec == 0x01 || setup.codec == 0x20) {
		setup.ulaw = true;
		setup.format.setSampleSize(16);
		setup.format.setSampleType(QAudioFormat::SignedInt);
		setup.format.setCodec("audio/PCMU");
	}

	if (setup.codec == 0x08 || setup.codec == 0x10 || setup.codec == 0x20 || setup.codec == 0x80) {
		setup.format.setChannelCount(2);
	}

	if (setup.codec == 0x04 || setup.codec == 0x10) {
		setup.format.setSampleSize(16);
		setup.format.setSampleType(QAudioFormat::SignedInt);
	}

	if (setup.codec == 0x40 || setup.codec == 0x80) {
		setup.format.setSampleSize(32);
		setup.format.setSampleType(QAudioFormat::Float);
		setup.format.setCodec("audio/opus");
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

    if (format.sampleType()==QAudioFormat::SignedInt) {
        format.setSampleType(QAudioFormat::Float);
        format.setSampleSize(32);
        if (!setup.port.isFormatSupported(format)) {
            qCritical(logAudio()) << (setup.isinput ? "Input" : "Output") << "Attempt to select 32bit Float failed, reverting to SignedInt";
            format.setSampleType(QAudioFormat::SignedInt);
            format.setSampleSize(16);
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

	qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Selected format: SampleSize" << format.sampleSize() << "Channel Count" << format.channelCount() <<
		"Sample Rate" << format.sampleRate() << "Codec" << format.codec() << "Sample Type" << format.sampleType();

	// We "hopefully" now have a valid format that is supported so try connecting

	converter = new audioConverter();
	converterThread = new QThread(this);
	converterThread->setObjectName("audioConverter()");
	converter->moveToThread(converterThread);

	connect(this, SIGNAL(setupConverter(QAudioFormat,QAudioFormat,quint8,quint8)), converter, SLOT(init(QAudioFormat,QAudioFormat,quint8,quint8)));
	connect(converterThread, SIGNAL(finished()), converter, SLOT(deleteLater()));
	connect(this, SIGNAL(sendToConverter(audioPacket)), converter, SLOT(convert(audioPacket)));
	converterThread->start(QThread::TimeCriticalPriority);

	if (setup.isinput) {
		audioInput = new QAudioInput(setup.port, format, this);
		connect(audioInput, SIGNAL(stateChanged(QAudio::State)), SLOT(stateChanged(QAudio::State)));
		emit setupConverter(format, setup.format, 7, setup.resampleQuality);
		connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(convertedInput(audioPacket)));
	}
	else {
		audioOutput = new QAudioOutput(setup.port, format, this);
		connect(audioOutput, SIGNAL(stateChanged(QAudio::State)), SLOT(stateChanged(QAudio::State)));
		emit setupConverter(setup.format, format, 7, setup.resampleQuality);
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
		this->open(QIODevice::WriteOnly);
		audioInput->start(this);
		connect(audioInput, SIGNAL(destroyed()), audioDevice, SLOT(deleteLater()), Qt::UniqueConnection);
		//connect(audioDevice, SIGNAL(readyRead()), this, SLOT(getNextAudioChunk()), Qt::UniqueConnection);
	}
	else {
		// Buffer size must be set before audio is started.
#ifdef Q_OS_WIN
		audioOutput->setBufferSize(format.bytesForDuration(setup.latency * 100));
#else
		audioOutput->setBufferSize(format.bytesForDuration(setup.latency * 1000));
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

qint64 audioHandler::readData(char* data, qint64 nBytes) {
	return nBytes;
}
qint64 audioHandler::writeData(const char* data, qint64 nBytes) {

	tempBuf.data.append(data,nBytes);

	while (tempBuf.data.length() >= format.bytesForDuration(setup.blockSize * 1000)) {
		audioPacket packet;
		packet.time = QTime::currentTime();
		packet.sent = 0;
		packet.volume = volume;
		memcpy(&packet.guid, setup.guid, GUIDLEN);
		QTime startProcessing = QTime::currentTime();
		packet.data.clear();
		packet.data = tempBuf.data.mid(0, format.bytesForDuration(setup.blockSize * 1000));
		tempBuf.data.remove(0, format.bytesForDuration(setup.blockSize * 1000));

		emit sendToConverter(packet);
	}

	return nBytes;
}


void audioHandler::setVolume(unsigned char volume)
{
    this->volume = audiopot[volume];
	qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "setVolume: " << volume << "(" << this->volume << ")";
}


void audioHandler::incomingAudio(audioPacket packet)
{
	QTime startProcessing = QTime::currentTime();

	packet.volume = volume;

	emit sendToConverter(packet);

	return;
}

void audioHandler::convertedOutput(audioPacket packet) {
	
	currentLatency = packet.time.msecsTo(QTime::currentTime()) + (format.durationForBytes(audioOutput->bufferSize() - audioOutput->bytesFree()) / 1000);
	if (audioDevice != Q_NULLPTR && audioOutput != Q_NULLPTR) {
		audioDevice->write(packet.data);
		if (lastReceived.msecsTo(QTime::currentTime()) > 100) {
			qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Time since last audio packet" << lastReceived.msecsTo(QTime::currentTime()) << "Expected around" << setup.blockSize;
		}
		lastReceived = QTime::currentTime();
	}

	/*
	if ((packet.seq > lastSentSeq + 1) && (setup.codec == 0x40 || setup.codec == 0x80)) {
		qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Attempting FEC on packet" << packet.seq << "as last is" << lastSentSeq;
		lastSentSeq = packet.seq;
		incomingAudio(packet); // Call myself again to run the packet a second time (FEC)
	}
	*/

	lastSentSeq = packet.seq;
	emit haveLevels(getAmplitude(), setup.latency, currentLatency, isUnderrun);

	amplitude = packet.amplitude;

}

void audioHandler::getNextAudioChunk()
{
	tempBuf.data.append(audioDevice->readAll());

	while (tempBuf.data.length() >= format.bytesForDuration(setup.blockSize * 1000)) {
		audioPacket packet;
		packet.time = QTime::currentTime();
		packet.sent = 0;
		packet.volume = volume;
		memcpy(&packet.guid, setup.guid, GUIDLEN);
		QTime startProcessing = QTime::currentTime();
		packet.data.clear();
		packet.data = tempBuf.data.mid(0, format.bytesForDuration(setup.blockSize * 1000));
		tempBuf.data.remove(0, format.bytesForDuration(setup.blockSize * 1000));

		emit sendToConverter(packet);
	}
	return;

}


void audioHandler::convertedInput(audioPacket audio) 
{
	emit haveAudioData(audio);
	if (lastReceived.msecsTo(QTime::currentTime()) > 100) {
		qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Time since last audio packet" << lastReceived.msecsTo(QTime::currentTime()) << "Expected around" << setup.blockSize ;
	}
	lastReceived = QTime::currentTime();
	//ret = livePacket.data;
	amplitude = audio.amplitude;
	emit haveLevels(getAmplitude(), setup.latency, currentLatency, isUnderrun);	
}

void audioHandler::changeLatency(const quint16 newSize)
{

	setup.latency = newSize;

	if (!setup.isinput) {
		stop();
		start();
	}
	qDebug(logAudio()) << (setup.isinput ? "Input" : "Output") << "Configured latency: " << setup.latency << "Buffer Duration:" << format.durationForBytes(audioOutput->bufferSize())/1000 << "ms";

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
