/*
    This class handles both RX and TX audio, each is created as a seperate instance of the class
    but as the setup/handling if output (RX) and input (TX) devices is so similar I have combined them. 
*/
#include "audiohandler.h"

static int8_t uLawEncode(int16_t number)
{
    const uint16_t MULAW_MAX = 0x1FFF;
    const uint16_t MULAW_BIAS = 33;
    uint16_t mask = 0x1000;
    uint8_t sign = 0;
    uint8_t position = 12;
    uint8_t lsb = 0;
    if (number < 0)
    {
        number = -number;
        sign = 0x80;
    }
    number += MULAW_BIAS;
    if (number > MULAW_MAX)
    {
        number = MULAW_MAX;
    }
    for (; ((number & mask) != mask && position >= 5); mask >>= 1, position--)
        ;
    lsb = (number >> (position - 4)) & 0x0f;
    return (~(sign | ((position - 5) << 4) | lsb));
}


static qint16 uLawDecode(quint8 in)
{
    static const qint16 ulaw_decode[256] = {
    -32124, -31100, -30076, -29052, -28028, -27004, -25980, -24956,
    -23932, -22908, -21884, -20860, -19836, -18812, -17788, -16764,
    -15996, -15484, -14972, -14460, -13948, -13436, -12924, -12412,
    -11900, -11388, -10876, -10364,  -9852,  -9340,  -8828,  -8316,
     -7932,  -7676,  -7420,  -7164,  -6908,  -6652,  -6396,  -6140,
     -5884,  -5628,  -5372,  -5116,  -4860,  -4604,  -4348,  -4092,
     -3900,  -3772,  -3644,  -3516,  -3388,  -3260,  -3132,  -3004,
     -2876,  -2748,  -2620,  -2492,  -2364,  -2236,  -2108,  -1980,
     -1884,  -1820,  -1756,  -1692,  -1628,  -1564,  -1500,  -1436,
     -1372,  -1308,  -1244,  -1180,  -1116,  -1052,   -988,   -924,
      -876,   -844,   -812,   -780,   -748,   -716,   -684,   -652,
      -620,   -588,   -556,   -524,   -492,   -460,   -428,   -396,
      -372,   -356,   -340,   -324,   -308,   -292,   -276,   -260,
      -244,   -228,   -212,   -196,   -180,   -164,   -148,   -132,
      -120,   -112,   -104,    -96,    -88,    -80,    -72,    -64,
       -56,    -48,    -40,    -32,    -24,    -16,     -8,      0,
     32124,  31100,  30076,  29052,  28028,  27004,  25980,  24956,
     23932,  22908,  21884,  20860,  19836,  18812,  17788,  16764,
     15996,  15484,  14972,  14460,  13948,  13436,  12924,  12412,
     11900,  11388,  10876,  10364,   9852,   9340,   8828,   8316,
      7932,   7676,   7420,   7164,   6908,   6652,   6396,   6140,
      5884,   5628,   5372,   5116,   4860,   4604,   4348,   4092,
      3900,   3772,   3644,   3516,   3388,   3260,   3132,   3004,
      2876,   2748,   2620,   2492,   2364,   2236,   2108,   1980,
      1884,   1820,   1756,   1692,   1628,   1564,   1500,   1436,
      1372,   1308,   1244,   1180,   1116,   1052,    988,    924,
       876,    844,    812,    780,    748,    716,    684,    652,
       620,    588,    556,    524,    492,    460,    428,    396,
       372,    356,    340,    324,    308,    292,    276,    260,
       244,    228,    212,    196,    180,    164,    148,    132,
       120,    112,    104,     96,     88,     80,     72,     64,
        56,     48,     40,     32,     24,     16,      8,      0 };
    if (in == 0x02) // MUZERO
        in = 0;
    return ulaw_decode[in];
}


audioHandler::audioHandler(QObject* parent) :
    QIODevice(parent),
    isInitialized(false),
    audioOutput(Q_NULLPTR),
    audioInput(Q_NULLPTR),
    isUlaw(false),
    bufferSize(0),
    isInput(0),
    volume(1.0f)
{
}

audioHandler::~audioHandler()
{
    stop();    
    if (audioOutput != Q_NULLPTR) {
        delete audioOutput;
    }
    if (audioInput != Q_NULLPTR) {
        delete audioInput;
    }
}

bool audioHandler::init(const quint8 bits, const quint8 channels, const quint16 samplerate, const quint16 buffer, const bool ulaw, const bool isinput)
{
    if (isInitialized) {
        return false;
    }
    /* Always use 16 bit 48K samples internally*/
    format.setSampleSize(16);
    format.setChannelCount(channels);
    format.setSampleRate(48000);
    format.setCodec("audio/pcm");
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::SignedInt);

    this->bufferSize = buffer;
    this->isUlaw = ulaw;
    this->isInput = isinput;
    this->radioSampleBits = bits;
    this->radioSampleRate = samplerate;

    if (isInput)
        isInitialized = setDevice(QAudioDeviceInfo::defaultInputDevice());
    else
        isInitialized = setDevice(QAudioDeviceInfo::defaultOutputDevice());

    this->start();
    return isInitialized;
}


bool audioHandler::setDevice(QAudioDeviceInfo deviceInfo)
{
    qDebug() << this->metaObject()->className() << ": setDevice() running :" << deviceInfo.deviceName();
    if (!deviceInfo.isFormatSupported(format)) {
        if (deviceInfo.isNull())
        {
            qDebug() << "No audio device was found. You probably need to install libqt5multimedia-plugins.";
        }
        else {
            qDebug() << "Audio Devices found: ";
            const auto deviceInfos = QAudioDeviceInfo::availableDevices(QAudio::AudioOutput);
            for (const QAudioDeviceInfo& deviceInfo : deviceInfos)
            {
                qDebug() << "Device name: " << deviceInfo.deviceName();
                qDebug() << "is null (probably not good):" << deviceInfo.isNull();
                qDebug() << "channel count:" << deviceInfo.supportedChannelCounts();
                qDebug() << "byte order:" << deviceInfo.supportedByteOrders();
                qDebug() << "supported codecs:" << deviceInfo.supportedCodecs();
                qDebug() << "sample rates:" << deviceInfo.supportedSampleRates();
                qDebug() << "sample sizes:" << deviceInfo.supportedSampleSizes();
                qDebug() << "sample types:" << deviceInfo.supportedSampleTypes();
            }
            qDebug() << "----- done with audio info -----";
        }

        qDebug() << "Format not supported, choosing nearest supported format - which may not work!";
        deviceInfo.nearestFormat(format);
    }
    this->deviceInfo = deviceInfo;
    this->reinit();
    return true;
}

void audioHandler::reinit()
{
    qDebug() << this->metaObject()->className() << ": reinit() running";
    bool running = false;
    if (audioOutput && audioOutput->state() != QAudio::StoppedState) {
        running = true;
    }
    this->stop();

    // Calculate the minimum required audio buffer
    // This may need work depending on how it performs on other platforms.
    int audioBuffer = format.sampleRate() / 10;
    audioBuffer = audioBuffer * (format.sampleSize() / 8);

    if (this->isInput)
    {
        // (Re)initialize audio input
        delete audioInput;
        audioInput = Q_NULLPTR;
        audioInput = new QAudioInput(deviceInfo, format, this);
        audioInput->setBufferSize(audioBuffer);
        audioInput->setNotifyInterval(20);

        connect(audioInput, SIGNAL(notify()), SLOT(notified()));
        connect(audioInput, SIGNAL(stateChanged(QAudio::State)), SLOT(stateChanged(QAudio::State)));

    }
    else {
        // (Re)initialize audio output
        delete audioOutput;
        audioOutput = Q_NULLPTR;
        audioOutput = new QAudioOutput(deviceInfo, format, this);
        audioOutput->setBufferSize(audioBuffer);
        connect(audioOutput, SIGNAL(notify()), SLOT(notified()));
        connect(audioOutput, SIGNAL(stateChanged(QAudio::State)), SLOT(stateChanged(QAudio::State)));
    }

    if (running) {
        this->start();
    }
    flush();

}

void audioHandler::start()
{
    qDebug() << this->metaObject()->className() << ": start() running";

    if ((audioOutput == Q_NULLPTR || audioOutput->state() != QAudio::StoppedState) &&
            (audioInput == Q_NULLPTR || audioInput->state() != QAudio::StoppedState) ) {
        return;
    }

    if (isInput) {
        this->open(QIODevice::WriteOnly);
    }
    else {
        this->open(QIODevice::ReadOnly);
    }

    buffer.clear(); // No buffer used on audioinput.

    if (isInput) {
        audioInput->start(this);
    }
    else {
        audioOutput->start(this);
    }
}

void audioHandler::setVolume(float volume)
{
    volume = volume;
}


void audioHandler::flush()
{
    // Flushing buffers is a bit tricky...
    // Don't modify this unless you're sure
    qDebug() << this->metaObject()->className() << ": flush() running";
    this->stop();
    if (isInput) {
        audioInput->reset();
    }
    else {
        audioOutput->reset();
    }
    this->start();
}

void audioHandler::stop()
{
    if (audioOutput && audioOutput->state() != QAudio::StoppedState) {
        // Stop audio output
        audioOutput->stop();
        buffer.clear();
        this->close();
    }

    if (audioInput && audioInput->state() != QAudio::StoppedState) {
        // Stop audio output
        audioInput->stop();
        buffer.clear();
        this->close();
    }
}

qint64 audioHandler::readData(char* data, qint64 maxlen)
{
    // Calculate output length, always full samples
    int outlen = 0;
    if (isUlaw)
    {
        // Need to process uLaw.
        // Input buffer is 8bit and output buffer is 16bit 
        outlen = qMin(buffer.length(), (int)maxlen / 2);
        for (int f = 0; f < outlen; f++)
        {
            qToLittleEndian<qint16>(uLawDecode(buffer.at(f)), data + f * 2);
        }
        buffer.remove(0, outlen);
        outlen = outlen * 2;
    }
    else {
        if (radioSampleBits == 8)
        {
            outlen = qMin(buffer.length(), (int)maxlen/2);
            for (int f = 0; f < outlen; f++)
            {
                qToLittleEndian<qint16>((qint16)(buffer[f]<<8) - 32640, data + f * 2);
            }
            buffer.remove(0, outlen);
            outlen = outlen * 2;
        } else {
            // Just copy it.
            outlen = qMin(buffer.length(), (int)maxlen);
            if (outlen % 2 != 0) {
                outlen += 1;
            }
            memcpy(data, buffer.data(), outlen);
        }
        buffer.remove(0, outlen);
    }

    return outlen;
}

qint64 audioHandler::writeData(const char* data, qint64 len)
{
    QMutexLocker locker(&mutex);

    if (buffer.length() > bufferSize * 4)
    {
        qWarning() << "writeData() Buffer overflow";
        buffer.clear();
    }

    //int chunkSize = 960; // Assume 8bit or uLaw.
    if (isUlaw) {
        for (int f = 0; f < len / 2; f++)
        {
            buffer.append(uLawEncode(qFromLittleEndian<qint16>(data + f * 2)));
        }
    }
    else if (radioSampleBits == 8) {
        for (int f = 0; f < len / 2; f++)
        {
            buffer.append((quint8)(((qFromLittleEndian<qint16>(data + f * 2) >> 8) ^ 0x80) & 0xff));
        }
    }
    else if (radioSampleBits == 16) {
        buffer.append(QByteArray::fromRawData(data, len));
        //chunkSize = 1920;
    }
    else {
        qWarning() << "Unsupported number of bits! :" << radioSampleBits;
    }
       
    return (len); // Always return the same number as we received
}

qint64 audioHandler::bytesAvailable() const
{
    return buffer.length() + QIODevice::bytesAvailable();
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
    if (state == QAudio::IdleState && audioOutput->error() == QAudio::UnderrunError) {
        qDebug() << this->metaObject()->className() << "RX:Buffer underrun";
        //if (buffer.length() < bufferSize) {
        //    audioOutput->suspend();
        //}
    }
    //qDebug() << this->metaObject()->className() << ": state = " << state;
}



void audioHandler::incomingAudio(const QByteArray& data)
{
    //qDebug() << "Got " << data.length() << " samples";
    QMutexLocker locker(&mutex);
    if (audioOutput != Q_NULLPTR && audioOutput->state() != QAudio::StoppedState) {
        // Append input data to the end of buffer
        buffer.append(data);

        //if (buffer.length() > bufferSize*2) {
        //    this->flush();
        //}

        // If audio is suspended and buffer is full, resume
        if (audioOutput->state() == QAudio::SuspendedState) {
            if (buffer.length() >= bufferSize) {
                qDebug() << this->metaObject()->className() << ": Resuming...";
                audioOutput->resume();
            }
        }
    }
    else {
        qDebug() << this->metaObject()->className() << ": Audio received from radio but audio output is stopped!";
    }
}

void audioHandler::changeBufferSize(const quint16 newSize)
{
    QMutexLocker locker(&mutex);
    qDebug() << this->metaObject()->className() << ": Changing buffer size to: " << newSize << " from " << bufferSize;
    bufferSize = newSize;
    flush();
}

void audioHandler::getBufferSize()
{
    emit sendBufferSize(audioOutput->bufferSize());
}

QByteArray audioHandler::getNextAudioChunk()
{
    QMutexLocker locker(&mutex);
    quint16 numSamples = radioSampleBits * 120;
    QByteArray ret;
    if (buffer.size() >= numSamples) {
        ret.append(buffer.mid(0, numSamples));
        buffer.remove(0, numSamples);
    }

    return ret;
}



