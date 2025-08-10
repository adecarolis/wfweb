#include "wfmain.h"
#include "icomserver.h"
#include "ui_wfmain.h"

#include "rigidentities.h"
#include "logcategories.h"


// This code is copyright 2017-2024 Elliott H. Liggett and Phil E. Taylor
// All rights reserved

// Log support:
QScopedPointer<QFile> m_logFile;
QMutex logMutex;
QMutex logTextMutex;
QScopedPointer<QTextStream> logStream;

QVector<QPair<QtMsgType,QString>> logStringBuffer;
#ifdef QT_DEBUG
bool debugModeLogging = true;
#else
bool debugModeLogging = false;
#endif

bool insaneDebugLogging = false;
bool rigctlDebugLogging = false;

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
    ui->setupUi(this);
    setWindowTitle(QString("wfview"));

    ui->monitorLabel->setText("<a href=\"#\" style=\"color:white; text-decoration:none;\">Mon</a>");


    logWindow = new loggingWindow(logFile);
    initLogging();
    logWindow->setInitialDebugState(debugMode);
    qInfo(logSystem()).noquote() << QString("wfview version: %1 (Git:%2 on %3 at %4 by %5@%6)")
                          .arg(QString(WFVIEW_VERSION),GITSHORT,__DATE__,__TIME__,UNAME,HOST);

    qInfo(logSystem()).noquote() << QString("Operating System: %0 (%1)").arg(QSysInfo::prettyProductName(),QSysInfo::buildCpuArchitecture());
    qInfo(logSystem()).noquote() << "Looking for External Dependencies:";
    qInfo(logSystem()).noquote() << QString("QT Runtime Version: %0").arg(qVersion());
    if (strncmp(QT_VERSION_STR, qVersion(),sizeof(QT_VERSION_STR)))
    {
        qWarning(logSystem()).noquote() << QString("QT Build Version Mismatch: %0").arg(QT_VERSION_STR);
    }

    qInfo(logSystem()).noquote()  << QString("OPUS Version: %0").arg(opus_get_version_string());

#ifdef HID_API_VERSION_MAJOR
    qInfo(logSystem()).noquote() << QString("HIDAPI Version: %0.%1.%2")
            .arg(HID_API_VERSION_MAJOR)
            .arg(HID_API_VERSION_MINOR)
            .arg(HID_API_VERSION_PATCH);

    if (HID_API_VERSION != HID_API_MAKE_VERSION(hid_version()->major, hid_version()->minor, hid_version()->patch)) {
        qWarning(logSystem()).noquote() << QString("HIDAPI Version mismatch: %0.%1.%2")
                .arg(hid_version()->major)
                .arg(hid_version()->minor)
                .arg(hid_version()->patch);
    }
#endif

#ifdef EIGEN_WORLD_VERSION
    qInfo(logSystem()).noquote() << QString("EIGEN Version: %0.%1.%2").arg(EIGEN_WORLD_VERSION).arg(EIGEN_MAJOR_VERSION).arg(EIGEN_MINOR_VERSION);
#endif

#ifdef QCUSTOMPLOT_VERSION_STR
    qInfo(logSystem()).noquote() << QString("QCUSTOMPLOT Version: %0").arg(QCUSTOMPLOT_VERSION_STR);
#endif

#ifdef RTAUDIO_VERSION
    qInfo(logSystem()).noquote() << QString("RTAUDIO Version: %0").arg(RTAUDIO_VERSION);
    if (RTAUDIO_VERSION != RtAudio::getVersion())
    {
        qWarning(logSystem()).noquote() << QString("RTAUDIO Version Mismatch: %0").arg(RtAudio::getVersion().c_str());
    }
#endif

    qInfo(logSystem()).noquote() << QString("PORTAUDIO Version: %0").arg(Pa_GetVersionText());

    cal = new calibrationWindow();
    rpt = new repeaterSetup();
    sat = new satelliteSetup();
    abtBox = new aboutbox();
    selRad = new selectRadio();
    bandbtns = new bandbuttons();

    finputbtns = new frequencyinputwidget();
    setupui = new settingswidget();

    connect(setupui, SIGNAL(havePortError(errorType)), this, SLOT(receivePortError(errorType)));


    qRegisterMetaType<udpPreferences>(); // Needs to be registered early.
    qRegisterMetaType<manufacturersType_t>();
    qRegisterMetaType<connectionType_t>();
    qRegisterMetaType<rigCapabilities>();
    qRegisterMetaType<duplexMode_t>();
    qRegisterMetaType<rptAccessTxRx_t>();
    qRegisterMetaType<rptrAccessData>();
    qRegisterMetaType<toneInfo>();
    qRegisterMetaType<rigInput>();
    qRegisterMetaType<inputTypes>();
    qRegisterMetaType<meter_t>();
    qRegisterMetaType<meterkind>();
    qRegisterMetaType<freqt>();
    qRegisterMetaType<vfo_t>();
    qRegisterMetaType<modeInfo>();
    qRegisterMetaType<rigMode_t>();
    qRegisterMetaType<pttType_t>();
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
    qRegisterMetaType<funcs>();
    qRegisterMetaType<rigTypedef>();
    qRegisterMetaType<memoryType>();
    qRegisterMetaType<memoryTagType>();
    qRegisterMetaType<antennaInfo>();
    qRegisterMetaType<scopeData>();
    qRegisterMetaType<queueItem>();
    qRegisterMetaType<cacheItem>();
    qRegisterMetaType<spectrumBounds>();
    qRegisterMetaType<centerSpanData>();
    qRegisterMetaType<bandStackType>();
    qRegisterMetaType<widthsType>();
    qRegisterMetaType<yaesu_scope_data>();
    qRegisterMetaType<rigInfo>();
    qRegisterMetaType<lpfhpf>();

    this->setObjectName("wfmain");
    queue = cachingQueue::getInstance(this);

    connect(queue,SIGNAL(sendValue(cacheItem)),this,SLOT(receiveValue(cacheItem)));
    connect(queue,SIGNAL(sendMessage(QString)),this,SLOT(showStatusBarText(QString)));
    connect(queue,SIGNAL(intervalUpdated(qint64)),this,SLOT(updatedQueueInterval(qint64)));

    connect(queue, SIGNAL(finished()), queue, SLOT(deleteLater()));

    // Disable all controls until connected.
    enableControls(false);

    // We need to populate the list of rigs as early as possible so do it now

    // Setup the connectiontimer as we may need it soon!
    ConnectionTimer.setSingleShot(true);
    connect(&ConnectionTimer,SIGNAL(timeout()), this,SLOT(connectionTimeout()));

    // Make sure we know about any changes to rigCaps
    connect(queue, SIGNAL(rigCapsUpdated(rigCapabilities*)), this, SLOT(receiveRigCaps(rigCapabilities*)));

    setupKeyShortcuts();

    setupMainUI();
    connectSettingsWidget();

    setSerialDevicesUI();

    setDefPrefs();


    getSettingsFilePath(settingsFile);

    setDefaultColorPresets();

    loadSettings(); // Look for saved preferences
    logWindow->ingestSettings(prefs);

    setManufacturer(prefs.manufacturer);
    setServerToPrefs();

    //setAudioDevicesUI(); // no need to call this as it will be called by the updated() signal

    setTuningSteps(); // TODO: Combine into preferences

    changeFullScreenMode(prefs.useFullScreen);
    useSystemTheme(prefs.useSystemTheme);

    // Don't update prefs until system theme is loaded.
    // This is so we can popup any dialog boxes in the correct theme.
    setupui->updateIfPrefs((int)if_all);
    setupui->updateColPrefs((int)col_all);
    setupui->updateRaPrefs((int)ra_all);
    setupui->updateRsPrefs((int)rs_all); // Most of these come from the rig anyway
    setupui->updateCtPrefs((int)ct_all);
    setupui->updateClusterPrefs((int)cl_all);
    setupui->updateLanPrefs((int)l_all);
    setupui->updateUdpPrefs((int)u_all);
    setupui->updateServerConfigs((int)s_all);

    finputbtns->setAutomaticSidebandSwitching(prefs.automaticSidebandSwitching);


    qDebug(logSystem()) << "Running setInitialTiming()";
    setInitialTiming();

    fts = new FirstTimeSetup();

    if(prefs.hasRunSetup) {
        qDebug(logSystem()) << "Running openRig()";
        openRig();
    } else {
        qInfo(logSystem()) << "Detected first-time run. Showing the First Time Setup widget.";

        connect(fts, &FirstTimeSetup::exitProgram, this, [=]() {
            qInfo(logSystem()) << "User elected exit program.";
            prefs.settingsChanged = false;
            prefs.confirmExit = false;
            QTimer::singleShot(10, this, [&](){
                on_exitBtn_clicked();
            });
        });
        connect(fts, &FirstTimeSetup::showSettings, this, [=](const bool networkEnabled) {
            qInfo(logSystem()) << "User elected to visit the Settings UI.";
            prefs.enableLAN = networkEnabled;
            setupui->updateLanPrefs((int)l_all);
            showAndRaiseWidget(setupui);
            prefs.settingsChanged = true;
            prefs.hasRunSetup = true;
        });
        connect(fts, &FirstTimeSetup::skipSetup, this, [=]() {
            qInfo(logSystem()) << "User elected to skip the setup. Marking setup complete.";
            prefs.settingsChanged = true;
            prefs.hasRunSetup = true;
        });

        fts->exec();
    }

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

    amTransmitting = false;

    setupLambdaSlots();

#if defined(USB_CONTROLLER)
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

    // Add right-click menu
    this->setContextMenuPolicy(Qt::CustomContextMenu);
    connect (this, &QMainWindow::customContextMenuRequested,this,&::wfmain::popupScreenMenu);
    screenMenu = new QMenu(this);

    QLabel *header = new QLabel("Enable Display");
    header->setStyleSheet("QLabel {border-bottom: 1px solid; padding:5px;}");
    QWidgetAction* headerAction = new QWidgetAction(screenMenu);
    headerAction->setDefaultWidget(header);
    screenMenu->addAction(headerAction);

    QList<QWidget *> items = this->findChildren<QWidget *>();

    for (const auto &g : items)
    {
        if (g->objectName() !="" && g->isVisible())
        {
            QCheckBox *chk = new QCheckBox(g->objectName(),screenMenu);
            chk->setChecked(true);
            chk->setStyleSheet("QCheckBox {padding:5px;}");

#if (QT_VERSION >= QT_VERSION_CHECK(6,7,0))
            QObject::connect(chk, &QCheckBox::checkStateChanged, this, [g](Qt::CheckState state){
#else
            QObject::connect(chk, &QCheckBox::stateChanged, this, [g](int state){
#endif
                g->setVisible(state);
            });

            QWidgetAction *chkAction = new QWidgetAction(screenMenu);
            chkAction->setDefaultWidget(chk);
            screenMenu->addAction(chkAction);
        }
    }

    for (int i=1;i<5;i++)
    {
        //QAction* act = new QAction(QString("Action %0").arg(i),this);
        //act->setCheckable(true);
        //screenMenu->addAction(act);
    }

}

void wfmain::popupScreenMenu(QPoint pos)
{
    screenMenu->popup(this->mapToGlobal(pos));
}

wfmain::~wfmain()
{
    if(rigThread != Q_NULLPTR) {
        rigThread->quit();
        rigThread->wait();
    }

    if (serverThread != Q_NULLPTR) {
        serverThread->quit();
        serverThread->wait();
    }

    // Delete all shortcuts
    for (const auto &s: shortcuts)
    {
        delete s;
    }
    shortcuts.clear();

    // Each rig needs deleting before we close.
    for (auto &rig: serverConfig.rigs) {
        delete rig;
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

    logStream->flush();
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
    isRadioAdmin = true; // Set user to admin, will be reset if not.
    // M0VSE: This could be in a better place maybe?
    if (prefs.audioSystem == tciAudio)
    {
        prefs.rxSetup.tci = tci;
        prefs.txSetup.tci = tci;
    }

    makeRig();


    if (prefs.enableLAN)
    {
        // "We need to setup the tx/rx audio:
        udpPrefs.waterfallFormat = prefs.waterfallFormat;
        // 60 second connection timeout.
        ConnectionTimer.start(60000);
        emit sendCommSetup(rigList, prefs.radioCIVAddr, udpPrefs, prefs.rxSetup, prefs.txSetup, prefs.virtualSerialPort, prefs.tcpPort);
    } else {
        if( (prefs.serialPortRadio.toLower() == QString("auto")))
        {
            findSerialPort();
        } else {
            serialPortRig = prefs.serialPortRadio;
        }
        emit sendCommSetup(rigList, prefs.radioCIVAddr, serialPortRig, prefs.serialPortBaud,prefs.virtualSerialPort, prefs.tcpPort,prefs.waterfallFormat);
        ui->statusBar->showMessage(QString("Connecting to rig using serial port ").append(serialPortRig), 1000);

    }


}

// Deprecated (moved to makeRig())
void wfmain::rigConnections()
{
}

void wfmain::makeRig()
{
    if (rigThread == Q_NULLPTR)
    {
        switch (prefs.manufacturer){
        case manufIcom:
            rig = new icomCommander();
            break;
        case manufKenwood:
            rig = new kenwoodCommander();
            break;
        case manufYaesu:
            rig = new yaesuCommander();
            break;
        default:
            qCritical() << "Unknown Manufacturer, aborting...";
            break;
        }

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
        connect(rig, SIGNAL(setRadioUsage(quint8, bool, quint8, QString, QString)), selRad, SLOT(setInUse(quint8, bool, quint8, QString, QString)));
        connect(rig, SIGNAL(setRadioUsage(quint8, bool, quint8, QString, QString)), this, SLOT(radioInUse(quint8, bool, quint8, QString, QString)));
        connect(selRad, SIGNAL(selectedRadio(quint8)), rig, SLOT(setCurrentRadio(quint8)));
        // Rig comm setup:
        connect(this, SIGNAL(sendCommSetup(rigTypedef,quint16, udpPreferences, audioSetup, audioSetup, QString, quint16)), rig, SLOT(commSetup(rigTypedef,quint16, udpPreferences, audioSetup, audioSetup, QString, quint16)));
        connect(this, SIGNAL(sendCommSetup(rigTypedef,quint16, QString, quint32,QString, quint16,quint8)), rig, SLOT(commSetup(rigTypedef,quint16, QString, quint32,QString, quint16,quint8)));
        connect(this, SIGNAL(setPTTType(pttType_t)), rig, SLOT(setPTTType(pttType_t)));

        connect(rig, SIGNAL(haveBaudRate(quint32)), this, SLOT(receiveBaudRate(quint32)));

        connect(this, SIGNAL(sendCloseComm()), rig, SLOT(closeComm()));
        connect(this, SIGNAL(sendChangeLatency(quint16)), rig, SLOT(changeLatency(quint16)));
        connect(this, SIGNAL(setRigID(quint16)), rig, SLOT(setRigID(quint16)));

        connect(rig, SIGNAL(commReady()), this, SLOT(receiveCommReady()));

        if (serverConfig.enabled) {
            qInfo(logRigServer()) << "**** Connecting rig instance to server";
            connect(rig, SIGNAL(haveAudioData(audioPacket)), server, SLOT(receiveAudioData(audioPacket)));
            connect(rig, SIGNAL(haveDataForServer(QByteArray)), server, SLOT(dataForServer(QByteArray)));
            connect(server, SIGNAL(haveDataFromServer(QByteArray)), rig, SLOT(dataFromServer(QByteArray)));
        }

        connect(this, SIGNAL(setCIVAddr(quint16)), rig, SLOT(setCIVAddr(quint16)));
        connect(this, SIGNAL(sendPowerOn()), rig, SLOT(powerOn()));
        connect(this, SIGNAL(sendPowerOff()), rig, SLOT(powerOff()));
        connect(this, SIGNAL(getDebug()), rig, SLOT(getDebug()));

        connect(rig, SIGNAL(haveRptOffsetFrequency(freqt)), rpt, SLOT(handleRptOffsetFrequency(freqt)));

        connect(this->rpt, &repeaterSetup::getTone, this->rig,
                [=]() {
            qDebug(logSystem()) << "Asking for TONE";
            queue->add(priorityImmediate,funcRepeaterTone,false,false);});

        connect(this->rpt, &repeaterSetup::getTSQL, this->rig,
                [=]() {
            qDebug(logSystem()) << "Asking for TSQL";
            queue->add(priorityImmediate,funcRepeaterTSQL,false,false);});

        connect(this->rpt, &repeaterSetup::setDTCS, this->rig,
                [=](const toneInfo& t) {
            qDebug(logSystem()) << "Setting DCS, code =" << t.tone;
            queue->add(priorityImmediate,queueItem(funcRepeaterDTCS,QVariant::fromValue<toneInfo>(t),false));});

        connect(this->rpt, &repeaterSetup::getRptAccessMode, this->rig,
                [=]() {
                    if (rigCaps->commands.contains(funcToneSquelchType)) {
                        queue->add(priorityImmediate,funcToneSquelchType,false,false);
                    } else {
                        queue->add(priorityImmediate,funcRepeaterTone,false,false);
                        queue->add(priorityImmediate,funcRepeaterTSQL,false,false);
                    }
                });


        connect(this->rig, &rigCommander::haveDuplexMode, this->rpt,
                [=](const duplexMode_t &dm) {
                    if(dm==dmSplitOn)
                        this->splitModeEnabled = true;
                    else
                        this->splitModeEnabled = false;
                });

        connect(this->rpt, &repeaterSetup::selectVFO, this->rig,
                [=](const vfo_t &v) { queue->add(priorityImmediate,queueItem(funcSelectVFO,QVariant::fromValue<vfo_t>(v),false));});

        connect(this->rpt, &repeaterSetup::equalizeVFOsAB, this->rig,
                [=]() { queue->add(priorityImmediate,funcVFOEqualAB,false,false);});

        connect(this->rpt, &repeaterSetup::equalizeVFOsMS, this->rig,
                [=]() {  queue->add(priorityImmediate,funcVFOEqualMS,false,false);});

        connect(this->rpt, &repeaterSetup::swapVFOs, this->rig,
                [=]() {  queue->add(priorityImmediate,funcVFOSwapMS,false,false);});

        connect(this->rpt, &repeaterSetup::setRptDuplexOffset, this->rig,
                [=](const freqt &fOffset) {
            queue->add(priorityImmediate,queueItem(funcSendFreqOffset,QVariant::fromValue<freqt>(fOffset),false));});

        connect(this->rpt, &repeaterSetup::getRptDuplexOffset, this->rig,
                [=]() {  queue->add(priorityImmediate,funcReadFreqOffset,false,false);});

        // Create link for server so it can have easy access to rig.
        if (serverConfig.rigs.first() != Q_NULLPTR) {
            serverConfig.rigs.first()->rig = rig;
            serverConfig.rigs.first()->rigThread = rigThread;
        }
    }
}

void wfmain::removeRig()
{
    ConnectionTimer.stop();

    if (rigThread != Q_NULLPTR)
    {
        rigThread->quit();
        rigThread->wait();
        rig = Q_NULLPTR;
        rigThread = Q_NULLPTR;
    }

    if (cw != Q_NULLPTR)
    {
        prefs.cwCutNumbers = cw->getCutNumbers();
        prefs.cwSendImmediate = cw->getSendImmediate();
        prefs.cwSidetoneEnabled = cw->getSidetoneEnable();
        prefs.cwSidetoneLevel = cw->getSidetoneLevel();
        prefs.cwMacroList = cw->getMacroText();

        delete cw;
        cw = Q_NULLPTR;
        ui->cwButton->setEnabled(false);
    }

    for (const auto &receiver:receivers) {
        delete receiver;
    }
    receivers.clear();
    currentReceiver=0;
    // Disable all controls until connected.
    enableControls(false);

}


void wfmain::findSerialPort()
{
    // Find the ICOM radio connected, or, if none, fall back to OS default.
    // qInfo(logSystem()) << "Searching for serial port...";
    bool found = false;
    // First try to find first Icom port:
    for(const QSerialPortInfo & serialPortInfo: QSerialPortInfo::availablePorts())
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
    //qInfo(logSystem()) << "Received CommReady!! ";
    if(!prefs.enableLAN)
    {
        // If we're not using the LAN, then we're on serial, and
        // we already know the baud rate and can calculate the timing parameters.
        calculateTimingParameters();
    }
    if(prefs.radioCIVAddr == 0)
    {
        // tell rigCommander to broadcast a request for all rig IDs.
        // qInfo(logSystem()) << "Beginning search from wfview for rigCIV (auto-detection broadcast)";
        ui->statusBar->showMessage(QString("Searching CI-V bus for connected radios."), 1000);

        queue->addUnique(priorityHigh,funcTransceiverId,true);
    } else {
        // don't bother, they told us the CIV they want, stick with it.
        // We still query the rigID to find the model, but at least we know the CIV.
        qInfo(logSystem()) << QString("Skipping automatic CIV, using user-supplied value of 0x%1").arg(prefs.radioCIVAddr,2,16) ;
        showStatusBarText(QString("Using user-supplied radio CI-V address of 0x%1").arg(prefs.radioCIVAddr, 2, 16));
        if(prefs.CIVisRadioModel)
        {
            qInfo(logSystem()) << QString("Skipping Rig ID query, using user-supplied model from CI-V address: 0x%1").arg(prefs.radioCIVAddr,2,16) ;
            emit setCIVAddr(prefs.radioCIVAddr);
            emit setRigID(prefs.radioCIVAddr);
        } else {
            emit setCIVAddr(prefs.radioCIVAddr);
            queue->addUnique(priorityHigh,funcTransceiverId,true);

        }
    }
    if (prefs.autoPowerOn)
    {
        // After 2s send powerOn command
        QTimer::singleShot(2000, rig, SLOT(powerOn()));

    }
}

void wfmain::receivePortError(errorType err)
{
    if (err.alert) {
        connectionHandler(false); // Force disconnect
        QMessageBox::critical(this, err.device, QString("%0: %1").arg(err.device,err.message), QMessageBox::Ok);
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

    if (queue->interval()==-1)
    {
        return;
    }

    // If we have received very high network latency, increase queue interval, don't set it to higher than 500ms though.
    if (prefs.polling_ms != 0 && queue->interval() == quint32(delayedCmdIntervalLAN_ms) && (status.networkLatency/2) > quint32(delayedCmdIntervalLAN_ms))
    {
        qInfo(logRig()) << QString("1/2 network latency %0 exceeds configured queue interval %1, increasing").arg(status.networkLatency/2).arg(delayedCmdIntervalLAN_ms);
        queue->interval(qMin(quint32(status.networkLatency/2),quint32(500)));
    }
    else if (prefs.polling_ms != 0 && queue->interval() != quint32(delayedCmdIntervalLAN_ms) && (status.networkLatency/2) < quint32(delayedCmdIntervalLAN_ms))
    {
        // Maybe it was a temporary issue, restore to default
        queue->interval(delayedCmdIntervalLAN_ms);
    }

    //this->rigStatus->setText(QString("%0/%1 %2").arg(mainElapsed).arg(subElapsed).arg(status.message));
    this->rigStatus->setText(status.message);
    selRad->audioOutputLevel(status.rxAudioLevel);
    selRad->audioInputLevel(status.txAudioLevel);
    selRad->addTimeDifference(status.timeDifference);
    //qInfo(logSystem()) << "Got Status Update" << status.rxAudioLevel;
}

void wfmain::receiveNetworkAudioLevels(networkAudioLevels l)
{
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

    oldFreqDialVal = ui->freqDial->value();

    ui->tuneLockChk->setChecked(false);
    freqLock = false;

    connect(
                ui->txPowerSlider, &QSlider::valueChanged, this,
                [=](const int &newValue) {
                    if (ui->txPowerSlider->maximum())
                        statusFromSliderPercent("Tx Power", 255*newValue/ui->txPowerSlider->maximum());
                }
    );

    connect(
                ui->rfGainSlider, &QSlider::valueChanged, this,
                [=](const int &newValue) {
                    if (ui->rfGainSlider->maximum())
                        statusFromSliderPercent("RF Gain", 255*newValue/ui->rfGainSlider->maximum());
                }
    );

    connect(
                ui->afGainSlider, &QSlider::valueChanged, this,
                [=](const int &newValue) {
                    if (ui->afGainSlider->maximum())
                        statusFromSliderPercent("AF Gain", 255*newValue/ui->afGainSlider->maximum());
                }
    );

    connect(
                ui->micGainSlider, &QSlider::valueChanged, this,
                [=](const int &newValue) {
                    if (ui->micGainSlider->maximum())
                        statusFromSliderPercent("TX Audio Gain", 255*newValue/ui->micGainSlider->maximum());
                }
    );

    connect(
                ui->sqlSlider, &QSlider::valueChanged, this,
                [=](const int &newValue) {
                    if (ui->sqlSlider->maximum())
                        statusFromSliderPercent("Squelch", 255*newValue/ui->sqlSlider->maximum());
                }
    );

    connect(
                ui->monitorSlider, &QSlider::valueChanged, this,
                [=](const int &newValue) {
                    if (ui->monitorSlider->maximum())
                        statusFromSliderPercent("Monitor", 255*newValue/ui->monitorSlider->maximum());
                }
    );

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

    connect(this, SIGNAL(connectionStatus(bool)), setupui, SLOT(connectionStatus(bool)));
    connect(setupui, SIGNAL(connectButtonPressed()), this, SLOT(handleExtConnectBtn()));
    connect(setupui, SIGNAL(saveSettingsButtonPressed()), this, SLOT(on_saveSettingsBtn_clicked()));
    connect(setupui, SIGNAL(revertSettingsButtonPressed()), this, SLOT(handleRevertSettingsBtn()));
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
        server = Q_NULLPTR;
        ui->statusBar->showMessage(QString("Server disabled"), 1000);
    }

    if (serverConfig.enabled) {
        serverConfig.lan = prefs.enableLAN;

        switch (prefs.manufacturer)
        {
            case manufIcom:
                server = new icomServer(&serverConfig);
                break;
            case manufKenwood:
                server = new kenwoodServer(&serverConfig);
                break;
            case manufYaesu:
                server = new yaesuServer(&serverConfig);
                break;
        default:
            return;

        }

        serverThread = new QThread(this);

        server->moveToThread(serverThread);

        if (serverConfig.lan) {
            connect(server, SIGNAL(haveNetworkStatus(networkStatus)), this, SLOT(receiveStatusUpdate(networkStatus)));
        } else {
            qInfo(logAudio()) << "Audio Input device " << serverConfig.rigs.first()->rxAudioSetup.name;
            qInfo(logAudio()) << "Audio Output device " << serverConfig.rigs.first()->txAudioSetup.name;
        }

        connect(this, SIGNAL(initServer()), server, SLOT(init()));
        connect(serverThread, SIGNAL(finished()), server, SLOT(deleteLater()));

        serverThread->start();

        emit initServer();

        ui->statusBar->showMessage(QString("Server enabled"), 1000);

    }
}

void wfmain::configureVFOs()
{
    qInfo(logSystem()) << "Running configureVFOs()";

    if (QThread::currentThread() != QCoreApplication::instance()->thread())
    {
        qCritical(logSystem()) << "Thread is NOT the main UI thread, cannot create VFO";
        return;
    }

    if (receivers.size()) {
        foreach (receiverWidget* receiver, receivers)
        {
            ui->vfoLayout->removeWidget(receiver);
            delete receiver;
        }
        receivers.clear();
    }

    for(uchar i=0;i<rigCaps->numReceiver;i++)
    {
        receiverWidget* receiver = new receiverWidget(rigCaps->hasSpectrum,i,rigCaps->numVFO,this);
        receiver->prepareScope(rigCaps->spectAmpMax, rigCaps->spectLenMax);
        receiver->setSeparators(prefs.groupSeparator,prefs.decimalSeparator);
        receiver->setUnderlayMode(prefs.underlayMode);
        receiver->wfAntiAliased(prefs.wfAntiAlias);
        receiver->wfInterpolate(prefs.wfInterpolate);
        receiver->setScrollSpeedXY(prefs.scopeScrollX, prefs.scopeScrollY);
        receiver->prepareWf(i==0?prefs.mainWflength:prefs.subWflength);
        receiver->setRange(i==0?prefs.mainPlotFloor:prefs.subPlotFloor,i==0?prefs.mainPlotCeiling:prefs.subPlotCeiling);
        receiver->wfTheme(i==0?prefs.mainWfTheme:prefs.subWfTheme);
        receiver->setClickDragTuning(prefs.clickDragTuningEnable);
        receiver->setTuningFloorZeros(prefs.niceTS);
        receiver->resizePlasmaBuffer(prefs.underlayBufferSize);
        receiver->setUnit((FctlUnit)prefs.frequencyUnits);
        colorPrefsType p = colorPreset[prefs.currentColorPresetNumber];
        receiver->colorPreset(&p);
        receiver->setIdentity(i==0?"Main Band":"Sub Band");
        ui->vfoLayout->addWidget(receiver);

        // Hide any secondary receivers until we need them!
        receiver->selected(i==0?true:false);
        receiver->setVisible(i==0?true:false);

        connect(receiver, SIGNAL(frequencyRange(uchar, double, double)), cluster, SLOT(freqRange(uchar, double, double)));

        connect(cluster, SIGNAL(sendSpots(uchar, QList<spotData>)), receiver, SLOT(receiveSpots(uchar, QList<spotData>)));
        connect(cluster, SIGNAL(sendOutput(QString)), this, SLOT(receiveClusterOutput(QString)));
        connect(receiver, SIGNAL(updateSettings(uchar,int,quint16,int,int)), this, SLOT(receiveScopeSettings(uchar,int,quint16,int,int)));

        connect(receiver, SIGNAL(dataChanged(modeInfo)), this, SLOT(dataModeChanged(modeInfo)));

        connect(receiver, &receiverWidget::bandChanged, receiver, [=](const uchar& rx, const bandType& b) {
            if (rx == currentReceiver && rigCaps->commands.contains(funcAntenna)) {
                ui->antennaSelCombo->setEnabled(b.ants);
                if (b.ants) {
                    queue->addUnique(priorityMediumHigh,queueItem(funcAntenna,true,i));
                } else {
                    queue->del(funcAntenna,i);
                }
            }
        });

        connect(receiver,SIGNAL(showStatusBarText(QString)),this,SLOT(showStatusBarText(QString)));
        connect(receiver,SIGNAL(sendScopeImage(uchar)),this,SLOT(receiveScopeImage(uchar)));
        receivers.append(receiver);

        // Currently tracking is disabled
        //if (receivers.size() > 1)
        //{
        //    connect(receivers[0], SIGNAL(sendTrack(int)),receivers[1], SLOT(receiveTrack(int)));
        //}

    }
}

void wfmain::setSerialDevicesUI()
{

}

QShortcut* wfmain::setupKeyShortcut(const QKeySequence k, bool appWide)
{
    QShortcut* sc=new QShortcut(this);
    sc->setKey(k);
    if(appWide)
        sc->setContext(Qt::ApplicationShortcut);

    connect(sc, &QShortcut::activated, this,
            [=]() {
        this->runShortcut(k);
    });
    return sc;
}

void wfmain::setupKeyShortcuts()
{
    shortcuts.append(setupKeyShortcut(Qt::Key_F1, true));
    shortcuts.append(setupKeyShortcut(Qt::Key_F2, true));
    shortcuts.append(setupKeyShortcut(Qt::Key_F3, true));
    shortcuts.append(setupKeyShortcut(Qt::Key_F4, true));
    shortcuts.append(setupKeyShortcut(Qt::Key_F5));
    shortcuts.append(setupKeyShortcut(Qt::Key_F6));
    shortcuts.append(setupKeyShortcut(Qt::Key_F7));
    shortcuts.append(setupKeyShortcut(Qt::Key_F8));
    shortcuts.append(setupKeyShortcut(Qt::Key_F9));
    shortcuts.append(setupKeyShortcut(Qt::Key_F10));
    shortcuts.append(setupKeyShortcut(Qt::Key_F11));
    shortcuts.append(setupKeyShortcut(Qt::Key_F12));
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::Key_T));
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::Key_R));
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::Key_P));
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::Key_I));
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::Key_U));
    shortcuts.append(setupKeyShortcut(Qt::Key_Asterisk));
    shortcuts.append(setupKeyShortcut(Qt::Key_Slash));
    shortcuts.append(setupKeyShortcut(Qt::Key_Backslash));
    shortcuts.append(setupKeyShortcut(Qt::Key_Minus));
    shortcuts.append(setupKeyShortcut(Qt::Key_Plus));
    shortcuts.append(setupKeyShortcut(Qt::SHIFT | Qt::Key_Minus));
    shortcuts.append(setupKeyShortcut(Qt::SHIFT | Qt::Key_Plus));
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::Key_Minus));
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::Key_Plus));
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::Key_Q));
    shortcuts.append(setupKeyShortcut(Qt::Key_PageUp));
    shortcuts.append(setupKeyShortcut(Qt::Key_PageDown));
    shortcuts.append(setupKeyShortcut(Qt::Key_F));
    shortcuts.append(setupKeyShortcut(Qt::Key_M));
    shortcuts.append(setupKeyShortcut(Qt::Key_K));
    shortcuts.append(setupKeyShortcut(Qt::Key_J));
    shortcuts.append(setupKeyShortcut(Qt::SHIFT | Qt::Key_K));
    shortcuts.append(setupKeyShortcut(Qt::SHIFT | Qt::Key_J));
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::Key_K));
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::Key_J));
    shortcuts.append(setupKeyShortcut(Qt::Key_H));
    shortcuts.append(setupKeyShortcut(Qt::Key_L));
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
#if (QT_VERSION > QT_VERSION_CHECK(6,0,0))
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::Key_D));
#endif


}

void wfmain::runShortcut(const QKeySequence k)
{
    qInfo() << "Running shortcut for key:" << k;

    if (k == Qt::Key_F1)
    {
        this->raise();
    }
    else if (k==Qt::Key_F11)
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
    else if (k==(Qt::CTRL | Qt::Key_Q))
    {
        on_exitBtn_clicked();
    }
    else if (k==(Qt::CTRL | Qt::Key_D) || k==(Qt::CTRL | Qt::SHIFT | Qt::Key_D))
    {
        debugBtn_clicked();
    }
    else if (rigCaps == Q_NULLPTR)
    {
        // No rig yet
        qWarning() << "Cannot run shortcut as not connected to rig";
        return;
    }

    // All of the below commands require a connected rig
    bool freqUpdate=false;
    freqt f;

    if (k == Qt::Key_F2)
    {
        showAndRaiseWidget(bandbtns);
    }
    else if (k==Qt::Key_F3 || k==Qt::Key_Asterisk)
    {
        showAndRaiseWidget(finputbtns);
    }
    else if (k==Qt::Key_F4)
    {
        showAndRaiseWidget(setupui);
    }
    else if (k==Qt::Key_F5)
    {
        changeMode(modeLSB, false, currentReceiver);
    }
    else if (k==Qt::Key_F6)
    {
        changeMode(modeUSB, false, currentReceiver);
    }
    else if (k==Qt::Key_F7)
    {
        changeMode(modeAM, false, currentReceiver);
    }
    else if (k==Qt::Key_F8)
    {
        changeMode(modeCW, false, currentReceiver);
    }
    else if (k==Qt::Key_F9)
    {
        changeMode(modeUSB, true, currentReceiver);
    }
    else if (k==Qt::Key_F10)
    {
        changeMode(modeFM, false, currentReceiver);
    }
    else if (k==Qt::Key_F12)
    {
        showStatusBarText("Sending speech command to radio.");
        queue->add(priorityImmediate,queueItem(funcSpeech,QVariant::fromValue(uchar(0U)),false,currentReceiver));
    }
    else if (k==(Qt::CTRL | Qt::Key_T))
    {
        // Transmit
        qDebug(logSystem()) << "Activated Control-T shortcut";
        showStatusBarText(QString("Transmitting. Press Control-R to receive."));
        extChangedRsPrefs(rs_pttOn);
    }
    else if (k==(Qt::CTRL | Qt::Key_R))
    {
        // Receive
        extChangedRsPrefs(rs_pttOff);
    }
    else if (k==(Qt::CTRL | Qt::Key_P))
    {
        // Toggle PTT
        if(amTransmitting) {
            extChangedRsPrefs(rs_pttOff);
        } else {
            extChangedRsPrefs(rs_pttOn);
            showStatusBarText(QString("Transmitting. Press Control-P again to receive."));
        }
    }
    else if (k==(Qt::CTRL | Qt::Key_I))
    {
        // Enable ATU
        ui->tuneEnableChk->click();
    }
    else if (k==(Qt::CTRL | Qt::Key_U))
    {
        // Run ATU tuning cycle
        ui->tuneNowBtn->click();
    }
    else if (k==Qt::Key_Slash)
    {
        for (size_t i = 0; i < rigCaps->modes.size(); i++) {
            if (rigCaps->modes[i].mk == receivers[currentReceiver]->currentMode().mk)
            {
                if (i + 1 < rigCaps->modes.size()) {
                    changeMode(rigCaps->modes[i + 1].mk,false,currentReceiver);
                }
                else {
                    changeMode(rigCaps->modes[0].mk,false,currentReceiver);
                }
                break;
            }
        }
    }
    else if (k==Qt::Key_Backslash)
    {
        bool found = false;
        availableBands band = bandUnknown;
        auto b = rigCaps->bands.cend();
        while (b != rigCaps->bands.cbegin())
        {
            b--;
            if ((b->region == "" || prefs.region == b->region))
            {
                if (!found && b->band == bandbtns->currentBand())
                {
                    found = true;
                }
                else if (found && b->band != bandbtns->currentBand())
                {
                    qInfo() << "Got new band:" << b->band << "Name:" << b->name << "region" << b->region;
                    band = b->band;
                    break;
                }
            }
        }
        if (band == bandUnknown)
        {
            band = rigCaps->bands[rigCaps->bands.size() - 1].band;
        }
        bandbtns->setBand(band);
    }
    else if (k==Qt::Key_Minus|| k==Qt::Key_J)
    {
        f.Hz = roundFrequencyWithStep(receivers[0]->getFrequency().Hz, -1, tsPlusHz);
        freqUpdate=true;
    }
    else if (k==Qt::Key_Plus || k==Qt::Key_K)
    {
        f.Hz = roundFrequencyWithStep(receivers[0]->getFrequency().Hz, 1, tsPlusHz);
        freqUpdate=true;
    }
    else if (k==(Qt::SHIFT | Qt::Key_Minus) || k==(Qt::SHIFT | Qt::Key_J))
    {
        f.Hz = roundFrequencyWithStep(receivers[0]->getFrequency().Hz, -1, tsPlusShiftHz);
        freqUpdate=true;
    }
    else if (k==(Qt::SHIFT | Qt::Key_Plus) || k==(Qt::SHIFT | Qt::Key_K))
    {
        f.Hz = roundFrequencyWithStep(receivers[0]->getFrequency().Hz, 1, tsPlusShiftHz);
        freqUpdate=true;
    }
    else if (k==(Qt::CTRL | Qt::Key_Minus) || k==(Qt::CTRL | Qt::Key_J))
    {
        f.Hz = roundFrequencyWithStep(receivers[0]->getFrequency().Hz, -1, tsPlusControlHz);
        freqUpdate=true;
    }
    else if (k==(Qt::CTRL | Qt::Key_Plus) || k==(Qt::CTRL | Qt::Key_K))
    {
        f.Hz = roundFrequencyWithStep(receivers[0]->getFrequency().Hz, 1, tsPlusControlHz);
        freqUpdate=true;
    }
    else if (k==Qt::Key_PageUp)
    {
        f.Hz = receivers[0]->getFrequency().Hz + tsPageHz;
        freqUpdate=true;
    }
    else if (k==Qt::Key_PageDown)
    {
        f.Hz = receivers[0]->getFrequency().Hz - tsPageHz;
        freqUpdate=true;
    }
    else if (k==Qt::Key_F)
    {
        showStatusBarText("Sending speech command (frequency) to radio.");
        queue->add(priorityImmediate,queueItem(funcSpeech, QVariant::fromValue<uchar>(1),false,currentReceiver));
    }
    else if (k==Qt::Key_M)
    {
        showStatusBarText("Sending speech command (mode) to radio.");
        queue->add(priorityImmediate,queueItem(funcSpeech, QVariant::fromValue<uchar>(2),false,currentReceiver));
    }
    else if (k==Qt::Key_H)
    {
        f.Hz = roundFrequencyWithStep(receivers.first()->getFrequency().Hz, -1, tsKnobHz);
        freqUpdate = true;
    }
    else if (k==Qt::Key_L)
    {
        f.Hz = roundFrequencyWithStep(receivers.first()->getFrequency().Hz, 1, tsKnobHz);
        freqUpdate = true;
    }


    if (!freqLock && freqUpdate)
    {
        vfoCommandType t = queue->getVfoCommand(vfoA,currentReceiver,true);
        f.MHzDouble = f.Hz / (double)1E6;
        queue->add(priorityImmediate,queueItem(t.freqFunc,
                                                        QVariant::fromValue<freqt>(f),false,uchar(t.receiver)));
    }
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
    connect(usbControllerDev, SIGNAL(doShuttle(bool,quint8)), this, SLOT(doShuttle(bool,quint8)));
    connect(usbControllerDev, SIGNAL(button(const COMMAND*)), this, SLOT(buttonControl(const COMMAND*)));
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
    queue->add(priorityImmediate,queueItem(funcTransceiverStatus,QVariant::fromValue<bool>(status),false,uchar(0)));

    // Start 3 minute timer
    if (status)
        pttTimer->start();
}

void wfmain::doShuttle(bool up, quint8 level)
{
    if (level == 1 && up)
        runShortcut(Qt::SHIFT | Qt::Key_Plus);
    else if (level == 1 && !up)
        runShortcut(Qt::SHIFT | Qt::Key_Minus);
    else if (level > 1 && level < 5 && up)
        for (int i = 1; i < level; i++)
            runShortcut(Qt::Key_Plus);
    else if (level > 1 && level < 5 && !up)
        for (int i = 1; i < level; i++)
            runShortcut(Qt::Key_Minus);
    else if (level > 4 && up)
        for (int i = 4; i < level; i++)
            runShortcut(Qt::CTRL | Qt::Key_Plus);
    else if (level > 4 && !up)
        for (int i = 4; i < level; i++)
            runShortcut(Qt::CTRL | Qt::Key_Minus);
}

void wfmain::buttonControl(const COMMAND* cmd)
{
    qDebug(logUsbControl()) << QString("executing command: %0 (%1) suffix:%2 value:%3" ).arg(cmd->text).arg(funcString[cmd->command]).arg(cmd->suffix).arg(cmd->value);
    quint8 vfo=0;
    if (rigCaps == Q_NULLPTR) {
        return;
    }

    switch (cmd->command) {
    case funcBandStackReg:
    {
        bool found = false;
        availableBands band = bandUnknown;
        if (cmd->value == 1) {
            auto b = rigCaps->bands.cend();
            while (b != rigCaps->bands.cbegin())
            {
                b--;
                if ((b->region == "" || prefs.region == b->region))
                {
                    if (!found && b->band == bandbtns->currentBand()) {
                        found = true;
                    } else if (found && b->band != bandbtns->currentBand()) {
                        qInfo() << "Got new band:" << b->band << "Name:" << b->name << "region" << b->region;
                        band = b->band;
                        break;
                    }
                }
            }
            if (band == bandUnknown)
            {
                band = rigCaps->bands[rigCaps->bands.size() - 1].band;
            }
        } else if (cmd->value == -1) {
            auto b = rigCaps->bands.cbegin();
            while (b != rigCaps->bands.cend())
            {
                if ((b->region == "" || prefs.region == b->region))
                {
                    if (!found && b->band == bandbtns->currentBand()) {
                        found = true;
                    } else if (found && b->band != bandbtns->currentBand()) {
                        qInfo() << "Got band:" << b->band << "Name:" << b->name << "region" << b->region;
                        band = b->band;
                        break;
                    }
                }
                b++;
            }
            if (band == bandUnknown) {
                band = rigCaps->bands[0].band;
            }
        } else {
            band=cmd->band;
        }
        bandbtns->setBand(band);
        break;
    }
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
    case funcUnselectedMode:
        vfo=1;
    case funcMode:
    case funcModeSet:
        qInfo(logRig()) << "Setting mode" << cmd->value << "or" << cmd->mode;
        if (cmd->suffix < receivers.size())
        {
            if (cmd->value == 1)
            {
                for (size_t i = 0; i < rigCaps->modes.size(); i++)
                {
                    if (rigCaps->modes[i].mk == receivers[cmd->suffix]->currentMode().mk)
                    {
                        if (i + 1 < rigCaps->modes.size())
                        {
                            changeMode(rigCaps->modes[i + 1].mk,false,cmd->suffix);
                        }
                        else
                        {
                            changeMode(rigCaps->modes[0].mk,false,cmd->suffix);
                        }
                    }
                }
            }
            else if (cmd->value == -1)
            {
                for (size_t i = 0; i < rigCaps->modes.size(); i++)
                {
                    if (rigCaps->modes[i].mk == receivers[cmd->suffix]->currentMode().mk)
                    {
                        if (i>0)
                        {
                            changeMode(rigCaps->modes[i - 1].mk,false,cmd->suffix);
                        }
                        else
                        {
                            changeMode(rigCaps->modes[rigCaps->modes.size()-1].mk,false,cmd->suffix);
                        }
                    }
                }
            }
            else
            {
                changeMode(cmd->mode,false,cmd->suffix);
            }
        }
        break;
    case funcTuningStep:
        if ((cmd->value > 0 && ui->tuningStepCombo->currentIndex()  < ui->tuningStepCombo->count()-cmd->value) ||
            (cmd->value < 0 && ui->tuningStepCombo->currentIndex() > 0))
        {
            ui->tuningStepCombo->setCurrentIndex(ui->tuningStepCombo->currentIndex() + cmd->value);
        }
        else
        {
            if (cmd->value < 0)
                ui->tuningStepCombo->setCurrentIndex(ui->tuningStepCombo->count()-1);
            else
                ui->tuningStepCombo->setCurrentIndex(0);
        }
        break;
    case funcScopeSpan:
        if (cmd->suffix < receivers.size()) {
            receivers[cmd->suffix]->changeSpan(cmd->value);
        }
        break;
    case funcUnselectedFreq:
        vfo=1;
    case funcFreq:
    case funcSelectedFreq:
    {
        if (!freqLock)
        {
            freqt f;
            if (cmd->suffix < receivers.size()) {
                f.Hz = roundFrequencyWithStep(receivers[cmd->suffix]->getFrequency().Hz, cmd->value, tsWfScrollHz);
            } else {
                f.Hz = 0;
            }
            f.MHzDouble = f.Hz / double(1E6);
            f.VFO=(selVFO_t)cmd->suffix;
            vfoCommandType t = queue->getVfoCommand(vfo?vfoB:vfoA,cmd->suffix,true);
            //qInfo() << "sending command" << funcString[t.freqFunc] << "freq:" << f.Hz << "rx:" << t.receiver << "vfo:" << t.vfo;
            queue->add(priorityImmediate,queueItem(t.freqFunc,QVariant::fromValue<freqt>(f),false,t.receiver));
        }
        break;
    }

    // These are 2 byte values, so use ushort (even though they would fit in a uchar)
    case funcCwPitch:
    case funcKeySpeed:
    case funcAfGain:
    case funcRfGain:
    case funcSquelch:
    case funcAPFLevel:
    case funcNRLevel:
    case funcPBTInner:
    case funcPBTOuter:
    case funcIFShift:
    case funcRFPower:
    case funcMicGain:
    case funcLANModLevel:
    case funcUSBModLevel:
    case funcACCAModLevel:
    case funcNotchFilter:
    case funcCompressorLevel:
    case funcBreakInDelay:
    case funcNBLevel:
    case funcDigiSelShift:
    case funcDriveGain:
    case funcMonitorGain:
    case funcVoxGain:
    case funcAntiVoxGain:
        qInfo(logUsbControl()) << "Command" << cmd->command << "Value" << cmd->value;
        queue->add(priorityImmediate,queueItem((funcs)cmd->command,QVariant::fromValue<ushort>(cmd->value),false,cmd->suffix));
        break;

    case funcTransceiverStatus:
    {
        if (cmd->value == -1)
        {
            // toggle PTT
            on_transmitBtn_clicked();
            break;
        }

    }
    default:
        qInfo(logUsbControl()) << "Command" << cmd->command << "Value" << cmd->value << "Suffix" << cmd->suffix;
        if (cmd->suffix == 0xff) {
            queue->add(priorityImmediate,queueItem((funcs)cmd->command,QVariant::fromValue<uchar>(cmd->value),false,cmd->suffix));
        } else {
            queue->add(priorityImmediate,queueItem((funcs)cmd->command,QVariant::fromValue<uchar>(cmd->suffix),false,0));
        }
        break;
    }
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif
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

    if (receivers.size())
    {
        freqt f;
        f.Hz = roundFrequencyWithStep(receivers[0]->getFrequency().Hz, value, tsWfScrollHz);
        f.MHzDouble = f.Hz / (double)1E6;
        vfoCommandType t = queue->getVfoCommand(vfoA,currentReceiver,true);
        queue->add(priorityImmediate,queueItem(t.freqFunc,
                                                QVariant::fromValue<freqt>(f),false,uchar(t.receiver)));
    }
}

void wfmain::setDefPrefs()
{
    defPrefs.hasRunSetup = false;
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
    defPrefs.pttType = pttCIV;
    defPrefs.serialPortRadio = QString("auto");
    defPrefs.serialPortBaud = 115200;
    defPrefs.enableLAN = false;
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
    defPrefs.confirmSettingsChanged = true;
    defPrefs.confirmMemories = false;
    defPrefs.meter1Type = meterS;
    defPrefs.meter2Type = meterNone;
    defPrefs.meter3Type = meterNone;
    defPrefs.compMeterReverse = false;
    defPrefs.region = "1";
    defPrefs.showBands = true;
    defPrefs.manufacturer = manufIcom;
    defPrefs.useUTC = false;
    defPrefs.setRadioTime = false;
    defPrefs.forceVfoMode = true;
    defPrefs.autoPowerOn=true;

    defPrefs.tcpPort = 0;
    defPrefs.tciPort = 50001;
    defPrefs.clusterUdpEnable = false;
    defPrefs.clusterTcpEnable = false;
    defPrefs.waterfallFormat = 0;
    defPrefs.audioSystem = qtAudio;
    defPrefs.enableUSBControllers = false;
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    defPrefs.groupSeparator = QLocale().groupSeparator();
    defPrefs.decimalSeparator = QLocale().decimalPoint();
#else
    defPrefs.groupSeparator = QLocale().groupSeparator().at(0);       // digit group separator
    defPrefs.decimalSeparator = QLocale().decimalPoint().at(0);       // digit group separator
#endif

    // Audio
    defPrefs.rxSetup.latency = 150;
    defPrefs.txSetup.latency = 150;
    defPrefs.rxSetup.isinput = false;
    defPrefs.txSetup.isinput = true;
    defPrefs.rxSetup.sampleRate = 48000;
    defPrefs.rxSetup.codec = 4;
    defPrefs.txSetup.codec = 4;
    defPrefs.rxSetup.resampleQuality = 4;
	
	// Cluster
	defPrefs.clusterUdpEnable = false;
    defPrefs.clusterTcpEnable = false;
    defPrefs.clusterUdpPort = 12060;
	
	// CW
	defPrefs.cwCutNumbers=false;
    defPrefs.cwSendImmediate=false;
    defPrefs.cwSidetoneEnabled=true;
    defPrefs.cwSidetoneLevel=100;

    udpDefPrefs.ipAddress = QString("");
    udpDefPrefs.controlLANPort = 50001;
    udpDefPrefs.serialLANPort = 50002;
    udpDefPrefs.audioLANPort = 50003;
    udpDefPrefs.scopeLANPort = 50004;
    udpDefPrefs.username = QString("");
    udpDefPrefs.password = QString("");
    udpDefPrefs.clientName = QHostInfo::localHostName();
    udpDefPrefs.connectionType = connectionLAN;
    udpDefPrefs.adminLogin = false;
}

void wfmain::loadSettings()
{
    qInfo(logSystem()) << "Loading settings from " << settings->fileName();

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
    prefs.hasRunSetup = settings->value("hasRunSetup", defPrefs.hasRunSetup).toBool();
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
    prefs.decimalSeparator = settings->value("DecimalSeparator", defPrefs.decimalSeparator).toChar();
    prefs.groupSeparator = settings->value("GroupSeparator", defPrefs.groupSeparator).toChar();
    prefs.forceVfoMode =  settings->value("ForceVfoMode", defPrefs.groupSeparator).toBool();
    prefs.autoPowerOn =  settings->value("AutoPowerOn", defPrefs.autoPowerOn).toBool();

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

    if (bandbtns != Q_NULLPTR)
        bandbtns->setGeometry(settings->value("BandWindowGeometry").toByteArray());

    if (finputbtns != Q_NULLPTR)
        finputbtns->setGeometry(settings->value("FreqWindowGeometry").toByteArray());

    prefs.confirmExit = settings->value("ConfirmExit", defPrefs.confirmExit).toBool();
    prefs.confirmPowerOff = settings->value("ConfirmPowerOff", defPrefs.confirmPowerOff).toBool();
    prefs.confirmSettingsChanged = settings->value("ConfirmSettingsChanged", defPrefs.confirmSettingsChanged).toBool();
    prefs.confirmMemories = settings->value("ConfirmMemories", defPrefs.confirmMemories).toBool();
    prefs.meter1Type = static_cast<meter_t>(settings->value("Meter1Type", defPrefs.meter1Type).toInt());
    prefs.meter2Type = static_cast<meter_t>(settings->value("Meter2Type", defPrefs.meter2Type).toInt());
    prefs.meter3Type = static_cast<meter_t>(settings->value("Meter3Type", defPrefs.meter3Type).toInt());
    prefs.compMeterReverse = settings->value("compMeterReverse", defPrefs.compMeterReverse).toBool();
    prefs.clickDragTuningEnable = settings->value("ClickDragTuning", false).toBool();

    prefs.useUTC = settings->value("UseUTC", defPrefs.useUTC).toBool();
    prefs.setRadioTime = settings->value("SetRadioTime", defPrefs.setRadioTime).toBool();

    prefs.rigCreatorEnable = settings->value("RigCreator",false).toBool();
    prefs.region = settings->value("Region",defPrefs.region).toString();
    bandbtns->setRegion(prefs.region);
    prefs.showBands = settings->value("ShowBands",defPrefs.showBands).toBool();

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
            p->gridColor = colorFromString(settings->value("gridColor", p->gridColor.name(QColor::HexArgb)).toString());
            p->axisColor = colorFromString(settings->value("axisColor", p->axisColor.name(QColor::HexArgb)).toString());
            p->textColor = colorFromString(settings->value("textColor", p->textColor.name(QColor::HexArgb)).toString());
            p->spectrumLine = colorFromString(settings->value("spectrumLine", p->spectrumLine.name(QColor::HexArgb)).toString());
            p->spectrumFill = colorFromString(settings->value("spectrumFill", p->spectrumFill.name(QColor::HexArgb)).toString());
            p->useSpectrumFillGradient = settings->value("useSpectrumFillGradient", p->useSpectrumFillGradient).toBool();
            p->spectrumFillTop = colorFromString(settings->value("spectrumFillTop", p->spectrumFillTop.name(QColor::HexArgb)).toString());
            p->spectrumFillBot = colorFromString(settings->value("spectrumFillBot", p->spectrumFillBot.name(QColor::HexArgb)).toString());
            p->underlayLine = colorFromString(settings->value("underlayLine", p->underlayLine.name(QColor::HexArgb)).toString());
            p->underlayFill = colorFromString(settings->value("underlayFill", p->underlayFill.name(QColor::HexArgb)).toString());
            p->useUnderlayFillGradient = settings->value("useUnderlayFillGradient", p->useUnderlayFillGradient).toBool();
            p->underlayFillTop = colorFromString(settings->value("underlayFillTop", p->underlayFillTop.name(QColor::HexArgb)).toString());
            p->underlayFillBot = colorFromString(settings->value("underlayFillBot", p->underlayFillBot.name(QColor::HexArgb)).toString());
            p->plotBackground = colorFromString(settings->value("plotBackground", p->plotBackground.name(QColor::HexArgb)).toString());
            p->tuningLine = colorFromString(settings->value("tuningLine", p->tuningLine.name(QColor::HexArgb)).toString());
            p->passband = colorFromString(settings->value("passband", p->passband.name(QColor::HexArgb)).toString());
            p->pbt = colorFromString(settings->value("pbt", p->pbt.name(QColor::HexArgb)).toString());
            p->wfBackground = colorFromString(settings->value("wfBackground", p->wfBackground.name(QColor::HexArgb)).toString());
            p->wfGrid = colorFromString(settings->value("wfGrid", p->wfGrid.name(QColor::HexArgb)).toString());
            p->wfAxis = colorFromString(settings->value("wfAxis", p->wfAxis.name(QColor::HexArgb)).toString());
            p->wfText = colorFromString(settings->value("wfText", p->wfText.name(QColor::HexArgb)).toString());
            p->meterLevel = colorFromString(settings->value("meterLevel", p->meterLevel.name(QColor::HexArgb)).toString());
            p->meterAverage = colorFromString(settings->value("meterAverage", p->meterAverage.name(QColor::HexArgb)).toString());
            p->meterPeakLevel = colorFromString(settings->value("meterPeakLevel", p->meterPeakLevel.name(QColor::HexArgb)).toString());
            p->meterPeakScale = colorFromString(settings->value("meterPeakScale", p->meterPeakScale.name(QColor::HexArgb)).toString());
            p->meterLowerLine = colorFromString(settings->value("meterLowerLine", p->meterLowerLine.name(QColor::HexArgb)).toString());
            p->meterLowText = colorFromString(settings->value("meterLowText", p->meterLowText.name(QColor::HexArgb)).toString());
            p->clusterSpots = colorFromString(settings->value("clusterSpots", p->clusterSpots.name(QColor::HexArgb)).toString());
            p->buttonOff = colorFromString(settings->value("buttonOff", p->buttonOff.name(QColor::HexArgb)).toString());
            p->buttonOn = colorFromString(settings->value("buttonOn", p->buttonOn.name(QColor::HexArgb)).toString());
        }
    }
    settings->endArray();
    settings->endGroup();

    // Radio and Comms: C-IV addr, port to use
    settings->beginGroup("Radio");
    prefs.manufacturer = (manufacturersType_t)settings->value("Manufacturer", defPrefs.manufacturer).value<manufacturersType_t>();
    prefs.radioCIVAddr = (quint16)settings->value("RigCIVuInt", defPrefs.radioCIVAddr).toInt();
    prefs.CIVisRadioModel = (bool)settings->value("CIVisRadioModel", defPrefs.CIVisRadioModel).toBool();
    prefs.pttType = (pttType_t)settings->value("PTTType", defPrefs.pttType).toInt();
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


    prefs.localAFgain = (quint8)settings->value("localAFgain", defPrefs.localAFgain).toUInt();
    prefs.rxSetup.localAFgain = prefs.localAFgain;
    prefs.txSetup.localAFgain = 255;

    prefs.audioSystem = static_cast<audioType>(settings->value("AudioSystem", defPrefs.audioSystem).toInt());

    settings->endGroup();

    // Misc. user settings (enable PTT, draw peaks, etc)
    settings->beginGroup("Controls");
    prefs.enablePTT = settings->value("EnablePTT", defPrefs.enablePTT).toBool();

    prefs.niceTS = settings->value("NiceTS", defPrefs.niceTS).toBool();

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

    prefs.tcpPort = settings->value("TCPServerPort", defPrefs.tcpPort).toInt();
    prefs.tciPort = settings->value("TCIServerPort", defPrefs.tciPort).toInt();

    prefs.waterfallFormat = settings->value("WaterfallFormat", defPrefs.waterfallFormat).toInt();

    udpPrefs.ipAddress = settings->value("IPAddress", udpDefPrefs.ipAddress).toString();
    udpPrefs.controlLANPort = settings->value("ControlLANPort", udpDefPrefs.controlLANPort).toInt();
    udpPrefs.serialLANPort = settings->value("SerialLANPort", udpDefPrefs.serialLANPort).toInt();
    udpPrefs.audioLANPort = settings->value("AudioLANPort", udpDefPrefs.audioLANPort).toInt();
    udpPrefs.scopeLANPort = settings->value("ScopeLANPort", udpDefPrefs.scopeLANPort).toInt();
    udpPrefs.adminLogin = settings->value("AdminLogin",udpDefPrefs.adminLogin).toBool();
    udpPrefs.username = settings->value("Username", udpDefPrefs.username).toString();
    udpPrefs.password = settings->value("Password", udpDefPrefs.password).toString();
    prefs.rxSetup.isinput = defPrefs.rxSetup.isinput;
    prefs.txSetup.isinput = defPrefs.txSetup.isinput;

    prefs.rxSetup.latency = settings->value("AudioRXLatency", defPrefs.rxSetup.latency).toInt();
    prefs.txSetup.latency = settings->value("AudioTXLatency", defPrefs.txSetup.latency).toInt();

    prefs.rxSetup.sampleRate=settings->value("AudioRXSampleRate", defPrefs.rxSetup.sampleRate).toInt();
    prefs.txSetup.sampleRate=prefs.rxSetup.sampleRate;

    prefs.rxSetup.codec = settings->value("AudioRXCodec", defPrefs.rxSetup.codec).toInt();
    prefs.txSetup.codec = settings->value("AudioTXCodec", defPrefs.txSetup.codec).toInt();
    prefs.rxSetup.name = settings->value("AudioOutput", "Default Output Device").toString();
    qInfo(logGui()) << "Got Audio Output from Settings: " << prefs.rxSetup.name;

    prefs.txSetup.name = settings->value("AudioInput", "Default Input Device").toString();
    qInfo(logGui()) << "Got Audio Input from Settings: " << prefs.txSetup.name;

    prefs.rxSetup.resampleQuality = settings->value("ResampleQuality", defPrefs.rxSetup.resampleQuality).toInt();
    prefs.txSetup.resampleQuality = prefs.rxSetup.resampleQuality;

    if (prefs.tciPort > 0 && tci == Q_NULLPTR) {

        tci = new tciServer();

        tciThread = new QThread(this);
        tciThread->setObjectName("TCIServer()");
        tci->moveToThread(tciThread);
        connect(queue,SIGNAL(rigCapsUpdated(rigCapabilities*)),tci, SLOT(receiveRigCaps(rigCapabilities*)));
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

    udpPrefs.connectionType = settings->value("ConnectionType", udpDefPrefs.connectionType).value<connectionType_t>();
    udpPrefs.clientName = settings->value("ClientName", udpDefPrefs.clientName).toString();

    udpPrefs.halfDuplex = settings->value("HalfDuplex", udpDefPrefs.halfDuplex).toBool();

    settings->endGroup();

    settings->beginGroup("Server");
    setupui->acceptServerConfig(&serverConfig);

    serverConfig.enabled = settings->value("ServerEnabled", false).toBool();
    serverConfig.disableUI = settings->value("DisableUI", false).toBool();
    // These defPrefs are actually for the client, but they are the same.
    serverConfig.controlPort = settings->value("ServerControlPort", udpDefPrefs.controlLANPort).toInt();
    serverConfig.civPort = settings->value("ServerCivPort", udpDefPrefs.serialLANPort).toInt();
    serverConfig.audioPort = settings->value("ServerAudioPort", udpDefPrefs.audioLANPort).toInt();

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
    rigTemp->pttType = prefs.pttType; // Use the global PTT type.

    rigTemp->baudRate = prefs.serialPortBaud;
    rigTemp->civAddr = 0;
    rigTemp->serialPort = prefs.serialPortRadio;
    QString guid = settings->value("GUID", "").toString();
    if (guid.isEmpty()) {
        guid = QUuid::createUuid().toString();
        settings->setValue("GUID", guid);
    }
#if (QT_VERSION >= QT_VERSION_CHECK(5,10,0))
    memcpy(rigTemp->guid, QUuid::fromString(guid).toRfc4122().constData(), GUIDLEN);
#endif

    rigTemp->rxAudioSetup.name = settings->value("ServerAudioInput", "").toString();
    rigTemp->txAudioSetup.name = settings->value("ServerAudioOutput", "").toString();

    serverConfig.rigs.append(rigTemp);

    // At this point, the users list has exactly one empty user.
    //setupui->updateServerConfig(s_users);

    settings->endGroup();

    // Memory channels

    settings->beginGroup("Memory");
    int size = settings->beginReadArray("Channel");
    int chan = 0;
    double freq;
    quint8 mode;
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

    prefs.clusterUdpEnable = settings->value("UdpEnabled", defPrefs.clusterUdpEnable).toBool();
    prefs.clusterTcpEnable = settings->value("TcpEnabled", defPrefs.clusterTcpEnable).toBool();
    prefs.clusterUdpPort = settings->value("UdpPort", defPrefs.clusterUdpPort).toInt();

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
    prefs.cwCutNumbers=settings->value("CutNumbers", defPrefs.cwCutNumbers).toBool();
    prefs.cwSendImmediate=settings->value("SendImmediate", defPrefs.cwSendImmediate).toBool();
    prefs.cwSidetoneEnabled=settings->value("SidetoneEnabled", defPrefs.cwSidetoneEnabled).toBool();
    prefs.cwSidetoneLevel=settings->value("SidetoneLevel", defPrefs.cwSidetoneLevel).toInt();

    int numMemories = settings->beginReadArray("macros");
    if(numMemories==10)
    {
        for(int m=0; m < 10; m++)
        {
            settings->setArrayIndex(m);
            prefs.cwMacroList << settings->value("macroText", "").toString();
        }
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
            tempPrefs.color = colorFromString(settings->value("Color", QColor(Qt::white).name(QColor::HexArgb)).toString());
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
            butt.textColour = colorFromString(settings->value("Colour", QColor(Qt::white).name(QColor::HexArgb)).toString());
            butt.backgroundOn = colorFromString(settings->value("BackgroundOn", QColor(Qt::lightGray).name(QColor::HexArgb)).toString());
            butt.backgroundOff = colorFromString(settings->value("BackgroundOff", QColor(Qt::blue).name(QColor::HexArgb)).toString());
            butt.toggle = settings->value("Toggle", false).toBool();
            // PET add Linux as it stops Qt6 building FIXME
#if (QT_VERSION > QT_VERSION_CHECK(6,0,0) && !defined(Q_OS_LINUX) && !defined(Q_OS_MACOS))
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
    setupui->acceptUdpPreferencesPtr(&udpPrefs);

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
        for (const auto& receiver: receivers) {
            receiver->setUnderlayMode(prefs.underlayMode);
        }
        break;
    case if_underlayBufferSize:
        for (const auto& receiver: receivers) {
            receiver->resizePlasmaBuffer(prefs.underlayBufferSize);
        }
        break;
    case if_wfAntiAlias:
        for (const auto& receiver: receivers) {
            receiver->wfAntiAliased(prefs.wfAntiAlias);
        }
        break;
    case if_wfInterpolate:
        for (const auto& receiver: receivers) {
            receiver->wfInterpolate(prefs.wfInterpolate);
        }
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
    case if_meter1Type:
        changeMeterType(prefs.meter1Type, 1);
        break;
    case if_meter2Type:
        changeMeterType(prefs.meter2Type, 2);
        break;
    case if_meter3Type:
        changeMeterType(prefs.meter3Type, 3);
        break;
    case if_clickDragTuningEnable:
        // There's nothing to do here since the code
        // already uses the preference variable as state.
        break;
    case if_compMeterReverse:
        ui->meter2Widget->setCompReverse(prefs.compMeterReverse);
        ui->meter3Widget->setCompReverse(prefs.compMeterReverse);
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
        for (const auto& receiver: receivers)
        {
            receiver->setUnit((FctlUnit)prefs.frequencyUnits);
        }
        break;
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
    case if_region:        
        bandbtns->setRegion(prefs.region);
        // Allow to fallthrough so that band indicators are updated correctly on region change.
    case if_showBands:
        for (const auto& receiver: receivers)
        {
            receiver->setBandIndicators(prefs.showBands, prefs.region, &rigCaps->bands);
        }
        break;
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif
    case if_separators:
        for (const auto& receiver: receivers)
        {
            receiver->setSeparators(prefs.groupSeparator,prefs.decimalSeparator);
        }
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

#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif

    // Any updated scope colors will cause the scope colorPreset to be changed.
    case col_buttonOff:
    case col_buttonOn:
        ui->scopeDualBtn->setStyleSheet(QString("QPushButton {background-color: %0;} QPushButton:checked {background-color: %1;border: 1px solid;}")
                                        .arg(cp->buttonOff.name(QColor::HexArgb),cp->buttonOn.name(QColor::HexArgb)));
        ui->dualWatchBtn->setStyleSheet(QString("QPushButton {background-color: %0;} QPushButton:checked {background-color: %1;border: 1px solid;}")
                                        .arg(cp->buttonOff.name(QColor::HexArgb),cp->buttonOn.name(QColor::HexArgb)));
        ui->splitBtn->setStyleSheet(QString("QPushButton {background-color: %0;} QPushButton:checked {background-color: %1;border: 1px solid;}")
                                        .arg(cp->buttonOff.name(QColor::HexArgb),cp->buttonOn.name(QColor::HexArgb)));
        //    ui->mainSubTrackingBtn->setStyleSheet(QString("QPushButton {background-color: %0;} QPushButton:checked {background-color: %1;border: 1px solid;}")
        //                                    .arg(cp->buttonOff.name(QColor::HexArgb),cp->buttonOn.name(QColor::HexArgb)));
    case col_grid:
    case col_axis:
    case col_text:
    case col_plotBackground:
    case col_spectrumLine:
    case col_spectrumFill:
    case col_useSpectrumFillGradient:
    case col_spectrumFillTop:
    case col_spectrumFillBot:
    case col_underlayLine:
    case col_underlayFill:
    case col_underlayFillTop:
    case col_underlayFillBot:
    case col_useUnderlayFillGradient:
    case col_tuningLine:
    case col_passband:
    case col_pbtIndicator:
    case col_waterfallBack:
    case col_waterfallGrid:
    case col_waterfallAxis:
    case col_waterfallText:
    case col_clusterSpots:
        for (const auto& receiver: receivers)
        {
            receiver->colorPreset(cp);
        }
        break;
    // Any updated meter colors, will cause the meter to be updated.
    case col_meterLevel:
    case col_meterAverage:
    case col_meterPeakLevel:
    case col_meterHighScale:
    case col_meterScale:
    case col_meterText:
        ui->meterSPoWidget->setColors(cp->meterLevel, cp->meterPeakScale, cp->meterPeakLevel, cp->meterAverage, cp->meterLowerLine, cp->meterLowText);
        ui->meter2Widget->setColors(cp->meterLevel, cp->meterPeakScale, cp->meterPeakLevel, cp->meterAverage, cp->meterLowerLine, cp->meterLowText);
        ui->meter3Widget->setColors(cp->meterLevel, cp->meterPeakScale, cp->meterPeakLevel, cp->meterAverage, cp->meterLowerLine, cp->meterLowText);
        break;

#if defined __GNUC__
#pragma GCC diagnostic pop
#endif

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
    case ra_pttType:
        // Not used as cannot be connected to radio when this is updated.
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
    case ra_manufacturer:
        setManufacturer(prefs.manufacturer);
        break;
    default:
        qWarning(logSystem()) << "Cannot update wfmain ra pref" << (int)i;
    }
}

void wfmain::setManufacturer(manufacturersType_t man)
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
                quint16 civ = rigSettings->value("CIVAddress",0).toInt();
                QString model = rigSettings->value("Model","").toString();
                QString path = systemRigDir.absoluteFilePath(rig);

                qDebug() << QString("Found Rig %0 with CI-V address of 0x%1 and version %2").arg(model).arg(civ,4,16,QChar('0')).arg(ver,0,'f',2);
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
                quint16 civ = rigSettings->value("CIVAddress",0).toInt();
                QString model = rigSettings->value("Model","").toString();
                QString path = userRigDir.absoluteFilePath(rig);

                auto it = this->rigList.find(civ);

                if (it != this->rigList.end())
                {
                    if (ver >= it.value().version) {
                        qInfo() << QString("Found User Rig %0 with CI-V address of 0x%1 and newer or same version than system one (%2>=%3)").arg(model).arg(civ,4,16,QChar('0')).arg(ver,0,'f',2).arg(it.value().version,0,'f',2);
                        this->rigList.insert(civ,rigInfo(civ,model,path,ver));
                    }
                } else {
                    qInfo() << QString("Found New User Rig %0 with CI-V address of 0x%1 version %2").arg(model).arg(civ,4,16,QChar('0')).arg(ver,0,'f',2);
                    this->rigList.insert(civ,rigInfo(civ,model,path,ver));
                }
                // Any user modified rig files will override system provided ones.
            }
            rigSettings->endGroup();
            delete rigSettings;
        }
    }

}

void wfmain::extChangedRsPref(prefRsItem i)
{
    prefs.settingsChanged = true;
    // Radio Settings prefs
    switch(i)
    {
    case rs_dataOffMod:
        queue->add(priorityImmediate,queueItem(funcDATAOffMod,QVariant::fromValue<rigInput>(prefs.inputSource[0]),false,currentReceiver));
        queue->addUnique(priorityHigh,queueItem(getInputTypeCommand(prefs.inputSource[0].type).cmd,true,currentReceiver));
        break;
    case rs_data1Mod:
        queue->add(priorityImmediate,queueItem(funcDATA1Mod,QVariant::fromValue<rigInput>(prefs.inputSource[1]),false,currentReceiver));
        queue->addUnique(priorityHigh,queueItem(getInputTypeCommand(prefs.inputSource[1].type).cmd,true,currentReceiver));
        break;
    case rs_data2Mod:
        queue->add(priorityImmediate,queueItem(funcDATA2Mod,QVariant::fromValue<rigInput>(prefs.inputSource[2]),false,currentReceiver));
        queue->addUnique(priorityHigh,queueItem(getInputTypeCommand(prefs.inputSource[2].type).cmd,true,currentReceiver));
        break;
    case rs_data3Mod:
        queue->add(priorityImmediate,queueItem(funcDATA3Mod,QVariant::fromValue<rigInput>(prefs.inputSource[3]),false,currentReceiver));
        queue->addUnique(priorityHigh,queueItem(getInputTypeCommand(prefs.inputSource[3].type).cmd,true,currentReceiver));
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
        queue->add(priorityImmediate,queueItem(funcTransceiverStatus,QVariant::fromValue<bool>(true),false,uchar(0)));
        pttTimer->start();
        break;
    case rs_pttOff:
        showStatusBarText("Sending PTT OFF command");
        queue->add(priorityImmediate,queueItem(funcTransceiverStatus,QVariant::fromValue<bool>(false),false,uchar(0)));
        pttTimer->stop();
        break;
    case rs_satOps:
        sat->show();
        break;
    case rs_adjRef:
        cal->show();
        break;
    case rs_clockUseUtc:
    case rs_setRadioTime:
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
        for (const auto& receiver: receivers)
        {
            receiver->setTuningFloorZeros(prefs.niceTS);
        }

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
#if defined(USB_CONTROLLER)
            usbButtons.clear();
            usbKnobs.clear();
            usbDevices.clear();
#endif
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
        if (connStatus == connConnected)
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
    case u_scopeLANPort:
        // Not used in the UI.
        break;
    case u_username:
        break;
    case u_password:
        break;
    case u_clientName:
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
    case u_connectionType:
        break;
    case u_adminLogin:
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
    case s_disableui:
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
        //qInfo() << "Server users updated :" << serverConfig.users.count() << "total users";
        break;
    case s_rigs:
        break;
    default:
        qWarning(logGui()) << "Did not find matching pref element in wfmain for Server pref item " << (int)i;
        break;
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
    settings->setValue("hasRunSetup", prefs.hasRunSetup);
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

    if (bandbtns != Q_NULLPTR)
        settings->setValue("BandWindowGeometry", bandbtns->getGeometry());
    if (finputbtns != Q_NULLPTR)
        settings->setValue("FreqWindowGeometry", finputbtns->getGeometry());

    settings->setValue("windowState", saveState());
    settings->setValue("MainWFLength", prefs.mainWflength);
    settings->setValue("SubWFLength", prefs.subWflength);
    settings->setValue("ConfirmExit", prefs.confirmExit);
    settings->setValue("ConfirmPowerOff", prefs.confirmPowerOff);
    settings->setValue("ConfirmSettingsChanged", prefs.confirmSettingsChanged);
    settings->setValue("ConfirmMmories", prefs.confirmMemories);
    settings->setValue("Meter1Type", (int)prefs.meter1Type);
    settings->setValue("Meter2Type", (int)prefs.meter2Type);
    settings->setValue("Meter3Type", (int)prefs.meter3Type);
    settings->setValue("compMeterReverse", prefs.compMeterReverse);
    settings->setValue("ClickDragTuning", prefs.clickDragTuningEnable);

    settings->setValue("UseUTC",prefs.useUTC);
    settings->setValue("SetRadioTime",prefs.setRadioTime);

    settings->setValue("RigCreator",prefs.rigCreatorEnable);
    settings->setValue("FrequencyUnits",prefs.frequencyUnits);
    settings->setValue("Region",prefs.region);
    settings->setValue("ShowBands",prefs.showBands);
    settings->setValue("GroupSeparator",prefs.groupSeparator);
    settings->setValue("DecimalSeparator",prefs.decimalSeparator);
    settings->setValue("ForceVfoMode",prefs.forceVfoMode);
    settings->setValue("AutoPowerOn",prefs.autoPowerOn);

    settings->endGroup();

    // Radio and Comms: C-IV addr, port to use
    settings->beginGroup("Radio");
    settings->setValue("Manufacturer", prefs.manufacturer);
    settings->setValue("RigCIVuInt", prefs.radioCIVAddr);
    settings->setValue("CIVisRadioModel", prefs.CIVisRadioModel);
    settings->setValue("PTTType", prefs.pttType);
    settings->setValue("polling_ms", prefs.polling_ms); // 0 = automatic
    if (!prefs.serialPortRadio.isEmpty())
        settings->setValue("SerialPortRadio", prefs.serialPortRadio);
    if (prefs.serialPortBaud != 0)
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
    settings->setValue("ScopeLANPort", udpPrefs.scopeLANPort);
    settings->setValue("AdminLogin",udpPrefs.adminLogin);
    settings->setValue("Username", udpPrefs.username);
    settings->setValue("Password", udpPrefs.password);
    settings->setValue("AudioRXLatency", prefs.rxSetup.latency);
    settings->setValue("AudioTXLatency", prefs.txSetup.latency);
    settings->setValue("AudioRXSampleRate", prefs.rxSetup.sampleRate);
    settings->setValue("AudioRXCodec", prefs.rxSetup.codec);
    settings->setValue("AudioTXSampleRate", prefs.txSetup.sampleRate);
    settings->setValue("AudioTXCodec", prefs.txSetup.codec);
    if (!prefs.rxSetup.name.isEmpty())
        settings->setValue("AudioOutput", prefs.rxSetup.name);
    if (!prefs.txSetup.name.isEmpty())
        settings->setValue("AudioInput", prefs.txSetup.name);
    settings->setValue("ResampleQuality", prefs.rxSetup.resampleQuality);
    settings->setValue("ClientName", udpPrefs.clientName);
    settings->setValue("WaterfallFormat", prefs.waterfallFormat);
    settings->setValue("HalfDuplex", udpPrefs.halfDuplex);
    settings->setValue("ConnectionType", udpPrefs.connectionType);

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
        settings->setValue("useSpectrumFillGradient", p->useSpectrumFillGradient);
        settings->setValue("spectrumFillTop", p->spectrumFillTop.name(QColor::HexArgb));
        settings->setValue("spectrumFillBot", p->spectrumFillBot.name(QColor::HexArgb));
        settings->setValue("underlayLine", p->underlayLine.name(QColor::HexArgb));
        settings->setValue("underlayFill", p->underlayFill.name(QColor::HexArgb));
        settings->setValue("useUnderlayFillGradient", p->useUnderlayFillGradient);
        settings->setValue("underlayFillTop", p->underlayFillTop.name(QColor::HexArgb));
        settings->setValue("underlayFillBot", p->underlayFillBot.name(QColor::HexArgb));
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
        settings->setValue("buttonOff", p->buttonOff.name(QColor::HexArgb));
        settings->setValue("buttonOn", p->buttonOn.name(QColor::HexArgb));
    }
    settings->endArray();
    settings->endGroup();

    settings->beginGroup("Server");

    settings->setValue("ServerEnabled", serverConfig.enabled);
    settings->setValue("DisableUI", serverConfig.disableUI);
    settings->setValue("ServerControlPort", serverConfig.controlPort);
    settings->setValue("ServerCivPort", serverConfig.civPort);
    settings->setValue("ServerAudioPort", serverConfig.audioPort);
    if (!serverConfig.rigs.first()->txAudioSetup.name.isEmpty())
        settings->setValue("ServerAudioOutput", serverConfig.rigs.first()->txAudioSetup.name);
    if (!serverConfig.rigs.first()->rxAudioSetup.name.isEmpty())
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

    if (cw != Q_NULLPTR) {
        prefs.cwCutNumbers = cw->getCutNumbers();
        prefs.cwSendImmediate = cw->getSendImmediate();
        prefs.cwSidetoneEnabled = cw->getSidetoneEnable();
        prefs.cwSidetoneLevel = cw->getSidetoneLevel();
        prefs.cwMacroList = cw->getMacroText();
    }

    settings->setValue("CutNumbers", prefs.cwCutNumbers);
    settings->setValue("SendImmediate", prefs.cwSendImmediate);
    settings->setValue("SidetoneEnabled", prefs.cwSidetoneEnabled);
    settings->setValue("SidetoneLevel", prefs.cwSidetoneLevel);

    if(prefs.cwMacroList.length() == 10)
    {
        settings->beginWriteArray("macros");
        for(int m=0; m < 10; m++)
        {
            settings->setArrayIndex(m);
            settings->setValue("macroText", prefs.cwMacroList.at(m));
        }
        settings->endArray();
    } else {
        qDebug(logSystem()) << "Error, CW macro list is wrong length: " << prefs.cwMacroList.length();
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

    for (auto it = usbDevices.begin(); it != usbDevices.end(); it++)
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
    for (auto &s: rigCaps->steps) {
        if (tsWfScrollHz == s.hz)
        {
            for (const auto& receiver: receivers)
            {
                receiver->setStepSize(s.hz);
            }
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

    // Avoid divide by zero (should never happen but an invalid step in the rig file could cause a crash.)
    if (tsHz < 1)
        tsHz=1;

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

funcType wfmain::getInputTypeCommand(inputTypes input)
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
    case inputACCUSB:
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
    //qInfo(logSystem()) << "Input type command for" << input << "is" << funcString[func];
    funcType type;
    auto f = rigCaps->commands.find(func);
    if (f != rigCaps->commands.end())
    {
        type = f.value();
    }

    return type;
}


void wfmain:: getInitialRigState()
{

    // Initial list of queries to the radio.
    // These are made when the program starts up
    // and are used to adjust the UI to match the radio settings
    // the polling interval is set at 200ms. Faster is possible but slower
    // computers will glitch occasionally.

    queue->del(funcTransceiverId); // This command is no longer required

    if (rigCaps->commands.contains(funcAutoInformation) && (!rigCaps->hasSpectrum || prefs.enableLAN)) {
        queue->add(priorityImmediate,queueItem(funcAutoInformation,QVariant::fromValue(uchar(2)),false,0));
        // Enable metering data in the AutoInformation stream:
        queue->add(priorityImmediate,queueItem(funcALCMeter,QVariant::fromValue(uchar(1)),false,0));
        queue->add(priorityImmediate,queueItem(funcCompMeter,QVariant::fromValue(uchar(1)),false,0));
        queue->add(priorityImmediate,queueItem(funcIdMeter,QVariant::fromValue(uchar(1)),false,0));
        queue->add(priorityImmediate,queueItem(funcVdMeter,QVariant::fromValue(uchar(1)),false,0));
        queue->add(priorityImmediate,queueItem(funcSWRMeter,QVariant::fromValue(uchar(1)),false,0));
    }

    if (prefs.forceVfoMode) {
        queue->add(priorityImmediate,queueItem(funcSelectVFO,QVariant::fromValue<vfo_t>(vfoA)));
        //if (rigCaps->commands.contains(funcSatelliteMode))
        //    queue->add(priorityImmediate,funcSatelliteMode,false,0); // are we in satellite mode?.

        //if (rigCaps->commands.contains(funcVFOModeSelect))
        //    queue->add(priorityImmediate,funcVFOModeSelect); // Make sure we are in VFO mode.

        //if (rigCaps->commands.contains(funcScopeMainSub))
        //    queue->add(priorityImmediate,queueItem(funcScopeMainSub,QVariant::fromValue(uchar(0)),false,0)); // Set main scope

    }
    //if (rigCaps->commands.contains(funcVFOBandMS))
    //    queue->add(priorityImmediate,queueItem(funcVFOBandMS,QVariant::fromValue(uchar(0)),false,0));

    if(rigCaps->hasSpectrum)
    {
        // Send commands to start scope immediately        
        queue->add(priorityHigh,queueItem(funcScopeOnOff,QVariant::fromValue(quint8(1)),false));
        queue->add(priorityHigh,queueItem(funcScopeDataOutput,QVariant::fromValue(quint8(1)),false));

        // Find the scope ref limits
        auto mr = rigCaps->commands.find(funcScopeRef);
        if (mr != rigCaps->commands.end())
        {
            for (uchar f = 0; f<receivers.size();f++) {
                receivers[f]->setRefLimits(mr.value().minVal,mr.value().maxVal);
                queue->add(priorityMediumHigh,funcScopeRef,false,f);
            }
        }
    }

    quint64 start=UINT64_MAX;
    quint64 end=0;
    for (auto &band: rigCaps->bands)
    {
        if (band.region == "" || band.region == prefs.region) {
            if (start > band.lowFreq)
                start = band.lowFreq;
            if (end < band.highFreq)
                end = band.highFreq;
        }
    }

    for (const auto& receiver: receivers)
    {
        //qInfo(logSystem()) << "Display Settings start:" << start << "end:" << end;
        receiver->displaySettings(0, start, end, 1,(FctlUnit)prefs.frequencyUnits, &rigCaps->bands);
        receiver->setBandIndicators(prefs.showBands, prefs.region, &rigCaps->bands);
    }

    if (rigCaps->commands.contains(funcRitFreq))
    {
        funcType func = rigCaps->commands.find(funcRitFreq).value();
        ui->ritTuneDial->setRange(func.minVal,func.maxVal);
    }

    if (rigCaps->commands.contains(funcTime) && prefs.setRadioTime) {
        setRadioTimeDatePrep();
    }

    if (cw != Q_NULLPTR)
    {
        cw->receiveEnabled(rigCaps->commands.contains(funcCWDecode));
    }

    if (rigCaps->commands.contains(funcVOIP))
    {
        queue->add(priorityHigh,queueItem(funcVOIP,QVariant::fromValue<uchar>(prefs.rxSetup.codec==0x80?2:1),false,0));
    }
    // Put Kenwood into Shift&Width mode for filter control.
    queue->add(priorityHigh,queueItem(funcFilterControlSSB,QVariant::fromValue<bool>(true),false,0));
    queue->add(priorityHigh,queueItem(funcFilterControlData,QVariant::fromValue<bool>(true),false,0));

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
    p->pbt = QColor(0x32,0xff,0x00,0x00);
    p->meterLevel = QColor(0x14,0x8c,0xd2).darker();
    p->meterAverage = QColor(0x2f,0xb7,0xcd);
    p->meterPeakLevel = QColor(0x3c,0xa0,0xdb).lighter();
    p->meterPeakScale = QColor(Qt::red);
    p->meterLowerLine = QColor(0xed,0xf0,0xf1);
    p->meterLowText = QColor(0xef,0xf0,0xf1);

    p->wfBackground = QColor(Qt::black);
    p->wfAxis = QColor(Qt::white);
    p->wfGrid = QColor(Qt::white);
    p->wfText = QColor(Qt::white);

    p->clusterSpots = QColor(Qt::red);

    p->buttonOff = QColor("transparent");
    p->buttonOn = QColor(0x50,0x50,0x50);


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
            p->underlayLine = QColor(0x96,0x33,0xff,0xff);
            p->underlayFill = QColor(20+200/4.0*1,70*(1.6-1/4.0), 150, 150);
            p->tuningLine = QColor(0xff,0x55,0xff,0xff);
            p->passband = QColor(0x32,0xff,0xff,0x80);
            p->pbt = QColor(0x32,0xff,0x00,0x00);

            p->meterLevel = QColor(0x14,0x8c,0xd2).darker();
            p->meterAverage = QColor(0x3f,0xb7,0xcd);
            p->meterPeakScale = QColor(Qt::red);
            p->meterPeakLevel = QColor(0x3c,0xa0,0xdb).lighter();
            p->meterLowerLine = QColor(0xef,0xf0,0xf1);
            p->meterLowText = QColor(0xef,0xf0,0xf1);

            p->wfBackground = QColor(Qt::black);
            p->wfAxis = QColor(Qt::white);
            p->wfGrid = QColor("transparent");
            p->wfText = QColor(Qt::white);
            p->clusterSpots = QColor(Qt::red);
            p->buttonOff = QColor("transparent");
            p->buttonOn = QColor(0x50,0x50,0x50);
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
            p->passband = QColor(0x64,0x00,0x00,0x80);
            p->pbt = QColor(0x32,0xff,0x00,0x00);

            p->meterAverage = QColor(0x3f,0xb7,0xcd);
            p->meterPeakLevel = QColor(0x3c,0xa0,0xdb);
            p->meterPeakScale = QColor(Qt::darkRed);
            p->meterLowerLine = QColor(Qt::black);
            p->meterLowText = QColor(Qt::black);

            p->wfBackground = QColor(Qt::white);
            p->wfAxis = QColor(200,200,200,255);
            p->wfGrid = QColor("transparent");
            p->wfText = QColor(Qt::black);
            p->clusterSpots = QColor(Qt::red);
            p->buttonOff = QColor("transparent");
            p->buttonOn = QColor(0x50,0x50,0x50);
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

void wfmain::initPeriodicCommands()
{
    // This function places periodic polling commands into the queue.
    // Can be run multiple times as it will remove all existing entries.

    qInfo(logSystem()) << "Start periodic commands (and delete unsupported)";

    queue->clear();

    foreach (auto cap, rigCaps->periodic) {
        if (cap.receiver == char(-1)) {
            for (uchar v=0;v<rigCaps->numReceiver;v++)
            {
                qDebug(logSystem()) << "Inserting command" << funcString[cap.func] << "priority" << cap.priority << "on Receiver" << QString::number(v);
                queue->add(queuePriority(cap.prioVal),cap.func,true,v);
            }
        }
        else {
            qDebug(logSystem()) << "Inserting command" << funcString[cap.func] << "priority" << cap.priority << "on Receiver" << QString::number(cap.receiver);
            queue->add(queuePriority(cap.prioVal),cap.func,true,cap.receiver);
        }
    }


    // Set the second meter here as I suspect we need to be connected for it to work?
    if (!rigCaps->commands.contains(funcMeterType))
    {
        prefs.meter1Type = meterS; // Just in case we have previously connected to a radio with meter type options.
    }
    changeMeterType(prefs.meter1Type, 1);
    changeMeterType(prefs.meter2Type, 2);
    changeMeterType(prefs.meter3Type, 3);
    ui->meter2Widget->blockMeterType(prefs.meter3Type);
    ui->meter3Widget->blockMeterType(prefs.meter2Type);
    ui->meter2Widget->setCompReverse(prefs.compMeterReverse);
    ui->meter3Widget->setCompReverse(prefs.compMeterReverse);

/*
    meter* marray[3];
    marray[0] = ui->meterSPoWidget;
    marray[1] = ui->meter2Widget;
    marray[2] = ui->meter3Widget;
    for(int m=0; m < 3; m++) {
        funcs meterCmd = meter_tToMeterCommand(marray[m]->getMeterType());
        if(meterCmd != funcNone) {
            qDebug() << "Adding meter command per current UI meters.";
            queue->add(priorityHighest,queueItem(meterCmd,true));
        }
    }
*/


}

void wfmain::receivePTTstatus(bool pttOn)
{
    // This is the only place where amTransmitting and the transmit button text should be changed:
    if (pttOn && !amTransmitting)
    {

        pttLed->setState(QLedLabel::State::StateError);
        pttLed->setToolTip("Transmitting");
        changePrimaryMeter(true);
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
        changePrimaryMeter(false);
        // Send updated BSR? Currently a work-in-progress M0VSE
        //receivers[currentReceiver]->updateBSR(&rigCaps->bands);
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

void wfmain::changePrimaryMeter(bool transmitOn) {
    // Change the Primary UI Meter and alter the queue

    // This function is only called from one place:
    // When we receive a *new* PTT status.

    funcs newCmd;
    funcs oldCmd;
    double lowVal = 0.0;
    double highVal = 255.0;
    double lineVal = 200;

    if(transmitOn) {
        oldCmd = meter_tToMeterCommand(meterS);
        newCmd = meter_tToMeterCommand(meterPower);
        ui->meterSPoWidget->setMeterType(meterPower);
        ui->meterSPoWidget->setMeterShortString( meterString[(int)meterPower] );
        if (rigCaps->meters[meterPower].size())
            getMeterExtremities(meterPower, lowVal, highVal, lineVal);
        ui->meterSPoWidget->setMeterExtremities(lowVal, highVal, lineVal);

    } else {
        oldCmd = meter_tToMeterCommand(meterPower);
        newCmd = meter_tToMeterCommand(meterS);
        ui->meterSPoWidget->setMeterType(meterS);
        ui->meterSPoWidget->setMeterShortString( meterString[(int)meterS] );
        if (rigCaps->meters[meterS].size())
            getMeterExtremities(meterS, lowVal, highVal, lineVal);
        ui->meterSPoWidget->setMeterExtremities(lowVal, highVal, lineVal);
    }
    queue->del(oldCmd,0);
    if (rigCaps->commands.contains(newCmd)) {
        queue->add(priorityHighest,queueItem(newCmd,true,0));
    }
    ui->meterSPoWidget->clearMeterOnPTTtoggle();
    ui->meter2Widget->clearMeterOnPTTtoggle();
    ui->meter3Widget->clearMeterOnPTTtoggle();
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

void wfmain::changeMode(rigMode_t mode, quint8 rx)
{
    bool dataOn = false;
    if(((quint8) mode >> 4) == 0x08)
    {
        dataOn = true;
        mode = (rigMode_t)((int)mode & 0x0f);
    }

    changeMode(mode, dataOn, rx);
}

void wfmain::changeMode(rigMode_t mode, quint8 data, quint8 rx)
{
    for (modeInfo &mi: rigCaps->modes)
    {
        if (mi.mk == mode)
        {
            if(receivers.size() > rx && mi.mk != receivers[rx]->currentMode().mk)
            {
                modeInfo m;
                m = modeInfo(mi);
                m.data = data;
                m.VFO=selVFO_t::activeVFO;
                m.filter = receivers[rx]->currentFilter();
                qDebug(logRig()) << "changeMode:" << mode << "data" << data << "rx" << rx;
                vfoCommandType t = queue->getVfoCommand(vfoA,rx,true);
                queue->add(priorityImmediate,queueItem(t.modeFunc,QVariant::fromValue<modeInfo>(m),false,t.receiver));
            }

            break;
        }
    }
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
    if(abs(delta) < ui->freqDial->singleStep()) {
        // Took a small step. Let's just round it up:
        if(delta > 0)
            delta = 1;
        else
            delta = -1;
    } else {
        delta = delta / ui->freqDial->singleStep();
    }


    // With the number of steps and direction of steps established,
    // we can now adjust the frequency:

    f.Hz = roundFrequencyWithStep(receivers[currentReceiver]->getFrequency().Hz, delta, tsKnobHz);
    f.MHzDouble = f.Hz / (double)1E6;
    if (f.Hz > 0)
    {
        oldFreqDialVal = value;
        receivers[currentReceiver]->setFrequency(f);
        queue->addUnique(priorityImmediate,queueItem(queue->getVfoCommand(vfoA,currentReceiver,true).freqFunc,
                                                QVariant::fromValue<freqt>(f),false,uchar(currentReceiver)));
    } else {
        ui->freqDial->blockSignals(true);
        ui->freqDial->setValue(oldFreqDialVal);
        ui->freqDial->blockSignals(false);
        return;
    }
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
    m.reg =(quint8) m.mk; // fallback, works only for some modes
    memFreq.Hz = temp.frequency * 1E6;
    memFreq.MHzDouble = memFreq.Hz / 1.0E6;
    if (receivers.size())
        receivers[0]->setFrequency(memFreq);
    qDebug(logGui()) << "Recalling preset number " << presetNumber << " as frequency " << temp.frequency << "MHz";
}

void wfmain::saveMemoryPreset(int presetNumber)
{
    // int, double, rigMode_t
    double frequency;
    if (receivers.size()) {
        if(receivers[0]->getFrequency().Hz == 0)
        {
            frequency = receivers[0]->getFrequency().MHzDouble;
        } else {
            frequency = receivers[0]->getFrequency().Hz / 1.0E6;
        }
        rigMode_t mode = currentMode;
        qDebug(logGui()) << "Saving preset number " << presetNumber << " to frequency " << frequency << " MHz";
        mem.setPreset(presetNumber, frequency, mode);
    }
}
\
void wfmain::on_rfGainSlider_valueChanged(int value)
{
    queue->addUnique(priorityImmediate,queueItem(funcRfGain,QVariant::fromValue<ushort>(value),false,currentReceiver));
}

void wfmain::on_afGainSlider_valueChanged(int value)
{
    if(prefs.enableLAN)
    {
        // Remember current setting.
        prefs.rxSetup.localAFgain = (quint8)(value);
        prefs.localAFgain = (quint8)(value);
    }

    queue->addUnique(priorityImmediate,queueItem(funcAfGain,QVariant::fromValue<ushort>(value),currentReceiver));
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


void wfmain::on_tuneNowBtn_clicked()
{

    if (!prefs.enablePTT)
    {
        showStatusBarText("PTT is disabled, not sending command. Change under Settings tab.");
        return;
    }

    queue->addUnique(priorityImmediate,queueItem(funcTunerStatus,QVariant::fromValue<uchar>(2U)));
    showStatusBarText("Starting ATU tuning cycle...");
    ATUCheckTimer.setSingleShot(true);
    ATUCheckTimer.start(5000);
}

void wfmain::on_tuneEnableChk_clicked(bool checked)
{
    queue->addUnique(priorityImmediate,queueItem(funcTunerStatus,QVariant::fromValue<uchar>(checked)));
    showStatusBarText(QString("Turning %0 ATU").arg(checked?"on":"off"));
    ATUCheckTimer.setSingleShot(true);
    ATUCheckTimer.start(5000);
}

bool wfmain::on_exitBtn_clicked()
{
    bool ret=false;
    if (prefs.settingsChanged && prefs.confirmSettingsChanged)
    {
        QCheckBox *cb = new QCheckBox("Don't ask me again");
        cb->setToolTip("Don't ask me to confirm saving settings again");
        QMessageBox msgbox;
        msgbox.setText("Settings have changed since last save, exit anyway?\n");
        msgbox.setIcon(QMessageBox::Icon::Question);
        QAbstractButton *yesButton = msgbox.addButton(QMessageBox::Yes);
        QAbstractButton *saveButton = msgbox.addButton(QMessageBox::Save);
        msgbox.addButton(QMessageBox::No);
        msgbox.setDefaultButton(QMessageBox::Yes);
        msgbox.setCheckBox(cb);
#if (QT_VERSION >= QT_VERSION_CHECK(6,7,0))
        QObject::connect(cb, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state){
            if (state == Qt::CheckState::Checked)
#else
        QObject::connect(cb, &QCheckBox::stateChanged, this, [this](int state){
            if (static_cast<Qt::CheckState>(state) == Qt::CheckState::Checked)
#endif
            {
                prefs.confirmSettingsChanged=false;
            } else {
                prefs.confirmSettingsChanged=true;
            }
            settings->beginGroup("Interface");
            settings->setValue("ConfirmSettingsChanged", this->prefs.confirmSettingsChanged);
            settings->endGroup();
            settings->sync();
        });

        msgbox.exec();
        delete cb;

        if (msgbox.clickedButton() == yesButton) {
            QApplication::exit();
        } else if (msgbox.clickedButton() == saveButton) {
            saveSettings();
        } else {
            return true;
        }

    }

    // Are you sure?
    if (!prefs.confirmExit) {
        QApplication::exit();
    } else {
        QCheckBox *cb = new QCheckBox(tr("Don't ask me again"));
        cb->setToolTip(tr("Don't ask me to confirm exit again"));
        QMessageBox msgbox;
        msgbox.setText(tr("Are you sure you wish to exit?\n"));
        msgbox.setIcon(QMessageBox::Icon::Question);
        QAbstractButton *yesButton = msgbox.addButton(QMessageBox::Yes);
        msgbox.addButton(QMessageBox::No);
        msgbox.setDefaultButton(QMessageBox::Yes);
        msgbox.setCheckBox(cb);
#if (QT_VERSION >= QT_VERSION_CHECK(6,7,0))
        QObject::connect(cb, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state){
            if (state == Qt::CheckState::Checked)
#else
        QObject::connect(cb, &QCheckBox::stateChanged, this, [this](int state){
            if (static_cast<Qt::CheckState>(state) == Qt::CheckState::Checked)
#endif
            {
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
    }
    return ret;
}

void wfmain::handlePttLimit()
{
    // transmission time exceeded!
    showStatusBarText("Transmit timeout at 3 minutes. Sending PTT OFF command now.");
    queue->add(priorityImmediate,queueItem(funcTransceiverStatus,QVariant::fromValue<bool>(false),false,uchar(0)));
}

void wfmain::on_saveSettingsBtn_clicked()
{
    saveSettings(); // save memory, UI, and radio settings
}

void wfmain::receiveATUStatus(quint8 atustatus)
{
    // qInfo(logSystem()) << "Received ATU status update: " << (unsigned int) atustatus;
    switch(atustatus)
    {
        case 0x00:
            // ATU not active
            ui->tuneEnableChk->blockSignals(true);
            ui->tuneEnableChk->setChecked(false);
            ui->tuneEnableChk->blockSignals(false);
            if(ATUCheckTimer.isActive())
                showStatusBarText("ATU not enabled.");
            break;
        case 0x01:
            // ATU enabled
            ui->tuneEnableChk->blockSignals(true);
            ui->tuneEnableChk->setChecked(true);
            ui->tuneEnableChk->blockSignals(false);
            if(ATUCheckTimer.isActive())
                showStatusBarText("ATU enabled.");
            break;
        case 0x02:
            // ATU tuning in-progress.
            // Add command queue to check again and update status bar
            // qInfo(logSystem()) << "Received ATU status update that *tuning* is taking place";
            showStatusBarText("ATU is Tuning...");
            ATUCheckTimer.stop();
            ATUCheckTimer.start(5000);
            queue->add(priorityHighest,funcTunerStatus);
            break;
        default:
            qInfo(logSystem()) << "Did not understand ATU status: " << (unsigned int) atustatus;
            break;
    }
}

void wfmain::handleExtConnectBtn() {
    // from settings widget
    on_connectBtn_clicked();
}

void wfmain::handleRevertSettingsBtn() {
    // from settings widget
    int ret = QMessageBox::warning(this, tr("Revert settings"),
                                   tr("Are you sure you wish to reset all wfview settings?\nIf so, wfview will exit and you will need to start the program again."),
                                   QMessageBox::Ok | QMessageBox::Cancel,
                                   QMessageBox::Cancel);
    if (ret == QMessageBox::Ok) {
        qInfo(logSystem()) << "Per user request, resetting preferences.";
        prefs = defPrefs;
        udpPrefs = udpDefPrefs;
        serverConfig.enabled = false;
        serverConfig.users.clear();

        saveSettings();
        qInfo(logSystem()) << "Closing wfview for full preference-reset.";
        QApplication::exit();
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
    queue->addUnique(priorityImmediate,queueItem(funcSquelch,QVariant::fromValue<ushort>(value),false,currentReceiver));
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
        queue->add(priorityImmediate,queueItem(funcTransceiverStatus,QVariant::fromValue<bool>(true),false,uchar(0)));

        // send PTT
        // Start 3 minute timer
        pttTimer->start();

    } else {
        // Currently transmitting
        queue->add(priorityImmediate,queueItem(funcTransceiverStatus,QVariant::fromValue<bool>(false),false,uchar(0)));

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
            //now.setTime(QTime::currentTime());
        } else {
            now = QDateTime::currentDateTime();
            //now.setTime(QTime::currentTime());
        }

        int second = now.time().second();

        // 2: Find how many mseconds until next minute
        int msecdelay = QTime::currentTime().msecsTo( QTime::currentTime().addSecs(60-second) );

        // 3: Compute time and date at one minute later
        QDateTime setpoint = now.addMSecs(msecdelay); // at HMS or possibly HMS + some ms. Never under though.

        // 4: Prepare data structs for the time at one minute later
        timesetpoint.hours = (quint8)setpoint.time().hour();
        timesetpoint.minutes = (quint8)setpoint.time().minute();
        datesetpoint.day = (quint8)setpoint.date().day();
        datesetpoint.month = (quint8)setpoint.date().month();
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
    queue->add(priorityHighest,queueItem(funcTime,QVariant::fromValue<timekind>(timesetpoint)));
    queue->add(priorityHighest,queueItem(funcDate,QVariant::fromValue<datekind>(datesetpoint)));

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

void wfmain::processModLevel(inputTypes source, quint8 level)
{
    if (receivers.size()) {
        quint8 data = receivers[0]->getDataMode();

        if(prefs.inputSource[data].type == source)
        {
            prefs.inputSource[data].level = level;
            changeSliderQuietly(ui->micGainSlider, level);
        }
    }
}

void wfmain::receiveModInput(rigInput input, quint8 data)
{
    // This will ONLY fire if the input type is different to the current one
    if (currentModSrc[data].type != input.type && receivers.size())
    {
        qInfo() << QString("Data: %0 Input: %1 current: %2").arg(data).arg(input.name).arg(prefs.inputSource[data].name);
        queue->del(getInputTypeCommand(prefs.inputSource[data].type).cmd,currentReceiver);
        prefs.inputSource[data] = input;
        if (receivers[0]->getDataMode() == data)
        {
            queue->addUnique(priorityHigh,getInputTypeCommand(input.type).cmd,true,currentReceiver);
            changeModLabel(input,false);
        }
        switch (data) {
        case 0:
            setupui->updateRsPrefs((int)rs_dataOffMod);
            break;
        case 1:
            setupui->updateRsPrefs((int)rs_data1Mod);
            break;
        case 2:
            setupui->updateRsPrefs((int)rs_data2Mod);
            break;
        case 3:
            setupui->updateRsPrefs((int)rs_data3Mod);
            break;
        }
        currentModSrc[data] = rigInput(input);
    }
}

void wfmain::receiveTuningStep(quint8 step)
{
    if (step > 0)
    {
        for (auto &s: rigCaps->steps)
        {
            if (step == s.num && ui->tuningStepCombo->currentData().toUInt() != s.hz) {
                qDebug(logSystem()) << QString("Received new Tuning Step %0").arg(s.name);
                ui->tuningStepCombo->setCurrentIndex(ui->tuningStepCombo->findData(s.hz));
                for (const auto& receiver: receivers)
                {
                    receiver->setStepSize(s.hz);
                }
                break;
            }
        }
    }
}

void wfmain::receiveMeter(meter_t inMeter, double level)
{

    switch(inMeter)
    {
    // These first two meters, S and Power,
    // are automatically assigned to the primary meter.
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
            meter* marray[2];
            marray[0] = ui->meter2Widget;
            marray[1] = ui->meter3Widget;
            for(int m=0; m < 2; m++) {
                if(marray[m]->getMeterType() == inMeter)
                {
                    // The incoming meter data matches the UI meter
                    marray[m]->setLevel(level);
                } else if ( (marray[m]->getMeterType() == meterAudio) &&
                            (inMeter == meterTxMod) && amTransmitting) {
                    marray[m]->setLevel(level);
                } else if (  (marray[m]->getMeterType() == meterAudio) &&
                             (inMeter == meterRxAudio) && !amTransmitting) {
                    marray[m]->setLevel(level);
                }
            }

            break;
    }
}


void wfmain::receiveMonitor(bool en)
{
    if (en)
        ui->monitorLabel->setText(QString("<b><a href=\"#\" style=\"color:%0; text-decoration:none;\">Mon</a></b>").arg(colorPrefs->textColor.name()));
    else
        ui->monitorLabel->setText(QString("<a href=\"#\" style=\"color:%0; text-decoration:none;\">Mon</a>").arg(colorPrefs->textColor.name()));
}



void wfmain::on_txPowerSlider_valueChanged(int value)
{
    queue->addUnique(priorityImmediate,queueItem(funcRFPower,QVariant::fromValue<ushort>(value),false));
}

void wfmain::on_micGainSlider_valueChanged(int value)
{
    processChangingCurrentModLevel((quint8) value);
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

    funcType f = getInputTypeCommand(input.type);

    queue->add(priorityMedium,f.cmd,true,currentReceiver);

    ui->micGainSlider->setRange(f.minVal,f.maxVal);

    ui->modSliderLbl->setText(input.name);

    if(updateLevel)
    {
        changeSliderQuietly(ui->micGainSlider, input.level);
    }
}

void wfmain::processChangingCurrentModLevel(quint8 level)
{
    // slider moved, so find the current mod and issue the level set command.

    funcType f;
    if (receivers.size()) {
        quint8 d = receivers[currentReceiver]->getDataMode();
        f = getInputTypeCommand(prefs.inputSource[d].type);

        qDebug(logSystem()) << "Updating mod level for" << funcString[f.cmd] << "setting to" << level;
        queue->addUnique(priorityImmediate,queueItem(f.cmd,QVariant::fromValue<ushort>(level),false,currentReceiver));
    }
}

void wfmain::on_tuneLockChk_clicked(bool checked)
{
    freqLock = checked;
    for (const auto &rx: receivers)
    {
        rx->setFreqLock(checked);
    }
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
    queue->add(priorityImmediate,queueItem(funcAttenuator,QVariant::fromValue<uchar>(ui->attSelCombo->itemData(index).toInt()),false,currentReceiver));
    queue->add(priorityHigh,funcPreamp,false);
}

void wfmain::on_preampSelCombo_activated(int index)
{
    queue->add(priorityImmediate,queueItem(funcPreamp,QVariant::fromValue<uchar>(ui->preampSelCombo->itemData(index).toInt()),false,currentReceiver));
    queue->add(priorityHigh,funcAttenuator,false);
}

void wfmain::on_antennaSelCombo_activated(int index)
{
    antennaInfo ant;
    ant.antenna = (quint8)ui->antennaSelCombo->itemData(index).toInt();
    ant.rx = ui->rxAntennaCheck->isChecked();
    queue->add(priorityImmediate,queueItem(funcAntenna,QVariant::fromValue<antennaInfo>(ant),false,currentReceiver));
}

void wfmain::on_rxAntennaCheck_clicked(bool value)
{
    antennaInfo ant;
    ant.antenna = (quint8)ui->antennaSelCombo->currentData().toInt();
    ant.rx = value;
    queue->add(priorityImmediate,queueItem(funcAntenna,QVariant::fromValue<antennaInfo>(ant),false,currentReceiver));
}

void wfmain::receivePreamp(quint8 pre, uchar receiver)
{
    if (receiver == currentReceiver) {
        ui->preampSelCombo->setCurrentIndex(ui->preampSelCombo->findData(pre));
    }
}

void wfmain::receiveAttenuator(quint8 att, uchar receiver)
{
    if (receiver == currentReceiver) {
        ui->attSelCombo->setCurrentIndex(ui->attSelCombo->findData(att));
    }
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

    if(rigCaps != Q_NULLPTR && rigCaps->hasFDcomms)
    {
        queue->interval(msMinTiming);
    } else {
        queue->interval(msMinTiming * 4);
    }

    qInfo(logSystem()) << "Delay command interval timing: " << queue->interval() << "ms";

    // Normal:
    delayedCmdIntervalLAN_ms =  queue->interval();
    delayedCmdIntervalSerial_ms = queue->interval();

    // startup initial state:
    delayedCmdStartupInterval_ms =  queue->interval() * 3;
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
    QCheckBox* cb = new QCheckBox(tr("Don't ask me again"));
    QMessageBox msgbox;
    msgbox.setWindowTitle(tr("Power"));
    msgbox.setText(tr("Power down the radio?\n"));
    msgbox.setIcon(QMessageBox::Icon::Question);
    QAbstractButton* yesButton = msgbox.addButton(QMessageBox::Yes);
    msgbox.addButton(QMessageBox::No);
    msgbox.setDefaultButton(QMessageBox::Yes);
    msgbox.setCheckBox(cb);
#if (QT_VERSION >= QT_VERSION_CHECK(6,7,0))
        QObject::connect(cb, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state){
            if (state == Qt::CheckState::Checked)
#else
        QObject::connect(cb, &QCheckBox::stateChanged, this, [this](int state){
            if (static_cast<Qt::CheckState>(state) == Qt::CheckState::Checked)
#endif
        {
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
    // Bypass the queue
    emit sendPowerOn();
    // Need to allow time for the rig to power-on.
    // If we have no rigCaps then usual rig detection should occur.
    if (rigCaps != Q_NULLPTR) {
        calculateTimingParameters(); // Set queue interval
        QTimer::singleShot(5000, this, SLOT(initPeriodicCommands()));
        QTimer::singleShot(6500, this, SLOT(getInitialRigState()));
    }
}

void wfmain::powerRigOff()
{
    // Clear the queue to stop sending lots of data.
    queue->clear();
    emit sendPowerOff();
    queue->interval(-1); // Queue Disabled
}

void wfmain::on_ritTuneDial_valueChanged(int value)
{
    queue->add(priorityImmediate,queueItem(funcRitFreq,QVariant::fromValue(short(value))));
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
        case meterSubS:
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
        case meterdBu:
        case meterdBm:
        case meterdBuEMF:
            c = funcAbsoluteMeter;
            break;
        default:
            c = funcNone;
            break;
    }

    return c;
}

void wfmain::getMeterExtremities(meter_t m, double &lowVal, double &highVal, double &redLineVal) {
    lowVal = UINT16_MAX;
    highVal = (-1)*UINT16_MAX;
    redLineVal = 200.0;
    if (m==meterSubS)
    {
        m = meterS;
    }

    if(m==meterNone)
    {
        return;
    } else if (rigCaps != Q_NULLPTR){
        redLineVal = rigCaps->meterLines[m];
        for (auto it = rigCaps->meters[m].keyValueBegin(); it != rigCaps->meters[m].keyValueEnd(); ++it) {
            if (it->second < lowVal)
                lowVal = it->second;
            if (it->second > highVal || highVal == 255.0)
                highVal = it->second;
        }
        qDebug(logSystem()) << "Meter extremities:" << meterString[m] << "values, low:" << lowVal << ", high:" << highVal << ", red line:" << redLineVal;
        return;
    } else {
        // Hopefully we come back here after connection and do this correctly.
        qWarning(logSystem()) << "Cannot setup meter correctly without rigCaps.";
        return;
    }
}

void wfmain::changeMeterType(meter_t m, int meterNum)
{
    qDebug() << "Changing meter type.";
    meter_t newMeterType;
    meter_t oldMeterType;
    meter* uiMeter = NULL;
    if(meterNum == 1) {
        uiMeter = ui->meterSPoWidget;
    } else if(meterNum == 2) {
        uiMeter = ui->meter2Widget;
    } else if (meterNum == 3) {
        uiMeter = ui->meter3Widget;
    } else {
        qCritical() << "Error, invalid meter requested: meterNum ==" << meterNum;
        return;
    }
    newMeterType = m;
    oldMeterType = uiMeter->getMeterType();

    if(newMeterType == oldMeterType) {
        qDebug() << "Debug note: the old meter was the same as the new meter.";
    }

    funcs newCmd = meter_tToMeterCommand(newMeterType);
    funcs oldCmd = meter_tToMeterCommand(oldMeterType);

    //if (oldCmd != funcSMeter && oldCmd != funcNone)
    queue->del(oldCmd,(oldMeterType==meterSubS)?uchar(1):uchar(0));

    double lowVal = 0.0;
    double highVal = 2.0;
    double lineVal = 1.0;


    switch (newMeterType)
    {
    case meterdBu:
        lowVal = 0.0;
        highVal = 80.0;
        lineVal = 80.0;
        break;
    case meterdBuEMF:
        lowVal = 0.0;
        highVal = 85.0;
        lineVal = 85.0;
        break;
    case meterdBm:
        lowVal = -100.0;
        highVal = -20.0;
        lineVal = -20.0;
        break;
    default:
        if (rigCaps->meters[newMeterType].size())
            getMeterExtremities(newMeterType, lowVal, highVal, lineVal);
        break;
    }


    if(newMeterType==meterNone)
    {
        uiMeter->setMeterType(newMeterType);
        uiMeter->setMeterShortString("None");
    } else if (rigCaps != Q_NULLPTR){
        uiMeter->show();
        uiMeter->setMeterType(newMeterType);
        uiMeter->setMeterExtremities(lowVal, highVal, lineVal);
        uiMeter->setMeterShortString( meterString[(int)newMeterType] );

        if((newMeterType!=meterRxAudio) && (newMeterType!=meterTxMod) && (newMeterType!=meterAudio))
        {
            if (rigCaps->commands.contains(newCmd)) {
                queue->addUnique(priorityHighest,queueItem(newCmd,true,(newMeterType==meterSubS)?uchar(1):uchar(0)));
            }
        }

        if (meterNum == 1)
        {
            if (rigCaps->commands.contains(funcMeterType))
            {
                if (newMeterType == meterS)
                    queue->add(priorityImmediate,queueItem(funcMeterType,QVariant::fromValue<uchar>(0),false,(newMeterType==meterSubS)?uchar(1):uchar(0)));
                if (newMeterType == meterSubS)
                    queue->add(priorityImmediate,queueItem(funcMeterType,QVariant::fromValue<uchar>(0),false,(newMeterType==meterSubS)?uchar(1):uchar(0)));
                else if (newMeterType == meterdBu)
                    queue->add(priorityImmediate,queueItem(funcMeterType,QVariant::fromValue<uchar>(1),false,(newMeterType==meterSubS)?uchar(1):uchar(0)));
                else if (newMeterType == meterdBuEMF)
                    queue->add(priorityImmediate,queueItem(funcMeterType,QVariant::fromValue<uchar>(2),false,(newMeterType==meterSubS)?uchar(1):uchar(0)));
                else if (newMeterType == meterdBm)
                    queue->add(priorityImmediate,queueItem(funcMeterType,QVariant::fromValue<uchar>(3),false,(newMeterType==meterSubS)?uchar(1):uchar(0)));
            }
            uiMeter->enableCombo(rigCaps->commands.contains(funcMeterType));
        }
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
    debugWindow* debug = new debugWindow();
    debug->setAttribute(Qt::WA_DeleteOnClose);
    debug->show();
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
    ui->meter3Widget->setColors(cp->meterLevel, cp->meterPeakScale, cp->meterPeakLevel, cp->meterAverage, cp->meterLowerLine, cp->meterLowText);

    ui->scopeDualBtn->setStyleSheet(QString("QPushButton {background-color: %0;} QPushButton:checked {background-color: %1;border: 1px solid;}")
                                    .arg(cp->buttonOff.name(QColor::HexArgb),cp->buttonOn.name(QColor::HexArgb)));
    ui->dualWatchBtn->setStyleSheet(QString("QPushButton {background-color: %0;} QPushButton:checked {background-color: %1;border: 1px solid;}")
                                    .arg(cp->buttonOff.name(QColor::HexArgb),cp->buttonOn.name(QColor::HexArgb)));
    ui->splitBtn->setStyleSheet(QString("QPushButton {background-color: %0;} QPushButton:checked {background-color: %1;border: 1px solid;}")
                                    .arg(cp->buttonOff.name(QColor::HexArgb),cp->buttonOn.name(QColor::HexArgb)));
    //ui->mainSubTrackingBtn->setStyleSheet(QString("QPushButton {background-color: %0;} QPushButton:checked {background-color: %1;border: 1px solid;}")
    //                                .arg(cp->buttonOff.name(QColor::HexArgb),cp->buttonOn.name(QColor::HexArgb)));

    for (const auto& receiver: receivers) {
        receiver->colorPreset(cp);
    }
    if (this->colorPrefs != Q_NULLPTR)
        delete this->colorPrefs;
    this->colorPrefs = new colorPrefsType(*cp);
    qInfo() << "Setting color Preset" << cp->presetNum << "name" << *(cp->presetName);
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
    logStream.reset(new QTextStream(m_logFile.data()));
    // Set handler
    qInstallMessageHandler(messageHandler);

    connect(logWindow, SIGNAL(setDebugMode(bool)), this, SLOT(setDebugLogging(bool)));
    connect(logWindow, SIGNAL(setInsaneLoggingMode(bool)), this, SLOT(setInsaneDebugLogging(bool)));
    connect(logWindow, SIGNAL(setRigctlLoggingMode(bool)), this, SLOT(setRigctlDebugLogging(bool)));

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

void wfmain::handleLogText(QPair<QtMsgType,QString> logMessage)
{
    // This function is just a pass-through
    logWindow->acceptLogText(logMessage);
}

void wfmain::setDebugLogging(bool debugModeOn)
{
    this->debugMode = debugModeOn;
    debugModeLogging = debugModeOn;
}

void wfmain::setInsaneDebugLogging(bool insaneLoggingOn)
{
    insaneDebugLogging = insaneLoggingOn;
}

void wfmain::setRigctlDebugLogging(bool rigctlLoggingOn)
{
    rigctlDebugLogging = rigctlLoggingOn;
}

void wfmain::messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    // Open stream file writes
    // bool insaneDebugLogging = true;// global
    if (type == QtDebugMsg && !debugModeLogging)
    {
        return;
    }

    if( (type == QtWarningMsg) && (msg.contains("QPainter::")) ) {
        // This is a message from QCP about a collapsed plot area.
        // Ignore.
        return;
    }

    if( (type == QtDebugMsg) && (!insaneDebugLogging) && (qstrncmp(context.category, "rigTraffic", 10)==0) ) {
        return;
    }

    if( (type == QtDebugMsg) && (!rigctlDebugLogging) && (qstrncmp(context.category, "rigctld", 10)==0) ) {
        return;
    }

    QMutexLocker locker(&logMutex);
    QString text;

    // Write the date of recording
    *logStream << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ");
    text.append(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz "));
    // By type determine to what level belongs message

    switch (type)
    {
        case QtDebugMsg:
            *logStream << "DBG ";
            text.append("DBG ");
            break;
        case QtInfoMsg:
            *logStream << "INF ";
            text.append("INF ");
            break;
        case QtWarningMsg:
            *logStream << "WRN ";
            text.append("WRN ");
            break;
        case QtCriticalMsg:
            *logStream << "CRT ";
            text.append("CRT ");
            break;
        case QtFatalMsg:
            *logStream << "FTL ";
            text.append("FLT ");
            break;
    }
    // Write to the output category of the message and the message itself
    *logStream << context.category << ": " << msg << "\n";
    logStream->flush();    // Clear the buffered data
    text.append(context.category);
    text.append(": ");
    text.append(msg);
    logTextMutex.lock();
    logStringBuffer.push_front(QPair<QtMsgType,QString>(type,text));
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
    delayedCmdIntervalLAN_ms =  timing_ms;
    delayedCmdIntervalSerial_ms = timing_ms;
    (void)setUI;
}

void wfmain::connectionHandler(bool connect)
{
    queue->clear();
    emit sendCloseComm();

    rigName->setText("NONE");
    connStatus = connDisconnected;

    emit connectionStatus(connect); // Signal any other parts that need to know if we are connecting/connected.


    if (connect) {
        openRig();
    } else {
        ui->connectBtn->setText("Connect to Radio");
        enableRigCtl(false);
        removeRig();
    }

    // Whatever happened, make sure we delete the memories window.
    if (memWindow != Q_NULLPTR) {
        memWindow->close();
        delete memWindow;
        memWindow = Q_NULLPTR;
    }


}

void wfmain::connectionTimeout()
{
    qWarning(logSystem()) << "No response received to connection request";
    connectionHandler(false);
    connectionHandler(true);
}

void wfmain::on_cwButton_clicked()
{
    if (cw != Q_NULLPTR) {
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
}

void wfmain::on_memoriesBtn_clicked()
{
    if (rigCaps != Q_NULLPTR) {

        if (memWindow == Q_NULLPTR) {
            // Add slowload option for background loading.
            memWindow = new memories(isRadioAdmin, false);
            this->memWindow->connect(this, SIGNAL(haveMemory(memoryType)), memWindow, SLOT(receiveMemory(memoryType)));
            this->memWindow->connect(this, SIGNAL(haveMemoryName(memoryTagType)), memWindow, SLOT(receiveMemoryName(memoryTagType)));
            for(const auto& r: receivers) {
                connect(memWindow,SIGNAL(memoryMode(bool)),r,SLOT(memoryMode(bool)));
            }
            memWindow->setRegion(prefs.region);
            memWindow->populate(); // Call populate to get the initial memories
        }

        // Are you sure?
        if (prefs.confirmMemories) {
            memWindow->show();
        } else {
            QCheckBox *cb = new QCheckBox(tr("Don't ask me again"));
            cb->setToolTip(tr("Don't ask me to confirm memories again"));
            QMessageBox msgbox;
            msgbox.setText(tr("Memories are considered an experimental feature,\nPlease make sure you have a full backup of your radio before making changes.\nAre you sure you want to continue?\n"));
            msgbox.setIcon(QMessageBox::Icon::Question);
            QAbstractButton *yesButton = msgbox.addButton(QMessageBox::Yes);
            msgbox.addButton(QMessageBox::No);
            msgbox.setDefaultButton(QMessageBox::Yes);
            msgbox.setCheckBox(cb);
    #if (QT_VERSION >= QT_VERSION_CHECK(6,7,0))
            QObject::connect(cb, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state){
                if (state == Qt::CheckState::Checked)
    #else
            QObject::connect(cb, &QCheckBox::stateChanged, this, [this](int state){
                if (static_cast<Qt::CheckState>(state) == Qt::CheckState::Checked)
    #endif
                {
                    prefs.confirmMemories=true;
                } else {
                    prefs.confirmMemories=false;
                }
                settings->beginGroup("Interface");
                settings->setValue("ConfirmMemories", this->prefs.confirmMemories);
                settings->endGroup();
                settings->sync();
            });

            msgbox.exec();
            delete cb;

            if (msgbox.clickedButton() == yesButton) {
                memWindow->show();
            }
        }
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

    uchar vfo=0;

    // Sometimes we can receive data before the rigCaps have been determined, so return if this happens:

    if (rigCaps == Q_NULLPTR)
    {
        qWarning(logSystem()) << "Data received before we have rigCaps(), aborting";
        return;
    }
    else if (val.receiver >= receivers.size())
    {
        qWarning(logSystem()) << "Data received for Radio/VFO that doesn't exist!" << val.receiver;
        return;
    }

    /* Certain radios (IC-9700) cannot provide direct access to Sub receiver
     * In this situation, set the receiver to currentReceiver so most commands received
     * work on this receiver only.
     *
     * This functionality has now been moved to the xxxxCommander class.
     */

    /*
    if (!rigCaps->hasCommand29 && val.receiver != currentReceiver)
    {
        switch (val.command) {
        case funcSelectedFreq: case funcSelectedMode: case funcUnselectedFreq: case funcUnselectedMode: case funcScopeMode: case funcScopeSpan:
        case funcScopeRef: case funcScopeHold: case funcScopeSpeed: case funcScopeRBW: case funcScopeVBW: case funcScopeCenterType: case funcScopeEdge:
           break;
        default:
            val.receiver=currentReceiver;
            break;
        }
    }
    */

    switch (val.command)
    {
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
    case funcUnselectedFreq:
        vfo = 1;
    case funcFreqGet:
    case funcFreqTR:
    case funcFreq:
    case funcSelectedFreq:
        receivers[val.receiver]->setFrequency(val.value.value<freqt>(),vfo);
        if (val.receiver==0 || vfo == 0)
            rpt->handleUpdateCurrentMainFrequency(val.value.value<freqt>());
        break;
    case funcModeGet:
    case funcModeTR:
        // These commands don't include filter, so queue an immediate request for filter
        queue->addUnique(priorityImmediate,funcDataModeWithFilter,false,0);
    case funcDataModeWithFilter:
    case funcUnselectedMode:
        if (val.command == funcUnselectedMode)
            vfo=1;
    case funcDataMode:
    case funcSelectedMode:
    case funcMode:
    {
        modeInfo m = val.value.value<modeInfo>();
        receivers[val.receiver]->receiveMode(m,vfo);
        // We are ONLY interested in VFOA
        if (val.receiver == currentReceiver && vfo == 0) {
            finputbtns->updateCurrentMode(m.mk);
            finputbtns->updateFilterSelection(m.filter);
            rpt->handleUpdateCurrentMainMode(m);
            if (cw != Q_NULLPTR) {
                cw->handleCurrentModeUpdate(m.mk);
            }
        }
        //qDebug() << funcString[val.command] << "receiver:" << val.receiver << "vfo:" << vfo << "mk:" << m.mk << "name:" << m.name << "data:" << m.data << "filter:" << m.filter;

        break;
    }
    case funcTXFreq:
        // Not sure if we want to do anything with this? M0VSE
        break;
    case funcVFODualWatch:        
        if (receivers.size()>1) // How can it not be?
        {
            ui->dualWatchBtn->blockSignals(true);
            ui->dualWatchBtn->setChecked(val.value.value<bool>());
            ui->dualWatchBtn->blockSignals(false);
        }
        break;
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif
    case funcVFOBandMS:
    {
        // This indicates whether main or sub is currently "active"
        // Swap recievers if necessary.                
        uchar r = val.value.value<uchar>();
        if (currentReceiver != r)
        {
            // VFO has swapped, make sure scope follows if we only have a single scope.
            // Th radio doesn't do this, but I don't see why we would want it without a scope?
            if (!rigCaps->hasCommand29 && rigCaps->commands.contains(funcScopeMainSub))
                queue->add(priorityImmediate,queueItem(funcScopeMainSub,val.value,false,0));

            for (const auto& rx: receivers)
            {
                rx->selected(rx->getReceiver() == r);
                rx->setVisible(rx->isSelected() || ui->scopeDualBtn->isChecked());
                rx->updateInfo();
            }
            currentReceiver = r;
        }
        break;
    }
    case funcSatelliteMode:
        // If satellite mode is enabled, disable mode/freq query commands.
        for (auto r: receivers){
            if (val.value.value<bool>()) {
                queue->del(funcUnselectedMode,r->getReceiver());
                queue->del(funcUnselectedFreq,r->getReceiver());
            }
            r->setSatMode(val.value.value<bool>());
        }
        //qInfo(logRig()) << "Is radio currently in satellite mode?" << val.value.value<bool>();
        break;
    case funcSatelliteMemory:
    case funcMemoryContents:
        emit haveMemory(val.value.value<memoryType>());
        break;
    case funcMemoryTag:
        emit haveMemoryName(val.value.value<memoryTagType>());
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
        ui->splitBtn->setChecked(val.value.value<duplexMode_t>()==dmSplitOn?true:false);
        rpt->receiveDuplexMode(val.value.value<duplexMode_t>());
        receivers[val.receiver]->setSplit(val.value.value<duplexMode_t>()==dmSplitOn?true:false);
        break;
    case funcQuickSplit:
        rpt->receiveQuickSplit(val.value.value<bool>());
        break;
    case funcTuningStep:
        receiveTuningStep(val.value.value<uchar>());
        break;
    case funcAttenuator:
        receiveAttenuator(val.value.value<uchar>(),val.receiver);
        break;
    case funcAntenna:
        if (val.receiver == currentReceiver) {
            ui->antennaSelCombo->setCurrentIndex(ui->antennaSelCombo->findData(val.value.value<antennaInfo>().antenna));
            ui->rxAntennaCheck->setChecked(val.value.value<antennaInfo>().rx);
        }
        break;
    case funcPBTOuter:

        receivers[val.receiver]->setPBTOuter(val.value.value<uchar>());
        break;
    case funcPBTInner:
    {
        receivers[val.receiver]->setPBTInner(val.value.value<uchar>());
        break;
    }
    case funcIFShift:
        receivers[val.receiver]->setIFShift(val.value.value<uchar>());
        break;
        /*
    connect(this->rig, &rigCommander::haveDashRatio,
        [=](const quint8& ratio) { cw->handleDashRatio(ratio); });
    connect(this->rig, &rigCommander::haveCWBreakMode,
            [=](const quint8 &bm) { cw->handleBreakInMode(bm);});
*/
    case funcCwPitch:
        // There is only a single CW Pitch setting, so send to all scopes
        for (const auto& receiver: receivers) {
            receiver->receiveCwPitch(val.value.value<quint16>());
        }
        // Also send to CW window
        if (cw != Q_NULLPTR) {
            cw->handlePitch(val.value.value<quint16>());
        }
        break;

    case funcMicGain:
        processModLevel(inputMic,val.value.value<uchar>());
        break;
    case funcKeySpeed:
        // Only used by CW window
        if (cw != Q_NULLPTR) {
            cw->handleKeySpeed(val.value.value<uchar>());
        }
        break;
    case funcNotchFilter:
        break;
    case funcAfGain:
        if (val.receiver == currentReceiver)
            changeSliderQuietly(ui->afGainSlider, val.value.value<uchar>());
        break;
    case funcMonitorGain:
        changeSliderQuietly(ui->monitorSlider, val.value.value<uchar>());
        break;
    case funcRfGain:
        if (val.receiver == currentReceiver)
            changeSliderQuietly(ui->rfGainSlider, val.value.value<uchar>());
        break;
    case funcSquelch:
        if (val.receiver == currentReceiver) {
            changeSliderQuietly(ui->sqlSlider, val.value.value<uchar>());
        }
        break;
    case funcRFPower:
        changeSliderQuietly(ui->txPowerSlider, val.value.value<uchar>());
        break;
    case funcCompressorLevel:
    case funcNBLevel:
    case funcNRLevel:
    case funcAPFLevel:
    case funcDriveGain:
    case funcVoxGain:
    case funcAntiVoxGain:
        break;
    case funcBreakInDelay:
        break;
    case funcDigiSelShift:
        break;
    // 0x15 Meters
    case funcSMeterSqlStatus:
        break;
    case funcSMeter:
        if (val.receiver ) {
            receiveMeter(meter_t::meterSubS,val.value.value<double>());
        }
        else {
            receiveMeter(meter_t::meterS,val.value.value<double>());
        }
        break;
    case funcAbsoluteMeter:
    {
        meterkind m = val.value.value<meterkind>();
        if (ui->meterSPoWidget->getMeterType() != m.type) {

            changeMeterType(m.type,1);
        }
        ui->meterSPoWidget->setLevel(m.value);
        break;
    }
    case funcMeterType:
    {
        meter_t m = val.value.value<meter_t>();
        if (ui->meterSPoWidget->getMeterType() != m)
            changeMeterType(m, 1);
        break;

    }
    case funcVariousSql:
        break;
    case funcOverflowStatus:
        receivers[val.receiver]->overflow(val.value.value<bool>());
        break;
    case funcCenterMeter:
        receiveMeter(meter_t::meterCenter,val.value.value<double>());
        break;
    case funcPowerMeter:
        receiveMeter(meter_t::meterPower,val.value.value<double>());
        break;
    case funcSWRMeter:
        receiveMeter(meter_t::meterSWR,val.value.value<double>());
        break;
    case funcALCMeter:
        receiveMeter(meter_t::meterALC,val.value.value<double>());
        break;
    case funcCompMeter:
        receiveMeter(meter_t::meterComp,val.value.value<double>());
        break;
    case funcVdMeter:
        receiveMeter(meter_t::meterVoltage,val.value.value<double>());
        break;
    case funcIdMeter:
        receiveMeter(meter_t::meterCurrent,val.value.value<double>());
        break;
    // 0x16 enable/disable functions:
    case funcPreamp:
        receivePreamp(val.value.value<uchar>(),val.receiver);
        break;
    case funcAGC:
    case funcAGCTimeConstant:
        break;
    case funcNoiseBlanker:
        if (val.receiver == currentReceiver) {
            ui->nbEnableChk->blockSignals(true);
            if (ui->nbEnableChk->isTristate())
                ui->nbEnableChk->setCheckState(Qt::CheckState(val.value.value<uchar>()));
            else
                ui->nbEnableChk->setChecked(val.value.value<bool>());

            ui->nbEnableChk->blockSignals(false);
        }
        break;
    case funcAudioPeakFilter:
        break;
    case funcFilterShape:
        if (rigCaps->manufacturer == manufIcom || receivers[val.receiver]->currentFilter() == val.value.value<uchar>() / 10)
        {
            receivers[val.receiver]->setFilterShape(val.value.value<uchar>() % 10);
        }
        break;
    case funcRoofingFilter:
        if (rigCaps->manufacturer == manufIcom || receivers[val.receiver]->currentFilter() == val.value.value<uchar>() / 10)
        {
            receivers[val.receiver]->setRoofing(val.value.value<uchar>() % 10);
        }
        break;
    case funcNoiseReduction:
        if (val.receiver == currentReceiver) {
            ui->nrEnableChk->blockSignals(true);
            if (ui->nrEnableChk->isTristate())
                ui->nrEnableChk->setCheckState(Qt::CheckState(val.value.value<uchar>()));
            else
                ui->nrEnableChk->setChecked(val.value.value<bool>());

            ui->nrEnableChk->blockSignals(false);
        }
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
        if (val.receiver == currentReceiver) {
            ui->compEnableChk->setChecked(val.value.value<bool>());
        }
        break;
    case funcMonitor:
        receiveMonitor(val.value.value<bool>());
        break;
    case funcVox:
        ui->voxEnableChk->setChecked(val.value.value<bool>());
        break;
    case funcManualNotch:
        break;
    case funcDigiSel:
        ui->digiselEnableChk->setChecked(val.value.value<bool>());
        ui->preampSelCombo->setEnabled(!val.value.value<bool>());
        break;
    case funcTwinPeakFilter:
        break;
    case funcDialLock:
        break;
    case funcRXAntenna:
        ui->rxAntennaCheck->setChecked(val.value.value<bool>());
        break;
    case funcManualNotchWidth:
        break;
    case funcSSBTXBandwidth:
        break;
    case funcMainSubTracking:

        //ui->mainSubTrackingBtn->setChecked(val.value.value<bool>());
        //for (const auto& receiver:receivers)
        //{
        //    receiver->setTracking(val.value.value<bool>());
        //}
        break;
    case funcToneSquelchType:
        break;
    case funcIPPlus:
        if (val.receiver == currentReceiver) {
            ui->ipPlusEnableChk->setChecked(val.value.value<bool>());
        }
        break;
    case funcBreakIn:
        if (cw != Q_NULLPTR) {
            cw->handleBreakInMode(val.value.value<uchar>());
        }
        break;
    // 0x17 is CW send and 0x18 is power control (no reply)
    // 0x19 it automatically added.
    case funcTransceiverId:
        break;
    // 0x1a
    case funcBandStackReg:
    {
        bandStackType bsr = val.value.value<bandStackType>();
        qDebug(logRig()) << __func__ << "BSR received into" << val.receiver << "Freq:" << bsr.freq.Hz << ", mode: " << bsr.mode << ", filter: " << bsr.filter << ", data mode: " << bsr.data;

        queue->add(priorityImmediate,queueItem(queue->getVfoCommand(vfoA,val.receiver,true).freqFunc,
                                                QVariant::fromValue<freqt>(bsr.freq),false,uchar(val.receiver)));

        for (auto &md: rigCaps->modes)
        {
            if (md.reg == bsr.mode) {
                modeInfo m(md);
                m.filter=bsr.filter;
                m.data=bsr.data;
                qDebug(logRig()) << __func__ << "Setting Mode/Data for new mode" << m.name << "data" << m.data << "filter" << m.filter << "reg" << m.reg;
                queue->add(priorityImmediate,queueItem(queue->getVfoCommand(vfoA,val.receiver,true).modeFunc,
                                                        QVariant::fromValue<modeInfo>(m),false,uchar(val.receiver)));
                break;
            }
        }
        break;
    }
    case funcFilterWidth:
        receivers[val.receiver]->receivePassband(val.value.value<ushort>());
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
        if (cw != Q_NULLPTR) {
            cw->handleDashRatio(val.value.value<uchar>());
        }
        break;
    case funcTXFreqMon:
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
    case funcRitFreq:
        ui->ritTuneDial->blockSignals(true);
        ui->ritTuneDial->setValue(val.value.value<short>());
        ui->ritTuneDial->blockSignals(false);
        break;
    case funcRitTXStatus:
        // Not sure what this is used for?
        break;
    // 0x27
    case funcScopeWaveData:
        receivers[val.receiver]->updateScope(val.value.value<scopeData>());
        break;
    case funcScopeOnOff:
        // confirming scope is on
        break;
    case funcScopeDataOutput:
        // confirming output enabled/disabled of wf data.
        break;
    case funcScopeMainSub:
    {
        // Has the primary scope changed?
        break;
    }
    case funcScopeSingleDual:
    {
        if (receivers.size()>1)
        {
            if (QThread::currentThread() != QCoreApplication::instance()->thread())
            {
                qCritical(logSystem()) << "Thread is NOT the main UI thread, cannot hide/unhide VFO";
            } else {
                // This tells us whether we are receiving single or dual scopes
                ui->scopeDualBtn->blockSignals(true);
                ui->scopeDualBtn->setChecked(val.value.value<bool>());
                ui->scopeDualBtn->blockSignals(false);
                for (const auto& rx: receivers) {
                    if (!rx->isSelected()) {
                        rx->setVisible(val.value.value<bool>());
                    }
                }
            }
        }
        break;
    }
    case funcScopeMode:
        //qDebug() << "Got new scope mode for receiver" << val.receiver << "mode" << val.value.value<uchar>();
        receivers[val.receiver]->setScopeMode(val.value.value<uchar>());
        break;
    case funcScopeSpan:
        receivers[val.receiver]->setSpan(val.value.value<centerSpanData>());
        break;
    case funcScopeEdge:
        receivers[val.receiver]->setEdge(val.value.value<uchar>());
        break;
    case funcScopeHold:
        receivers[val.receiver]->setHold(val.value.value<bool>());
        break;
    case funcScopeRef:
        receivers[val.receiver]->setRef(val.value.value<int>());
        break;
    case funcScopeSpeed:
        receivers[val.receiver]->setSpeed(val.value.value<uchar>());
        break;
    case funcScopeDuringTX:
    case funcScopeCenterType:
    case funcScopeVBW:
    case funcScopeFixedEdgeFreq:
    case funcScopeRBW:
        break;
    // 0x28
    case funcVoiceTX:
        break;
    //0x29 - Prefix certain commands with this to get/set certain values without changing current VFO
    // If we use it for get commands, need to parse the \x29\x<VFO> first.
    case funcMainSubPrefix:
        break;
    case funcPowerControl:
        // We could indicate the rig being powered-on somehow?
        break;
    case funcCWDecode:
        cw->receive(val.value.value<QString>());
    default:
        //qWarning(logSystem()) << "Unhandled command received from rigcommander()" << funcString[val.command] << "Contact support!";
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
    if (rigCaps->commands.contains(funcVFOBandMS)) {
        queue->add(priorityImmediate,queueItem(funcVFOBandMS,QVariant::fromValue<uchar>(currentReceiver==0),false,0));
    }

 }

void wfmain::on_scopeDualBtn_toggled(bool en)
{
    if (rigCaps->commands.contains(funcScopeSingleDual))
    {
        queue->add(priorityImmediate,queueItem(funcScopeSingleDual,QVariant::fromValue(en),false,0));
        queue->add(priorityImmediate,funcScopeMainSub,false,0);
    } else {
        // As the IC9700 doesn't have a single/dual command, pretend it does!
        for (const auto& rx: receivers) {
            if (rx->getReceiver() != currentReceiver) {
                rx->selected(false);
                rx->setVisible(en);
            }
        }
    }
}

void wfmain::on_dualWatchBtn_toggled(bool en)
{
    queue->add(priorityImmediate,queueItem(funcVFODualWatch,QVariant::fromValue(en),false,0));
    if (!rigCaps->hasCommand29)
        queue->add(priorityImmediate,funcScopeMainSub,false,0);

}

void wfmain::on_swapMainSubBtn_clicked()
{
    queue->add(priorityImmediate,funcVFOSwapMS);
    for (const auto &receiver: receivers)
    {        
        queue->add(priorityImmediate,queue->getVfoCommand(vfoA,receiver->getReceiver(),true).freqFunc,false,receiver->getReceiver());
        queue->add(priorityImmediate,queue->getVfoCommand(vfoA,receiver->getReceiver(),true).modeFunc,false,receiver->getReceiver());
        queue->add(priorityImmediate,queue->getVfoCommand(vfoB,receiver->getReceiver(),true).freqFunc,false,receiver->getReceiver());
        queue->add(priorityImmediate,queue->getVfoCommand(vfoB,receiver->getReceiver(),true).modeFunc,false,receiver->getReceiver());
    }
}

/*
void wfmain::on_mainSubTrackingBtn_toggled(bool en)
{
    queue->add(priorityImmediate,queueItem(funcMainSubTracking,QVariant::fromValue(en),false));
}
*/

void wfmain::on_splitBtn_toggled(bool en)
{
    queue->add(priorityImmediate,queueItem(funcSplitStatus,QVariant::fromValue(en),false));
}

void wfmain::on_mainEqualsSubBtn_clicked()
{
    queue->add(priorityImmediate,funcVFOEqualMS);
    for (const auto &receiver: receivers)
    {
        queue->add(priorityImmediate,queue->getVfoCommand(vfoA,receiver->getReceiver(),true).modeFunc,false,receiver->getReceiver());
        queue->add(priorityImmediate,queue->getVfoCommand(vfoA,receiver->getReceiver(),true).freqFunc,false,receiver->getReceiver());
        queue->add(priorityImmediate,queue->getVfoCommand(vfoB,receiver->getReceiver(),true).modeFunc,false,receiver->getReceiver());
        queue->add(priorityImmediate,queue->getVfoCommand(vfoB,receiver->getReceiver(),true).freqFunc,false,receiver->getReceiver());
    }
}

void wfmain::dataModeChanged(modeInfo m)
{
    // As the data mode may have changed, we need to make sure that we invalidate the input selection.
    // Also request the current input from the rig.
    //qInfo(logSystem()) << "*** DATA MODE HAS CHANGED ***";
    if (m.data != 0xff) {
        currentModSrc[m.data] = rigInput();

        // Request the current inputSource.
        switch(m.data)
        {
        case 0:
            queue->add(priorityImmediate,funcDATAOffMod,false,false);
            break;
        case 1:
            queue->add(priorityImmediate,funcDATA1Mod,false,false);
            break;
        case 2:
            queue->add(priorityImmediate,funcDATA2Mod,false,false);
            break;
        case 3:
            queue->add(priorityImmediate,funcDATA3Mod,false,false);
            break;
        }
    }

}

void wfmain::receiveScopeSettings(uchar receiver, int theme, quint16 len, int floor, int ceiling)
{
    if (receiver) {
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

void wfmain::receiveRigCaps(rigCapabilities* caps)
{
    this->rigCaps = caps;

    // Convenient place to stop the connection timer as rigCaps has changed.
    ConnectionTimer.stop();

    //bandbtns->acceptRigCaps(rigCaps);

    if(caps == Q_NULLPTR)
    {
        // Note: This line makes it difficult to accept a different radio connecting.
        return;
    } else {

        // Enable all controls
        enableControls(true);

        if (prefs.manufacturer == manufIcom)
            showStatusBarText(QString("Found radio of name %0 and model ID %1.").arg(rigCaps->modelName).arg(rigCaps->modelID,2,16,QChar('0')));
        else
            showStatusBarText(QString("Found radio of name %0 and model ID %1.").arg(rigCaps->modelName).arg(rigCaps->modelID));

        qDebug(logSystem()) << "Rig name: " << rigCaps->modelName;
        qDebug(logSystem()) << "Has LAN capabilities: " << rigCaps->hasLan;
        qDebug(logSystem()) << "Rig ID received into wfmain: spectLenMax: " << rigCaps->spectLenMax;
        qDebug(logSystem()) << "Rig ID received into wfmain: spectAmpMax: " << rigCaps->spectAmpMax;
        qDebug(logSystem()) << "Rig ID received into wfmain: spectSeqMax: " << rigCaps->spectSeqMax;
        qDebug(logSystem()) << "Rig ID received into wfmain: hasSpectrum: " << rigCaps->hasSpectrum;

        configureVFOs(); // Now we have a rig connection, need to configure the VFOs

        rigName->setText(rigCaps->modelName);
        if (serverConfig.enabled) {
            serverConfig.rigs.first()->modelName = rigCaps->modelName;
            serverConfig.rigs.first()->rigName = rigCaps->modelName;
            serverConfig.rigs.first()->civAddr = rigCaps->modelID;
            serverConfig.rigs.first()->baudRate = rigCaps->baudRate;
        }
        setWindowTitle(rigCaps->modelName);

        if(rigCaps->hasSpectrum)
        {
            for (const auto& receiver: receivers)
            {
                receiver->prepareScope(rigCaps->spectAmpMax, rigCaps->spectLenMax);
            }
        }

        ui->transmitBtn->setEnabled(rigCaps->hasTransmit);
        ui->micGainSlider->setEnabled(rigCaps->hasTransmit);
        ui->txPowerSlider->setEnabled(rigCaps->hasTransmit);

        if (rigCaps->commands.contains(funcSendCW)) {
            // We have a send CW function, so enable the window.
            cw = new cwSender(this,rig);
            cw->setCutNumbers(prefs.cwCutNumbers);
            cw->setSendImmediate(prefs.cwSendImmediate);
            cw->setSidetoneEnable(prefs.cwSidetoneEnabled);
            cw->setSidetoneLevel(prefs.cwSidetoneLevel);
            cw->setMacroText(prefs.cwMacroList);
        }

        ui->cwButton->setEnabled(rigCaps->commands.contains(funcSendCW));
        ui->memoriesBtn->setEnabled(rigCaps->commands.contains(funcMemoryContents));
        ui->monitorSlider->setEnabled(rigCaps->commands.contains(funcMonitorGain));
        ui->rfGainSlider->setEnabled(rigCaps->commands.contains(funcRfGain));

        // Only show settingsgroup if rig has sub
        ui->scopeSettingsGroup->setVisible(rigCaps->commands.contains(funcVFODualWatch));

        ui->scopeDualBtn->setVisible(rigCaps->commands.contains(funcVFODualWatch));
        ui->mainEqualsSubBtn->setVisible(rigCaps->commands.contains(funcVFOEqualMS));
        ui->swapMainSubBtn->setVisible(rigCaps->commands.contains(funcVFOSwapMS));
        //ui->mainSubTrackingBtn->setVisible(rigCaps->commands.contains(funcMainSubTracking));
        // Only show this split button on IC7610/IC785x
        ui->splitBtn->setVisible(rigCaps->commands.contains(funcSplitStatus) && rigCaps->commands.contains(funcVFOEqualMS));
        ui->antennaGroup->setVisible(rigCaps->commands.contains(funcAntenna));
        ui->preampAttGroup->setVisible(rigCaps->commands.contains(funcPreamp));
        //ui->dualWatchBtn->setVisible(rigCaps->hasCommand29);

        ui->nbEnableChk->setTristate(false);
        ui->nrEnableChk->setTristate(false);
        ui->nbEnableChk->setChecked(false);
        ui->nrEnableChk->setChecked(false);

        if (rigCaps->commands.contains(funcNoiseBlanker)) {
            qInfo () << "nb max val" << rigCaps->commands.find(funcNoiseBlanker)->maxVal;
            if (rigCaps->commands.find(funcNoiseBlanker)->maxVal > 1)
                ui->nbEnableChk->setTristate(true);
        }
        ui->nbEnableChk->setEnabled(rigCaps->commands.contains(funcNoiseBlanker));

        if (rigCaps->commands.contains(funcNoiseReduction)) {
            qInfo () << "nr max val" << rigCaps->commands.find(funcNoiseReduction)->maxVal;
            if (rigCaps->commands.find(funcNoiseReduction)->maxVal > 1)
                ui->nrEnableChk->setTristate(true);
        }

        if (rigCaps->commands.contains(funcRFPower)) {
            auto f = rigCaps->commands.find(funcRFPower);
            ui->txPowerSlider->setRange(f->minVal,f->maxVal);
        }

        if (rigCaps->commands.contains(funcMonitorGain)) {
            auto f = rigCaps->commands.find(funcMonitorGain);
            ui->monitorSlider->setRange(f->minVal,f->maxVal);
        }

        if (rigCaps->commands.contains(funcSquelch)) {
            auto f = rigCaps->commands.find(funcSquelch);
            ui->sqlSlider->setRange(f->minVal,f->maxVal);
        }

        ui->nrEnableChk->setEnabled(rigCaps->commands.contains(funcNoiseReduction));

        ui->ipPlusEnableChk->setEnabled(rigCaps->commands.contains(funcIPPlus));
        ui->compEnableChk->setEnabled(rigCaps->commands.contains(funcCompressor));
        ui->voxEnableChk->setEnabled(rigCaps->commands.contains(funcVox));
        ui->digiselEnableChk->setEnabled(rigCaps->commands.contains(funcDigiSel));

        for (const auto& receiver: receivers) {
            if (receiver->getReceiver() == 0) {
                // Report scope redraw time to Select Radio window (only scope 0)
                connect(receiver,SIGNAL(spectrumTime(double)),selRad,SLOT(spectrumTime(double)));
                connect(receiver,SIGNAL(waterfallTime(double)),selRad,SLOT(waterfallTime(double)));
                connect(receiver,SIGNAL(spectrumTime(double)),rig,SLOT(spectrumTime(double)));
                connect(receiver,SIGNAL(waterfallTime(double)),rig,SLOT(waterfallTime(double)));
            }
            // Setup various combo box up for each VFO:
            receiver->clearMode();
            for (auto &m: rigCaps->modes)
            {
                receiver->addMode(m.name+" Mode",QVariant::fromValue(m));
            }

            receiver->clearFilter();
            for (auto& f: rigCaps->filters)
            {
                receiver->addFilter(f.name,f.num);
            }

            receiver->clearRoofing();
            for (auto& f: rigCaps->roofing)
            {
                receiver->addRoofing(f.name,f.num);
            }

            receiver->clearData();

            receiver->addData("Data Off",0);

            if (rigCaps->commands.contains(funcDATA1Mod))
            {
                setupui->updateModSourceList(1, rigCaps->inputs);
                if (!rigCaps->commands.contains(funcDATA2Mod))
                {
                    receiver->addData("Data On", 2);
                }
            }

            if (rigCaps->commands.contains(funcDATA2Mod))
            {
                setupui->updateModSourceList(2, rigCaps->inputs);
                receiver->addData("Data 1", 2);
                receiver->addData("Data 2", 2);
            }

            if (rigCaps->commands.contains(funcDATA3Mod))
            {
                setupui->updateModSourceList(3, rigCaps->inputs);
                receiver->addData("Data 3", 3);
            }
            setupui->enableModSource(0,rigCaps->commands.contains(funcDATAOffMod));
            setupui->enableModSource(1,rigCaps->commands.contains(funcDATA1Mod));
            setupui->enableModSource(2,rigCaps->commands.contains(funcDATA2Mod));
            setupui->enableModSource(3,rigCaps->commands.contains(funcDATA3Mod));

            // Disable unsupported mod sources
            for (const auto &r: rigCaps->inputs)
            {
                setupui->enableModSourceItem(0,r,rigCaps->commands.contains(funcDATAOffMod));
                setupui->enableModSourceItem(1,r,(rigCaps->commands.contains(funcDATA1Mod) && r.reg > -1) ? true : false);
                setupui->enableModSourceItem(2,r,(rigCaps->commands.contains(funcDATA2Mod) && r.reg > -1) ? true : false);
                setupui->enableModSourceItem(3,r,(rigCaps->commands.contains(funcDATA3Mod) && r.reg > -1) ? true : false);
            }

            receiver->clearSpans();
            if(rigCaps->hasSpectrum)
            {
                for(unsigned int i=0; i < rigCaps->scopeCenterSpans.size(); i++)
                {
                    receiver->addSpan(rigCaps->scopeCenterSpans.at(i).name, QVariant::fromValue(rigCaps->scopeCenterSpans.at(i)));
                }
                receiver->setRange(prefs.mainPlotFloor, prefs.mainPlotCeiling);
            }
        }

        // Set the tuning step combo box up:
        ui->tuningStepCombo->blockSignals(true);
        ui->tuningStepCombo->clear();
        for (auto &s: rigCaps->steps)
        {
            ui->tuningStepCombo->addItem(s.name, s.hz);
        }

        ui->tuningStepCombo->setCurrentIndex(2);
        ui->tuningStepCombo->blockSignals(false);

        setupui->updateModSourceList(0, rigCaps->inputs);

        ui->attSelCombo->clear();
        if(rigCaps->commands.contains(funcAttenuator))
        {
            ui->attSelCombo->setDisabled(false);
            for (auto &att: rigCaps->attenuators)
            {
                ui->attSelCombo->addItem(att.name,att.num);
            }
        } else {
            ui->attSelCombo->setDisabled(true);
        }

        ui->preampSelCombo->clear();
        if(rigCaps->commands.contains(funcPreamp))
        {
            ui->preampSelCombo->setDisabled(false);
            for (auto &pre: rigCaps->preamps)
            {
                ui->preampSelCombo->addItem(pre.name, pre.num);
            }
        } else {
            ui->preampSelCombo->setDisabled(true);
        }

        ui->antennaSelCombo->clear();
        if(rigCaps->commands.contains(funcAntenna))
        {
            ui->antennaSelCombo->setDisabled(false);
            for (auto &ant: rigCaps->antennas)
            {
                ui->antennaSelCombo->addItem(ant.name,ant.num);
            }
        } else {
            ui->antennaSelCombo->setDisabled(true);
        }

        ui->rxAntennaCheck->setEnabled(rigCaps->commands.contains(funcRXAntenna));
        ui->rxAntennaCheck->setChecked(false);

        ui->tuneEnableChk->setEnabled(rigCaps->commands.contains(funcTunerStatus));
        ui->tuneNowBtn->setEnabled(rigCaps->commands.contains(funcTunerStatus));

        ui->memoriesBtn->setEnabled(rigCaps->commands.contains(funcMemoryContents));

        ui->connectBtn->setText("Disconnect from Radio"); // We must be connected now.
        connStatus = connConnected;

        // Now we know that we are connected, enable rigctld
        enableRigCtl(prefs.enableRigCtlD);

        if(prefs.enableLAN)
        {
            ui->afGainSlider->setValue(prefs.localAFgain);
            queue->receiveValue(funcAfGain,quint8(prefs.localAFgain),currentReceiver);
        } else {
            // If not network connected, select the requested PTT type.
            emit setPTTType(prefs.pttType);
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

    }

    initPeriodicCommands();
    getInitialRigState();
}


void wfmain::radioInUse(quint8 radio, bool admin, quint8 busy, QString user, QString ip)
{
   Q_UNUSED(busy)
   Q_UNUSED(radio)
   Q_UNUSED(user)
   Q_UNUSED(ip)
   qDebug(logSystem()) << "Is this user an admin? " << ((admin)?"yes":"no");
   isRadioAdmin = admin;
}

void wfmain::receiveScopeImage(uchar receiver)
{

    #if defined (USB_CONTROLLER)
            // Send to USB Controllers if requested
        if (receiver == 0) {
            for (auto it = usbDevices.begin(); it != usbDevices.end(); it++)
            {
                auto dev = &it.value();

                if (dev->connected && dev->type.model == usbDeviceType::StreamDeckPlus && dev->lcd == funcLCDWaterfall )
                {
                    lcdImage = receivers[receiver]->getWaterfallImage();
                    emit sendControllerRequest(dev, usbFeatureType::featureLCD, 0, "", &lcdImage);
                }
                else if (dev->connected && dev->type.model == usbDeviceType::StreamDeckPlus && dev->lcd == funcLCDSpectrum)
                {
                    lcdImage = receivers[receiver]->getSpectrumImage();
                    emit sendControllerRequest(dev, usbFeatureType::featureLCD, 0, "", &lcdImage);
                }
            }
        }
    #endif

}

// Assorted checkboxes

// lambda slots, these cuts-down on number of dedicated slot functions
void wfmain::setupLambdaSlots()
{

// As checkState is deprecated, use checkStateChanged if available
#if (QT_VERSION < QT_VERSION_CHECK(6,7,0))
#define CHKFUNC &QCheckBox::stateChanged, this, [=](int checked)
#else
#define CHKFUNC &QCheckBox::checkStateChanged, this, [=](Qt::CheckState checked)
#endif

    connect(ui->nrEnableChk, CHKFUNC {
        queue->addUnique(priorityImmediate,queueItem(funcNoiseReduction,
                QVariant::fromValue<uchar>(ui->nrEnableChk->isTristate()?checked:bool(checked)),false,currentReceiver));
    });

    connect(ui->nbEnableChk, CHKFUNC {
        queue->addUnique(priorityImmediate,queueItem(funcNoiseBlanker,
                QVariant::fromValue<uchar>(ui->nbEnableChk->isTristate()?checked:bool(checked)),false,currentReceiver));
    });

    connect(ui->ipPlusEnableChk, CHKFUNC {
        queue->addUnique(priorityImmediate,queueItem(funcIPPlus,QVariant::fromValue<bool>(checked),false,currentReceiver));
    });

    connect(ui->compEnableChk, CHKFUNC {
        queue->addUnique(priorityImmediate,queueItem(funcCompressor,QVariant::fromValue<bool>(checked),false,currentReceiver));
    });

    connect(ui->voxEnableChk, CHKFUNC {
        queue->addUnique(priorityImmediate,queueItem(funcVox,QVariant::fromValue<bool>(checked),false,currentReceiver));
    });

    connect(ui->digiselEnableChk, CHKFUNC {
        queue->addUnique(priorityImmediate,queueItem(funcDigiSel,QVariant::fromValue<bool>(checked),false,currentReceiver));
    });

    // Slider tooltip
    connect(ui->txPowerSlider, &QSlider::sliderMoved,
        [&](int value) {
          QToolTip::showText(QCursor::pos(), QString("%1").arg(value*100/255), nullptr);
        });

    // Meter widgets
    connect(ui->meterSPoWidget, &meter::configureMeterSignal, this,
            [=](const meter_t &meterTypeRequested) {
        // Change the preferences and update settings widget to reflect new meter selection:
        prefs.meter1Type = meterTypeRequested;
        setupui->updateIfPref(if_meter1Type);
        // Change the meter locally:
        qInfo() << "New meter type" << meterTypeRequested;
        changeMeterType(meterTypeRequested, 1);
    });

    connect(ui->meter2Widget, &meter::configureMeterSignal, this,
            [=](const meter_t &meterTypeRequested) {
        // Change the preferences and update settings widget to reflect new meter selection:
        prefs.meter2Type = meterTypeRequested;
        setupui->updateIfPref(if_meter2Type);
        // Change the meter locally:
        changeMeterType(meterTypeRequested, 2);
        // Block duplicate meter selection in the other meter:
        ui->meter3Widget->blockMeterType(meterTypeRequested);
    });

    connect(ui->meter3Widget, &meter::configureMeterSignal, this,
            [=](const meter_t &meterTypeRequested) {
        // Change the preferences and update settings widget to reflect new meter selection:
        prefs.meter3Type = meterTypeRequested;
        setupui->updateIfPref(if_meter3Type);
        // Change the meter locally:
        changeMeterType(meterTypeRequested, 3);
        // Block duplicate meter selection in the other meter:
        ui->meter2Widget->blockMeterType(meterTypeRequested);
    });


}


void wfmain::enableControls(bool en)
{
    // This should contain all of the controls within the main window (other than power buttons)
    // So should be updated if any are added/removed/renamed.

    ui->freqDial->setEnabled(en);
    ui->tuningStepCombo->setEnabled(en);
    ui->tuneLockChk->setEnabled(en);
    ui->ritEnableChk->setEnabled(en);
    ui->ritTuneDial->setEnabled(en);
    ui->afGainSlider->setEnabled(en);
    ui->micGainSlider->setEnabled(en);
    ui->monitorSlider->setEnabled(en);
    ui->rfGainSlider->setEnabled(en);
    ui->sqlSlider->setEnabled(en);
    ui->txPowerSlider->setEnabled(en);

    ui->OtherControlsGrp->setEnabled(en);

    ui->cwButton->setEnabled(en);
    ui->memoriesBtn->setEnabled(en);
    ui->rptSetupBtn->setEnabled(en);
    ui->transmitBtn->setEnabled(en);
    ui->tuneEnableChk->setEnabled(en);
    ui->tuneNowBtn->setEnabled(en);

    ui->scopeSettingsGroup->setEnabled(en);
    ui->preampAttGroup->setEnabled(en);
    ui->antennaGroup->setEnabled(en);

    ui->meterSPoWidget->setEnabled(en);
    ui->meter2Widget->setEnabled(en);
    ui->meter3Widget->setEnabled(en);

    ui->showBandsBtn->setEnabled(en);
    ui->showFreqBtn->setEnabled(en);
}

void wfmain::updatedQueueInterval(qint64 interval)
{
    if (interval == -1)
        enableControls(false);
    else if (rigCaps != Q_NULLPTR)
        enableControls(true);
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

