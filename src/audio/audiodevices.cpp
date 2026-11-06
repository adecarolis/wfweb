/* 
	wfweb class to enumerate audio devices and assist with matching saved devices

	Written by Phil Taylor M0VSE Nov 2022.

*/

#include "audiodevices.h"
#include "logcategories.h"

#if defined(Q_OS_LINUX) && (QT_VERSION >= QT_VERSION_CHECK(5,15,0)) && (QT_VERSION < QT_VERSION_CHECK(6,0,0))
#define WFWEB_WAIT_FOR_SERVER_CODEC
#include <QElapsedTimer>
#include <QFile>
#include <QThread>

// The rig's USB CODEC is listed twice: raw ALSA (realm "alsa") and through the
// sound server (PipeWire / PulseAudio, any other realm). The raw hw: device has
// one owner, so taking it locks every other program out of the rig's audio.
// A wfweb started together with the session (user unit, autostart) enumerates
// within ~100 ms of WirePlumber listing its devices: it found the CODEC's
// sink but not yet its source and settled on hw: (2026-11-06). So when a
// sound server is there and the CODEC exists, give the server a moment to
// list it before the choice is made.
static void waitForServerCodec()
{
    const QString runtime = QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR"));
    if (qEnvironmentVariableIsEmpty("PULSE_SERVER") &&
        (runtime.isEmpty() || !QFile::exists(runtime + "/pulse/native")))
        return;     // no sound server to wait for: raw ALSA is all there is

    const int timeoutMs = 10000;
    auto missing = [](QAudio::Mode mode) {
        bool raw = false, served = false;
        for (const auto &dev : QAudioDeviceInfo::availableDevices(mode)) {
            const QString name = dev.deviceName();
            if (!name.contains("CODEC", Qt::CaseInsensitive) || name.endsWith(QStringLiteral(".monitor")))
                continue;
            if (dev.realm() == "alsa") raw = true; else served = true;
        }
        return raw && !served;
    };

    QElapsedTimer timer;
    timer.start();
    bool waited = false;
    while (missing(QAudio::AudioInput) || missing(QAudio::AudioOutput)) {
        if (!waited) {
            qInfo(logAudio()) << "Rig CODEC not listed by the sound server yet, waiting up to"
                              << timeoutMs / 1000 << "s for it";
            waited = true;
        }
        if (timer.elapsed() >= timeoutMs) {
            qWarning(logAudio()) << "Rig CODEC still not listed by the sound server: using the raw ALSA"
                                    " device, which no other program can share";
            return;
        }
        QThread::msleep(200);
    }
    if (waited)
        qInfo(logAudio()) << "Rig CODEC listed by the sound server after" << timer.elapsed() << "ms";
}
#endif

audioDevices::audioDevices(audioType type, QFontMetrics fm, QObject* parent) :
    QObject(parent),
    system(type),
    fm(fm)
{

#if (QT_VERSION >= QT_VERSION_CHECK(6,0,0))
    connect(&mediaDevices, &QMediaDevices::audioInputsChanged, this, &audioDevices::enumerate);
    connect(&mediaDevices, &QMediaDevices::audioOutputsChanged, this, &audioDevices::enumerate);
#endif

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

#ifdef WFWEB_WAIT_FOR_SERVER_CODEC
            waitForServerCodec();
#endif

            qInfo(logAudio()) << "Audio device(s) found (*=default)";

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
            for(const auto &deviceInfo: QAudioDeviceInfo::availableDevices(QAudio::AudioInput))
#else
            const auto audioInputs = mediaDevices.audioInputs();
            for (const QAudioDevice& deviceInfo : audioInputs)
#endif
            {
                bool isDefault = false;
                if (numInputDevices == 0) {

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
                    defaultInputDeviceName = QString(deviceInfo.deviceName());
#else
                    defaultInputDeviceName = QString(deviceInfo.description());
#endif
                }
#if (defined(Q_OS_WIN) && (QT_VERSION < QT_VERSION_CHECK(6,0,0)))
                if (deviceInfo.realm() == audioApi || audioApi == "") {
#endif
                    /* Append Input Device Here */
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
                    if (deviceInfo.deviceName() == defaultInputDeviceName) {
#else
                    if (deviceInfo.description() == defaultInputDeviceName) {
#endif
                        isDefault = true;
                    }

#if ((QT_VERSION >= QT_VERSION_CHECK(5,15,0)) && (QT_VERSION < QT_VERSION_CHECK(6,0,0)))
                    inputs.append(new audioDevice(deviceInfo.deviceName(), deviceInfo, deviceInfo.realm(), isDefault));
                    qInfo(logAudio()) << (deviceInfo.deviceName() == defaultInputDeviceName ? "*" : " ") <<
                        "(" << numInputDevices <<" " << deviceInfo.realm() << ") Input Device : " << 
                        deviceInfo.deviceName();
#elif (QT_VERSION < QT_VERSION_CHECK(5,15,0))
                    inputs.append(new audioDevice(deviceInfo.deviceName(), deviceInfo, "", isDefault));
                    qInfo(logAudio()) << (deviceInfo.deviceName() == defaultInputDeviceName ? "*" : " ") << 
                        "(" << numInputDevices << ") Input Device : " << deviceInfo.deviceName();
#else
                    inputs.append(new audioDevice(deviceInfo.description(), deviceInfo, "", isDefault));
                    qInfo(logAudio()) << (deviceInfo.description() == defaultInputDeviceName ? "*" : " ") << 
                        "(" << numInputDevices << ") Input Device : " << deviceInfo.description();
#endif

#ifndef BUILD_WFSERVER
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
                    if (fm.boundingRect(deviceInfo.deviceName()).width() > numCharsIn)
                        numCharsIn = fm.boundingRect(deviceInfo.deviceName()).width();
#else
                    if (fm.boundingRect(deviceInfo.description()).width() > numCharsIn)
                        numCharsIn = fm.boundingRect(deviceInfo.description()).width();
#endif
#endif

#if (defined(Q_OS_WIN) && (QT_VERSION < QT_VERSION_CHECK(6,0,0)))
                    }
#endif
                numInputDevices++;
            }

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
            for(const auto &deviceInfo: QAudioDeviceInfo::availableDevices(QAudio::AudioOutput))
#else
            const auto audioOutputs = mediaDevices.audioOutputs();
            for (const QAudioDevice& deviceInfo : audioOutputs)
#endif
            {
                bool isDefault = false;
                if (numOutputDevices == 0)
                {
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
                    defaultOutputDeviceName = QString(deviceInfo.deviceName());
#else
                    defaultOutputDeviceName = QString(deviceInfo.description());
#endif
                }

#if (defined(Q_OS_WIN) && (QT_VERSION < QT_VERSION_CHECK(6,0,0)))
                if (deviceInfo.realm() == "wasapi") {
#endif
                    /* Append Output Device Here */
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
                    if (deviceInfo.deviceName() == defaultOutputDeviceName) {
#else
                    if (deviceInfo.description() == defaultOutputDeviceName) {
#endif
                        isDefault = true;
                    }

#if ((QT_VERSION >= QT_VERSION_CHECK(5,15,0)) && (QT_VERSION < QT_VERSION_CHECK(6,0,0)))
                    outputs.append(new audioDevice(deviceInfo.deviceName(), deviceInfo, deviceInfo.realm() , isDefault));
                    qInfo(logAudio()) << (deviceInfo.deviceName() == defaultOutputDeviceName ? "*" : " ") << 
                        "(" << numOutputDevices << " " << deviceInfo.realm() << ") Output Device : " << 
                        deviceInfo.deviceName();
#elif (QT_VERSION < QT_VERSION_CHECK(5,15,0))
                    outputs.append(new audioDevice(deviceInfo.deviceName(), deviceInfo, "", isDefault));
                    qInfo(logAudio()) << (deviceInfo.deviceName() == defaultOutputDeviceName ? "*" : " ") << 
                        "(" << numOutputDevices << ") Output Device : " << deviceInfo.deviceName();
#else
                    outputs.append(new audioDevice(deviceInfo.description(), deviceInfo, "", isDefault));
                    qInfo(logAudio()) << (deviceInfo.description() == defaultOutputDeviceName ? "*" : " ") << 
                        "(" << numOutputDevices << ") Output Device : " << deviceInfo.description();
#endif

#ifndef BUILD_WFSERVER
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
                    if (fm.boundingRect(deviceInfo.deviceName()).width() > numCharsOut)
                        numCharsOut = fm.boundingRect(deviceInfo.deviceName()).width();
#else
                    if (fm.boundingRect(deviceInfo.description()).width() > numCharsOut)
                        numCharsOut = fm.boundingRect(deviceInfo.description()).width();
#endif
#endif

#if (defined(Q_OS_WIN) && (QT_VERSION < QT_VERSION_CHECK(6,0,0)))
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

            qInfo(logAudio()) << "PortAudio version: " << Pa_GetVersionText();

            int numDevices = Pa_GetDeviceCount();
            qInfo(logAudio()) << "Pa_CountDevices returned" << numDevices << "audio device(s) (*=default)";


            const PaDeviceInfo* info;
            for (int i = 0; i < numDevices; i++)
            {
                info = Pa_GetDeviceInfo(i);
                if (info->maxInputChannels > 0) {
                    bool isDefault = false;
                    numInputDevices++;
                    qInfo(logAudio()) << (i == Pa_GetDefaultInputDevice() ? "*" : " ") << "(" << i << ") Input Device : " << QString(info->name);
                    if (i == Pa_GetDefaultInputDevice()) {
                        defaultInputDeviceName = info->name;
                        isDefault = true;
                    }
                    inputs.append(new audioDevice(QString(info->name), i,isDefault));
#ifndef BUILD_WFSERVER
                    if (fm.boundingRect(QString(info->name)).width() > numCharsIn)
                        numCharsIn = fm.boundingRect(QString(info->name)).width();
#endif

                }
                if (info->maxOutputChannels > 0) {
                    bool isDefault = false;
                    numOutputDevices++;
                    qInfo(logAudio()) << (i == Pa_GetDefaultOutputDevice() ? "*" : " ") << "(" << i << ") Output Device  : " << QString(info->name);
                    if (i == Pa_GetDefaultOutputDevice()) {
                        defaultOutputDeviceName = info->name;
                        isDefault = true;
                    }
                    outputs.append(new audioDevice(QString(info->name), i,isDefault));
#ifndef BUILD_WFSERVER
                    if (fm.boundingRect(QString(info->name)).width() > numCharsOut)
                        numCharsOut = fm.boundingRect(QString(info->name)).width();
#endif
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
#elif defined(Q_OS_MACOS)
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

            unsigned int devicecount = audio->getDeviceCount();

#if (RTAUDIO_VERSION_MAJOR > 5)
            std::vector<unsigned int> devices = audio->getDeviceIds();
#endif
            qInfo(logAudio()) << "Found:" << devicecount << " audio device(s) (*=default)";

            for (unsigned int i = 0; i < devicecount; i++) {

#if (RTAUDIO_VERSION_MAJOR > 5)
                info = audio->getDeviceInfo(devices[i]);
#else
                info = audio->getDeviceInfo(i);
#endif
                if (info.inputChannels > 0) {
                    bool isDefault = false;
                    qInfo(logAudio()) << (info.isDefaultInput ? "*" : " ") << "(" << i << ") Input Device  : " << QString::fromStdString(info.name);
                    numInputDevices++;

                    if (info.isDefaultInput) {
                        defaultInputDeviceName = QString::fromStdString(info.name);
                        isDefault = true;
                    }

#if (RTAUDIO_VERSION_MAJOR > 5)
                    inputs.append(new audioDevice(QString::fromStdString(info.name), devices[i], isDefault));
#else
                    inputs.append(new audioDevice(QString::fromStdString(info.name), i, isDefault));
#endif

#ifndef BUILD_WFSERVER
                    if (fm.boundingRect(QString::fromStdString(info.name)).width() > numCharsIn)
                        numCharsIn = fm.boundingRect(QString::fromStdString(info.name)).width();
#endif

                }
                if (info.outputChannels > 0) {
                    bool isDefault = false;
                    qInfo(logAudio()) << (info.isDefaultOutput ? "*" : " ") << "(" << i << ") Output Device : " << QString::fromStdString(info.name);
                    numOutputDevices++;

                    if (info.isDefaultOutput) {
                        defaultOutputDeviceName = QString::fromStdString(info.name);
                        isDefault = true;
                    }

#if (RTAUDIO_VERSION_MAJOR > 5)
                    outputs.append(new audioDevice(QString::fromStdString(info.name), devices[i], isDefault));
#else
                    outputs.append(new audioDevice(QString::fromStdString(info.name), i, isDefault));
#endif

#ifndef BUILD_WFSERVER
                    if (fm.boundingRect(QString::fromStdString(info.name)).width() > numCharsOut)
                        numCharsOut = fm.boundingRect(QString::fromStdString(info.name)).width();
#endif
                }
            }

            delete audio;
            break;
        }
        case tciAudio:
        {
            inputs.append(new audioDevice("<TCI Audio>",0,1));
            outputs.append(new audioDevice("<TCI Audio>",0,1));
            break;
        }

    }
    emit updated();

}

audioDevices::~audioDevices()
{
    outputs.clear();
    inputs.clear();
}

QStringList audioDevices::getInputs()
{
    QStringList list;

    for (int f = 0; f < inputs.size(); f++) {
        list.append(inputs[f]->name);
    }

    return list;
}

QStringList audioDevices::getOutputs()
{
    QStringList list;   

    for (int f = 0; f < outputs.size(); f++) {
        list.append(outputs[f]->name);
    }

    return list;
}

// Pick the device whose name is the system default, otherwise the first.
// Used to disambiguate when multiple audio devices match a user-supplied name
// (e.g. several USB CODEC dongles all matching "USB Audio CODEC").
static int preferDefault(const QList<audioDevice*>& devices, const QList<int>& candidates)
{
    if (candidates.isEmpty()) return -1;
    // Marked-default wins outright.
    for (int idx : candidates) {
        if (devices[idx]->isDefault) return idx;
    }
    // On Linux, the same physical USB CODEC is enumerated twice: once as
    // a raw ALSA hw: device (realm "alsa") and once via the pulseaudio
    // plugin (realm "default", names like alsa_input.usb-Burr-Brown_...).
    // The hw: entry comes first in Qt's enumeration, so without this
    // tiebreak we'd land on it — which forces nearestFormat fallbacks
    // (48000/mono → 44100/stereo on IC-7300 CODECs) and trips Qt5's
    // flaky IdleState reporting on push-mode QAudioOutput. Prefer any
    // non-"alsa" realm so the pulseaudio entry wins; Windows ("wasapi")
    // and macOS ("coreaudio") have no "alsa" entries, so this is a
    // no-op there.
    for (int idx : candidates) {
        if (devices[idx]->realm != "alsa") return idx;
    }
    return candidates.first();
}

int audioDevices::findInput(QString type, QString name, bool ignoreDefault)
{
    if (type != "Server" && system == tciAudio)
    {
        return 0;
    }

    int ret = -1;
    int def = -1;
    QList<int> prefixMatches;
    QList<int> substringMatches;
    QList<int> codecMatches;
    QList<int> usbMatches;
    QString msg;
    QTextStream s(&msg);

    const bool haveName = !name.isEmpty() && name != "default";

    for (int f = 0; f < inputs.size(); f++)
    {
        if (haveName) {
            if (inputs[f]->name.startsWith(name)) {
                prefixMatches.append(f);
            } else if (inputs[f]->name.contains(name, Qt::CaseInsensitive)) {
                substringMatches.append(f);
            }
        }
        if (!ignoreDefault) {
            if (inputs[f]->isDefault) {
                def = f;
            }
            // PulseAudio pairs every output sink with a .monitor virtual
            // source that captures what's being *played* to that sink, not
            // what's coming in from the rig CODEC. A .monitor named after
            // the rig (alsa_output.usb-Burr-Brown_..._USB_Audio_CODEC-...
            // .analog-stereo.monitor) would otherwise satisfy the CODEC
            // match below and silently shadow the real rig input — the
            // browser would record TX-loopback silence instead of RX audio.
            if (inputs[f]->name.endsWith(QStringLiteral(".monitor"))) {
                continue;
            }
            // "CODEC" is the strong signal for a USB rig sound chip (Burr-Brown
            // USB Audio CODEC etc.); "USB" alone also matches ALSA's bogus
            // usbstream:CARD=PCH virtual node, which can't be opened. Prefer
            // CODEC matches and only fall back to USB-only matches if no CODEC
            // device is present.
            if (inputs[f]->name.contains("CODEC", Qt::CaseInsensitive)) {
                codecMatches.append(f);
            } else if (inputs[f]->name.contains("USB", Qt::CaseInsensitive)) {
                usbMatches.append(f);
            }
        }
    }

    const QList<int>& effectiveCodec = !codecMatches.isEmpty() ? codecMatches : usbMatches;

    if (!prefixMatches.isEmpty()) {
        ret = preferDefault(inputs, prefixMatches);
        s << type << " Audio input device " << name << " found! ";
        if (prefixMatches.size() > 1) {
            s << "(" << prefixMatches.size() << " matches; using "
              << (inputs[ret]->isDefault ? "system default " : "first match ")
              << inputs[ret]->name << ") ";
        }
    } else if (!substringMatches.isEmpty() && !ignoreDefault) {
        ret = preferDefault(inputs, substringMatches);
        s << type << " Audio input device " << name << " matched by substring: "
          << (substringMatches.size() > 1 && inputs[ret]->isDefault ? "system default " : "")
          << inputs[ret]->name;
    }

    if (ret == -1 && !ignoreDefault)
    {
        const int codec = preferDefault(inputs, effectiveCodec);
        if (name == "default") {
            if (codec > -1 && type != "Client") {
                s << type << " Audio input: using USB/CODEC device " << inputs[codec]->name;
                ret = codec;
            } else if (def > -1) {
                s << type << " Audio input: using system default device " << inputs[def]->name;
                ret = def;
            }
        }
        else {
            s << type << " Audio input device " << name << " Not found: ";

            if (inputs.size() == 1) {
                s << "Selecting first device " << inputs[0]->name;
                ret = 0;
            }
            else if (codec > -1 && type != "Client")
            {
                s << " Selecting USB/CODEC device " << inputs[codec]->name;
                ret = codec;
            }
            else if (def > -1)
            {
                s << " Selecting default device " << inputs[def]->name;
                ret = def;
            }
            else {
                s << " and no default device found, aborting!";
            }
        }
    } else if (ret == -1 && ignoreDefault)
    {
        s << "configured audio input device not found:" << name;
    }

    qInfo(logAudio()) << msg;
    return ret;
}

int audioDevices::findOutput(QString type, QString name, bool ignoreDefault)
{
    if (type != "Server" && system == tciAudio)
    {
        return 0;
    }

    int ret = -1;
    int def = -1;
    QList<int> prefixMatches;
    QList<int> substringMatches;
    QList<int> codecMatches;
    QList<int> usbMatches;
    QString msg;
    QTextStream s(&msg);

    const bool haveName = !name.isEmpty() && name != "default";

    for (int f = 0; f < outputs.size(); f++)
    {
        if (haveName) {
            if (outputs[f]->name.startsWith(name)) {
                prefixMatches.append(f);
            } else if (outputs[f]->name.contains(name, Qt::CaseInsensitive)) {
                substringMatches.append(f);
            }
        }
        if (!ignoreDefault) {
            if (outputs[f]->isDefault) {
                def = f;
            }
            // See findInput(): prefer CODEC over plain USB so we don't pick
            // ALSA's unopenable usbstream:CARD=PCH on hosts with both an
            // onboard PCH chipset and a real USB rig sound chip.
            if (outputs[f]->name.contains("CODEC", Qt::CaseInsensitive)) {
                codecMatches.append(f);
            } else if (outputs[f]->name.contains("USB", Qt::CaseInsensitive)) {
                usbMatches.append(f);
            }
        }
    }

    const QList<int>& effectiveCodec = !codecMatches.isEmpty() ? codecMatches : usbMatches;

    if (!prefixMatches.isEmpty()) {
        ret = preferDefault(outputs, prefixMatches);
        s << type << " Audio output device " << name << " found! ";
        if (prefixMatches.size() > 1) {
            s << "(" << prefixMatches.size() << " matches; using "
              << (outputs[ret]->isDefault ? "system default " : "first match ")
              << outputs[ret]->name << ") ";
        }
    } else if (!substringMatches.isEmpty() && !ignoreDefault) {
        ret = preferDefault(outputs, substringMatches);
        s << type << " Audio output device " << name << " matched by substring: "
          << (substringMatches.size() > 1 && outputs[ret]->isDefault ? "system default " : "")
          << outputs[ret]->name;
    }

    if (ret == -1 && !ignoreDefault)
    {
        const int codec = preferDefault(outputs, effectiveCodec);
        if (name == "default") {
            if (codec > -1 && type != "Client") {
                s << type << " Audio output: using USB/CODEC device " << outputs[codec]->name;
                ret = codec;
            } else if (def > -1) {
                s << type << " Audio output: using system default device " << outputs[def]->name;
                ret = def;
            }
        }
        else {
            s << type << " Audio output device " << name << " Not found: ";

            if (outputs.size() == 1) {
                s << " Selecting first device " << outputs[0]->name;
                ret = 0;
            }
            else if (codec > -1 && type != "Client")
            {
                s << " Selecting USB/CODEC device " << outputs[codec]->name;
                ret = codec;
            }
            else if (def > -1)
            {
                s << " Selecting default device " << outputs[def]->name;
                ret = def;
            }
            else {
                s << " and no default device found, aborting!";
            }
        }
    } else if (ret == -1 && ignoreDefault)
    {
        s << "configured audio output device not found:" << name;
    }

    qInfo(logAudio()) << msg;
    return ret;
}
