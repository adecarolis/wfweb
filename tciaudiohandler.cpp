#include "tciaudiohandler.h"

#include "logcategories.h"
#include "tciserver.h"


tciAudioHandler::tciAudioHandler(QObject* parent)
{
	Q_UNUSED(parent)
}

tciAudioHandler::~tciAudioHandler()
{

	if (converterThread != Q_NULLPTR) {
		converterThread->quit();
		converterThread->wait();
	}
	
}

bool tciAudioHandler::init(audioSetup setup)
{
	if (isInitialized) {
		return false;
	}

	this->setup = setup;
    qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "TCI Audio handler starting:" << setup.name;

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

	codecType codec = LPCM;
	if (setup.codec == 0x01 || setup.codec == 0x20)
		codec = PCMU;
	else if (setup.codec == 0x40 || setup.codec == 0x40)
        codec = OPUS;

    if (setup.isinput)
        nativeFormat.setChannelCount(2);
    else
        nativeFormat.setChannelCount(2);

    nativeFormat.setSampleRate(48000);

    this->setVolume(setup.localAFgain);
	
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    nativeFormat.setSampleType(QAudioFormat::Float);
    nativeFormat.setSampleSize(32);
#else
    nativeFormat.setSampleFormat(QAudioFormat::Float);
#endif

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
        emit setupConverter(nativeFormat, codecType::LPCM, radioFormat, codec, 7, setup.resampleQuality);
        connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(convertedInput(audioPacket)));
        if (setup.tci != Q_NULLPTR) {
            connect((tciServer*)setup.tci, SIGNAL(sendTCIAudio(QByteArray)), this, SLOT(receiveTCIAudio(QByteArray)));
            //connect(this,SIGNAL(setupTxPacket(int)), (tciServer*)setup.tci, SLOT(setupTxPacket(int)));
            //emit setupTxPacket((nativeFormat.bytesForDuration(setup.blockSize * 1000)*2)/sizeof(float));
        } else {
            qCritical(logAudio()) << "***** TCI NOT FOUND *****";
        }
    }
    else {
        emit setupConverter(radioFormat, codec, nativeFormat, codecType::LPCM, 7, setup.resampleQuality);
        connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(convertedOutput(audioPacket)));
        if (setup.tci != Q_NULLPTR) {
            connect(this,SIGNAL(sendTCIAudio(audioPacket)), (tciServer*)setup.tci, SLOT(receiveTCIAudio(audioPacket)));
        } else {
            qCritical(logAudio()) << "***** TCI NOT FOUND *****";
        }
    }


    return isInitialized;
}

void tciAudioHandler::setVolume(unsigned char volume)
{
	this->volume = audiopot[volume];
}

void tciAudioHandler::incomingAudio(audioPacket packet)
{
	packet.volume = volume;
	emit sendToConverter(packet);
	return;
}

void tciAudioHandler::convertedOutput(audioPacket packet)
{
	arrayBuffer.append(packet.data);
	amplitude = packet.amplitudePeak;
    currentLatency = 0;
	emit haveLevels(getAmplitude(), packet.amplitudeRMS, setup.latency, currentLatency, isUnderrun, isOverrun);
    if (arrayBuffer.length() >= TCI_AUDIO_LENGTH * sizeof(float)) {
        packet.data.clear();
        packet.data = arrayBuffer.mid(0, TCI_AUDIO_LENGTH * sizeof(float));
        arrayBuffer.remove(0, TCI_AUDIO_LENGTH * sizeof(float));
        emit sendTCIAudio(packet);
    }
}


void tciAudioHandler::convertedInput(audioPacket packet)
{
	if (packet.data.size() > 0) {
		emit haveAudioData(packet);
		amplitude = packet.amplitudePeak;
        currentLatency = 0;
		emit haveLevels(getAmplitude(), static_cast<quint16>(packet.amplitudeRMS * 255.0), setup.latency, currentLatency, isUnderrun, isOverrun);
    }
}

void tciAudioHandler::getNextAudio()
{
    if (tempBuf.data.length() >= nativeFormat.bytesForDuration(setup.blockSize * 1000)) {
        audioPacket packet;
        packet.time = QTime::currentTime();
        packet.sent = 0;
        packet.volume = volume;
        memcpy(&packet.guid, setup.guid, GUIDLEN);
        packet.data.clear();
        audioMutex.lock();
        packet.data = tempBuf.data.mid(0, nativeFormat.bytesForDuration(setup.blockSize * 1000));
        tempBuf.data.remove(0, nativeFormat.bytesForDuration(setup.blockSize * 1000));
        audioMutex.unlock();
        emit sendToConverter(packet);
    }
    /* If there is still enough data in the buffer, call myself again in 20ms */
    if (tempBuf.data.length() >= nativeFormat.bytesForDuration(setup.blockSize * 1000)) {
        QTimer::singleShot(setup.blockSize, this, &tciAudioHandler::getNextAudio);
    }
}

void tciAudioHandler::receiveTCIAudio(const QByteArray packet) {


    if (packet.size() > 0) {
        audioMutex.lock();
        tempBuf.data.append(packet);
        audioMutex.unlock();
    }

    getNextAudio();
}


void tciAudioHandler::changeLatency(const quint16 newSize)
{
	qInfo(logAudio()) << (setup.isinput ? "Input" : "Output") << "Changing latency to: " << newSize << " from " << setup.latency;
}

int tciAudioHandler::getLatency()
{
	return currentLatency;
}

quint16 tciAudioHandler::getAmplitude()
{
	return static_cast<quint16>(amplitude * 255.0);
}
