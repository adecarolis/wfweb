/*
    This class handles both RX and TX audio, each is created as a seperate instance of the class
    but as the setup/handling if output (RX) and input (TX) devices is so similar I have combined them. 
*/
#include "audiohandler.h"

#ifndef USE_RTAUDIO
#include "logcategories.h"

#define MULAW_BIAS 33
#define MULAW_MAX 0x1fff

audioHandler::audioHandler(QObject* parent) :
    QIODevice(parent),
    isInitialized(false),
    audioOutput(Q_NULLPTR),
    audioInput(Q_NULLPTR),
    isUlaw(false),
    audioLatency(0),
    isInput(0),
	chunkAvailable(false)
{
}

audioHandler::~audioHandler()
{
    stop();    
    if (audioOutput != Q_NULLPTR) {
		audioOutput->stop();
        delete audioOutput;
    }
    if (audioInput != Q_NULLPTR) {
		audioInput->stop();
        delete audioInput;
    }

	if (resampler != NULL) {
		speex_resampler_destroy(resampler);
	}
}

bool audioHandler::init(const quint8 bits, const quint8 channels, const quint16 samplerate, const quint16 audioLatency, const bool ulaw, const bool isinput, int device, quint8 resampleQuality)
{
    if (isInitialized) {
        return false;
    }

	/* Always use 16 bit 48K samples internally*/
    format.setSampleSize(16);
    format.setChannelCount(channels);
    format.setSampleRate(INTERNAL_SAMPLE_RATE);
    format.setCodec("audio/pcm");
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::SignedInt);

	this->audioLatency = latency;
	this->isUlaw = ulaw;
    this->isInput = isinput;
    this->radioSampleBits = bits;
    this->radioSampleRate = samplerate;
	this->radioChannels = channels;

	// chunk size is always relative to Internal Sample Rate.
	this->chunkSize = (INTERNAL_SAMPLE_RATE / 25) * radioChannels;

	qInfo(logAudio()) << (isInput ? "Input" : "Output") << "chunkSize: " << this->chunkSize;
	qInfo(logAudio()) << (isInput ? "Input" : "Output") << "bufferLength (latency): " << this->latency;

	int resample_error=0;

	if (isInput) {
		resampler = wf_resampler_init(radioChannels, INTERNAL_SAMPLE_RATE, samplerate, resampleQuality, &resample_error);

		isInitialized = setDevice(port);

		if (!isInitialized) {
            qInfo(logAudio()) << "Input device " << port.deviceName() << " not found, using default";
			isInitialized = setDevice(QAudioDeviceInfo::defaultInputDevice());
		}
	}
	else
	{
		resampler = wf_resampler_init(radioChannels, samplerate, INTERNAL_SAMPLE_RATE, resampleQuality, &resample_error);

		isInitialized = setDevice(port);

		if (!isInitialized) {
            qInfo(logAudio()) << "Output device " << deviceInfo.deviceName() << " not found, using default";
			isInitialized = setDevice(QAudioDeviceInfo::defaultOutputDevice());
		}
	}


	wf_resampler_get_ratio(resampler, &ratioNum, &ratioDen);
	qInfo(logAudio()) << (isInput ? "Input" : "Output") <<  "wf_resampler_init() returned: " << resample_error << " ratioNum" << ratioNum << " ratioDen" << ratioDen;

    qInfo(logAudio()) << (isInput ? "Input" : "Output") << "audio port name: " << deviceInfo.deviceName();
 	return isInitialized;
}

void audioHandler::setVolume(unsigned char volume)
{
	//qInfo(logAudio()) << (isInput ? "Input" : "Output") << "setVolume: " << volume << "(" << (qreal)(volume/255.0) << ")";

	if (audioOutput != Q_NULLPTR) {
		audioOutput->setVolume((qreal)(volume / 255.0));
	}
	else if (audioInput != Q_NULLPTR) {
		audioInput->setVolume((qreal)(volume / 255.0));
	}

}


bool audioHandler::setDevice(QAudioDeviceInfo deviceInfo)
{
	bool ret = true;
    qInfo(logAudio()) << (isInput ? "Input" : "Output") << "setDevice() running :" << deviceInfo.deviceName();
    if (!deviceInfo.isFormatSupported(format)) {
        if (deviceInfo.isNull())
        {
            qInfo(logAudio()) << "No audio device was found. You probably need to install libqt5multimedia-plugins.";
			ret = false;
        }
        else {
			/*
            qInfo(logAudio()) << "Audio Devices found: ";
            const auto deviceInfos = QAudioDeviceInfo::availableDevices(QAudio::AudioOutput);
            for (const QAudioDeviceInfo& deviceInfo : deviceInfos)
            {
                qInfo(logAudio()) << "Device name: " << deviceInfo.deviceName();
                qInfo(logAudio()) << "is null (probably not good):" << deviceInfo.isNull();
                qInfo(logAudio()) << "channel count:" << deviceInfo.supportedChannelCounts();
                qInfo(logAudio()) << "byte order:" << deviceInfo.supportedByteOrders();
                qInfo(logAudio()) << "supported codecs:" << deviceInfo.supportedCodecs();
                qInfo(logAudio()) << "sample rates:" << deviceInfo.supportedSampleRates();
                qInfo(logAudio()) << "sample sizes:" << deviceInfo.supportedSampleSizes();
                qInfo(logAudio()) << "sample types:" << deviceInfo.supportedSampleTypes();
            }
            qInfo(logAudio()) << "----- done with audio info -----";
			*/
			ret=false;
        }

        qInfo(logAudio()) << "Format not supported, choosing nearest supported format - which may not work!";
        deviceInfo.nearestFormat(format);
    }
    this->deviceInfo = deviceInfo;
    this->reinit();
    return ret;
}

void audioHandler::reinit()
{
    qInfo(logAudio()) << (isInput ? "Input" : "Output") << "reinit() running";
    if (audioOutput != Q_NULLPTR && audioOutput->state() != QAudio::StoppedState) {
        this->stop();
    }

    if (this->isInput)
    {
        // (Re)initialize audio input
        if (audioInput != Q_NULLPTR)
            delete audioInput;
        audioInput = new QAudioInput(deviceInfo, format, this);

        connect(audioInput, SIGNAL(notify()), SLOT(notified()));
        connect(audioInput, SIGNAL(stateChanged(QAudio::State)), SLOT(stateChanged(QAudio::State)));

    }
    else {
        // (Re)initialize audio output
        if (audioOutput != Q_NULLPTR)
            delete audioOutput;
        audioOutput = Q_NULLPTR;
        audioOutput = new QAudioOutput(deviceInfo, format, this);

		// This seems to only be needed on Linux but is critical in aligning buffer sizes.
//#ifdef Q_OS_MAC
        audioOutput->setBufferSize(chunkSize*8);
//#else
//        audioOutput->setBufferSize(chunkSize*4);
//#endif

		connect(audioOutput, SIGNAL(notify()), SLOT(notified()));
        connect(audioOutput, SIGNAL(stateChanged(QAudio::State)), SLOT(stateChanged(QAudio::State)));
    }

    this->start();
    this->flush();
}

void audioHandler::start()
{
    qInfo(logAudio()) << (isInput ? "Input" : "Output") << "start() running";

    if ((audioOutput == Q_NULLPTR || audioOutput->state() != QAudio::StoppedState) &&
            (audioInput == Q_NULLPTR || audioInput->state() != QAudio::StoppedState) ) {
        return;
    }

    if (isInput) {
		//this->open(QIODevice::WriteOnly | QIODevice::Unbuffered);
		this->open(QIODevice::WriteOnly);
		audioInput->start(this);
    }
    else {
		//this->open(QIODevice::ReadOnly | QIODevice::Unbuffered);
		this->open(QIODevice::ReadOnly);
		audioOutput->start(this);
    }	
}


void audioHandler::flush()
{
    qInfo(logAudio()) << (isInput ? "Input" : "Output") << "flush() running";
    this->stop();
    if (isInput) {
        audioInput->reset();
    }
    else {
        audioOutput->reset();
    }

	QMutexLocker locker(&mutex);
    audioBuffer.clear();
    this->start();
}

void audioHandler::stop()
{
    if (audioOutput != Q_NULLPTR && audioOutput->state() != QAudio::StoppedState) {
        // Stop audio output
        audioOutput->stop();
        this->close();
    }

    if (audioInput != Q_NULLPTR && audioInput->state() != QAudio::StoppedState) {
        // Stop audio output
        audioInput->stop();
        this->close();
    }
}

/// <summary>
/// This function processes the incoming audio FROM the radio and pushes it into the playback buffer *data
/// </summary>
/// <param name="data"></param>
/// <param name="maxlen"></param>
/// <returns></returns>
qint64 audioHandler::readData(char* data, qint64 maxlen)
{
    // Calculate output length, always full samples
	int sentlen = 0;

	//qInfo(logAudio()) << "Looking for: " << maxlen << " bytes";

	// We must lock the mutex for the entire time that the buffer may be modified.
	// Get next packet from buffer.
	if (!inputBuffer.isEmpty())
	{

		// Output buffer is ALWAYS 16 bit.
        QMutexLocker locker(&mutex);
		auto packet = inputBuffer.begin();
        
		while (packet != inputBuffer.end() && sentlen < maxlen)
		{
			int timediff = packet->time.msecsTo(QTime::currentTime());
            
			if (timediff > (int)audioLatency * 2) {
                qInfo(logAudio()) << (isInput ? "Input" : "Output") << "Packet " << hex << packet->seq <<
                    " arrived too late (increase output latency!) " <<
                    dec << packet->time.msecsTo(QTime::currentTime()) << "ms";

                while (packet !=audioBuffer.end() && timediff > (int)audioLatency) {
                    timediff = packet->time.msecsTo(QTime::currentTime());
                    lastSeq=packet->seq;
                    packet = inputBuffer.erase(packet); // returns next packet
                }
                if (packet == inputBuffer.end()) {
                    break;
                }
			}
            
            // If we got here then packet time must be within latency threshold
            
			if (packet->seq == lastSeq+1 || packet->seq <= lastSeq)
			{
				int send = qMin((int)maxlen-sentlen, packet->dataout.length() - packet->sent);
				lastSeq = packet->seq;
				//qInfo(logAudio()) << "Packet " << hex << packet->seq << " arrived on time " << Qt::dec << packet->time.msecsTo(QTime::currentTime()) << "ms";

				memcpy(data + sentlen, packet->dataout.constData() + packet->sent, send);

				sentlen = sentlen + send;

				if (send == packet->dataout.length() - packet->sent)
				{
					//qInfo(logAudio()) << "Get next packet";
					packet = inputBuffer.erase(packet); // returns next packet
				} 
				else
				{
					// Store sent amount (could be zero if audioOutput buffer full) then break.
                    packet->sent = send;
                    break;
                }
			} else {
				qInfo(logAudio()) << (isInput ? "Input" : "Output") << "Missing audio packet(s) from: " << hex << lastSeq + 1 << " to " << hex << packet->seq - 1;
				lastSeq = packet->seq;
			}
		}
	}
	else {
		// Fool audio system into thinking it has valid data, this seems to be required 
		// for MacOS Built in audio but shouldn't cause any issues with other platforms.
		memset(data, 0x0, maxlen);
		return maxlen;
	}

    return sentlen;
}

qint64 audioHandler::writeData(const char* data, qint64 len)
{
	qint64 sentlen = 0;
	QMutexLocker locker(&mutex);
	audioPacket *current;

	while (sentlen < len) {
		if (!audioBuffer.isEmpty())
		{
			if (audioBuffer.last().sent == chunkSize)
			{
				audioBuffer.append(audioPacket());
				audioBuffer.last().sent = 0;
			}
		}
		else
		{
			audioBuffer.append(audioPacket());
			audioBuffer.last().sent = 0;
		}
		current = &audioBuffer.last();

		int send = qMin((int)(len - sentlen), (int)chunkSize-current->sent);

		current->datain.append(QByteArray::fromRawData(data + sentlen, send ));

		sentlen = sentlen + send;

		current->seq = 0; // Not used in TX
		current->time = QTime::currentTime();
		current->sent = current->datain.length();
		
		if (current->sent == chunkSize)
		{
			chunkAvailable = true;
		}
		else if (audioBuffer.length()<=1 && current->sent != chunkSize) {
			chunkAvailable = false;
		}
		
	}

    return (sentlen); // Always return the same number as we received
}

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
			qInfo(logAudio()) << (isInput ? "Input" : "Output") << "Audio now in idle state: " << audioBuffer.length() << " packets in buffer";
			if (audioOutput != Q_NULLPTR && audioOutput->error() == QAudio::UnderrunError)
			{
				qInfo(logAudio()) << (isInput ? "Input" : "Output") << "buffer underrun";
				audioOutput->suspend();
			}
			break;
		}
		case QAudio::ActiveState:
		{
			qInfo(logAudio()) << (isInput ? "Input" : "Output") << "Audio now in active state: " << audioBuffer.length() << " packets in buffer";
			break;
		}
		case QAudio::SuspendedState:
		{
			qInfo(logAudio()) << (isInput ? "Input" : "Output") << "Audio now in suspended state: " << audioBuffer.length() << " packets in buffer";
			break;
		}
		case QAudio::StoppedState:
		{
			qInfo(logAudio()) << (isInput ? "Input" : "Output") << "Audio now in stopped state: " << audioBuffer.length() << " packets in buffer";
			break;
		}
		default: {
			qInfo(logAudio()) << (isInput ? "Input" : "Output") << "Unhandled audio state: " << audioBuffer.length() << " packets in buffer";
		}
	}
}



void audioHandler::incomingAudio(audioPacket data)
{
    if (audioOutput != Q_NULLPTR && audioOutput->state() != QAudio::StoppedState) {
        QMutexLocker locker(&mutex);

		// Incoming data is 8bits?
		if (radioSampleBits == 8)
		{
			QByteArray inPacket((int)data.datain.length() * 2, (char)0xff);
			qint16* in = (qint16*)inPacket.data();
			for (int f = 0; f < data.datain.length(); f++)
			{
				if (isUlaw)
				{
					in[f] = ulaw_decode[(quint8)data.datain[f]];
				}
				else
				{
					// Convert 8-bit sample to 16-bit
					in[f] = (qint16)(((quint8)data.datain[f] << 8) - 32640);
				}
			}
			data.datain = inPacket; // Replace incoming data with converted.
		}

		//qInfo(logAudio()) << "Adding packet to buffer:" << data.seq << ": " << data.datain.length();

		/*	We now have an array of 16bit samples in the NATIVE samplerate of the radio
			If the radio sample rate is below 48000, we need to resample.
		 */

		if (ratioDen != 1) {

			// We need to resample
			quint32 outFrames = ((data.datain.length() / 2) * ratioDen) / radioChannels;
			quint32 inFrames = (data.datain.length() / 2) / radioChannels;
			data.dataout.resize(outFrames * 2 * radioChannels); // Preset the output buffer size.

			int err = 0;
			if (this->radioChannels == 1) {
				err = wf_resampler_process_int(resampler, 0, (const qint16*)data.datain.constData(), &inFrames, (qint16*)data.dataout.data(), &outFrames);
			}
			else {
				err = wf_resampler_process_interleaved_int(resampler, (const qint16*)data.datain.constData(), &inFrames, (qint16*)data.dataout.data(), &outFrames);
			}
			if (err) {
				qInfo(logAudio()) <<(isInput ? "Input" : "Output") << "Resampler error " << err << " inFrames:" << inFrames << " outFrames:" << outFrames;
			}
		}
		else {
			data.dataout = data.datain; 
		}

		inputBuffer.insert(data.seq, data);

		// Restart playback
		if (audioOutput->state() == QAudio::SuspendedState)
		{
			qInfo(logAudio()) << "Output Audio Suspended, Resuming...";
			audioOutput->resume();
		}
	}
}

void audioHandler::changeLatency(const quint16 newSize)
{
    qInfo(logAudio()) << (isInput ? "Input" : "Output") << "Changing latency to: " << newSize << " from " << audioLatency;
    audioLatency = newSize;
}

void audioHandler::getLatency()
{
    emit sendLatency(audioLatency);
}

bool audioHandler::isChunkAvailable()
{
    return (chunkAvailable);
}

void audioHandler::getNextAudioChunk(QByteArray& ret)
{
	if (!audioBuffer.isEmpty() && chunkAvailable)
	{

		QMutexLocker locker(&mutex);

		// Skip through audio buffer deleting any old entry.
		auto packet = audioBuffer.begin();
		while (packet != audioBuffer.end())
		{
            if (packet->time.msecsTo(QTime::currentTime()) > latency) {
                //qInfo(logAudio()) << "TX Packet too old " << dec << packet->time.msecsTo(QTime::currentTime()) << "ms";
				packet = audioBuffer.erase(packet); // returns next packet
			}
			else {
				if (packet->datain.length() == chunkSize && ret.length() == 0)
				{
					/*	We now have an array of samples in the computer native format (48000)
						If the radio sample rate is below 48000, we need to resample.
					*/

					if (ratioNum != 1) 
					{
						// We need to resample (we are STILL 16 bit!)
						quint32 outFrames = ((packet->datain.length() / 2) / ratioNum) / radioChannels;
						quint32 inFrames = (packet->datain.length() / 2) / radioChannels;
						packet->dataout.resize(outFrames * 2 * radioChannels); // Preset the output buffer size.

						const qint16* in = (qint16*)packet->datain.constData();
						qint16* out = (qint16*)packet->dataout.data();
						int err = 0;
						if (this->radioChannels == 1) {
							err = wf_resampler_process_int(resampler, 0, in, &inFrames, out, &outFrames);
						}
						else {
							err = wf_resampler_process_interleaved_int(resampler, in, &inFrames, out, &outFrames);
						}
						if (err) {
							qInfo(logAudio()) << (isInput ? "Input" : "Output") << "Resampler error " << err << " inFrames:" << inFrames << " outFrames:" << outFrames;
						}
						//qInfo(logAudio()) << "Resampler run " << err << " inFrames:" << inFrames << " outFrames:" << outFrames;
						//qInfo(logAudio()) << "Resampler run inLen:" << packet->datain.length() << " outLen:" << packet->dataout.length();
						if (radioSampleBits == 8)
						{
							packet->datain=packet->dataout; // Copy output packet back to input buffer.
							packet->dataout.clear(); // Buffer MUST be cleared ready to be re-filled by the upsampling below.
						}
					}
					else if (radioSampleBits == 16 ){
						// Only copy buffer if radioSampleBits is 16, as it will be handled below otherwise.
						packet->dataout = packet->datain;
					}

					// Do we need to convert 16-bit to 8-bit?
					if (radioSampleBits == 8) {
						packet->dataout.resize(packet->datain.length() / 2);
						qint16* in = (qint16*)packet->datain.data();
						for (int f = 0; f < packet->dataout.length(); f++)
						{
							quint8 outdata = 0;
							if (isUlaw) {
								qint16 enc = qFromLittleEndian<quint16>(in + f);
								if (enc >= 0)
									outdata = ulaw_encode[enc];
								else
									outdata = 0x7f & ulaw_encode[-enc];
							}
							else {
								outdata = (quint8)(((qFromLittleEndian<qint16>(in + f) >> 8) ^ 0x80) & 0xff);
							}
							packet->dataout[f] = (char)outdata;
						}
					}
					ret=packet->dataout;
					packet = audioBuffer.erase(packet); // returns next packet
				}
				else {
					packet++;
				}
			}
		}
	}
    return;
}

#endif

