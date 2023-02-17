#include "cwsidetone.h"

#include "logcategories.h"
#include "qapplication.h"

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
    output->stop();
    delete output;
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
    format.setSampleFormat(QAudioFormat::Float);
    QAudioDevice device = QMediaDevices::defaultAudioOutput();
#endif
    if (!device.isNull()) {
        if (!device.isFormatSupported(format)) {
            qWarning(logCW()) << "Default format not supported, using preferred";
            format = device.preferredFormat();
        }

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        output = new QAudioOutput(device,format);
#else
        output = new QAudioSink(device,format);
#endif

        output->setVolume((qreal)volume/100.0);

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        qInfo(logCW()) << QString("Sidetone Output: %0 (volume: %1 rate: %2 size: %3 type: %4)")
                          .arg(device.deviceName()).arg(volume).arg(format.sampleRate()).arg(format.sampleSize()).arg(format.sampleType());
#else
        qInfo(logCW()) << QString("Sidetone Output: %0 (volume: %1 rate: %2 type: %3")
                          .arg(device.description()).arg(volume).arg(format.sampleRate()).arg(format.sampleFormat());
#endif


        outputDevice = output->start();
    }
}

void cwSidetone::send(QString text)
{
    if (output != Q_NULLPTR && outputDevice != Q_NULLPTR) {
        text=text.simplified();
        buffer.clear();
        QString currentChar;
        int pos = 0;
        if (output->state() == QAudio::StoppedState || output->state() == QAudio::SuspendedState) {
            output->resume();
        }
        while (pos < text.size())
        {
            QChar ch = text.at(pos).toUpper();
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
            pos++;
        }
        if (outputDevice != Q_NULLPTR) {
            qint64 written = 0;
            while (written != -1 && written < buffer.size())
            {
                written += outputDevice->write(buffer.data() + written, buffer.size() - written);
                QApplication::processEvents();
            }
            if (written == -1)
            {
                qWarning(logCW()) << QString("Sidetone sending error occurred, aborting (%0 bytes of %1 remaining)").arg(buffer.size()-written).arg(buffer.size());
            }
        }
    }
    //qInfo(logCW()) << "Sending" << this->currentChar;
    emit finished();
    return;
}

void cwSidetone::generateMorse(QString morse)
{

    int dit = int(double(SIDETONE_MULTIPLIER / this->speed * SIDETONE_MULTIPLIER));
    int dah = int(dit * this->ratio);

    for (int i=0;i<morse.size();i++)
    {
        QChar c = morse.at(i);
        if (c == '-')
        {
            buffer.append(generateData(dah,this->frequency));
        } else if (c == '.')
        {
            buffer.append(generateData(dit,this->frequency));
        } else // Space char
        {
            buffer.append(generateData(dah+dit,0));
        }

        if (i+1<morse.size())
        {
            buffer.append(generateData(dit,0));
        }
        else
        {
            buffer.append(generateData(dah,0)); // inter-character space
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
    qint64 length = (sampleRate * channels * channelBytes) * len / 100000;
    const int sampleBytes = channels * channelBytes;

    length -= length % sampleBytes;
    Q_UNUSED(sampleBytes) // suppress warning in release builds

    data.resize(length);
    unsigned char *ptr = reinterpret_cast<unsigned char *>(data.data());
    int sampleIndex = 0;

    while (length) {
        const qreal x = qSin(2 * M_PI * freq * qreal(sampleIndex % sampleRate) / sampleRate);
        for (int i=0; i<channels; ++i) {
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
            if (format.sampleSize() == 8 && format.sampleType() == QAudioFormat::UnSignedInt)
#else
            if (format.sampleFormat() == QAudioFormat::UInt8)
#endif
            {
                const quint8 value = static_cast<quint8>((1.0 + x) / 2 * 255);
                *reinterpret_cast<quint8*>(ptr) = value;

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
            } else if (format.sampleType() == QAudioFormat::Float)
#else
            } else if (format.sampleFormat() == QAudioFormat::Float)
#endif
            {
                float value = static_cast<float>(x);
                qToLittleEndian<float>(value, ptr);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
            } else if (format.sampleSize() == 32 && format.sampleType() == QAudioFormat::SignedInt)
#else
            } else if (format.sampleFormat() == QAudioFormat::Int32)
#endif
            {
                qint32 value = static_cast<qint32>(x);
                qToLittleEndian<qint32>(value, ptr);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
            } else if (format.sampleSize() == 16 && format.sampleType() == QAudioFormat::SignedInt)
#else
            } else if (format.sampleFormat() == QAudioFormat::Int16)
#endif
            {
                qint16 value = static_cast<qint16>(x * 32767);
                qToLittleEndian<qint16>(value, ptr);
            }
            else {
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
                qWarning(logCW()) << QString("Unsupported sample size: %0 type: %1").arg(format.sampleSize()).arg(format.sampleType());
#else
                qWarning(logCW()) << QString("Unsupported sample format: %0").arg(format.sampleFormat());
#endif
            }
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
    if (output != Q_NULLPTR) {
        output->setVolume((qreal)level/100.0);
    }

}
