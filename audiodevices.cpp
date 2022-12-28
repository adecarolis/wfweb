/* 
	wfview class to enumerate audio devices and assist with matching saved devices

	Written by Phil Taylor M0VSE Nov 2022.

*/

#include "audiodevices.h"
#include "logcategories.h"

audioDevices::audioDevices(audioType type, QFontMetrics fm, QObject* parent) :
    system(type),
    fm(fm),
	QObject(parent)
{
    
}


void audioDevices::enumerate()
{
    numInputDevices = 0;
    numOutputDevices = 0;
    numCharsIn = 0;
    numCharsOut = 0;
    inputs.clear();
    outputs.clear();
    switch (system)
    {
        case qtAudio:
        {
            Pa_Terminate();

            foreach(const QAudioDeviceInfo & deviceInfo, QAudioDeviceInfo::availableDevices(QAudio::AudioInput))
            {
                bool isDefault = false;
                if (numInputDevices == 0) {
                    defaultInputDeviceName = QString(deviceInfo.deviceName());
                }
#ifdef Q_OS_WIN
                if (deviceInfo.realm() == "wasapi") {
#endif
                    /* Append Input Device Here */
                    if (deviceInfo.deviceName() == defaultInputDeviceName) {
                        isDefault = true;
                    }
                    inputs.append(audioDevice(deviceInfo.deviceName(), deviceInfo, deviceInfo.realm(),isDefault ));

                    if (fm.boundingRect(deviceInfo.deviceName()).width() > numCharsIn)
                        numCharsIn = fm.boundingRect(deviceInfo.deviceName()).width();

#ifdef Q_OS_WIN
                }
#endif
                numInputDevices++;
            }

            foreach(const QAudioDeviceInfo & deviceInfo, QAudioDeviceInfo::availableDevices(QAudio::AudioOutput))
            {
                bool isDefault = false;
                if (numOutputDevices == 0)
                {
                    defaultOutputDeviceName = QString(deviceInfo.deviceName());
                }
#ifdef Q_OS_WIN
                if (deviceInfo.realm() == "wasapi") {
#endif
                    /* Append Output Device Here */
                    if (deviceInfo.deviceName() == defaultOutputDeviceName) {
                        isDefault = true;
                    }
                    outputs.append(audioDevice(deviceInfo.deviceName(), deviceInfo, deviceInfo.realm(),isDefault));
                    if (fm.boundingRect(deviceInfo.deviceName()).width() > numCharsOut)
                        numCharsOut = fm.boundingRect(deviceInfo.deviceName()).width();

#ifdef Q_OS_WIN
                }
#endif
                numOutputDevices++;
            }
            break;
        }
        case portAudio:
        {
            PaError err;

            err = Pa_Initialize();

            if (err != paNoError)
            {
                qInfo(logAudio()) << "ERROR: Cannot initialize Portaudio";
                return;
            }

            qInfo(logAudio()) << "PortAudio version: " << Pa_GetVersionInfo()->versionText;

            int numDevices = Pa_GetDeviceCount();
            qInfo(logAudio()) << "Pa_CountDevices returned" << numDevices;

            const PaDeviceInfo* info;
            for (int i = 0; i < numDevices; i++)
            {
                info = Pa_GetDeviceInfo(i);
                if (info->maxInputChannels > 0) {
                    bool isDefault = false;
                    numInputDevices++;
                    qDebug(logAudio()) << (i == Pa_GetDefaultInputDevice() ? "*" : " ") << "(" << i << ") Input Device : " << QString(info->name);
                    if (i == Pa_GetDefaultInputDevice()) {
                        defaultInputDeviceName = info->name;
                        isDefault = true;
                    }
                    inputs.append(audioDevice(QString(info->name), i,isDefault));
                    if (fm.boundingRect(QString(info->name)).width() > numCharsIn)
                        numCharsIn = fm.boundingRect(QString(info->name)).width();

                }
                if (info->maxOutputChannels > 0) {
                    bool isDefault = false;
                    numOutputDevices++;
                    qDebug(logAudio()) << (i == Pa_GetDefaultOutputDevice() ? "*" : " ") << "(" << i << ") Output Device  : " << QString(info->name);
                    if (i == Pa_GetDefaultOutputDevice()) {
                        defaultOutputDeviceName = info->name;
                        isDefault = true;
                    }
                    outputs.append(audioDevice(QString(info->name), i,isDefault));
                    if (fm.boundingRect(QString(info->name)).width() > numCharsOut)
                        numCharsOut = fm.boundingRect(QString(info->name)).width();
                }
            }
            break;
        }
        case rtAudio:
        {
            Pa_Terminate();

#if defined(Q_OS_LINUX)
            RtAudio* audio = new RtAudio(RtAudio::Api::LINUX_ALSA);
#elif defined(Q_OS_WIN)
            RtAudio* audio = new RtAudio(RtAudio::Api::WINDOWS_WASAPI);
#elif defined(Q_OS_MACX)
            RtAudio* audio = new RtAudio(RtAudio::Api::MACOSX_CORE);
#endif


            // Enumerate audio devices, need to do before settings are loaded.
            std::map<int, std::string> apiMap;
            apiMap[RtAudio::MACOSX_CORE] = "OS-X Core Audio";
            apiMap[RtAudio::WINDOWS_ASIO] = "Windows ASIO";
            apiMap[RtAudio::WINDOWS_DS] = "Windows DirectSound";
            apiMap[RtAudio::WINDOWS_WASAPI] = "Windows WASAPI";
            apiMap[RtAudio::UNIX_JACK] = "Jack Client";
            apiMap[RtAudio::LINUX_ALSA] = "Linux ALSA";
            apiMap[RtAudio::LINUX_PULSE] = "Linux PulseAudio";
            apiMap[RtAudio::LINUX_OSS] = "Linux OSS";
            apiMap[RtAudio::RTAUDIO_DUMMY] = "RtAudio Dummy";

            std::vector< RtAudio::Api > apis;
            RtAudio::getCompiledApi(apis);

            qInfo(logAudio()) << "RtAudio Version " << QString::fromStdString(RtAudio::getVersion());

            qInfo(logAudio()) << "Compiled APIs:";
            for (unsigned int i = 0; i < apis.size(); i++) {
                qInfo(logAudio()) << "  " << QString::fromStdString(apiMap[apis[i]]);
            }

            RtAudio::DeviceInfo info;

            qInfo(logAudio()) << "Current API: " << QString::fromStdString(apiMap[audio->getCurrentApi()]);

            unsigned int devices = audio->getDeviceCount();
            qInfo(logAudio()) << "Found " << devices << " audio device(s) *=default";

            for (unsigned int i = 1; i < devices; i++) {
                info = audio->getDeviceInfo(i);
                if (info.inputChannels > 0) {
                    bool isDefault = false;
                    qInfo(logAudio()) << (info.isDefaultInput ? "*" : " ") << "(" << i << ") Input Device  : " << QString::fromStdString(info.name);
                    numInputDevices++;

                    if (info.isDefaultInput) {
                        defaultInputDeviceName = QString::fromStdString(info.name);
                        isDefault = true;
                    }

                    inputs.append(audioDevice(QString::fromStdString(info.name), i, isDefault));

                    if (fm.boundingRect(QString::fromStdString(info.name)).width() > numCharsIn)
                        numCharsIn = fm.boundingRect(QString::fromStdString(info.name)).width();

                }
                if (info.outputChannels > 0) {
                    bool isDefault = false;
                    qInfo(logAudio()) << (info.isDefaultOutput ? "*" : " ") << "(" << i << ") Output Device : " << QString::fromStdString(info.name);
                    numOutputDevices++;

                    if (info.isDefaultOutput) {
                        defaultOutputDeviceName = QString::fromStdString(info.name);
                        isDefault = true;
                    }

                    outputs.append(audioDevice(QString::fromStdString(info.name), i, isDefault));

                    if (fm.boundingRect(QString::fromStdString(info.name)).width() > numCharsOut)
                        numCharsOut = fm.boundingRect(QString::fromStdString(info.name)).width();
                }
            }

            delete audio;
            break;
        }

    }
    emit updated();

}

audioDevices::~audioDevices()
{

}

QStringList audioDevices::getInputs()
{
    QStringList list;
    foreach(const audioDevice input, inputs) {
        list.append(input.name);
    }

    return list;
}

QStringList audioDevices::getOutputs()
{
    QStringList list;
    foreach(const audioDevice output, outputs) {
        list.append(output.name);
    }

    return list;
}

int audioDevices::findInput(QString type, QString name) 
{
    int ret = -1;
    int def = -1;
    QString msg;
    QTextStream s(&msg);
    for (int f = 0; f < inputs.size(); f++)
    {
        //qInfo(logAudio()) << "Found device" << inputs[f].name;
        if (inputs[f].name.startsWith(name)) {
            s << type << " Audio input device " << name << " found! ";
            ret = f;
        }
        if (inputs[f].isDefault == true)
        {
            def = f;
        }
    }
 
    if (ret == -1)
    {
        s << type << " Audio input device " << name << " Not found: ";

        if (inputs.size() == 1) {
            s << "Selecting first device " << inputs[0].name;
            ret = 0;
        }
        else if (def > -1)
        {
            s << " Selecting default device " << inputs[def].name;
            ret = def;
        }
        else {
        s << " and no default device found, aborting!";
        }
    }

    qInfo(logAudio()) << msg;
    return ret;
}

int audioDevices::findOutput(QString type, QString name) 
{
    int ret = -1;
    int def = -1;
    QString msg;
    QTextStream s(&msg);
    for (int f = 0; f < outputs.size(); f++)
    {
        //qInfo(logAudio()) << "Found device" << outputs[f].name;
        if (outputs[f].name.startsWith(name)) {
            ret = f;
            s << type << " Audio output device " << name << " found! ";
        }
        if (outputs[f].isDefault == true)
        {
            def = f;
        }
    }

    if (ret == -1)
    {
        s << type << " Audio output device " << name << " Not found: ";

        if (outputs.size() == 1) {
            s << " Selecting first device " << outputs[0].name;
            ret = 0;
        }
        else if (def > -1)
        {
            s << " Selecting default device " << outputs[def].name;
            ret = def;
        }
        else {
            s << " and no default device found, aborting!";
        }
    }
    qInfo(logAudio()) << msg;
    return ret;
}