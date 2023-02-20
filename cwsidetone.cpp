#include "cwsidetone.h"

#include "logcategories.h"

cwSidetone::cwSidetone(int level, int speed, int freq, double ratio, QWidget* parent) :
    parent(parent),
    volume(level),
    speed(speed),
    frequency(freq),
    ratio(ratio)
{

    /*
     * Characters to match Icom table
     * Unknown characters will return '?'
    */
    cwTable.clear();
    cwTable['0'] = "-----";
    cwTable['1'] = ".----";
    cwTable['2'] = "..---";
    cwTable['3'] = "...--";
    cwTable['4'] = "....-";
    cwTable['5'] = ".....";
    cwTable['6'] = "-....";
    cwTable['7'] = "--...";
    cwTable['8'] = "---..";
    cwTable['9'] = "----.";

    cwTable['A'] = ".-";
    cwTable['B'] = "-...";
    cwTable['C'] = "-.-.";
    cwTable['D'] = "-..";
    cwTable['E'] = ".";
    cwTable['F'] = "..-.";
    cwTable['G'] = "--.";
    cwTable['H'] = "....";
    cwTable['I'] = "..";
    cwTable['J'] = ".---";
    cwTable['K'] = "-.-";
    cwTable['L'] = ".-..";
    cwTable['M'] = "--";
    cwTable['N'] = "-.";
    cwTable['O'] = "---";
    cwTable['P'] = ".--.";
    cwTable['Q'] = "--.-";
    cwTable['R'] = ".-.";
    cwTable['S'] = "...";
    cwTable['T'] = "-";
    cwTable['U'] = "..-";
    cwTable['V'] = "...-";
    cwTable['W'] = ".--";
    cwTable['X'] = "-..-";
    cwTable['Y'] = "-.--";
    cwTable['Z'] = "--..";

    cwTable['/'] = "-..-.";
    cwTable['?'] = "..--..";
    cwTable['.'] = ".-.-.-";
    cwTable['-'] = "-....-";
    cwTable[','] = "--..--";
    cwTable[':'] = "---...";
    cwTable['\''] = ".----.";
    cwTable['('] = "-.--.-";
    cwTable[')'] = "-.--.-";
    cwTable['='] = "-...-";
    cwTable['+'] = ".-.-.";
    cwTable['"'] = ".-..-.";

    cwTable[' '] = " ";

    init();
}

cwSidetone::~cwSidetone()
{
    qInfo(logCW()) << "cwSidetone() finished";
    this->stop();
    output->stop();
}

void cwSidetone::init()
{
    format.setSampleRate(44100);
    format.setChannelCount(1);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    format.setCodec("audio/pcm");
    format.setSampleSize(16);
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::SignedInt);
    QAudioDeviceInfo device(QAudioDeviceInfo::defaultOutputDevice());
#else
    format.setSampleFormat(QAudioFormat::Int16);
    QAudioDevice device = QMediaDevices::defaultAudioOutput();
#endif
    if (!device.isNull()) {
        if (!device.isFormatSupported(format)) {
            qWarning(logCW()) << "Default format not supported, using preferred";
            format = device.preferredFormat();
        }

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        output.reset(new QAudioOutput(device,format));
#else
        output.reset(new QAudioSink(device,format));
#endif

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        qInfo(logCW()) << QString("Sidetone Output: %0 (volume: %1 rate: %2 size: %3 type: %4)")
                          .arg(device.deviceName()).arg(volume).arg(format.sampleRate()).arg(format.sampleSize()).arg(format.sampleType());
#else
        qInfo(logCW()) << QString("Sidetone Output: %0 (volume: %1 rate: %2 type: %3")
                          .arg(device.description()).arg(volume).arg(format.sampleRate()).arg(format.sampleFormat());
#endif        
    }
    this->start(); // Start QIODevice
}

void cwSidetone::send(QString text)
{
    text=text.simplified();

    for (int pos=0; pos < text.size(); pos++)
    {
        QChar ch = text.at(pos).toUpper();
        QString currentChar;
        if (ch == NULL)
        {
            currentChar = cwTable[' '];
        }
        else if (this->cwTable.contains(ch))
        {
            currentChar = cwTable[ch];
        }
        else
        {
            currentChar=cwTable['?'];
        }
        generateMorse(currentChar);
    }

    if (output->state() == QAudio::StoppedState)
    {
        output->start(this);
    }
    else if (output->state() == QAudio::IdleState) {
        output->suspend();
        output->resume();
    }
    return;
}


void cwSidetone::start()
{
    open(QIODevice::ReadOnly);
}

void cwSidetone::stop()
{
    close();
}

qint64 cwSidetone::readData(char *data, qint64 len)
{
    QMutexLocker locker(&mutex);
    const qint64 total = qMin(((qint64)buffer.size()), len);
    memcpy(data, buffer.constData(), total);
    buffer.remove(0,total);
    if (buffer.size() == 0) {
        emit finished();
    }
    return total;
}

qint64 cwSidetone::writeData(const char *data, qint64 len)
{
    Q_UNUSED(data);
    Q_UNUSED(len);

    return 0;
}

qint64 cwSidetone::bytesAvailable() const
{
    return buffer.size() + QIODevice::bytesAvailable();
}

void cwSidetone::generateMorse(QString morse)
{

    int dit = int(double(SIDETONE_MULTIPLIER / this->speed * SIDETONE_MULTIPLIER));
    int dah = int(double(dit * this->ratio));


    QMutexLocker locker(&mutex);
    for (int i=0;i<morse.size();i++)
    {
        QChar c = morse.at(i);
        if (c == '-')
        {
            buffer.append(generateData(dah,this->frequency));
        }
        else if (c == '.')
        {
            buffer.append(generateData(dit,this->frequency));
        }
        else // Space char
        {
            buffer.append(generateData(dit,0));
        }

        if (i<morse.size()-1)
        {
            buffer.append(generateData(dit,0));
        }
        else
        {
            buffer.append(generateData(dit*3,0)); // inter-character space
        }
    }
    return;
}

QByteArray cwSidetone::generateData(qint64 len, qint64 freq)
{
    QByteArray data;

    const int channels = format.channelCount();
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    const int channelBytes = format.sampleSize() / 8;
#else
    const int channelBytes = format.bytesPerSample();
#endif

    const int sampleRate = format.sampleRate();
    //qint64 length = (sampleRate * channels * channelBytes) * len / 100000;
    qint64 length = format.bytesForDuration(len);
    const int sampleBytes = channels * channelBytes;

    length -= length % sampleBytes;
    Q_UNUSED(sampleBytes) // suppress warning in release builds

    data.resize(length);
    unsigned char *ptr = reinterpret_cast<unsigned char *>(data.data());
    int sampleIndex = 0;

    while (length) {
        const qreal x = (qSin(2 * M_PI * freq * qreal(sampleIndex % sampleRate) / sampleRate)) * qreal(volume/100.0);
        for (int i=0; i<channels; ++i) {
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
            if (format.sampleSize() == 8 && format.sampleType() == QAudioFormat::UnSignedInt)
                *reinterpret_cast<quint8 *>(ptr) = static_cast<quint8>((1.0 + x) / 2 * 255);
            else if (format.sampleSize() == 16 && format.sampleType() == QAudioFormat::SignedInt)
                *reinterpret_cast<qint16 *>(ptr) = static_cast<qint16>(x * std::numeric_limits<qint16>::max());
            else if (format.sampleSize() == 32 && format.sampleType() == QAudioFormat::SignedInt)
                *reinterpret_cast<qint32 *>(ptr) = static_cast<qint32>(x * std::numeric_limits<qint32>::max());
            else if (format.sampleType() == QAudioFormat::Float)
                *reinterpret_cast<float *>(ptr) = x;
            else
                qWarning(logCW()) << QString("Unsupported sample size: %0 type: %1").arg(format.sampleSize()).arg(format.sampleType());
#else
            if (format.sampleFormat() == QAudioFormat::UInt8)
                *reinterpret_cast<quint8 *>(ptr) = static_cast<quint8>((1.0 + x) / 2 * 255);
            else if (format.sampleFormat() == QAudioFormat::Int16)
                *reinterpret_cast<qint16 *>(ptr) = static_cast<qint16>(x * std::numeric_limits<qint16>::max());
            else if (format.sampleFormat() == QAudioFormat::Int32)
                *reinterpret_cast<qint32 *>(ptr) = static_cast<qint32>(x * std::numeric_limits<qint32>::max());
            else if (format.sampleFormat() == QAudioFormat::Float)
                *reinterpret_cast<float *>(ptr) = x;
            else
                qWarning(logCW()) << QString("Unsupported sample format: %0").arg(format.sampleFormat());
#endif

            ptr += channelBytes;
            length -= channelBytes;
        }
        ++sampleIndex;
    }

    return data;
}

void cwSidetone::setSpeed(unsigned char speed)
{
    this->speed = (int)speed;
}

void cwSidetone::setFrequency(unsigned char frequency)
{
    this->frequency = round((((600.0 / 255.0) * frequency) + 300) / 5.0) * 5.0;
}

void cwSidetone::setRatio(unsigned char ratio)
{
    this->ratio = (double)ratio/10.0;
}

void cwSidetone::setLevel(int level) {
    volume = level;
}

void cwSidetone::stopSending() {
    QMutexLocker locker(&mutex);
    buffer.clear();
    emit finished();
}


