#include "wfmain.h"
#include "ui_wfmain.h"

#include "commhandler.h"
#include "rigidentities.h"
#include "logcategories.h"


// This code is copyright 2017-2022 Elliott H. Liggett
// All rights reserved

// Log support:
//static void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);
QScopedPointer<QFile> m_logFile;
QMutex logMutex;
QMutex logTextMutex;
QVector<QString> logStringBuffer;
#ifdef QT_DEBUG
bool debugModeLogging = true;
#else
bool debugModeLogging = false;
#endif

wfmain::wfmain(const QString settingsFile, const QString logFile, bool debugMode, QWidget *parent ) :
    QMainWindow(parent),
    ui(new Ui::wfmain),
    logFilename(logFile)
{
    QGuiApplication::setApplicationDisplayName("wfview");
    QGuiApplication::setApplicationName(QString("wfview"));

    setWindowIcon(QIcon( QString(":resources/wfview.png")));
    this->debugMode = debugMode;
    debugModeLogging = debugMode;
    version = QString("wfview version: %1 (Git:%2 on %3 at %4 by %5@%6). Operating System: %7 (%8). Build Qt Version %9. Current Qt Version: %10")
        .arg(QString(WFVIEW_VERSION))
        .arg(GITSHORT).arg(__DATE__).arg(__TIME__).arg(UNAME).arg(HOST)
        .arg(QSysInfo::prettyProductName()).arg(QSysInfo::buildCpuArchitecture())
        .arg(QT_VERSION_STR).arg(qVersion());

    ui->setupUi(this);
    setWindowTitle(QString("wfview"));

    ui->monitorLabel->setText("<a href=\"#\" style=\"color:white; text-decoration:none;\">Mon</a>");


    logWindow = new loggingWindow(logFile);
    initLogging();
    logWindow->setInitialDebugState(debugMode);
    qInfo(logSystem()) << version;

    cal = new calibrationWindow();
    rpt = new repeaterSetup();
    sat = new satelliteSetup();
    trxadj = new transceiverAdjustments();
    cw = new cwSender();
    abtBox = new aboutbox();
    selRad = new selectRadio();
    bandbtns = new bandbuttons();
    finputbtns = new frequencyinputwidget();
    setupui = new settingswidget();

    qRegisterMetaType<udpPreferences>(); // Needs to be registered early.
    qRegisterMetaType<rigCapabilities>();
    qRegisterMetaType<duplexMode_t>();
    qRegisterMetaType<rptAccessTxRx_t>();
    qRegisterMetaType<rptrAccessData>();
    qRegisterMetaType<rigInput>();
    qRegisterMetaType<inputTypes>();
    qRegisterMetaType<meter_t>();
    qRegisterMetaType<spectrumMode_t>();
    qRegisterMetaType<freqt>();
    qRegisterMetaType<vfo_t>();
    qRegisterMetaType<modeInfo>();
    qRegisterMetaType<rigMode_t>();
    qRegisterMetaType<audioPacket>();
    qRegisterMetaType<audioSetup>();
    qRegisterMetaType<SERVERCONFIG>();
    qRegisterMetaType<timekind>();
    qRegisterMetaType<datekind>();
    qRegisterMetaType<QList<radio_cap_packet>>();
    qRegisterMetaType<QVector<BUTTON>*>();
    qRegisterMetaType<QVector<KNOB>*>();
    qRegisterMetaType<QVector<COMMAND>*>();
    qRegisterMetaType<const COMMAND*>();
    qRegisterMetaType<const USBDEVICE*>();
    qRegisterMetaType<QList<radio_cap_packet>>();
    qRegisterMetaType<QList<spotData>>();
    qRegisterMetaType<networkStatus>();
    qRegisterMetaType<networkAudioLevels>();
    qRegisterMetaType<codecType>();
    qRegisterMetaType<errorType>();
    qRegisterMetaType<usbFeatureType>();
    qRegisterMetaType<cmds>();
    qRegisterMetaType<funcs>();
    qRegisterMetaType<rigTypedef>();
    qRegisterMetaType<memoryType>();
    qRegisterMetaType<antennaInfo>();
    qRegisterMetaType<scopeData>();
    qRegisterMetaType<queueItemType>();
    qRegisterMetaType<queueItem>();
    qRegisterMetaType<cacheItem>();
    qRegisterMetaType<spectrumBounds>();
    qRegisterMetaType<centerSpanData>();
    qRegisterMetaType<bandStackType>();

    haveRigCaps = false;

    this->setObjectName("wfmain");
    queue = cachingQueue::getInstance(this);

    connect(queue,SIGNAL(sendValue(cacheItem)),this,SLOT(receiveValue(cacheItem)));
    // We need to populate the list of rigs as early as possible so do it now

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
        foreach (QString rig, rigs) {
            QSettings* rigSettings = new QSettings(systemRigDir.absoluteFilePath(rig), QSettings::Format::IniFormat);
            if (!rigSettings->childGroups().contains("Rig"))
            {
                qWarning() << rig << "Does not seem to be a rig description file";
                delete rigSettings;
                continue;
            }
            rigSettings->beginGroup("Rig");
            qDebug() << QString("Found Rig %0 with CI-V address of %1").arg(rigSettings->value("Model","").toString(), rigSettings->value("CIVAddress",0).toString());
            // Any user modified rig files will override system provided ones.
            this->rigList.insert(rigSettings->value("CIVAddress",0).toInt(),systemRigDir.absoluteFilePath(rig));
            rigSettings->endGroup();
            delete rigSettings;
        }
    }

    QString userRigLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/rigs";
    QDir userRigDir(userRigLocation);
    if (userRigDir.exists()){
        QStringList rigs = userRigDir.entryList(QStringList() << "*.rig" << "*.RIG", QDir::Files);
        foreach (QString rig, rigs) {
            QSettings* rigSettings = new QSettings(userRigDir.absoluteFilePath(rig), QSettings::Format::IniFormat);
            if (!rigSettings->childGroups().contains("Rig"))
            {
                qWarning() << rig << "Does not seem to be a rig description file";
                delete rigSettings;
                continue;
            }
            rigSettings->beginGroup("Rig");
            qDebug() << QString("Found User Rig %0 with CI-V address of %1").arg(rigSettings->value("Model","").toString(), rigSettings->value("CIVAddress",0).toString());
            // Any user modified rig files will override system provided ones.
            this->rigList.insert(rigSettings->value("CIVAddress",0).toInt(),userRigDir.absoluteFilePath(rig));
            rigSettings->endGroup();
            delete rigSettings;
        }
    }

    ui->subScope->setVisible(false); // Disable sub scope until we need it


    setupKeyShortcuts();

    setupMainUI();
    connectSettingsWidget();

    setSerialDevicesUI();

    setDefPrefs();


    getSettingsFilePath(settingsFile);

    setDefaultColorPresets();

    loadSettings(); // Look for saved preferences


    //setAudioDevicesUI(); // no need to call this as it will be called by the updated() signal

    setTuningSteps(); // TODO: Combine into preferences

    qDebug(logSystem()) << "Running setUIToPrefs()";
    setUIToPrefs();

    qDebug(logSystem()) << "Running setInititalTiming()";
    setInitialTiming();

    qDebug(logSystem()) << "Running openRig()";
    openRig();

    qDebug(logSystem()) << "Running rigConnections()";
    rigConnections();

    cluster = new dxClusterClient();

    clusterThread = new QThread(this);
    clusterThread->setObjectName("dxcluster()");

    cluster->moveToThread(clusterThread);

    connect(this, SIGNAL(setClusterEnableUdp(bool)), cluster, SLOT(enableUdp(bool)));
    connect(this, SIGNAL(setClusterEnableTcp(bool)), cluster, SLOT(enableTcp(bool)));
    connect(this, SIGNAL(setClusterUdpPort(int)), cluster, SLOT(setUdpPort(int)));
    connect(this, SIGNAL(setClusterServerName(QString)), cluster, SLOT(setTcpServerName(QString)));
    connect(this, SIGNAL(setClusterTcpPort(int)), cluster, SLOT(setTcpPort(int)));
    connect(this, SIGNAL(setClusterUserName(QString)), cluster, SLOT(setTcpUserName(QString)));
    connect(this, SIGNAL(setClusterPassword(QString)), cluster, SLOT(setTcpPassword(QString)));
    connect(this, SIGNAL(setClusterTimeout(int)), cluster, SLOT(setTcpTimeout(int)));
    connect(this, SIGNAL(setClusterSkimmerSpots(bool)), cluster, SLOT(enableSkimmerSpots(bool)));

    connect(ui->mainScope, SIGNAL(frequencyRange(bool, double, double)), cluster, SLOT(freqRange(bool, double, double)));
    connect(ui->subScope, SIGNAL(frequencyRange(bool, double, double)), cluster, SLOT(freqRange(bool, double, double)));
    connect(cluster, SIGNAL(sendMainSpots(QList<spotData>)), ui->mainScope, SLOT(receiveSpots(QList<spotData>)));
    connect(cluster, SIGNAL(sendSubSpots(QList<spotData>)), ui->subScope, SLOT(receiveSpots(QList<spotData>)));
    connect(cluster, SIGNAL(sendOutput(QString)), this, SLOT(receiveClusterOutput(QString)));

    connect(clusterThread, SIGNAL(finished()), cluster, SLOT(deleteLater()));

    clusterThread->start();

    emit setClusterUdpPort(prefs.clusterUdpPort);
    emit setClusterEnableUdp(prefs.clusterUdpEnable);

    for (int f = 0; f < prefs.clusters.size(); f++)
    {
        if (prefs.clusters[f].isdefault)
        {
            emit setClusterServerName(prefs.clusters[f].server);
            emit setClusterTcpPort(prefs.clusters[f].port);
            emit setClusterUserName(prefs.clusters[f].userName);
            emit setClusterPassword(prefs.clusters[f].password);
            emit setClusterTimeout(prefs.clusters[f].timeout);
            emit setClusterSkimmerSpots(prefs.clusterSkimmerSpotsEnable);
        }
    }
    emit setClusterEnableTcp(prefs.clusterTcpEnable);

    setServerToPrefs();

    amTransmitting = false;

    connect(ui->txPowerSlider, &QSlider::sliderMoved,
        [&](int value) {
          QToolTip::showText(QCursor::pos(), QString("%1").arg(value*100/255), nullptr);
        });


#if !defined(USB_CONTROLLER)
    ui->enableUsbChk->setVisible(false);
    ui->usbControllerBtn->setVisible(false);
    ui->usbControllersResetBtn->setVisible(false);
    ui->usbResetLbl->setVisible(false);
#else
    #if defined(USB_HOTPLUG) && defined(Q_OS_LINUX)
        uDev = udev_new();
        if (!uDev)
        {
            qInfo(logUsbControl()) << "Cannot register udev, hotplug of USB devices is not available";
            return;
        }
        uDevMonitor = udev_monitor_new_from_netlink(uDev, "udev");
        if (!uDevMonitor)
        {
            qInfo(logUsbControl()) << "Cannot register udev_monitor, hotplug of USB devices is not available";
            return;
        }
    int fd = udev_monitor_get_fd(uDevMonitor);
        uDevNotifier = new QSocketNotifier(fd, QSocketNotifier::Read,this);
    connect(uDevNotifier, SIGNAL(activated(int)), this, SLOT(uDevEvent()));
        udev_monitor_enable_receiving(uDevMonitor);
    #endif
#endif

}

wfmain::~wfmain()
{
    rigThread->quit();
    rigThread->wait();

    if (serverThread != Q_NULLPTR) {
        serverThread->quit();
        serverThread->wait();
    }
    if (clusterThread != Q_NULLPTR) {
        clusterThread->quit();
        clusterThread->wait();
    }

    if (tciThread != Q_NULLPTR) {
        tciThread->quit();
        tciThread->wait();
    }

    if (rigCtl != Q_NULLPTR) {
        delete rigCtl;
    }

    if (colorPrefs != Q_NULLPTR) {
        delete colorPrefs;
    }

    delete rpt;
    delete ui;
    delete settings;
#if defined(USB_CONTROLLER)
    if (usbControllerThread != Q_NULLPTR) {
        usbControllerThread->quit();
        usbControllerThread->wait();
    }
    #if defined(Q_OS_LINUX)
        if (uDevMonitor)
        {
            udev_monitor_unref(uDevMonitor);
            udev_unref(uDev);
            delete uDevNotifier;
        }
    #endif
#endif
}

void wfmain::closeEvent(QCloseEvent *event)
{
    if (on_exitBtn_clicked())
        event->ignore();

}

void wfmain::openRig()
{
    // This function is intended to handle opening a connection to the rig.
    // the connection can be either serial or network,
    // and this function is also responsible for initiating the search for a rig model and capabilities.
    // Any errors, such as unable to open connection or unable to open port, are to be reported to the user.

    //TODO: if(hasRunPreviously)

    //TODO: if(useNetwork){...

    // } else {

    // if (prefs.fileWasNotFound) {
    //     showRigSettings(); // rig setting dialog box for network/serial, CIV, hostname, port, baud rate, serial device, etc
    // TODO: How do we know if the setting was loaded?

    emit connectionStatus(true); // Signal any other parts that need to know if we are connecting/connected.
    ui->connectBtn->setText("Cancel connection"); // We are attempting to connect
    connStatus = connConnecting;

    makeRig();

    if (prefs.enableLAN)
    {
        usingLAN = true;
        // "We need to setup the tx/rx audio:
        udpPrefs.waterfallFormat = prefs.waterfallFormat;
        emit sendCommSetup(rigList, prefs.radioCIVAddr, udpPrefs, prefs.rxSetup, prefs.txSetup, prefs.virtualSerialPort, prefs.tcpPort);
    } else {
        if( (prefs.serialPortRadio.toLower() == QString("auto")))
        {
            findSerialPort();
        } else {
            serialPortRig = prefs.serialPortRadio;
        }
        usingLAN = false;
        emit sendCommSetup(rigList, prefs.radioCIVAddr, serialPortRig, prefs.serialPortBaud,prefs.virtualSerialPort, prefs.tcpPort,prefs.waterfallFormat);
        ui->statusBar->showMessage(QString("Connecting to rig using serial port ").append(serialPortRig), 1000);
    }
}

void wfmain::rigConnections()
{


    connect(this, SIGNAL(setCIVAddr(unsigned char)), rig, SLOT(setCIVAddr(unsigned char)));

    connect(this, SIGNAL(sendPowerOn()), rig, SLOT(powerOn()));
    connect(this, SIGNAL(sendPowerOff()), rig, SLOT(powerOff()));


    connect(this, SIGNAL(getDebug()), rig, SLOT(getDebug()));

    // Repeater, duplex, and split:
    //connect(rpt, SIGNAL(getDuplexMode()), rig, SLOT(getDuplexMode()));
    //connect(rpt, SIGNAL(setDuplexMode(duplexMode_t)), rig, SLOT(setDuplexMode(duplexMode_t)));
    //connect(rig, SIGNAL(haveDuplexMode(duplexMode_t)), rpt, SLOT(receiveDuplexMode(duplexMode_t)));
    //connect(this, SIGNAL(getRptDuplexOffset()), rig, SLOT(getRptDuplexOffset()));
    connect(rig, SIGNAL(haveRptOffsetFrequency(freqt)), rpt, SLOT(handleRptOffsetFrequency(freqt)));

    // Memories
    //connect(this, SIGNAL(setMemoryMode()), rig, SLOT(setMemoryMode()));
    //connect(this, SIGNAL(setSatelliteMode(bool)), rig, SLOT(setSatelliteMode(bool)));
    //connect(this, SIGNAL(getMemory(quint32)), rig, SLOT(getMemory(quint32)));
    //connect(this, SIGNAL(getSatMemory(quint32)), rig, SLOT(getSatMemory(quint32)));
    //connect(this, SIGNAL(setMemory(memoryType)), rig, SLOT(setMemory(memoryType)));
    //connect(this, SIGNAL(clearMemory(quint32)), rig, SLOT(clearMemory(quint32)));
    //connect(this, SIGNAL(recallMemory(quint32)), rig, SLOT(recallMemory(quint32)));

    connect(this->rpt, &repeaterSetup::setDuplexMode, this->rig,
            [=](const duplexMode_t &t) { queue->add(priorityImmediate,queueItem(funcSplitStatus,QVariant::fromValue<duplexMode_t>(t),false));});

    connect(this->rpt, &repeaterSetup::getTone, this->rig,
            [=]() { queue->add(priorityImmediate,funcRepeaterTone);});

    connect(this->rpt, &repeaterSetup::setTSQL, this->rig,
            [=](const toneInfo& t) { queue->add(priorityImmediate,queueItem(funcTSQLFreq,QVariant::fromValue<toneInfo>(t),false));});

    connect(this->rpt, &repeaterSetup::getTSQL, this->rig,
            [=]() { queue->add(priorityImmediate,funcRepeaterTSQL);});

    connect(this->rpt, &repeaterSetup::setDTCS, this->rig,
            [=](const toneInfo& t) { queue->add(priorityImmediate,queueItem(funcRepeaterDTCS,QVariant::fromValue<toneInfo>(t),false));});

    connect(this->rpt, &repeaterSetup::getDTCS, this->rig,
            [=]() { queue->add(priorityImmediate,funcRepeaterDTCS);});


    connect(this->rpt, &repeaterSetup::getRptAccessMode, this->rig,
            [=]() {
            if (rigCaps.commands.contains(funcToneSquelchType)) {
                queue->add(priorityImmediate,funcToneSquelchType,false);
            } else {
                queue->add(priorityImmediate,funcRepeaterTone,false);
                queue->add(priorityImmediate,funcRepeaterTSQL,false);
            }
    });

    connect(this->rpt, &repeaterSetup::setQuickSplit, this->rig,
            [=](const bool &qsEnabled) {
            queue->add(priorityImmediate,queueItem(funcQuickSplit,QVariant::fromValue<bool>(qsEnabled),false));
    });

    connect(this->rpt, &repeaterSetup::setRptAccessMode, this->rig,
            [=](const rptrAccessData &rd) {
                queue->add(priorityImmediate,queueItem(funcToneSquelchType,QVariant::fromValue<rptrAccessData>(rd),false));
        });

    connect(this->rig, &rigCommander::haveDuplexMode, this->rpt,
            [=](const duplexMode_t &dm) {
                if(dm==dmSplitOn)
                    this->splitModeEnabled = true;
                else
                    this->splitModeEnabled = false;
    });

    connect(this->rpt, &repeaterSetup::setTransmitFrequency, this->rig,
            [=](const freqt &transmitFreq) { queue->add(priorityImmediate,queueItem(funcFreqSet,QVariant::fromValue<freqt>(transmitFreq),false));});

    connect(this->rpt, &repeaterSetup::setTransmitMode, this->rig,
            [=](const modeInfo &transmitMode) {  queue->add(priorityImmediate,queueItem(funcModeSet,QVariant::fromValue<modeInfo>(transmitMode),false));});

    connect(this->rpt, &repeaterSetup::selectVFO, this->rig,
            [=](const vfo_t &v) { queue->add(priorityImmediate,queueItem(funcSelectVFO,QVariant::fromValue<vfo_t>(v),false));});

    connect(this->rpt, &repeaterSetup::equalizeVFOsAB, this->rig,
            [=]() { queue->add(priorityImmediate,funcVFOEqualAB);});

    connect(this->rpt, &repeaterSetup::equalizeVFOsMS, this->rig,
            [=]() {  queue->add(priorityImmediate,funcVFOEqualMS);});

    connect(this->rpt, &repeaterSetup::swapVFOs, this->rig,
            [=]() {  queue->add(priorityImmediate,funcVFOSwapMS);});

    connect(this->rpt, &repeaterSetup::setRptDuplexOffset, this->rig,
            [=](const freqt &fOffset) { queue->add(priorityImmediate,queueItem(funcSendFreqOffset,QVariant::fromValue<freqt>(fOffset),false));});

    connect(this->rpt, &repeaterSetup::getRptDuplexOffset, this->rig,
            [=]() {  queue->add(priorityImmediate,funcReadFreqOffset);});

    connect(this, SIGNAL(setAfGain(unsigned char)), rig, SLOT(setAfGain(unsigned char)));

    connect(ui->mainScope, SIGNAL(updateSettings(bool,int,quint16,int,int)), this, SLOT(receiveScopeSettings(bool,int,quint16,int,int)));
    connect(ui->subScope, SIGNAL(updateSettings(bool,int,quint16,int,int)), this, SLOT(receiveScopeSettings(bool,int,quint16,int,int)));

    connect(ui->mainScope, SIGNAL(elapsedTime(bool,qint64)), this, SLOT(receiveElapsed(bool,qint64)));
    connect(ui->subScope, SIGNAL(elapsedTime(bool,qint64)), this, SLOT(receiveElapsed(bool,qint64)));
}

void wfmain::makeRig()
{
    if (rigThread == Q_NULLPTR)
    {
        rig = new rigCommander();
        rigThread = new QThread(this);
        rigThread->setObjectName("rigCommander()");


        // Thread:
        rig->moveToThread(rigThread);
        connect(rigThread, SIGNAL(started()), rig, SLOT(process()));
        connect(rigThread, SIGNAL(finished()), rig, SLOT(deleteLater()));
        rigThread->start();

        // Rig status and Errors:
        connect(rig, SIGNAL(havePortError(errorType)), this, SLOT(receivePortError(errorType)));
        connect(rig, SIGNAL(haveStatusUpdate(networkStatus)), this, SLOT(receiveStatusUpdate(networkStatus)));
        connect(rig, SIGNAL(haveNetworkAudioLevels(networkAudioLevels)), this, SLOT(receiveNetworkAudioLevels(networkAudioLevels)));

        connect(rig, SIGNAL(requestRadioSelection(QList<radio_cap_packet>)), this, SLOT(radioSelection(QList<radio_cap_packet>)));
        connect(rig, SIGNAL(setRadioUsage(quint8, quint8, QString, QString)), selRad, SLOT(setInUse(quint8, quint8, QString, QString)));
        connect(selRad, SIGNAL(selectedRadio(quint8)), rig, SLOT(setCurrentRadio(quint8)));
        // Rig comm setup:
        connect(this, SIGNAL(sendCommSetup(rigTypedef,unsigned char, udpPreferences, audioSetup, audioSetup, QString, quint16)), rig, SLOT(commSetup(rigTypedef,unsigned char, udpPreferences, audioSetup, audioSetup, QString, quint16)));
        connect(this, SIGNAL(sendCommSetup(rigTypedef,unsigned char, QString, quint32,QString, quint16,quint8)), rig, SLOT(commSetup(rigTypedef,unsigned char, QString, quint32,QString, quint16,quint8)));
        connect(this, SIGNAL(setRTSforPTT(bool)), rig, SLOT(setRTSforPTT(bool)));

        connect(rig, SIGNAL(haveBaudRate(quint32)), this, SLOT(receiveBaudRate(quint32)));

        connect(this, SIGNAL(sendCloseComm()), rig, SLOT(closeComm()));
        connect(this, SIGNAL(sendChangeLatency(quint16)), rig, SLOT(changeLatency(quint16)));
        connect(this, SIGNAL(getRigCIV()), rig, SLOT(findRigs()));
        connect(this, SIGNAL(setRigID(unsigned char)), rig, SLOT(setRigID(unsigned char)));
        connect(rig, SIGNAL(discoveredRigID(rigCapabilities)), this, SLOT(receiveFoundRigID(rigCapabilities)));
        connect(rig, SIGNAL(commReady()), this, SLOT(receiveCommReady()));

        // Create link for server so it can have easy access to rig.
        if (serverConfig.rigs.first() != Q_NULLPTR) {
            serverConfig.rigs.first()->rig = rig;
            serverConfig.rigs.first()->rigThread = rigThread;
        }
    }
}

void wfmain::removeRig()
{
    if (rigThread != Q_NULLPTR)
    {
        rigThread->quit();
        rigThread->wait();
        rig = Q_NULLPTR;
        rigThread = Q_NULLPTR;
    }
}


void wfmain::findSerialPort()
{
    // Find the ICOM radio connected, or, if none, fall back to OS default.
    // qInfo(logSystem()) << "Searching for serial port...";
    bool found = false;
    // First try to find first Icom port:
    foreach(const QSerialPortInfo & serialPortInfo, QSerialPortInfo::availablePorts())
    {
        if (serialPortInfo.serialNumber().left(3) == "IC-") {
            qInfo(logSystem()) << "Serial Port found: " << serialPortInfo.portName() << "Manufacturer:" << serialPortInfo.manufacturer() << "Product ID" << serialPortInfo.description() << "S/N" << serialPortInfo.serialNumber();
#if defined(Q_OS_LINUX) || defined(Q_OS_MAC)
            serialPortRig = (QString("/dev/") + serialPortInfo.portName());
#else
            serialPortRig = serialPortInfo.portName();
#endif
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
        }
        else {
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
}

void wfmain::receiveCommReady()
{
    qInfo(logSystem()) << "Received CommReady!! ";
    if(!usingLAN)
    {
        // usingLAN gets set when we emit the sendCommSetup signal.
        // If we're not using the LAN, then we're on serial, and
        // we already know the baud rate and can calculate the timing parameters.
        calculateTimingParameters();
    }
    if(prefs.radioCIVAddr == 0)
    {
        // tell rigCommander to broadcast a request for all rig IDs.
        // qInfo(logSystem()) << "Beginning search from wfview for rigCIV (auto-detection broadcast)";
        ui->statusBar->showMessage(QString("Searching CI-V bus for connected radios."), 1000);

        //emit getRigCIV();
        //queue->add(priorityImmediate,funcTransceiverId,false);
        queue->addUnique(priorityHighest,funcTransceiverId,true);
        //issueDelayedCommand(cmdGetRigCIV);
        //delayedCommand->start();
    } else {
        // don't bother, they told us the CIV they want, stick with it.
        // We still query the rigID to find the model, but at least we know the CIV.
        qInfo(logSystem()) << "Skipping automatic CIV, using user-supplied value of " << prefs.radioCIVAddr;
        showStatusBarText(QString("Using user-supplied radio CI-V address of 0x%1").arg(prefs.radioCIVAddr, 2, 16));
        if(prefs.CIVisRadioModel)
        {
            qInfo(logSystem()) << "Skipping Rig ID query, using user-supplied model from CI-V address: " << prefs.radioCIVAddr;
            emit setCIVAddr(prefs.radioCIVAddr);
            emit setRigID(prefs.radioCIVAddr);
        } else {
            emit setCIVAddr(prefs.radioCIVAddr);
            //emit getRigCIV();
            //queue->add(priorityImmediate,funcTransceiverId,false);
            queue->addUnique(priorityHighest,funcTransceiverId,true);
            //emit getRigID();
            //issueDelayedCommand(cmdGetRigID);
            //delayedCommand->start();
        }
    }
}


void wfmain::receiveFoundRigID(rigCapabilities rigCaps)
{
    // Entry point for unknown rig being identified at the start of the program.
    //now we know what the rig ID is:
    //qInfo(logSystem()) << "In wfview, we now have a reply to our request for rig identity sent to CIV BROADCAST.";

    receiveRigID(rigCaps);

    initPeriodicCommands();
    getInitialRigState();

    return;
}

void wfmain::receivePortError(errorType err)
{
    if (err.alert) {
        connectionHandler(false); // Force disconnect
        QMessageBox::critical(this, err.device, err.message, QMessageBox::Ok);
    }
    else
    {
        qInfo(logSystem()) << "wfmain: received error for device: " << err.device << " with message: " << err.message;
        ui->statusBar->showMessage(QString("ERROR: using device ").append(err.device).append(": ").append(err.message), 10000);
    }
    // TODO: Dialog box, exit, etc
}

void wfmain::receiveStatusUpdate(networkStatus status)
{

    //this->rigStatus->setText(QString("%0/%1 %2").arg(mainElapsed).arg(subElapsed).arg(status.message));
    this->rigStatus->setText(status.message);
    selRad->audioOutputLevel(status.rxAudioLevel);
    selRad->audioInputLevel(status.txAudioLevel);
    //qInfo(logSystem()) << "Got Status Update" << status.rxAudioLevel;
}

void wfmain::receiveNetworkAudioLevels(networkAudioLevels l)
{
    /*
    meter_t m2mtr = ui->meter2Widget->getMeterType();

    if(m2mtr == meterAudio)
    {
        if(amTransmitting)
        {
            if(l.haveTxLevels)
                ui->meter2Widget->setLevels(l.txAudioRMS, l.txAudioPeak);
        } else {
            if(l.haveRxLevels)
                ui->meter2Widget->setLevels(l.rxAudioRMS, l.rxAudioPeak);
        }
    } else if (m2mtr == meterTxMod) {
        if(l.haveTxLevels)
            ui->meter2Widget->setLevels(l.txAudioRMS, l.txAudioPeak);
    } else if (m2mtr == meterRxAudio) {
        if(l.haveRxLevels)
            ui->meter2Widget->setLevels(l.rxAudioRMS, l.rxAudioPeak);
    }
    */


    meter_t m = meterNone;
    if(l.haveRxLevels)
    {
        m = meterRxAudio;
        receiveMeter(m, l.rxAudioPeak);
    }
    if(l.haveTxLevels)
    {
        m = meterTxMod;
        receiveMeter(m, l.txAudioPeak);
    }

}

void wfmain::setupMainUI()
{
    ui->meter2Widget->hide();

    // Future ideas:
    //ui->meter2selectionCombo->addItem("Transmit Audio", meterTxMod);
    //ui->meter2selectionCombo->addItem("Receive Audio", meterRxAudio);
    //ui->meter2selectionCombo->addItem("Latency", meterLatency);

    // Set scroll wheel response (tick interval)
    // and set arrow key response (single step)
    ui->rfGainSlider->setTickInterval(100);
    ui->rfGainSlider->setSingleStep(10);

    ui->afGainSlider->setTickInterval(100);
    ui->afGainSlider->setSingleStep(10);

    ui->sqlSlider->setTickInterval(100);
    ui->sqlSlider->setSingleStep(10);

    ui->txPowerSlider->setTickInterval(100);
    ui->txPowerSlider->setSingleStep(10);

    ui->micGainSlider->setTickInterval(100);
    ui->micGainSlider->setSingleStep(10);

    ui->monitorSlider->setTickInterval(100);
    ui->monitorSlider->setSingleStep(10);

    // Keep this code when the rest is removed from this function:
    qDebug(logSystem()) << "Running with debugging options enabled.";


    rigStatus = new QLabel(this);
    ui->statusBar->addPermanentWidget(rigStatus);
    ui->statusBar->showMessage("Connecting to rig...", 1000);

    pttLed = new QLedLabel(this);
    ui->statusBar->addPermanentWidget(pttLed);
    pttLed->setState(QLedLabel::State::StateOk);
    pttLed->setToolTip("Receiving");

    connectedLed = new QLedLabel(this);
    ui->statusBar->addPermanentWidget(connectedLed);

    rigName = new QLabel(this);
    rigName->setAlignment(Qt::AlignRight);
    ui->statusBar->addPermanentWidget(rigName);
    rigName->setText("NONE");
    rigName->setFixedWidth(60);

    freqt f;
    f.MHzDouble = 0.0;
    f.Hz = 0;
    ui->mainScope->setIdentity("Main Band",false);
    ui->subScope->setIdentity("Sub Band",true);

    connect(ui->mainScope,SIGNAL(showStatusBarText(QString)),this,SLOT(showStatusBarText(QString)));
    connect(ui->subScope,SIGNAL(showStatusBarText(QString)),this,SLOT(showStatusBarText(QString)));

    oldFreqDialVal = ui->freqDial->value();

    ui->tuneLockChk->setChecked(false);
    freqLock = false;

    connect(
        ui->txPowerSlider, &QSlider::valueChanged, this,
        [=](const int &newValue) { statusFromSliderPercent("Tx Power", newValue);}
        );

    connect(
                ui->rfGainSlider, &QSlider::valueChanged, this,
                [=](const int &newValue) { statusFromSliderPercent("RF Gain", newValue);}
    );

    connect(
                ui->afGainSlider, &QSlider::valueChanged, this,
                [=](const int &newValue) { statusFromSliderPercent("AF Gain", newValue);}
    );

    connect(
                ui->micGainSlider, &QSlider::valueChanged, this,
                [=](const int &newValue) { statusFromSliderPercent("TX Audio Gain", newValue);}
    );

    connect(
                ui->sqlSlider, &QSlider::valueChanged, this,
                [=](const int &newValue) { statusFromSliderPercent("Squelch", newValue);}
    );

/*
    connect(this->cw, &cwSender::sendCW,
            [=](const QString &cwMessage) { issueCmd(cmdSendCW, cwMessage);});
    connect(this->cw, &cwSender::stopCW,
            [=]() { issueDelayedCommand(cmdStopCW);});
    connect(this->cw, &cwSender::setBreakInMode,
            [=](const unsigned char &bmode) { issueCmd(cmdSetBreakMode, bmode);});
    connect(this->cw, &cwSender::setKeySpeed,
        [=](const unsigned char& wpm) { issueCmd(cmdSetKeySpeed, wpm); });
    connect(this->cw, &cwSender::setPitch,
        [=](const unsigned char& pitch) { issueCmd(cmdSetCwPitch, pitch); });
    connect(this->cw, &cwSender::getCWSettings,
            [=]() { issueDelayedCommand(cmdGetKeySpeed);
                    issueDelayedCommand(cmdGetBreakMode);});
*/

}

void wfmain::connectSettingsWidget()
{
    connect(setupui, SIGNAL(changedClusterPref(prefClusterItem)), this, SLOT(extChangedClusterPref(prefClusterItem)));
    connect(setupui, SIGNAL(changedClusterPrefs(quint64)), this, SLOT(extChangedClusterPrefs(quint64)));

    connect(setupui, SIGNAL(changedCtPref(prefCtItem)), this, SLOT(extChangedCtPref(prefCtItem)));
    connect(setupui, SIGNAL(changedCtPrefs(quint64)), this, SLOT(extChangedCtPrefs(quint64)));

    connect(setupui, SIGNAL(changedIfPref(prefIfItem)), this, SLOT(extChangedIfPref(prefIfItem)));
    connect(setupui, SIGNAL(changedIfPrefs(quint64)), this, SLOT(extChangedIfPrefs(quint64)));

    connect(setupui, SIGNAL(changedColPref(prefColItem)), this, SLOT(extChangedColPref(prefColItem)));
    connect(setupui, SIGNAL(changedColPrefs(quint64)), this, SLOT(extChangedColPrefs(quint64)));

    connect(setupui, SIGNAL(changedLanPref(prefLanItem)), this, SLOT(extChangedLanPref(prefLanItem)));
    connect(setupui, SIGNAL(changedLanPrefs(quint64)), this, SLOT(extChangedLanPrefs(quint64)));

    connect(setupui, SIGNAL(changedRaPref(prefRaItem)), this, SLOT(extChangedRaPref(prefRaItem)));
    connect(setupui, SIGNAL(changedRaPrefs(quint64)), this, SLOT(extChangedRaPrefs(quint64)));

    connect(setupui, SIGNAL(changedRsPref(prefRsItem)), this, SLOT(extChangedRsPref(prefRsItem)));
    connect(setupui, SIGNAL(changedRsPrefs(quint64)), this, SLOT(extChangedRsPrefs(quint64)));

    connect(setupui, SIGNAL(changedUdpPref(prefUDPItem)), this, SLOT(extChangedUdpPref(prefUDPItem)));
    connect(setupui, SIGNAL(changedUdpPrefs(quint64)), this, SLOT(extChangedUdpPrefs(quint64)));

    connect(setupui, SIGNAL(changedServerPref(prefServerItem)), this, SLOT(extChangedServerPref(prefServerItem)));
    connect(setupui, SIGNAL(changedServerPrefs(quint64)), this, SLOT(extChangedServerPrefs(quint64)));

    //connect(setupui, SIGNAL(changedAudioInputCombo(int)), this, SLOT(changedAudioInput(int)));
    //connect(setupui, SIGNAL(changedAudioOutputCombo(int)), this, SLOT(changedAudioOutput(int)));
    //connect(setupui, SIGNAL(changedServerRXAudioInputCombo(int)), this, SLOT(changedServerRXAudioInput(int)));
    //connect(setupui, SIGNAL(changedServerTXAudioOutputCombo(int)), this, SLOT(changedServerTXAudioOutput(int)));

    connect(setupui, SIGNAL(changedModInput(uchar,inputTypes)), this, SLOT(changedModInput(uchar,inputTypes)));

    connect(this, SIGNAL(connectionStatus(bool)), setupui, SLOT(connectionStatus(bool)));
}

// NOT Migrated, EHL TODO, carefully remove this function

void wfmain::getSettingsFilePath(QString settingsFile)
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
        settings = new QSettings(path + file, QSettings::Format::IniFormat);
    }
}


void wfmain::setInitialTiming()
{
    loopTickCounter = 0;
    delayedCmdIntervalLAN_ms = 70; // interval for regular delayed commands, including initial rig/UI state queries
    delayedCmdIntervalSerial_ms = 100; // interval for regular delayed commands, including initial rig/UI state queries
    delayedCmdStartupInterval_ms = 250; // interval for rigID polling
    delayedCommand = new QTimer(this);
    queue->interval(delayedCmdStartupInterval_ms);

    pttTimer = new QTimer(this);
    pttTimer->setInterval(180*1000); // 3 minute max transmit time in ms
    pttTimer->setSingleShot(true);
    connect(pttTimer, SIGNAL(timeout()), this, SLOT(handlePttLimit()));

    timeSync = new QTimer(this);
    connect(timeSync, SIGNAL(timeout()), this, SLOT(setRadioTimeDateSend()));
    waitingToSetTimeDate = false;
    lastFreqCmdTime_ms = QDateTime::currentMSecsSinceEpoch() - 5000; // 5 seconds ago
}

void wfmain::setServerToPrefs()
{

    // Start server if enabled in config
    //ui->serverSetupGroup->setEnabled(serverConfig.enabled);
    if (serverThread != Q_NULLPTR) {
        serverThread->quit();
        serverThread->wait();
        serverThread = Q_NULLPTR;
        udp = Q_NULLPTR;
        ui->statusBar->showMessage(QString("Server disabled"), 1000);
    }

    if (serverConfig.enabled) {
        serverConfig.lan = prefs.enableLAN;

        udp = new udpServer(&serverConfig);

        serverThread = new QThread(this);

        udp->moveToThread(serverThread);


        connect(this, SIGNAL(initServer()), udp, SLOT(init()));
        connect(serverThread, SIGNAL(finished()), udp, SLOT(deleteLater()));

        if (rig != Q_NULLPTR) {
            connect(rig, SIGNAL(haveAudioData(audioPacket)), udp, SLOT(receiveAudioData(audioPacket)));
            // Need to add a signal/slot for audio from the client to rig.
            //connect(udp, SIGNAL(haveAudioData(audioPacket)), rig, SLOT(receiveAudioData(audioPacket)));
            connect(rig, SIGNAL(haveDataForServer(QByteArray)), udp, SLOT(dataForServer(QByteArray)));
            connect(udp, SIGNAL(haveDataFromServer(QByteArray)), rig, SLOT(dataFromServer(QByteArray)));
        }

        if (serverConfig.lan) {
            connect(udp, SIGNAL(haveNetworkStatus(networkStatus)), this, SLOT(receiveStatusUpdate(networkStatus)));
        } else {
            qInfo(logAudio()) << "Audio Input device " << serverConfig.rigs.first()->rxAudioSetup.name;
            qInfo(logAudio()) << "Audio Output device " << serverConfig.rigs.first()->txAudioSetup.name;
        }

        serverThread->start();

        emit initServer();

        connect(this, SIGNAL(sendRigCaps(rigCapabilities)), udp, SLOT(receiveRigCaps(rigCapabilities)));

        ui->statusBar->showMessage(QString("Server enabled"), 1000);

    }
}

void wfmain::setUIToPrefs()
{
    //ui->fullScreenChk->setChecked(prefs.useFullScreen);
    changeFullScreenMode(prefs.useFullScreen);

    //ui->useSystemThemeChk->setChecked(prefs.useSystemTheme);
    useSystemTheme(prefs.useSystemTheme);

    ui->mainScope->setUnderlayMode(prefs.underlayMode);
    ui->mainScope->wfAntiAliased(prefs.wfAntiAlias);
    ui->mainScope->wfInterpolate(prefs.wfInterpolate);
    ui->mainScope->wfTheme(prefs.mainWfTheme);
    ui->mainScope->setScrollSpeedXY(prefs.scopeScrollX, prefs.scopeScrollY);

    ui->subScope->setUnderlayMode(prefs.underlayMode);
    ui->subScope->wfAntiAliased(prefs.wfAntiAlias);
    ui->subScope->wfInterpolate(prefs.wfInterpolate);
    ui->subScope->wfTheme(prefs.subWfTheme);
    ui->subScope->setScrollSpeedXY(prefs.scopeScrollX, prefs.scopeScrollY);

    //    switch(underlayMode)
//    {
//        case underlayNone:
//            ui->underlayNone->setChecked(true);
//            break;
//        case underlayPeakHold:
//            ui->underlayPeakHold->setChecked(true);
//            break;
//        case underlayPeakBuffer:
//            ui->underlayPeakBuffer->setChecked(true);
//            break;
//        case underlayAverageBuffer:
//            ui->underlayAverageBuffer->setChecked(true);
//            break;
//        default:
//            break;
//    }

    ui->mainScope->prepareWf(prefs.mainWflength);
    ui->mainScope->preparePlasma();
    ui->subScope->prepareWf(prefs.subWflength);
    ui->subScope->preparePlasma();

    ui->mainScope->setRange(prefs.mainPlotFloor, prefs.mainPlotCeiling);
    ui->subScope->setRange(prefs.subPlotFloor, prefs.subPlotCeiling);

    ui->mainScope->wfTheme(prefs.mainWfTheme);
    ui->subScope->wfTheme(prefs.subWfTheme);

    finputbtns->setAutomaticSidebandSwitching(prefs.automaticSidebandSwitching);
}

void wfmain::setSerialDevicesUI()
{

}


void wfmain::setupKeyShortcuts()
{
    keyF1 = new QShortcut(this);
    keyF1->setKey(Qt::Key_F1);
    connect(keyF1, SIGNAL(activated()), this, SLOT(shortcutF1()));

    keyF2 = new QShortcut(this);
    keyF2->setKey(Qt::Key_F2);
    connect(keyF2, SIGNAL(activated()), this, SLOT(shortcutF2()));

    keyF3 = new QShortcut(this);
    keyF3->setKey(Qt::Key_F3);
    connect(keyF3, SIGNAL(activated()), this, SLOT(shortcutF3()));

    keyF4 = new QShortcut(this);
    keyF4->setKey(Qt::Key_F4);
    connect(keyF4, SIGNAL(activated()), this, SLOT(shortcutF4()));

    keyF5 = new QShortcut(this);
    keyF5->setKey(Qt::Key_F5);
    connect(keyF5, SIGNAL(activated()), this, SLOT(shortcutF5()));

    keyF6 = new QShortcut(this);
    keyF6->setKey(Qt::Key_F6);
    connect(keyF6, SIGNAL(activated()), this, SLOT(shortcutF6()));

    keyF7 = new QShortcut(this);
    keyF7->setKey(Qt::Key_F7);
    connect(keyF7, SIGNAL(activated()), this, SLOT(shortcutF7()));

    keyF8 = new QShortcut(this);
    keyF8->setKey(Qt::Key_F8);
    connect(keyF8, SIGNAL(activated()), this, SLOT(shortcutF8()));

    keyF9 = new QShortcut(this);
    keyF9->setKey(Qt::Key_F9);
    connect(keyF9, SIGNAL(activated()), this, SLOT(shortcutF9()));

    keyF10 = new QShortcut(this);
    keyF10->setKey(Qt::Key_F10);
    connect(keyF10, SIGNAL(activated()), this, SLOT(shortcutF10()));

    keyF11 = new QShortcut(this);
    keyF11->setKey(Qt::Key_F11);
    connect(keyF11, SIGNAL(activated()), this, SLOT(shortcutF11()));

    keyF12 = new QShortcut(this);
    keyF12->setKey(Qt::Key_F12);
    connect(keyF12, SIGNAL(activated()), this, SLOT(shortcutF12()));

    keyControlT = new QShortcut(this);
    keyControlT->setKey(Qt::CTRL | Qt::Key_T);
    connect(keyControlT, SIGNAL(activated()), this, SLOT(shortcutControlT()));

    keyControlR = new QShortcut(this);
    keyControlR->setKey(Qt::CTRL | Qt::Key_R);
    connect(keyControlR, SIGNAL(activated()), this, SLOT(shortcutControlR()));

    keyControlI = new QShortcut(this);
    keyControlI->setKey(Qt::CTRL | Qt::Key_I);
    connect(keyControlI, SIGNAL(activated()), this, SLOT(shortcutControlI()));

    keyControlU = new QShortcut(this);
    keyControlU->setKey(Qt::CTRL | Qt::Key_U);
    connect(keyControlU, SIGNAL(activated()), this, SLOT(shortcutControlU()));

    keyStar = new QShortcut(this);
    keyStar->setKey(Qt::Key_Asterisk);
    connect(keyStar, SIGNAL(activated()), this, SLOT(shortcutStar()));

    keySlash = new QShortcut(this);
    keySlash->setKey(Qt::Key_Slash);
    connect(keySlash, SIGNAL(activated()), this, SLOT(shortcutSlash()));

    keyMinus = new QShortcut(this);
    keyMinus->setKey(Qt::Key_Minus);
    connect(keyMinus, SIGNAL(activated()), this, SLOT(shortcutMinus()));

    keyPlus = new QShortcut(this);
    keyPlus->setKey(Qt::Key_Plus);
    connect(keyPlus, SIGNAL(activated()), this, SLOT(shortcutPlus()));

    keyShiftMinus = new QShortcut(this);
    keyShiftMinus->setKey(Qt::SHIFT | Qt::Key_Minus);
    connect(keyShiftMinus, SIGNAL(activated()), this, SLOT(shortcutShiftMinus()));

    keyShiftPlus = new QShortcut(this);
    keyShiftPlus->setKey(Qt::SHIFT | Qt::Key_Plus);
    connect(keyShiftPlus, SIGNAL(activated()), this, SLOT(shortcutShiftPlus()));

    keyControlMinus = new QShortcut(this);
    keyControlMinus->setKey(Qt::CTRL | Qt::Key_Minus);
    connect(keyControlMinus, SIGNAL(activated()), this, SLOT(shortcutControlMinus()));

    keyControlPlus = new QShortcut(this);
    keyControlPlus->setKey(Qt::CTRL | Qt::Key_Plus);
    connect(keyControlPlus, SIGNAL(activated()), this, SLOT(shortcutControlPlus()));

    keyQuit = new QShortcut(this);
    keyQuit->setKey(Qt::CTRL | Qt::Key_Q);
    connect(keyQuit, SIGNAL(activated()), this, SLOT(on_exitBtn_clicked()));

    keyPageUp = new QShortcut(this);
    keyPageUp->setKey(Qt::Key_PageUp);
    connect(keyPageUp, SIGNAL(activated()), this, SLOT(shortcutPageUp()));

    keyPageDown = new QShortcut(this);
    keyPageDown->setKey(Qt::Key_PageDown);
    connect(keyPageDown, SIGNAL(activated()), this, SLOT(shortcutPageDown()));

    keyF = new QShortcut(this);
    keyF->setKey(Qt::Key_F);
    connect(keyF, SIGNAL(activated()), this, SLOT(shortcutF()));

    keyM = new QShortcut(this);
    keyM->setKey(Qt::Key_M);
    connect(keyM, SIGNAL(activated()), this, SLOT(shortcutM()));

    // Alternate for plus:
    keyK = new QShortcut(this);
    keyK->setKey(Qt::Key_K);
    connect(keyK, &QShortcut::activated,
            [=]() {
        if (freqLock) return;
        this->shortcutPlus();
    });

    // Alternate for minus:
    keyJ = new QShortcut(this);
    keyJ->setKey(Qt::Key_J);
    connect(keyJ, &QShortcut::activated,
            [=]() {
        if (freqLock) return;
        this->shortcutMinus();
    });

    keyShiftK = new QShortcut(this);
    keyShiftK->setKey(Qt::SHIFT | Qt::Key_K);
    connect(keyShiftK, &QShortcut::activated,
            [=]() {
        if (freqLock) return;
        this->shortcutShiftPlus();
    });


    keyShiftJ = new QShortcut(this);
    keyShiftJ->setKey(Qt::SHIFT | Qt::Key_J);
    connect(keyShiftJ, &QShortcut::activated,
            [=]() {
        if (freqLock) return;
        this->shortcutShiftMinus();
    });

    keyControlK = new QShortcut(this);
    keyControlK->setKey(Qt::CTRL | Qt::Key_K);
    connect(keyControlK, &QShortcut::activated,
            [=]() {
        if (freqLock) return;
        this->shortcutControlPlus();
    });


    keyControlJ = new QShortcut(this);
    keyControlJ->setKey(Qt::CTRL | Qt::Key_J);
    connect(keyControlJ, &QShortcut::activated,
            [=]() {
        if (freqLock) return;
        this->shortcutControlMinus();
    });
    // Move the tuning knob by the tuning step selected:
    // H = Down
    keyH = new QShortcut(this);
    keyH->setKey(Qt::Key_H);
    connect(keyH, &QShortcut::activated,
            [=]() {
        if (freqLock) return;

        freqt f;
        f.Hz = roundFrequencyWithStep(ui->mainScope->getFrequency().Hz, -1, tsKnobHz);
        f.MHzDouble = f.Hz / (double)1E6;
        queue->add(priorityImmediate,queueItem(funcSelectedFreq,QVariant::fromValue<freqt>(f)));
    });

    // L = Up
    keyL = new QShortcut(this);
    keyL->setKey(Qt::Key_L);
    connect(keyL, &QShortcut::activated,
            [=]() {
        if (freqLock) return;

        freqt f;
        f.Hz = roundFrequencyWithStep(ui->mainScope->getFrequency().Hz, 1, tsKnobHz);
        f.MHzDouble = f.Hz / (double)1E6;
        queue->add(priorityImmediate,queueItem(funcSelectedFreq,QVariant::fromValue<freqt>(f)));
    });

    keyDebug = new QShortcut(this);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    keyDebug->setKey(Qt::CTRL | Qt::SHIFT | Qt::Key_D);
#else
    keyDebug->setKey(Qt::CTRL | Qt::Key_D);
#endif
    connect(keyDebug, SIGNAL(activated()), this, SLOT(debugBtn_clicked()));
}

void wfmain::setupUsbControllerDevice()
{
#if defined (USB_CONTROLLER)

    if (usbWindow == Q_NULLPTR) {
        usbWindow = new controllerSetup();
    }

    usbControllerDev = new usbController();
    usbControllerThread = new QThread(this);
    usbControllerThread->setObjectName("usb()");
    usbControllerDev->moveToThread(usbControllerThread);
    connect(usbControllerThread, SIGNAL(started()), usbControllerDev, SLOT(run()));
    connect(usbControllerThread, SIGNAL(finished()), usbControllerDev, SLOT(deleteLater()));
    connect(usbControllerThread, SIGNAL(finished()), usbWindow, SLOT(deleteLater())); // Delete window when controller is deleted
    connect(usbControllerDev, SIGNAL(sendJog(int)), this, SLOT(changeFrequency(int)));
    connect(usbControllerDev, SIGNAL(doShuttle(bool,unsigned char)), this, SLOT(doShuttle(bool,unsigned char)));
    connect(usbControllerDev, SIGNAL(button(const COMMAND*)), this, SLOT(buttonControl(const COMMAND*)));
    connect(usbControllerDev, SIGNAL(setBand(int)), this, SLOT(setBand(int)));
    connect(usbControllerDev, SIGNAL(removeDevice(USBDEVICE*)), usbWindow, SLOT(removeDevice(USBDEVICE*)));
    connect(usbControllerDev, SIGNAL(initUI(usbDevMap*, QVector<BUTTON>*, QVector<KNOB>*, QVector<COMMAND>*, QMutex*)), usbWindow, SLOT(init(usbDevMap*, QVector<BUTTON>*, QVector<KNOB>*, QVector<COMMAND>*, QMutex*)));
    connect(usbControllerDev, SIGNAL(changePage(USBDEVICE*,int)), usbWindow, SLOT(pageChanged(USBDEVICE*,int)));
    connect(usbControllerDev, SIGNAL(setConnected(USBDEVICE*)), usbWindow, SLOT(setConnected(USBDEVICE*)));
    connect(usbControllerDev, SIGNAL(newDevice(USBDEVICE*)), usbWindow, SLOT(newDevice(USBDEVICE*)));
    usbControllerThread->start(QThread::LowestPriority);

    connect(usbWindow, SIGNAL(sendRequest(USBDEVICE*, usbFeatureType, int, QString, QImage*, QColor *)), usbControllerDev, SLOT(sendRequest(USBDEVICE*, usbFeatureType, int, QString, QImage*, QColor *)));
    connect(this, SIGNAL(sendControllerRequest(USBDEVICE*, usbFeatureType, int, QString, QImage*, QColor *)), usbControllerDev, SLOT(sendRequest(USBDEVICE*, usbFeatureType, int, QString, QImage*, QColor *)));
    connect(usbWindow, SIGNAL(programPages(USBDEVICE*,int)), usbControllerDev, SLOT(programPages(USBDEVICE*,int)));
    connect(usbWindow, SIGNAL(programDisable(USBDEVICE*,bool)), usbControllerDev, SLOT(programDisable(USBDEVICE*,bool)));
    connect(this, SIGNAL(sendLevel(funcs,unsigned char)), usbControllerDev, SLOT(receiveLevel(funcs,unsigned char)));
    connect(this, SIGNAL(initUsbController(QMutex*,usbDevMap*,QVector<BUTTON>*,QVector<KNOB>*)), usbControllerDev, SLOT(init(QMutex*,usbDevMap*,QVector<BUTTON>*,QVector<KNOB>*)));
    connect(this, SIGNAL(usbHotplug()), usbControllerDev, SLOT(run()));
    connect(usbWindow, SIGNAL(backup(USBDEVICE*,QString)), usbControllerDev, SLOT(backupController(USBDEVICE*,QString)));
    connect(usbWindow, SIGNAL(restore(USBDEVICE*,QString)), usbControllerDev, SLOT(restoreController(USBDEVICE*,QString)));

#endif
}

void wfmain::pttToggle(bool status)
{
    // is it enabled?

    if (!prefs.enablePTT)
    {
        showStatusBarText("PTT is disabled, not sending command. Change under Settings tab.");
        return;
    }
    queue->add(priorityImmediate,queueItem(funcTransceiverStatus,QVariant::fromValue<bool>(status),false));

    // Start 3 minute timer
    if (status)
        pttTimer->start();

    queue->add(priorityImmediate,funcTransceiverStatus);
}

void wfmain::doShuttle(bool up, unsigned char level)
{
    if (level == 1 && up)
            shortcutShiftPlus();
    else if (level == 1 && !up)
            shortcutShiftMinus();
    else if (level > 1 && level < 5 && up)
        for (int i = 1; i < level; i++)
            shortcutPlus();
    else if (level > 1 && level < 5 && !up)
        for (int i = 1; i < level; i++)
            shortcutMinus();
    else if (level > 4 && up)
        for (int i = 4; i < level; i++)
            shortcutControlPlus();
    else if (level > 4 && !up)
        for (int i = 4; i < level; i++)
            shortcutControlMinus();
}

void wfmain::buttonControl(const COMMAND* cmd)
{
    qDebug(logUsbControl()) << QString("executing command: %0 (%1) suffix:%2 value:%3" ).arg(cmd->text).arg(cmd->command).arg(cmd->suffix).arg(cmd->value);
    switch (cmd->command) {
    case funcBandStackReg:
        if (cmd->value == 100) {
            for (size_t i = 0; i < rigCaps.bands.size(); i++) {
                if (rigCaps.bands[i].band == lastRequestedBand)
                {
                    if (i > 0) {
                        //issueCmd(cmdGetBandStackReg, rigCaps.bands[i - 1].band);
                    }
                    else {
                        //issueCmd(cmdGetBandStackReg, rigCaps.bands[rigCaps.bands.size() - 1].band);
                    }
                }
            }
        } else if (cmd->value == -100) {
            for (size_t i = 0; i < rigCaps.bands.size(); i++) {
                if (rigCaps.bands[i].band == lastRequestedBand)
                {
                    if (i + 1 < rigCaps.bands.size()) {
                        //issueCmd(cmdGetBandStackReg, rigCaps.bands[i + 1].band);
                    }
                    else {
                        //issueCmd(cmdGetBandStackReg, rigCaps.bands[0].band);
                    }
                }
            }
        } else {
            foreach (auto band, rigCaps.bands)
            {
                if (band.band == cmd->value)
                {
                    //issueCmd((cmds)cmd->command, cmd->band);
                }
            }
        }
        break;
    case funcModeSet:
        if (cmd->value == 100) {
            for (size_t i = 0; i < rigCaps.modes.size(); i++) {
                if (rigCaps.modes[i].mk == currentModeInfo.mk)
                {
                    if (i + 1 < rigCaps.modes.size()) {
                        changeMode(rigCaps.modes[i + 1].mk);
                    }
                    else {
                        changeMode(rigCaps.modes[0].mk);
                    }
                }
            }
        } else if (cmd->value == -100) {
            for (size_t i = 0; i < rigCaps.modes.size(); i++) {
                if (rigCaps.modes[i].mk == currentModeInfo.mk)
                {
                    if (i>0) {
                        changeMode(rigCaps.modes[i - 1].mk);
                    }
                    else {
                        changeMode(rigCaps.modes[rigCaps.modes.size()-1].mk);
                    }
                }
            }
        } else {
            changeMode(cmd->mode);
        }
        break;
    case funcTuningStep:
        if (cmd->value == 100) {
            if (ui->tuningStepCombo->currentIndex() < ui->tuningStepCombo->count()-1)
            {
                ui->tuningStepCombo->setCurrentIndex(ui->tuningStepCombo->currentIndex() + 1);
            }
            else
            {
                ui->tuningStepCombo->setCurrentIndex(0);
            }
        } else if (cmd->value == -100) {
            if (ui->tuningStepCombo->currentIndex() > 0)
            {
                ui->tuningStepCombo->setCurrentIndex(ui->tuningStepCombo->currentIndex() - 1);
            }
            else
            {
                ui->tuningStepCombo->setCurrentIndex(ui->tuningStepCombo->count() - 1);
            }
        } else {
            //Potentially add option to select specific step size?
        }
        break;
    case funcScopeMainSpan:
        if (cmd->value == 100) {
            //if (ui->scopeBWCombo->currentIndex() < ui->scopeBWCombo->count()-1)
            //{
            //    ui->scopeBWCombo->setCurrentIndex(ui->scopeBWCombo->currentIndex() + 1);
            //}
            //else
            //{
            //    ui->scopeBWCombo->setCurrentIndex(0);
            //}
        } else if (cmd->value == -100) {
            //if (ui->scopeBWCombo->currentIndex() > 0)
            //{
            //    ui->scopeBWCombo->setCurrentIndex(ui->scopeBWCombo->currentIndex() - 1);
            //}
            //else
            //{
            //    ui->scopeBWCombo->setCurrentIndex(ui->scopeBWCombo->count() - 1);
            //}
        } else {
            //Potentially add option to select specific step size?
        }
        break;
    case funcSelectedFreq:
    case funcUnselectedFreq:
    {
        freqt f;
        bool sub=false;
        if ((funcs)cmd->command == funcUnselectedFreq) {
            f.Hz = roundFrequencyWithStep(ui->subScope->getFrequency().Hz, cmd->value, tsWfScrollHz);
            sub=true;
        } else {
            f.Hz = roundFrequencyWithStep(ui->mainScope->getFrequency().Hz, cmd->value, tsWfScrollHz);
        }
        f.MHzDouble = f.Hz / (double)1E6;
        f.VFO=(selVFO_t)cmd->suffix;
        queue->add(priorityImmediate,queueItem((funcs)cmd->command,QVariant::fromValue<freqt>(f),sub));
        break;
    }
    default:
        qInfo(logUsbControl()) << "Command" << cmd->command << "Suffix" << cmd->suffix;
        queue->add(priorityImmediate,queueItem((funcs)cmd->command,QVariant::fromValue<uchar>(cmd->suffix),false));
        break;
    }

    // Make sure we get status quickly by sending a get command
    if (cmd->command != funcNone) {
        queue->add(priorityHigh,(funcs)cmd->command);
    }
}

void wfmain::stepUp()
{
    if (ui->tuningStepCombo->currentIndex() < ui->tuningStepCombo->count() - 1)
        ui->tuningStepCombo->setCurrentIndex(ui->tuningStepCombo->currentIndex() + 1);
}

void wfmain::stepDown()
{
    if (ui->tuningStepCombo->currentIndex() > 0)
        ui->tuningStepCombo->setCurrentIndex(ui->tuningStepCombo->currentIndex() - 1);
}

void wfmain::changeFrequency(int value) {
    if (freqLock) return;

    freqt f;
    f.Hz = roundFrequencyWithStep(ui->mainScope->getFrequency().Hz, value, tsWfScrollHz);
    f.MHzDouble = f.Hz / (double)1E6;
    queue->add(priorityImmediate,queueItem(funcSelectedFreq,QVariant::fromValue<freqt>(f),false));
}

void wfmain::setDefPrefs()
{
    defPrefs.useFullScreen = false;
    defPrefs.useSystemTheme = false;
    defPrefs.drawPeaks = true;
    defPrefs.currentColorPresetNumber = 0;
    defPrefs.underlayMode = underlayNone;
    defPrefs.underlayBufferSize = 64;
    defPrefs.wfEnable = 2;
    defPrefs.wfAntiAlias = false;
    defPrefs.wfInterpolate = true;
    defPrefs.stylesheetPath = QString("qdarkstyle/style.qss");
    defPrefs.radioCIVAddr = 0x00; // previously was 0x94 for 7300.
    defPrefs.CIVisRadioModel = false;
    defPrefs.forceRTSasPTT = false;
    defPrefs.serialPortRadio = QString("auto");
    defPrefs.serialPortBaud = 115200;
    defPrefs.polling_ms = 0; // 0 = Automatic
    defPrefs.enablePTT = true;
    defPrefs.niceTS = true;
    defPrefs.enableRigCtlD = false;
    defPrefs.rigCtlPort = 4533;
    defPrefs.virtualSerialPort = QString("none");
    defPrefs.localAFgain = 255;
    defPrefs.mainWflength = 160;
    defPrefs.mainWfTheme = static_cast<int>(QCPColorGradient::gpJet);
    defPrefs.mainPlotFloor = 0;
    defPrefs.mainPlotCeiling = 160;
    defPrefs.subWflength = 160;
    defPrefs.subWfTheme = static_cast<int>(QCPColorGradient::gpJet);
    defPrefs.subPlotFloor = 0;
    defPrefs.subPlotCeiling = 160;
    defPrefs.scopeScrollX = 120;
    defPrefs.scopeScrollY = 120;
    defPrefs.confirmExit = true;
    defPrefs.confirmPowerOff = true;
    defPrefs.meter2Type = meterNone;
    defPrefs.tcpPort = 0;
    defPrefs.tciPort = 50001;
    defPrefs.waterfallFormat = 0;
    defPrefs.audioSystem = qtAudio;
    defPrefs.enableUSBControllers = false;

    udpDefPrefs.ipAddress = QString("");
    udpDefPrefs.controlLANPort = 50001;
    udpDefPrefs.serialLANPort = 50002;
    udpDefPrefs.audioLANPort = 50003;
    udpDefPrefs.username = QString("");
    udpDefPrefs.password = QString("");
    udpDefPrefs.clientName = QHostInfo::localHostName();
}

void wfmain::loadSettings()
{
    qInfo(logSystem()) << "Loading settings from " << settings->fileName();
    setupui->acceptServerConfig(&serverConfig);

    QString currentVersionString = QString(WFVIEW_VERSION);
    float currentVersionFloat = currentVersionString.toFloat();

    settings->beginGroup("Program");
    QString priorVersionString = settings->value("version", "unset").toString();
    float priorVersionFloat = priorVersionString.toFloat();
    if (currentVersionString != priorVersionString)
    {
        qWarning(logSystem()) << "Settings previously saved under version " << priorVersionString << ", you should review your settings and press SaveSettings.";
    }
    if(priorVersionFloat > currentVersionFloat)
    {
        qWarning(logSystem()).noquote().nospace() << "It looks like the previous version of wfview (" << priorVersionString << ") was newer than this version (" << currentVersionString << ")";
    }
    prefs.version = priorVersionString;
    prefs.majorVersion = settings->value("majorVersion", defPrefs.majorVersion).toInt();
    prefs.minorVersion = settings->value("minorVersion", defPrefs.minorVersion).toInt();
    settings->endGroup();

    // UI: (full screen, dark theme, draw peaks, colors, etc)
    settings->beginGroup("Interface");
    prefs.useFullScreen = settings->value("UseFullScreen", defPrefs.useFullScreen).toBool();
    prefs.useSystemTheme = settings->value("UseSystemTheme", defPrefs.useSystemTheme).toBool();
    prefs.wfEnable = settings->value("WFEnable", defPrefs.wfEnable).toInt();
    //ui->scopeEnableWFBtn->setCheckState(Qt::CheckState(prefs.wfEnable));
    prefs.mainWfTheme = settings->value("MainWFTheme", defPrefs.mainWfTheme).toInt();
    prefs.subWfTheme = settings->value("SubWFTheme", defPrefs.subWfTheme).toInt();
    prefs.mainPlotFloor = settings->value("MainPlotFloor", defPrefs.mainPlotFloor).toInt();
    prefs.subPlotFloor = settings->value("SubPlotFloor", defPrefs.subPlotFloor).toInt();
    prefs.mainPlotCeiling = settings->value("MainPlotCeiling", defPrefs.mainPlotCeiling).toInt();
    prefs.subPlotCeiling = settings->value("SubPlotCeiling", defPrefs.subPlotCeiling).toInt();
    prefs.scopeScrollX = settings->value("scopeScrollX", defPrefs.scopeScrollX).toInt();
    prefs.scopeScrollY = settings->value("scopeScrollY", defPrefs.scopeScrollY).toInt();

    prefs.drawPeaks = settings->value("DrawPeaks", defPrefs.drawPeaks).toBool();
    prefs.underlayBufferSize = settings->value("underlayBufferSize", defPrefs.underlayBufferSize).toInt();
    prefs.underlayMode = static_cast<underlay_t>(settings->value("underlayMode", defPrefs.underlayMode).toInt());
    prefs.wfAntiAlias = settings->value("WFAntiAlias", defPrefs.wfAntiAlias).toBool();
    prefs.wfInterpolate = settings->value("WFInterpolate", defPrefs.wfInterpolate).toBool();
    prefs.mainWflength = (unsigned int)settings->value("MainWFLength", defPrefs.mainWflength).toInt();
    prefs.subWflength = (unsigned int)settings->value("SubWFLength", defPrefs.subWflength).toInt();
    prefs.stylesheetPath = settings->value("StylesheetPath", defPrefs.stylesheetPath).toString();
    //ui->splitter->restoreState(settings->value("splitter").toByteArray());

    restoreGeometry(settings->value("windowGeometry").toByteArray());
    restoreState(settings->value("windowState").toByteArray());
    setWindowState(Qt::WindowActive); // Works around QT bug to returns window+keyboard focus.
    prefs.confirmExit = settings->value("ConfirmExit", defPrefs.confirmExit).toBool();
    prefs.confirmPowerOff = settings->value("ConfirmPowerOff", defPrefs.confirmPowerOff).toBool();
    prefs.meter2Type = static_cast<meter_t>(settings->value("Meter2Type", defPrefs.meter2Type).toInt());
    prefs.clickDragTuningEnable = settings->value("ClickDragTuning", false).toBool();
    ui->mainScope->setClickDragTuning(prefs.clickDragTuningEnable);
    ui->subScope->setClickDragTuning(prefs.clickDragTuningEnable);

    prefs.rigCreatorEnable = settings->value("RigCreator",false).toBool();
    ui->rigCreatorBtn->setEnabled(prefs.rigCreatorEnable);

    prefs.frequencyUnits = settings->value("FrequencyUnits",3).toInt();

    settings->endGroup();

    // Load in the color presets. The default values are already loaded.

    settings->beginGroup("ColorPresets");
    prefs.currentColorPresetNumber = settings->value("currentColorPresetNumber", defPrefs.currentColorPresetNumber).toInt();
    if(prefs.currentColorPresetNumber > numColorPresetsTotal-1)
        prefs.currentColorPresetNumber = 0;

    int numPresetsInFile = settings->beginReadArray("ColorPreset");
    // We will use the number of presets that the working copy of wfview
    // supports, as we must never exceed the available number.
    if (numPresetsInFile > 0)
    {
        colorPrefsType* p;
        QString tempName;
        for (int pn = 0; pn < numColorPresetsTotal; pn++)
        {
            settings->setArrayIndex(pn);
            p = &(colorPreset[pn]);
            p->presetNum = settings->value("presetNum", p->presetNum).toInt();
            tempName = settings->value("presetName", *p->presetName).toString();
            if ((!tempName.isEmpty()) && tempName.length() < 11)
            {
                    p->presetName->clear();
                    p->presetName->append(tempName);
            }
            p->gridColor.setNamedColor(settings->value("gridColor", p->gridColor.name(QColor::HexArgb)).toString());
            p->axisColor.setNamedColor(settings->value("axisColor", p->axisColor.name(QColor::HexArgb)).toString());
            p->textColor.setNamedColor(settings->value("textColor", p->textColor.name(QColor::HexArgb)).toString());
            p->spectrumLine.setNamedColor(settings->value("spectrumLine", p->spectrumLine.name(QColor::HexArgb)).toString());
            p->spectrumFill.setNamedColor(settings->value("spectrumFill", p->spectrumFill.name(QColor::HexArgb)).toString());
            p->underlayLine.setNamedColor(settings->value("underlayLine", p->underlayLine.name(QColor::HexArgb)).toString());
            p->underlayFill.setNamedColor(settings->value("underlayFill", p->underlayFill.name(QColor::HexArgb)).toString());
            p->plotBackground.setNamedColor(settings->value("plotBackground", p->plotBackground.name(QColor::HexArgb)).toString());
            p->tuningLine.setNamedColor(settings->value("tuningLine", p->tuningLine.name(QColor::HexArgb)).toString());
            p->passband.setNamedColor(settings->value("passband", p->passband.name(QColor::HexArgb)).toString());
            p->pbt.setNamedColor(settings->value("pbt", p->pbt.name(QColor::HexArgb)).toString());
            p->wfBackground.setNamedColor(settings->value("wfBackground", p->wfBackground.name(QColor::HexArgb)).toString());
            p->wfGrid.setNamedColor(settings->value("wfGrid", p->wfGrid.name(QColor::HexArgb)).toString());
            p->wfAxis.setNamedColor(settings->value("wfAxis", p->wfAxis.name(QColor::HexArgb)).toString());
            p->wfText.setNamedColor(settings->value("wfText", p->wfText.name(QColor::HexArgb)).toString());
            p->meterLevel.setNamedColor(settings->value("meterLevel", p->meterLevel.name(QColor::HexArgb)).toString());
            p->meterAverage.setNamedColor(settings->value("meterAverage", p->meterAverage.name(QColor::HexArgb)).toString());
            p->meterPeakLevel.setNamedColor(settings->value("meterPeakLevel", p->meterPeakLevel.name(QColor::HexArgb)).toString());
            p->meterPeakScale.setNamedColor(settings->value("meterPeakScale", p->meterPeakScale.name(QColor::HexArgb)).toString());
            p->meterLowerLine.setNamedColor(settings->value("meterLowerLine", p->meterLowerLine.name(QColor::HexArgb)).toString());
            p->meterLowText.setNamedColor(settings->value("meterLowText", p->meterLowText.name(QColor::HexArgb)).toString());
            p->clusterSpots.setNamedColor(settings->value("clusterSpots", p->clusterSpots.name(QColor::HexArgb)).toString());
        }
    }
    settings->endArray();
    settings->endGroup();

    // Radio and Comms: C-IV addr, port to use
    settings->beginGroup("Radio");
    prefs.radioCIVAddr = (unsigned char)settings->value("RigCIVuInt", defPrefs.radioCIVAddr).toInt();
    prefs.CIVisRadioModel = (bool)settings->value("CIVisRadioModel", defPrefs.CIVisRadioModel).toBool();
    prefs.forceRTSasPTT = (bool)settings->value("ForceRTSasPTT", defPrefs.forceRTSasPTT).toBool();
    prefs.serialPortRadio = settings->value("SerialPortRadio", defPrefs.serialPortRadio).toString();
    prefs.serialPortBaud = (quint32)settings->value("SerialPortBaud", defPrefs.serialPortBaud).toInt();

    if (prefs.serialPortBaud > 0)
    {
        serverConfig.baudRate = prefs.serialPortBaud;
    }

    prefs.polling_ms = settings->value("polling_ms", defPrefs.polling_ms).toInt();
    // Migrated
    if(prefs.polling_ms == 0)
    {
        // Automatic

    } else {
        // Manual

    }

    prefs.virtualSerialPort = settings->value("VirtualSerialPort", defPrefs.virtualSerialPort).toString();


    prefs.localAFgain = (unsigned char)settings->value("localAFgain", defPrefs.localAFgain).toUInt();
    prefs.rxSetup.localAFgain = prefs.localAFgain;
    prefs.txSetup.localAFgain = 255;

    prefs.audioSystem = static_cast<audioType>(settings->value("AudioSystem", defPrefs.audioSystem).toInt());

    settings->endGroup();

    // Misc. user settings (enable PTT, draw peaks, etc)
    settings->beginGroup("Controls");
    prefs.enablePTT = settings->value("EnablePTT", defPrefs.enablePTT).toBool();

    prefs.niceTS = settings->value("NiceTS", defPrefs.niceTS).toBool();
    ui->mainScope->setTuningFloorZeros(prefs.niceTS);
    ui->subScope->setTuningFloorZeros(prefs.niceTS);

    prefs.automaticSidebandSwitching = settings->value("automaticSidebandSwitching", defPrefs.automaticSidebandSwitching).toBool();
    settings->endGroup();

    settings->beginGroup("LAN");

    prefs.enableLAN = settings->value("EnableLAN", defPrefs.enableLAN).toBool();

    // If LAN is enabled, server gets its audio straight from the LAN
    // migrated, remove these
    //ui->serverRXAudioInputCombo->setEnabled(!prefs.enableLAN);
    //ui->serverTXAudioOutputCombo->setEnabled(!prefs.enableLAN);

    // If LAN is not enabled, disable local audio input/output
    //ui->audioOutputCombo->setEnabled(prefs.enableLAN);
    //ui->audioInputCombo->setEnabled(prefs.enableLAN);

    ui->connectBtn->setEnabled(true);

    prefs.enableRigCtlD = settings->value("EnableRigCtlD", defPrefs.enableRigCtlD).toBool();
    prefs.rigCtlPort = settings->value("RigCtlPort", defPrefs.rigCtlPort).toInt();
    // Call the function to start rigctld if enabled.
    enableRigCtl(prefs.enableRigCtlD);

    prefs.tcpPort = settings->value("TCPServerPort", defPrefs.tcpPort).toInt();
    prefs.tciPort = settings->value("TCIServerPort", defPrefs.tciPort).toInt();

    prefs.waterfallFormat = settings->value("WaterfallFormat", defPrefs.waterfallFormat).toInt();

    udpPrefs.ipAddress = settings->value("IPAddress", udpDefPrefs.ipAddress).toString();
    udpPrefs.controlLANPort = settings->value("ControlLANPort", udpDefPrefs.controlLANPort).toInt();
    udpPrefs.username = settings->value("Username", udpDefPrefs.username).toString();
    udpPrefs.password = settings->value("Password", udpDefPrefs.password).toString();
    prefs.rxSetup.isinput = false;
    prefs.txSetup.isinput = true;
    prefs.rxSetup.latency = settings->value("AudioRXLatency", "150").toInt();
    prefs.txSetup.latency = settings->value("AudioTXLatency", "150").toInt();

    prefs.rxSetup.sampleRate=settings->value("AudioRXSampleRate", "48000").toInt();
    prefs.txSetup.sampleRate=prefs.rxSetup.sampleRate;

    prefs.rxSetup.codec = settings->value("AudioRXCodec", "4").toInt();
    prefs.txSetup.codec = settings->value("AudioTXCodec", "4").toInt();
    prefs.rxSetup.name = settings->value("AudioOutput", "Default Output Device").toString();
    qInfo(logGui()) << "Got Audio Output from Settings: " << prefs.rxSetup.name;

    prefs.txSetup.name = settings->value("AudioInput", "Default Input Device").toString();
    qInfo(logGui()) << "Got Audio Input from Settings: " << prefs.txSetup.name;

    prefs.rxSetup.resampleQuality = settings->value("ResampleQuality", "4").toInt();
    prefs.txSetup.resampleQuality = prefs.rxSetup.resampleQuality;

    if (prefs.tciPort > 0 && tci == Q_NULLPTR) {

        tci = new tciServer(this);

        tciThread = new QThread(this);
        tciThread->setObjectName("TCIServer()");
        tci->moveToThread(tciThread);
        connect(this,SIGNAL(tciInit(quint16)),tci, SLOT(init(quint16)));
        connect(tciThread, SIGNAL(finished()), tci, SLOT(deleteLater()));
        tciThread->start(QThread::TimeCriticalPriority);
        emit tciInit(prefs.tciPort);
    }

    if (prefs.audioSystem == tciAudio)
    {
        prefs.rxSetup.tci = tci;
        prefs.txSetup.tci = tci;
    }

    udpPrefs.clientName = settings->value("ClientName", udpDefPrefs.clientName).toString();

    udpPrefs.halfDuplex = settings->value("HalfDuplex", udpDefPrefs.halfDuplex).toBool();
    //ui->audioDuplexCombo->setVisible(false);
    //ui->label_51->setVisible(false);

    settings->endGroup();

    settings->beginGroup("Server");
    serverConfig.enabled = settings->value("ServerEnabled", false).toBool();
    serverConfig.controlPort = settings->value("ServerControlPort", 50001).toInt();
    serverConfig.civPort = settings->value("ServerCivPort", 50002).toInt();
    serverConfig.audioPort = settings->value("ServerAudioPort", 50003).toInt();

    setupui->updateServerConfigs((int)(s_enabled |
                                 s_controlPort |
                                 s_civPort |
                                 s_audioPort));

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
        /* Support old way of storing users*/
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

    RIGCONFIG* rigTemp = new RIGCONFIG();
    rigTemp->rxAudioSetup.isinput = true;
    rigTemp->txAudioSetup.isinput = false;
    rigTemp->rxAudioSetup.localAFgain = 255;
    rigTemp->txAudioSetup.localAFgain = 255;
    rigTemp->rxAudioSetup.resampleQuality = 4;
    rigTemp->txAudioSetup.resampleQuality = 4;
    rigTemp->rxAudioSetup.type = prefs.audioSystem;
    rigTemp->txAudioSetup.type = prefs.audioSystem;

    rigTemp->baudRate = prefs.serialPortBaud;
    rigTemp->civAddr = prefs.radioCIVAddr;
    rigTemp->serialPort = prefs.serialPortRadio;
    QString guid = settings->value("GUID", "").toString();
    if (guid.isEmpty()) {
        guid = QUuid::createUuid().toString();
        settings->setValue("GUID", guid);
    }
#if (QT_VERSION >= QT_VERSION_CHECK(5,10,0))
    memcpy(rigTemp->guid, QUuid::fromString(guid).toRfc4122().constData(), GUIDLEN);
#endif

    rigTemp->rxAudioSetup.name = settings->value("ServerAudioInput", "Default Input Device").toString();
    rigTemp->txAudioSetup.name = settings->value("ServerAudioOutput", "Default Output Device").toString();

    serverConfig.rigs.append(rigTemp);

    // At this point, the users list has exactly one empty user.
    setupui->updateServerConfig(s_users);


    settings->endGroup();

    // Memory channels

    settings->beginGroup("Memory");
    int size = settings->beginReadArray("Channel");
    int chan = 0;
    double freq;
    unsigned char mode;
    bool isSet;

    // Annoying: QSettings will write the array to the
    // preference file starting the array at 1 and ending at 100.
    // Thus, we are writing the channel number each time.
    // It is also annoying that they get written with their array
    // numbers in alphabetical order without zero padding.
    // Also annoying that the preference groups are not written in
    // the order they are specified here.

    for (int i = 0; i < size; i++)
    {
        settings->setArrayIndex(i);
        chan = settings->value("chan", 0).toInt();
        freq = settings->value("freq", 12.345).toDouble();
        mode = settings->value("mode", 0).toInt();
        isSet = settings->value("isSet", false).toBool();

        if (isSet)
        {
            mem.setPreset(chan, freq, (rigMode_t)mode);
        }
    }

    settings->endArray();
    settings->endGroup();

    settings->beginGroup("Cluster");

    prefs.clusterUdpEnable = settings->value("UdpEnabled", false).toBool();
    prefs.clusterTcpEnable = settings->value("TcpEnabled", false).toBool();
    prefs.clusterUdpPort = settings->value("UdpPort", 12060).toInt();

    int numClusters = settings->beginReadArray("Servers");
    prefs.clusters.clear();
    if (numClusters > 0) {
        {
            for (int f = 0; f < numClusters; f++)
            {
                settings->setArrayIndex(f);
                clusterSettings c;
                c.server = settings->value("ServerName", "").toString();
                c.port = settings->value("Port", 7300).toInt();
                c.userName = settings->value("UserName", "").toString();
                c.password = settings->value("Password", "").toString();
                c.timeout = settings->value("Timeout", 0).toInt();
                c.isdefault = settings->value("Default", false).toBool();
                if (!c.server.isEmpty()) {
                    prefs.clusters.append(c);
                }
            }
        }
    }
    settings->endArray();
    settings->endGroup();

    // CW Memory Load:
    settings->beginGroup("Keyer");
    cw->setCutNumbers(settings->value("CutNumbers", false).toBool());
    cw->setSendImmediate(settings->value("SendImmediate", false).toBool());
    cw->setSidetoneEnable(settings->value("SidetoneEnabled", true).toBool());
    cw->setSidetoneLevel(settings->value("SidetoneLevel", 100).toInt());
    int numMemories = settings->beginReadArray("macros");
    if(numMemories==10)
    {
        QStringList macroList;
        for(int m=0; m < 10; m++)
        {
            settings->setArrayIndex(m);
            macroList << settings->value("macroText", "").toString();
        }
        cw->setMacroText(macroList);
    }
    settings->endArray();
    settings->endGroup();

#if defined (USB_CONTROLLER)
    settings->beginGroup("USB");
    /* Load USB buttons*/
    prefs.enableUSBControllers = settings->value("EnableUSBControllers", defPrefs.enableUSBControllers).toBool();

    /*Ensure that no operations on the usb commands/buttons/knobs take place*/
    QMutexLocker locker(&usbMutex);

    int numControllers = settings->beginReadArray("Controllers");
    if (numControllers == 0) {
        settings->endArray();
    }
    else {
        usbDevices.clear();
        for (int nc = 0; nc < numControllers; nc++)
        {
            settings->setArrayIndex(nc);
            USBDEVICE tempPrefs;
            tempPrefs.path = settings->value("Path", "").toString();
            tempPrefs.disabled = settings->value("Disabled", false).toBool();
            tempPrefs.sensitivity = settings->value("Sensitivity", 1).toInt();
            tempPrefs.pages = settings->value("Pages", 1).toInt();
            tempPrefs.brightness = (quint8)settings->value("Brightness", 2).toInt();
            tempPrefs.orientation = (quint8)settings->value("Orientation", 2).toInt();
            tempPrefs.speed = (quint8)settings->value("Speed", 2).toInt();
            tempPrefs.timeout = (quint8)settings->value("Timeout", 30).toInt();
            tempPrefs.color.setNamedColor(settings->value("Color", QColor(Qt::white).name(QColor::HexArgb)).toString());
            tempPrefs.lcd = (funcs)settings->value("LCD",0).toInt();

            if (!tempPrefs.path.isEmpty()) {
                usbDevices.insert(tempPrefs.path,tempPrefs);
            }
        }
        settings->endArray();
    }

    int numButtons = settings->beginReadArray("Buttons");
    if (numButtons == 0) {
        settings->endArray();
    }
    else {
        usbButtons.clear();
        for (int nb = 0; nb < numButtons; nb++)
        {
            settings->setArrayIndex(nb);
            BUTTON butt;
            butt.path = settings->value("Path", "").toString();
            butt.page = settings->value("Page", 1).toInt();
            auto it = usbDevices.find(butt.path);
            if (it==usbDevices.end())
            {
                qWarning(logUsbControl) << "Cannot find existing device while creating button, aborting!";
                continue;
            }
            butt.parent = &it.value();
            butt.dev = (usbDeviceType)settings->value("Dev", 0).toInt();
            butt.num = settings->value("Num", 0).toInt();
            butt.name = settings->value("Name", "").toString();
            butt.pos = QRect(settings->value("Left", 0).toInt(),
                settings->value("Top", 0).toInt(),
                settings->value("Width", 0).toInt(),
                settings->value("Height", 0).toInt());
            butt.textColour.setNamedColor(settings->value("Colour", QColor(Qt::white).name(QColor::HexArgb)).toString());
            butt.backgroundOn.setNamedColor(settings->value("BackgroundOn", QColor(Qt::lightGray).name(QColor::HexArgb)).toString());
            butt.backgroundOff.setNamedColor(settings->value("BackgroundOff", QColor(Qt::blue).name(QColor::HexArgb)).toString());
            butt.toggle = settings->value("Toggle", false).toBool();
            // PET add Linux as it stops Qt6 building FIXME
#if (QT_VERSION > QT_VERSION_CHECK(6,0,0) && !defined(Q_OS_LINUX))
            if (settings->value("Icon",NULL) != NULL) {
                butt.icon = new QImage(settings->value("Icon",NULL).value<QImage>());
                butt.iconName = settings->value("IconName", "").toString();
            }
#endif
            butt.on = settings->value("OnCommand", "None").toString();
            butt.off = settings->value("OffCommand", "None").toString();
            butt.graphics = settings->value("Graphics", false).toBool();
            butt.led = settings->value("Led", 0).toInt();
            if (!butt.path.isEmpty())
                usbButtons.append(butt);
        }
        settings->endArray();
    }

    int numKnobs = settings->beginReadArray("Knobs");
    if (numKnobs == 0) {
        settings->endArray();
    }
    else {
        usbKnobs.clear();
        for (int nk = 0; nk < numKnobs; nk++)
        {
            settings->setArrayIndex(nk);
            KNOB kb;
            kb.path = settings->value("Path", "").toString();
            auto it = usbDevices.find(kb.path);
            if (it==usbDevices.end())
            {
                qWarning(logUsbControl) << "Cannot find existing device while creating knob, aborting!";
                continue;
            }
            kb.parent = &it.value();
            kb.page = settings->value("Page", 1).toInt();
            kb.dev = (usbDeviceType)settings->value("Dev", 0).toInt();
            kb.num = settings->value("Num", 0).toInt();
            kb.name = settings->value("Name", "").toString();
            kb.pos = QRect(settings->value("Left", 0).toInt(),
                settings->value("Top", 0).toInt(),
                settings->value("Width", 0).toInt(),
                settings->value("Height", 0).toInt());
            kb.textColour = QColor((settings->value("Colour", "Green").toString()));

            kb.cmd = settings->value("Command", "None").toString();
            if (!kb.path.isEmpty())
                usbKnobs.append(kb);
        }
        settings->endArray();
    }

    settings->endGroup();

    if (prefs.enableUSBControllers) {
        // Setup USB Controller
        setupUsbControllerDevice();
        emit initUsbController(&usbMutex,&usbDevices,&usbButtons,&usbKnobs);
    }


#endif

    setupui->acceptPreferencesPtr(&prefs);
    setupui->acceptColorPresetPtr(colorPreset);
    setupui->updateIfPrefs((int)if_all);
    setupui->updateColPrefs((int)col_all);
    setupui->updateRaPrefs((int)ra_all);
    setupui->updateRsPrefs((int)rs_all); // Most of these come from the rig anyway
    setupui->updateCtPrefs((int)ct_all);
    setupui->updateClusterPrefs((int)cl_all);
    setupui->updateLanPrefs((int)l_all);

    setupui->acceptUdpPreferencesPtr(&udpPrefs);
    setupui->updateUdpPrefs((int)u_all);

    prefs.settingsChanged = false;
}

void wfmain::extChangedIfPrefs(quint64 items)
{
    prefIfItem pif;
    if(items & (int)if_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)if_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logSystem()) << "Updating If pref in wfmain" << (int)i;
            pif = (prefIfItem)i;
            extChangedIfPref(pif);
        }
    }
}

void wfmain::extChangedColPrefs(quint64 items)
{
    prefColItem col;
    if(items & (int)col_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)col_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logSystem()) << "Updating Color pref in wfmain" << (int)i;
            col = (prefColItem)i;
            extChangedColPref(col);
        }
    }
}

void wfmain::extChangedRaPrefs(quint64 items)
{
    prefRaItem pra;
    if(items & (int)ra_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)ra_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logSystem()) << "Updating Ra pref in wfmain" << (int)i;
            pra = (prefRaItem)i;
            extChangedRaPref(pra);
        }
    }
}

void wfmain::extChangedRsPrefs(quint64 items)
{
    prefRsItem prs;
    if(items & (int)rs_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)rs_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logSystem()) << "Updating Rs pref in wfmain" << (int)i;
            prs = (prefRsItem)i;
            extChangedRsPref(prs);
        }
    }
}

void wfmain::extChangedCtPrefs(quint64 items)
{
    prefCtItem pct;
    if(items & (int)ct_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)ct_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logSystem()) << "Updating Ct pref in wfmain" << (int)i;
            pct = (prefCtItem)i;
            extChangedCtPref(pct);
        }
    }
}

void wfmain::extChangedLanPrefs(quint64 items)
{
    prefLanItem plan;
    if(items & (int)l_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)l_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logSystem()) << "Updating Lan pref in wfmain" << (int)i;
            plan = (prefLanItem)i;
            extChangedLanPref(plan);
        }
    }
}

void wfmain::extChangedClusterPrefs(quint64 items)
{
    prefClusterItem pcl;
    if(items & (int)cl_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)cl_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logSystem()) << "Updating Cluster pref in wfmain" << (int)i;
            pcl = (prefClusterItem)i;
            extChangedClusterPref(pcl);
        }
    }
}

void wfmain::extChangedUdpPrefs(quint64 items)
{
    prefUDPItem upi;
    if(items & (int)u_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)u_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logSystem()) << "Updating UDP preference in wfmain:" << i;
            upi = (prefUDPItem)i;
            extChangedUdpPref(upi);
        }
    }
}


void wfmain::extChangedServerPrefs(quint64 items)
{
    prefServerItem svi;
    if(items & (int)u_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)u_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logSystem()) << "Updating Server preference in wfmain:" << i;
            svi = (prefServerItem)i;
            extChangedServerPref(svi);
        }
    }
}

void wfmain::extChangedIfPref(prefIfItem i)
{
    prefs.settingsChanged = true;
    switch(i)
    {
    case if_useFullScreen:
        changeFullScreenMode(prefs.useFullScreen);
        break;
    case if_useSystemTheme:
        useSystemTheme(prefs.useSystemTheme);
        break;
    case if_drawPeaks:
        // depreciated;
        break;
    case if_underlayMode:
        ui->mainScope->setUnderlayMode(prefs.underlayMode);
        ui->subScope->setUnderlayMode(prefs.underlayMode);
        break;
    case if_underlayBufferSize:
        ui->mainScope->resizePlasmaBuffer(prefs.underlayBufferSize);
        ui->subScope->resizePlasmaBuffer(prefs.underlayBufferSize);
        break;
    case if_wfAntiAlias:
        ui->mainScope->wfAntiAliased(prefs.wfAntiAlias);
        ui->subScope->wfAntiAliased(prefs.wfAntiAlias);
        break;
    case if_wfInterpolate:
        ui->mainScope->wfInterpolate(prefs.wfInterpolate);
        ui->subScope->wfInterpolate(prefs.wfInterpolate);
        break;
    case if_wftheme:
        // Not in settings widget
        break;
    case if_plotFloor:
        // Not in settings widget
        break;
    case if_plotCeiling:
        // Not in settings widget
        break;
    case if_stylesheetPath:
        // Not in settings widget
        break;
    case if_wflength:
        // Not in settings widget
        break;
    case if_confirmExit:
        // Not in settings widget
        break;
    case if_confirmPowerOff:
        // Not in settings widget
        break;
    case if_meter2Type:
        changeMeter2Type(prefs.meter2Type);
        break;
    case if_clickDragTuningEnable:
        // There's nothing to do here since the code
        // already uses the preference variable as state.
        break;
    case if_rigCreatorEnable:
        ui->rigCreatorBtn->setEnabled(prefs.rigCreatorEnable);
        break;
    case if_currentColorPresetNumber:
    {
        colorPrefsType p = colorPreset[prefs.currentColorPresetNumber];
        useColorPreset(&p);
        // TODO.....
        break;
    }
    case if_frequencyUnits:
        ui->mainScope->setUnit((FctlUnit)prefs.frequencyUnits);
        ui->subScope->setUnit((FctlUnit)prefs.frequencyUnits);
        break;
    default:
        qWarning(logSystem()) << "Did not understand if pref update in wfmain for item " << (int)i;
        break;
    }
}

void wfmain::extChangedColPref(prefColItem i)
{
    prefs.settingsChanged = true;
    colorPrefsType* cp = &colorPreset[prefs.currentColorPresetNumber];

    // These are all updated by the widget itself
    switch(i)
    {
    case col_grid:
    case col_axis:
    case col_text:
    case col_plotBackground:
    case col_spectrumLine:
    case col_spectrumFill:
    case col_underlayLine:
    case col_underlayFill:
    case col_tuningLine:
    case col_passband:
    case col_pbtIndicator:
    case col_waterfallBack:
    case col_waterfallGrid:
    case col_waterfallAxis:
    case col_waterfallText:
    case col_clusterSpots:
        ui->mainScope->colorPreset(cp);
        ui->subScope->colorPreset(cp);
        break;
    case col_meterLevel:
    case col_meterAverage:
    case col_meterPeakLevel:
    case col_meterHighScale:
    case col_meterScale:
    case col_meterText:
        ui->meterSPoWidget->setColors(cp->meterLevel, cp->meterPeakScale, cp->meterPeakLevel, cp->meterAverage, cp->meterLowerLine, cp->meterLowText);
        ui->meter2Widget->setColors(cp->meterLevel, cp->meterPeakScale, cp->meterPeakLevel, cp->meterAverage, cp->meterLowerLine, cp->meterLowText);
        break;
    default:
        qWarning(logSystem()) << "Cannot update wfmain col pref" << (int)i;
    }
}

void wfmain::extChangedRaPref(prefRaItem i)
{
    prefs.settingsChanged = true;
    // Radio Access Prefs
    switch(i)
    {
    case ra_radioCIVAddr:
        if(prefs.radioCIVAddr == 0) {
            showStatusBarText("Setting radio CI-V address to: 'auto'. Make sure CI-V Transceive is enabled on the radio.");
        } else {
            showStatusBarText(QString("Setting radio CI-V address to: 0x%1. Press Save Settings to retain.").arg(prefs.radioCIVAddr, 2, 16));
        }
        break;
    case ra_CIVisRadioModel:
        break;
    case ra_forceRTSasPTT:
        emit setRTSforPTT(prefs.forceRTSasPTT);
        break;
    case ra_polling_ms:
        if(prefs.polling_ms == 0)
        {
            // Automatic
            qInfo(logSystem()) << "User set radio polling interval to automatic.";
            calculateTimingParameters();
        } else {
            // Manual
            changePollTiming(prefs.polling_ms);
        }
        break;
    case ra_serialPortRadio:
    {
        prefs.serialPortRadio = prefs.serialPortRadio;
        showStatusBarText(QString("Changed serial port to %1. Press Save Settings to retain.").arg(prefs.serialPortRadio));
        break;
    }
    case ra_serialPortBaud:
        prefs.serialPortBaud = prefs.serialPortBaud;
        serverConfig.baudRate = prefs.serialPortBaud;
        showStatusBarText(QString("Changed baud rate to %1 bps. Press Save Settings to retain.").arg(prefs.serialPortBaud));
        break;
    case ra_virtualSerialPort:
        break;
    case ra_localAFgain:
        // Not handled here.
        break;
    case ra_audioSystem:
        // Not handled here
        break;
    default:
        qWarning(logSystem()) << "Cannot update wfmain ra pref" << (int)i;
    }
}

void wfmain::extChangedRsPref(prefRsItem i)
{
    prefs.settingsChanged = true;
    // Radio Settings prefs
    switch(i)
    {
    case rs_dataOffMod:
        break;
    case rs_data1Mod:
        break;
    case rs_data2Mod:
        break;
    case rs_data3Mod:
        break;
    case rs_setClock:
        setRadioTimeDatePrep();
    break;
    case rs_pttOn:
        if(!prefs.enablePTT)
        {
            showStatusBarText("PTT is disabled, not sending command. Change under Settings tab.");
            return;
        }
        showStatusBarText("Sending PTT ON command. Use Control-R to receive.");
        queue->add(priorityImmediate,queueItem(funcTransceiverStatus,QVariant::fromValue<bool>(true),false));
        pttTimer->start();
        break;
    case rs_pttOff:
        showStatusBarText("Sending PTT OFF command");
        queue->add(priorityImmediate,queueItem(funcTransceiverStatus,QVariant::fromValue<bool>(false),false));
        pttTimer->stop();
        break;
    case rs_satOps:
        sat->show();
        break;
    case rs_adjRef:
        cal->show();
        break;
    case rs_clockUseUtc:
        break;
    default:
        qWarning(logSystem()) << "Cannot update wfmain rs pref" << (int)i;
    }
}


void wfmain::extChangedCtPref(prefCtItem i)
{
    prefs.settingsChanged = true;
    switch(i)
    {
    case ct_enablePTT:
        break;
    case ct_niceTS:
        ui->mainScope->setTuningFloorZeros(prefs.niceTS);
        ui->subScope->setTuningFloorZeros(prefs.niceTS);
        break;
    case ct_automaticSidebandSwitching:
        finputbtns->setAutomaticSidebandSwitching(prefs.automaticSidebandSwitching);
        break;
    case ct_enableUSBControllers:
#if defined (USB_CONTROLLER)
        if (usbControllerThread != Q_NULLPTR) {
            usbControllerThread->quit();
            usbControllerThread->wait();
            usbControllerThread = Q_NULLPTR;
            usbWindow = Q_NULLPTR;
        }

        if (prefs.enableUSBControllers) {
            // Setup USB Controller
            setupUsbControllerDevice();
            emit initUsbController(&usbMutex,&usbDevices,&usbButtons,&usbKnobs);
        }
#endif
        break;
    case ct_USBControllersSetup:
        if (usbWindow != Q_NULLPTR) {
            if (usbWindow->isVisible()) {
                usbWindow->hide();
            }
            else {
                qInfo(logUsbControl()) << "Showing USB Controller window";
                usbWindow->show();
                usbWindow->raise();
            }
        }
        break;
    case ct_USBControllersReset:
    {
        int ret = QMessageBox::warning(this, tr("wfview"),
                                       tr("Are you sure you wish to reset the USB controllers?"),
                                       QMessageBox::Ok | QMessageBox::Cancel,
                                       QMessageBox::Cancel);
        if (ret == QMessageBox::Ok) {
            qInfo(logUsbControl()) << "Resetting USB controllers to default values";

            usbButtons.clear();
            usbKnobs.clear();
            usbDevices.clear();
            if (prefs.enableUSBControllers)
                extChangedCtPref(ct_enableUSBControllers);
        }
        break;
    }
    default:
        qWarning(logGui()) << "No UI element matches setting" << (int)i;
        break;
    }
}

void wfmain::extChangedLanPref(prefLanItem i)
{
    prefs.settingsChanged = true;
    switch(i)
    {
    case l_enableLAN:
        ui->connectBtn->setEnabled(true); // always set, not sure why.
        break;
    case l_enableRigCtlD:
        enableRigCtl(prefs.enableRigCtlD);
        break;
    case l_rigCtlPort:
        // no action
        break;
    case l_tcpPort:
        // no action
        break;
    case l_waterfallFormat:
        // no action
        break;
    default:
        qWarning(logSystem()) << "Did not find matching preference in wfmain for LAN ui update:" << (int)i;
    }
}

void wfmain::extChangedClusterPref(prefClusterItem i)
{
    prefs.settingsChanged = true;
    switch(i)
    {
    case cl_clusterUdpEnable:
        emit setClusterEnableUdp(prefs.clusterUdpEnable);
        break;
    case cl_clusterTcpEnable:
        emit setClusterEnableTcp(prefs.clusterTcpEnable);
        break;
    case cl_clusterUdpPort:
        emit setClusterUdpPort(prefs.clusterUdpPort);
        break;
    case cl_clusterTcpServerName:
        emit setClusterServerName(prefs.clusterTcpServerName);
        break;
    case cl_clusterTcpUserName:
        emit setClusterUserName(prefs.clusterTcpUserName);
        break;
    case cl_clusterTcpPassword:
        emit setClusterPassword(prefs.clusterTcpPassword);
        break;
    case cl_clusterTcpPort:
        emit setClusterTcpPort(prefs.clusterTcpPort);
        break;
    case cl_clusterTimeout:
        // Used?
        emit setClusterTimeout(prefs.clusterTimeout);
        break;
    case cl_clusterSkimmerSpotsEnable:
        emit setClusterSkimmerSpots(prefs.clusterSkimmerSpotsEnable);
        break;
    case cl_clusterTcpConnect:
        emit setClusterEnableTcp(false);
        emit setClusterServerName(prefs.clusterTcpServerName);
        emit setClusterUserName(prefs.clusterTcpUserName);
        emit setClusterPassword(prefs.clusterTcpPassword);
        emit setClusterTcpPort(prefs.clusterTcpPort);
        emit setClusterTimeout(prefs.clusterTimeout);
        emit setClusterSkimmerSpots(prefs.clusterSkimmerSpotsEnable);
        emit setClusterEnableTcp(true);
        break;
    case cl_clusterTcpDisconnect:
        emit setClusterEnableTcp(false);
        break;
    default:
        qWarning(logSystem()) << "Did not find matching preference element in wfmain for cluster preference " << (int)i;
        break;
    }
}

void wfmain::extChangedUdpPref(prefUDPItem i)
{
    prefs.settingsChanged = true;
    switch(i)
    {
    case u_ipAddress:
        break;
    case u_controlLANPort:
        break;
    case u_serialLANPort:
        // Not used in the UI.
        break;
    case u_audioLANPort:
        // Not used in the UI.
        break;
    case u_username:
        break;
    case u_password:
        break;
    case u_clientName:
        // Not used in the UI.
        break;
    case u_waterfallFormat:
        // Not used in the UI.
        break;
    case u_halfDuplex:
        break;
    case u_sampleRate:
        break;
    case u_rxCodec:
        break;
    case u_txCodec:
        break;
    case u_rxLatency:
        // Should be updated immediately
        emit sendChangeLatency(prefs.rxSetup.latency);
        break;
    case u_txLatency:
        break;
    case u_audioInput:
        break;
    case u_audioOutput:
        break;
    default:
        qWarning(logGui()) << "Did not find matching pref element in wfmain for UDP pref item " << (int)i;
        break;
    }
}


void wfmain::extChangedServerPref(prefServerItem i)
{
    prefs.settingsChanged = true;
    switch(i)
    {
    case s_enabled:
        setServerToPrefs();
        break;
    case s_lan:
        break;
    case s_controlPort:
        break;
    case s_civPort:
        break;
    case s_audioPort:
        break;
    case s_audioOutput:
        break;
    case s_audioInput:
        break;
    case s_resampleQuality:
        break;
    case s_baudRate:
        break;
    case s_users:
        break;
    case s_rigs:
        break;
    default:
        qWarning(logGui()) << "Did not find matching pref element in wfmain for Server pref item " << (int)i;
        break;
    }
}

void wfmain::changedModInput(uchar val, inputTypes type)
{

    rigInput r=currentModDataOffSrc;

    if (val == 1)
        r = currentModData1Src;
    else if (val == 2)
        r = currentModData2Src;
    else if (val == 3)
        r = currentModData3Src;

    queue->del(getInputTypeCommand(r.type));

    qInfo(logSystem()) << "Changing Mod Input" << val << "from" << r.name;

    funcs func=funcNone;

    switch (val)
    {
    case 0:
        func = funcDATAOffMod;
        break;
    case 1:
        func = funcDATA1Mod;
        break;
    case 2:
        func = funcDATA2Mod;
        break;
    case 3:
        func = funcDATA3Mod;
        break;
    default:
        break;
    }

    foreach(auto inp, rigCaps.inputs)
    {
        if (inp.type == type)
        {
            queue->add(priorityImmediate,queueItem(func,QVariant::fromValue<rigInput>(inp)));
            if(usingDataMode==val)
            {
                changeModLabel(inp);
            }
            break;
        }
    }
}


void wfmain::saveSettings()
{
    qInfo(logSystem()) << "Saving settings to " << settings->fileName();
    // Basic things to load:

    prefs.settingsChanged = false;

    QString versionstr = QString(WFVIEW_VERSION);
    QString majorVersion = "-1";
    QString minorVersion = "-1";
    if(versionstr.contains(".") && (versionstr.split(".").length() == 2))
    {
        majorVersion = versionstr.split(".").at(0);
        minorVersion = versionstr.split(".").at(1);
    }

    settings->beginGroup("Program");
    settings->setValue("version", versionstr);
    settings->setValue("majorVersion", int(majorVersion.toInt()));
    settings->setValue("minorVersion", int(minorVersion.toInt()));
    settings->setValue("gitShort", QString(GITSHORT));
    settings->endGroup();

    // UI: (full screen, dark theme, draw peaks, colors, etc)
    settings->beginGroup("Interface");
    settings->setValue("UseFullScreen", prefs.useFullScreen);
    settings->setValue("UseSystemTheme", prefs.useSystemTheme);
    settings->setValue("WFEnable", prefs.wfEnable);
    settings->setValue("DrawPeaks", prefs.drawPeaks);
    settings->setValue("underlayMode", prefs.underlayMode);
    settings->setValue("underlayBufferSize", prefs.underlayBufferSize);
    settings->setValue("WFAntiAlias", prefs.wfAntiAlias);
    settings->setValue("WFInterpolate", prefs.wfInterpolate);
    settings->setValue("MainWFTheme", prefs.mainWfTheme);
    settings->setValue("SubWFTheme", prefs.subWfTheme);
    settings->setValue("MainPlotFloor", prefs.mainPlotFloor);
    settings->setValue("SubPlotFloor", prefs.subPlotFloor);
    settings->setValue("MainPlotCeiling", prefs.mainPlotCeiling);
    settings->setValue("SubPlotCeiling", prefs.subPlotCeiling);
    settings->setValue("StylesheetPath", prefs.stylesheetPath);
    //settings->setValue("splitter", ui->splitter->saveState());
    settings->setValue("windowGeometry", saveGeometry());
    settings->setValue("windowState", saveState());
    settings->setValue("MainWFLength", prefs.mainWflength);
    settings->setValue("SubWFLength", prefs.subWflength);
    settings->setValue("ConfirmExit", prefs.confirmExit);
    settings->setValue("ConfirmPowerOff", prefs.confirmPowerOff);
    settings->setValue("Meter2Type", (int)prefs.meter2Type);
    settings->setValue("ClickDragTuning", prefs.clickDragTuningEnable);
    settings->setValue("RigCreator",prefs.rigCreatorEnable);
    settings->setValue("FrequencyUnits",prefs.frequencyUnits);

    settings->endGroup();

    // Radio and Comms: C-IV addr, port to use
    settings->beginGroup("Radio");
    settings->setValue("RigCIVuInt", prefs.radioCIVAddr);
    settings->setValue("CIVisRadioModel", prefs.CIVisRadioModel);
    settings->setValue("ForceRTSasPTT", prefs.forceRTSasPTT);
    settings->setValue("polling_ms", prefs.polling_ms); // 0 = automatic
    settings->setValue("SerialPortRadio", prefs.serialPortRadio);
    settings->setValue("SerialPortBaud", prefs.serialPortBaud);
    settings->setValue("VirtualSerialPort", prefs.virtualSerialPort);
    settings->setValue("localAFgain", prefs.localAFgain);
    settings->setValue("AudioSystem", prefs.audioSystem);

    settings->endGroup();

    // Misc. user settings (enable PTT, draw peaks, etc)
    settings->beginGroup("Controls");
    settings->setValue("EnablePTT", prefs.enablePTT);
    settings->setValue("NiceTS", prefs.niceTS);
    settings->setValue("automaticSidebandSwitching", prefs.automaticSidebandSwitching);
    settings->endGroup();

    settings->beginGroup("LAN");
    settings->setValue("EnableLAN", prefs.enableLAN);
    settings->setValue("EnableRigCtlD", prefs.enableRigCtlD);
    settings->setValue("TcpServerPort", prefs.tcpPort);
    settings->setValue("RigCtlPort", prefs.rigCtlPort);
    settings->setValue("TCPServerPort", prefs.tcpPort);
    settings->setValue("TCIServerPort", prefs.tciPort);
    settings->setValue("IPAddress", udpPrefs.ipAddress);
    settings->setValue("ControlLANPort", udpPrefs.controlLANPort);
    settings->setValue("SerialLANPort", udpPrefs.serialLANPort);
    settings->setValue("AudioLANPort", udpPrefs.audioLANPort);
    settings->setValue("Username", udpPrefs.username);
    settings->setValue("Password", udpPrefs.password);
    settings->setValue("AudioRXLatency", prefs.rxSetup.latency);
    settings->setValue("AudioTXLatency", prefs.txSetup.latency);
    settings->setValue("AudioRXSampleRate", prefs.rxSetup.sampleRate);
    settings->setValue("AudioRXCodec", prefs.rxSetup.codec);
    settings->setValue("AudioTXSampleRate", prefs.txSetup.sampleRate);
    settings->setValue("AudioTXCodec", prefs.txSetup.codec);
    settings->setValue("AudioOutput", prefs.rxSetup.name);
    settings->setValue("AudioInput", prefs.txSetup.name);
    settings->setValue("ResampleQuality", prefs.rxSetup.resampleQuality);
    settings->setValue("ClientName", udpPrefs.clientName);
    settings->setValue("WaterfallFormat", prefs.waterfallFormat);
    settings->setValue("HalfDuplex", udpPrefs.halfDuplex);

    settings->endGroup();

    // Memory channels
    settings->beginGroup("Memory");
    settings->beginWriteArray("Channel", (int)mem.getNumPresets());

    preset_kind temp;
    for (int i = 0; i < (int)mem.getNumPresets(); i++)
    {
        temp = mem.getPreset((int)i);
        settings->setArrayIndex(i);
        settings->setValue("chan", i);
        settings->setValue("freq", temp.frequency);
        settings->setValue("mode", temp.mode);
        settings->setValue("isSet", temp.isSet);
    }

    settings->endArray();
    settings->endGroup();

    // Color presets:
    settings->beginGroup("ColorPresets");
    settings->setValue("currentColorPresetNumber", prefs.currentColorPresetNumber);
    settings->beginWriteArray("ColorPreset", numColorPresetsTotal);
    colorPrefsType *p;
    for(int pn=0; pn < numColorPresetsTotal; pn++)
    {
        p = &(colorPreset[pn]);
        settings->setArrayIndex(pn);
        settings->setValue("presetNum", p->presetNum);
        settings->setValue("presetName", *(p->presetName));
        settings->setValue("gridColor", p->gridColor.name(QColor::HexArgb));
        settings->setValue("axisColor", p->axisColor.name(QColor::HexArgb));
        settings->setValue("textColor", p->textColor.name(QColor::HexArgb));
        settings->setValue("spectrumLine", p->spectrumLine.name(QColor::HexArgb));
        settings->setValue("spectrumFill", p->spectrumFill.name(QColor::HexArgb));
        settings->setValue("underlayLine", p->underlayLine.name(QColor::HexArgb));
        settings->setValue("underlayFill", p->underlayFill.name(QColor::HexArgb));
        settings->setValue("plotBackground", p->plotBackground.name(QColor::HexArgb));
        settings->setValue("tuningLine", p->tuningLine.name(QColor::HexArgb));
        settings->setValue("passband", p->passband.name(QColor::HexArgb));
        settings->setValue("pbt", p->pbt.name(QColor::HexArgb));
        settings->setValue("wfBackground", p->wfBackground.name(QColor::HexArgb));
        settings->setValue("wfGrid", p->wfGrid.name(QColor::HexArgb));
        settings->setValue("wfAxis", p->wfAxis.name(QColor::HexArgb));
        settings->setValue("wfText", p->wfText.name(QColor::HexArgb));
        settings->setValue("meterLevel", p->meterLevel.name(QColor::HexArgb));
        settings->setValue("meterAverage", p->meterAverage.name(QColor::HexArgb));
        settings->setValue("meterPeakScale", p->meterPeakScale.name(QColor::HexArgb));
        settings->setValue("meterPeakLevel", p->meterPeakLevel.name(QColor::HexArgb));
        settings->setValue("meterLowerLine", p->meterLowerLine.name(QColor::HexArgb));
        settings->setValue("meterLowText", p->meterLowText.name(QColor::HexArgb));
        settings->setValue("clusterSpots", p->clusterSpots.name(QColor::HexArgb));
    }
    settings->endArray();
    settings->endGroup();

    settings->beginGroup("Server");

    settings->setValue("ServerEnabled", serverConfig.enabled);
    settings->setValue("ServerControlPort", serverConfig.controlPort);
    settings->setValue("ServerCivPort", serverConfig.civPort);
    settings->setValue("ServerAudioPort", serverConfig.audioPort);
    settings->setValue("ServerAudioOutput", serverConfig.rigs.first()->txAudioSetup.name);
    settings->setValue("ServerAudioInput", serverConfig.rigs.first()->rxAudioSetup.name);

    /* Remove old format users*/
    int numUsers = settings->value("ServerNumUsers", 0).toInt();
    if (numUsers > 0) {
        settings->remove("ServerNumUsers");
        for (int f = 0; f < numUsers; f++)
        {
            settings->remove("ServerUsername_" + QString::number(f));
            settings->remove("ServerPassword_" + QString::number(f));
            settings->remove("ServerUserType_" + QString::number(f));
        }
    }

    settings->beginWriteArray("Users");

    for (int f = 0; f < serverConfig.users.count(); f++)
    {
        settings->setArrayIndex(f);
        settings->setValue("Username", serverConfig.users[f].username);
        settings->setValue("Password", serverConfig.users[f].password);
        settings->setValue("UserType", serverConfig.users[f].userType);
    }

    settings->endArray();
    settings->endGroup();

    settings->beginGroup("Cluster");
    settings->setValue("UdpEnabled", prefs.clusterUdpEnable);
    settings->setValue("TcpEnabled", prefs.clusterTcpEnable);
    settings->setValue("UdpPort", prefs.clusterUdpPort);

    settings->beginWriteArray("Servers");

    for (int f = 0; f < prefs.clusters.count(); f++)
    {
        settings->setArrayIndex(f);
        settings->setValue("ServerName", prefs.clusters[f].server);
        settings->setValue("UserName", prefs.clusters[f].userName);
        settings->setValue("Port", prefs.clusters[f].port);
        settings->setValue("Password", prefs.clusters[f].password);
        settings->setValue("Timeout", prefs.clusters[f].timeout);
        settings->setValue("Default", prefs.clusters[f].isdefault);
        if (prefs.clusters[f].isdefault  == true) {
            prefs.clusterTcpServerName = prefs.clusters[f].server;
            prefs.clusterTcpUserName = prefs.clusters[f].userName;
            prefs.clusterTcpPassword = prefs.clusters[f].password;
            prefs.clusterTcpPort = prefs.clusters[f].port;
            prefs.clusterTimeout = prefs.clusters[f].timeout;
        }
    }

    settings->endArray();
    settings->endGroup();

    settings->beginGroup("Keyer");
    settings->setValue("CutNumbers", cw->getCutNumbers());
    settings->setValue("SendImmediate", cw->getSendImmediate());
    settings->setValue("SidetoneEnabled", cw->getSidetoneEnable());
    settings->setValue("SidetoneLevel", cw->getSidetoneLevel());
    QStringList macroList = cw->getMacroText();
    if(macroList.length() == 10)
    {
        settings->beginWriteArray("macros");
        for(int m=0; m < 10; m++)
        {
            settings->setArrayIndex(m);
            settings->setValue("macroText", macroList.at(m));
        }
        settings->endArray();
    } else {
        qDebug(logSystem()) << "Error, CW macro list is wrong length: " << macroList.length();
    }
    settings->endGroup();

#if defined(USB_CONTROLLER)
    settings->beginGroup("USB");

    settings->setValue("EnableUSBControllers", prefs.enableUSBControllers);

    QMutexLocker locker(&usbMutex);

    // Store USB Controller

    settings->remove("Controllers");
    settings->beginWriteArray("Controllers");
    int nc=0;

    auto it = usbDevices.begin();
    while (it != usbDevices.end())
    {
        auto dev = &it.value();
        settings->setArrayIndex(nc);

        settings->setValue("Model", dev->product);
        settings->setValue("Path", dev->path);
        settings->setValue("Disabled", dev->disabled);
        settings->setValue("Sensitivity", dev->sensitivity);
        settings->setValue("Brightness", dev->brightness);
        settings->setValue("Orientation", dev->orientation);
        settings->setValue("Speed", dev->speed);
        settings->setValue("Timeout", dev->timeout);
        settings->setValue("Pages", dev->pages);
        settings->setValue("Color", dev->color.name(QColor::HexArgb));
        settings->setValue("LCD", dev->lcd);

        ++it;
        ++nc;
    }
    settings->endArray();


    settings->remove("Buttons");
    settings->beginWriteArray("Buttons");
    for (int nb = 0; nb < usbButtons.count(); nb++)
    {
        settings->setArrayIndex(nb);
        settings->setValue("Page", usbButtons[nb].page);
        settings->setValue("Dev", usbButtons[nb].dev);
        settings->setValue("Num", usbButtons[nb].num);
        settings->setValue("Path", usbButtons[nb].path);
        settings->setValue("Name", usbButtons[nb].name);
        settings->setValue("Left", usbButtons[nb].pos.left());
        settings->setValue("Top", usbButtons[nb].pos.top());
        settings->setValue("Width", usbButtons[nb].pos.width());
        settings->setValue("Height", usbButtons[nb].pos.height());
        settings->setValue("Colour", usbButtons[nb].textColour.name(QColor::HexArgb));
        settings->setValue("BackgroundOn", usbButtons[nb].backgroundOn.name(QColor::HexArgb));
        settings->setValue("BackgroundOff", usbButtons[nb].backgroundOff.name(QColor::HexArgb));
        if (usbButtons[nb].icon != Q_NULLPTR) {
            settings->setValue("Icon", *usbButtons[nb].icon);
            settings->setValue("IconName", usbButtons[nb].iconName);
        }
        settings->setValue("Toggle", usbButtons[nb].toggle);

        if (usbButtons[nb].onCommand != Q_NULLPTR)
            settings->setValue("OnCommand", usbButtons[nb].onCommand->text);
        if (usbButtons[nb].offCommand != Q_NULLPTR)
            settings->setValue("OffCommand", usbButtons[nb].offCommand->text);
        settings->setValue("Graphics",usbButtons[nb].graphics);
        if (usbButtons[nb].led) {
            settings->setValue("Led", usbButtons[nb].led);
        }
    }

    settings->endArray();

    settings->remove("Knobs");
    settings->beginWriteArray("Knobs");
    for (int nk = 0; nk < usbKnobs.count(); nk++)
    {
        settings->setArrayIndex(nk);
        settings->setValue("Page", usbKnobs[nk].page);
        settings->setValue("Dev", usbKnobs[nk].dev);
        settings->setValue("Num", usbKnobs[nk].num);
        settings->setValue("Path", usbKnobs[nk].path);
        settings->setValue("Left", usbKnobs[nk].pos.left());
        settings->setValue("Top", usbKnobs[nk].pos.top());
        settings->setValue("Width", usbKnobs[nk].pos.width());
        settings->setValue("Height", usbKnobs[nk].pos.height());
        settings->setValue("Colour", usbKnobs[nk].textColour.name());
        if (usbKnobs[nk].command != Q_NULLPTR)
            settings->setValue("Command", usbKnobs[nk].command->text);
    }

    settings->endArray();

    settings->endGroup();
#endif

    settings->sync(); // Automatic, not needed (supposedly)
}


void wfmain::showHideSpectrum(bool show)
{

    ui->mainScope->setVisible(show);
    ui->subScope->setVisible(false);
}

// Key shortcuts (hotkeys)

void wfmain::shortcutF11()
{
    if(onFullscreen)
    {
        this->showNormal();
        onFullscreen = false;
        prefs.useFullScreen = false;
    } else {
        this->showFullScreen();
        onFullscreen = true;
        prefs.useFullScreen = true;
    }
    setupui->updateIfPref(if_useFullScreen);
}

void wfmain::shortcutF1()
{

}

void wfmain::shortcutF2()
{
    showAndRaiseWidget(bandbtns);
}

void wfmain::shortcutF3()
{
    showAndRaiseWidget(finputbtns);
}

void wfmain::shortcutF4()
{

}

// Mode switch keys:
void wfmain::shortcutF5()
{
    // LSB
    changeMode(modeLSB, false);
}

void wfmain::shortcutF6()
{
    // USB
    changeMode(modeUSB, false);
}

void wfmain::shortcutF7()
{
    // AM
    changeMode(modeAM, false);
}

void wfmain::shortcutF8()
{
    // CW
    changeMode(modeCW, false);
}

void wfmain::shortcutF9()
{
    // USB-D
    changeMode(modeUSB, true);
}

void wfmain::shortcutF10()
{
    // FM
    changeMode(modeFM, false);
}

void wfmain::shortcutF12()
{
    // Speak current frequency and mode from the radio
    showStatusBarText("Sending speech command to radio.");
    queue->add(priorityImmediate,queueItem(funcSpeech,QVariant::fromValue(uchar(0U))));
}

void wfmain::shortcutControlT()
{
    // Transmit
    qInfo(logSystem()) << "Activated Control-T shortcut";
    showStatusBarText(QString("Transmitting. Press Control-R to receive."));
    extChangedRsPrefs(rs_pttOn);
}

void wfmain::shortcutControlR()
{
    // Receive
    extChangedRsPrefs(rs_pttOff);
}

void wfmain::shortcutControlI()
{
    // Enable ATU
    ui->tuneEnableChk->click();
}

void wfmain::shortcutControlU()
{
    // Run ATU tuning cycle
    ui->tuneNowBtn->click();
}

void wfmain::shortcutStar()
{
    // Jump to frequency tab from Asterisk key on keypad
    showAndRaiseWidget(finputbtns);
}

void wfmain::shortcutSlash()
{
    // Cycle through available modes
    //ui->modeSelectCombo->setCurrentIndex( (ui->modeSelectCombo->currentIndex()+1) % ui->modeSelectCombo->count() );
    //on_modeSelectCombo_activated( ui->modeSelectCombo->currentIndex() );
}

void wfmain::setTuningSteps()
{
    // TODO: interact with preferences, tuning step drop down box, and current operating mode
    // Units are MHz:
    tsPlusControl = 0.010f;
    tsPlus =        0.001f;
    tsPlusShift =   0.0001f;
    tsPage =        1.0f;
    tsPageShift =   0.5f; // TODO, unbind this keystroke from the dial
    tsWfScroll =    0.0001f; // modified by tuning step selector
    tsKnobMHz =     0.0001f; // modified by tuning step selector

    // Units are in Hz:
    tsPlusControlHz =   10000;
    tsPlusHz =           1000;
    tsPlusShiftHz =       100;
    tsPageHz =        1000000;
    tsPageShiftHz =    500000; // TODO, unbind this keystroke from the dial
    tsWfScrollHz =        100; // modified by tuning step selector
    tsKnobHz =            100; // modified by tuning step selector

}

void wfmain::on_tuningStepCombo_currentIndexChanged(int index)
{
    tsWfScroll = (float)ui->tuningStepCombo->itemData(index).toUInt() / 1000000.0;
    tsKnobMHz =  (float)ui->tuningStepCombo->itemData(index).toUInt() / 1000000.0;

    tsWfScrollHz = ui->tuningStepCombo->itemData(index).toUInt();
    tsKnobHz = ui->tuningStepCombo->itemData(index).toUInt();
    foreach (auto s, rigCaps.steps) {
        if (tsWfScrollHz == s.hz)
        {
            ui->mainScope->setStepSize(s.hz);
            ui->subScope->setStepSize(s.hz);
            queue->add(priorityImmediate,queueItem(funcTuningStep,QVariant::fromValue<uchar>(s.num),false));
        }
    }
}

quint64 wfmain::roundFrequency(quint64 frequency, unsigned int tsHz)
{
    quint64 rounded = 0;
    if(prefs.niceTS)
    {
        rounded = ((frequency % tsHz) > tsHz/2) ? frequency + tsHz - frequency%tsHz : frequency - frequency%tsHz;
        return rounded;
    } else {
        return frequency;
    }
}

quint64 wfmain::roundFrequencyWithStep(quint64 frequency, int steps, unsigned int tsHz)
{
    quint64 rounded = 0;

    if(steps > 0)
    {
        frequency = frequency + (quint64)(steps*tsHz);
    } else {
        frequency = frequency - std::min((quint64)(abs(steps)*tsHz), frequency);
    }

    if(prefs.niceTS)
    {
        rounded = ((frequency % tsHz) > tsHz/2) ? frequency + tsHz - frequency%tsHz : frequency - frequency%tsHz;
        return rounded;
    } else {
        return frequency;
    }
}

void wfmain::shortcutMinus()
{
    if (freqLock) return;

    freqt f;
    f.Hz = roundFrequencyWithStep(ui->mainScope->getFrequency().Hz, -1, tsPlusHz);
    f.MHzDouble = f.Hz / (double)1E6;
    queue->add(priorityImmediate,queueItem(funcSelectedFreq,QVariant::fromValue<freqt>(f),false));
}

void wfmain::shortcutPlus()
{
    if (freqLock) return;

    freqt f;
    f.Hz = roundFrequencyWithStep(ui->mainScope->getFrequency().Hz, 1, tsPlusHz);
    f.MHzDouble = f.Hz / (double)1E6;
    queue->add(priorityImmediate,queueItem(funcSelectedFreq,QVariant::fromValue<freqt>(f),false));
}

void wfmain::shortcutStepMinus()
{
    if (freqLock) return;

    freqt f;
    f.Hz = roundFrequencyWithStep(ui->mainScope->getFrequency().Hz, -1, ui->tuningStepCombo->currentData().toInt());
    f.MHzDouble = f.Hz / (double)1E6;
    queue->add(priorityImmediate,queueItem(funcSelectedFreq,QVariant::fromValue<freqt>(f),false));

}

void wfmain::shortcutStepPlus()
{
    if (freqLock) return;

    freqt f;
    f.Hz = roundFrequencyWithStep(ui->mainScope->getFrequency().Hz, 1, ui->tuningStepCombo->currentData().toInt());

    f.MHzDouble = f.Hz / (double)1E6;
    queue->add(priorityImmediate,queueItem(funcSelectedFreq,QVariant::fromValue<freqt>(f),false));
}

void wfmain::shortcutShiftMinus()
{
    if(freqLock) return;

    freqt f;
    f.Hz = roundFrequencyWithStep(ui->mainScope->getFrequency().Hz, -1, tsPlusShiftHz);
    f.MHzDouble = f.Hz / (double)1E6;
    queue->add(priorityImmediate,queueItem(funcSelectedFreq,QVariant::fromValue<freqt>(f),false));
}

void wfmain::shortcutShiftPlus()
{
    if(freqLock) return;

    freqt f;
    f.Hz = roundFrequencyWithStep(ui->mainScope->getFrequency().Hz, 1, tsPlusShiftHz);
    f.MHzDouble = f.Hz / (double)1E6;
    queue->add(priorityImmediate,queueItem(funcSelectedFreq,QVariant::fromValue<freqt>(f),false));

}

void wfmain::shortcutControlMinus()
{
    if(freqLock) return;

    freqt f;
    f.Hz = roundFrequencyWithStep(ui->mainScope->getFrequency().Hz, -1, tsPlusControlHz);
    f.MHzDouble = f.Hz / (double)1E6;
    queue->add(priorityImmediate,queueItem(funcSelectedFreq,QVariant::fromValue<freqt>(f),false));
}

void wfmain::shortcutControlPlus()
{
    if(freqLock) return;

    freqt f;
    f.Hz = roundFrequencyWithStep(ui->mainScope->getFrequency().Hz, 1, tsPlusControlHz);
    f.MHzDouble = f.Hz / (double)1E6;
    queue->add(priorityImmediate,queueItem(funcSelectedFreq,QVariant::fromValue<freqt>(f),false));
}

void wfmain::shortcutPageUp()
{
    if(freqLock) return;

    freqt f;
    f.Hz = ui->mainScope->getFrequency().Hz + tsPageHz;
    f.MHzDouble = f.Hz / (double)1E6;
    queue->add(priorityImmediate,queueItem(funcSelectedFreq,QVariant::fromValue<freqt>(f),false));
}

void wfmain::shortcutPageDown()
{
    if(freqLock) return;

    freqt f;
    f.Hz = ui->mainScope->getFrequency().Hz - tsPageHz;
    f.MHzDouble = f.Hz / (double)1E6;
    queue->add(priorityImmediate,queueItem(funcSelectedFreq,QVariant::fromValue<freqt>(f),false));
}

void wfmain::shortcutF()
{
    showStatusBarText("Sending speech command (frequency) to radio.");
    emit sayFrequency();
}

void wfmain::shortcutM()
{
    showStatusBarText("Sending speech command (mode) to radio.");
    emit sayMode();
}

funcs wfmain::getInputTypeCommand(inputTypes input)
{
    funcs func;

    switch(input)
    {
    case inputMICACCA:
    case inputMICACCB:
    case inputMICACCAACCB:
    case inputMICUSB:
    case inputMic:
        func = funcMicGain;
        break;

    case inputACCAACCB:
    case inputACCA:
        func = funcACCAModLevel;
        break;

    case inputACCB:
        func = funcACCBModLevel;
        break;

    case inputUSB:
        func = funcUSBModLevel;
        break;

    case inputLAN:
        func = funcLANModLevel;
        break;
    case inputSPDIF:
        func = funcSPDIFModLevel;
        break;
    default:
        func = funcNone;
        break;
    }
    return func;
}


void wfmain:: getInitialRigState()
{
    // Initial list of queries to the radio.
    // These are made when the program starts up
    // and are used to adjust the UI to match the radio settings
    // the polling interval is set at 200ms. Faster is possible but slower
    // computers will glitch occasionally.

    queue->del(funcTransceiverId); // This command is no longer required

    /*
    queue->add(priorityImmediate,(rigCaps.commands.contains(funcSelectedFreq)?funcSelectedFreq:funcFreqGet),false);
    queue->add(priorityImmediate,(rigCaps.commands.contains(funcSelectedMode)?funcSelectedMode:funcModeGet),false);
    queue->add(priorityImmediate,(rigCaps.commands.contains(funcUnselectedFreq)?funcUnselectedFreq:funcNone),false,true);
    queue->add(priorityImmediate,(rigCaps.commands.contains(funcUnselectedMode)?funcUnselectedMode:funcNone),false,true);

    // From left to right in the UI:
    if (rigCaps.hasTransmit)
    {
        queue->add(priorityImmediate,funcDataModeWithFilter);
        queue->add(priorityImmediate,(rigCaps.commands.contains(funcDATAOffMod)?funcDATAOffMod:funcNone),false);
        queue->add(priorityImmediate,(rigCaps.commands.contains(funcDATA1Mod)?funcDATA1Mod:funcNone),false);
        queue->add(priorityImmediate,(rigCaps.commands.contains(funcDATA2Mod)?funcDATA2Mod:funcNone),false);
        queue->add(priorityImmediate,(rigCaps.commands.contains(funcDATA3Mod)?funcDATA3Mod:funcNone),false);
        queue->add(priorityImmediate,(rigCaps.commands.contains(funcRFPower)?funcRFPower:funcNone),false);
        queue->add(priorityImmediate,getInputTypeCommand(currentModDataOffSrc.type),false);
        queue->add(priorityImmediate,getInputTypeCommand(currentModData1Src.type),false);
        if (rigCaps.commands.contains(funcDATA2Mod))
            queue->add(priorityImmediate,getInputTypeCommand(currentModData2Src.type),false);
        if (rigCaps.commands.contains(funcDATA3Mod))
            queue->add(priorityImmediate,getInputTypeCommand(currentModData3Src.type),false);
    }

    queue->add(priorityImmediate,funcRfGain,false,false);

    if (!usingLAN)
        queue->add(priorityImmediate,funcAfGain,false);

    queue->add(priorityImmediate,funcMonitor,false);
    queue->add(priorityImmediate,funcMonitorGain,false);

    */
    if(rigCaps.hasSpectrum)
    {
        queue->add(priorityImmediate,queueItem(funcScopeOnOff,QVariant::fromValue(quint8(1)),false));
        queue->add(priorityImmediate,queueItem(funcScopeDataOutput,QVariant::fromValue(quint8(1)),false));
    }

    ui->mainScope->enableScope(this->rigCaps.commands.contains(funcScopeMainMode));
    ui->subScope->enableScope(this->rigCaps.commands.contains(funcScopeSubMode));

    // Only show settingsgroup if rig has sub
    ui->scopeSettingsGroup->setVisible(rigCaps.commands.contains(funcScopeSubWaveData));

    ui->scopeDualBtn->setVisible(rigCaps.commands.contains(funcScopeSingleDual));
    ui->antennaGroup->setVisible(rigCaps.commands.contains(funcAntenna));
    ui->preampAttGroup->setVisible(rigCaps.commands.contains(funcPreamp));
    quint64 start=UINT64_MAX;
    quint64 end=0;
    foreach (auto band, rigCaps.bands)
    {
        if (start > band.lowFreq)
            start = band.lowFreq;
        if (end < band.highFreq)
            end = band.highFreq;
    }
    ui->mainScope->displaySettings(0, start, end, 1,(FctlUnit)prefs.frequencyUnits,&rigCaps.bands);
    ui->subScope->displaySettings(0, start, end, 1,(FctlUnit)prefs.frequencyUnits,&rigCaps.bands);

    /*

    if (rigCaps.commands.contains(funcFilterWidth))
        queue->add(priorityHigh,funcFilterWidth,false);


    if (rigCaps.commands.contains(funcSplitStatus))
        queue->add(priorityHigh,funcSplitStatus,false);

    if(rigCaps.commands.contains(funcTuningStep))
        queue->add(priorityImmediate,funcTuningStep,false);

    if(rigCaps.commands.contains(funcRepeaterTone))
    {
        queue->add(priorityImmediate,funcRepeaterTone,false);
        queue->add(priorityImmediate,funcRepeaterTSQL,false);
    }

    if(rigCaps.commands.contains(funcRepeaterDTCS))
        queue->add(priorityImmediate,funcRepeaterDTCS,false);

    if(rigCaps.commands.contains(funcToneSquelchType))
        queue->add(priorityImmediate,funcToneSquelchType,false);

    if(rigCaps.commands.contains(funcAntenna))
        queue->add(priorityImmediate,funcAntenna,false);

    if(rigCaps.commands.contains(funcAttenuator))
        queue->add(priorityImmediate,funcAttenuator,false);

    if(rigCaps.commands.contains(funcPreamp))
        queue->add(priorityImmediate,funcPreamp,false);

    if (rigCaps.commands.contains(funcRitStatus))
    {
        queue->add(priorityImmediate,funcRITFreq,false);
        queue->add(priorityImmediate,funcRitStatus,false);
    }

    if(rigCaps.commands.contains(funcIFShift))
        queue->add(priorityImmediate,funcIFShift,false);

    if(rigCaps.commands.contains(funcPBTInner) && rigCaps.commands.contains(funcPBTOuter))
    {
        queue->add(priorityImmediate,funcPBTInner,false);
        queue->add(priorityImmediate,funcPBTOuter,false);
    }

    if(rigCaps.commands.contains(funcTunerStatus))
        queue->add(priorityImmediate,funcTunerStatus,false);

    */
}

void wfmain::showStatusBarText(QString text)
{
    ui->statusBar->showMessage(text, 5000);
}

void wfmain::useSystemTheme(bool checked)
{
    setAppTheme(!checked);
    prefs.useSystemTheme = checked;
}

void wfmain::setAppTheme(bool isCustom)
{
    if(isCustom)
    {
#ifndef Q_OS_LINUX
        QFile f(":"+prefs.stylesheetPath); // built-in resource
#else
        QFile f(PREFIX "/share/wfview/" + prefs.stylesheetPath);
#endif
        if (!f.exists())
        {
            printf("Unable to set stylesheet, file not found\n");
            printf("Tried to load: [%s]\n", f.fileName().toStdString().c_str() );
        }
        else
        {
            f.open(QFile::ReadOnly | QFile::Text);
            QTextStream ts(&f);
            qApp->setStyleSheet(ts.readAll());
        }
    } else {
        qApp->setStyleSheet("");
    }
}

void wfmain::setDefaultColors(int presetNumber)
{
    // These are the default color schemes
    if(presetNumber > numColorPresetsTotal-1)
        return;

    colorPrefsType *p = &colorPreset[presetNumber];

    // Begin every parameter with these safe defaults first:
    if(p->presetName == Q_NULLPTR)
    {
        p->presetName = new QString();
    }
    p->presetName->clear();
    p->presetName->append(QString("%1").arg(presetNumber));
    p->presetNum = presetNumber;
    p->gridColor = QColor(0,0,0,255);
    p->axisColor = QColor(Qt::white);
    p->textColor = QColor(Qt::white);
    p->spectrumLine = QColor(Qt::yellow);
    p->spectrumFill = QColor("transparent");
    p->underlayLine = QColor(20+200/4.0*1,70*(1.6-1/4.0), 150, 150).lighter(200);
    p->underlayFill = QColor(20+200/4.0*1,70*(1.6-1/4.0), 150, 150);
    p->plotBackground = QColor(Qt::black);
    p->tuningLine = QColor(Qt::blue);
    p->passband = QColor(Qt::blue);
    p->pbt = QColor("#32ff0000");

    p->meterLevel = QColor("#148CD2").darker();
    p->meterAverage = QColor("#3FB7CD");
    p->meterPeakLevel = QColor("#3CA0DB").lighter();
    p->meterPeakScale = QColor(Qt::red);
    p->meterLowerLine = QColor("#eff0f1");
    p->meterLowText = QColor("#eff0f1");

    p->wfBackground = QColor(Qt::black);
    p->wfAxis = QColor(Qt::white);
    p->wfGrid = QColor(Qt::white);
    p->wfText = QColor(Qt::white);

    p->clusterSpots = QColor(Qt::red);

    //qInfo(logSystem()) << "default color preset [" << pn << "] set to pn.presetNum index [" << p->presetNum << "]" << ", with name " << *(p->presetName);

    switch (presetNumber)
    {
        case 0:
        {
            // Dark
            p->presetName->clear();
            p->presetName->append("Dark");
            p->plotBackground = QColor(0,0,0,255);
            p->axisColor = QColor(Qt::white);
            p->textColor = QColor(255,255,255,255);
            p->gridColor = QColor(0,0,0,255);
            p->spectrumFill = QColor("transparent");
            p->spectrumLine = QColor(Qt::yellow);
            p->underlayLine = QColor("#9633ff55");
            p->underlayFill = QColor(20+200/4.0*1,70*(1.6-1/4.0), 150, 150);
            p->tuningLine = QColor("#ff55ffff");
            p->passband = QColor("#32ffffff");
            p->pbt = QColor("#32ff0000");

            p->meterLevel = QColor("#148CD2").darker();
            p->meterAverage = QColor("#3FB7CD");
            p->meterPeakScale = QColor(Qt::red);
            p->meterPeakLevel = QColor("#3CA0DB").lighter();
            p->meterLowerLine = QColor("#eff0f1");
            p->meterLowText = QColor("#eff0f1");

            p->wfBackground = QColor(Qt::black);
            p->wfAxis = QColor(Qt::white);
            p->wfGrid = QColor("transparent");
            p->wfText = QColor(Qt::white);
            p->clusterSpots = QColor(Qt::red);
            break;
        }
        case 1:
        {
            // Bright
            p->presetName->clear();
            p->presetName->append("Bright");
            p->plotBackground = QColor(Qt::white);
            p->axisColor = QColor(200,200,200,255);
            p->gridColor = QColor(255,255,255,0);
            p->textColor = QColor(Qt::black);
            p->spectrumFill = QColor("transparent");
            p->spectrumLine = QColor(Qt::black);
            p->underlayLine = QColor(Qt::blue);
            p->tuningLine = QColor(Qt::darkBlue);
            p->passband = QColor("#64000080");
            p->pbt = QColor("#32ff0000");

            p->meterAverage = QColor("#3FB7CD");
            p->meterPeakLevel = QColor("#3CA0DB");
            p->meterPeakScale = QColor(Qt::darkRed);
            p->meterLowerLine = QColor(Qt::black);
            p->meterLowText = QColor(Qt::black);

            p->wfBackground = QColor(Qt::white);
            p->wfAxis = QColor(200,200,200,255);
            p->wfGrid = QColor("transparent");
            p->wfText = QColor(Qt::black);
            p->clusterSpots = QColor(Qt::red);
            break;
        }

        case 2:
        case 3:
        case 4:
        default:
            break;

    }
    //ui->colorPresetCombo->setItemText(presetNumber, *(p->presetName));
}

void wfmain::receiveRigID(rigCapabilities rigCaps)
{
    // Note: We intentionally request rigID several times
    // because without rigID, we can't do anything with the waterfall.
    bandbtns->acceptRigCaps(rigCaps);
    if(haveRigCaps)
    {
        // Note: This line makes it difficult to accept a different radio connecting.
        return;
    } else {

        showStatusBarText(QString("Found radio at address 0x%1 of name %2 and model ID %3.").arg(rigCaps.civ,2,16).arg(rigCaps.modelName).arg(rigCaps.modelID));

        qDebug(logSystem()) << "Rig name: " << rigCaps.modelName;
        qDebug(logSystem()) << "Has LAN capabilities: " << rigCaps.hasLan;
        qDebug(logSystem()) << "Rig ID received into wfmain: spectLenMax: " << rigCaps.spectLenMax;
        qDebug(logSystem()) << "Rig ID received into wfmain: spectAmpMax: " << rigCaps.spectAmpMax;
        qDebug(logSystem()) << "Rig ID received into wfmain: spectSeqMax: " << rigCaps.spectSeqMax;
        qDebug(logSystem()) << "Rig ID received into wfmain: hasSpectrum: " << rigCaps.hasSpectrum;

        this->rigCaps = rigCaps;
        rigName->setText(rigCaps.modelName);
        if (serverConfig.enabled) {
            serverConfig.rigs.first()->modelName = rigCaps.modelName;
            serverConfig.rigs.first()->rigName = rigCaps.modelName;
            serverConfig.rigs.first()->civAddr = rigCaps.civ;
            serverConfig.rigs.first()->baudRate = rigCaps.baudRate;
        }
        setWindowTitle(rigCaps.modelName);

        if(rigCaps.hasSpectrum)
        {
            ui->mainScope->prepareScope(rigCaps.spectAmpMax, rigCaps.spectLenMax);
            ui->subScope->prepareScope(rigCaps.spectAmpMax, rigCaps.spectLenMax);
        }

        haveRigCaps = true;


        if (rigCaps.bands.size() > 0) {
            lastRequestedBand = rigCaps.bands[0].band;
        }

        // Added so that server receives rig capabilities.
        emit sendRigCaps(rigCaps);
        rpt->setRig(rigCaps);
        trxadj->setRig(rigCaps);

        // Set the mode combo box up:
        ui->mainScope->clearMode();
        ui->subScope->clearMode();

        foreach (auto m, rigCaps.modes)
        {
            //ui->modeSelectCombo->addItem(m.name, m.mk);
            ui->mainScope->addMode("Mode "+m.name,QVariant::fromValue(m));
            ui->subScope->addMode("Mode "+m.name,QVariant::fromValue(m));
        }

        ui->mainScope->clearFilter();
        ui->subScope->clearFilter();

        foreach (auto f, rigCaps.filters)
        {
            ui->mainScope->addFilter(f.name,f.num);
            ui->subScope->addFilter(f.name,f.num);
        }


        // Set the tuning step combo box up:
        ui->tuningStepCombo->blockSignals(true);
        ui->tuningStepCombo->clear();
        foreach (auto s, rigCaps.steps)
        {
            ui->tuningStepCombo->addItem(s.name, s.hz);
        }

        ui->tuningStepCombo->setCurrentIndex(2);
        ui->tuningStepCombo->blockSignals(false);

        setupui->updateModSourceList(0, rigCaps.inputs);
        setupui->updateModSourceList(1, rigCaps.inputs);

        ui->mainScope->clearData();
        ui->subScope->clearData();

        ui->mainScope->addData("Data Off",0);
        ui->subScope->addData("Data Off",0);

        if (rigCaps.commands.contains(funcDATA1Mod))
        {
            setupui->updateModSourceList(1, rigCaps.inputs);
            if (!rigCaps.commands.contains(funcDATA2Mod))
            {
                ui->mainScope->addData("Data On", 2);
                ui->subScope->addData("Data On", 2);
            }
        }

        if (rigCaps.commands.contains(funcDATA2Mod))
        {
            setupui->updateModSourceList(2, rigCaps.inputs);
            ui->mainScope->addData("Data 1", 2);
            ui->subScope->addData("Data 1", 2);
            ui->mainScope->addData("Data 2", 2);
            ui->subScope->addData("Data 2", 2);
        }

        if (rigCaps.commands.contains(funcDATA3Mod))
        {
            setupui->updateModSourceList(3, rigCaps.inputs);
            ui->mainScope->addData("Data 3", 3);
            ui->subScope->addData("Data 3", 3);
        }

        ui->attSelCombo->clear();
        if(rigCaps.commands.contains(funcAttenuator))
        {
            ui->attSelCombo->setDisabled(false);
            foreach (auto att, rigCaps.attenuators)
            {
                ui->attSelCombo->addItem(((att == 0) ? QString("0 dB") : QString("-%1 dB").arg(att,2,16)),att);
            }
        } else {
            ui->attSelCombo->setDisabled(true);
        }

        ui->preampSelCombo->clear();
        if(rigCaps.commands.contains(funcPreamp))
        {
            ui->preampSelCombo->setDisabled(false);
            foreach (auto pre, rigCaps.preamps)
            {
                ui->preampSelCombo->addItem(pre.name, pre.num);
            }
        } else {
            ui->preampSelCombo->setDisabled(true);
        }

        ui->antennaSelCombo->clear();
        if(rigCaps.commands.contains(funcAntenna))
        {
            ui->antennaSelCombo->setDisabled(false);
            foreach (auto ant, rigCaps.antennas)
            {
                ui->antennaSelCombo->addItem(ant.name,ant.num);
            }
        } else {
            ui->antennaSelCombo->setDisabled(true);
        }

        ui->rxAntennaCheck->setEnabled(rigCaps.commands.contains(funcRXAntenna));
        ui->rxAntennaCheck->setChecked(false);

        //ui->scopeBWCombo->blockSignals(true);
        //ui->scopeBWCombo->clear();
        ui->mainScope->clearSpans();
        ui->subScope->clearSpans();
        if(rigCaps.hasSpectrum)
        {
            //ui->scopeBWCombo->setHidden(false);
            for(unsigned int i=0; i < rigCaps.scopeCenterSpans.size(); i++)
            {
                ui->mainScope->addSpan(rigCaps.scopeCenterSpans.at(i).name, QVariant::fromValue(rigCaps.scopeCenterSpans.at(i)));
                ui->subScope->addSpan(rigCaps.scopeCenterSpans.at(i).name, QVariant::fromValue(rigCaps.scopeCenterSpans.at(i)));
            }
            ui->mainScope->setRange(prefs.mainPlotFloor, prefs.mainPlotCeiling);
            ui->subScope->setRange(prefs.subPlotFloor, prefs.subPlotCeiling);
        }// else {
            //ui->scopeBWCombo->setHidden(true);
       //}
        //ui->scopeBWCombo->blockSignals(false);

        //setBandButtons();

        ui->tuneEnableChk->setEnabled(rigCaps.commands.contains(funcTunerStatus));
        ui->tuneNowBtn->setEnabled(rigCaps.commands.contains(funcTunerStatus));

        ui->memoriesBtn->setEnabled(rigCaps.commands.contains(funcMemoryContents));

        //ui->useRTSforPTTchk->setChecked(prefs.forceRTSasPTT);

       // ui->audioSystemCombo->setEnabled(false);
        //ui->audioSystemServerCombo->setEnabled(false);

        ui->connectBtn->setText("Disconnect from Radio"); // We must be connected now.
        connStatus = connConnected;
        if(usingLAN)
        {
            ui->afGainSlider->setValue(prefs.localAFgain);
            emit sendLevel(funcAfGain,prefs.localAFgain);
        }
        // Adding these here because clearly at this point we have valid
        // rig comms. In the future, we should establish comms and then
        // do all the initial grabs. For now, this hack of adding them here and there:
        // recalculate command timing now that we know the rig better:
        if(prefs.polling_ms != 0)
        {
            changePollTiming(prefs.polling_ms, true);
        } else {
            calculateTimingParameters();
        }

        // Set the second meter here as I suspect we need to be connected for it to work?
        changeMeter2Type(prefs.meter2Type);
//        for (int i = 0; i < ui->meter2selectionCombo->count(); i++)
//        {
//            if (static_cast<meter_t>(ui->meter2selectionCombo->itemData(i).toInt()) == prefs.meter2Type)
//            {
//                // I thought that setCurrentIndex() would call the activated() function for the combobox
//                // but it doesn't, so call it manually.
//                //ui->meter2selectionCombo->setCurrentIndex(i);
//                changeMeter2Type(i);
//            }
//        }
    }

}

void wfmain::initPeriodicCommands()
{
    // This function places periodic polling commands into the queue.
    // Can be run multiple times as it will remove all existing entries.

    qInfo(logSystem()) << "Start periodic commands (and delete unsupported)";

    queue->clear();

    queue->add(priorityMedium,funcSelectedFreq,true,false);
    queue->add(priorityMedium,funcSelectedMode,true,false);
    //queue->add(priorityMedium,(rigCaps.commands.contains(funcSelectedMode)?funcNone:funcDataModeWithFilter),true,false);
    queue->add(priorityMedium,funcUnselectedFreq,true,true);
    queue->add(priorityMedium,funcUnselectedMode,true,true);

    if (rigCaps.hasSpectrum)
    {
        queue->add(priorityHigh,funcOverflowStatus,true,false);
        queue->add(priorityMediumHigh,funcScopeMainMode,true,false);
        queue->add(priorityMediumHigh,funcScopeSubMode,true,true);
        queue->add(priorityMediumHigh,funcScopeMainSpan,true,false);
        queue->add(priorityMediumHigh,funcScopeSubSpan,true,true);
        queue->add(priorityMediumHigh,funcScopeSingleDual,true,false);
        queue->add(priorityMediumHigh,funcScopeMainSub,true,false);
        queue->add(priorityMedium,funcScopeMainSpeed,true,false);
        queue->add(priorityMedium,funcScopeSubSpeed,true,true);
        queue->add(priorityMedium,funcScopeMainHold,true,false);
        queue->add(priorityMedium,funcScopeSubHold,true,true);
    }

    if(rigCaps.hasTransmit) {
        queue->add(priorityHigh,funcTransceiverStatus,true,false);
        queue->add(priorityMediumHigh,funcDATAOffMod,true,false);
        queue->add(priorityMediumHigh,funcDATA1Mod,true,false);
        queue->add(priorityMediumHigh,funcDATA2Mod,true,false);
        queue->add(priorityMediumHigh,funcDATA3Mod,true,false);
        queue->add(priorityMedium,funcRFPower,true,false);
        queue->add(priorityMediumLow,funcMonitorGain,true,false);
        queue->add(priorityMediumLow,funcMonitor,true,false);
    }

    queue->add(priorityMedium,funcRfGain,true,false);
    queue->add(priorityMediumLow,funcTuningStep,true,false);
    queue->add(priorityMediumLow,funcAttenuator,true,false);
    queue->add(priorityMediumLow,funcPreamp,true,false);
    queue->add(priorityMediumLow,funcAntenna,true,false);
    queue->add(priorityMediumLow,funcSplitStatus,true,false);
    queue->add(priorityMediumLow,funcToneSquelchType,true,false);

    queue->add(priorityHighest,queueItem(funcSMeter,true,false));
}

void wfmain::receivePTTstatus(bool pttOn)
{
    emit sendLevel(funcTransceiverStatus,pttOn);

    // This is the only place where amTransmitting and the transmit button text should be changed:
    if (pttOn && !amTransmitting)
    {

        pttLed->setState(QLedLabel::State::StateError);
        pttLed->setToolTip("Transmitting");
        if(splitModeEnabled)
        {
            pttLed->setState(QLedLabel::State::StateSplitErrorOk);
            pttLed->setToolTip("TX Split");
        } else {
            pttLed->setState(QLedLabel::State::StateError);
            pttLed->setToolTip("Transmitting");
        }
    }
    else if (!pttOn && amTransmitting)
    {
        pttLed->setState(QLedLabel::State::StateOk);
        pttLed->setToolTip("Receiving");
    }
    amTransmitting = pttOn;
    rpt->handleTransmitStatus(pttOn);
    changeTxBtn();
}

void wfmain::changeTxBtn()
{
    if(amTransmitting)
    {
        ui->transmitBtn->setText("Receive");
    } else {
        ui->transmitBtn->setText("Transmit");
    }
}

void wfmain::changeFullScreenMode(bool checked)
{
    if(checked)
    {
        this->showFullScreen();
        onFullscreen = true;
    } else {
        this->showNormal();
        onFullscreen = false;
    }
    prefs.useFullScreen = checked;
}

void wfmain::changeMode(rigMode_t mode)
{
    bool dataOn = false;
    if(((unsigned char) mode >> 4) == 0x08)
    {
        dataOn = true;
        mode = (rigMode_t)((int)mode & 0x0f);
    }

    changeMode(mode, dataOn);
}

void wfmain::changeMode(rigMode_t mode, unsigned char data)
{
    foreach (modeInfo mi, rigCaps.modes)
    {
        if (mi.mk == mode)
        {
            modeInfo m;
            m = modeInfo(mi);
            m.data = data;
            m.VFO=selVFO_t::activeVFO;
            if((m.mk != currentModeInfo.mk) && prefs.automaticSidebandSwitching)
            {
                queue->add(priorityImmediate,queueItem((rigCaps.commands.contains(funcSelectedMode)?funcSelectedMode:funcModeGet),QVariant::fromValue<modeInfo>(m),false));
                if (!rigCaps.commands.contains(funcSelectedMode))
                    queue->add(priorityImmediate,queueItem(funcDataModeWithFilter,QVariant::fromValue<modeInfo>(m),false));
            }
            break;
        }
    }

    queue->add(priorityImmediate,(rigCaps.commands.contains(funcSelectedMode)?funcSelectedMode:funcModeGet),false);
}

void wfmain::on_freqDial_valueChanged(int value)
{
    int fullSweep = ui->freqDial->maximum() - ui->freqDial->minimum();

    freqt f;
    f.Hz = 0;
    f.MHzDouble = 0;

    volatile int delta = 0;

    if(freqLock)
    {
        ui->freqDial->blockSignals(true);
        ui->freqDial->setValue(oldFreqDialVal);
        ui->freqDial->blockSignals(false);
        return;
    }

    delta = (value - oldFreqDialVal);

    if(delta > fullSweep/2)
    {
        // counter-clockwise past the zero mark
        // ie, from +3000 to 3990, old=3000, new = 3990, new-old = 990
        // desired delta here would actually be -10
        delta = delta - fullSweep;
    } else if (delta < -fullSweep/2)
    {
        // clock-wise past the zero mark
        // ie, from +3990 to 3000, old=3990, new = 3000, new-old = -990
        // desired delta here would actually be +10
        delta = fullSweep + delta;
    }

    // The step size is 10, which forces the knob to not skip a step crossing zero.
    delta = delta / ui->freqDial->singleStep();

    // With the number of steps and direction of steps established,
    // we can now adjust the frequency:

    f.Hz = roundFrequencyWithStep(ui->mainScope->getFrequency().Hz, delta, tsKnobHz);
    f.MHzDouble = f.Hz / (double)1E6;
    if (f.Hz > 0)
    {
        oldFreqDialVal = value;
        ui->mainScope->setFrequency(f);
        queue->add(priorityImmediate,queueItem(funcSelectedFreq,QVariant::fromValue<freqt>(f),false));
    } else {
        ui->freqDial->blockSignals(true);
        ui->freqDial->setValue(oldFreqDialVal);
        ui->freqDial->blockSignals(false);
        return;
    }
}

void wfmain::setBand(int band)
{
    queue->add(priorityImmediate,queueItem(funcBandStackReg,QVariant::fromValue<uchar>(band),false));
}

void wfmain::on_aboutBtn_clicked()
{
    abtBox->show();
}

void wfmain::gotoMemoryPreset(int presetNumber)
{
    preset_kind temp = mem.getPreset(presetNumber);
    if(!temp.isSet)
    {
        qWarning(logGui()) << "Recalled Preset #" << presetNumber << "is not set.";
    }
    //setFilterVal = ui->modeFilterCombo->currentIndex()+1; // TODO, add to memory
    setModeVal = temp.mode;
    freqt memFreq;
    modeInfo m;
    m.mk = temp.mode;
    //m.filter = ui->modeFilterCombo->currentIndex()+1;
    m.reg =(unsigned char) m.mk; // fallback, works only for some modes
    memFreq.Hz = temp.frequency * 1E6;
    //issueCmd(cmdSetFreq, memFreq);
    //issueDelayedCommand(cmdSetModeFilter); // goes to setModeVal
    //issueCmd(cmdSetMode, m);
    memFreq.MHzDouble = memFreq.Hz / 1.0E6;
    ui->mainScope->setFrequency(memFreq);
    qDebug(logGui()) << "Recalling preset number " << presetNumber << " as frequency " << temp.frequency << "MHz";
}

void wfmain::saveMemoryPreset(int presetNumber)
{
    // int, double, rigMode_t
    double frequency;
    if(ui->mainScope->getFrequency().Hz == 0)
    {
        frequency = ui->mainScope->getFrequency().MHzDouble;
    } else {
        frequency = ui->mainScope->getFrequency().Hz / 1.0E6;
    }
    rigMode_t mode = currentMode;
    qDebug(logGui()) << "Saving preset number " << presetNumber << " to frequency " << frequency << " MHz";
    mem.setPreset(presetNumber, frequency, mode);
}

void wfmain::on_rfGainSlider_valueChanged(int value)
{
    queue->addUnique(priorityImmediate,queueItem(funcRfGain,QVariant::fromValue<ushort>(value),false,false));
}

void wfmain::on_afGainSlider_valueChanged(int value)
{
    if(usingLAN)
    {
        prefs.rxSetup.localAFgain = (unsigned char)(value);
        prefs.localAFgain = (unsigned char)(value);
        emit setAfGain(value);
        emit sendLevel(funcAfGain,value);
    } else {
        queue->addUnique(priorityImmediate,queueItem(funcAfGain,QVariant::fromValue<ushort>(value),false));
    }
}

void wfmain::on_monitorSlider_valueChanged(int value)
{
    queue->addUnique(priorityImmediate,queueItem(funcMonitorGain,QVariant::fromValue<ushort>(value),false));
}

void wfmain::on_monitorLabel_linkActivated(const QString&)
{
    cacheItem ca = queue->getCache(funcMonitor);
    bool mon = ca.value.toBool();
    queue->add(priorityImmediate,queueItem(funcMonitor,QVariant::fromValue<bool>(!mon)));
}

void wfmain::receiveRfGain(unsigned char level)
{
    // qInfo(logSystem()) << "Receive RF  level of" << (int)level << " = " << 100*level/255.0 << "%";
    ui->rfGainSlider->blockSignals(true);
    ui->rfGainSlider->setValue(level);
    ui->rfGainSlider->blockSignals(false);
    emit sendLevel(funcRfGain,level);
}

void wfmain::receiveAfGain(unsigned char level)
{
    // qInfo(logSystem()) << "Receive AF  level of" << (int)level << " = " << 100*level/255.0 << "%";
    ui->afGainSlider->blockSignals(true);
    ui->afGainSlider->setValue(level);
    ui->afGainSlider->blockSignals(false);
    emit sendLevel(funcAfGain,level);
}

void wfmain::receiveSql(unsigned char level)
{
    ui->sqlSlider->setValue(level);
    emit sendLevel(funcSquelch,level);
}

void wfmain::receiveIFShift(unsigned char level)
{
    trxadj->updateIFShift(level);
    emit sendLevel(funcIFShift,level);
}

void wfmain::on_tuneNowBtn_clicked()
{
    queue->addUnique(priorityImmediate,queueItem(funcTunerStatus,QVariant::fromValue<uchar>(2U)));
    showStatusBarText("Starting ATU tuning cycle...");
}

void wfmain::on_tuneEnableChk_clicked(bool checked)
{
    queue->addUnique(priorityImmediate,queueItem(funcTunerStatus,QVariant::fromValue<uchar>(checked)));

    showStatusBarText(QString("Turning %0 ATU").arg(checked?"on":"off"));
}

bool wfmain::on_exitBtn_clicked()
{
    bool ret=false;
    if (prefs.settingsChanged)
    {
        // Settings have changed since last save
        qInfo() << "Settings have changed since last save";
        int reply = QMessageBox::question(this,"wfview","Settings have changed since last save, exit anyway?",QMessageBox::Save | QMessageBox::No |QMessageBox::Yes);
        if (reply == QMessageBox::Save)
        {
            saveSettings();
        } else if (reply == QMessageBox::No)
        {
            return true;
        }
    }

    // Are you sure?
    if (!prefs.confirmExit) {
        QApplication::exit();
    }
    QCheckBox *cb = new QCheckBox("Don't ask me again");
    cb->setToolTip("Don't ask me to confirm exit again");
    QMessageBox msgbox;
    msgbox.setText("Are you sure you wish to exit?\n");
    msgbox.setIcon(QMessageBox::Icon::Question);
    QAbstractButton *yesButton = msgbox.addButton(QMessageBox::Yes);
    msgbox.addButton(QMessageBox::No);
    msgbox.setDefaultButton(QMessageBox::Yes);
    msgbox.setCheckBox(cb);

    QObject::connect(cb, &QCheckBox::stateChanged, this, [this](int state){
        if (static_cast<Qt::CheckState>(state) == Qt::CheckState::Checked) {
            prefs.confirmExit=false;
        } else {
            prefs.confirmExit=true;
        }
        settings->beginGroup("Interface");
        settings->setValue("ConfirmExit", this->prefs.confirmExit);
        settings->endGroup();
        settings->sync();
    });

    msgbox.exec();
    delete cb;

    if (msgbox.clickedButton() == yesButton) {
        QApplication::exit();
    } else {
        ret=true;
    }
    return ret;
}

void wfmain::handlePttLimit()
{
    // transmission time exceeded!
    showStatusBarText("Transmit timeout at 3 minutes. Sending PTT OFF command now.");
    //queue->add(priorityImmediate,queueItem(funcTransceiverStatus,QVariant::fromValue<bool>(false),false));
    //issueDelayedCommand(cmdGetPTT);
}

void wfmain::on_saveSettingsBtn_clicked()
{
    saveSettings(); // save memory, UI, and radio settings
}

void wfmain::receiveATUStatus(unsigned char atustatus)
{
    // qInfo(logSystem()) << "Received ATU status update: " << (unsigned int) atustatus;
    switch(atustatus)
    {
        case 0x00:
            // ATU not active
            ui->tuneEnableChk->blockSignals(true);
            ui->tuneEnableChk->setChecked(false);
            ui->tuneEnableChk->blockSignals(false);
            showStatusBarText("ATU not enabled.");
            break;
        case 0x01:
            // ATU enabled
            ui->tuneEnableChk->blockSignals(true);
            ui->tuneEnableChk->setChecked(true);
            ui->tuneEnableChk->blockSignals(false);
            showStatusBarText("ATU enabled.");
            break;
        case 0x02:
            // ATU tuning in-progress.
            // Add command queue to check again and update status bar
            // qInfo(logSystem()) << "Received ATU status update that *tuning* is taking place";
            showStatusBarText("ATU is Tuning...");
            queue->add(priorityHighest,funcTunerStatus);
            break;
        default:
            qInfo(logSystem()) << "Did not understand ATU status: " << (unsigned int) atustatus;
            break;
    }
}

void wfmain::on_connectBtn_clicked()
{
    this->rigStatus->setText(""); // Clear status

    if (connStatus == connDisconnected) {
        connectionHandler(true);
    }
    else
    {
        connectionHandler(false);
    }

    ui->connectBtn->clearFocus();
}

void wfmain::on_sqlSlider_valueChanged(int value)
{
    queue->addUnique(priorityImmediate,queueItem(funcSquelch,QVariant::fromValue<ushort>(value)));
}

void wfmain::on_transmitBtn_clicked()
{
    if(!amTransmitting)
    {
        // Currently receiving
        if(!prefs.enablePTT)
        {
            showStatusBarText("PTT is disabled, not sending command. Change under Settings tab.");
            return;
        }

        // Are we already PTT? Not a big deal, just send again anyway.
        showStatusBarText("Sending PTT ON command. Use Control-R to receive.");
        queue->add(priorityImmediate,queueItem(funcTransceiverStatus,QVariant::fromValue<bool>(true),false));

        // send PTT
        // Start 3 minute timer
        pttTimer->start();

    } else {
        // Currently transmitting
        queue->add(priorityImmediate,queueItem(funcTransceiverStatus,QVariant::fromValue<bool>(false),false));

        pttTimer->stop();
    }
}

void wfmain::setRadioTimeDatePrep()
{
    if(!waitingToSetTimeDate)
    {
        // 1: Find the current time and date
        QDateTime now;
        if(prefs.useUTC)
        {
            now = QDateTime::currentDateTimeUtc();
            now.setTime(QTime::currentTime());
        } else {
            now = QDateTime::currentDateTime();
            now.setTime(QTime::currentTime());
        }

        int second = now.time().second();

        // 2: Find how many mseconds until next minute
        int msecdelay = QTime::currentTime().msecsTo( QTime::currentTime().addSecs(60-second) );

        // 3: Compute time and date at one minute later
        QDateTime setpoint = now.addMSecs(msecdelay); // at HMS or possibly HMS + some ms. Never under though.

        // 4: Prepare data structs for the time at one minute later
        timesetpoint.hours = (unsigned char)setpoint.time().hour();
        timesetpoint.minutes = (unsigned char)setpoint.time().minute();
        datesetpoint.day = (unsigned char)setpoint.date().day();
        datesetpoint.month = (unsigned char)setpoint.date().month();
        datesetpoint.year = (uint16_t)setpoint.date().year();
        unsigned int utcOffsetSeconds = (unsigned int)abs(setpoint.offsetFromUtc());
        bool isMinus = setpoint.offsetFromUtc() < 0;
        utcsetting.hours = utcOffsetSeconds / 60 / 60;
        utcsetting.minutes = (utcOffsetSeconds - (utcsetting.hours*60*60) ) / 60;
        utcsetting.isMinus = isMinus;

        timeSync->setInterval(msecdelay);
        timeSync->setSingleShot(true);

        // 5: start one-shot timer for the delta computed in #2.
        timeSync->start();
        waitingToSetTimeDate = true;
        showStatusBarText(QString("Setting time, date, and UTC offset for radio in %1 seconds.").arg(msecdelay/1000));
    }
}

void wfmain::setRadioTimeDateSend()
{
    // Issue priority commands for UTC offset, date, and time
    // UTC offset must come first, otherwise the radio may "help" and correct for any changes.

    showStatusBarText(QString("Setting time, date, and UTC offset for radio now."));

    queue->add(priorityImmediate,queueItem(funcUTCOffset,QVariant::fromValue<timekind>(utcsetting)));
    queue->add(priorityImmediate,queueItem(funcTime,QVariant::fromValue<timekind>(timesetpoint)));
    queue->add(priorityImmediate,queueItem(funcDate,QVariant::fromValue<datekind>(datesetpoint)));

    waitingToSetTimeDate = false;
}

void wfmain::showAndRaiseWidget(QWidget *w)
{
    if(!w)
        return;

    if(w->isMinimized())
    {
        w->raise();
        w->activateWindow();
        return;
    }
    w->show();
    w->raise();
    w->activateWindow();
}

void wfmain::changeSliderQuietly(QSlider *slider, int value)
{
    if (slider->value() != value) {
        slider->blockSignals(true);
        slider->setValue(value);
        slider->blockSignals(false);
    }
}

void wfmain::statusFromSliderRaw(QString name, int rawValue)
{
    showStatusBarText(name + QString(": %1").arg(rawValue));
}

void wfmain::statusFromSliderPercent(QString name, int rawValue)
{
    showStatusBarText(name + QString(": %1%").arg((int)(100*rawValue/255.0)));
}

void wfmain::receiveTxPower(unsigned char power)
{
    changeSliderQuietly(ui->txPowerSlider, power);
    emit sendLevel(funcRFPower,power);
}

void wfmain::receiveMicGain(unsigned char gain)
{
    processModLevel(inputTypes(inputMic), gain);
    emit sendLevel(funcMicGain,gain);
}

void wfmain::processModLevel(inputTypes source, unsigned char level)
{
    rigInput currentIn;

    if (usingDataMode == 1) {
        currentIn = currentModData1Src;
    } else if (usingDataMode == 2) {
        currentIn = currentModData2Src;
    } else if (usingDataMode == 3) {
        currentIn = currentModData3Src;
    } else {
        currentIn = currentModDataOffSrc;
    }

    if(currentIn.type == source)
    {
        currentIn.level = level;
        changeSliderQuietly(ui->micGainSlider, level);
        emit sendLevel(funcLANModLevel,level);
    }

}

void wfmain::receiveModInput(rigInput input, unsigned char data)
{

    prefRsItem item= rs_all;
    // This will ONLY fire if the input type is different to the current one
    if (data == 0 && currentModDataOffSrc.type != input.type) {
        qInfo() << QString("Data: %0 Input: %1 current: %2").arg(data).arg(input.name).arg(currentModDataOffSrc.name);
        queue->del(getInputTypeCommand(currentModDataOffSrc.type));
        item = rs_dataOffMod;
        prefs.inputDataOff = input.type;
        currentModDataOffSrc = input;
    } else if (data == 1 && currentModData1Src.type != input.type) {
        qInfo() << QString("Data: %0 Input: %1 current: %2").arg(data).arg(input.name).arg(currentModData1Src.name);
        queue->del(getInputTypeCommand(currentModData1Src.type));
        item = rs_data1Mod;
        prefs.inputData1 = input.type;
        currentModData1Src = input;
    } else if (data == 2 && currentModData2Src.type != input.type) {
        qInfo() << QString("Data: %0 Input: %1 current: %2").arg(data).arg(input.name).arg(currentModData2Src.name);
        queue->del(getInputTypeCommand(currentModData2Src.type));
        item = rs_data2Mod;
        prefs.inputData2 = input.type;
        currentModData2Src = input;
    } else if (data == 3 && currentModData3Src.type != input.type) {
        qInfo() << QString("Data: %0 Input: %1 current: %2").arg(data).arg(input.name).arg(currentModData3Src.name);
        queue->del(getInputTypeCommand(currentModData3Src.type));
        item = rs_data3Mod;
        prefs.inputData3 = input.type;
        currentModData3Src = input;
    }


    if (item != rs_all)
    {
        setupui->updateRsPrefs(item);
        //if (ui->datamodeCombo->currentData().toUInt() == data) {
        //    queue->add(priorityImmediate,getInputTypeCommand(input.type),false);
        //    changeModLabel(input);
        //}

    }
}

void wfmain::receivePassband(quint16 pass)
{
    double pb = (double)(pass / 1000000.0);
    if (ui->mainScope->getPassbandWidth() != pb) {
        ui->mainScope->setPassbandWidth(pb);

        qInfo(logSystem()) << QString("Received new IF Filter/Passband %0 Hz").arg(pass);
        showStatusBarText(QString("IF filter width %0 Hz (%1 MHz)").arg(pass).arg(passbandWidth));
    }
}

void wfmain::receiveCwPitch(unsigned char pitch) {
    if (currentModeInfo.mk == modeCW || currentModeInfo.mk == modeCW_R) {
        quint16 p = round((((600.0 / 255.0) * pitch) + 300) / 5.0) * 5.0;
        if (p != cwPitch)
        {
            passbandCenterFrequency = p / 2000000.0;
            qInfo(logSystem()) << QString("Received new CW Pitch %0 Hz was %1 (center freq %2 MHz)").arg(p).arg(cwPitch).arg(passbandCenterFrequency);
            cwPitch = p;
            emit sendLevel(funcCwPitch,pitch);
        }
    }
}

void wfmain::receivePBTInner(unsigned char level) {
    /*
    * This is written like this as although PBT is supposed to be sent in 25Hz steps,
    * sometimes it sends the 'nearest' value. This results in math errors.
    * In CW mode, the value is also dependant on the CW Pitch setting
    */
    qint16 shift = (qint16)(level - 128);
    double tempVar = ceil((shift / 127.0) * ui->mainScope->getPassbandWidth() * 20000.0) / 20000.0;
    // tempVar now contains value to the nearest 50Hz If CW mode, add/remove the cwPitch.
    double pitch = 0.0;
    if ((currentModeInfo.mk == modeCW || currentModeInfo.mk == modeCW_R) && ui->mainScope->getPassbandWidth() > 0.0006)
    {
        pitch = (600.0 - cwPitch) / 1000000.0;
    }
    double inner = round((tempVar + pitch) * 200000.0) / 200000.0; // Nearest 5Hz.
    if (inner != PBTInner)
    {
        qInfo(logSystem()) << QString("Received new PBT Inner %0 MHz").arg(inner);
        PBTInner = inner;
        emit sendLevel(funcPBTInner,level);
    }
}

void wfmain::receivePBTOuter(unsigned char level) {
    /*
    * This is written like this as although PBT is supposed to be sent in 25Hz steps,
    * sometimes it sends the 'nearest' value. This results in math errors.
    * In CW mode, the value is also dependant on the CW Pitch setting
    */
    qint16 shift = (qint16)(level - 128);
    double tempVar = ceil((shift / 127.0) * ui->mainScope->getPassbandWidth() * 20000.0) / 20000.0;
    // tempVar now contains value to the nearest 50Hz If CW mode, add/remove the cwPitch.
    double pitch = 0.0;
    if ((currentModeInfo.mk == modeCW || currentModeInfo.mk == modeCW_R) && ui->mainScope->getPassbandWidth() > 0.0006)
    {
        pitch = (600.0 - cwPitch) / 1000000.0;
    }
    double outer = round((tempVar + pitch) * 200000.0) / 200000.0; // Nearest 5Hz.
    if (outer != PBTOuter)
    {
        qInfo(logSystem()) << QString("Received new PBT Outer %0 MHz").arg(outer);
        PBTOuter = outer;
        emit sendLevel(funcPBTOuter,level);
    }
}

void wfmain::receiveTuningStep(unsigned char step)
{
    if (step > 0)
    {
        foreach (auto s, rigCaps.steps)
        {
            if (step == s.num && ui->tuningStepCombo->currentData().toUInt() != s.hz) {
                qInfo(logSystem()) << QString("Received new Tuning Step %0").arg(s.name);
                ui->tuningStepCombo->setCurrentIndex(ui->tuningStepCombo->findData(s.hz));
                ui->mainScope->setStepSize(s.hz);
                ui->subScope->setStepSize(s.hz);
                break;
            }
        }
    }
}

void wfmain::receiveMeter(meter_t inMeter, unsigned char level)
{

    switch(inMeter)
    {
        case meterS:
            ui->meterSPoWidget->setMeterType(meterS);
            ui->meterSPoWidget->setLevel(level);
            ui->meterSPoWidget->repaint();
            break;
        case meterPower:
            ui->meterSPoWidget->setMeterType(meterPower);
            ui->meterSPoWidget->setLevel(level);
            ui->meterSPoWidget->update();
            break;
        default:
            if(ui->meter2Widget->getMeterType() == inMeter)
            {
                ui->meter2Widget->setLevel(level);
            } else if ( (ui->meter2Widget->getMeterType() == meterAudio) &&
                        (inMeter == meterTxMod) && amTransmitting) {
                ui->meter2Widget->setLevel(level);
            } else if (  (ui->meter2Widget->getMeterType() == meterAudio) &&
                         (inMeter == meterRxAudio) && !amTransmitting) {
                ui->meter2Widget->setLevel(level);
            }
            break;
    }
}

void wfmain::receiveCompLevel(unsigned char compLevel)
{
    emit sendLevel(funcCompressorLevel,compLevel);
}

void wfmain::receiveMonitorGain(unsigned char monitorGain)
{
    changeSliderQuietly(ui->monitorSlider, monitorGain);
    emit sendLevel(funcMonitorGain,monitorGain);
}

void wfmain::receiveVoxGain(unsigned char voxGain)
{
    emit sendLevel(funcVoxGain,voxGain);
}

void wfmain::receiveAntiVoxGain(unsigned char antiVoxGain)
{
    emit sendLevel(funcAntiVoxGain,antiVoxGain);
}

void wfmain::receiveNBLevel(unsigned char level)
{
    emit sendLevel(funcNBLevel,level);
}

void wfmain::receiveNRLevel(unsigned char level)
{
    emit sendLevel(funcNRLevel,level);
}


void wfmain::receiveComp(bool en)
{
    emit sendLevel(funcCompressor,en);
}

void wfmain::receiveMonitor(bool en)
{
    if (en)
            ui->monitorLabel->setText(QString("<b><a href=\"#\" style=\"color:%0; text-decoration:none;\">Mon</a></b>").arg(colorPrefs->textColor.name()));
    else
        ui->monitorLabel->setText(QString("<a href=\"#\" style=\"color:%0; text-decoration:none;\">Mon</a>").arg(colorPrefs->textColor.name()));
    emit sendLevel(funcMonitor,en);
}

void wfmain::receiveVox(bool en)
{
    emit sendLevel(funcVox,en);
}

void wfmain::receiveNB(bool en)
{
    emit sendLevel(funcNoiseBlanker,en);
}

void wfmain::receiveNR(bool en)
{
    emit sendLevel(funcNoiseReduction,en);
}

void wfmain::on_txPowerSlider_valueChanged(int value)
{
    queue->addUnique(priorityImmediate,queueItem(funcRFPower,QVariant::fromValue<ushort>(value),false));
}

void wfmain::on_micGainSlider_valueChanged(int value)
{
    processChangingCurrentModLevel((unsigned char) value);
}


void wfmain::changeModLabelAndSlider(rigInput source)
{
    changeModLabel(source, true);
}

void wfmain::changeModLabel(rigInput input)
{
    changeModLabel(input, false);
}

void wfmain::changeModLabel(rigInput input, bool updateLevel)
{

    queue->add(priorityMedium,getInputTypeCommand(input.type),true);

    ui->modSliderLbl->setText(input.name);

    if(updateLevel)
    {
        changeSliderQuietly(ui->micGainSlider, input.level);
    }
}

void wfmain::processChangingCurrentModLevel(unsigned char level)
{
    // slider moved, so find the current mod and issue the level set command.

    funcs f = funcNone;
    if(usingDataMode == 0)
    {
        f = getInputTypeCommand(currentModDataOffSrc.type);
    } else if (usingDataMode == 1) {
        f = getInputTypeCommand(currentModData1Src.type);
    } else if (usingDataMode == 2) {
        f = getInputTypeCommand(currentModData2Src.type);
    } else if (usingDataMode == 3) {
        f = getInputTypeCommand(currentModData3Src.type);
    }

    queue->addUnique(priorityImmediate,queueItem(f,QVariant::fromValue<ushort>(level),false));
}

void wfmain::on_tuneLockChk_clicked(bool checked)
{
    freqLock = checked;
}

void wfmain::on_rptSetupBtn_clicked()
{
    if(rpt->isMinimized())
    {
        rpt->raise();
        rpt->activateWindow();
        return;
    }
    rpt->show();
    rpt->raise();
    rpt->activateWindow();
}

void wfmain::on_attSelCombo_activated(int index)
{
    queue->add(priorityImmediate,queueItem(funcAttenuator,QVariant::fromValue<uchar>(ui->attSelCombo->itemData(index).toInt()),false));
    queue->add(priorityHigh,funcPreamp,false);
}

void wfmain::on_preampSelCombo_activated(int index)
{
    queue->add(priorityImmediate,queueItem(funcPreamp,QVariant::fromValue<uchar>(ui->preampSelCombo->itemData(index).toInt()),false));
    queue->add(priorityHigh,funcAttenuator,false);
}

void wfmain::on_antennaSelCombo_activated(int index)
{
    antennaInfo ant;
    ant.antenna = (unsigned char)ui->antennaSelCombo->itemData(index).toInt();
    ant.rx = ui->rxAntennaCheck->isChecked();
    queue->add(priorityImmediate,queueItem(funcAntenna,QVariant::fromValue<antennaInfo>(ant),false));
}

void wfmain::on_rxAntennaCheck_clicked(bool value)
{
    antennaInfo ant;
    ant.antenna = (unsigned char)ui->antennaSelCombo->currentData().toInt();
    ant.rx = value;
    queue->add(priorityImmediate,queueItem(funcAntenna,QVariant::fromValue<antennaInfo>(ant),false));
}

void wfmain::receivePreamp(unsigned char pre)
{
    ui->preampSelCombo->setCurrentIndex(ui->preampSelCombo->findData(pre));
}

void wfmain::receiveAttenuator(unsigned char att)
{
    ui->attSelCombo->setCurrentIndex(ui->attSelCombo->findData(att));
}

void wfmain::receiveAntennaSel(unsigned char ant, bool rx)
{
    ui->antennaSelCombo->setCurrentIndex(ant);
    ui->rxAntennaCheck->setChecked(rx);
}

void wfmain::calculateTimingParameters()
{
    // Function for calculating polling parameters.
    // Requires that we know the "baud rate" of the actual
    // radio connection.

    // baud on the serial port reflects the actual rig connection,
    // even if a client-server connection is being used.
    // Computed time for a 10 byte message, with a safety factor of 2.

    if (prefs.serialPortBaud == 0)
    {
        prefs.serialPortBaud = 9600;
        qInfo(logSystem()) << "WARNING: baud rate received was zero. Assuming 9600 baud, performance may suffer.";
    }

    unsigned int usPerByte = 9600*1000 / prefs.serialPortBaud;
    unsigned int msMinTiming=usPerByte * 10*2/1000;
    if(msMinTiming < 25)
        msMinTiming = 25;

    if(haveRigCaps && rigCaps.hasFDcomms)
    {
        delayedCommand->setInterval( msMinTiming); // 20 byte message
        queue->interval(msMinTiming);
    } else {
        delayedCommand->setInterval( msMinTiming * 4); // Multiply by 4 to allow rigMemories to be received.
        queue->interval(msMinTiming * 4);
    }

    qInfo(logSystem()) << "Delay command interval timing: " << delayedCommand->interval() << "ms";

    // Normal:
    delayedCmdIntervalLAN_ms =  delayedCommand->interval();
    delayedCmdIntervalSerial_ms = delayedCommand->interval();

    // startup initial state:
    delayedCmdStartupInterval_ms =  delayedCommand->interval() * 3;
}

void wfmain::receiveBaudRate(quint32 baud)
{
    qInfo() << "Received serial port baud rate from remote server:" << baud;
    prefs.serialPortBaud = baud;
    calculateTimingParameters();
}

void wfmain::on_rigPowerOnBtn_clicked()
{
    powerRigOn();
}

void wfmain::on_rigPowerOffBtn_clicked()
{
    // Are you sure?
    if (!prefs.confirmPowerOff) {
        powerRigOff();
        return;
    }
    QCheckBox* cb = new QCheckBox("Don't ask me again");
    QMessageBox msgbox;
    msgbox.setWindowTitle("Power");
    msgbox.setText("Power down the radio?\n");
    msgbox.setIcon(QMessageBox::Icon::Question);
    QAbstractButton* yesButton = msgbox.addButton(QMessageBox::Yes);
    msgbox.addButton(QMessageBox::No);
    msgbox.setDefaultButton(QMessageBox::Yes);
    msgbox.setCheckBox(cb);

    QObject::connect(cb, &QCheckBox::stateChanged, [this](int state) {
        if (static_cast<Qt::CheckState>(state) == Qt::CheckState::Checked) {
            prefs.confirmPowerOff = false;
        }
        else {
            prefs.confirmPowerOff = true;
        }
        settings->beginGroup("Interface");
        settings->setValue("ConfirmPowerOff", this->prefs.confirmPowerOff);
        settings->endGroup();
        settings->sync();
    });

    msgbox.exec();

    if (msgbox.clickedButton() == yesButton) {
        powerRigOff();
    }
}

void wfmain::powerRigOn()
{

    emit sendPowerOn();

    delayedCommand->setInterval(3000); // 3 seconds
    //if(ui->scopeEnableWFBtn->checkState() != Qt::Unchecked)
    //{
        //issueDelayedCommand(cmdDispEnable);
        //issueDelayedCommand(cmdQueNormalSpeed);
        //issueDelayedCommand(cmdSpecOn);
        //issueDelayedCommand(cmdStartRegularPolling); // s-meter, etc
    //} else {
        //issueDelayedCommand(cmdQueNormalSpeed);
        //issueDelayedCommand(cmdSpecOff);
        //issueDelayedCommand(cmdStartRegularPolling); // s-meter, etc
    //}
    delayedCommand->start();
    calculateTimingParameters(); // Set queue interval

}

void wfmain::powerRigOff()
{
    delayedCommand->stop();
    queue->interval(0);

    emit sendPowerOff();
}

void wfmain::on_ritTuneDial_valueChanged(int value)
{
    queue->add(priorityImmediate,queueItem(funcRITFreq,QVariant::fromValue(short(value))));
}

void wfmain::on_ritEnableChk_clicked(bool checked)
{
    queue->add(priorityImmediate,queueItem(funcRitStatus,QVariant::fromValue(checked)));
}

void wfmain::receiveRITStatus(bool ritEnabled)
{
    ui->ritEnableChk->blockSignals(true);
    ui->ritEnableChk->setChecked(ritEnabled);
    ui->ritEnableChk->blockSignals(false);
}

void wfmain::receiveRITValue(int ritValHz)
{
    if((ritValHz > -500) && (ritValHz < 500))
    {
        ui->ritTuneDial->blockSignals(true);
        ui->ritTuneDial->setValue(ritValHz);
        ui->ritTuneDial->blockSignals(false);
    } else {
        qInfo(logSystem()) << "Warning: out of range RIT value received: " << ritValHz << " Hz";
    }
}

void wfmain::showButton(QPushButton *btn)
{
    btn->setHidden(false);
}

void wfmain::hideButton(QPushButton *btn)
{
    btn->setHidden(true);
}

funcs wfmain::meter_tToMeterCommand(meter_t m)
{
    funcs c;
    switch(m)
    {

    case meterNone:
            c = funcNone;
            break;
        case meterS:
            c = funcSMeter;
            break;
        case meterCenter:
            c = funcCenterMeter;
            break;
        case meterPower:
            c = funcPowerMeter;
            break;
        case meterSWR:
            c = funcSWRMeter;
            break;
        case meterALC:
            c = funcALCMeter;
            break;
        case meterComp:
            c = funcCompMeter;
            break;
        case meterCurrent:
            c = funcIdMeter;
            break;
        case meterVoltage:
            c = funcVdMeter;
            break;
        default:
            c = funcNone;
            break;
    }

    return c;
}


void wfmain::changeMeter2Type(meter_t m)
{
    meter_t newMeterType;
    meter_t oldMeterType;
    newMeterType = m;
    oldMeterType = ui->meter2Widget->getMeterType();

    if(newMeterType == oldMeterType)
        return;

    funcs newCmd = meter_tToMeterCommand(newMeterType);
    funcs oldCmd = meter_tToMeterCommand(oldMeterType);

    //removePeriodicCommand(oldCmd);
    if (oldCmd != funcSMeter && oldCmd != funcNone)
        queue->del(oldCmd);


    if(newMeterType==meterNone)
    {
        ui->meter2Widget->hide();
        ui->meter2Widget->setMeterType(newMeterType);
    } else {
        ui->meter2Widget->show();
        ui->meter2Widget->setMeterType(newMeterType);
        if((newMeterType!=meterRxAudio) && (newMeterType!=meterTxMod) && (newMeterType!=meterAudio))
            queue->add(priorityHighest,queueItem(newCmd,true,false));
    }
}

void wfmain::enableRigCtl(bool enabled)
{
    // migrated to this, keep
    if (rigCtl != Q_NULLPTR)
    {
        rigCtl->disconnect();
        delete rigCtl;
        rigCtl = Q_NULLPTR;
    }

    if (enabled) {
        // Start rigctld
        rigCtl = new rigCtlD(this);
        rigCtl->startServer(prefs.rigCtlPort);
        connect(this, SIGNAL(sendRigCaps(rigCapabilities)), rigCtl, SLOT(receiveRigCaps(rigCapabilities)));
        if (rig != Q_NULLPTR) {
            // We are already connected to a rig.
            emit sendRigCaps(rigCaps);
        }
    }
}

void wfmain::radioSelection(QList<radio_cap_packet> radios)
{
    selRad->populate(radios);
}

void wfmain::on_radioStatusBtn_clicked()
{
    if (selRad->isVisible())
    {
        selRad->hide();
    }
    else
    {
        selRad->show();
    }
}

void wfmain::setAudioDevicesUI()
{

}


// --- DEBUG FUNCTION ---
void wfmain::debugBtn_clicked()
{
    qInfo(logSystem()) << "Debug button pressed.";
    //qDebug(logSystem()) << "Query for repeater access mode (tone, tsql, etc) sent.";
    //issueDelayedCommand(cmdGetRptAccessMode);
    debugWindow* debug = new debugWindow();
    debug->setAttribute(Qt::WA_DeleteOnClose);
    debug->show();

    qInfo(logSystem()) << "Scroll wheel debug: ";
    qInfo(logSystem()) << "tick interval: " << ui->afGainSlider->tickInterval();
    qInfo(logSystem()) << "page step: " << ui->afGainSlider->pageStep();
    qInfo(logSystem()) << "single step: " << ui->afGainSlider->singleStep();
    qInfo(logSystem()) << "QApp wheel scroll lines: " << QApplication::wheelScrollLines();
    qInfo(logSystem()) << "QStyleHint wheel scroll lines: " <<  QApplication::styleHints()->wheelScrollLines();


    //showAndRaiseWidget(setupui);
    //setupui->updateIfPrefs((int)if_all);
    //setupui->updateRaPrefs((int)ra_all);
    //setupui->updateCtPrefs((int)ct_all);
    //setupui->updateClusterPrefs((int)cl_all);
    //setupui->updateUdpPrefs((int)u_all);
}

// ----------   color helper functions:   ---------- //

void wfmain::useColorPreset(colorPrefsType *cp)
{
    // Apply the given preset to the UI elements
    // prototyped from setPlotTheme()
    if(cp == Q_NULLPTR)
        return;

    //qInfo(logSystem()) << "Setting plots to color preset number " << cp->presetNum << ", with name " << *(cp->presetName);
    ui->meterSPoWidget->setColors(cp->meterLevel, cp->meterPeakScale, cp->meterPeakLevel, cp->meterAverage, cp->meterLowerLine, cp->meterLowText);
    ui->meter2Widget->setColors(cp->meterLevel, cp->meterPeakScale, cp->meterPeakLevel, cp->meterAverage, cp->meterLowerLine, cp->meterLowText);
    ui->mainScope->colorPreset(cp);
    ui->subScope->colorPreset(cp);
    if (this->colorPrefs != Q_NULLPTR)
        delete this->colorPrefs;
    this->colorPrefs = new colorPrefsType(*cp);
    qInfo() << "Setting color Preset" << cp->presetNum << "name" << cp->presetName;
}


void wfmain::setDefaultColorPresets()
{
    // Default wfview colors in each preset
    // gets overridden after preferences are loaded
    for(int pn=0; pn < numColorPresetsTotal; pn++)
    {
        setDefaultColors(pn);
    }
}



void wfmain::on_showLogBtn_clicked()
{
    if(logWindow->isMinimized())
    {
        logWindow->raise();
        logWindow->activateWindow();
        return;
    }
    logWindow->show();
    logWindow->raise();
    logWindow->activateWindow();
}

void wfmain::initLogging()
{
    // Set the logging file before doing anything else.
    m_logFile.reset(new QFile(logFilename));
    // Open the file logging
    m_logFile.data()->open(QFile::WriteOnly | QFile::Truncate | QFile::Text);
    // Set handler
    qInstallMessageHandler(messageHandler);

    connect(logWindow, SIGNAL(setDebugMode(bool)), this, SLOT(setDebugLogging(bool)));

    // Interval timer for log window updates:
    logCheckingTimer.setInterval(100);
    connect(&logCheckingTimer, SIGNAL(timeout()), this, SLOT(logCheck()));
    logCheckingTimer.start();
}

void wfmain::logCheck()
{
    // This is called by a timer to check for new log messages and copy
    // the messages into the logWindow.
    QMutexLocker locker(&logTextMutex);
    int size = logStringBuffer.size();
    for(int i=0; i < size; i++)
    {
        handleLogText(logStringBuffer.back());
        logStringBuffer.pop_back();
    }
}

void wfmain::handleLogText(QString text)
{
    // This function is just a pass-through
    logWindow->acceptLogText(text);
}

void wfmain::setDebugLogging(bool debugModeOn)
{
    this->debugMode = debugModeOn;
    debugModeLogging = debugModeOn;
}

void wfmain::messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    // Open stream file writes

    if (type == QtDebugMsg && !debugModeLogging)
    {
        return;
    }

    QMutexLocker locker(&logMutex);
    QTextStream out(m_logFile.data());
    QString text;

    // Write the date of recording
    out << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ");
    text.append(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz "));
    // By type determine to what level belongs message

    switch (type)
    {
        case QtDebugMsg:
            out << "DBG ";
            text.append("DBG ");
            break;
        case QtInfoMsg:
            out << "INF ";
            text.append("INF ");
            break;
        case QtWarningMsg:
            out << "WRN ";
            text.append("WRN ");
            break;
        case QtCriticalMsg:
            out << "CRT ";
            text.append("CRT ");
            break;
        case QtFatalMsg:
            out << "FTL ";
            text.append("FLT ");
            break;
    }
    // Write to the output category of the message and the message itself
    out << context.category << ": " << msg << "\n";
    out.flush();    // Clear the buffered data

    text.append(context.category);
    text.append(": ");
    text.append(msg);
    logTextMutex.lock();
    logStringBuffer.push_front(text);
    logTextMutex.unlock();

}

void wfmain::receiveClusterOutput(QString text) {
    setupui->insertClusterOutputText(text);
}



void wfmain::changePollTiming(int timing_ms, bool setUI)
{
    queue->interval(timing_ms);
    qInfo(logSystem()) << "User changed radio polling interval to " << timing_ms << "ms.";
    showStatusBarText("User changed radio polling interval to " + QString("%1").arg(timing_ms) + "ms.");
    prefs.polling_ms = timing_ms;
    (void)setUI;
}

void wfmain::connectionHandler(bool connect)
{
    queue->clear();
    emit sendCloseComm();
    haveRigCaps = false;
    rigName->setText("NONE");
    connStatus = connDisconnected;

    emit connectionStatus(connect); // Signal any other parts that need to know if we are connecting/connected.

    if (connect) {
        openRig();
    } else {
        ui->connectBtn->setText("Connect to Radio");
        removeRig();
    }

    // Whatever happened, make sure we delete the memories window.
    if (memWindow != Q_NULLPTR) {
        delete memWindow;
        memWindow = Q_NULLPTR;
    }


}

void wfmain::on_cwButton_clicked()
{
    if(cw->isMinimized())
    {
        cw->raise();
        cw->activateWindow();
        return;
    }
    cw->show();
    cw->raise();
    cw->activateWindow();
}

void wfmain::on_memoriesBtn_clicked()
{
    if (haveRigCaps) {
        if (memWindow == Q_NULLPTR) {
            // Add slowload option for background loading.
            memWindow = new memories(rigCaps,false,this);
            this->memWindow->connect(this, SIGNAL(haveMemory(memoryType)), memWindow, SLOT(receiveMemory(memoryType)));

            this->memWindow->connect(this->memWindow, &memories::getMemory, rig,[=](const quint32 &mem) {
                queue->add(priorityImmediate,queueItem(funcMemoryContents,QVariant::fromValue<uint>(mem)));
            });

            this->memWindow->connect(this->memWindow, &memories::getSatMemory, rig, [=](const quint32 &mem) {
                queue->add(priorityImmediate,queueItem(funcSatelliteMemory,QVariant::fromValue<ushort>(mem & 0xffff)));
            });

            this->memWindow->connect(this->memWindow, &memories::setMemory, rig, [=](const memoryType &mem) {
                queue->add(priorityImmediate,queueItem((mem.sat?funcSatelliteMemory:funcMemoryContents),QVariant::fromValue<memoryType>(mem)));
            });

            this->memWindow->connect(this->memWindow, &memories::clearMemory, rig, [=](const quint32 &mem) {
                queue->add(priorityImmediate,queueItem(funcMemoryContents,QVariant::fromValue<uint>(mem)));
            });

            this->memWindow->connect(this->memWindow, &memories::recallMemory, rig, [=](const quint32 &mem) {
                queue->add(priorityImmediate,queueItem(funcMemoryMode,QVariant::fromValue<uint>(mem)));
            });

            this->memWindow->connect(this->memWindow, &memories::setBand, rig, [=](const char &band) {
                queue->add(priorityImmediate,queueItem(funcBandStackReg,QVariant::fromValue<uchar>(band)));
            });

            this->memWindow->connect(this->memWindow, &memories::memoryMode, rig, [=]() {
                queue->add(priorityImmediate,funcMemoryMode);
                queue->del(funcSelectedFreq,false);
                queue->del(funcSelectedMode,false);
                queue->del(funcUnselectedFreq,true);
                queue->del(funcUnselectedMode,true);
            });

            this->memWindow->connect(this->memWindow, &memories::vfoMode, rig, [this]() {
                queue->addUnique(priorityMedium,funcSelectedFreq,true,false);
                queue->addUnique(priorityMedium,funcSelectedMode,true,false);
                queue->addUnique(priorityMedium,funcUnselectedFreq,true,true);
                queue->addUnique(priorityMedium,funcUnselectedMode,true,true);
            });

            this->memWindow->connect(this->memWindow, &memories::setSatelliteMode, rig, [this](const bool &en) {
                queue->add(priorityImmediate,queueItem(funcSatelliteMode,QVariant::fromValue<bool>(en)));
                if (en) {
                    queue->del(funcSelectedFreq,false);
                    queue->del(funcSelectedMode,false);
                    queue->del(funcUnselectedFreq,true);
                    queue->del(funcUnselectedMode,true);
                } else {
                    queue->addUnique(priorityMedium,funcSelectedFreq,true,false);
                    queue->addUnique(priorityMedium,funcSelectedMode,true,false);
                    queue->addUnique(priorityMedium,funcUnselectedFreq,true,true);
                    queue->addUnique(priorityMedium,funcUnselectedMode,true,true);
                }
            });

            memWindow->populate(); // Call populate to get the initial memories
        }
        memWindow->show();
    } else {
        if (memWindow != Q_NULLPTR)
        {
            delete memWindow;
            memWindow = Q_NULLPTR;
        }
    }
}

void wfmain::on_rigCreatorBtn_clicked()
{
    rigCreator* create = new rigCreator();
    create->setAttribute(Qt::WA_DeleteOnClose);
    create->show();
}


void wfmain::receiveValue(cacheItem val){


    switch (val.command)
    {
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
    case funcUnselectedFreq:
        val.sub=true;
    case funcFreqGet:
    case funcFreqTR:
    case funcSelectedFreq:
    {
        freqt f = val.value.value<freqt>();

        if (!val.sub)
        {
            ui->mainScope->setFrequency(f);
        } else {
            ui->subScope->setFrequency(f);
        }
    }
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif
    case funcReadTXFreq:
        break;
    case funcVFODualWatch:
        ui->dualWatchBtn->setChecked(val.value.value<bool>());
        break;
    case funcModeGet:
    case funcModeTR:
    case funcSelectedMode:
        ui->mainScope->receiveMode(val.value.value<modeInfo>());
        break;
    case funcUnselectedMode:
        val.sub=true;
        ui->subScope->receiveMode(val.value.value<modeInfo>());
        break;
    case funcSatelliteMemory:
    case funcMemoryContents:
        emit haveMemory(val.value.value<memoryType>());
        break;
    case funcMemoryClear:
    case funcMemoryKeyer:
    case funcMemoryToVFO:
    case funcMemoryWrite:
        break;
    case funcScanning:
        break;
    case funcReadFreqOffset:
        break;
    case funcSplitStatus:
        rpt->receiveDuplexMode(val.value.value<duplexMode_t>());
        break;
    case funcTuningStep:
        receiveTuningStep(val.value.value<uchar>());
        break;
    case funcAttenuator:
        receiveAttenuator(val.value.value<uchar>());
        break;
    case funcAntenna:
        receiveAntennaSel(val.value.value<antennaInfo>().antenna,val.value.value<antennaInfo>().rx);
        break;
    case funcAfGain:
        receiveAfGain(val.value.value<uchar>());
        break;
    case funcRfGain:
        receiveRfGain(val.value.value<uchar>());
        break;
    case funcSquelch:
        receiveSql(val.value.value<uchar>());
        break;
    case funcAPFLevel:
        break;
    case funcNRLevel:
        receiveNRLevel(val.value.value<uchar>());
        break;
    case funcPBTOuter:
    case funcPBTInner:
    {
        uchar level = val.value.value<uchar>();
        qint16 shift = (qint16)(level - 128);
        double tempVar = ceil((shift / 127.0) * ui->mainScope->getPassbandWidth() * 20000.0) / 20000.0;
        // tempVar now contains value to the nearest 50Hz If CW mode, add/remove the cwPitch.
        double pitch = 0.0;
        if ((currentModeInfo.mk == modeCW || currentModeInfo.mk == modeCW_R) && ui->mainScope->getPassbandWidth() > 0.0006)
        {
                pitch = (600.0 - cwPitch) / 1000000.0;
        }
        double newVal = round((tempVar + pitch) * 200000.0) / 200000.0; // Nearest 5Hz.

        if (val.command == funcPBTInner) {
            if (val.sub) {
                ui->subScope->setPBTInner(newVal);
            } else {
                ui->mainScope->setPBTInner(newVal);
            }
        } else {
            if (val.sub) {
                ui->subScope->setPBTOuter(newVal);
            } else {
                ui->mainScope->setPBTOuter(newVal);
            }
        }
        break;
    }
    case funcIFShift:
        receiveIFShift(val.value.value<uchar>());
        break;
        /*
    connect(this->rig, &rigCommander::haveDashRatio,
        [=](const unsigned char& ratio) { cw->handleDashRatio(ratio); });
    connect(this->rig, &rigCommander::haveCWBreakMode,
            [=](const unsigned char &bm) { cw->handleBreakInMode(bm);});
*/
    case funcCwPitch:
        // There is only a single CW Pitch setting, so send to both scopes.
        ui->mainScope->receiveCwPitch(val.value.value<uchar>());
        ui->subScope->receiveCwPitch(val.value.value<uchar>());
        //receiveCwPitch(val.value.value<uchar>());
        cw->handlePitch(val.value.value<uchar>());
        break;
    case funcRFPower:
        receiveTxPower(val.value.value<uchar>());
        break;
    case funcMicGain:
        processModLevel(inputMic,val.value.value<uchar>());
        break;
    case funcKeySpeed:
        // Only used by CW window
        cw->handleKeySpeed(val.value.value<uchar>());
        break;
    case funcNotchFilter:
        break;
    case funcCompressorLevel:
        receiveCompLevel(val.value.value<uchar>());
        break;
    case funcBreakInDelay:
        break;
    case funcNBLevel:
        receiveNBLevel(val.value.value<uchar>());
        break;
    case funcDigiSelShift:
        break;
    case funcDriveGain:
        break;
    case funcMonitorGain:
        receiveMonitorGain(val.value.value<uchar>());
        break;
    case funcVoxGain:
        break;
    case funcAntiVoxGain:
        break;
    // 0x15 Meters
    case funcSMeterSqlStatus:
        break;
    case funcSMeter:
        receiveMeter(meter_t::meterS,val.value.value<uchar>());
        break;
    case funcVariousSql:
        break;
    case funcOverflowStatus:
        ui->mainScope->overflow(val.value.value<bool>());
        ui->subScope->overflow(val.value.value<bool>());
        break;
    case funcCenterMeter:
        receiveMeter(meter_t::meterCenter,val.value.value<uchar>());
        break;
    case funcPowerMeter:
        receiveMeter(meter_t::meterPower,val.value.value<uchar>());
        break;
    case funcSWRMeter:
        receiveMeter(meter_t::meterSWR,val.value.value<uchar>());
        break;
    case funcALCMeter:
        receiveMeter(meter_t::meterALC,val.value.value<uchar>());
        break;
    case funcCompMeter:
        receiveMeter(meter_t::meterComp,val.value.value<uchar>());
        break;
    case funcVdMeter:
        receiveMeter(meter_t::meterVoltage,val.value.value<uchar>());
        break;
    case funcIdMeter:
        receiveMeter(meter_t::meterCurrent,val.value.value<uchar>());
        break;
    // 0x16 enable/disable functions:
    case funcPreamp:
        receivePreamp(val.value.value<uchar>());
        break;
    case funcAGCTime:
        break;
    case funcNoiseBlanker:
        receiveNB(val.value.value<bool>());
        break;
    case funcAudioPeakFilter:
        break;
    case funcNoiseReduction:
        receiveNR(val.value.value<bool>());
        break;
    case funcAutoNotch:
        break;
    case funcRepeaterTone:
        rpt->handleRptAccessMode(rptAccessTxRx_t((val.value.value<bool>())?ratrTONEon:ratrTONEoff));
        break;
    case funcRepeaterTSQL:
        rpt->handleRptAccessMode(rptAccessTxRx_t((val.value.value<bool>())?ratrTSQLon:ratrTSQLoff));
        break;
    case funcRepeaterDTCS:
        break;
    case funcRepeaterCSQL:
        break;
    case funcCompressor:
        receiveComp(val.value.value<bool>());
        break;
    case funcMonitor:
        receiveMonitor(val.value.value<bool>());
        break;
    case funcVox:
        break;
    case funcManualNotch:
        break;
    case funcDigiSel:
        break;
    case funcTwinPeakFilter:
        break;
    case funcDialLock:
        break;
    case funcRXAntenna:
        ui->rxAntennaCheck->setChecked(val.value.value<bool>());
        break;
    case funcDSPIFFilter:
        break;
    case funcManualNotchWidth:
        break;
    case funcSSBTXBandwidth:
        break;
    case funcMainSubTracking:
        break;
    case funcToneSquelchType:
        break;
    case funcIPPlus:
        break;
    case funcBreakIn:
        cw->handleBreakInMode(val.value.value<uchar>());
        break;
    // 0x17 is CW send and 0x18 is power control (no reply)
    // 0x19 it automatically added.
    case funcTransceiverId:
        break;
    // 0x1a
    case funcBandStackReg:
    {
        bandStackType bsr = val.value.value<bandStackType>();
        qInfo(logSystem()) << __func__ << "BSR received into main: Freq: " << bsr.freq.Hz << ", mode: " << bsr.mode << ", filter: " << bsr.filter << ", data mode: " << bsr.data;

        queue->add(priorityImmediate,queueItem(funcSelectedFreq,QVariant::fromValue<freqt>(bsr.freq),false));

        foreach (auto md, rigCaps.modes)
        {
                if (md.reg == bsr.mode) {
                    md.filter=bsr.filter;
                    md.data=bsr.data;
                    queue->add(priorityImmediate,queueItem((rigCaps.commands.contains(funcSelectedMode)?funcSelectedMode:funcModeSet),QVariant::fromValue<modeInfo>(md),false));
                    queue->add(priorityImmediate,queueItem((rigCaps.commands.contains(funcSelectedMode)?funcNone:funcDataModeWithFilter),QVariant::fromValue<modeInfo>(md),false));

                    break;
                }
        }
        break;
    }
    case funcFilterWidth:
        if (val.sub)
            ui->subScope->receivePassband(val.value.value<ushort>());
        else
            ui->mainScope->receivePassband(val.value.value<ushort>());
        //receivePassband(val.value.value<ushort>());
        break;
    case funcDataModeWithFilter:
        if (val.sub)
            ui->subScope->receiveMode(val.value.value<modeInfo>());
        else
            ui->mainScope->receiveMode(val.value.value<modeInfo>());
        break;
    case funcAFMute:
        break;
    // 0x1a 0x05 various registers!
    case funcREFAdjust:
        break;
    case funcREFAdjustFine:
        //break;
    case funcACCAModLevel:
        processModLevel(inputACCA,val.value.value<uchar>());
        break;
    case funcACCBModLevel:
        processModLevel(inputACCB,val.value.value<uchar>());
        break;
    case funcUSBModLevel:
        processModLevel(inputUSB,val.value.value<uchar>());
        break;
    case funcSPDIFModLevel:
        processModLevel(inputSPDIF,val.value.value<uchar>());
        break;
    case funcLANModLevel:
        processModLevel(inputLAN,val.value.value<uchar>());
        break;
    case funcDATAOffMod:
        receiveModInput(val.value.value<rigInput>(), 0);
        break;
    case funcDATA1Mod:
        receiveModInput(val.value.value<rigInput>(), 1);
        break;
    case funcDATA2Mod:
        receiveModInput(val.value.value<rigInput>(), 2);
        break;
    case funcDATA3Mod:
        receiveModInput(val.value.value<rigInput>(), 3);
        break;
    case funcDashRatio:
        cw->handleDashRatio(val.value.value<uchar>());
        break;
    // 0x1b register
    case funcToneFreq:
        rpt->handleTone(val.value.value<toneInfo>().tone);
        break;
    case funcTSQLFreq:
        rpt->handleTSQL(val.value.value<toneInfo>().tone);
        break;
    case funcDTCSCode:
    {
        toneInfo t = val.value.value<toneInfo>();
        rpt->handleDTCS(t.tone,t.rinv,t.tinv);
        break;
    }
    case funcCSQLCode:
        break;
    // 0x1c register
    case funcRitStatus:
        receiveRITStatus(val.value.value<bool>());
        break;
    case funcTransceiverStatus:
        receivePTTstatus(val.value.value<bool>());
        break;
    case funcTunerStatus:
        receiveATUStatus(val.value.value<uchar>());
        break;
    // 0x21 RIT:
    case funcRITFreq:
        receiveRITValue(val.value.value<short>());
        break;
    // 0x27
    case funcScopeMainWaveData:
        ui->mainScope->update(val.value.value<scopeData>());
        break;
    case funcScopeSubWaveData:
        ui->subScope->update(val.value.value<scopeData>());
        break;
    case funcScopeOnOff:
        // confirming scope is on
        break;
    case funcScopeDataOutput:
        // confirming output enabled/disabled of wf data.
        break;
    case funcScopeMainSub:
    {
        // This tells us whether we are receiving main or sub data
        subScope = val.value.value<bool>();
        if (!subScope && !ui->mainScope->isVisible()) {
                ui->subScope->setVisible(false);
                ui->mainScope->setVisible(true);
        } else if (subScope && !ui->subScope->isVisible()) {
                ui->mainScope->setVisible(false);
                ui->subScope->setVisible(true);
        }

        if (ui->scopeDualBtn->isChecked()) {
            ui->mainScope->selected(!subScope);
            ui->subScope->selected(subScope);
        } else {
            ui->mainScope->selected(false);
            ui->subScope->selected(false);
        }

        break;
    }
    case funcScopeSingleDual:
    {
        // This tells us whether we are receiving single or dual scopes
        ui->scopeDualBtn->setChecked(val.value.value<bool>());
        if (val.value.value<bool>()) {
            if (!ui->subScope->isVisible())
            {
                ui->subScope->setVisible(true);
            }
            else if (!ui->mainScope->isVisible())
            {
                ui->subScope->setVisible(true);
            }
        } else {
            if (ui->mainScope->isVisible())
            {
                ui->subScope->setVisible(false);
            }
            else if (ui->subScope->isVisible())
            {
                ui->mainScope->setVisible(false);
            }
        }
        break;
    }
    case funcScopeMainMode:
        ui->mainScope->selectScopeMode(val.value.value<spectrumMode_t>());
        break;
    case funcScopeSubMode:
        ui->subScope->selectScopeMode(val.value.value<spectrumMode_t>());
        break;
    case funcScopeMainSpan:
        ui->mainScope->selectSpan(val.value.value<centerSpanData>());
        break;
    case funcScopeSubSpan:
        ui->subScope->selectSpan(val.value.value<centerSpanData>());
        break;
    case funcScopeMainEdge:
        // read edge mode center in edge mode
        // [1] 0x16
        // [2] 0x01, 0x02, 0x03: Edge 1,2,3
        break;
    case funcScopeMainHold:
        ui->mainScope->setHold(val.value.value<bool>());
        break;
    case funcScopeSubHold:
        ui->subScope->setHold(val.value.value<bool>());
        break;
    case funcScopeMainRef:
    {
        // scope reference level
        // [1] 0x19
        // [2] 0x00
        // [3] 10dB digit, 1dB digit
        // [4] 0.1dB digit, 0
        // [5] 0x00 = +, 0x01 = -
        break;
    }
    case funcScopeMainSpeed:
        ui->mainScope->setSpeed(val.value.value<uchar>());
        break;
    case funcScopeSubSpeed:
        ui->subScope->setSpeed(val.value.value<uchar>());
        break;
    case funcScopeDuringTX:
    case funcScopeCenterType:
    case funcScopeMainVBW:
    case funcScopeFixedEdgeFreq:
    case funcScopeMainRBW:
        break;
    // 0x28
    case funcVoiceTX:
        break;
    //0x29 - Prefix certain commands with this to get/set certain values without changing current VFO
    // If we use it for get commands, need to parse the \x29\x<VFO> first.
    case funcMainSubPrefix:
        break;
    default:
        qWarning(logSystem()) << "Unhandled command received from rigcommander()" << funcString[val.command] << "Contact support!";
        break;
    }
}


void wfmain::on_showBandsBtn_clicked()
{
    showAndRaiseWidget(bandbtns);
}

void wfmain::on_showFreqBtn_clicked()
{
    showAndRaiseWidget(finputbtns);
}

void wfmain::on_showSettingsBtn_clicked()
{
    showAndRaiseWidget(setupui);
}

void wfmain::on_scopeMainSubBtn_clicked()
{
    subScope = !subScope;
    queue->add(priorityImmediate,queueItem(funcScopeMainSub,QVariant::fromValue(subScope),false,false));
}

void wfmain::on_scopeDualBtn_toggled(bool en)
{
    queue->add(priorityImmediate,queueItem(funcScopeSingleDual,QVariant::fromValue(en),false,false));
    if (en)
        queue->add(priorityImmediate,queueItem(funcScopeMainSub,QVariant::fromValue(false),false,false)); // Set main scope
}

void wfmain::on_dualWatchBtn_toggled(bool en)
{
    queue->add(priorityImmediate,queueItem(funcVFODualWatch,QVariant::fromValue(en),false,false));
}

void wfmain::receiveScopeSettings(bool sub, int theme, quint16 len, int floor, int ceiling)
{
    if (sub) {
        prefs.subWfTheme = theme;
        prefs.subWflength = len;
        prefs.subPlotFloor = floor;
        prefs.subPlotCeiling = ceiling;
    }
    else
    {
        prefs.mainWfTheme = theme;
        prefs.mainWflength = len;
        prefs.mainPlotFloor = floor;
        prefs.mainPlotCeiling = ceiling;
    }
}


void wfmain::receiveElapsed(bool sub, qint64 us)
{
    if (sub)
        subElapsed = us;
    else
        mainElapsed = us;
}


/* USB Hotplug support added at the end of the file for convenience */
#ifdef USB_HOTPLUG

#ifdef Q_OS_WINDOWS
bool wfmain::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    Q_UNUSED(eventType);
    Q_UNUSED(result);

    if (QDateTime::currentMSecsSinceEpoch() > lastUsbNotify + 10)
    {
        static bool created = false;

        MSG * msg = static_cast< MSG * > (message);
        switch (msg->message)
        {
        case WM_PAINT:
        {
            if (!created) {
                    GUID InterfaceClassGuid = {0x745a17a0, 0x74d3, 0x11d0,{ 0xb6, 0xfe, 0x00, 0xa0, 0xc9, 0x0f, 0x57, 0xda}};
                    DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;
                    ZeroMemory( &NotificationFilter, sizeof(NotificationFilter) );
                    NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
                    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
                    NotificationFilter.dbcc_classguid = InterfaceClassGuid;
                    HWND hw = (HWND) this->effectiveWinId();   //Main window handle
                    RegisterDeviceNotification(hw,&NotificationFilter, DEVICE_NOTIFY_ALL_INTERFACE_CLASSES );
                    created = true;
            }
            break;
        }
        case WM_DEVICECHANGE:
        {
            switch (msg->wParam) {
            case DBT_DEVICEARRIVAL:
            case DBT_DEVICEREMOVECOMPLETE:
                    emit usbHotplug();
                    lastUsbNotify = QDateTime::currentMSecsSinceEpoch();
                    break;
            case DBT_DEVNODES_CHANGED:
                    break;
            default:
                    break;
            }

            return true;
            break;
        }
        default:
            break;
        }
    }
    return false; // Process native events as normal
}

#elif defined(Q_OS_LINUX)

void wfmain::uDevEvent()
{
    udev_device *dev = udev_monitor_receive_device(uDevMonitor);
    if (dev)
    {
        const char* action = udev_device_get_action(dev);
        if (action && strcmp(action, "add") == 0 && QDateTime::currentMSecsSinceEpoch() > lastUsbNotify + 10)
        {
            emit usbHotplug();
            lastUsbNotify = QDateTime::currentMSecsSinceEpoch();
        }
        udev_device_unref(dev);
    }
}
#endif
#endif

