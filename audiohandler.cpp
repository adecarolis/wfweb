/*
	This class handles both RX and TX audio, each is created as a seperate instance of the class
	but as the setup/handling if output (RX) and input (TX) devices is so similar I have combined them.
*/
#include "audiohandler.h"

#include "logcategories.h"


audioHandler::audioHandler(QObject* parent) :
	isInitialized(false),
	isUlaw(false),
	audioLatency(0),
	isInput(0),
	chunkAvailable(false)
{
}

audioHandler::~audioHandler()
{
	//stop();

	if (resampler != Q_NULLPTR) {
		speex_resampler_destroy(resampler);
	}
	if (audio.isStreamRunning())
	{
		audio.stopStream();
	}
	delete buf;
}

bool audioHandler::init(const quint8 bits, const quint8 channels, const quint16 samplerate, const quint16 latency, const bool ulaw, const bool isinput, int port, quint8 resampleQuality)
{
	if (isInitialized) {
		return false;
	}

	this->audioLatency = latency;
	this->isUlaw = ulaw;
	this->isInput = isinput;
	this->radioSampleBits = bits;
	this->radioSampleRate = samplerate;
	this->radioChannels = channels;

	// chunk size is always relative to Internal Sample Rate.
	this->chunkSize = (INTERNAL_SAMPLE_RATE / 25) * radioChannels;

	if (port > 0) {
		aParams.deviceId = port;
	}
	else if (isInput) {
		aParams.deviceId = audio.getDefaultInputDevice();
	}
	else {
		aParams.deviceId = audio.getDefaultOutputDevice();
	}
	aParams.nChannels = channels;
	aParams.firstChannel = 0;

	info = audio.getDeviceInfo(aParams.deviceId);

	buf = new quint16[960];

	if (info.probed)
	{
		qInfo(logAudio()) << (isInput ? "Input" : "Output") << QString::fromStdString(info.name) << "(" << aParams.deviceId << ") successfully probed";
		if (info.nativeFormats == 0)
			qInfo(logAudio()) << "		No natively supported data formats!";
		else {
			qDebug(logAudio()) << "		Supported formats:" <<
				(info.nativeFormats & RTAUDIO_SINT8 ? "8-bit int," : "") <<
				(info.nativeFormats & RTAUDIO_SINT16 ? "16-bit int," : "") <<
				(info.nativeFormats & RTAUDIO_SINT24 ? "24-bit int," : "") <<
				(info.nativeFormats & RTAUDIO_SINT32 ? "32-bit int," : "") <<
				(info.nativeFormats & RTAUDIO_FLOAT32 ? "32-bit float," : "") <<
				(info.nativeFormats & RTAUDIO_FLOAT64 ? "64-bit float," : "");
			qInfo(logAudio()) << "		Preferred sample rate:" << info.preferredSampleRate;
		}

		qInfo(logAudio())  << "		chunkSize: " << chunkSize;
	}
	else
	{
		qCritical(logAudio()) << (isInput ? "Input" : "Output") << QString::fromStdString(info.name) << "(" << aParams.deviceId << ") could not be probed, check audio configuration!";
	}

	int resample_error = 0;

	if (isInput) {
		resampler = wf_resampler_init(radioChannels, INTERNAL_SAMPLE_RATE, samplerate, resampleQuality, &resample_error);
		try {
			audio.openStream(NULL, &aParams, RTAUDIO_SINT16, INTERNAL_SAMPLE_RATE, &this->chunkSize, &staticWrite, this);
			audio.startStream();
		}
		catch (RtAudioError& e) {
			qInfo(logAudio()) << "Error opening:" << QString::fromStdString(e.getMessage());
			return false;
		}
	}
	else
	{
		resampler = wf_resampler_init(radioChannels, samplerate, INTERNAL_SAMPLE_RATE, resampleQuality, &resample_error);
		try {
			unsigned int length = chunkSize / 2;
			audio.openStream(&aParams, NULL, RTAUDIO_SINT16, INTERNAL_SAMPLE_RATE, &length, &staticRead, this);
			audio.startStream();
		}
		catch (RtAudioError& e) {
			qInfo(logAudio()) << "Error opening:" << QString::fromStdString(e.getMessage());
			return false;
		}
	}
	qInfo(logAudio()) << (isInput ? "Input" : "Output") << "device successfully opened";

	wf_resampler_get_ratio(resampler, &ratioNum, &ratioDen);
	qInfo(logAudio()) << (isInput ? "Input" : "Output") << "wf_resampler_init() returned: " << resample_error << " ratioNum" << ratioNum << " ratioDen" << ratioDen;

	return isInitialized;
}

void audioHandler::setVolume(unsigned char volume)
{
	qInfo(logAudio()) << (isInput ? "Input" : "Output") << "setVolume: " << volume << "(" << (qreal)(volume/255.0) << ")";
}



/// <summary>
/// This function processes the incoming audio FROM the radio and pushes it into the playback buffer *data
/// </summary>
/// <param name="data"></param>
/// <param name="maxlen"></param>
/// <returns></returns>
int audioHandler::readData(void* outputBuffer, void* inputBuffer, unsigned int nFrames, double streamTime, RtAudioStreamStatus status)
{
	// Calculate output length, always full samples
	int sentlen = 0;
	qint16* buffer = (qint16*)outputBuffer;
	//qDebug(logAudio()) << "looking for: " << nFrames << this->audioBuffer.size();


	if (!audioBuffer.isEmpty())
	{

		// Output buffer is ALWAYS 16 bit.
		auto packet = audioBuffer.begin();

		while (packet != audioBuffer.end() && sentlen < nFrames/2)
		{
			int timediff = packet->time.msecsTo(QTime::currentTime());

			if (timediff > (int)audioLatency * 2) {
				qInfo(logAudio()) << (isInput ? "Input" : "Output") << "Packet " << hex << packet->seq <<
					" arrived too late (increase output latency!) " <<
					dec << packet->time.msecsTo(QTime::currentTime()) << "ms";
				while (packet != audioBuffer.end() && timediff > (int)audioLatency) {
					timediff = packet->time.msecsTo(QTime::currentTime());
					lastSeq = packet->seq;
					packet = audioBuffer.erase(packet); // returns next packet
				}
				if (packet == audioBuffer.end()) {
					break;
				}
			}

			// If we got here then packet time must be within latency threshold

			if (packet->seq == lastSeq + 1 || packet->seq <= lastSeq)
			{
				int send = qMin((int)nFrames*2 - sentlen, packet->dataout.length() - packet->sent);
				lastSeq = packet->seq;
				//qInfo(logAudio()) << "Packet " << hex << packet->seq << " arrived on time " << Qt::dec << packet->time.msecsTo(QTime::currentTime()) << "ms";

				memcpy(buffer + sentlen, packet->dataout.constData() + packet->sent, send);

				sentlen = sentlen + send;

				if (send == packet->dataout.length() - packet->sent)
				{
					//qInfo(logAudio()) << "Get next packet";
					packet = audioBuffer.erase(packet); // returns next packet
				}
				else
				{
					// Store sent amount (could be zero if audioOutput buffer full) then break.
					packet->sent = send;
					break;
				}
			}
			else {
				qInfo(logAudio()) << (isInput ? "Input" : "Output") << "Missing audio packet(s) from: " << hex << lastSeq + 1 << " to " << hex << packet->seq - 1;
				lastSeq = packet->seq;
			}
		}
	}
	else {
		// Fool audio system into thinking it has valid data, this seems to be required 
		// for MacOS Built in audio but shouldn't cause any issues with other platforms.
		return 0;
	}


	return 0;
}

quint16 audioHandler::getBuffer(int i) {
	return buf[i];
}

int audioHandler::writeData(void* outputBuffer, void* inputBuffer, unsigned int nFrames, double streamTime, RtAudioStreamStatus status)
{
	int sentlen = 0;
	/*
	QMutexLocker locker(&mutex);
	audioPacket* current;

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

		int send = qMin((int)(len - sentlen), (int)chunkSize - current->sent);

		current->datain.append(QByteArray::fromRawData(data + sentlen, send));

		sentlen = sentlen + send;

		current->seq = 0; // Not used in TX
		current->time = QTime::currentTime();
		current->sent = current->datain.length();

		if (current->sent == chunkSize)
		{
			chunkAvailable = true;
		}
		else if (audioBuffer.length() <= 1 && current->sent != chunkSize) {
			chunkAvailable = false;
		}

	}
	*/
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
	
}



void audioHandler::incomingAudio(audioPacket data)
{
	// No point buffering audio until stream is actually running.
	if (!audio.isStreamRunning())
	{
		return;
	}
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
				qInfo(logAudio()) << (isInput ? "Input" : "Output") << "Resampler error " << err << " inFrames:" << inFrames << " outFrames:" << outFrames;
			}
		}
		else {
			data.dataout = data.datain;
		}

		//memcpy(buf, data.dataout.constData(), data.dataout.length());
		//qDebug(logAudio()) << "Got data: " << data.dataout.length();
		audioBuffer.insert( data.seq, data );
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
	/*
	if (!audioBuffer.isEmpty() && chunkAvailable)
	{

		QMutexLocker locker(&mutex);

		// Skip through audio buffer deleting any old entry.
		auto packet = audioBuffer.begin();
		while (packet != audioBuffer.end())
		{
			if (packet->time.msecsTo(QTime::currentTime()) > 100) {
				//qInfo(logAudio()) << "TX Packet too old " << dec << packet->time.msecsTo(QTime::currentTime()) << "ms";
				packet = audioBuffer.erase(packet); // returns next packet
			}
			else {
				if (packet->datain.length() == chunkSize && ret.length() == 0)
				{
					//	We now have an array of samples in the computer native format (48000)
					//	If the radio sample rate is below 48000, we need to resample.
					

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
							packet->datain = packet->dataout; // Copy output packet back to input buffer.
							packet->dataout.clear(); // Buffer MUST be cleared ready to be re-filled by the upsampling below.
						}
					}
					else if (radioSampleBits == 16) {
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
					ret = packet->dataout;
					packet = audioBuffer.erase(packet); // returns next packet
				}
				else {
					packet++;
				}
			}
		}
	}
	*/
	return;
}



