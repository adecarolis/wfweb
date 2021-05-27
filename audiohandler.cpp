/*
	This class handles both RX and TX audio, each is created as a seperate instance of the class
	but as the setup/handling if output (RX) and input (TX) devices is so similar I have combined them.
*/
#include "audiohandler.h"

#include "logcategories.h"
#include "ulaw.h"

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
		audio.closeStream();
	}
	if (ringBuf != Q_NULLPTR)
		delete ringBuf;
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

	ringBuf = new wilt::Ring<audioPacket>(100); // Should be customizable.
	tempBuf.sent = 0;

	if (port > 0) {
		aParams.deviceId = port;
	}
	else if (isInput) {
		aParams.deviceId = audio.getDefaultInputDevice();
	}
	else {
		aParams.deviceId = audio.getDefaultOutputDevice();
	}
	aParams.nChannels = 2; // Internally this is always 2 channels
	aParams.firstChannel = 0;

	try {
		info = audio.getDeviceInfo(aParams.deviceId);
	}
	catch (RtAudioError& e) {
		qInfo(logAudio()) << "Device error:" << aParams.deviceId << ":" << QString::fromStdString(e.getMessage());
		return false;
	}

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

	quint8* buffer = (quint8*)outputBuffer; 
	if (status == RTAUDIO_OUTPUT_UNDERFLOW)
		qDebug(logAudio()) << "Underflow detected";

	unsigned int nBytes = nFrames * 2 * 2; // This is ALWAYS 2 bytes per sample and 2 channels

	if (ringBuf->size()>0)
	{
		// Output buffer is ALWAYS 16 bit.

		while (sentlen < nBytes)
		{
			audioPacket packet;
			if (!ringBuf->try_read(packet))
			{
				qDebug() << "No more data available but buffer is not full! sentlen:" << sentlen << " nBytes:" << nBytes ;
				return 0;
			}
			currentLatency = packet.time.msecsTo(QTime::currentTime());

			// This shouldn't be required but if we did output a partial packet
			// This will add the remaining packet data to the output buffer.
			if (tempBuf.sent != tempBuf.data.length())
			{
				int send = qMin((int)nBytes - sentlen, tempBuf.data.length() - tempBuf.sent);
				memcpy(buffer + sentlen, tempBuf.data.constData() + tempBuf.sent, send);
				tempBuf.sent = tempBuf.sent + send;
				sentlen = sentlen + send;
				qDebug(logAudio()) << "Adding partial:" << send;
			}

			while (currentLatency > (int)audioLatency) {
				qInfo(logAudio()) << (isInput ? "Input" : "Output") << "Packet " << hex << packet.seq <<
					" arrived too late (increase output latency!) " <<
					dec << packet.time.msecsTo(QTime::currentTime()) << "ms";
				lastSeq = packet.seq;
				if (!ringBuf->try_read(packet))
					return sentlen;
				currentLatency = packet.time.msecsTo(QTime::currentTime());
			}

			int send = qMin((int)nBytes - sentlen, packet.data.length());
			memcpy(buffer + sentlen, packet.data.constData(), send);
			sentlen = sentlen + send;
			if (send < packet.data.length())
			{
				qDebug(logAudio()) << "Asking for partial, sent:" << send << "packet length" << packet.data.length();
				tempBuf = packet;
				tempBuf.sent = tempBuf.sent + send;
				//lastSeq = packet.seq;
				//break;
			}

			if (packet.seq <= lastSeq) {
				qInfo(logAudio()) << (isInput ? "Input" : "Output") << "Duplicate/early audio packet: " << hex << lastSeq << " got " << hex << packet.seq;
			}
			else if (packet.seq != lastSeq + 1) {
				qInfo(logAudio()) << (isInput ? "Input" : "Output") << "Missing audio packet(s) from: " << hex << lastSeq + 1 << " to " << hex << packet.seq - 1;
			}
			lastSeq = packet.seq;
		}
	}
	//qDebug(logAudio()) << "looking for: " << nBytes << " got: " << sentlen;

	return 0;
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

		current->data.append(QByteArray::fromRawData(data + sentlen, send));

		sentlen = sentlen + send;

		current->seq = 0; // Not used in TX
		current->time = QTime::currentTime();
		current->sent = current->data.length();

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



int audioHandler::incomingAudio(audioPacket data)
{
	// No point buffering audio until stream is actually running.
	if (!audio.isStreamRunning())
	{
		qDebug(logAudio()) << "Packet received before stream was started";
		return currentLatency;
	}
	// Incoming data is 8bits?
	if (radioSampleBits == 8)
	{
		QByteArray outPacket((int)data.data.length() * 2, (char)0xff);
		qint16* out = (qint16*)outPacket.data();
		for (int f = 0; f < data.data.length(); f++)
		{
			if (isUlaw)
			{
				out[f] = ulaw_decode[(quint8)data.data[f]];
			}
			else
			{
				// Convert 8-bit sample to 16-bit
				out[f] = (qint16)(((quint8)data.data[f] << 8) - 32640);
			}
		}
		data.data.clear();
		data.data = outPacket; // Replace incoming data with converted.
	}

	//qInfo(logAudio()) << "Adding packet to buffer:" << data.seq << ": " << data.data.length();

	/*	We now have an array of 16bit samples in the NATIVE samplerate of the radio
		If the radio sample rate is below 48000, we need to resample.
		*/

	if (ratioDen != 1) {

		// We need to resample
		quint32 outFrames = ((data.data.length() / 2) * ratioDen) / radioChannels;
		quint32 inFrames = (data.data.length() / 2) / radioChannels;
		QByteArray outPacket(outFrames * 2 * radioChannels, 0xff); // Preset the output buffer size.

		int err = 0;
		if (this->radioChannels == 1) {
			err = wf_resampler_process_int(resampler, 0, (const qint16*)data.data.constData(), &inFrames, (qint16*)outPacket.data(), &outFrames);
		}
		else {
			err = wf_resampler_process_interleaved_int(resampler, (const qint16*)data.data.constData(), &inFrames, (qint16*)outPacket.data(), &outFrames);
		}
		if (err) {
			qInfo(logAudio()) << (isInput ? "Input" : "Output") << "Resampler error " << err << " inFrames:" << inFrames << " outFrames:" << outFrames;
		}
		data.data.clear();
		data.data = outPacket; // Replace incoming data with converted.
	}

	if (radioChannels == 1)
	{
		// Convert to stereo
		QByteArray outPacket(data.data.length()*2, 0xff); // Preset the output buffer size.
		qint16* in = (qint16*)data.data.data();
		qint16* out = (qint16*)outPacket.data();
		for (int f = 0; f < data.data.length()/2; f++)
		{
			*out++ = *in;
			*out++ = *in++;
		}
		data.data.clear();
		data.data = outPacket; // Replace incoming data with converted.
	}

	if (!ringBuf->try_write(data))
	{
		qDebug(logAudio()) << "Buffer full! capacity:" << ringBuf->capacity() << "length" << ringBuf->size();
	}
	return currentLatency;
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
				if (packet->data.length() == chunkSize && ret.length() == 0)
				{
					//	We now have an array of samples in the computer native format (48000)
					//	If the radio sample rate is below 48000, we need to resample.
					

					if (ratioNum != 1)
					{
						// We need to resample (we are STILL 16 bit!)
						quint32 outFrames = ((packet->data.length() / 2) / ratioNum) / radioChannels;
						quint32 inFrames = (packet->data.length() / 2) / radioChannels;
						packet->dataout.resize(outFrames * 2 * radioChannels); // Preset the output buffer size.

						const qint16* in = (qint16*)packet->data.constData();
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
						//qInfo(logAudio()) << "Resampler run inLen:" << packet->data.length() << " outLen:" << packet->dataout.length();
						if (radioSampleBits == 8)
						{
							packet->data = packet->dataout; // Copy output packet back to input buffer.
							packet->dataout.clear(); // Buffer MUST be cleared ready to be re-filled by the upsampling below.
						}
					}
					else if (radioSampleBits == 16) {
						// Only copy buffer if radioSampleBits is 16, as it will be handled below otherwise.
						packet->dataout = packet->data;
					}

					// Do we need to convert 16-bit to 8-bit?
					if (radioSampleBits == 8) {
						packet->dataout.resize(packet->data.length() / 2);
						qint16* in = (qint16*)packet->data.data();
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



