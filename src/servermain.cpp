#include "servermain.h"

#include "commhandler.h"
#include "rigidentities.h"
#include "logcategories.h"
#include <iostream>

// This code is copyright 2017-2020 Elliott H. Liggett
// All rights reserved

servermain::servermain(const QString settingsFile, const cmdLineOverrides& overrides)
    : cliOverrides(overrides)
{

    qRegisterMetaType <udpPreferences>(); // Needs to be registered early.
    qRegisterMetaType <rigCapabilities>();
    qRegisterMetaType <rigInput>();
    qRegisterMetaType <freqt>();
    qRegisterMetaType <audioPacket>();
    qRegisterMetaType <audioSetup>();
    qRegisterMetaType <SERVERCONFIG>();
    qRegisterMetaType <timekind>();
    qRegisterMetaType <datekind>();
    qRegisterMetaType<rigstate*>();
    qRegisterMetaType<QList<radio_cap_packet>>();
    qRegisterMetaType<networkStatus>();
    qRegisterMetaType<codecType>();
    qRegisterMetaType<errorType>();
    qRegisterMetaType<cacheItem>();
    qRegisterMetaType<queueItem>();
    qRegisterMetaType<funcs>();
    qRegisterMetaType<modeInfo>();
    qRegisterMetaType<vfo_t>();
    qRegisterMetaType<scopeData>();
    qRegisterMetaType<centerSpanData>();
    qRegisterMetaType<meter_t>();
    qRegisterMetaType<spectrumBounds>();
    qRegisterMetaType<rigTypedef>();

    this->setObjectName("wfweb");
    queue = cachingQueue::getInstance(this);
    queue->interval(100); // 100ms polling interval for serial connections
    // We need to open rig files

    // Make sure we know about any changes to rigCaps
    connect(queue, SIGNAL(rigCapsUpdated(rigCapabilities*)), this, SLOT(receiveRigCaps(rigCapabilities*)));

    // Watch for power state changes from the radio
    connect(queue, &cachingQueue::cacheUpdated, this, [this](cacheItem item) {
        if (item.command == funcPowerControl) {
            bool poweredOn = item.value.toBool();
            if (poweredOn && !rigPoweredOn) {
                qInfo(logSystem()) << "Rig powered ON detected, restoring periodic polling";
                rigPoweredOn = true;
                initPeriodicPolling();
            } else if (!poweredOn && rigPoweredOn) {
                qInfo(logSystem()) << "Rig powered OFF detected, stopping periodic polling";
                rigPoweredOn = false;
                queue->clear();
                startPowerProbe();
            }
        }
    });

    setDefPrefs();

    getSettingsFilePath(settingsFile);

    loadSettings(); // Look for saved preferences
    applyCLIOverrides();

    setManufacturer(prefs.manufacturer);

    audioDev = new audioDevices(prefs.audioSystem, QFontMetrics(QFont()));
    connect(audioDev, SIGNAL(updated()), this, SLOT(updateAudioDevices()));
    audioDev->enumerate();

    setInitialTiming();

    // Initialize web server before openRig so audio connections can be made
    // --no-web disables the web server entirely (enables rig server instead)
    quint16 effectiveWebPort = (cliOverrides.webPort > 0) ? cliOverrides.webPort : prefs.webPort;
    if (effectiveWebPort > 0 && !cliOverrides.noWeb) {
        web = new webServer();
        webThread = new QThread(this);
        webThread->setObjectName("WebServer()");
        web->moveToThread(webThread);
        connect(queue, SIGNAL(rigCapsUpdated(rigCapabilities*)), web, SLOT(receiveRigCaps(rigCapabilities*)));
        connect(webThread, SIGNAL(finished()), web, SLOT(deleteLater()));
        connect(web, &webServer::requestPowerOn, this, &servermain::powerRigOn);
        connect(web, &webServer::requestPowerOff, this, &servermain::powerRigOff);
        webThread->start();
        QMetaObject::invokeMethod(web, "init", Qt::QueuedConnection,
            Q_ARG(quint16, effectiveWebPort), Q_ARG(quint16, effectiveWebPort + 1));
        qInfo(logSystem()) << "Web server starting on port" << effectiveWebPort;
    }

    openRig();

    setServerToPrefs();

    amTransmitting = false;

}

servermain::~servermain()
{
    // Shut down webThread first: it has cross-thread signal connections to rig
    // objects (haveAudioData, haveTxAudioData) and timers (FreeDV TX drain).
    // Destroying rig objects while webThread is still running causes a segfault.
    if (webThread != Q_NULLPTR) {
        webThread->quit();
        webThread->wait();
    }
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

    if (audioDev != Q_NULLPTR) {
        delete audioDev;
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
        if (radio->rigThread != Q_NULLPTR)
        {
            if (prefs.enableLAN) {
                usingLAN = true;
                udpPrefs.waterfallFormat = radio->waterfallFormat;
                audioSetup lanRxSetup = rxSetup;
                audioSetup lanTxSetup = txSetup;
                memcpy(lanRxSetup.guid, radio->guid, GUIDLEN);
                memcpy(lanTxSetup.guid, radio->guid, GUIDLEN);
                QMetaObject::invokeMethod(radio->rig, [=]() {
                    radio->rig->commSetup(rigList, radio->civAddr, udpPrefs,
                        lanRxSetup, lanTxSetup, QString("none"), 0);
                }, Qt::QueuedConnection);
                qInfo(logSystem()) << "Connecting via LAN to" << udpPrefs.ipAddress;
            } else {
                QMetaObject::invokeMethod(radio->rig, [=]() {
                    radio->rig->commSetup(rigList, radio->civAddr, radio->serialPort,
                        radio->baudRate, QString("none"), 0, radio->waterfallFormat);
                }, Qt::QueuedConnection);
            }
        }
    }

    // Setup web audio
    if (web != Q_NULLPTR) {
        if (prefs.enableLAN) {
            QMetaObject::invokeMethod(web, "setupAudio", Qt::QueuedConnection,
                Q_ARG(quint8, rxSetup.codec), Q_ARG(quint32, rxSetup.sampleRate));
        } else {
            QMetaObject::invokeMethod(web, "setupUsbAudio", Qt::QueuedConnection,
                Q_ARG(quint32, 48000));
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
            // We can't currently mix different manufacturers
            switch (prefs.manufacturer){
            case manufIcom:
                radio->rig = new icomCommander(radio->guid);
                break;
            case manufKenwood:
                radio->rig = new kenwoodCommander(radio->guid);
                break;
            case manufYaesu:
                radio->rig = new yaesuCommander(radio->guid);
                break;
            default:
                qCritical() << "Unknown Manufacturer, aborting...";
                break;
            }

            radio->rigThread = new QThread(this);
            radio->rigThread->setObjectName("rigCommander()");

            // Thread:
            radio->rig->moveToThread(radio->rigThread);
            connect(radio->rigThread, SIGNAL(started()), radio->rig, SLOT(process()));
            connect(radio->rigThread, SIGNAL(finished()), radio->rig, SLOT(deleteLater()));
            radio->rigThread->start();
            // Rig status and Errors:
            connect(radio->rig, SIGNAL(havePortError(errorType)), this, SLOT(receivePortError(errorType)));
            connect(radio->rig, SIGNAL(haveStatusUpdate(networkStatus)), this, SLOT(receiveStatusUpdate(networkStatus)));

            // Rig comm setup:
            connect(this, SIGNAL(setPTTType(pttType_t)), radio->rig, SLOT(setPTTType(pttType_t)));

            connect(radio->rig, SIGNAL(haveBaudRate(quint32)), this, SLOT(receiveBaudRate(quint32)));

            connect(this, SIGNAL(sendCloseComm()), radio->rig, SLOT(closeComm()));
            connect(this, SIGNAL(sendChangeLatency(quint16)), radio->rig, SLOT(changeLatency(quint16)));
            connect(radio->rig, SIGNAL(commReady()), this, SLOT(receiveCommReady()));

            connect(this, SIGNAL(setCIVAddr(unsigned char)), radio->rig, SLOT(setCIVAddr(unsigned char)));
            connect(this, SIGNAL(sendPowerOn()), radio->rig, SLOT(powerOn()));
            connect(this, SIGNAL(sendPowerOff()), radio->rig, SLOT(powerOff()));

            connect(this, SIGNAL(getDebug()), radio->rig, SLOT(getDebug()));

            // Web server audio connections
            if (web != Q_NULLPTR) {
                connect(radio->rig, SIGNAL(haveAudioData(audioPacket)), web, SLOT(receiveRxAudio(audioPacket)));
                connect(web, SIGNAL(haveTxAudioData(audioPacket)), radio->rig, SLOT(receiveWebTxAudio(audioPacket)));
            }

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

void servermain::receiveStatusUpdate(networkStatus status)
{
    if (status.message != lastMessage && !status.message.contains("rx latency")) {
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
                // qInfo(logSystem()) << "Beginning search from wfweb for rigCIV (auto-detection broadcast)";
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
        qDebug(logSystem()) << "Searching for rig on" << rig->serialPort;
        QMetaObject::invokeMethod(rig->rig, [=]() {
            rig->rig->receiveCommand(funcTransceiverId,QVariant(),0);
        }, Qt::QueuedConnection);

    }
    else {
        rig->connectTimer->stop();
    }
}

void servermain::receiveRigCaps(rigCapabilities* rigCaps)
{

    // Entry point for unknown rig being identified at the start of the program.
    //now we know what the rig ID is:

    // Signal may come from rigCommander or cachingQueue
    rigCommander* sender = qobject_cast<rigCommander*>(QObject::sender());

    // Use the GUID to determine which radio the response is from
    for (RIGCONFIG* radio : serverConfig.rigs)
    {
        bool guidMatch = (sender != Q_NULLPTR && radio->rig != Q_NULLPTR && !memcmp(sender->getGUID(), radio->guid, GUIDLEN));
        bool fromQueue = (sender == Q_NULLPTR); // Signal came from cachingQueue

        if ((guidMatch || fromQueue) && radio->rig != Q_NULLPTR && !radio->rigAvailable)
        {
            // Set rigAvailable immediately to prevent re-entry from rigServer race
            radio->rigAvailable = true;

            qDebug(logSystem()) << "Rig name: " << rigCaps->modelName;
            qDebug(logSystem()) << "Has LAN capabilities: " << rigCaps->hasLan;
            qDebug(logSystem()) << "Rig ID received into servermain: spectLenMax: " << rigCaps->spectLenMax;
            qDebug(logSystem()) << "Rig ID received into servermain: spectAmpMax: " << rigCaps->spectAmpMax;
            qDebug(logSystem()) << "Rig ID received into servermain: spectSeqMax: " << rigCaps->spectSeqMax;
            qDebug(logSystem()) << "Rig ID received into servermain: hasSpectrum: " << rigCaps->hasSpectrum;
            qDebug(logSystem()).noquote() << QString("Rig ID received into servermain: GUID: {%1%2%3%4-%5%6-%7%8-%9%10-%11%12%13%14%15%16}")
                                                 .arg(rigCaps->guid[0], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[1], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[2], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[3], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[4], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[5], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[6], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[7], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[8], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[9], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[10], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[11], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[12], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[13], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[14], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[15], 2, 16, QLatin1Char('0'))
                ;
            radio->rigCaps = rigCaps;

            // Enable CI-V Auto Information so the rig pushes meter/freq/mode
            // updates automatically (same as wfweb does in setupInitialValues)
            if (rigCaps->commands.contains(funcAutoInformation)) {
                queue->add(priorityImmediate, queueItem(funcAutoInformation, QVariant::fromValue(uchar(2)), false, 0));
                // Enable metering data in the AutoInformation stream
                if (rigCaps->commands.contains(funcSWRMeter))
                    queue->add(priorityImmediate, queueItem(funcSWRMeter, QVariant::fromValue(uchar(1)), false, 0));
                if (rigCaps->commands.contains(funcALCMeter))
                    queue->add(priorityImmediate, queueItem(funcALCMeter, QVariant::fromValue(uchar(1)), false, 0));
                if (rigCaps->commands.contains(funcCompMeter))
                    queue->add(priorityImmediate, queueItem(funcCompMeter, QVariant::fromValue(uchar(1)), false, 0));
                if (rigCaps->commands.contains(funcIdMeter))
                    queue->add(priorityImmediate, queueItem(funcIdMeter, QVariant::fromValue(uchar(1)), false, 0));
                if (rigCaps->commands.contains(funcVdMeter))
                    queue->add(priorityImmediate, queueItem(funcVdMeter, QVariant::fromValue(uchar(1)), false, 0));
                qInfo(logSystem()) << "Auto Information enabled for meters";
            }

            // Set up queue interval and periodic polling commands
            initPeriodicPolling();
        }
    }
    return;
}

void servermain::receivePortError(errorType err)
{
    qInfo(logSystem()) << "servermain: received error for device: " << err.device << " with message: " << err.message;
}


void servermain::getSettingsFilePath(QString settingsFile)
{
    if (settingsFile.isNull()) {
        settings = new QSettings();
    }
    else
    {
        // Resolve relative paths against the current working directory
        QFileInfo info(settingsFile);
        QString resolved = info.absoluteFilePath();

        qInfo(logSystem()) << "Loading settings from:" << resolved;
        settings = new QSettings(resolved, QSettings::Format::IniFormat);
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
	
    serverConfig.lan = prefs.enableLAN;

    // Start server if enabled in config
    if (serverThread != Q_NULLPTR) {
        serverThread->quit();
        serverThread->wait();
        serverThread = Q_NULLPTR;
        server = Q_NULLPTR;
    }

    if (!cliOverrides.noWeb) {
        qInfo(logSystem()) << "Rig server disabled (use --no-web to enable)";
        return;
    }

    serverThread = new QThread(this);

    switch (prefs.manufacturer)
    {
    case manufIcom:
        server = new icomServer(&serverConfig);
        serverThread->setObjectName("icomServer()");
        break;
    case manufKenwood:
        server = new kenwoodServer(&serverConfig);
        serverThread->setObjectName("kenwoodServer()");
        break;
    case manufYaesu:
        server = new yaesuServer(&serverConfig);
        serverThread->setObjectName("yaesuServer()");
        break;
    default:
        return;

    }


    server->moveToThread(serverThread);


    connect(this, SIGNAL(initServer()), server, SLOT(init()));
    connect(serverThread, SIGNAL(finished()), server, SLOT(deleteLater()));

    // Step through all radios and connect them to the server, 
    // server will then use GUID to determine which actual radio it belongs to.

    for (RIGCONFIG* radio : serverConfig.rigs)
    {
        if (radio->rigThread != Q_NULLPTR)
        {

            if (radio->rig != Q_NULLPTR) {
                connect(radio->rig, SIGNAL(haveAudioData(audioPacket)), server, SLOT(receiveAudioData(audioPacket)));
                connect(radio->rig, SIGNAL(haveDataForServer(QByteArray)), server, SLOT(dataForServer(QByteArray)));
            }
        }
    }

    connect(server, SIGNAL(haveNetworkStatus(networkStatus)), this, SLOT(receiveStatusUpdate(networkStatus)));

    serverThread->start();

    emit initServer();
}


void servermain::setDefPrefs()
{
    defPrefs.manufacturer = manufIcom;
    defPrefs.radioCIVAddr = 0x00; // previously was 0x94 for 7300.
    defPrefs.CIVisRadioModel = false;
    defPrefs.pttType = pttCIV;
    defPrefs.serialPortRadio = QString("auto");
    defPrefs.serialPortBaud = 115200;
    defPrefs.localAFgain = 255;
    defPrefs.tcpPort = 0;
    defPrefs.audioSystem = qtAudio;
    defPrefs.webPort = 8080;
    defPrefs.rxAudio.name = QString("default");
    defPrefs.txAudio.name = QString("default");
    defPrefs.enableLAN = false;

    // LAN audio defaults
    rxSetup.codec = 4;       // LPCM 16-bit
    txSetup.codec = 4;
    rxSetup.sampleRate = 48000;
    txSetup.sampleRate = 48000;
    rxSetup.isinput = false;
    txSetup.isinput = true;
    rxSetup.latency = 150;
    txSetup.latency = 150;

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
    prefs.audioSystem = static_cast<audioType>(settings->value("AudioSystem", defPrefs.audioSystem).toInt());
    prefs.manufacturer = static_cast<manufacturersType_t>(settings->value("Manufacturer",defPrefs.manufacturer).toInt());
    prefs.webPort = settings->value("WebServerPort", defPrefs.webPort).toInt();

    int numRadios = settings->beginReadArray("Radios");
    if (numRadios == 0) {
        settings->endArray();

        // No Radios array found. Try reading flat [Radio] section (wfweb-style config).
        unsigned char flatCiv = defPrefs.radioCIVAddr;
        manufacturersType_t flatManuf = defPrefs.manufacturer;
        QString flatSerial = defPrefs.serialPortRadio;
        quint32 flatBaud = defPrefs.serialPortBaud;

        settings->beginGroup("Radio");
        if (settings->contains("RigCIVuInt")) {
            flatCiv = (unsigned char)settings->value("RigCIVuInt", defPrefs.radioCIVAddr).toInt();
        }
        if (settings->contains("Manufacturer")) {
            flatManuf = static_cast<manufacturersType_t>(settings->value("Manufacturer", defPrefs.manufacturer).toInt());
            prefs.manufacturer = flatManuf;
        }
        if (settings->contains("SerialPortRadio")) {
            flatSerial = settings->value("SerialPortRadio", defPrefs.serialPortRadio).toString();
        }
        if (settings->contains("SerialPortBaud")) {
            flatBaud = settings->value("SerialPortBaud", defPrefs.serialPortBaud).toInt();
        }
        settings->endGroup();

        qInfo(logSystem()) << "Creating radio config from settings file" << settings->fileName();
        settings->setValue("AudioSystem", defPrefs.audioSystem);
        settings->setValue("Manufacturer", prefs.manufacturer);

        numRadios = 1;
        settings->beginWriteArray("Radios");
        for (int i = 0; i < numRadios; i++)
        {
            settings->setArrayIndex(i);
            settings->setValue("RigCIVuInt", flatCiv);
            settings->setValue("PTTType", defPrefs.pttType);
            settings->setValue("SerialPortRadio", flatSerial);
            settings->setValue("RigName", "<NONE>");
            settings->setValue("SerialPortBaud", flatBaud);
            settings->setValue("AudioInput", "default");
            settings->setValue("AudioOutput", "default");
            settings->setValue("WaterfallFormat", 0);
        }
        settings->endArray();

        settings->beginGroup("Server");
        settings->setValue("ServerEnabled", false);
        settings->setValue("ServerControlPort", udpDefPrefs.controlLANPort);
        settings->setValue("ServerCivPort", udpDefPrefs.serialLANPort);
        settings->setValue("ServerAudioPort", udpDefPrefs.audioLANPort);

        settings->beginWriteArray("Users");
        settings->setArrayIndex(0);
        settings->setValue("Username", "user");
        QByteArray pass;
        passcode("password", pass);
        settings->setValue("Password", QString(pass));
        settings->setValue("UserType", 0);

        settings->endArray();
    
        settings->endGroup();
        settings->sync();

    } else {
        settings->endArray();
    }

    numRadios = settings->beginReadArray("Radios");
    int tempNum = numRadios;
    
    for (int i = 0; i < numRadios; i++) {
        settings->setArrayIndex(i);
        RIGCONFIG* tempPrefs = new RIGCONFIG();
        tempPrefs->civAddr = (unsigned char)settings->value("RigCIVuInt", defPrefs.radioCIVAddr).toInt();

        tempPrefs->pttType = (pttType_t)settings->value("PTTType", defPrefs.pttType).toInt();
        // Workaround for old config option
        if ((bool)settings->value("ForceRTSasPTT", false).toBool())
            tempPrefs->pttType = pttRTS;
        tempPrefs->serialPort = settings->value("SerialPortRadio", defPrefs.serialPortRadio).toString();
        tempPrefs->rigName = settings->value("RigName", "<NONE>").toString();
        tempPrefs->baudRate = (quint32)settings->value("SerialPortBaud", defPrefs.serialPortBaud).toInt();
        tempPrefs->rxAudioSetup.name = settings->value("AudioInput", "default").toString();
        tempPrefs->txAudioSetup.name = settings->value("AudioOutput", "default").toString();
        tempPrefs->waterfallFormat = settings->value("WaterfallFormat", 0).toInt();
        tempPrefs->rxAudioSetup.type = prefs.audioSystem;
        tempPrefs->txAudioSetup.type = prefs.audioSystem;

        if (tempPrefs->serialPort == "auto") {
            // Find the ICOM radio connected, or, if none, fall back to OS default.
            // qInfo(logSystem()) << "Searching for serial port...";
            QString serialPortRig="";
            bool found = false;
            // First try to find first Icom port:
            for(const QSerialPortInfo & serialPortInfo: QSerialPortInfo::availablePorts())
            {
                if (serialPortInfo.serialNumber().left(3) == "IC-") {
                    qInfo(logSystem()) << "Icom Serial Port found: " << serialPortInfo.portName() << "S/N" << serialPortInfo.serialNumber();
#if defined(Q_OS_LINUX) || defined(Q_OS_MAC)
                    tempPrefs->serialPort = (QString("/dev/") + serialPortInfo.portName());
#else
                    tempPrefs->serialPort = serialPortInfo.portName();
#endif
                    tempPrefs->rigName = serialPortInfo.serialNumber();
                    found = true;
                    break;
                }
            }

            if (!found) {
                QDirIterator it73("/dev/serial/by-id", QStringList() << "*IC-7300*", QDir::Files, QDirIterator::Subdirectories);
                QDirIterator it97("/dev/serial", QStringList() << "*IC-9700*A*", QDir::Files, QDirIterator::Subdirectories);
                QDirIterator it785x("/dev/serial", QStringList() << "*IC-785*A*", QDir::Files, QDirIterator::Subdirectories);
                QDirIterator it705("/dev/serial", QStringList() << "*IC-705*A", QDir::Files, QDirIterator::Subdirectories);
                QDirIterator it7610("/dev/serial", QStringList() << "*IC-7610*A", QDir::Files, QDirIterator::Subdirectories);
                QDirIterator itR8600("/dev/serial", QStringList() << "*IC-R8600*A", QDir::Files, QDirIterator::Subdirectories);

                if(!it73.filePath().isEmpty())
                {
                    // IC-7300
                    tempPrefs->serialPort = it73.filePath(); // first
                } else if(!it97.filePath().isEmpty())
                {
                    // IC-9700
                    tempPrefs->serialPort = it97.filePath();
                } else if(!it785x.filePath().isEmpty())
                {
                    // IC-785x
                    tempPrefs->serialPort = it785x.filePath();
                } else if(!it705.filePath().isEmpty())
                {
                    // IC-705
                    tempPrefs->serialPort = it705.filePath();
                } else if(!it7610.filePath().isEmpty())
                {
                    // IC-7610
                    tempPrefs->serialPort = it7610.filePath();
                } else if(!itR8600.filePath().isEmpty())
                {
                    // IC-R8600
                    tempPrefs->serialPort = itR8600.filePath();
                }
                else {
                    //fall back:
                    qInfo(logSystem()) << "Could not find an Icom serial port. Falling back to OS default. Use --serial-port to specify, or modify preferences.";
                    qInfo(logSystem()) << "Found serial ports:";
                    for(const QSerialPortInfo & serialPortInfo: QSerialPortInfo::availablePorts())
                    {
                            qInfo(logSystem()) << serialPortInfo.portName() << "Manufacturer:" << serialPortInfo.manufacturer() << "S/N:" << serialPortInfo.serialNumber();
                    }

#ifdef Q_OS_MAC
                    tempPrefs->serialPort = QString("/dev/tty.SLAB_USBtoUART");
#endif
#ifdef Q_OS_LINUX
                    tempPrefs->serialPort = QString("/dev/ttyUSB0");
#endif
#ifdef Q_OS_WIN
                    tempPrefs->serialPort = QString("COM1");
#endif
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

    settings->endGroup();

    // Read [LAN] section for direct LAN/UDP connection to radio
    settings->beginGroup("LAN");
    prefs.enableLAN = settings->value("EnableLAN", defPrefs.enableLAN).toBool();
    if (prefs.enableLAN) {
        udpPrefs.ipAddress = settings->value("IPAddress", udpDefPrefs.ipAddress).toString();
        udpPrefs.controlLANPort = settings->value("ControlLANPort", udpDefPrefs.controlLANPort).toInt();
        udpPrefs.serialLANPort = settings->value("SerialLANPort", udpDefPrefs.serialLANPort).toInt();
        udpPrefs.audioLANPort = settings->value("AudioLANPort", udpDefPrefs.audioLANPort).toInt();
        udpPrefs.username = settings->value("Username", udpDefPrefs.username).toString();
        udpPrefs.password = settings->value("Password", udpDefPrefs.password).toString();
        rxSetup.sampleRate = settings->value("AudioRXSampleRate", 48000).toInt();
        txSetup.sampleRate = rxSetup.sampleRate;
        rxSetup.codec = settings->value("AudioRXCodec", 4).toInt();
        txSetup.codec = settings->value("AudioTXCodec", 4).toInt();
        qInfo(logSystem()) << "LAN mode enabled, connecting to" << udpPrefs.ipAddress
                           << "control:" << udpPrefs.controlLANPort
                           << "serial:" << udpPrefs.serialLANPort
                           << "audio:" << udpPrefs.audioLANPort;
    }
    settings->endGroup();

    settings->sync();

#if defined(RTAUDIO)
    delete audio;
#endif

}

void servermain::applyCLIOverrides()
{
    // --lan <ip> enables LAN mode and sets IP
    if (!cliOverrides.lanIP.isEmpty()) {
        // If LAN wasn't already enabled via settings file, seed from defaults
        if (!prefs.enableLAN) {
            prefs.enableLAN = true;
            udpPrefs.controlLANPort = udpDefPrefs.controlLANPort;
            udpPrefs.serialLANPort = udpDefPrefs.serialLANPort;
            udpPrefs.audioLANPort = udpDefPrefs.audioLANPort;
            udpPrefs.username = udpDefPrefs.username;
            udpPrefs.password = udpDefPrefs.password;
            udpPrefs.clientName = udpDefPrefs.clientName;
        }
        udpPrefs.ipAddress = cliOverrides.lanIP;
    }

    // LAN port overrides
    if (cliOverrides.controlPort > 0)
        udpPrefs.controlLANPort = cliOverrides.controlPort;
    if (cliOverrides.serialPort > 0)
        udpPrefs.serialLANPort = cliOverrides.serialPort;
    if (cliOverrides.audioPort > 0)
        udpPrefs.audioLANPort = cliOverrides.audioPort;

    // LAN credentials
    if (!cliOverrides.lanUser.isEmpty())
        udpPrefs.username = cliOverrides.lanUser;
    if (!cliOverrides.lanPass.isEmpty())
        udpPrefs.password = cliOverrides.lanPass;

    // Radio overrides
    if (cliOverrides.civAddr >= 0) {
        prefs.radioCIVAddr = (unsigned char)cliOverrides.civAddr;
        for (RIGCONFIG* radio : serverConfig.rigs) {
            radio->civAddr = prefs.radioCIVAddr;
        }
    }
    if (cliOverrides.manufacturer >= 0) {
        prefs.manufacturer = static_cast<manufacturersType_t>(cliOverrides.manufacturer);
    }
    if (cliOverrides.audioSystem >= 0) {
        prefs.audioSystem = static_cast<audioType>(cliOverrides.audioSystem);
        for (RIGCONFIG* radio : serverConfig.rigs) {
            radio->rxAudioSetup.type = prefs.audioSystem;
            radio->txAudioSetup.type = prefs.audioSystem;
        }
    }

    // Audio device override
    if (!cliOverrides.audioDevice.isEmpty()) {
        for (RIGCONFIG* radio : serverConfig.rigs) {
            radio->rxAudioSetup.name = cliOverrides.audioDevice;
            radio->txAudioSetup.name = cliOverrides.audioDevice;
        }
        qInfo(logSystem()) << "Audio device overridden via CLI:" << cliOverrides.audioDevice;
    }

    // Serial port override
    if (!cliOverrides.usbPort.isEmpty()) {
        for (RIGCONFIG* radio : serverConfig.rigs) {
            radio->serialPort = cliOverrides.usbPort;
        }
        qInfo(logSystem()) << "Serial port overridden via CLI:" << cliOverrides.usbPort;
    }

    // Log if LAN mode was enabled via CLI
    if (prefs.enableLAN && !cliOverrides.lanIP.isEmpty()) {
        qInfo(logSystem()) << "LAN mode enabled via CLI, connecting to" << udpPrefs.ipAddress
                           << "control:" << udpPrefs.controlLANPort
                           << "serial:" << udpPrefs.serialLANPort
                           << "audio:" << udpPrefs.audioLANPort;
    }
}

void servermain::updateAudioDevices()
{

    for (RIGCONFIG* rig : serverConfig.rigs)
    {
        qDebug(logAudio()) << "Rig" << rig->rigName << "configured rxAudio device:" << rig->rxAudioSetup.name;
        qDebug(logAudio()) << "Rig" << rig->rigName << "configured txAudio device:" << rig->txAudioSetup.name;

        int inputNum = audioDev->findInput(rig->rigName, rig->rxAudioSetup.name);
        int outputNum = audioDev->findOutput(rig->rigName, rig->txAudioSetup.name);

        if (inputNum < 0 || outputNum < 0) {
            qWarning(logAudio()) << "No local audio devices found for" << rig->rigName
                                 << "- skipping local audio setup (LAN audio unaffected)";
            continue;
        }

        if (prefs.audioSystem == qtAudio) {
            rig->rxAudioSetup.port = audioDev->getInputDeviceInfo(inputNum);
            rig->txAudioSetup.port = audioDev->getOutputDeviceInfo(outputNum);
        }
        else {
            rig->rxAudioSetup.portInt = audioDev->getInputDeviceInt(inputNum);
            rig->txAudioSetup.portInt = audioDev->getOutputDeviceInt(outputNum);
        }
        rig->rxAudioSetup.name = audioDev->getInputName(inputNum);
        rig->txAudioSetup.name = audioDev->getOutputName(outputNum);
    }

}

void servermain::receivePTTstatus(bool pttOn)
{
    // This is the only place where amTransmitting and the transmit button text should be changed:
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
    // Start heartbeat probe while rig boots; after 5 seconds
    // force-reinitialize even if echo detection hasn't fired yet.
    startPowerProbe();
    QTimer::singleShot(5000, this, [this]() {
        if (!rigPoweredOn) {
            rigPoweredOn = true;
            // Immediately notify frontend that rig is on
            queue->receiveValue(funcPowerControl, QVariant::fromValue<bool>(true), 0);
            initPeriodicPolling();
        }
    });
}

void servermain::powerRigOff()
{
    rigPoweredOn = false;
    queue->clear();
    // Immediately notify frontend that rig is off
    queue->receiveValue(funcPowerControl, QVariant::fromValue<bool>(false), 0);
    emit sendPowerOff();
    // Keep a slow heartbeat so we can detect power-on (hardware button)
    startPowerProbe();
}

void servermain::initPeriodicPolling()
{
    // Restore the normal queue interval
    for (RIGCONFIG* radio : serverConfig.rigs) {
        if (radio->rigAvailable && radio->rigCaps != Q_NULLPTR) {
            rigCapabilities* rigCaps = radio->rigCaps;

            if (usingLAN) {
                queue->interval(70);
            } else {
                quint32 baud = prefs.serialPortBaud;
                if (baud == 0) baud = 9600;
                unsigned int usPerByte = 9600 * 1000 / baud;
                unsigned int msMinTiming = usPerByte * 10 * 2 / 1000;
                if (msMinTiming < 25) msMinTiming = 25;
                queue->interval(rigCaps->hasFDcomms ? msMinTiming : msMinTiming * 4);
            }

            // Re-enable CI-V Auto Information so the rig pushes
            // meter/freq/mode updates automatically
            if (rigCaps->commands.contains(funcAutoInformation)) {
                queue->add(priorityImmediate, queueItem(funcAutoInformation, QVariant::fromValue(uchar(2)), false, 0));
                if (rigCaps->commands.contains(funcSWRMeter))
                    queue->add(priorityImmediate, queueItem(funcSWRMeter, QVariant::fromValue(uchar(1)), false, 0));
                if (rigCaps->commands.contains(funcALCMeter))
                    queue->add(priorityImmediate, queueItem(funcALCMeter, QVariant::fromValue(uchar(1)), false, 0));
                if (rigCaps->commands.contains(funcCompMeter))
                    queue->add(priorityImmediate, queueItem(funcCompMeter, QVariant::fromValue(uchar(1)), false, 0));
                if (rigCaps->commands.contains(funcIdMeter))
                    queue->add(priorityImmediate, queueItem(funcIdMeter, QVariant::fromValue(uchar(1)), false, 0));
                if (rigCaps->commands.contains(funcVdMeter))
                    queue->add(priorityImmediate, queueItem(funcVdMeter, QVariant::fromValue(uchar(1)), false, 0));
            }

            // Re-add periodic poll commands
            QSet<funcs> periodicFuncs;
            foreach (auto cap, rigCaps->periodic) {
                if (cap.receiver == char(-1)) {
                    for (uchar v = 0; v < rigCaps->numReceiver; v++)
                        queue->add(queuePriority(cap.prioVal), cap.func, true, v);
                } else {
                    queue->add(queuePriority(cap.prioVal), cap.func, true, cap.receiver);
                }
                periodicFuncs.insert(cap.func);
            }

            if (!periodicFuncs.contains(funcPowerMeter) && rigCaps->commands.contains(funcPowerMeter))
                queue->addUnique(priorityHighest, queueItem(funcPowerMeter, true, 0));
            if (!periodicFuncs.contains(funcSWRMeter) && rigCaps->commands.contains(funcSWRMeter))
                queue->addUnique(priorityHighest, queueItem(funcSWRMeter, true, 0));
            if (!periodicFuncs.contains(funcALCMeter) && rigCaps->commands.contains(funcALCMeter))
                queue->addUnique(priorityHighest, queueItem(funcALCMeter, true, 0));

            if (rigCaps->hasSpectrum) {
                queue->add(priorityImmediate, queueItem(funcScopeOnOff, QVariant::fromValue(quint8(1)), false));
                queue->add(priorityImmediate, queueItem(funcScopeDataOutput, QVariant::fromValue(quint8(1)), false));
            }

            qInfo(logSystem()) << "Periodic polling restored for" << rigCaps->modelName;
            break; // Only one rig supported currently
        }
    }
}

void servermain::startPowerProbe()
{
    // Slow heartbeat: send one recurring probe to detect power state changes.
    // icomcommander detects power via echo responses on any command;
    // Yaesu/Kenwood detect via successful/failed responses.
    // Use priorityHighest so it fires every 2nd tick (every ~4 seconds).
    queue->clear();
    queue->interval(2000);
    queue->add(priorityHighest, funcFreq, true, 0);
}

void servermain::receiveStateInfo(rigstate* state)
{
    qInfo("Received rig state for wfmain");
    Q_UNUSED(state);
    //rigState = state;
}

void servermain::setManufacturer(manufacturersType_t man)
{

    this->rigList.clear();
    qInfo() << "Searching for radios with Manufacturer =" << man;

#ifndef Q_OS_LINUX
    QString systemRigLocation = QCoreApplication::applicationDirPath();
#else
    QString systemRigLocation = PREFIX;
#endif

#ifdef Q_OS_LINUX
    systemRigLocation += "/share/wfview/rigs";
#else
    systemRigLocation +="/rigs";
#endif

    QDir systemRigDir(systemRigLocation);

    if (!systemRigDir.exists()) {
        qWarning() << "********* Rig directory does not exist ********";
    } else {
        QStringList rigs = systemRigDir.entryList(QStringList() << "*.rig" << "*.RIG", QDir::Files);
        for (QString &rig: rigs) {
            QSettings* rigSettings = new QSettings(systemRigDir.absoluteFilePath(rig), QSettings::Format::IniFormat);

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
            rigSettings->setIniCodec("UTF-8");
#endif

            if (!rigSettings->childGroups().contains("Rig"))
            {
                qWarning() << rig << "Does not seem to be a rig description file";
                delete rigSettings;
                continue;
            }

            float ver = rigSettings->value("Version","0.0").toString().toFloat();

            rigSettings->beginGroup("Rig");
            manufacturersType_t manuf = rigSettings->value("Manufacturer",manufIcom).value<manufacturersType_t>();
            if (manuf == man) {
                uchar civ = rigSettings->value("CIVAddress",0).toInt();
                QString model = rigSettings->value("Model","").toString();
                QString path = systemRigDir.absoluteFilePath(rig);

                qDebug() << QString("Found Rig %0 with CI-V address of 0x%1 and version %2").arg(model).arg(civ,2,16,QChar('0')).arg(ver,0,'f',2);
                // Any user modified rig files will override system provided ones.
                this->rigList.insert(civ,rigInfo(civ,model,path,ver));
            }
            rigSettings->endGroup();
            delete rigSettings;
        }
    }

    QString userRigLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/rigs";
    QDir userRigDir(userRigLocation);
    if (userRigDir.exists()){
        QStringList rigs = userRigDir.entryList(QStringList() << "*.rig" << "*.RIG", QDir::Files);
        for (QString& rig: rigs) {
            QSettings* rigSettings = new QSettings(userRigDir.absoluteFilePath(rig), QSettings::Format::IniFormat);

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
            rigSettings->setIniCodec("UTF-8");
#endif

            if (!rigSettings->childGroups().contains("Rig"))
            {
                qWarning() << rig << "Does not seem to be a rig description file";
                delete rigSettings;
                continue;
            }

            float ver = rigSettings->value("Version","0.0").toString().toFloat();

            rigSettings->beginGroup("Rig");

            manufacturersType_t manuf = rigSettings->value("Manufacturer",manufIcom).value<manufacturersType_t>();
            if (manuf == man) {
                uchar civ = rigSettings->value("CIVAddress",0).toInt();
                QString model = rigSettings->value("Model","").toString();
                QString path = userRigDir.absoluteFilePath(rig);

                auto it = this->rigList.find(civ);

                if (it != this->rigList.end())
                {
                    if (ver >= it.value().version) {
                        qInfo() << QString("Found User Rig %0 with CI-V address of 0x%1 and newer or same version than system one (%2>=%3)").arg(model).arg(civ,2,16,QChar('0')).arg(ver,0,'f',2).arg(it.value().version,0,'f',2);
                        this->rigList.insert(civ,rigInfo(civ,model,path,ver));
                    }
                } else {
                    qInfo() << QString("Found New User Rig %0 with CI-V address of 0x%1 version %2").arg(model).arg(civ,2,16,QChar('0')).arg(ver,0,'f',2);
                    this->rigList.insert(civ,rigInfo(civ,model,path,ver));
                }
                // Any user modified rig files will override system provided ones.
            }
            rigSettings->endGroup();
            delete rigSettings;
        }
    }

}
