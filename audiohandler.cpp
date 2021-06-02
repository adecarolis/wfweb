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
	Q_UNUSED(parent)
}

audioHandler::~audioHandler()
{
	//stop();

	if (resampler != Q_NULLPTR) {
		speex_resampler_destroy(resampler);
	}


	if (audio != Q_NULLPTR) {
		try {
			audio->abortStream();
			audio->closeStream();
		}
		catch (RtAudioError& e) {
			qInfo(logAudio()) << "Error closing stream:" << aParams.deviceId << ":" << QString::fromStdString(e.getMessage());
		}
		delete audio;
	}

    if (ringBuf != Q_NULLPTR) {
		delete ringBuf;
    }
}

bool audioHandler::init(const quint8 bits, const quint8 radioChan, const quint16 samplerate, const quint16 latency, const bool ulaw, const bool isinput, int port, quint8 resampleQuality)
{
	if (isInitialized) {
		return false;
	}

	this->audioLatency = latency;
	this->isUlaw = ulaw;
	this->isInput = isinput;
	this->radioSampleBits = bits;
	this->radioSampleRate = samplerate;
	this->radioChannels = radioChan;

	// chunk size is always relative to Internal Sample Rate.

	ringBuf = new wilt::Ring<audioPacket>(100); // Should be customizable.



	tempBuf.sent = 0;

	if (port > 0) {
		aParams.deviceId = port;
	}
	else if (isInput) {
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
		return false;
	}

	if (info.probed)
	{
		// if "preferred" sample rate is 44100, try 48K instead
		if (info.preferredSampleRate == (unsigned int)44100) {
			qDebug(logAudio()) << "Preferred sample rate 44100, trying 48000";
			this->nativeSampleRate = 48000;
		}
		else {
			this->nativeSampleRate = info.preferredSampleRate;
		}

		// Per channel chunk size.
		this->chunkSize = (this->nativeSampleRate / 50);

		qInfo(logAudio()) << (isInput ? "Input" : "Output") << QString::fromStdString(info.name) << "(" << aParams.deviceId << ") successfully probed";
		if (info.nativeFormats == 0)
		{
			qInfo(logAudio()) << "		No natively supported data formats!";
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
			if (isInput) {
				devChannels = info.inputChannels;
			} 
			else {
				devChannels = info.outputChannels;
			}
			qInfo(logAudio()) << "		Channels:" << devChannels;
			if (devChannels > 2) {
				devChannels = 2;
			}
			aParams.nChannels = devChannels;
		}

		qInfo(logAudio())  << "		chunkSize: " << chunkSize;
	}
	else
	{
		qCritical(logAudio()) << (isInput ? "Input" : "Output") << QString::fromStdString(info.name) << "(" << aParams.deviceId << ") could not be probed, check audio configuration!";
		return false;
	}

	int resample_error = 0;

#if !defined(Q_OS_MACX)
    options.flags = ((!RTAUDIO_HOG_DEVICE) | (RTAUDIO_MINIMIZE_LATENCY));
#endif

	if (isInput) {
        resampler = wf_resampler_init(devChannels, nativeSampleRate, samplerate, resampleQuality, &resample_error);
		try {
            audio->openStream(NULL, &aParams, RTAUDIO_SINT16, nativeSampleRate, &this->chunkSize, &staticWrite, this, &options);
			audio->startStream();
		}
		catch (RtAudioError& e) {
			qInfo(logAudio()) << "Error opening:" << QString::fromStdString(e.getMessage());
			return false;
		}
	}
	else
	{
		resampler = wf_resampler_init(devChannels, samplerate, this->nativeSampleRate, resampleQuality, &resample_error);
		try {
			audio->openStream(&aParams, NULL, RTAUDIO_SINT16, this->nativeSampleRate, &this->chunkSize, &staticRead, this, &options);
			audio->startStream();
		}
		catch (RtAudioError& e) {
			qInfo(logAudio()) << "Error opening:" << QString::fromStdString(e.getMessage());
			return false;
		}
	}
	qInfo(logAudio()) << (isInput ? "Input" : "Output") << "device successfully opened";

	qInfo(logAudio()) << (isInput ? "Input" : "Output") << "detected latency:" <<audio->getStreamLatency();

	wf_resampler_get_ratio(resampler, &ratioNum, &ratioDen);
	qInfo(logAudio()) << (isInput ? "Input" : "Output") << "wf_resampler_init() returned: " << resample_error << " ratioNum" << ratioNum << " ratioDen" << ratioDen;

    qInfo(logAudio()) << (isInput ? "Input" : "Output") << "thread id" << QThread::currentThreadId();
	return isInitialized;
}

void audioHandler::setVolume(unsigned char volume)
{
	this->volume = (qreal)volume/255.0;
	qInfo(logAudio()) << (isInput ? "Input" : "Output") << "setVolume: " << volume << "(" << this->volume << ")";
}



/// <summary>
/// This function processes the incoming audio FROM the radio and pushes it into the playback buffer *data
/// </summary>
/// <param name="data"></param>
/// <param name="maxlen"></param>
/// <returns></returns>
int audioHandler::readData(void* outputBuffer, void* inputBuffer, unsigned int nFrames, double streamTime, RtAudioStreamStatus status)
{
	Q_UNUSED(inputBuffer);
	Q_UNUSED(streamTime);
	// Calculate output length, always full samples
	int sentlen = 0;
	quint8* buffer = (quint8*)outputBuffer; 
	if (status == RTAUDIO_OUTPUT_UNDERFLOW)
		qDebug(logAudio()) << "Underflow detected";

	int nBytes = nFrames * devChannels * 2; // This is ALWAYS 2 bytes per sample and 2 channels

	if (ringBuf->size()>0)
	{
		// Output buffer is ALWAYS 16 bit.
		//qDebug(logAudio()) << "Read: nFrames" << nFrames << "nBytes" << nBytes;
		while (sentlen < nBytes)
		{
			audioPacket packet;
			if (!ringBuf->try_read(packet))
			{
				qDebug() << "No more data available but buffer is not full! sentlen:" << sentlen << " nBytes:" << nBytes ;
				break;
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
				if (tempBuf.sent != tempBuf.data.length())
				{
					// We still don't have enough buffer space for this?
					break;
				}
				//qDebug(logAudio()) << "Adding partial:" << send;
			}

			if (currentLatency > (int)audioLatency) {
				qInfo(logAudio()) << (isInput ? "Input" : "Output") << "Packet " << hex << packet.seq <<
					" arrived too late (increase output latency!) " <<
					dec << packet.time.msecsTo(QTime::currentTime()) << "ms";
				lastSeq = packet.seq;
				if (!ringBuf->try_read(packet))
					break;
				currentLatency = packet.time.msecsTo(QTime::currentTime());
			}

			int send = qMin((int)nBytes - sentlen, packet.data.length());
			memcpy(buffer + sentlen, packet.data.constData(), send);
			sentlen = sentlen + send;
			if (send < packet.data.length())
			{
				//qDebug(logAudio()) << "Asking for partial, sent:" << send << "packet length" << packet.data.length();
				tempBuf = packet;
				tempBuf.sent = tempBuf.sent + send;
				lastSeq = packet.seq;
				break;
			}

			if (packet.seq <= lastSeq) {
				qDebug(logAudio()) << (isInput ? "Input" : "Output") << "Duplicate/early audio packet: " << hex << lastSeq << " got " << hex << packet.seq;
			}
			else if (packet.seq != lastSeq + 1) {
				qDebug(logAudio()) << (isInput ? "Input" : "Output") << "Missing audio packet(s) from: " << hex << lastSeq + 1 << " to " << hex << packet.seq - 1;
			}
			lastSeq = packet.seq;
		}
	}
    //qDebug(logAudio()) << "looking for: " << nBytes << " got: " << sentlen;

    // fill the rest of the buffer with silence
    if (nBytes > sentlen) {
        memset(buffer+sentlen,0,nBytes-sentlen);
    }
	return 0;
}


int audioHandler::writeData(void* outputBuffer, void* inputBuffer, unsigned int nFrames, double streamTime, RtAudioStreamStatus status)
{
	Q_UNUSED(outputBuffer);
	Q_UNUSED(streamTime);
	Q_UNUSED(status);
	int sentlen = 0;
	int nBytes = nFrames * devChannels * 2; // This is ALWAYS 2 bytes per sample
	const char* data = (const char*)inputBuffer;
	//qDebug(logAudio()) << "nFrames" << nFrames << "nBytes" << nBytes;
	while (sentlen < nBytes) {
		if (tempBuf.sent != nBytes)
		{
			int send = qMin((int)(nBytes - sentlen), (int)nBytes - tempBuf.sent);
			tempBuf.data.append(QByteArray::fromRawData(data + sentlen, send));
			sentlen = sentlen + send;
			tempBuf.seq = 0; // Not used in TX
			tempBuf.time = QTime::currentTime();
			tempBuf.sent = tempBuf.sent + send;
		}
		else {
			ringBuf->write(tempBuf);
			/*
			if (!ringBuf->try_write(tempBuf))
			{
				qDebug(logAudio()) << "outgoing audio buffer full!";
				break;
			} */
			tempBuf.data.clear();
			tempBuf.sent = 0;
		}
	}

	//qDebug(logAudio()) << "sentlen" << sentlen;

	return 0; 
}


void audioHandler::incomingAudio(audioPacket inPacket)
{
	// No point buffering audio until stream is actually running.
	// Regardless of the radio stream format, the buffered audio will ALWAYS be
	// 16bit sample interleaved stereo 48K (or whatever the native sample rate is)

	if (!audio->isStreamRunning())
	{
		qDebug(logAudio()) << "Packet received before stream was started";
		return;
	}
    //qDebug(logAudio()) << "Got" << radioSampleBits << "bits, length" << inPacket.data.length();
	// Incoming data is 8bits?
	if (radioSampleBits == 8)
	{
		// Current packet is 8bit so need to create a new buffer that is 16bit 
		QByteArray outPacket((int)inPacket.data.length() * 2 *(devChannels/radioChannels), (char)0xff);
		qint16* out = (qint16*)outPacket.data();
		for (int f = 0; f < inPacket.data.length(); f++)
		{
			for (int g = radioChannels; g <= devChannels; g++)
			{
				if (isUlaw)
					*out++ = ulaw_decode[(quint8)inPacket.data[f]] * this->volume;
				else
					*out++ = (qint16)(((quint8)inPacket.data[f] << 8) - 32640 * this->volume);
			}
		}
		inPacket.data.clear();
		inPacket.data = outPacket; // Replace incoming data with converted.
	}
	else 
	{
		// This is already a 16bit stream, do we need to convert to stereo?
		if (radioChannels == 1 && devChannels > 1) {
			// Yes
			QByteArray outPacket(inPacket.data.length() * 2, (char)0xff); // Preset the output buffer size.
			qint16* in = (qint16*)inPacket.data.data();
			qint16* out = (qint16*)outPacket.data();
			for (int f = 0; f < inPacket.data.length() / 2; f++)
			{
				*out++ = (qint16)*in * this->volume;
				*out++ = (qint16)*in++ * this->volume;
			}
			inPacket.data.clear();
			inPacket.data = outPacket; // Replace incoming data with converted.
		}
		else 
		{
			// We already have the same number of channels so just update volume.
			qint16* in = (qint16*)inPacket.data.data();
			for (int f = 0; f < inPacket.data.length() / 2; f++)
			{
                *in = *in * this->volume;
                in++;
			}
		}

	}

	/*	We now have an array of 16bit samples in the NATIVE samplerate of the radio
		If the radio sample rate is below 48000, we need to resample.
		*/
    //qDebug(logAudio()) << "Now 16 bit stereo, length" << inPacket.data.length();

	if (ratioDen != 1) {

		// We need to resample
		// We have a stereo 16bit stream.
		quint32 outFrames = ((inPacket.data.length() / 2 / devChannels) * ratioDen);
		quint32 inFrames = (inPacket.data.length() / 2 / devChannels);
		QByteArray outPacket(outFrames * 4, (char)0xff); // Preset the output buffer size.

		const qint16* in = (qint16*)inPacket.data.constData();
		qint16* out = (qint16*)outPacket.data();

		int err = 0;
		err = wf_resampler_process_interleaved_int(resampler, in, &inFrames, out, &outFrames);
		if (err) {
			qInfo(logAudio()) << (isInput ? "Input" : "Output") << "Resampler error " << err << " inFrames:" << inFrames << " outFrames:" << outFrames;
		}
		inPacket.data.clear();
		inPacket.data = outPacket; // Replace incoming data with converted.
	}

    //qDebug(logAudio()) << "Adding packet to buffer:" << inPacket.seq << ": " << inPacket.data.length();

	if (!ringBuf->try_write(inPacket))
	{
		qDebug(logAudio()) << "Buffer full! capacity:" << ringBuf->capacity() << "length" << ringBuf->size();
	}
	return;
}

void audioHandler::changeLatency(const quint16 newSize)
{
	qInfo(logAudio()) << (isInput ? "Input" : "Output") << "Changing latency to: " << newSize << " from " << audioLatency;
	audioLatency = newSize;
}

int audioHandler::getLatency()
{
	return currentLatency;
}

void audioHandler::getNextAudioChunk(QByteArray& ret)
{
	audioPacket packet;
	packet.sent = 0;

	if (ringBuf != Q_NULLPTR && ringBuf->try_read(packet))
	{

		//qDebug(logAudio) << "Chunksize" << this->chunkSize << "Packet size" << packet.data.length();
		// Packet will arrive as stereo interleaved 16bit 48K
		if (ratioNum != 1)
		{
			quint32 outFrames = ((packet.data.length() / 2 / devChannels) / ratioNum);
			quint32 inFrames = (packet.data.length() / 2 / devChannels);
			QByteArray outPacket((int)outFrames * 2 * devChannels, (char)0xff);

			const qint16* in = (qint16*)packet.data.constData();
			qint16* out = (qint16*)outPacket.data();

			int err = 0;
			err = wf_resampler_process_interleaved_int(resampler, in, &inFrames, out, &outFrames);
			if (err) {
				qInfo(logAudio()) << (isInput ? "Input" : "Output") << "Resampler error " << err << " inFrames:" << inFrames << " outFrames:" << outFrames;
			}
			//qInfo(logAudio()) << "Resampler run " << err << " inFrames:" << inFrames << " outFrames:" << outFrames;
			//qInfo(logAudio()) << "Resampler run inLen:" << packet->datain.length() << " outLen:" << packet->dataout.length();
			packet.data.clear();
			packet.data = outPacket; // Copy output packet back to input buffer.
		}

		//qDebug(logAudio()) << "Now resampled, length" << packet.data.length();

		// Do we need to convert mono to stereo?
		if (radioChannels == 1 && devChannels > 1)
		{
			// Strip out right channel?
			QByteArray outPacket(packet.data.length()/2, (char)0xff);
			const qint16* in = (qint16*)packet.data.constData();
			qint16* out = (qint16*)outPacket.data();
			for (int f = 0; f < outPacket.length()/2; f++)
			{
				*out++ = *in++;
				in++; // Skip each even channel.
			}
			packet.data.clear();
			packet.data = outPacket; // Copy output packet back to input buffer.
		}
		
		//qDebug(logAudio()) << "Now mono, length" << packet.data.length();

		// Do we need to convert 16-bit to 8-bit?
		if (radioSampleBits == 8) {
			QByteArray outPacket((int)packet.data.length() / 2, (char)0xff);
			qint16* in = (qint16*)packet.data.data();
			for (int f = 0; f < outPacket.length(); f++)
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
				outPacket[f] = (char)outdata;
			}
			packet.data.clear();
			packet.data = outPacket; // Copy output packet back to input buffer.
		}
		ret = packet.data;
		//qDebug(logAudio()) << "Now radio format, length" << packet.data.length();
	}


	return;

}



