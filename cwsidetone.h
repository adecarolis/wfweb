#ifndef CWSIDETONE_H
#define CWSIDETONE_H

#include <QApplication>
#include <QAudioOutput>
#include <QMap>
#include <QScopedPointer>
#include <QtMath>
#include <QMutex>
#include <QMutexLocker>

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
#include <QAudioDeviceInfo>
#include <QAudioOutput>
#else
#include <QAudioDevice>
#include <QAudioSink>
#include <QMediaDevices>
#endif

//#define SIDETONE_MULTIPLIER 386.0
#define SIDETONE_MULTIPLIER 1095.46

class cwSidetone : public QIODevice
{
    Q_OBJECT
public:
    explicit cwSidetone(int level, int speed, int freq, double ratio, QWidget *parent = 0);
    ~cwSidetone();

    void start();
    void stop();

    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *data, qint64 len) override;
    qint64 bytesAvailable() const override;
    qint64 size() const override { return buffer.size(); }

signals:
    void finished();
public slots:
    void send(QString text);
    void setSpeed(unsigned char speed);
    void setFrequency(unsigned char frequency);
    void setRatio(unsigned char ratio);
    void setLevel(int level);
    void stopSending();
private:
    void init();

    void generateMorse(QString morse);
    QByteArray generateData(qint64 len, qint64 freq);
    QByteArray buffer;
    QMap< QChar, QString> cwTable;
    QWidget* parent;
    int volume;
    int speed;
    int frequency;
    double ratio;
    QAudioFormat format;
    QMutex mutex;
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QScopedPointer<QAudioOutput> output;
#else
    QScopedPointer<QAudioSink> output;
#endif
};

#endif // CWSIDETONE_H
