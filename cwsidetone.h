#ifndef CWSIDETONE_H
#define CWSIDETONE_H

#include <QApplication>
#include <QAudioOutput>
#include <QMap>

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
#include <QAudioDeviceInfo>
#include <QAudioOutput>
#else
#include <QAudioDevice>
#include <QAudioSink>
#include <QMediaDevices>
#endif

#include <QtMath>
#include <QtEndian>
#include <QTimer>

#include <deque>

//#define SIDETONE_MULTIPLIER 386.0
#define SIDETONE_MULTIPLIER 1095.46

class cwSidetone : public QObject
{
    Q_OBJECT
public:
    explicit cwSidetone(int level, int speed, int freq, double ratio, QWidget *parent = 0);
    ~cwSidetone();

signals:
    void finished();
public slots:
    void send(QString text);
    void setSpeed(unsigned char speed);
    void setFrequency(unsigned char frequency);
    void setRatio(unsigned char ratio);
    void setLevel(int level);
private:
    void init();

    void generateMorse(QString morse);
    QByteArray generateData(qint64 len, qint64 freq);
    QByteArray buffer;
    QMap< QChar, QString> cwTable;
    int position = 0;
    int charSpace = 20;
    int wordSpace = 60;
    QWidget* parent;
    int volume;
    int speed;
    int frequency;
    double ratio;
    QAudioFormat format;
    std::deque <QString> messages;
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QAudioOutput* output = Q_NULLPTR;
#else
    QAudioSink* output = Q_NULLPTR;
#endif
    QIODevice* outputDevice = Q_NULLPTR;
};

#endif // CWSIDETONE_H
