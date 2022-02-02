#include "servermain.h"

#include "commhandler.h"
#include "rigidentities.h"
#include "logcategories.h"
#include <iostream>

// This code is copyright 2017-2020 Elliott H. Liggett
// All rights reserved

servermain::servermain(const QString serialPortCL, const QString hostCL, const QString settingsFile)
{
    this->serialPortCL = serialPortCL;
    this->hostCL = hostCL;

    qRegisterMetaType <udpPreferences>(); // Needs to be registered early.
    qRegisterMetaType <rigCapabilities>();
    qRegisterMetaType <duplexMode>();
    qRegisterMetaType <rptAccessTxRx>();
    qRegisterMetaType <rigInput>();
    qRegisterMetaType <meterKind>();
    qRegisterMetaType <spectrumMode>();
    qRegisterMetaType <freqt>();
    qRegisterMetaType <mode_info>();
    qRegisterMetaType <audioPacket>();
    qRegisterMetaType <audioSetup>();
    qRegisterMetaType <SERVERCONFIG>();
    qRegisterMetaType <timekind>();
    qRegisterMetaType <datekind>();
    qRegisterMetaType<rigstate*>();
    qRegisterMetaType<QList<radio_cap_packet>>();
    qRegisterMetaType<networkStatus>();

    setDefPrefs();

    getSettingsFilePath(settingsFile);

    loadSettings(); // Look for saved preferences

    setInitialTiming();

    openRig();

    setServerToPrefs();

    amTransmitting = false;

}

servermain::~servermain()
{
    for (RIGCONFIG* radio : serverConfig.rigs)
    {
        if (radio->rigThread != Q_NULLPTR)
        {
            radio->rigThread->quit();
            radio->rigThread->wait();
        }
        delete radio; // This has been created by new in loadSettings();
    }
    serverConfig.rigs.clear();
    if (serverThread != Q_NULLPTR) {
        serverThread->quit();
        serverThread->wait();
    }

    delete settings;

#if defined(PORTAUDIO)
    Pa_Terminate();
#endif

}

void servermain::openRig()
{
    // This function is intended to handle opening a connection to the rig.
    // the connection can be either serial or network,
    // and this function is also responsible for initiating the search for a rig model and capabilities.
    // Any errors, such as unable to open connection or unable to open port, are to be reported to the user.


    makeRig();

    for (RIGCONFIG* radio : serverConfig.rigs)
    {
        //qInfo(logSystem()) << "Opening rig";
        if (radio->rigThread != Q_NULLPTR)
        {
            //qInfo(logSystem()) << "Got rig";
            QMetaObject::invokeMethod(radio->rig, [=]() {
                radio->rig->commSetup(radio->civAddr, radio->serialPort, radio->baudRate, QString("none"));
            }, Qt::QueuedConnection);
        }
    }
}

void servermain::makeRig()
{
    for (RIGCONFIG* radio : serverConfig.rigs)
    {
        if (radio->rigThread == Q_NULLPTR)
        {
            qInfo(logSystem()) << "Creating new rigThread()";
            radio->rig = new rigCommander(radio->guid);
            radio->rigThread = new QThread(this);

            // Thread:
            radio->rig->moveToThread(radio->rigThread);
            connect(radio->rigThread, SIGNAL(started()), radio->rig, SLOT(process()));
            connect(radio->rigThread, SIGNAL(finished()), radio->rig, SLOT(deleteLater()));
            radio->rigThread->start();
            // Rig status and Errors:
            connect(radio->rig, SIGNAL(haveSerialPortError(QString, QString)), this, SLOT(receiveSerialPortError(QString, QString)));
            connect(radio->rig, SIGNAL(haveStatusUpdate(networkStatus)), this, SLOT(receiveStatusUpdate(networkStatus)));

            // Rig comm setup:
            //connect(this, SIGNAL(sendCommSetup(unsigned char, udpPreferences, audioSetup, audioSetup, QString)), radio->rig, SLOT(commSetup(unsigned char, udpPreferences, audioSetup, audioSetup, QString)));
            //connect(this, SIGNAL(sendCommSetup(unsigned char, QString, quint32, QString)), radio->rig, SLOT(commSetup(unsigned char, QString, quint32, QString)));
            connect(this, SIGNAL(setRTSforPTT(bool)), radio->rig, SLOT(setRTSforPTT(bool)));

            connect(radio->rig, SIGNAL(haveBaudRate(quint32)), this, SLOT(receiveBaudRate(quint32)));

            connect(this, SIGNAL(sendCloseComm()), radio->rig, SLOT(closeComm()));
            connect(this, SIGNAL(sendChangeLatency(quint16)), radio->rig, SLOT(changeLatency(quint16)));
            //connect(this, SIGNAL(getRigCIV()), radio->rig, SLOT(findRigs()));
            //connect(this, SIGNAL(setRigID(unsigned char)), radio->rig, SLOT(setRigID(unsigned char)));
            connect(radio->rig, SIGNAL(discoveredRigID(rigCapabilities)), this, SLOT(receiveFoundRigID(rigCapabilities)));
            connect(radio->rig, SIGNAL(commReady()), this, SLOT(receiveCommReady()));

            connect(this, SIGNAL(requestRigState()), radio->rig, SLOT(sendState()));
            connect(this, SIGNAL(stateUpdated()), radio->rig, SLOT(stateUpdated()));
            connect(radio->rig, SIGNAL(stateInfo(rigstate*)), this, SLOT(receiveStateInfo(rigstate*)));

            //Other connections
            connect(this, SIGNAL(setCIVAddr(unsigned char)), radio->rig, SLOT(setCIVAddr(unsigned char)));

            connect(radio->rig, SIGNAL(havePTTStatus(bool)), this, SLOT(receivePTTstatus(bool)));
            connect(this, SIGNAL(setPTT(bool)), radio->rig, SLOT(setPTT(bool)));
            connect(this, SIGNAL(getPTT()), radio->rig, SLOT(getPTT()));
            connect(this, SIGNAL(getDebug()), radio->rig, SLOT(getDebug()));
            if (radio->rigThread->isRunning()) {
                qInfo(logSystem()) << "Rig thread is running";
            }
            else {
                qInfo(logSystem()) << "Rig thread is not running";
            }

        }

    }

}

void servermain::removeRig()
{
    for (RIGCONFIG* radio : serverConfig.rigs)
    {
        if (radio->rigThread != Q_NULLPTR)
        {
            radio->rigThread->disconnect();
            radio->rig->disconnect();
            delete radio->rigThread;
            delete radio->rig;
            radio->rig = Q_NULLPTR;
        }
    }
}


void servermain::findSerialPort()
{
    // Find the ICOM radio connected, or, if none, fall back to OS default.
    // qInfo(logSystem()) << "Searching for serial port...";
    QDirIterator it73("/dev/serial/by-id", QStringList() << "*IC-7300*", QDir::Files, QDirIterator::Subdirectories);
    QDirIterator it97("/dev/serial", QStringList() << "*IC-9700*A*", QDir::Files, QDirIterator::Subdirectories);
    QDirIterator it785x("/dev/serial", QStringList() << "*IC-785*A*", QDir::Files, QDirIterator::Subdirectories);
    QDirIterator it705("/dev/serial", QStringList() << "*IC-705*A", QDir::Files, QDirIterator::Subdirectories);
    QDirIterator it7610("/dev/serial", QStringList() << "*IC-7610*A", QDir::Files, QDirIterator::Subdirectories);
    QDirIterator itR8600("/dev/serial", QStringList() << "*IC-R8600*A", QDir::Files, QDirIterator::Subdirectories);

    if(!it73.filePath().isEmpty())
    {
        // IC-7300
        serialPortRig = it73.filePath(); // first
    } else if(!it97.filePath().isEmpty())
    {
        // IC-9700
        serialPortRig = it97.filePath();
    } else if(!it785x.filePath().isEmpty())
    {
        // IC-785x
        serialPortRig = it785x.filePath();
    } else if(!it705.filePath().isEmpty())
    {
        // IC-705
        serialPortRig = it705.filePath();
    } else if(!it7610.filePath().isEmpty())
    {
        // IC-7610
        serialPortRig = it7610.filePath();
    } else if(!itR8600.filePath().isEmpty())
    {
        // IC-R8600
        serialPortRig = itR8600.filePath();
    } else {
        //fall back:
        qInfo(logSystem()) << "Could not find Icom serial port. Falling back to OS default. Use --port to specify, or modify preferences.";
#ifdef Q_OS_MAC
        serialPortRig = QString("/dev/tty.SLAB_USBtoUART");
#endif
#ifdef Q_OS_LINUX
        serialPortRig = QString("/dev/ttyUSB0");
#endif
#ifdef Q_OS_WIN
        serialPortRig = QString("COM1");
#endif
    }
}

void servermain::receiveStatusUpdate(networkStatus status)
{
    if (status.message != lastMessage) {
        std::cout << status.message.toLocal8Bit().toStdString() << "\n";
        lastMessage = status.message;
    }
}


void servermain::receiveCommReady()
{
    rigCommander* sender = qobject_cast<rigCommander*>(QObject::sender());

    // Use the GUID to determine which radio the response is from

    for (RIGCONFIG* radio : serverConfig.rigs)
    {
        if (sender != Q_NULLPTR && radio->rig != Q_NULLPTR && !memcmp(sender->getGUID(), radio->guid, GUIDLEN))
        {

            qInfo(logSystem()) << "Received CommReady!! ";
            if (radio->civAddr == 0)
            {
                // tell rigCommander to broadcast a request for all rig IDs.
                // qInfo(logSystem()) << "Beginning search from wfview for rigCIV (auto-detection broadcast)";
                if (!radio->rigAvailable) {
                    if (radio->connectTimer == Q_NULLPTR) {
                        radio->connectTimer = new QTimer();
                        connect(radio->connectTimer, &QTimer::timeout, this, std::bind(&servermain::connectToRig, this, radio));
                    }
                    radio->connectTimer->start(500);
                }
            }
            else {
                // don't bother, they told us the CIV they want, stick with it.
                // We still query the rigID to find the model, but at least we know the CIV.
                qInfo(logSystem()) << "Skipping automatic CIV, using user-supplied value of " << radio->civAddr;
                QMetaObject::invokeMethod(radio->rig, [=]() {
                    radio->rig->setRigID(radio->civAddr);
                    }, Qt::QueuedConnection);
            }
        }
    }
}

void servermain::connectToRig(RIGCONFIG* rig)
{
    if (!rig->rigAvailable) {
        //qDebug(logSystem()) << "Searching for rig on" << rig->serialPort;
        QMetaObject::invokeMethod(rig->rig, [=]() {
            rig->rig->findRigs();
        }, Qt::QueuedConnection);
    }
    else {
        rig->connectTimer->stop();
    }
}

void servermain::receiveFoundRigID(rigCapabilities rigCaps)
{
    // Entry point for unknown rig being identified at the start of the program.
    //now we know what the rig ID is:

    rigCommander* sender = qobject_cast<rigCommander*>(QObject::sender());

    // Use the GUID to determine which radio the response is from
    for (RIGCONFIG* radio : serverConfig.rigs)
    {

        if (sender != Q_NULLPTR && radio->rig != Q_NULLPTR && !radio->rigAvailable && !memcmp(sender->getGUID(), radio->guid, GUIDLEN))
        {

            qDebug(logSystem()) << "Rig name: " << rigCaps.modelName;
            qDebug(logSystem()) << "Has LAN capabilities: " << rigCaps.hasLan;
            qDebug(logSystem()) << "Rig ID received into servermain: spectLenMax: " << rigCaps.spectLenMax;
            qDebug(logSystem()) << "Rig ID received into servermain: spectAmpMax: " << rigCaps.spectAmpMax;
            qDebug(logSystem()) << "Rig ID received into servermain: spectSeqMax: " << rigCaps.spectSeqMax;
            qDebug(logSystem()) << "Rig ID received into servermain: hasSpectrum: " << rigCaps.hasSpectrum;
            qDebug(logSystem()).noquote() << QString("Rig ID received into servermain: GUID: {%1%2%3%4-%5%6-%7%8-%9%10-%11%12%13%14%15%16}")
                .arg(rigCaps.guid[0], 2, 16, QLatin1Char('0'))
                .arg(rigCaps.guid[1], 2, 16, QLatin1Char('0'))
                .arg(rigCaps.guid[2], 2, 16, QLatin1Char('0'))
                .arg(rigCaps.guid[3], 2, 16, QLatin1Char('0'))
                .arg(rigCaps.guid[4], 2, 16, QLatin1Char('0'))
                .arg(rigCaps.guid[5], 2, 16, QLatin1Char('0'))
                .arg(rigCaps.guid[6], 2, 16, QLatin1Char('0'))
                .arg(rigCaps.guid[7], 2, 16, QLatin1Char('0'))
                .arg(rigCaps.guid[8], 2, 16, QLatin1Char('0'))
                .arg(rigCaps.guid[9], 2, 16, QLatin1Char('0'))
                .arg(rigCaps.guid[10], 2, 16, QLatin1Char('0'))
                .arg(rigCaps.guid[11], 2, 16, QLatin1Char('0'))
                .arg(rigCaps.guid[12], 2, 16, QLatin1Char('0'))
                .arg(rigCaps.guid[13], 2, 16, QLatin1Char('0'))
                .arg(rigCaps.guid[14], 2, 16, QLatin1Char('0'))
                .arg(rigCaps.guid[15], 2, 16, QLatin1Char('0'))
                ;
            radio->rigCaps = rigCaps;
            // Added so that server receives rig capabilities.
            emit sendRigCaps(rigCaps);
        }
    }
    return;
}

void servermain::receiveSerialPortError(QString port, QString errorText)
{
    qInfo(logSystem()) << "servermain: received serial port error for port: " << port << " with message: " << errorText;

    // TODO: Dialog box, exit, etc
}


void servermain::getSettingsFilePath(QString settingsFile)
{
    if (settingsFile.isNull()) {
        settings = new QSettings();
    }
    else
    {
        QString file = settingsFile;
        QFile info(settingsFile);
        QString path="";
        if (!QFileInfo(info).isAbsolute())
        {
            path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            if (path.isEmpty())
            {
                path = QDir::homePath();
            }
            path = path + "/";
            file = info.fileName();
        }

        qInfo(logSystem()) << "Loading settings from:" << path + file;
        settings = new QSettings(path + file, QSettings::Format::IniFormat);
    }
}


void servermain::setInitialTiming()
{
    loopTickCounter = 0;
    pttTimer = new QTimer(this);
    pttTimer->setInterval(180*1000); // 3 minute max transmit time in ms
    pttTimer->setSingleShot(true);
    connect(pttTimer, SIGNAL(timeout()), this, SLOT(handlePttLimit()));
}

void servermain::setServerToPrefs()
{
	
    // Start server if enabled in config
    if (serverThread != Q_NULLPTR) {
        serverThread->quit();
        serverThread->wait();
        serverThread = Q_NULLPTR;
        udp = Q_NULLPTR;
    }

    udp = new udpServer(serverConfig, serverTxSetup, serverRxSetup);

    serverThread = new QThread(this);

    udp->moveToThread(serverThread);


    connect(this, SIGNAL(initServer()), udp, SLOT(init()));
    connect(serverThread, SIGNAL(finished()), udp, SLOT(deleteLater()));

    // Step through all radios and connect them to the server, 
    // server will then use GUID to determine which actual radio it belongs to.

    for (RIGCONFIG* radio : serverConfig.rigs)
    {
        if (radio->rigThread != Q_NULLPTR)
        {

            if (radio->rig != Q_NULLPTR) {
                connect(radio->rig, SIGNAL(haveAudioData(audioPacket)), udp, SLOT(receiveAudioData(audioPacket)));
                connect(radio->rig, SIGNAL(haveDataForServer(QByteArray)), udp, SLOT(dataForServer(QByteArray)));
                //connect(udp, SIGNAL(haveDataFromServer(QByteArray)), radio->rig, SLOT(dataFromServer(QByteArray)));
                connect(this, SIGNAL(sendRigCaps(rigCapabilities)), udp, SLOT(receiveRigCaps(rigCapabilities)));
            }
        }
    }

    connect(udp, SIGNAL(haveNetworkStatus(networkStatus)), this, SLOT(receiveStatusUpdate(networkStatus)));

    serverThread->start();

    emit initServer();
}


void servermain::setDefPrefs()
{
    defPrefs.radioCIVAddr = 0x00; // previously was 0x94 for 7300.
    defPrefs.CIVisRadioModel = false;
    defPrefs.forceRTSasPTT = false;
    defPrefs.serialPortRadio = QString("auto");
    defPrefs.serialPortBaud = 115200;
    defPrefs.localAFgain = 255;

    udpDefPrefs.ipAddress = QString("");
    udpDefPrefs.controlLANPort = 50001;
    udpDefPrefs.serialLANPort = 50002;
    udpDefPrefs.audioLANPort = 50003;
    udpDefPrefs.username = QString("");
    udpDefPrefs.password = QString("");
    udpDefPrefs.clientName = QHostInfo::localHostName();
}

void servermain::loadSettings()
{
    qInfo(logSystem()) << "Loading settings from " << settings->fileName();

    int numRadios=settings->beginReadArray("Radios");
    int tempNum = numRadios;
    if (numRadios == 0) {
        settings->endArray();
        numRadios = 1;
    }

#if defined(RTAUDIO)

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

#elif defined(PORTAUDIO)
    // Use PortAudio device enumeration

    PaError err;

    err = Pa_Initialize();

    if (err != paNoError)
    {
        qInfo(logAudio()) << "ERROR: Cannot initialize Portaudio";
    }

    qInfo(logAudio()) << "PortAudio version: " << Pa_GetVersionInfo()->versionText;

    int numDevices;
    numDevices = Pa_GetDeviceCount();
    qInfo(logAudio()) << "Pa_CountDevices returned" << numDevices;

    const   PaDeviceInfo* info;

#else

    const auto audioOutputs = QAudioDeviceInfo::availableDevices(QAudio::AudioOutput);
    const auto audioInputs = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);

#endif
    for (int i = 0; i < numRadios; i++) {
        if (tempNum == 0) {
            settings->beginGroup("Radio");
        }
        else {
            settings->setArrayIndex(i);
        }
        RIGCONFIG* tempPrefs = new RIGCONFIG();
        tempPrefs->civAddr = (unsigned char)settings->value("RigCIVuInt", defPrefs.radioCIVAddr).toInt();
        tempPrefs->forceRTSasPTT = (bool)settings->value("ForceRTSasPTT", defPrefs.forceRTSasPTT).toBool();
        tempPrefs->serialPort = settings->value("SerialPortRadio", defPrefs.serialPortRadio).toString();
        tempPrefs->rigName = settings->value("RigName", "<NONE>").toString();
        tempPrefs->baudRate = (quint32)settings->value("SerialPortBaud", defPrefs.serialPortBaud).toInt();

        if (tempPrefs->rigName=="<NONE>")
        {
            foreach(const QSerialPortInfo & serialPortInfo, QSerialPortInfo::availablePorts())
            {
                qDebug(logSystem()) << "Serial Port found: " << serialPortInfo.portName() << "Manufacturer:" << serialPortInfo.manufacturer() << "Product ID" << serialPortInfo.description() << "S/N" << serialPortInfo.serialNumber();
                if (serialPortInfo.portName() == tempPrefs->serialPort && !serialPortInfo.serialNumber().isEmpty())
                {
                    tempPrefs->rigName = serialPortInfo.serialNumber();
                }
            }
        }
        QString guid = settings->value("GUID", "").toString();
        if (guid.isEmpty()) {
            guid = QUuid::createUuid().toString();
            settings->setValue("GUID", guid);
        }
#if (QT_VERSION >= QT_VERSION_CHECK(5,10,0))
        memcpy(tempPrefs->guid, QUuid::fromString(guid).toRfc4122().constData(), GUIDLEN);
#endif
        tempPrefs->rxAudioSetup.isinput = true;
        tempPrefs->txAudioSetup.isinput = false;
        tempPrefs->rxAudioSetup.localAFgain = 255;
        tempPrefs->txAudioSetup.localAFgain = 255;
        tempPrefs->rxAudioSetup.resampleQuality = 4;
        tempPrefs->txAudioSetup.resampleQuality = 4;

        tempPrefs->rxAudioSetup.name = settings->value("AudioInput", "").toString();
        tempPrefs->txAudioSetup.name = settings->value("AudioOutput", "").toString();
        bool rxDeviceFound = false;
        bool txDeviceFound = false;
        // Find the actual audio devices 
#if defined(RTAUDIO)
        for (unsigned int i = 1; i < devices; i++) {
            info = audio->getDeviceInfo(i);
            if (info.outputChannels > 0) {
                if (tempPrefs->txAudioSetup.name == info->name) {
                    tempPrefs->txAudioSetup.port = i;
                    txDeviceFound = true;
                }
            }
            if (info.inputChannels > 0) {
                if (tempPrefs->rxAudioSetup.name == info->name) {
                    tempPrefs->rxAudioSetup.port = i;
                    rxDeviceFound = true;
                }
            }
        }
#elif defined(PORTAUDIO)
        for (int i = 0; i < numDevices; i++)
        {
            info = Pa_GetDeviceInfo(i);
            if (info->maxInputChannels > 0) {
                if (tempPrefs->txAudioSetup.name == info->name) {
                    tempPrefs->txAudioSetup.port = i;
                    txDeviceFound = true;
                }
            }
            if (info->maxOutputChannels > 0) {
                if (tempPrefs->rxAudioSetup.name == info->name) {
                    tempPrefs->rxAudioSetup.port = i;
                    rxDeviceFound = true;
    }
            }
        }
#else

        /* If no external library is configured, use QTMultimedia
        // Set these to default audio devices initially.
        */

        //qInfo(logAudio()) << "Looking for audio output devices";
        for (const QAudioDeviceInfo& deviceInfo : audioOutputs) {
            qDebug(logSystem()) << "Found Audio output: " << deviceInfo.deviceName();
            if (deviceInfo.deviceName() == tempPrefs->txAudioSetup.name
#ifdef Q_OS_WIN
                && deviceInfo.realm() == "wasapi"
#endif
          ) {
                qDebug(logSystem()) << "Audio output: " << deviceInfo.deviceName();
                tempPrefs->txAudioSetup.port = deviceInfo;
                txDeviceFound = true;
            }
        }

        //qInfo(logAudio()) << "Looking for audio input devices";
        for (const QAudioDeviceInfo& deviceInfo : audioInputs) {
            qDebug(logSystem()) << "Found Audio input: " << deviceInfo.deviceName();
            if (deviceInfo.deviceName() == tempPrefs->rxAudioSetup.name
#ifdef Q_OS_WIN
                && deviceInfo.realm() == "wasapi"
#endif
          ) {
                qDebug(logSystem()) << "Audio input: " << deviceInfo.deviceName();
                tempPrefs->rxAudioSetup.port = deviceInfo;
                rxDeviceFound = true;
            }
        }
#endif

        if (!txDeviceFound) {
            qInfo() << "Cannot find txAudioDevice" << tempPrefs->txAudioSetup.name;
        }
        if (!rxDeviceFound) {
            qInfo() << "Cannot find rxAudioDevice" << tempPrefs->rxAudioSetup.name;
        }
        tempPrefs->rig = Q_NULLPTR;
        tempPrefs->rigThread = Q_NULLPTR;
        serverConfig.rigs.append(tempPrefs);
        if (tempNum == 0) {
            settings->endGroup();
        }
    }
    if (tempNum > 0) {
        settings->endArray();
    }


    settings->beginGroup("Server");
    serverConfig.enabled = settings->value("ServerEnabled", false).toBool();
    serverConfig.controlPort = settings->value("ServerControlPort", 50001).toInt();
    serverConfig.civPort = settings->value("ServerCivPort", 50002).toInt();
    serverConfig.audioPort = settings->value("ServerAudioPort", 50003).toInt();

    serverConfig.users.clear();

    int numUsers = settings->beginReadArray("Users");
    if (numUsers > 0) {
        {
            for (int f = 0; f < numUsers; f++)
            {
                settings->setArrayIndex(f);
                SERVERUSER user;
                user.username = settings->value("Username", "").toString();
                user.password = settings->value("Password", "").toString();
                user.userType = settings->value("UserType", 0).toInt();
                serverConfig.users.append(user);

            }
        }
        settings->endArray();
    }
    else {
        /* Support old way of storing users just to get them loaded*/
        settings->endArray();
        numUsers = settings->value("ServerNumUsers", 2).toInt();
        for (int f = 0; f < numUsers; f++)
        {
            SERVERUSER user;
            user.username = settings->value("ServerUsername_" + QString::number(f), "").toString();
            user.password = settings->value("ServerPassword_" + QString::number(f), "").toString();
            user.userType = settings->value("ServerUserType_" + QString::number(f), 0).toInt();
            serverConfig.users.append(user);
        }
    }

    settings->endGroup();
    settings->sync();

#if defined(RTAUDIO)
    delete audio;
#endif

}


void servermain::receivePTTstatus(bool pttOn)
{
    // This is the only place where amTransmitting and the transmit button text should be changed:
    //qInfo(logSystem()) << "PTT status: " << pttOn;
    amTransmitting = pttOn;
}

void servermain::handlePttLimit()
{
    // ptt time exceeded!
    std::cout << "Transmit timeout at 3 minutes. Sending PTT OFF command now.\n";
    emit setPTT(false);
    emit getPTT();
}

void servermain::receiveBaudRate(quint32 baud)
{
    qInfo() << "Received serial port baud rate from remote server:" << baud;
    prefs.serialPortBaud = baud;
}

void servermain::powerRigOn()
{
    emit sendPowerOn();
}

void servermain::powerRigOff()
{
    emit sendPowerOff();
}

void servermain::receiveStateInfo(rigstate* state)
{
    qInfo("Received rig state for wfmain");
    Q_UNUSED(state);
    //rigState = state;
}
