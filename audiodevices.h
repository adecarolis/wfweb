#ifndef AUDIODEVICES_H
#define AUDIODEVICES_H
#include <QObject>

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
#include <QAudioDeviceInfo>
#else
#include <QMediaDevices>
#include <QAudioDevice>
#include <QString>
#endif

#include <QFontMetrics>

#include <portaudio.h>
#ifndef Q_OS_LINUX
#include "RtAudio.h"
#else
#include "rtaudio/RtAudio.h"
#endif

#include "wfviewtypes.h"


struct audioDevice {
    audioDevice(QString name, int deviceInt, bool isDefault) : name(name), deviceInt(deviceInt), isDefault(isDefault) {};

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    audioDevice(QString name, const QAudioDeviceInfo deviceInfo, QString realm, bool isDefault) : name(name), deviceInfo(deviceInfo), realm(realm), isDefault(isDefault) {};
#else
    audioDevice(QString name, QAudioDevice deviceInfo, QString realm, bool isDefault) : name(name), deviceInfo(deviceInfo), realm(realm), isDefault(isDefault) {};
#endif

    QString name;
    int deviceInt;

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    const QAudioDeviceInfo deviceInfo;
#else
    QAudioDevice deviceInfo;
#endif

    QString realm;
    bool isDefault;
};

class audioDevices : public QObject
{
    Q_OBJECT

public:
    explicit audioDevices(audioType type, QFontMetrics fm, QObject* parent = nullptr);
    ~audioDevices();
    void setAudioType(audioType type) { system = type; };
    audioType getAudioType() { return system; };
    int getNumCharsIn() { return numCharsIn; };
    int getNumCharsOut() { return numCharsOut; };

    QString getInputName(int num) { return inputs[num]->name; };
    QString getOutputName(int num) { return outputs[num]->name; };

    int getInputDeviceInt(int num) { return inputs[num]->deviceInt; };
    int getOutputDeviceInt(int num) { return outputs[num]->deviceInt; };

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    const QAudioDeviceInfo getInputDeviceInfo(int num) { return inputs[num]->deviceInfo; };
    const QAudioDeviceInfo getOutputDeviceInfo(int num) { return outputs[num]->deviceInfo; };
#else
    const QAudioDevice getInputDeviceInfo(int num) { return inputs[num]->deviceInfo; };
    const QAudioDevice getOutputDeviceInfo(int num) { return outputs[num]->deviceInfo; };
#endif

    void enumerate();

    QStringList getInputs();
    QStringList getOutputs();

    int findInput(QString type, QString name);
    int findOutput(QString type, QString name);

#if (QT_VERSION >= QT_VERSION_CHECK(6,0,0))
    QMediaDevices mediaDevices;
#endif

public slots:

signals:
    void updated();
protected:
private:

    audioType system;
    QFontMetrics fm;
    QString defaultInputDeviceName;
    QString defaultOutputDeviceName;
    int numInputDevices;
    int numOutputDevices;
    QList<audioDevice*> inputs;
    QList<audioDevice*> outputs;
    int numCharsIn = 0;
    int numCharsOut = 0;
    QString audioApi = "wasapi";

};

#endif
