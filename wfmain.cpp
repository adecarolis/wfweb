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

    logWindow = new loggingWindow(logFile);
    initLogging();
    logWindow->setInitialDebugState(debugMode);
    qInfo(logSystem()) << version;

    cal = new calibrationWindow();
    rpt = new repeaterSetup();
    sat = new satelliteSetup();
    trxadj = new transceiverAdjustments();
    cw = new cwSender();
    shut = new controllerSetup();
    abtBox = new aboutbox();
    selRad = new selectRadio();

    qRegisterMetaType<udpPreferences>(); // Needs to be registered early.
    qRegisterMetaType<rigCapabilities>();
    qRegisterMetaType<duplexMode>();
    qRegisterMetaType<rptAccessTxRx>();
    qRegisterMetaType<rptrAccessData_t>();
    qRegisterMetaType<rigInput>();
    qRegisterMetaType<meterKind>();
    qRegisterMetaType<spectrumMode>();
    qRegisterMetaType<freqt>();
    qRegisterMetaType<vfo_t>();
    qRegisterMetaType<rptrTone_t>();
    qRegisterMetaType<mode_info>();
    qRegisterMetaType<mode_kind>();
    qRegisterMetaType<audioPacket>();
    qRegisterMetaType <audioSetup>();
    qRegisterMetaType <SERVERCONFIG>();
    qRegisterMetaType <timekind>();
    qRegisterMetaType <datekind>();
    qRegisterMetaType<rigstate*>();
    qRegisterMetaType<QList<radio_cap_packet>>();
    qRegisterMetaType<QVector<BUTTON>*>();
    qRegisterMetaType<QVector<COMMAND>*>();
    qRegisterMetaType<const COMMAND*>();
    qRegisterMetaType<QList<radio_cap_packet>>();
    qRegisterMetaType<QList<spotData>>();
    qRegisterMetaType<networkStatus>();
    qRegisterMetaType<networkAudioLevels>();
    qRegisterMetaType<codecType>();
    qRegisterMetaType<errorType>();

    haveRigCaps = false;

    setupKeyShortcuts();

    setupMainUI();
    prepareSettingsWindow();

    setSerialDevicesUI();

    setDefPrefs();


    getSettingsFilePath(settingsFile);

    setupPlots();
    setDefaultColorPresets();

    loadSettings(); // Look for saved preferences

    audioDev = new audioDevices(prefs.audioSystem, QFontMetrics(ui->audioInputCombo->font()));
    connect(audioDev, SIGNAL(updated()), this, SLOT(setAudioDevicesUI()));
    audioDev->enumerate();
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
    connect(this, SIGNAL(setFrequencyRange(double, double)), cluster, SLOT(freqRange(double, double)));
    connect(this, SIGNAL(setClusterSkimmerSpots(bool)), cluster, SLOT(enableSkimmerSpots(bool)));

    connect(cluster, SIGNAL(sendSpots(QList<spotData>)), this, SLOT(receiveSpots(QList<spotData>)));
    connect(cluster, SIGNAL(sendOutput(QString)), this, SLOT(receiveClusterOutput(QString)));

    connect(clusterThread, SIGNAL(finished()), cluster, SLOT(deleteLater()));

    clusterThread->start();

    emit setClusterUdpPort(prefs.clusterUdpPort);
    emit setClusterEnableUdp(prefs.clusterUdpEnable);

    for (int f = 0; f < clusters.size(); f++)
    {
        if (clusters[f].isdefault)
        {
            emit setClusterServerName(clusters[f].server);
            emit setClusterTcpPort(clusters[f].port);
            emit setClusterUserName(clusters[f].userName);
            emit setClusterPassword(clusters[f].password);
            emit setClusterTimeout(clusters[f].timeout);
        }
    }
    emit setClusterEnableTcp(prefs.clusterTcpEnable);

    setServerToPrefs();

    amTransmitting = false;

#if defined(USB_CONTROLLER)
    // Setup USB Controller
    setupUsbControllerDevice();
    emit sendUsbControllerCommands(&usbCommands);
    emit sendUsbControllerButtons(&usbButtons);
#else
    ui->usbControllerBtn->setVisible(false);
#endif

    connect(ui->txPowerSlider, &QSlider::sliderMoved,
        [&](int value) {
          QToolTip::showText(QCursor::pos(), QString("%1").arg(value*100/255), nullptr);
        });

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
    if (rigCtl != Q_NULLPTR) {
        delete rigCtl;
    }

    if (audioDev != Q_NULLPTR) {
        delete audioDev;
    }

    if (prefs.audioSystem == portAudio) {
        Pa_Terminate();
    }
    delete rpt;
    delete ui;
    delete settings;
#if defined(USB_CONTROLLER)
    if (usbControllerThread != Q_NULLPTR) {
        usbControllerThread->quit();
        usbControllerThread->wait();
    }
#endif
}

void wfmain::closeEvent(QCloseEvent *event)
{
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

    QObject::connect(cb, &QCheckBox::stateChanged, [this](int state){
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

    if (msgbox.clickedButton() == yesButton) {
        QApplication::exit();
    } else {
        event->ignore();
    }
    delete cb;
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

    ui->audioSystemServerCombo->setEnabled(false);
    ui->audioSystemCombo->setEnabled(false);

    ui->connectBtn->setText("Cancel connection"); // We are attempting to connect

    makeRig();

    if (prefs.enableLAN)
    {
        ui->lanEnableBtn->setChecked(true);
        usingLAN = true;
        // We need to setup the tx/rx audio:
        udpPrefs.waterfallFormat = prefs.waterfallFormat;
        emit sendCommSetup(prefs.radioCIVAddr, udpPrefs, rxSetup, txSetup, prefs.virtualSerialPort, prefs.tcpPort);
    } else {
        ui->serialEnableBtn->setChecked(true);
        if( (prefs.serialPortRadio.toLower() == QString("auto")))
        {
            findSerialPort();
        } else {
            serialPortRig = prefs.serialPortRadio;
        }
        usingLAN = false;
        emit sendCommSetup(prefs.radioCIVAddr, serialPortRig, prefs.serialPortBaud,prefs.virtualSerialPort, prefs.tcpPort,prefs.waterfallFormat);
        ui->statusBar->showMessage(QString("Connecting to rig using serial port ").append(serialPortRig), 1000);
    }



}

void wfmain::createSettingsListItems()
{
    // Add items to the settings tab list widget
    ui->settingsList->addItem("Radio Access");     // 0
    ui->settingsList->addItem("User Interface");   // 1
    ui->settingsList->addItem("Radio Settings");   // 2
    ui->settingsList->addItem("Radio Server");     // 3
    ui->settingsList->addItem("External Control"); // 4
    ui->settingsList->addItem("DX Cluster"); // 5
    ui->settingsList->addItem("Experimental");     // 6
    //ui->settingsList->addItem("Audio Processing"); // 7
    ui->settingsStack->setCurrentIndex(0);
}

void wfmain::on_settingsList_currentRowChanged(int currentRow)
{
    ui->settingsStack->setCurrentIndex(currentRow);
}


void wfmain::connectSettingsList()
{

}


void wfmain::rigConnections()
{
    connect(this, SIGNAL(setCIVAddr(unsigned char)), rig, SLOT(setCIVAddr(unsigned char)));

    connect(this, SIGNAL(sendPowerOn()), rig, SLOT(powerOn()));
    connect(this, SIGNAL(sendPowerOff()), rig, SLOT(powerOff()));

    connect(rig, SIGNAL(haveFrequency(freqt)), this, SLOT(receiveFreq(freqt)));
    connect(this, SIGNAL(getFrequency()), rig, SLOT(getFrequency()));
    connect(this, SIGNAL(getMode()), rig, SLOT(getMode()));
    connect(this, SIGNAL(getDataMode()), rig, SLOT(getDataMode()));
    connect(this, SIGNAL(setDataMode(bool, unsigned char)), rig, SLOT(setDataMode(bool, unsigned char)));
    connect(this, SIGNAL(getBandStackReg(char,char)), rig, SLOT(getBandStackReg(char,char)));
    connect(rig, SIGNAL(havePTTStatus(bool)), this, SLOT(receivePTTstatus(bool)));
    connect(this, SIGNAL(setPTT(bool)), rig, SLOT(setPTT(bool)));
    connect(this, SIGNAL(getPTT()), rig, SLOT(getPTT()));

    connect(this, SIGNAL(selectVFO(vfo_t)), rig, SLOT(selectVFO(vfo_t)));
    connect(this, SIGNAL(sendVFOSwap()), rig, SLOT(exchangeVFOs()));
    connect(this, SIGNAL(sendVFOEqualAB()), rig, SLOT(equalizeVFOsAB()));
    connect(this, SIGNAL(sendVFOEqualMS()), rig, SLOT(equalizeVFOsMS()));

    connect(this, SIGNAL(sendCW(QString)), rig, SLOT(sendCW(QString)));
    connect(this, SIGNAL(stopCW()), rig, SLOT(sendStopCW()));
    connect(this, SIGNAL(setKeySpeed(unsigned char)), rig, SLOT(setKeySpeed(unsigned char)));
    connect(this, SIGNAL(getKeySpeed()), rig, SLOT(getKeySpeed()));
    connect(this, SIGNAL(setCwPitch(unsigned char)), rig, SLOT(setCwPitch(unsigned char)));
    connect(this, SIGNAL(setCWBreakMode(unsigned char)), rig, SLOT(setBreakIn(unsigned char)));
    connect(this, SIGNAL(getCWBreakMode()), rig, SLOT(getBreakIn()));
    connect(this->rig, &rigCommander::haveKeySpeed,
        [=](const unsigned char& wpm) { cw->handleKeySpeed(wpm); });
    connect(this->rig, &rigCommander::haveCwPitch,
        [=](const unsigned char& speed) { cw->handlePitch(speed); });
    connect(this->rig, &rigCommander::haveCWBreakMode,
            [=](const unsigned char &bm) { cw->handleBreakInMode(bm);});

    connect(rig, SIGNAL(haveBandStackReg(freqt,char,char,bool)), this, SLOT(receiveBandStackReg(freqt,char,char,bool)));
    connect(this, SIGNAL(setRitEnable(bool)), rig, SLOT(setRitEnable(bool)));
    connect(this, SIGNAL(setRitValue(int)), rig, SLOT(setRitValue(int)));
    connect(rig, SIGNAL(haveRitEnabled(bool)), this, SLOT(receiveRITStatus(bool)));
    connect(rig, SIGNAL(haveRitFrequency(int)), this, SLOT(receiveRITValue(int)));
    connect(this, SIGNAL(getRitEnabled()), rig, SLOT(getRitEnabled()));
    connect(this, SIGNAL(getRitValue()), rig, SLOT(getRitValue()));

    connect(this, SIGNAL(getDebug()), rig, SLOT(getDebug()));

    connect(this, SIGNAL(spectOutputDisable()), rig, SLOT(disableSpectOutput()));
    connect(this, SIGNAL(spectOutputEnable()), rig, SLOT(enableSpectOutput()));
    connect(this, SIGNAL(scopeDisplayDisable()), rig, SLOT(disableSpectrumDisplay()));
    connect(this, SIGNAL(scopeDisplayEnable()), rig, SLOT(enableSpectrumDisplay()));
    connect(rig, SIGNAL(haveMode(unsigned char, unsigned char)), this, SLOT(receiveMode(unsigned char, unsigned char)));
    connect(rig, SIGNAL(haveDataMode(bool)), this, SLOT(receiveDataModeStatus(bool)));
    connect(rig, SIGNAL(havePassband(quint16)), this, SLOT(receivePassband(quint16)));
    connect(rig, SIGNAL(haveCwPitch(unsigned char)), this, SLOT(receiveCwPitch(unsigned char)));

    // Repeater, duplex, and split:
    connect(rpt, SIGNAL(getDuplexMode()), rig, SLOT(getDuplexMode()));
    connect(rpt, SIGNAL(setDuplexMode(duplexMode)), rig, SLOT(setDuplexMode(duplexMode)));
    connect(rig, SIGNAL(haveDuplexMode(duplexMode)), rpt, SLOT(receiveDuplexMode(duplexMode)));
    connect(this, SIGNAL(getRptDuplexOffset()), rig, SLOT(getRptDuplexOffset()));
    connect(rig, SIGNAL(haveRptOffsetFrequency(freqt)), rpt, SLOT(handleRptOffsetFrequency(freqt)));
    connect(rpt, SIGNAL(getTone()), rig, SLOT(getTone()));
    connect(rpt, SIGNAL(getTSQL()), rig, SLOT(getTSQL()));
    connect(rpt, SIGNAL(getDTCS()), rig, SLOT(getDTCS()));

    connect(this->rpt, &repeaterSetup::setTone,
            [=](const rptrTone_t &t) { issueCmd(cmdSetTone, t);});

    connect(this->rpt, &repeaterSetup::setTSQL,
            [=](const rptrTone_t &t) { issueCmd(cmdSetTSQL, t);});

    connect(rpt, SIGNAL(setDTCS(quint16,bool,bool)), rig, SLOT(setDTCS(quint16,bool,bool)));
    connect(rpt, SIGNAL(getRptAccessMode()), rig, SLOT(getRptAccessMode()));

    //connect(rpt, SIGNAL(setRptAccessMode(rptAccessTxRx)), rig, SLOT(setRptAccessMode(rptAccessTxRx)));

    connect(this->rpt, &repeaterSetup::setRptAccessMode,
            [=](const rptrAccessData_t &rd) { issueCmd(cmdSetRptAccessMode, rd);});
    connect(this, SIGNAL(setRepeaterAccessMode(rptrAccessData_t)), rig, SLOT(setRptAccessMode(rptrAccessData_t)));
    connect(this, SIGNAL(setTone(rptrTone_t)), rig, SLOT(setTone(rptrTone_t)));

    connect(rig, SIGNAL(haveTone(quint16)), rpt, SLOT(handleTone(quint16)));
    connect(rig, SIGNAL(haveTSQL(quint16)), rpt, SLOT(handleTSQL(quint16)));
    connect(rig, SIGNAL(haveDTCS(quint16,bool,bool)), rpt, SLOT(handleDTCS(quint16,bool,bool)));
    connect(rig, SIGNAL(haveRptAccessMode(rptAccessTxRx)), rpt, SLOT(handleRptAccessMode(rptAccessTxRx)));
    connect(this->rpt, &repeaterSetup::setTransmitFrequency,
            [=](const freqt &transmitFreq) { issueCmd(cmdSetFreq, transmitFreq);});
    connect(this->rpt, &repeaterSetup::setTransmitMode,
            [=](const mode_info &transmitMode) { issueCmd(cmdSetMode, transmitMode);});
    connect(this->rpt, &repeaterSetup::selectVFO,
            [=](const vfo_t &v) { issueCmd(cmdSelVFO, v);});
    connect(this->rpt, &repeaterSetup::equalizeVFOsAB,
            [=]() { issueDelayedCommand(cmdVFOEqualAB);});
    connect(this->rpt, &repeaterSetup::equalizeVFOsMS,
            [=]() { issueDelayedCommand(cmdVFOEqualMS);});
    connect(this->rpt, &repeaterSetup::swapVFOs,
            [=]() { issueDelayedCommand(cmdVFOSwap);});
    connect(this->rpt, &repeaterSetup::setRptDuplexOffset,
            [=](const freqt &fOffset) { issueCmd(cmdSetRptDuplexOffset, fOffset);});
    connect(this->rpt, &repeaterSetup::getRptDuplexOffset,
            [=]() { issueDelayedCommand(cmdGetRptDuplexOffset);});

    connect(this, SIGNAL(setRptDuplexOffset(freqt)), rig, SLOT(setRptDuplexOffset(freqt)));
    connect(this, SIGNAL(getDuplexMode()), rig, SLOT(getDuplexMode()));

    connect(this, SIGNAL(getPassband()), rig, SLOT(getPassband()));
    connect(this, SIGNAL(setPassband(quint16)), rig, SLOT(setPassband(quint16)));
    connect(this, SIGNAL(getCwPitch()), rig, SLOT(getCwPitch()));
    connect(this, SIGNAL(getPskTone()), rig, SLOT(getPskTone()));
    connect(this, SIGNAL(getRttyMark()), rig, SLOT(getRttyMark()));
    connect(this, SIGNAL(getTone()), rig, SLOT(getTone()));
    connect(this, SIGNAL(getTSQL()), rig, SLOT(getTSQL()));
    connect(this, SIGNAL(getRptAccessMode()), rig, SLOT(getRptAccessMode()));

    connect(this, SIGNAL(getModInput(bool)), rig, SLOT(getModInput(bool)));
    connect(rig, SIGNAL(haveModInput(rigInput,bool)), this, SLOT(receiveModInput(rigInput, bool)));
    connect(this, SIGNAL(setModInput(rigInput, bool)), rig, SLOT(setModInput(rigInput,bool)));

    connect(rig, SIGNAL(haveSpectrumData(QByteArray, double, double)), this, SLOT(receiveSpectrumData(QByteArray, double, double)));
    connect(rig, SIGNAL(haveSpectrumMode(spectrumMode)), this, SLOT(receiveSpectrumMode(spectrumMode)));
    connect(this, SIGNAL(setScopeMode(spectrumMode)), rig, SLOT(setSpectrumMode(spectrumMode)));
    connect(this, SIGNAL(getScopeMode()), rig, SLOT(getScopeMode()));

    connect(this, SIGNAL(setFrequency(unsigned char, freqt)), rig, SLOT(setFrequency(unsigned char, freqt)));
    connect(this, SIGNAL(setScopeEdge(char)), rig, SLOT(setScopeEdge(char)));
    connect(this, SIGNAL(setScopeSpan(char)), rig, SLOT(setScopeSpan(char)));
    //connect(this, SIGNAL(getScopeMode()), rig, SLOT(getScopeMode()));
    connect(this, SIGNAL(getScopeEdge()), rig, SLOT(getScopeEdge()));
    connect(this, SIGNAL(getScopeSpan()), rig, SLOT(getScopeSpan()));
    connect(rig, SIGNAL(haveScopeSpan(freqt,bool)), this, SLOT(receiveSpectrumSpan(freqt,bool)));
    connect(this, SIGNAL(setScopeFixedEdge(double,double,unsigned char)), rig, SLOT(setSpectrumBounds(double,double,unsigned char)));

    connect(this, SIGNAL(setMode(unsigned char, unsigned char)), rig, SLOT(setMode(unsigned char, unsigned char)));
    connect(this, SIGNAL(setMode(mode_info)), rig, SLOT(setMode(mode_info)));

    // Levels (read and write)
    // Levels: Query:
    connect(this, SIGNAL(getLevels()), rig, SLOT(getLevels()));
    connect(this, SIGNAL(getRfGain()), rig, SLOT(getRfGain()));
    connect(this, SIGNAL(getAfGain()), rig, SLOT(getAfGain()));
    connect(this, SIGNAL(getSql()), rig, SLOT(getSql()));
    connect(this, SIGNAL(getIfShift()), rig, SLOT(getIFShift()));
    connect(this, SIGNAL(getTPBFInner()), rig, SLOT(getTPBFInner()));
    connect(this, SIGNAL(getTPBFOuter()), rig, SLOT(getTPBFOuter()));
    connect(this, SIGNAL(getTxPower()), rig, SLOT(getTxLevel()));
    connect(this, SIGNAL(getMicGain()), rig, SLOT(getMicGain()));
    connect(this, SIGNAL(getSpectrumRefLevel()), rig, SLOT(getSpectrumRefLevel()));
    connect(this, SIGNAL(getModInputLevel(rigInput)), rig, SLOT(getModInputLevel(rigInput)));


    // Levels: Set:
    connect(this, SIGNAL(setRfGain(unsigned char)), rig, SLOT(setRfGain(unsigned char)));
    connect(this, SIGNAL(setAfGain(unsigned char)), rig, SLOT(setAfGain(unsigned char)));
    connect(this, SIGNAL(setSql(unsigned char)), rig, SLOT(setSquelch(unsigned char)));
    connect(this, SIGNAL(setIFShift(unsigned char)), rig, SLOT(setIFShift(unsigned char)));
    connect(this, SIGNAL(setTPBFInner(unsigned char)), rig, SLOT(setTPBFInner(unsigned char)));
    connect(this, SIGNAL(setTPBFOuter(unsigned char)), rig, SLOT(setTPBFOuter(unsigned char)));
    connect(this, SIGNAL(setTxPower(unsigned char)), rig, SLOT(setTxPower(unsigned char)));
    connect(this, SIGNAL(setMicGain(unsigned char)), rig, SLOT(setMicGain(unsigned char)));
    connect(this, SIGNAL(setMonitorLevel(unsigned char)), rig, SLOT(setMonitorLevel(unsigned char)));
    connect(this, SIGNAL(setVoxGain(unsigned char)), rig, SLOT(setVoxGain(unsigned char)));
    connect(this, SIGNAL(setAntiVoxGain(unsigned char)), rig, SLOT(setAntiVoxGain(unsigned char)));
    connect(this, SIGNAL(setSpectrumRefLevel(int)), rig, SLOT(setSpectrumRefLevel(int)));
    connect(this, SIGNAL(setModLevel(rigInput, unsigned char)), rig, SLOT(setModInputLevel(rigInput, unsigned char)));

    // Levels: handle return on query:
    connect(rig, SIGNAL(haveRfGain(unsigned char)), this, SLOT(receiveRfGain(unsigned char)));
    connect(rig, SIGNAL(haveAfGain(unsigned char)), this, SLOT(receiveAfGain(unsigned char)));
    connect(rig, SIGNAL(haveSql(unsigned char)), this, SLOT(receiveSql(unsigned char)));
    connect(rig, SIGNAL(haveIFShift(unsigned char)), trxadj, SLOT(updateIFShift(unsigned char)));
    connect(rig, SIGNAL(haveTPBFInner(unsigned char)), trxadj, SLOT(updateTPBFInner(unsigned char)));
    connect(rig, SIGNAL(haveTPBFOuter(unsigned char)), trxadj, SLOT(updateTPBFOuter(unsigned char)));
    connect(rig, SIGNAL(haveTPBFInner(unsigned char)), this, SLOT(receiveTPBFInner(unsigned char)));
    connect(rig, SIGNAL(haveTPBFOuter(unsigned char)), this, SLOT(receiveTPBFOuter(unsigned char)));
    connect(rig, SIGNAL(haveTxPower(unsigned char)), this, SLOT(receiveTxPower(unsigned char)));
    connect(rig, SIGNAL(haveMicGain(unsigned char)), this, SLOT(receiveMicGain(unsigned char)));
    connect(rig, SIGNAL(haveSpectrumRefLevel(int)), this, SLOT(receiveSpectrumRefLevel(int)));
    connect(rig, SIGNAL(haveACCGain(unsigned char,unsigned char)), this, SLOT(receiveACCGain(unsigned char,unsigned char)));
    connect(rig, SIGNAL(haveUSBGain(unsigned char)), this, SLOT(receiveUSBGain(unsigned char)));
    connect(rig, SIGNAL(haveLANGain(unsigned char)), this, SLOT(receiveLANGain(unsigned char)));

    //Metering:
    connect(this, SIGNAL(getMeters(meterKind)), rig, SLOT(getMeters(meterKind)));
    connect(rig, SIGNAL(haveMeter(meterKind,unsigned char)), this, SLOT(receiveMeter(meterKind,unsigned char)));

    // Rig and ATU info:
    connect(this, SIGNAL(startATU()), rig, SLOT(startATU()));
    connect(this, SIGNAL(setATU(bool)), rig, SLOT(setATU(bool)));
    connect(this, SIGNAL(getATUStatus()), rig, SLOT(getATUStatus()));
    connect(this, SIGNAL(getRigID()), rig, SLOT(getRigID()));
    connect(rig, SIGNAL(haveATUStatus(unsigned char)), this, SLOT(receiveATUStatus(unsigned char)));
    connect(rig, SIGNAL(haveRigID(rigCapabilities)), this, SLOT(receiveRigID(rigCapabilities)));
    connect(this, SIGNAL(setAttenuator(unsigned char)), rig, SLOT(setAttenuator(unsigned char)));
    connect(this, SIGNAL(setPreamp(unsigned char)), rig, SLOT(setPreamp(unsigned char)));
    connect(this, SIGNAL(setAntenna(unsigned char, bool)), rig, SLOT(setAntenna(unsigned char, bool)));
    connect(this, SIGNAL(getPreamp()), rig, SLOT(getPreamp()));
    connect(rig, SIGNAL(havePreamp(unsigned char)), this, SLOT(receivePreamp(unsigned char)));
    connect(this, SIGNAL(getAttenuator()), rig, SLOT(getAttenuator()));
    connect(rig, SIGNAL(haveAttenuator(unsigned char)), this, SLOT(receiveAttenuator(unsigned char)));
    connect(this, SIGNAL(getAntenna()), rig, SLOT(getAntenna()));
    connect(rig, SIGNAL(haveAntenna(unsigned char,bool)), this, SLOT(receiveAntennaSel(unsigned char,bool)));


    // Speech (emitted from rig speaker)
    connect(this, SIGNAL(sayAll()), rig, SLOT(sayAll()));
    connect(this, SIGNAL(sayFrequency()), rig, SLOT(sayFrequency()));
    connect(this, SIGNAL(sayMode()), rig, SLOT(sayMode()));

    // calibration window:
    connect(cal, SIGNAL(requestRefAdjustCourse()), rig, SLOT(getRefAdjustCourse()));
    connect(cal, SIGNAL(requestRefAdjustFine()), rig, SLOT(getRefAdjustFine()));
    connect(rig, SIGNAL(haveRefAdjustCourse(unsigned char)), cal, SLOT(handleRefAdjustCourse(unsigned char)));
    connect(rig, SIGNAL(haveRefAdjustFine(unsigned char)), cal, SLOT(handleRefAdjustFine(unsigned char)));
    connect(cal, SIGNAL(setRefAdjustCourse(unsigned char)), rig, SLOT(setRefAdjustCourse(unsigned char)));
    connect(cal, SIGNAL(setRefAdjustFine(unsigned char)), rig, SLOT(setRefAdjustFine(unsigned char)));

    // Date and Time:
    connect(this, SIGNAL(setTime(timekind)), rig, SLOT(setTime(timekind)));
    connect(this, SIGNAL(setDate(datekind)), rig, SLOT(setDate(datekind)));
    connect(this, SIGNAL(setUTCOffset(timekind)), rig, SLOT(setUTCOffset(timekind)));

}

//void wfmain::removeRigConnections()
//{

//}

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
        connect(this, SIGNAL(sendCommSetup(unsigned char, udpPreferences, audioSetup, audioSetup, QString, quint16)), rig, SLOT(commSetup(unsigned char, udpPreferences, audioSetup, audioSetup, QString, quint16)));
        connect(this, SIGNAL(sendCommSetup(unsigned char, QString, quint32,QString, quint16,quint8)), rig, SLOT(commSetup(unsigned char, QString, quint32,QString, quint16,quint8)));
        connect(this, SIGNAL(setRTSforPTT(bool)), rig, SLOT(setRTSforPTT(bool)));

        connect(rig, SIGNAL(haveBaudRate(quint32)), this, SLOT(receiveBaudRate(quint32)));

        connect(this, SIGNAL(sendCloseComm()), rig, SLOT(closeComm()));
        connect(this, SIGNAL(sendChangeLatency(quint16)), rig, SLOT(changeLatency(quint16)));
        connect(this, SIGNAL(getRigCIV()), rig, SLOT(findRigs()));
        connect(this, SIGNAL(setRigID(unsigned char)), rig, SLOT(setRigID(unsigned char)));
        connect(rig, SIGNAL(discoveredRigID(rigCapabilities)), this, SLOT(receiveFoundRigID(rigCapabilities)));
        connect(rig, SIGNAL(commReady()), this, SLOT(receiveCommReady()));

        connect(this, SIGNAL(requestRigState()), rig, SLOT(sendState()));
        connect(this, SIGNAL(stateUpdated()), rig, SLOT(stateUpdated()));
        connect(rig, SIGNAL(stateInfo(rigstate*)), this, SLOT(receiveStateInfo(rigstate*)));
        if (rigCtl != Q_NULLPTR) {
            connect(rig, SIGNAL(stateInfo(rigstate*)), rigCtl, SLOT(receiveStateInfo(rigstate*)));
            connect(rigCtl, SIGNAL(stateUpdated()), rig, SLOT(stateUpdated()));
        }
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
        if (rigCtl != Q_NULLPTR) {
            rigCtl->disconnect();
        }
        rigThread->disconnect();

        rig->disconnect();
        
        delete rigThread;
        delete rig;
        rig = Q_NULLPTR;
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
        emit getRigCIV();
        issueDelayedCommand(cmdGetRigCIV);
        delayedCommand->start();
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
            emit getRigID();
            issueDelayedCommand(cmdGetRigID);
            delayedCommand->start();
        }
    }
}


void wfmain::receiveFoundRigID(rigCapabilities rigCaps)
{
    // Entry point for unknown rig being identified at the start of the program.
    //now we know what the rig ID is:
    //qInfo(logSystem()) << "In wfview, we now have a reply to our request for rig identity sent to CIV BROADCAST.";

    if(rig->usingLAN())
    {
        usingLAN = true;
    } else {
        usingLAN = false;
    }

    receiveRigID(rigCaps);
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
    
    this->rigStatus->setText(status.message);
    selRad->audioOutputLevel(status.rxAudioLevel);
    selRad->audioInputLevel(status.txAudioLevel);
    //qInfo(logSystem()) << "Got Status Update" << status.rxAudioLevel;
}

void wfmain::receiveNetworkAudioLevels(networkAudioLevels l)
{
    /*
    meterKind m2mtr = ui->meter2Widget->getMeterType();

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


    meterKind m = meterNone;
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

void wfmain::setupPlots()
{
    spectrumDrawLock = true;
    plot = ui->plot;

    wf = ui->waterfall;

    passbandIndicator = new QCPItemRect(plot);
    passbandIndicator->setAntialiased(true);
    passbandIndicator->setPen(QPen(Qt::red));
    passbandIndicator->setBrush(QBrush(Qt::red));
    passbandIndicator->setSelectable(true);

    pbtIndicator = new QCPItemRect(plot);
    pbtIndicator->setAntialiased(true);
    pbtIndicator->setPen(QPen(Qt::red));
    pbtIndicator->setBrush(QBrush(Qt::red));
    pbtIndicator->setSelectable(true);
    pbtIndicator->setVisible(false);

    freqIndicatorLine = new QCPItemLine(plot);
    freqIndicatorLine->setAntialiased(true);
    freqIndicatorLine->setPen(QPen(Qt::blue));

    /*
    text = new QCPItemText(plot);
    text->setAntialiased(true);
    text->setColor(QColor(Qt::red));
    text->setText("TEST");
    text->position->setCoords(14.195, rigCaps.spectAmpMax);
    text->setFont(QFont(font().family(), 12));
    */

    ui->plot->addGraph(); // primary
    ui->plot->addGraph(0, 0); // secondary, peaks, same axis as first.
    ui->plot->addLayer( "Top Layer", ui->plot->layer("main"));
    ui->plot->graph(0)->setLayer("Top Layer");


    ui->waterfall->addGraph();

    colorMap = new QCPColorMap(wf->xAxis, wf->yAxis);
    colorMapData = NULL;

#if QCUSTOMPLOT_VERSION < 0x020001
    wf->addPlottable(colorMap);
#endif

    colorScale = new QCPColorScale(wf);

    ui->tabWidget->setCurrentIndex(0);

    QColor color(20+200/4.0*1,70*(1.6-1/4.0), 150, 150);
    plot->graph(1)->setLineStyle(QCPGraph::lsLine);
    plot->graph(1)->setPen(QPen(color.lighter(200)));
    plot->graph(1)->setBrush(QBrush(color));

    freqIndicatorLine->start->setCoords(0.5, 0);
    freqIndicatorLine->end->setCoords(0.5, 160);

    passbandIndicator->topLeft->setCoords(0.5, 0);
    passbandIndicator->bottomRight->setCoords(0.5, 160);

    pbtIndicator->topLeft->setCoords(0.5, 0);
    pbtIndicator->bottomRight->setCoords(0.5, 160);

    // Plot user interaction
    connect(plot, SIGNAL(mouseDoubleClick(QMouseEvent*)), this, SLOT(handlePlotDoubleClick(QMouseEvent*)));
    connect(wf, SIGNAL(mouseDoubleClick(QMouseEvent*)), this, SLOT(handleWFDoubleClick(QMouseEvent*)));
    connect(plot, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(handlePlotClick(QMouseEvent*)));
    connect(wf, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(handleWFClick(QMouseEvent*)));
    connect(plot, SIGNAL(mouseRelease(QMouseEvent*)), this, SLOT(handlePlotMouseRelease(QMouseEvent*)));
    connect(plot, SIGNAL(mouseMove(QMouseEvent*)), this, SLOT(handlePlotMouseMove(QMouseEvent *)));
    connect(wf, SIGNAL(mouseWheel(QWheelEvent*)), this, SLOT(handleWFScroll(QWheelEvent*)));
    connect(plot, SIGNAL(mouseWheel(QWheelEvent*)), this, SLOT(handlePlotScroll(QWheelEvent*)));
    spectrumDrawLock = false;
}

void wfmain::setupMainUI()
{
    createSettingsListItems();

    ui->bandStkLastUsedBtn->setVisible(false);
    ui->bandStkVoiceBtn->setVisible(false);
    ui->bandStkDataBtn->setVisible(false);
    ui->bandStkCWBtn->setVisible(false);

    ui->baudRateCombo->insertItem(0, QString("115200"), 115200);
    ui->baudRateCombo->insertItem(1, QString("57600"), 57600);
    ui->baudRateCombo->insertItem(2, QString("38400"), 38400);
    ui->baudRateCombo->insertItem(3, QString("28800"), 28800);
    ui->baudRateCombo->insertItem(4, QString("19200"), 19200);
    ui->baudRateCombo->insertItem(5, QString("9600"), 9600);
    ui->baudRateCombo->insertItem(6, QString("4800"), 4800);
    ui->baudRateCombo->insertItem(7, QString("2400"), 2400);
    ui->baudRateCombo->insertItem(8, QString("1200"), 1200);
    ui->baudRateCombo->insertItem(9, QString("300"), 300);

    ui->spectrumModeCombo->addItem("Center", (spectrumMode)spectModeCenter);
    ui->spectrumModeCombo->addItem("Fixed", (spectrumMode)spectModeFixed);
    ui->spectrumModeCombo->addItem("Scroll-C", (spectrumMode)spectModeScrollC);
    ui->spectrumModeCombo->addItem("Scroll-F", (spectrumMode)spectModeScrollF);

    ui->modeSelectCombo->addItem("LSB",  0x00);
    ui->modeSelectCombo->addItem("USB",  0x01);
    ui->modeSelectCombo->addItem("FM",   0x05);
    ui->modeSelectCombo->addItem("AM",   0x02);
    ui->modeSelectCombo->addItem("CW",   0x03);
    ui->modeSelectCombo->addItem("CW-R", 0x07);
    ui->modeSelectCombo->addItem("RTTY", 0x04);
    ui->modeSelectCombo->addItem("RTTY-R", 0x08);


    ui->modeFilterCombo->addItem("1", 1);
    ui->modeFilterCombo->addItem("2", 2);
    ui->modeFilterCombo->addItem("3", 3);
    ui->modeFilterCombo->addItem("Setup...", 99);

    ui->tuningStepCombo->blockSignals(true);

    ui->tuningStepCombo->addItem("1 Hz",      (unsigned int)       1);
    ui->tuningStepCombo->addItem("10 Hz",     (unsigned int)      10);
    ui->tuningStepCombo->addItem("100 Hz",    (unsigned int)     100);
    ui->tuningStepCombo->addItem("500 Hz",    (unsigned int)     500);
    ui->tuningStepCombo->addItem("1 kHz",     (unsigned int)    1000);
    ui->tuningStepCombo->addItem("2.5 kHz",   (unsigned int)    2500);
    ui->tuningStepCombo->addItem("5 kHz",     (unsigned int)    5000);
    ui->tuningStepCombo->addItem("6.125 kHz", (unsigned int)    6125);	// PMR
    ui->tuningStepCombo->addItem("8.333 kHz", (unsigned int)    8333);	// airband stepsize
    ui->tuningStepCombo->addItem("9 kHz",     (unsigned int)    9000);	// European medium wave stepsize
    ui->tuningStepCombo->addItem("10 kHz",    (unsigned int)   10000);
    ui->tuningStepCombo->addItem("12.5 kHz",  (unsigned int)   12500);
    ui->tuningStepCombo->addItem("25 kHz",    (unsigned int)   25000);
    ui->tuningStepCombo->addItem("100 kHz",   (unsigned int)  100000);
    ui->tuningStepCombo->addItem("250 kHz",   (unsigned int)  250000);
    ui->tuningStepCombo->addItem("1 MHz",     (unsigned int) 1000000);  //for 23 cm and HF


    ui->tuningStepCombo->setCurrentIndex(2);
    ui->tuningStepCombo->blockSignals(false);

    ui->wfthemeCombo->addItem("Jet", QCPColorGradient::gpJet);
    ui->wfthemeCombo->addItem("Cold", QCPColorGradient::gpCold);
    ui->wfthemeCombo->addItem("Hot", QCPColorGradient::gpHot);
    ui->wfthemeCombo->addItem("Thermal", QCPColorGradient::gpThermal);
    ui->wfthemeCombo->addItem("Night", QCPColorGradient::gpNight);
    ui->wfthemeCombo->addItem("Ion", QCPColorGradient::gpIon);
    ui->wfthemeCombo->addItem("Gray", QCPColorGradient::gpGrayscale);
    ui->wfthemeCombo->addItem("Geography", QCPColorGradient::gpGeography);
    ui->wfthemeCombo->addItem("Hues", QCPColorGradient::gpHues);
    ui->wfthemeCombo->addItem("Polar", QCPColorGradient::gpPolar);
    ui->wfthemeCombo->addItem("Spectrum", QCPColorGradient::gpSpectrum);
    ui->wfthemeCombo->addItem("Candy", QCPColorGradient::gpCandy);

    ui->meter2selectionCombo->addItem("None", meterNone);
    ui->meter2selectionCombo->addItem("SWR", meterSWR);
    ui->meter2selectionCombo->addItem("ALC", meterALC);
    ui->meter2selectionCombo->addItem("Compression", meterComp);
    ui->meter2selectionCombo->addItem("Voltage", meterVoltage);
    ui->meter2selectionCombo->addItem("Current", meterCurrent);
    ui->meter2selectionCombo->addItem("Center", meterCenter);
    ui->meter2selectionCombo->addItem("TxRxAudio", meterAudio);
    ui->meter2selectionCombo->addItem("RxAudio", meterRxAudio);
    ui->meter2selectionCombo->addItem("TxAudio", meterTxMod);

    ui->meter2Widget->hide();

    ui->meter2selectionCombo->show();
    ui->meter2selectionCombo->setCurrentIndex((int)prefs.meter2Type);

    ui->secondaryMeterSelectionLabel->show();


    // Future ideas:
    //ui->meter2selectionCombo->addItem("Transmit Audio", meterTxMod);
    //ui->meter2selectionCombo->addItem("Receive Audio", meterRxAudio);
    //ui->meter2selectionCombo->addItem("Latency", meterLatency);




    spans << "2.5k" << "5.0k" << "10k" << "25k";
    spans << "50k" << "100k" << "250k" << "500k";
    ui->scopeBWCombo->insertItems(0, spans);

    edges << "1" << "2" << "3" << "4";
    ui->scopeEdgeCombo->insertItems(0, edges);

    ui->splitter->setHandleWidth(5);

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

    ui->scopeRefLevelSlider->setTickInterval(50);
    ui->scopeRefLevelSlider->setSingleStep(20);

    ui->freqMhzLineEdit->setValidator( new QDoubleValidator(0, 100, 6, this));
    ui->controlPortTxt->setValidator(new QIntValidator(this));

    qDebug(logSystem()) << "Running with debugging options enabled.";
#ifdef QT_DEBUG
    ui->debugBtn->setVisible(true);
    ui->satOpsBtn->setVisible(true);
#else
    ui->debugBtn->setVisible(false);
    ui->satOpsBtn->setVisible(false);
#endif

    rigStatus = new QLabel(this);
    ui->statusBar->addPermanentWidget(rigStatus);
    ui->statusBar->showMessage("Connecting to rig...", 1000);

    pttLed = new QLedLabel(this);
    ui->statusBar->addPermanentWidget(pttLed);
    pttLed->setState(QLedLabel::State::StateOk);

    connectedLed = new QLedLabel(this);
    ui->statusBar->addPermanentWidget(connectedLed);

    rigName = new QLabel(this);
    rigName->setAlignment(Qt::AlignRight);
    ui->statusBar->addPermanentWidget(rigName);
    rigName->setText("NONE");
    rigName->setFixedWidth(60);

    freq.MHzDouble = 0.0;
    freq.Hz = 0;
    oldFreqDialVal = ui->freqDial->value();

    ui->tuneLockChk->setChecked(false);
    freqLock = false;

    connect(ui->tabWidget, SIGNAL(currentChanged(int)), this, SLOT(updateSizes(int)));

    connect(
                ui->txPowerSlider, &QSlider::valueChanged,
                [=](const int &newValue) { statusFromSliderPercent("Tx Power", newValue);}
    );

    connect(
                ui->rfGainSlider, &QSlider::valueChanged,
                [=](const int &newValue) { statusFromSliderPercent("RF Gain", newValue);}
    );

    connect(
                ui->afGainSlider, &QSlider::valueChanged,
                [=](const int &newValue) { statusFromSliderPercent("AF Gain", newValue);}
    );

    connect(
                ui->micGainSlider, &QSlider::valueChanged,
                [=](const int &newValue) { statusFromSliderPercent("TX Audio Gain", newValue);}
    );

    connect(
                ui->sqlSlider, &QSlider::valueChanged,
                [=](const int &newValue) { statusFromSliderPercent("Squelch", newValue);}
    );

    // -200 0 +200.. take log?
    connect(
                ui->scopeRefLevelSlider, &QSlider::valueChanged,
                [=](const int &newValue) { statusFromSliderRaw("Scope Ref Level", newValue);}
    );

    connect(
                ui->wfLengthSlider, &QSlider::valueChanged,
                [=](const int &newValue) { statusFromSliderRaw("Waterfall Length", newValue);}
    );

    connect(this->trxadj, &transceiverAdjustments::setIFShift,
            [=](const unsigned char &newValue) { issueCmdUniquePriority(cmdSetIFShift, newValue);}
    );

    connect(this->trxadj, &transceiverAdjustments::setTPBFInner,
            [=](const unsigned char &newValue) { issueCmdUniquePriority(cmdSetTPBFInner, newValue);}
    );

    connect(this->trxadj, &transceiverAdjustments::setTPBFOuter,
            [=](const unsigned char &newValue) { issueCmdUniquePriority(cmdSetTPBFOuter, newValue);}
    );
    connect(this->trxadj, &transceiverAdjustments::setPassband,
            [=](const quint16 &passbandHz) { issueCmdUniquePriority(cmdSetPassband, passbandHz);}
    );

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

}

void wfmain::prepareSettingsWindow()
{
    settingsTabisAttached = true;

    settingsWidgetWindow = new QWidget;
    settingsWidgetLayout = new QGridLayout;
    settingsWidgetTab = new QTabWidget;

    settingsWidgetWindow->setLayout(settingsWidgetLayout);
    settingsWidgetLayout->addWidget(settingsWidgetTab);
    settingsWidgetWindow->setWindowFlag(Qt::WindowCloseButtonHint, false);
    //settingsWidgetWindow->setWindowFlag(Qt::WindowMinimizeButtonHint, false);
    //settingsWidgetWindow->setWindowFlag(Qt::WindowMaximizeButtonHint, false);
    // TODO: Capture an event when the window closes and handle accordingly.
}

void wfmain::updateSizes(int tabIndex)
{

    // This function does nothing unless you are using a rig without spectrum.
    // This is a hack. It is not great, but it seems to work ok.
    if(!rigCaps.hasSpectrum)
    {
        // Set "ignore" size policy for non-selected tabs:
        for(int i=1;i<ui->tabWidget->count();i++)
            if((i!=tabIndex))
                ui->tabWidget->widget(i)->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored); // allows size to be any size that fits the tab bar

        if(tabIndex==0)
        {

            ui->tabWidget->widget(0)->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
            ui->tabWidget->widget(0)->setMaximumSize(ui->tabWidget->widget(0)->minimumSizeHint());
            ui->tabWidget->widget(0)->adjustSize(); // tab
            this->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
            this->setMaximumSize(QSize(940,350));
            this->setMinimumSize(QSize(940,350));

            resize(minimumSize());
            adjustSize(); // main window
        } else {
            // At some other tab, with or without spectrum:
            ui->tabWidget->widget(tabIndex)->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
            this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            this->setMinimumSize(QSize(1024, 600)); // not large enough for settings tab
            this->setMaximumSize(QSize(65535,65535));
            resize(minimumSize());
            adjustSize();
        }
    } else {
        ui->tabWidget->widget(tabIndex)->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
        ui->tabWidget->widget(tabIndex)->setMaximumSize(65535,65535);
        //ui->tabWidget->widget(0)->setMinimumSize();
    }

}

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
    delayedCommand->setInterval(delayedCmdStartupInterval_ms); // 250ms until we find rig civ and id, then 100ms.
    delayedCommand->setSingleShot(false);
    connect(delayedCommand, SIGNAL(timeout()), this, SLOT(sendRadioCommandLoop()));

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
    ui->serverSetupGroup->setEnabled(serverConfig.enabled);
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
    ui->fullScreenChk->setChecked(prefs.useFullScreen);
    on_fullScreenChk_clicked(prefs.useFullScreen);

    ui->useSystemThemeChk->setChecked(prefs.useSystemTheme);
    on_useSystemThemeChk_clicked(prefs.useSystemTheme);

    underlayMode = prefs.underlayMode;
    switch(underlayMode)
    {
        case underlayNone:
            ui->underlayNone->setChecked(true);
            break;
        case underlayPeakHold:
            ui->underlayPeakHold->setChecked(true);
            break;
        case underlayPeakBuffer:
            ui->underlayPeakBuffer->setChecked(true);
            break;
        case underlayAverageBuffer:
            ui->underlayAverageBuffer->setChecked(true);
            break;
        default:
            break;
    }

    ui->underlayBufferSlider->setValue(prefs.underlayBufferSize);
    on_underlayBufferSlider_valueChanged(prefs.underlayBufferSize);

    ui->wfAntiAliasChk->setChecked(prefs.wfAntiAlias);
    on_wfAntiAliasChk_clicked(prefs.wfAntiAlias);

    ui->wfInterpolateChk->setChecked(prefs.wfInterpolate);
    on_wfInterpolateChk_clicked(prefs.wfInterpolate);

    ui->wfLengthSlider->setValue(prefs.wflength);
    prepareWf(prefs.wflength);
    preparePlasma();
    ui->topLevelSlider->setValue(prefs.plotCeiling);
    ui->botLevelSlider->setValue(prefs.plotFloor);

    plot->yAxis->setRange(QCPRange(prefs.plotFloor, prefs.plotCeiling));
    colorMap->setDataRange(QCPRange(prefs.plotFloor, prefs.plotCeiling));

    colorPrefsType p;
    for(int pn=0; pn < numColorPresetsTotal; pn++)
    {
        p = colorPreset[pn];
        if(p.presetName != Q_NULLPTR)
            ui->colorPresetCombo->setItemText(pn, *p.presetName);
    }

    ui->colorPresetCombo->setCurrentIndex(prefs.currentColorPresetNumber);
    loadColorPresetToUIandPlots(prefs.currentColorPresetNumber);

    ui->wfthemeCombo->setCurrentIndex(ui->wfthemeCombo->findData(prefs.wftheme));
    colorMap->setGradient(static_cast<QCPColorGradient::GradientPreset>(prefs.wftheme));

    ui->tuningFloorZerosChk->blockSignals(true);
    ui->tuningFloorZerosChk->setChecked(prefs.niceTS);
    ui->tuningFloorZerosChk->blockSignals(false);

    ui->autoSSBchk->blockSignals(true);
    ui->autoSSBchk->setChecked(prefs.automaticSidebandSwitching);
    ui->autoSSBchk->blockSignals(false);

    ui->useCIVasRigIDChk->blockSignals(true);
    ui->useCIVasRigIDChk->setChecked(prefs.CIVisRadioModel);
    ui->useCIVasRigIDChk->blockSignals(false);
}

void wfmain::setSerialDevicesUI()
{
    ui->serialDeviceListCombo->blockSignals(true);
    ui->serialDeviceListCombo->addItem("Auto", 0);
    int i = 0;
    foreach(const QSerialPortInfo & serialPortInfo, QSerialPortInfo::availablePorts())
    {
        portList.append(serialPortInfo.portName());
#if defined(Q_OS_LINUX) || defined(Q_OS_MAC)
        ui->serialDeviceListCombo->addItem(QString("/dev/") + serialPortInfo.portName(), i++);
#else
        ui->serialDeviceListCombo->addItem(serialPortInfo.portName(), i++);
        //qInfo(logSystem()) << "Serial Port found: " << serialPortInfo.portName() << "Manufacturer:" << serialPortInfo.manufacturer() << "Product ID" << serialPortInfo.description() << "S/N" << serialPortInfo.serialNumber();
#endif
    }
#if defined(Q_OS_LINUX) || defined(Q_OS_MAC)
    ui->serialDeviceListCombo->addItem("Manual...", 256);
#endif
    ui->serialDeviceListCombo->blockSignals(false);

    ui->vspCombo->blockSignals(true);

#ifdef Q_OS_WIN
    ui->vspCombo->addItem(QString("None"), i++);

    foreach(const QSerialPortInfo & serialPortInfo, QSerialPortInfo::availablePorts())
    {
        ui->vspCombo->addItem(serialPortInfo.portName());
    }
#else
    // Provide reasonable names for the symbolic link to the pty device
#ifdef Q_OS_MAC
    QString vspName = QStandardPaths::standardLocations(QStandardPaths::DownloadLocation)[0] + "/rig-pty";
#else
    QString vspName = QDir::homePath() + "/rig-pty";
#endif
    for (i = 1; i < 8; i++) {
        ui->vspCombo->addItem(vspName + QString::number(i));

        if (QFile::exists(vspName + QString::number(i))) {
            auto* model = qobject_cast<QStandardItemModel*>(ui->vspCombo->model());
            auto* item = model->item(ui->vspCombo->count() - 1);
            item->setEnabled(false);
        }
    }
    ui->vspCombo->addItem(vspName + QString::number(i));
    ui->vspCombo->addItem(QString("None"), i++);

#endif
    ui->vspCombo->setEditable(true);
    ui->vspCombo->blockSignals(false);
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
    keyControlT->setKey(Qt::CTRL + Qt::Key_T);
    connect(keyControlT, SIGNAL(activated()), this, SLOT(shortcutControlT()));

    keyControlR = new QShortcut(this);
    keyControlR->setKey(Qt::CTRL + Qt::Key_R);
    connect(keyControlR, SIGNAL(activated()), this, SLOT(shortcutControlR()));

    keyControlI = new QShortcut(this);
    keyControlI->setKey(Qt::CTRL + Qt::Key_I);
    connect(keyControlI, SIGNAL(activated()), this, SLOT(shortcutControlI()));

    keyControlU = new QShortcut(this);
    keyControlU->setKey(Qt::CTRL + Qt::Key_U);
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
    keyShiftMinus->setKey(Qt::SHIFT + Qt::Key_Minus);
    connect(keyShiftMinus, SIGNAL(activated()), this, SLOT(shortcutShiftMinus()));

    keyShiftPlus = new QShortcut(this);
    keyShiftPlus->setKey(Qt::SHIFT + Qt::Key_Plus);
    connect(keyShiftPlus, SIGNAL(activated()), this, SLOT(shortcutShiftPlus()));

    keyControlMinus = new QShortcut(this);
    keyControlMinus->setKey(Qt::CTRL + Qt::Key_Minus);
    connect(keyControlMinus, SIGNAL(activated()), this, SLOT(shortcutControlMinus()));

    keyControlPlus = new QShortcut(this);
    keyControlPlus->setKey(Qt::CTRL + Qt::Key_Plus);
    connect(keyControlPlus, SIGNAL(activated()), this, SLOT(shortcutControlPlus()));

    keyQuit = new QShortcut(this);
    keyQuit->setKey(Qt::CTRL + Qt::Key_Q);
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
    keyShiftK->setKey(Qt::SHIFT + Qt::Key_K);
    connect(keyShiftK, &QShortcut::activated,
            [=]() {
        if (freqLock) return;
        this->shortcutShiftPlus();
    });


    keyShiftJ = new QShortcut(this);
    keyShiftJ->setKey(Qt::SHIFT + Qt::Key_J);
    connect(keyShiftJ, &QShortcut::activated,
            [=]() {
        if (freqLock) return;
        this->shortcutShiftMinus();
    });

    keyControlK = new QShortcut(this);
    keyControlK->setKey(Qt::CTRL + Qt::Key_K);
    connect(keyControlK, &QShortcut::activated,
            [=]() {
        if (freqLock) return;
        this->shortcutControlPlus();
    });


    keyControlJ = new QShortcut(this);
    keyControlJ->setKey(Qt::CTRL + Qt::Key_J);
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
        f.Hz = roundFrequencyWithStep(freq.Hz, -1, tsKnobHz);
        f.MHzDouble = f.Hz / (double)1E6;
        freq.Hz = f.Hz;
        freq.MHzDouble = f.MHzDouble;
        setUIFreq();
        issueCmd(cmdSetFreq, f);
    });

    // L = Up
    keyL = new QShortcut(this);
    keyL->setKey(Qt::Key_L);
    connect(keyL, &QShortcut::activated,
            [=]() {
        if (freqLock) return;

        freqt f;
        f.Hz = roundFrequencyWithStep(freq.Hz, 1, tsKnobHz);
        f.MHzDouble = f.Hz / (double)1E6;
        ui->freqLabel->setText(QString("%1").arg(f.MHzDouble, 0, 'f'));
        freq.Hz = f.Hz;
        freq.MHzDouble = f.MHzDouble;
        setUIFreq();
        issueCmd(cmdSetFreq, f);
    });

    keyDebug = new QShortcut(this);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    keyDebug->setKey(Qt::CTRL + Qt::SHIFT + Qt::Key_D);
#else
    keyDebug->setKey(Qt::CTRL + Qt::Key_D);
#endif
    connect(keyDebug, SIGNAL(activated()), this, SLOT(on_debugBtn_clicked()));
}

void wfmain::setupUsbControllerDevice()
{
#if defined (USB_CONTROLLER)
    usbControllerDev = new usbController();
    usbControllerThread = new QThread(this);
    usbControllerDev->moveToThread(usbControllerThread);
    connect(usbControllerThread, SIGNAL(started()), usbControllerDev, SLOT(run()));
    connect(usbControllerThread, SIGNAL(finished()), usbControllerDev, SLOT(deleteLater()));
    connect(usbControllerDev, SIGNAL(sendJog(int)), this, SLOT(changeFrequency(int)));
    connect(usbControllerDev, SIGNAL(doShuttle(bool, unsigned char)), this, SLOT(doShuttle(bool, unsigned char)));
    connect(usbControllerDev, SIGNAL(button(const COMMAND*)), this, SLOT(buttonControl(const COMMAND*)));
    connect(usbControllerDev, SIGNAL(setBand(int)), this, SLOT(setBand(int)));
    connect(this, SIGNAL(controllerLed(bool, unsigned char)), usbControllerDev, SLOT(ledControl(bool, unsigned char)));
    connect(usbControllerDev, SIGNAL(newDevice(unsigned char, QVector<BUTTON>*, QVector<COMMAND>*)), shut, SLOT(newDevice(unsigned char, QVector<BUTTON>*, QVector<COMMAND>*)));
    usbControllerThread->start(QThread::LowestPriority);

    connect(this, SIGNAL(sendUsbControllerCommands(QVector<COMMAND>*)), usbControllerDev, SLOT(receiveCommands(QVector<COMMAND>*)));
    connect(this, SIGNAL(sendUsbControllerButtons(QVector<BUTTON>*)), usbControllerDev, SLOT(receiveButtons(QVector<BUTTON>*)));
#endif
}

void wfmain::pttToggle(bool status)
{
    // is it enabled?

    if (!ui->pttEnableChk->isChecked())
    {
        showStatusBarText("PTT is disabled, not sending command. Change under Settings tab.");
        return;
    }

    emit setPTT(status);
    emit controllerLed(status, 1);
    // Start 3 minute timer
    if (status)
        pttTimer->start();

    issueDelayedCommand(cmdGetPTT);
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

    if (cmd->type==normalCommand) {
        //qDebug() << "Other command";
        issueCmdUniquePriority((cmds)cmd->command, cmd->suffix);
    }
    else if (cmd->type == bandswitch)
    {
        //qDebug() << "Bandswitch";
        //issueCmd((cmds)cmd->command, cmd->band); // Needs fixing!
    }
    else if (cmd->type == modeswitch)
    {
        //qDebug() << "Bandswitch";
        changeMode(cmd->mode);
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
    f.Hz = roundFrequencyWithStep(freq.Hz, value, tsKnobHz);

    f.MHzDouble = f.Hz / (double)1E6;
    setUIFreq();
    issueCmdUniquePriority(cmdSetFreq, f);
    issueDelayedCommandUnique(cmdGetFreq);
}

void wfmain::setDefPrefs()
{
    defPrefs.useFullScreen = false;
    defPrefs.useSystemTheme = false;
    defPrefs.drawPeaks = true;
    defPrefs.currentColorPresetNumber = 0;
    defPrefs.underlayMode = underlayNone;
    defPrefs.underlayBufferSize = 64;
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
    defPrefs.wflength = 160;
    defPrefs.wftheme = static_cast<int>(QCPColorGradient::gpJet);
    defPrefs.plotFloor = 0;
    defPrefs.plotCeiling = 160;
    defPrefs.confirmExit = true;
    defPrefs.confirmPowerOff = true;
    defPrefs.meter2Type = meterNone;
    defPrefs.tcpPort = 0;
    defPrefs.waterfallFormat = 0;
    defPrefs.audioSystem = qtAudio;

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

    QString currentVersionString = QString(WFVIEW_VERSION);
    float currentVersionFloat = currentVersionString.toFloat();

    settings->beginGroup("Program");
    QString priorVersionString = settings->value("version", "unset").toString();
    float priorVersionFloat = priorVersionString.toFloat();
    if(currentVersionString != priorVersionString)
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
    prefs.wftheme = settings->value("WFTheme", defPrefs.wftheme).toInt();
    prefs.plotFloor = settings->value("plotFloor", defPrefs.plotFloor).toInt();
    prefs.plotCeiling = settings->value("plotCeiling", defPrefs.plotCeiling).toInt();
    plotFloor = prefs.plotFloor;
    plotCeiling = prefs.plotCeiling;
    wfFloor = prefs.plotFloor;
    wfCeiling = prefs.plotCeiling;
    prefs.drawPeaks = settings->value("DrawPeaks", defPrefs.drawPeaks).toBool();
    prefs.underlayBufferSize = settings->value("underlayBufferSize", defPrefs.underlayBufferSize).toInt();
    prefs.underlayMode = static_cast<underlay_t>(settings->value("underlayMode", defPrefs.underlayMode).toInt());
    prefs.wfAntiAlias = settings->value("WFAntiAlias", defPrefs.wfAntiAlias).toBool();
    prefs.wfInterpolate = settings->value("WFInterpolate", defPrefs.wfInterpolate).toBool();
    prefs.wflength = (unsigned int)settings->value("WFLength", defPrefs.wflength).toInt();
    prefs.stylesheetPath = settings->value("StylesheetPath", defPrefs.stylesheetPath).toString();
    ui->splitter->restoreState(settings->value("splitter").toByteArray());

    restoreGeometry(settings->value("windowGeometry").toByteArray());
    restoreState(settings->value("windowState").toByteArray());
    setWindowState(Qt::WindowActive); // Works around QT bug to returns window+keyboard focus.
    prefs.confirmExit = settings->value("ConfirmExit", defPrefs.confirmExit).toBool();
    prefs.confirmPowerOff = settings->value("ConfirmPowerOff", defPrefs.confirmPowerOff).toBool();
    prefs.meter2Type = static_cast<meterKind>(settings->value("Meter2Type", defPrefs.meter2Type).toInt());
    prefs.clickDragTuningEnable = settings->value("ClickDragTuning", false).toBool();
    ui->clickDragTuningEnableChk->setChecked(prefs.clickDragTuningEnable);
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
    if (prefs.radioCIVAddr != 0)
    {
        ui->rigCIVManualAddrChk->setChecked(true);
        ui->rigCIVaddrHexLine->blockSignals(true);
        ui->rigCIVaddrHexLine->setText(QString("%1").arg(prefs.radioCIVAddr, 2, 16));
        ui->rigCIVaddrHexLine->setEnabled(true);
        ui->rigCIVaddrHexLine->blockSignals(false);
    }
    else {
        ui->rigCIVManualAddrChk->setChecked(false);
        ui->rigCIVaddrHexLine->setEnabled(false);
    }
    prefs.CIVisRadioModel = (bool)settings->value("CIVisRadioModel", defPrefs.CIVisRadioModel).toBool();
    prefs.forceRTSasPTT = (bool)settings->value("ForceRTSasPTT", defPrefs.forceRTSasPTT).toBool();

    ui->useRTSforPTTchk->setChecked(prefs.forceRTSasPTT);

    prefs.serialPortRadio = settings->value("SerialPortRadio", defPrefs.serialPortRadio).toString();
    int serialIndex = ui->serialDeviceListCombo->findText(prefs.serialPortRadio);
    if (serialIndex != -1) {
        ui->serialDeviceListCombo->setCurrentIndex(serialIndex);
    }

    prefs.serialPortBaud = (quint32)settings->value("SerialPortBaud", defPrefs.serialPortBaud).toInt();
    ui->baudRateCombo->blockSignals(true);
    ui->baudRateCombo->setCurrentIndex(ui->baudRateCombo->findData(prefs.serialPortBaud));
    ui->baudRateCombo->blockSignals(false);

    if (prefs.serialPortBaud > 0)
    {
        serverConfig.baudRate = prefs.serialPortBaud;
    }

    prefs.polling_ms = settings->value("polling_ms", defPrefs.polling_ms).toInt();
    if(prefs.polling_ms == 0)
    {
        // Automatic
        ui->pollingButtonGroup->blockSignals(true);
        ui->autoPollBtn->setChecked(true);
        ui->manualPollBtn->setChecked(false);
        ui->pollingButtonGroup->blockSignals(false);
        ui->pollTimeMsSpin->setEnabled(false);
    } else {
        // Manual
        ui->pollingButtonGroup->blockSignals(true);
        ui->autoPollBtn->setChecked(false);
        ui->manualPollBtn->setChecked(true);
        ui->pollingButtonGroup->blockSignals(false);
        ui->pollTimeMsSpin->blockSignals(true);
        ui->pollTimeMsSpin->setValue(prefs.polling_ms);
        ui->pollTimeMsSpin->blockSignals(false);
        ui->pollTimeMsSpin->setEnabled(true);
    }

    prefs.virtualSerialPort = settings->value("VirtualSerialPort", defPrefs.virtualSerialPort).toString();
    int vspIndex = ui->vspCombo->findText(prefs.virtualSerialPort);
    if (vspIndex != -1) {
        ui->vspCombo->setCurrentIndex(vspIndex);
    }
    else
    {
        ui->vspCombo->addItem(prefs.virtualSerialPort);
        ui->vspCombo->setCurrentIndex(ui->vspCombo->count() - 1);
    }

    prefs.localAFgain = (unsigned char)settings->value("localAFgain", defPrefs.localAFgain).toUInt();
    rxSetup.localAFgain = prefs.localAFgain;
    txSetup.localAFgain = 255;

    prefs.audioSystem = static_cast<audioType>(settings->value("AudioSystem", defPrefs.audioSystem).toInt());
    ui->audioSystemCombo->blockSignals(true);
    ui->audioSystemServerCombo->blockSignals(true);
    ui->audioSystemCombo->setCurrentIndex(prefs.audioSystem);
    ui->audioSystemServerCombo->setCurrentIndex(prefs.audioSystem);
    ui->audioSystemServerCombo->blockSignals(false);
    ui->audioSystemCombo->blockSignals(false);


    settings->endGroup();

    // Misc. user settings (enable PTT, draw peaks, etc)
    settings->beginGroup("Controls");
    prefs.enablePTT = settings->value("EnablePTT", defPrefs.enablePTT).toBool();
    ui->pttEnableChk->setChecked(prefs.enablePTT);
    prefs.niceTS = settings->value("NiceTS", defPrefs.niceTS).toBool();
    prefs.automaticSidebandSwitching = settings->value("automaticSidebandSwitching", defPrefs.automaticSidebandSwitching).toBool();
    settings->endGroup();

    settings->beginGroup("LAN");

    prefs.enableLAN = settings->value("EnableLAN", defPrefs.enableLAN).toBool();

    // If LAN is enabled, server gets its audio straight from the LAN
    ui->serverRXAudioInputCombo->setEnabled(!prefs.enableLAN);
    ui->serverTXAudioOutputCombo->setEnabled(!prefs.enableLAN);
    ui->audioSystemServerCombo->setEnabled(!prefs.enableLAN);

    ui->baudRateCombo->setEnabled(!prefs.enableLAN);
    ui->serialDeviceListCombo->setEnabled(!prefs.enableLAN);

    ui->lanEnableBtn->setChecked(prefs.enableLAN);
    ui->connectBtn->setEnabled(true);

    prefs.enableRigCtlD = settings->value("EnableRigCtlD", defPrefs.enableRigCtlD).toBool();
    ui->enableRigctldChk->setChecked(prefs.enableRigCtlD);
    prefs.rigCtlPort = settings->value("RigCtlPort", defPrefs.rigCtlPort).toInt();
    ui->rigctldPortTxt->setText(QString("%1").arg(prefs.rigCtlPort));
    // Call the function to start rigctld if enabled.
    on_enableRigctldChk_clicked(prefs.enableRigCtlD);

    prefs.tcpPort = settings->value("TcpServerPort", defPrefs.tcpPort).toInt();
    ui->tcpServerPortTxt->setText(QString("%1").arg(prefs.tcpPort));

    prefs.waterfallFormat = settings->value("WaterfallFormat", defPrefs.waterfallFormat).toInt();
    ui->waterfallFormatCombo->blockSignals(true);
    ui->waterfallFormatCombo->setCurrentIndex(prefs.waterfallFormat);
    ui->waterfallFormatCombo->blockSignals(false);

    udpPrefs.ipAddress = settings->value("IPAddress", udpDefPrefs.ipAddress).toString();
    ui->ipAddressTxt->setEnabled(ui->lanEnableBtn->isChecked());
    ui->ipAddressTxt->setText(udpPrefs.ipAddress);

    udpPrefs.controlLANPort = settings->value("ControlLANPort", udpDefPrefs.controlLANPort).toInt();
    ui->controlPortTxt->setEnabled(ui->lanEnableBtn->isChecked());
    ui->controlPortTxt->setText(QString("%1").arg(udpPrefs.controlLANPort));

    udpPrefs.username = settings->value("Username", udpDefPrefs.username).toString();
    ui->usernameTxt->setEnabled(ui->lanEnableBtn->isChecked());
    ui->usernameTxt->setText(QString("%1").arg(udpPrefs.username));

    udpPrefs.password = settings->value("Password", udpDefPrefs.password).toString();
    ui->passwordTxt->setEnabled(ui->lanEnableBtn->isChecked());
    ui->passwordTxt->setText(QString("%1").arg(udpPrefs.password));

    rxSetup.isinput = false;
    txSetup.isinput = true;

    rxSetup.latency = settings->value("AudioRXLatency", "150").toInt();
    ui->rxLatencySlider->setEnabled(ui->lanEnableBtn->isChecked());
    ui->rxLatencySlider->setValue(rxSetup.latency);
    ui->rxLatencySlider->setTracking(false); // Stop it sending value on every change.

    txSetup.latency = settings->value("AudioTXLatency", "150").toInt();
    ui->txLatencySlider->setEnabled(ui->lanEnableBtn->isChecked());
    ui->txLatencySlider->setValue(txSetup.latency);
    ui->txLatencySlider->setTracking(false); // Stop it sending value on every change.

    ui->audioSampleRateCombo->blockSignals(true);
    rxSetup.sampleRate=settings->value("AudioRXSampleRate", "48000").toInt();
    txSetup.sampleRate=rxSetup.sampleRate;

    ui->audioSampleRateCombo->setEnabled(ui->lanEnableBtn->isChecked());
    int audioSampleRateIndex = ui->audioSampleRateCombo->findText(QString::number(rxSetup.sampleRate));
    if (audioSampleRateIndex != -1) {
        ui->audioSampleRateCombo->setCurrentIndex(audioSampleRateIndex);
    }
    ui->audioSampleRateCombo->blockSignals(false);

    // Add codec combobox items here so that we can add userdata!
    ui->audioRXCodecCombo->addItem("LPCM 1ch 16bit", 4);
    ui->audioRXCodecCombo->addItem("LPCM 1ch 8bit", 2);
    ui->audioRXCodecCombo->addItem("uLaw 1ch 8bit", 1);
    ui->audioRXCodecCombo->addItem("LPCM 2ch 16bit", 16);
    ui->audioRXCodecCombo->addItem("uLaw 2ch 8bit", 32);
    ui->audioRXCodecCombo->addItem("PCM 2ch 8bit", 8);
    ui->audioRXCodecCombo->addItem("Opus 1ch", 64);
    ui->audioRXCodecCombo->addItem("Opus 2ch", 128);

    ui->audioRXCodecCombo->blockSignals(true);
    rxSetup.codec = settings->value("AudioRXCodec", "4").toInt();
    ui->audioRXCodecCombo->setEnabled(ui->lanEnableBtn->isChecked());
    for (int f = 0; f < ui->audioRXCodecCombo->count(); f++)
        if (ui->audioRXCodecCombo->itemData(f).toInt() == rxSetup.codec)
            ui->audioRXCodecCombo->setCurrentIndex(f);
    ui->audioRXCodecCombo->blockSignals(false);

    ui->audioTXCodecCombo->addItem("LPCM 1ch 16bit", 4);
    ui->audioTXCodecCombo->addItem("LPCM 1ch 8bit", 2);
    ui->audioTXCodecCombo->addItem("uLaw 1ch 8bit", 1);
    ui->audioTXCodecCombo->addItem("Opus 1ch", 64);

    ui->audioRXCodecCombo->blockSignals(true);
    txSetup.codec = settings->value("AudioTXCodec", "4").toInt();
    ui->audioTXCodecCombo->setEnabled(ui->lanEnableBtn->isChecked());
    for (int f = 0; f < ui->audioTXCodecCombo->count(); f++)
        if (ui->audioTXCodecCombo->itemData(f).toInt() == txSetup.codec)
            ui->audioTXCodecCombo->setCurrentIndex(f);
    ui->audioRXCodecCombo->blockSignals(false);

    rxSetup.name = settings->value("AudioOutput", "Default Output Device").toString();
    qInfo(logGui()) << "Got Audio Output from Settings: " << rxSetup.name;

    txSetup.name = settings->value("AudioInput", "Default Input Device").toString();
    qInfo(logGui()) << "Got Audio Input from Settings: " << txSetup.name;


    rxSetup.resampleQuality = settings->value("ResampleQuality", "4").toInt();
    txSetup.resampleQuality = rxSetup.resampleQuality;

    udpPrefs.clientName = settings->value("ClientName", udpDefPrefs.clientName).toString();

    udpPrefs.halfDuplex = settings->value("HalfDuplex", udpDefPrefs.halfDuplex).toBool();

    settings->endGroup();

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


    ui->serverEnableCheckbox->setChecked(serverConfig.enabled);
    ui->serverControlPortText->setText(QString::number(serverConfig.controlPort));
    ui->serverCivPortText->setText(QString::number(serverConfig.civPort));
    ui->serverAudioPortText->setText(QString::number(serverConfig.audioPort));

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

    int row = 0;
    ui->serverUsersTable->setRowCount(0);

    QList<SERVERUSER>::iterator user = serverConfig.users.begin();

    while (user != serverConfig.users.end())
    {
        if (user->username != "" && user->password != "")
        {
            serverAddUserLine(user->username, user->password, user->userType);
            row++;
            user++;
        }
        else {
            user = serverConfig.users.erase(user);
        }
    }

    if (row == 0) {
        serverAddUserLine("", "", 0);
        SERVERUSER user;
        user.username = "";
        user.password = "";
        user.userType = 0;
        serverConfig.users.append(user);

        ui->serverAddUserBtn->setEnabled(false);
    }

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
            mem.setPreset(chan, freq, (mode_kind)mode);
        }
    }

    settings->endArray();
    settings->endGroup();

    settings->beginGroup("Cluster");
    
    prefs.clusterUdpEnable = settings->value("UdpEnabled", false).toBool();
    prefs.clusterTcpEnable = settings->value("TcpEnabled", false).toBool();
    prefs.clusterUdpPort = settings->value("UdpPort", 12060).toInt();
    ui->clusterUdpPortLineEdit->setText(QString::number(prefs.clusterUdpPort));
    ui->clusterUdpEnable->setChecked(prefs.clusterUdpEnable);
    ui->clusterTcpEnable->setChecked(prefs.clusterTcpEnable);

    int numClusters = settings->beginReadArray("Servers");
    clusters.clear();
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
                    clusters.append(c);
                }
            }
            int defaultCluster = 0;
            ui->clusterServerNameCombo->blockSignals(true);
            for (int f = 0; f < clusters.size(); f++)
            {
                ui->clusterServerNameCombo->addItem(clusters[f].server);
                if (clusters[f].isdefault) {
                    defaultCluster = f;
                }
            }
            ui->clusterServerNameCombo->blockSignals(false);

            if (clusters.size() > defaultCluster)
            {
                ui->clusterServerNameCombo->setCurrentIndex(defaultCluster);
                ui->clusterTcpPortLineEdit->blockSignals(true);
                ui->clusterUsernameLineEdit->blockSignals(true);
                ui->clusterPasswordLineEdit->blockSignals(true);
                ui->clusterTimeoutLineEdit->blockSignals(true);
                ui->clusterTcpPortLineEdit->setText(QString::number(clusters[defaultCluster].port));
                ui->clusterUsernameLineEdit->setText(clusters[defaultCluster].userName);
                ui->clusterPasswordLineEdit->setText(clusters[defaultCluster].password);
                ui->clusterTimeoutLineEdit->setText(QString::number(clusters[defaultCluster].timeout));
                ui->clusterTcpPortLineEdit->blockSignals(false);
                ui->clusterUsernameLineEdit->blockSignals(false);
                ui->clusterPasswordLineEdit->blockSignals(false);
                ui->clusterTimeoutLineEdit->blockSignals(false);
            }
        }
    }
    else {
        ui->clusterTcpPortLineEdit->setEnabled(false);
        ui->clusterUsernameLineEdit->setEnabled(false);
        ui->clusterPasswordLineEdit->setEnabled(false);
        ui->clusterTimeoutLineEdit->setEnabled(false);
    }
    settings->endArray();
    settings->endGroup();

    // CW Memory Load:
    settings->beginGroup("Keyer");
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
    /* Load USB buttons*/
    settings->beginGroup("USB");
    int numCommands = settings->beginReadArray("Commands");
    if (numCommands == 0) {
        settings->endArray();
        // We have no buttons so create defaults
        usbCommands.clear();
        usbCommands.append(COMMAND(0, "None", cmdNone, 0x0));
        usbCommands.append(COMMAND(1, "PTT On", cmdSetPTT, 0x1));
        usbCommands.append(COMMAND(2, "PTT Off", cmdSetPTT, 0x0));
        usbCommands.append(COMMAND(3, "PTT Toggle", cmdPTTToggle, 0x0));
        usbCommands.append(COMMAND(4, "Tune", cmdNone, 0x0));
        usbCommands.append(COMMAND(5, "Step+", cmdNone, 0x0));
        usbCommands.append(COMMAND(6, "Step-", cmdNone, 0x0));
        usbCommands.append(COMMAND(7, "Mode+", cmdNone, 0x0));
        usbCommands.append(COMMAND(8, "Mode-", cmdNone, 0x0));
        usbCommands.append(COMMAND(9, "Band+", cmdNone, 0x0));
        usbCommands.append(COMMAND(10, "Band-", cmdNone, 0x0));
        usbCommands.append(COMMAND(11, "NR", cmdNone, 0x0));
        usbCommands.append(COMMAND(12, "NB", cmdNone, 0x0));
        usbCommands.append(COMMAND(13, "AGC", cmdNone, 0x0));
        usbCommands.append(COMMAND(14, "NB", cmdNone, 0x0));
        usbCommands.append(COMMAND(15, "23cm", cmdGetBandStackReg, band23cm));
        usbCommands.append(COMMAND(16, "70cm", cmdGetBandStackReg, band70cm));
        usbCommands.append(COMMAND(17, "2m", cmdGetBandStackReg, band2m));
        usbCommands.append(COMMAND(18, "AIR", cmdGetBandStackReg, bandAir));
        usbCommands.append(COMMAND(19, "WFM", cmdGetBandStackReg, bandWFM));
        usbCommands.append(COMMAND(20, "4m", cmdGetBandStackReg, band4m));
        usbCommands.append(COMMAND(21, "6m", cmdGetBandStackReg, band6m));
        usbCommands.append(COMMAND(22, "10m", cmdGetBandStackReg, band10m));
        usbCommands.append(COMMAND(23, "12m", cmdGetBandStackReg, band12m));
        usbCommands.append(COMMAND(24, "15m", cmdGetBandStackReg, band15m));
        usbCommands.append(COMMAND(25, "17m", cmdGetBandStackReg, band17m));
        usbCommands.append(COMMAND(26, "20m", cmdGetBandStackReg, band20m));
        usbCommands.append(COMMAND(27, "30m", cmdGetBandStackReg, band30m));
        usbCommands.append(COMMAND(28, "40m", cmdGetBandStackReg, band40m));
        usbCommands.append(COMMAND(29, "60m", cmdGetBandStackReg, band60m));
        usbCommands.append(COMMAND(30, "80m", cmdGetBandStackReg, band80m));
        usbCommands.append(COMMAND(31, "160m", cmdGetBandStackReg, band160m));
        usbCommands.append(COMMAND(32, "630m", cmdGetBandStackReg, band630m));
        usbCommands.append(COMMAND(33, "2200m", cmdGetBandStackReg, band2200m));
        usbCommands.append(COMMAND(34, "GEN", cmdGetBandStackReg, bandGen));
        usbCommands.append(COMMAND(35, "Mode LSB", cmdSetMode, modeLSB));
        usbCommands.append(COMMAND(36, "Mode USB", cmdSetMode, modeUSB));
        usbCommands.append(COMMAND(37, "Mode CW", cmdSetMode, modeCW));
        usbCommands.append(COMMAND(38, "Mode FM", cmdSetMode, modeFM));


        /*
            modeLSB = 0x00,
            modeUSB = 0x01,
            modeAM = 0x02,
            modeCW = 0x03,
            modeRTTY = 0x04,
            modeFM = 0x05,
            modeCW_R = 0x07,
            modeRTTY_R = 0x08,
            modeLSB_D = 0x80,
            modeUSB_D = 0x81,
            modeDV = 0x17,
            modeDD = 0x27,
            modeWFM,
            modeS_AMD,
            modeS_AML,
            modeS_AMU,
            modeP25,
            modedPMR,
            modeNXDN_VN,
            modeNXDN_N,
            modeDCR,
            modePSK,
            modePSK_R
            */
    }
    else {
        for (int nc = 0; nc < numCommands; nc++)
        {
            settings->setArrayIndex(nc);
            COMMAND comm;
            comm.index = settings->value("Num", 0).toInt();
            comm.text = settings->value("Text", "").toString();
            comm.command = settings->value("Command", 0).toInt();
            //comm.band = (bandType)settings->value("Band", 0).toInt(); // Needs fixing!
            usbCommands.append(comm);
        }
        settings->endArray();
    }

    int numButtons = settings->beginReadArray("Buttons");
    if (numButtons == 0) {
        settings->endArray();
        // We have no buttons so create defaults
        usbButtons.clear();

        // ShuttleXpress
        usbButtons.append(BUTTON(1, 0, QRect(60, 66, 40, 30), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(1, 1, QRect(114, 50, 40, 30), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(1, 2, QRect(169, 47, 40, 30), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(1, 3, QRect(225, 59, 40, 30), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(1, 4, QRect(41, 132, 40, 30), Qt::red, &usbCommands[0], &usbCommands[0]));

        // ShuttlePro2
        usbButtons.append(BUTTON(2, 0, QRect(60, 66, 40, 30), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(2, 1, QRect(114, 50, 40, 30), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(2, 2, QRect(169, 47, 40, 30), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(2, 3, QRect(225, 59, 40, 30), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(2, 4, QRect(41, 132, 40, 30), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(2, 5, QRect(91, 105, 40, 30), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(2, 6, QRect(144, 93, 40, 30), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(2, 7, QRect(204, 99, 40, 30), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(2, 8, QRect(253, 124, 40, 30), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(2, 9, QRect(50, 270, 70, 55), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(2, 10, QRect(210, 270, 70, 55), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(2, 11, QRect(50, 335, 70, 55), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(2, 12, QRect(210, 335, 70, 55), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(2, 13, QRect(30, 195, 25, 80), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(2, 14, QRect(280, 195, 25, 80), Qt::red, &usbCommands[0], &usbCommands[0]));

        // RC28 
        usbButtons.append(BUTTON(3, 0, QRect(52, 445, 238, 64), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(3, 1, QRect(52, 373, 98, 46), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(3, 2, QRect(193, 373, 98, 46), Qt::red, &usbCommands[0], &usbCommands[0]));

        // Xbox Gamepad
        usbButtons.append(BUTTON(4, "UP", QRect(256, 229, 50, 50), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(4, "DOWN", QRect(256, 316, 50, 50), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(4, "LEFT", QRect(203, 273, 50, 50), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(4, "RIGHT", QRect(303, 273, 50, 50), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(4, "SELECT", QRect(302, 160, 40, 40), Qt::red, &usbCommands[0], &usbCommands[0])); 
        usbButtons.append(BUTTON(4, "START", QRect(412, 163, 40, 40), Qt::red, &usbCommands[0], &usbCommands[0])); 
        usbButtons.append(BUTTON(4, "Y", QRect(534, 104, 53, 53), Qt::red, &usbCommands[0], &usbCommands[0])); 
        usbButtons.append(BUTTON(4, "X", QRect(485, 152, 53, 53), Qt::red, &usbCommands[0], &usbCommands[0])); 
        usbButtons.append(BUTTON(4, "B", QRect(590, 152, 53, 53), Qt::red, &usbCommands[0], &usbCommands[0])); 
        usbButtons.append(BUTTON(4, "A", QRect(534, 202, 53, 53), Qt::red, &usbCommands[0], &usbCommands[0])); 
        usbButtons.append(BUTTON(4, "L1", QRect(123, 40, 70, 45), Qt::red, &usbCommands[0], &usbCommands[0])); 
        usbButtons.append(BUTTON(4, "R1", QRect(562, 40, 70, 45), Qt::red, &usbCommands[0], &usbCommands[0])); 
        usbButtons.append(BUTTON(4, "LEFTX", QRect(143, 119, 83, 35), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(4, "LEFTY", QRect(162, 132, 50, 57), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(4, "RIGHTX", QRect(430, 298, 83, 35), Qt::red, &usbCommands[0], &usbCommands[0]));
        usbButtons.append(BUTTON(4, "RIGHTY", QRect(453, 233, 50, 57), Qt::red, &usbCommands[0], &usbCommands[0]));
        //usbButtons.append(BUTTON(4, 14, QRect(280, 195, 25, 80), Qt::red, &usbCommands[0], &usbCommands[0]));

    }
    else {
        usbButtons.clear();
        for (int nb = 0; nb < numButtons; nb++)
        {
            settings->setArrayIndex(nb);
            BUTTON butt;
            butt.dev = settings->value("Dev", 0).toInt();
            butt.num = settings->value("Num", 0).toInt();
            butt.name = settings->value("Name", "").toString();
            butt.pos = QRect(settings->value("Left", 0).toInt(),
                settings->value("Top", 0).toInt(),
                settings->value("Width", 0).toInt(),
                settings->value("Height", 0).toInt());
            butt.textColour = QColor((settings->value("Colour", "Green").toString()));

            QString on = settings->value("OnCommand", "None").toString();
            QString off = settings->value("OffCommand", "None").toString();

            QVector<COMMAND>::iterator usbc = usbCommands.begin();

            while (usbc != usbCommands.end())
            {
                if (on == usbc->text)
                    butt.onCommand = usbc;
                if (off == usbc->text)
                    butt.offCommand = usbc;
                ++usbc;
            }
            usbButtons.append(butt);

        }
        settings->endArray();
    }

    settings->endGroup();
#endif
}

void wfmain::serverAddUserLine(const QString& user, const QString& pass, const int& type)
{
    ui->serverUsersTable->blockSignals(true);

    ui->serverUsersTable->insertRow(ui->serverUsersTable->rowCount());

    ui->serverUsersTable->setItem(ui->serverUsersTable->rowCount() - 1, 0, new QTableWidgetItem());
    ui->serverUsersTable->setItem(ui->serverUsersTable->rowCount() - 1, 1, new QTableWidgetItem());
    ui->serverUsersTable->setItem(ui->serverUsersTable->rowCount() - 1, 2, new QTableWidgetItem());
    ui->serverUsersTable->setItem(ui->serverUsersTable->rowCount() - 1, 3, new QTableWidgetItem());



    QLineEdit* username = new QLineEdit();
    username->setProperty("row", (int)ui->serverUsersTable->rowCount() - 1);
    username->setProperty("col", (int)0);
    username->setText(user);
    connect(username, SIGNAL(editingFinished()), this, SLOT(onServerUserFieldChanged()));
    ui->serverUsersTable->setCellWidget(ui->serverUsersTable->rowCount() - 1, 0, username);

    QLineEdit* password = new QLineEdit();
    password->setProperty("row", (int)ui->serverUsersTable->rowCount() - 1);
    password->setProperty("col", (int)1);
    password->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    password->setText(pass);
    connect(password, SIGNAL(editingFinished()), this, SLOT(onServerUserFieldChanged()));
    ui->serverUsersTable->setCellWidget(ui->serverUsersTable->rowCount() - 1, 1, password);

    QComboBox* comboBox = new QComboBox();
    comboBox->insertItems(0, { "Full User","Full with no TX","Monitor only" });
    comboBox->setProperty("row", (int)ui->serverUsersTable->rowCount() - 1);
    comboBox->setProperty("col", (int)2);
    comboBox->setCurrentIndex(type);
    connect(comboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onServerUserFieldChanged()));
    ui->serverUsersTable->setCellWidget(ui->serverUsersTable->rowCount() - 1, 2, comboBox);

    QPushButton* button = new QPushButton();
    button->setText("Delete");
    button->setProperty("row", (int)ui->serverUsersTable->rowCount() - 1);
    button->setProperty("col", (int)3);
    connect(button, SIGNAL(clicked()), this, SLOT(onServerUserFieldChanged()));
    ui->serverUsersTable->setCellWidget(ui->serverUsersTable->rowCount() - 1, 3, button);

    ui->serverUsersTable->blockSignals(false);

}

void wfmain::onServerUserFieldChanged()
{

    int row = sender()->property("row").toInt();
    int col = sender()->property("col").toInt();
    qDebug() << "Server User field col" << col << "row" << row << "changed";

    // This is a new user line so add to serverUsersTable
    if (serverConfig.users.length() <= row)
    {
        qInfo() << "Something bad has happened, serverConfig.users is shorter than table!";
    }
    else
    {
        if (col == 0)
        {
            QLineEdit* username = (QLineEdit*)ui->serverUsersTable->cellWidget(row, 0);
            if (username->text() != serverConfig.users[row].username) {
                serverConfig.users[row].username = username->text();
            }
        }
        else if (col == 1)
        {
            QLineEdit* password = (QLineEdit*)ui->serverUsersTable->cellWidget(row, 1);
            QByteArray pass;
            passcode(password->text(), pass);
            if (QString(pass) != serverConfig.users[row].password) {
                serverConfig.users[row].password = pass;
            }
        }
        else if (col == 2)
        {
            QComboBox* comboBox = (QComboBox*)ui->serverUsersTable->cellWidget(row, 2);
            serverConfig.users[row].userType = comboBox->currentIndex();
        }
        else if (col == 3)
        {          
            serverConfig.users.removeAt(row);
            ui->serverUsersTable->setRowCount(0);
            foreach(SERVERUSER user, serverConfig.users)
            {
                serverAddUserLine(user.username, user.password, user.userType);
            }
        }
        if (row == ui->serverUsersTable->rowCount() - 1) {
            ui->serverAddUserBtn->setEnabled(true);
        }

    }
}

void wfmain::on_serverAddUserBtn_clicked()
{
    serverAddUserLine("", "", 0);
    SERVERUSER user;
    user.username = "";
    user.password = "";
    user.userType = 0;
    serverConfig.users.append(user);

    ui->serverAddUserBtn->setEnabled(false);
}


void wfmain::on_serverEnableCheckbox_clicked(bool checked)
{
    ui->serverSetupGroup->setEnabled(checked);
    serverConfig.enabled = checked;
    setServerToPrefs();
}

void wfmain::on_serverControlPortText_textChanged(QString text)
{
    serverConfig.controlPort = text.toInt();
}

void wfmain::on_serverCivPortText_textChanged(QString text)
{
    serverConfig.civPort = text.toInt();
}

void wfmain::on_serverAudioPortText_textChanged(QString text)
{
    serverConfig.audioPort = text.toInt();
}

void wfmain::on_serverRXAudioInputCombo_currentIndexChanged(int value)
{

    if (!serverConfig.rigs.isEmpty() && value>=0)
    {
        if (prefs.audioSystem == qtAudio) {
            serverConfig.rigs.first()->rxAudioSetup.port = audioDev->getInputDeviceInfo(value);
        }
        else {
            serverConfig.rigs.first()->rxAudioSetup.portInt = audioDev->getInputDeviceInt(value);
        }

        serverConfig.rigs.first()->rxAudioSetup.name = audioDev->getInputName(value);
    }

}

void wfmain::on_serverTXAudioOutputCombo_currentIndexChanged(int value)
{

    if (!serverConfig.rigs.isEmpty() && value>=0)
    {
        if (prefs.audioSystem == qtAudio) {
            serverConfig.rigs.first()->txAudioSetup.port = audioDev->getOutputDeviceInfo(value);
        }
        else {
            serverConfig.rigs.first()->txAudioSetup.portInt = audioDev->getOutputDeviceInt(value);
        }

        serverConfig.rigs.first()->txAudioSetup.name = audioDev->getOutputName(value);
    }


}



void wfmain::saveSettings()
{
    qInfo(logSystem()) << "Saving settings to " << settings->fileName();
    // Basic things to load:


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
    settings->setValue("DrawPeaks", prefs.drawPeaks);
    settings->setValue("underlayMode", prefs.underlayMode);
    settings->setValue("underlayBufferSize", prefs.underlayBufferSize);
    settings->setValue("WFAntiAlias", prefs.wfAntiAlias);
    settings->setValue("WFInterpolate", prefs.wfInterpolate);
    settings->setValue("WFTheme", prefs.wftheme);
    settings->setValue("plotFloor", prefs.plotFloor);
    settings->setValue("plotCeiling", prefs.plotCeiling);
    settings->setValue("StylesheetPath", prefs.stylesheetPath);
    settings->setValue("splitter", ui->splitter->saveState());
    settings->setValue("windowGeometry", saveGeometry());
    settings->setValue("windowState", saveState());
    settings->setValue("WFLength", prefs.wflength);
    settings->setValue("ConfirmExit", prefs.confirmExit);
    settings->setValue("ConfirmPowerOff", prefs.confirmPowerOff);
    settings->setValue("Meter2Type", (int)prefs.meter2Type);
    settings->setValue("ClickDragTuning", prefs.clickDragTuningEnable);

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
    settings->setValue("tcpServerPort", prefs.tcpPort);
    settings->setValue("IPAddress", udpPrefs.ipAddress);
    settings->setValue("ControlLANPort", udpPrefs.controlLANPort);
    settings->setValue("SerialLANPort", udpPrefs.serialLANPort);
    settings->setValue("AudioLANPort", udpPrefs.audioLANPort);
    settings->setValue("Username", udpPrefs.username);
    settings->setValue("Password", udpPrefs.password);
    settings->setValue("AudioRXLatency", rxSetup.latency);
    settings->setValue("AudioTXLatency", txSetup.latency);
    settings->setValue("AudioRXSampleRate", rxSetup.sampleRate);
    settings->setValue("AudioRXCodec", rxSetup.codec);
    settings->setValue("AudioTXSampleRate", txSetup.sampleRate);
    settings->setValue("AudioTXCodec", txSetup.codec);
    settings->setValue("AudioOutput", rxSetup.name);
    settings->setValue("AudioInput", txSetup.name);
    settings->setValue("ResampleQuality", rxSetup.resampleQuality);
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

    for (int f = 0; f < clusters.count(); f++)
    {
        settings->setArrayIndex(f);
        settings->setValue("ServerName", clusters[f].server);
        settings->setValue("UserName", clusters[f].userName);
        settings->setValue("Port", clusters[f].port);
        settings->setValue("Password", clusters[f].password);
        settings->setValue("Timeout", clusters[f].timeout);
        settings->setValue("Default", clusters[f].isdefault);
    }

    settings->endArray();
    settings->endGroup();

    settings->beginGroup("Keyer");
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
    // Store USB Controller

    settings->beginWriteArray("Buttons");
    for (int nb = 0; nb < usbButtons.count(); nb++)
    {
        settings->setArrayIndex(nb);
        settings->setValue("Dev", usbButtons[nb].dev);
        settings->setValue("Num", usbButtons[nb].num);
        settings->setValue("Name", usbButtons[nb].name);
        settings->setValue("Left", usbButtons[nb].pos.left());
        settings->setValue("Top", usbButtons[nb].pos.top());
        settings->setValue("Width", usbButtons[nb].pos.width());
        settings->setValue("Height", usbButtons[nb].pos.height());
        settings->setValue("Colour", usbButtons[nb].textColour.name());
        if (usbButtons[nb].onCommand != Q_NULLPTR)
            settings->setValue("OnCommand", usbButtons[nb].onCommand->text);
        if (usbButtons[nb].offCommand != Q_NULLPTR)
            settings->setValue("OffCommand", usbButtons[nb].offCommand->text);
    }

    settings->endArray();

    settings->endGroup();
#endif

    settings->sync(); // Automatic, not needed (supposedly)
}


void wfmain::showHideSpectrum(bool show)
{

    if(show)
    {
        wf->show();
        plot->show();
    } else {
        wf->hide();
        plot->hide();
    }

    // Controls:
    ui->spectrumGroupBox->setVisible(show);
    ui->spectrumModeCombo->setVisible(show);
    ui->scopeBWCombo->setVisible(show);
    ui->scopeEdgeCombo->setVisible(show);
    ui->scopeEnableWFBtn->setVisible(show);
    ui->scopeRefLevelSlider->setEnabled(show);
    ui->wfLengthSlider->setEnabled(show);
    ui->wfthemeCombo->setVisible(show);
    ui->toFixedBtn->setVisible(show);
    ui->customEdgeBtn->setVisible(show);
    ui->clearPeakBtn->setVisible(show);

    // And the labels:
    ui->specEdgeLabel->setVisible(show);
    ui->specModeLabel->setVisible(show);
    ui->specSpanLabel->setVisible(show);
    ui->specThemeLabel->setVisible(show);

    // And the layout for space:
    ui->specControlsHorizLayout->setEnabled(show);
    ui->splitter->setVisible(show);
    ui->plot->setVisible(show);
    ui->waterfall->setVisible(show);
    ui->spectrumGroupBox->setEnabled(show);

    // Window resize:
    updateSizes(ui->tabWidget->currentIndex());

}

void wfmain::prepareWf()
{
    prepareWf(160);
}

void wfmain::prepareWf(unsigned int wfLength)
{
    // All this code gets moved in from the constructor of wfmain.

    if(haveRigCaps)
    {
        showHideSpectrum(rigCaps.hasSpectrum);
        if(!rigCaps.hasSpectrum)
        {
            return;
        }
        // TODO: Lock the function that draws on the spectrum while we are updating.
        spectrumDrawLock = true;

        spectWidth = rigCaps.spectLenMax;
        wfLengthMax = 1024;

        this->wfLength = wfLength; // fixed for now, time-length of waterfall

        // Initialize before use!

        QByteArray empty((int)spectWidth, '\x01');
        spectrumPeaks = QByteArray( (int)spectWidth, '\x01' );

        if((unsigned int)wfimage.size() < wfLengthMax)
        {
            unsigned int i=0;
            unsigned int oldSize = wfimage.size();
            for(i=oldSize; i<(wfLengthMax); i++)
            {
                wfimage.append(empty);
            }
        } else {
            // Keep wfimage, do not trim, no performance impact.
            //wfimage.remove(wfLength, wfimage.size()-wfLength);
        }

        wfimage.squeeze();
        //colorMap->clearData();
        colorMap->data()->clear();

        colorMap->data()->setValueRange(QCPRange(0, wfLength-1));
        colorMap->data()->setKeyRange(QCPRange(0, spectWidth-1));
        colorMap->setDataRange(QCPRange(prefs.plotFloor, prefs.plotCeiling));
        colorMap->setGradient(static_cast<QCPColorGradient::GradientPreset>(ui->wfthemeCombo->currentData().toInt()));

        if(colorMapData == Q_NULLPTR)
        {
            colorMapData = new QCPColorMapData(spectWidth, wfLength, QCPRange(0, spectWidth-1), QCPRange(0, wfLength-1));
        } else {
            //delete colorMapData; // TODO: Figure out why it crashes if we delete first.
            colorMapData = new QCPColorMapData(spectWidth, wfLength, QCPRange(0, spectWidth-1), QCPRange(0, wfLength-1));
        }
        colorMap->setData(colorMapData);

        wf->yAxis->setRangeReversed(true);
        wf->xAxis->setVisible(false);

        spectrumDrawLock = false;
    } else {
        qInfo(logSystem()) << "Cannot prepare WF view without rigCaps. Waiting on this.";
        return;
    }

}


// Key shortcuts (hotkeys)

void wfmain::shortcutF11()
{
    if(onFullscreen)
    {
        this->showNormal();
        onFullscreen = false;
    } else {
        this->showFullScreen();
        onFullscreen = true;
    }
    ui->fullScreenChk->setChecked(onFullscreen);
}

void wfmain::shortcutF1()
{
    ui->tabWidget->setCurrentIndex(0);
}

void wfmain::shortcutF2()
{
    ui->tabWidget->setCurrentIndex(1);
}

void wfmain::shortcutF3()
{
    ui->tabWidget->setCurrentIndex(2);
    ui->freqMhzLineEdit->clear();
    ui->freqMhzLineEdit->setFocus();
}

void wfmain::shortcutF4()
{
    ui->tabWidget->setCurrentIndex(3);
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
    emit sayAll();
}

void wfmain::shortcutControlT()
{
    // Transmit
    qInfo(logSystem()) << "Activated Control-T shortcut";
    showStatusBarText(QString("Transmitting. Press Control-R to receive."));
    ui->pttOnBtn->click();
}

void wfmain::shortcutControlR()
{
    // Receive
    issueCmdUniquePriority(cmdSetPTT, false);
    pttTimer->stop();
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
    ui->tabWidget->setCurrentIndex(2);
    ui->freqMhzLineEdit->clear();
    ui->freqMhzLineEdit->setFocus();
}

void wfmain::shortcutSlash()
{
    // Cycle through available modes
    ui->modeSelectCombo->setCurrentIndex( (ui->modeSelectCombo->currentIndex()+1) % ui->modeSelectCombo->count() );
    on_modeSelectCombo_activated( ui->modeSelectCombo->currentIndex() );
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
}

quint64 wfmain::roundFrequency(quint64 frequency, unsigned int tsHz)
{
    quint64 rounded = 0;
    if(ui->tuningFloorZerosChk->isChecked())
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

    if(ui->tuningFloorZerosChk->isChecked())
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
    f.Hz = roundFrequencyWithStep(freq.Hz, -1, tsPlusHz);
    f.MHzDouble = f.Hz / (double)1E6;
    freq.Hz = f.Hz;
    freq.MHzDouble = f.MHzDouble;
    setUIFreq();
    //issueCmd(cmdSetFreq, f);
    issueCmdUniquePriority(cmdSetFreq, f);
    //issueDelayedCommandUnique(cmdGetFreq);
}

void wfmain::shortcutPlus()
{
    if (freqLock) return;

    freqt f;
    f.Hz = roundFrequencyWithStep(freq.Hz, 1, tsPlusHz);
    f.MHzDouble = f.Hz / (double)1E6;
    freq.Hz = f.Hz;
    freq.MHzDouble = f.MHzDouble;
    setUIFreq();
    //issueCmd(cmdSetFreq, f);
    issueCmdUniquePriority(cmdSetFreq, f);
    //issueDelayedCommandUnique(cmdGetFreq);
}

void wfmain::shortcutStepMinus()
{
    if (freqLock) return;

    freqt f;
    f.Hz = roundFrequencyWithStep(freq.Hz, -1, ui->tuningStepCombo->currentData().toInt());

    f.MHzDouble = f.Hz / (double)1E6;
    setUIFreq();
    issueCmd(cmdSetFreq, f);
    issueDelayedCommandUnique(cmdGetFreq);
}

void wfmain::shortcutStepPlus()
{
    if (freqLock) return;

    freqt f;
    f.Hz = roundFrequencyWithStep(freq.Hz, 1, ui->tuningStepCombo->currentData().toInt());

    f.MHzDouble = f.Hz / (double)1E6;
    setUIFreq();
    issueCmd(cmdSetFreq, f);
    issueDelayedCommandUnique(cmdGetFreq);
}

void wfmain::shortcutShiftMinus()
{
    if(freqLock) return;

    freqt f;
    f.Hz = roundFrequencyWithStep(freq.Hz, -1, tsPlusShiftHz);
    f.MHzDouble = f.Hz / (double)1E6;
    freq.Hz = f.Hz;
    freq.MHzDouble = f.MHzDouble;
    setUIFreq();
    //emit setFrequency(0,f);
    issueCmd(cmdSetFreq, f);
    //issueDelayedCommandUnique(cmdGetFreq);
}

void wfmain::shortcutShiftPlus()
{
    if(freqLock) return;

    freqt f;
    f.Hz = roundFrequencyWithStep(freq.Hz, 1, tsPlusShiftHz);
    f.MHzDouble = f.Hz / (double)1E6;
    freq.Hz = f.Hz;
    freq.MHzDouble = f.MHzDouble;
    setUIFreq();
    //emit setFrequency(0,f);
    issueCmd(cmdSetFreq, f);
    //issueDelayedCommandUnique(cmdGetFreq);
}

void wfmain::shortcutControlMinus()
{
    if(freqLock) return;

    freqt f;
    f.Hz = roundFrequencyWithStep(freq.Hz, -1, tsPlusControlHz);
    f.MHzDouble = f.Hz / (double)1E6;
    freq.Hz = f.Hz;
    freq.MHzDouble = f.MHzDouble;
    setUIFreq();
    issueCmd(cmdSetFreq, f);
    //issueDelayedCommandUnique(cmdGetFreq);
}

void wfmain::shortcutControlPlus()
{
    if(freqLock) return;

    freqt f;
    f.Hz = roundFrequencyWithStep(freq.Hz, 1, tsPlusControlHz);
    f.MHzDouble = f.Hz / (double)1E6;
    freq.Hz = f.Hz;
    freq.MHzDouble = f.MHzDouble;
    setUIFreq();
    issueCmd(cmdSetFreq, f);
    //issueDelayedCommandUnique(cmdGetFreq);
}

void wfmain::shortcutPageUp()
{
    if(freqLock) return;

    freqt f;
    f.Hz = freq.Hz + tsPageHz;
    f.MHzDouble = f.Hz / (double)1E6;
    freq.Hz = f.Hz;
    freq.MHzDouble = f.MHzDouble;
    setUIFreq();
    issueCmd(cmdSetFreq, f);
    //issueDelayedCommandUnique(cmdGetFreq);
}

void wfmain::shortcutPageDown()
{
    if(freqLock) return;

    freqt f;
    f.Hz = freq.Hz - tsPageHz;
    f.MHzDouble = f.Hz / (double)1E6;
    freq.Hz = f.Hz;
    freq.MHzDouble = f.MHzDouble;
    setUIFreq();
    issueCmd(cmdSetFreq, f);
    //issueDelayedCommandUnique(cmdGetFreq);
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

void wfmain::setUIFreq(double frequency)
{
    ui->freqLabel->setText(QString("%1").arg(frequency, 0, 'f'));
}

void wfmain::setUIFreq()
{
    // Call this function, without arguments, if you know that the
    // freqMhz variable is already set correctly.
    setUIFreq(freq.MHzDouble);
}

void wfmain:: getInitialRigState()
{
    // Initial list of queries to the radio.
    // These are made when the program starts up
    // and are used to adjust the UI to match the radio settings
    // the polling interval is set at 200ms. Faster is possible but slower
    // computers will glitch occasionally.

    issueDelayedCommand(cmdGetFreq);
    issueDelayedCommand(cmdGetMode);

    issueDelayedCommand(cmdNone);

    issueDelayedCommand(cmdGetFreq);
    issueDelayedCommand(cmdGetMode);

    // From left to right in the UI:
    if (rigCaps.hasTransmit) 
    {
        issueDelayedCommand(cmdGetDataMode);
        issueDelayedCommand(cmdGetModInput);
        issueDelayedCommand(cmdGetModDataInput);
    }
    issueDelayedCommand(cmdGetRxGain);
    issueDelayedCommand(cmdGetAfGain);
    issueDelayedCommand(cmdGetSql);
    
    if (rigCaps.hasTransmit) 
    {
        issueDelayedCommand(cmdGetTxPower);
        issueDelayedCommand(cmdGetCurrentModLevel); // level for currently selected mod sources
    }
    
    issueDelayedCommand(cmdGetSpectrumRefLevel);
    issueDelayedCommand(cmdGetDuplexMode);

    if(rigCaps.hasSpectrum)
    {
        issueDelayedCommand(cmdDispEnable);
        issueDelayedCommand(cmdSpecOn);
    }

    if (rigCaps.hasTransmit)
    {
        issueDelayedCommand(cmdGetModInput);
        issueDelayedCommand(cmdGetModDataInput);
    }

    if(rigCaps.hasCTCSS)
    {
        issueDelayedCommand(cmdGetTone);
        issueDelayedCommand(cmdGetTSQL);
    }
    if(rigCaps.hasDTCS)
    {
        issueDelayedCommand(cmdGetDTCS);
    }
    issueDelayedCommand(cmdGetRptAccessMode);

    if(rigCaps.hasAntennaSel)
    {
        issueDelayedCommand(cmdGetAntenna);
    }
    if(rigCaps.hasAttenuator)
    {
        issueDelayedCommand(cmdGetAttenuator);
    }
    if(rigCaps.hasPreamp)
    {
        issueDelayedCommand(cmdGetPreamp);
    }

    issueDelayedCommand(cmdGetRitEnabled);
    issueDelayedCommand(cmdGetRitValue);

    if(rigCaps.hasIFShift)
        issueDelayedCommand(cmdGetIFShift);
    if(rigCaps.hasTBPF)
    {
        issueDelayedCommand(cmdGetTPBFInner);
        issueDelayedCommand(cmdGetTPBFOuter);
    }

    if(rigCaps.hasSpectrum)
    {
        issueDelayedCommand(cmdGetSpectrumMode);
        issueDelayedCommand(cmdGetSpectrumSpan);
    }

    issueDelayedCommand(cmdNone);
    issueDelayedCommand(cmdStartRegularPolling);

    if(rigCaps.hasATU)
    {
        issueDelayedCommand(cmdGetATUStatus);
    }

    delayedCommand->start();
}

void wfmain::showStatusBarText(QString text)
{
    ui->statusBar->showMessage(text, 5000);
}

void wfmain::on_useSystemThemeChk_clicked(bool checked)
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
    ui->colorPresetCombo->setItemText(presetNumber, *(p->presetName));
}

void wfmain::doCmd(commandtype cmddata)
{
    cmds cmd = cmddata.cmd;
    std::shared_ptr<void> data = cmddata.data;

    // This switch is for commands with parameters.
    // the "default" for non-parameter commands is to call doCmd(cmd).
    switch (cmd)
    {
        case cmdSetFreq:
        {
            lastFreqCmdTime_ms = QDateTime::currentMSecsSinceEpoch();
            freqt f = (*std::static_pointer_cast<freqt>(data));
            emit setFrequency(f.VFO,f);
            break;
        }
        case cmdSetMode:
        {
            mode_info m = (*std::static_pointer_cast<mode_info>(data));
            emit setMode(m);
            break;
        }
        case cmdSelVFO:
        {
            vfo_t v = (*std::static_pointer_cast<vfo_t>(data));
            emit selectVFO(v);
            break;
        }
        case cmdSetTxPower:
        {
            unsigned char txpower = (*std::static_pointer_cast<unsigned char>(data));
            emit setTxPower(txpower);
            break;
        }
        case cmdSetMicGain:
        {
            unsigned char micgain = (*std::static_pointer_cast<unsigned char>(data));
            emit setTxPower(micgain);
            break;
        }
        case cmdSetRxRfGain:
        {
            unsigned char rfgain = (*std::static_pointer_cast<unsigned char>(data));
            emit setRfGain(rfgain);
            break;
        }
        case cmdSetModLevel:
        {
            unsigned char modlevel = (*std::static_pointer_cast<unsigned char>(data));
            rigInput currentIn;
            if(usingDataMode)
            {
                currentIn = currentModDataSrc;
            } else {
                currentIn = currentModSrc;
            }
            emit setModLevel(currentIn, modlevel);
            break;
        }
        case cmdSetAfGain:
        {
            unsigned char afgain = (*std::static_pointer_cast<unsigned char>(data));
            emit setAfGain(afgain);
            break;
        }
        case cmdSetSql:
        {
            unsigned char sqlLevel = (*std::static_pointer_cast<unsigned char>(data));
            emit setSql(sqlLevel);
            break;
        }
        case cmdSetIFShift:
        {
            unsigned char IFShiftLevel = (*std::static_pointer_cast<unsigned char>(data));
            emit setIFShift(IFShiftLevel);
            break;
        }
        case cmdSetTPBFInner:
        {
            unsigned char innterLevel = (*std::static_pointer_cast<unsigned char>(data));
            emit setTPBFInner(innterLevel);
            break;
        }
        case cmdSetTPBFOuter:
        {
            unsigned char outerLevel = (*std::static_pointer_cast<unsigned char>(data));
            emit setTPBFOuter(outerLevel);
            break;
        }
        case cmdSetTone:
        {
            rptrTone_t t = (*std::static_pointer_cast<rptrTone_t>(data));
            emit setTone(t);
            break;
        }
        case cmdSetTSQL:
        {
            rptrTone_t t = (*std::static_pointer_cast<rptrTone_t>(data));
            emit setTSQL(t);
            break;
        }
        case cmdSetRptAccessMode:
        {
            rptrAccessData_t rd = (*std::static_pointer_cast<rptrAccessData_t>(data));
            emit setRepeaterAccessMode(rd);
            break;
        }
        case cmdSetRptDuplexOffset:
        {
            freqt f = (*std::static_pointer_cast<freqt>(data));
            emit setRptDuplexOffset(f);
            break;
        }
        case cmdSetPTT:
        {
            bool pttrequest = (*std::static_pointer_cast<bool>(data));
            emit setPTT(pttrequest);
            emit controllerLed(pttrequest, 1);

            ui->meter2Widget->clearMeterOnPTTtoggle();
            if (pttrequest)
            {
                ui->meterSPoWidget->setMeterType(meterPower);
            }
            else {
                ui->meterSPoWidget->setMeterType(meterS);
            }
            break;
        }
        case cmdPTTToggle:
        {
            bool pttrequest = !amTransmitting;
            emit setPTT(pttrequest);
            emit controllerLed(pttrequest, 1);
            ui->meter2Widget->clearMeterOnPTTtoggle();
            if (pttrequest)
            {
                ui->meterSPoWidget->setMeterType(meterPower);
            }
            else {
                ui->meterSPoWidget->setMeterType(meterS);
            }
            break;
        }
        case cmdSendCW:
        {
            QString messageText = (*std::static_pointer_cast<QString>(data));
            emit sendCW(messageText);
            break;
        }
        case cmdSetBreakMode:
        {
            unsigned char bmode = (*std::static_pointer_cast<unsigned char>(data));
            emit setCWBreakMode(bmode);
            break;
        }
        case cmdSetKeySpeed:
        {
            unsigned char wpm = (*std::static_pointer_cast<unsigned char>(data));
            emit setKeySpeed(wpm);
            break;
        }
        case cmdSetCwPitch:
        {
            unsigned char pitch = (*std::static_pointer_cast<unsigned char>(data));
            emit setCwPitch(pitch);
            break;
        }
        case cmdSetATU:
        {
            bool atuOn = (*std::static_pointer_cast<bool>(data));
            emit setATU(atuOn);
            break;
        }
        case cmdSetUTCOffset:
        {
            timekind u = (*std::static_pointer_cast<timekind>(data));
            emit setUTCOffset(u);
            break;
        }
        case cmdSetTime:
        {
            timekind t = (*std::static_pointer_cast<timekind>(data));
            emit setTime(t);
            break;
        }
        case cmdSetDate:
        {
            datekind d = (*std::static_pointer_cast<datekind>(data));
            emit setDate(d);
            break;
        }
        case cmdGetBandStackReg:
        {
            char band = (*std::static_pointer_cast<char>(data));
            bandStkBand = rigCaps.bsr[(availableBands)band]; // 23cm Needs fixing
            bandStkRegCode = ui->bandStkPopdown->currentIndex() + 1;
            emit getBandStackReg(bandStkBand, bandStkRegCode);
            break;
        }
        case cmdSetPassband:
        {
            quint16 pass = (*std::static_pointer_cast<quint16>(data));
            emit setPassband(pass);
            break;
        }
        default:
            doCmd(cmd);
            break;
    }
}

void wfmain::doCmd(cmds cmd)
{
    // Use this function to take action upon a command.
    switch(cmd)
    {
        case cmdNone:
            //qInfo(logSystem()) << "NOOP";
            break;
        case cmdGetRigID:
            if(!haveRigCaps)
            {
                emit getRigID();
                issueDelayedCommand(cmdGetRigID);
            }
        break;
        case cmdGetRigCIV:
            // if(!know rig civ already)
            if(!haveRigCaps)
            {
                emit getRigCIV();
                issueDelayedCommand(cmdGetRigCIV); // This way, we stay here until we get an answer.
            }
            break;
        case cmdGetFreq:
            emit getFrequency();
            break;
        case cmdGetMode:
            emit getMode();
            break;
        case cmdVFOSwap:
            emit sendVFOSwap();
            break;
        case cmdVFOEqualAB:
            emit sendVFOEqualAB();
            break;
        case cmdVFOEqualMS:
            emit sendVFOEqualMS();
            break;
        case cmdGetDataMode:
            if(rigCaps.hasDataModes)
                emit getDataMode();
            break;
        case cmdSetModeFilter:
            emit setMode(setModeVal, setFilterVal);
            break;
        case cmdSetDataModeOff:
            emit setDataMode(false, (unsigned char)ui->modeFilterCombo->currentData().toInt());
            break;
        case cmdSetDataModeOn:
            emit setDataMode(true, (unsigned char)ui->modeFilterCombo->currentData().toInt());
            break;
        case cmdGetRitEnabled:
            emit getRitEnabled();
            break;
        case cmdGetRitValue:
            emit getRitValue();
            break;
        case cmdGetModInput:
            emit getModInput(false);
            break;
        case cmdGetModDataInput:
            emit getModInput(true);
            break;
        case cmdGetCurrentModLevel:
            // TODO: Add delay between these queries
            emit getModInputLevel(currentModSrc);
            emit getModInputLevel(currentModDataSrc);
            break;
        case cmdGetDuplexMode:
            emit getDuplexMode();
            break;
        case cmdGetRptDuplexOffset:
            emit getRptDuplexOffset();
            break;
        case cmdGetPassband:
            emit getPassband();
            break;
        case cmdGetCwPitch:
            emit getCwPitch();
            break;
        case cmdGetPskTone:
            emit getPskTone();
            break;
        case cmdGetRttyMark:
            emit getRttyMark();
            break;
        case cmdGetTone:
            emit getTone();
            break;
        case cmdGetTSQL:
            emit getTSQL();
            break;
        case cmdGetDTCS:
            emit getDTCS();
            break;
        case cmdGetRptAccessMode:
            emit getRptAccessMode();
            break;
        case cmdDispEnable:
            emit scopeDisplayEnable();
            break;
        case cmdDispDisable:
            emit scopeDisplayDisable();
            break;
        case cmdGetSpectrumMode:
            emit getScopeMode();
            break;
        case cmdGetSpectrumSpan:
            emit getScopeSpan();
            break;
        case cmdSpecOn:
            emit spectOutputEnable();
            break;
        case cmdSpecOff:
            emit spectOutputDisable();
            break;
        case cmdGetRxGain:
            emit getRfGain();
            break;
        case cmdGetAfGain:
            emit getAfGain();
            break;
        case cmdGetSql:
            emit getSql();
            break;
        case cmdGetIFShift:
            emit getIfShift();
            break;
        case cmdGetTPBFInner:
            emit getTPBFInner();
            break;
        case cmdGetTPBFOuter:
            emit getTPBFOuter();
            break;
        case cmdGetTxPower:
            emit getTxPower();
            break;
        case cmdGetMicGain:
            emit getMicGain();
            break;
        case cmdGetSpectrumRefLevel:
            emit getSpectrumRefLevel();
            break;
        case cmdGetATUStatus:
            if(rigCaps.hasATU)
                emit getATUStatus();
            break;
        case cmdStartATU:
            if(rigCaps.hasATU)
                emit startATU();
            break;
        case cmdGetAttenuator:
            emit getAttenuator();
            break;
        case cmdGetPreamp:
            emit getPreamp();
            break;
        case cmdGetAntenna:
            emit getAntenna();
            break;
        case cmdScopeCenterMode:
            emit setScopeMode(spectModeCenter);
            break;
        case cmdScopeFixedMode:
            emit setScopeMode(spectModeFixed);
            break;
        case cmdGetPTT:
            emit getPTT();
            break;
        case cmdGetTxRxMeter:
            if(amTransmitting)
                emit getMeters(meterPower);
            else
                emit getMeters(meterS);
            break;
        case cmdGetSMeter:
            if(!amTransmitting)
                emit getMeters(meterS);
            break;
        case cmdGetCenterMeter:
            if(!amTransmitting)
                emit getMeters(meterCenter);
            break;
        case cmdGetPowerMeter:
            if(amTransmitting)
                emit getMeters(meterPower);
            break;
        case cmdGetSWRMeter:
            if(amTransmitting)
                emit getMeters(meterSWR);
            break;
        case cmdGetIdMeter:
            emit getMeters(meterCurrent);
            break;
        case cmdGetVdMeter:
            emit getMeters(meterVoltage);
            break;
        case cmdGetALCMeter:
            if(amTransmitting)
                    emit getMeters(meterALC);
            break;
        case cmdGetCompMeter:
            if(amTransmitting)
                emit getMeters(meterComp);
            break;
        case cmdGetKeySpeed:
            emit getKeySpeed();
            break;
        case cmdGetBreakMode:
            emit getCWBreakMode();
            break;
        case cmdStopCW:
            emit stopCW();
            break;
        case cmdStartRegularPolling:
            runPeriodicCommands = true;
            break;
        case cmdStopRegularPolling:
            runPeriodicCommands = false;
            break;
        case cmdQueNormalSpeed:
            if(usingLAN)
            {
                delayedCommand->setInterval(delayedCmdIntervalLAN_ms);
            } else {
                delayedCommand->setInterval(delayedCmdIntervalSerial_ms);
            }
            break;
        default:
            qInfo(logSystem()) << __PRETTY_FUNCTION__ << "WARNING: Command fell through of type: " << (unsigned int)cmd;
            break;
    }
}


void wfmain::sendRadioCommandLoop()
{
    // Called by the periodicPollingTimer, see setInitialTiming()

    if(!(loopTickCounter % 2))
    {
        // if ther's a command waiting, run it.
        if(!delayedCmdQue.empty())
        {
            commandtype cmddata = delayedCmdQue.front();
            delayedCmdQue.pop_front();
            doCmd(cmddata);
        } else if(!(loopTickCounter % 10))
        {
            // pick from useful queries to make now and then
            if(haveRigCaps && !slowPollCmdQueue.empty())
            {

                int nCmds = (int)slowPollCmdQueue.size();
                cmds sCmd = slowPollCmdQueue[(slowCmdNum++)%nCmds];
                doCmd(sCmd);
            }
        } else if ((!rapidPollCmdQueue.empty()) && rapidPollCmdQueueEnabled)
        {
            int nrCmds = (int)rapidPollCmdQueue.size();
            cmds rCmd = rapidPollCmdQueue[(rapidCmdNum++)%nrCmds];
            doCmd(rCmd);
        }
    } else {
        // odd-number ticks:
        // s-meter or other metering
        if(haveRigCaps && !periodicCmdQueue.empty())
        {
            int nCmds = (int)periodicCmdQueue.size();
            cmds pcmd = periodicCmdQueue[ (pCmdNum++)%nCmds ];
            doCmd(pcmd);
        }
    }
    loopTickCounter++;
}

void wfmain::issueDelayedCommand(cmds cmd)
{
    // Append to end of command queue
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = NULL;
    delayedCmdQue.push_back(cmddata);
}

void wfmain::issueDelayedCommandPriority(cmds cmd)
{
    // Places the new command at the top of the queue
    // Use only when needed.
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = NULL;
    delayedCmdQue.push_front(cmddata);
}

void wfmain::issueDelayedCommandUnique(cmds cmd)
{
    // Use this function to insert commands where
    // multiple (redundant) commands don't make sense.

    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = NULL;

    delayedCmdQue.push_front(cmddata);
    delayedCmdQue.erase(std::remove_if(delayedCmdQue.begin() + 1, delayedCmdQue.end(), [cmd](const commandtype& c) {  return (c.cmd == cmd); }), delayedCmdQue.end());

    // The following is both expensive and not that great,
    // since it does not check if the arguments are the same.    
/*    bool found = false;



    for(unsigned int i=0; i < delayedCmdQue.size(); i++)
    {
        if(delayedCmdQue.at(i).cmd == cmd)
        {
            found = true;
            break;
        }
    }

    if(!found)
    {
        delayedCmdQue.push_front(cmddata);
    }

//    if( std::find(delayedCmdQue.begin(), delayedCmdQue.end(), cmddata ) == delayedCmdQue.end())
//    {
//        delayedCmdQue.push_front(cmddata);
//    }
*/

//    delayedCmdQue.push_front(cmddata);
}

void wfmain::issueCmd(cmds cmd, mode_info m)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<mode_info>(new mode_info(m));
    delayedCmdQue.push_back(cmddata);
}

void wfmain::issueCmd(cmds cmd, freqt f)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<freqt>(new freqt(f));
    delayedCmdQue.push_back(cmddata);
}

void wfmain::issueCmd(cmds cmd, vfo_t v)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<vfo_t>(new vfo_t(v));
    delayedCmdQue.push_back(cmddata);
}

void wfmain::issueCmd(cmds cmd, rptrTone_t v)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<rptrTone_t>(new rptrTone_t(v));
    delayedCmdQue.push_back(cmddata);
}

void wfmain::issueCmd(cmds cmd, rptrAccessData_t rd)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<rptrAccessData_t>(new rptrAccessData_t(rd));
    delayedCmdQue.push_back(cmddata);
}

void wfmain::issueCmd(cmds cmd, timekind t)
{
    qDebug(logSystem()) << "Issuing timekind command with data: " << t.hours << " hours, " << t.minutes << " minutes, " << t.isMinus << " isMinus";
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<timekind>(new timekind(t));
    delayedCmdQue.push_front(cmddata);
}

void wfmain::issueCmd(cmds cmd, datekind d)
{
    qDebug(logSystem()) << "Issuing datekind command with data: " << d.day << " day, " << d.month << " month, " << d.year << " year.";
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<datekind>(new datekind(d));
    delayedCmdQue.push_front(cmddata);
}

void wfmain::issueCmd(cmds cmd, int i)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<int>(new int(i));
    delayedCmdQue.push_back(cmddata);
}

void wfmain::issueCmd(cmds cmd, char c)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<char>(new char(c));
    delayedCmdQue.push_back(cmddata);
}

void wfmain::issueCmd(cmds cmd, bool b)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<bool>(new bool(b));
    delayedCmdQue.push_back(cmddata);
}

void wfmain::issueCmd(cmds cmd, unsigned char c)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<unsigned char>(new unsigned char(c));
    delayedCmdQue.push_back(cmddata);
}

void wfmain::issueCmd(cmds cmd, quint16 c)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<quint16>(new quint16(c));
    delayedCmdQue.push_back(cmddata);
}

void wfmain::issueCmd(cmds cmd, qint16 c)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<qint16>(new qint16(c));
    delayedCmdQue.push_back(cmddata);
}

void wfmain::issueCmd(cmds cmd, QString s)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<QString>(new QString(s));
    delayedCmdQue.push_back(cmddata);
}

void wfmain::issueCmdUniquePriority(cmds cmd, bool b)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<bool>(new bool(b));
    delayedCmdQue.push_front(cmddata);
    delayedCmdQue.erase(std::remove_if(delayedCmdQue.begin() + 1, delayedCmdQue.end(), [cmd](const commandtype& c) { return (c.cmd == cmd); }), delayedCmdQue.end());

    //removeSimilarCommand(cmd);
}

void wfmain::issueCmdUniquePriority(cmds cmd, unsigned char c)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<unsigned char>(new unsigned char(c));
    delayedCmdQue.push_front(cmddata);
    delayedCmdQue.erase(std::remove_if(delayedCmdQue.begin() + 1, delayedCmdQue.end(), [cmd](const commandtype& c) { return (c.cmd == cmd); }), delayedCmdQue.end());

    //removeSimilarCommand(cmd);
}

void wfmain::issueCmdUniquePriority(cmds cmd, char c)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<char>(new char(c));
    delayedCmdQue.push_front(cmddata);
    delayedCmdQue.erase(std::remove_if(delayedCmdQue.begin() + 1, delayedCmdQue.end(), [cmd](const commandtype& c) { return (c.cmd == cmd); }), delayedCmdQue.end());
    //removeSimilarCommand(cmd);
}

void wfmain::issueCmdUniquePriority(cmds cmd, freqt f)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<freqt>(new freqt(f));
    delayedCmdQue.push_front(cmddata);
    delayedCmdQue.erase(std::remove_if(delayedCmdQue.begin() + 1, delayedCmdQue.end(), [cmd](const commandtype& c) { return (c.cmd == cmd); }), delayedCmdQue.end());
    //removeSimilarCommand(cmd);
}

void wfmain::issueCmdUniquePriority(cmds cmd, quint16 c)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<quint16>(new quint16(c));
    delayedCmdQue.push_front(cmddata);
    delayedCmdQue.erase(std::remove_if(delayedCmdQue.begin() + 1, delayedCmdQue.end(), [cmd](const commandtype& c) { return (c.cmd == cmd); }), delayedCmdQue.end());
    //removeSimilarCommand(cmd);
}

void wfmain::issueCmdUniquePriority(cmds cmd, qint16 c)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<qint16>(new qint16(c));
    delayedCmdQue.push_front(cmddata);
    delayedCmdQue.erase(std::remove_if(delayedCmdQue.begin() + 1, delayedCmdQue.end(), [cmd](const commandtype& c) { return (c.cmd == cmd); }), delayedCmdQue.end());
    //removeSimilarCommand(cmd);
}

void wfmain::removeSimilarCommand(cmds cmd)
{
    // pop anything out that is of the same kind of command:
    // pop anything out that is of the same kind of command:
    // Start at 1 since we put one in at zero that we want to keep.

    for (auto it = delayedCmdQue.begin()+1; it != delayedCmdQue.end();)
    {
        if (it->cmd == cmd)
        {
            it = delayedCmdQue.erase(it);
        }
        else {
            it++;
        }
    }
    /*
    for(unsigned int i=1; i < delayedCmdQue.size(); i++)
    {
        if (delayedCmdQue.at(i).cmd == cmd)
        {
            //delayedCmdQue[i].cmd = cmdNone;
            delayedCmdQue.erase(delayedCmdQue.begin()+i);
            // i -= 1;
        }
    }
    */
}

void wfmain::receiveRigID(rigCapabilities rigCaps)
{
    // Note: We intentionally request rigID several times
    // because without rigID, we can't do anything with the waterfall.
    if(haveRigCaps)
    {
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
        this->spectWidth = rigCaps.spectLenMax; // used once haveRigCaps is true.
        //wfCeiling = rigCaps.spectAmpMax;
        //plotCeiling = rigCaps.spectAmpMax;
        if(rigCaps.hasSpectrum)
        {
            ui->topLevelSlider->setVisible(true);
            ui->labelTop->setVisible(true);
            ui->botLevelSlider->setVisible(true);
            ui->labelBot->setVisible(true);
            ui->scopeRefLevelSlider->setVisible(true);
            ui->refLabel->setVisible(true);
            ui->wfLengthSlider->setVisible(true);
            ui->lenLabel->setVisible(true);

            ui->topLevelSlider->setMaximum(rigCaps.spectAmpMax);
            ui->botLevelSlider->setMaximum(rigCaps.spectAmpMax);
        } else {
            ui->scopeRefLevelSlider->setVisible(false);
            ui->refLabel->setVisible(false);
            ui->wfLengthSlider->setVisible(false);
            ui->lenLabel->setVisible(false);
            ui->topLevelSlider->setVisible(false);
            ui->labelTop->setVisible(false);
            ui->botLevelSlider->setVisible(false);
            ui->labelBot->setVisible(false);
        }
        haveRigCaps = true;

        // Added so that server receives rig capabilities.
        emit sendRigCaps(rigCaps);
        rpt->setRig(rigCaps);
        trxadj->setRig(rigCaps);

        // Set the mode combo box up:

        ui->modeSelectCombo->blockSignals(true);
        ui->modeSelectCombo->clear();

        for(unsigned int i=0; i < rigCaps.modes.size(); i++)
        {
            ui->modeSelectCombo->addItem(rigCaps.modes.at(i).name,
                                            rigCaps.modes.at(i).reg);
        }

        ui->modeSelectCombo->blockSignals(false);

        if(rigCaps.model == model9700)
        {
            ui->satOpsBtn->setDisabled(false);
            ui->adjRefBtn->setDisabled(false);
        } else {
            ui->satOpsBtn->setDisabled(true);
            ui->adjRefBtn->setDisabled(true);
        }
        QString inName;
        // Clear input combos before adding known inputs.
        ui->modInputCombo->clear();
        ui->modInputDataCombo->clear();

        for(int i=0; i < rigCaps.inputs.length(); i++)
        {
            switch(rigCaps.inputs.at(i))
            {
                case inputMic:
                    inName = "Mic";
                    break;
                case inputLAN:
                    inName = "LAN";
                    break;
                case inputUSB:
                    inName = "USB";
                    break;
                case inputACC:
                    inName = "ACC";
                    break;
                case inputACCA:
                    inName = "ACCA";
                    break;
                case inputACCB:
                    inName = "ACCB";
                    break;
                default:
                    inName = "Unknown";
                    break;

            }
            ui->modInputCombo->addItem(inName, rigCaps.inputs.at(i));
            ui->modInputDataCombo->addItem(inName, rigCaps.inputs.at(i));
        }
        if(rigCaps.inputs.length() == 0)
        {
            ui->modInputCombo->addItem("None", inputNone);
            ui->modInputDataCombo->addItem("None", inputNone);
        }

        ui->attSelCombo->clear();
        if(rigCaps.hasAttenuator)
        {
            ui->attSelCombo->setDisabled(false);
            for(unsigned int i=0; i < rigCaps.attenuators.size(); i++)
            {
                inName = (i==0)?QString("0dB"):QString("-%1 dB").arg(rigCaps.attenuators.at(i), 0, 16);
                ui->attSelCombo->addItem(inName, rigCaps.attenuators.at(i));
            }
        } else {
            ui->attSelCombo->setDisabled(true);
        }

        ui->preampSelCombo->clear();
        if(rigCaps.hasPreamp)
        {
            ui->preampSelCombo->setDisabled(false);
            for(unsigned int i=0; i < rigCaps.preamps.size(); i++)
            {
                inName = (i==0)?QString("Disabled"):QString("Pre #%1").arg(rigCaps.preamps.at(i), 0, 16);
                ui->preampSelCombo->addItem(inName, rigCaps.preamps.at(i));
            }
        } else {
            ui->preampSelCombo->setDisabled(true);
        }

        ui->antennaSelCombo->clear();
        if(rigCaps.hasAntennaSel)
        {
            ui->antennaSelCombo->setDisabled(false);
            for(unsigned int i=0; i < rigCaps.antennas.size(); i++)
            {
                inName = QString("%1").arg(rigCaps.antennas.at(i)+1, 0, 16);		// adding 1 to have the combobox start with ant 1 instead of 0
                ui->antennaSelCombo->addItem(inName, rigCaps.antennas.at(i));
            }
        } else {
            ui->antennaSelCombo->setDisabled(true);
        }

        ui->rxAntennaCheck->setEnabled(rigCaps.hasRXAntenna);
        ui->rxAntennaCheck->setChecked(false);

        ui->scopeBWCombo->blockSignals(true);
        ui->scopeBWCombo->clear();
        if(rigCaps.hasSpectrum)
        {
            ui->scopeBWCombo->setHidden(false);
            for(unsigned int i=0; i < rigCaps.scopeCenterSpans.size(); i++)
            {
                ui->scopeBWCombo->addItem(rigCaps.scopeCenterSpans.at(i).name, (int)rigCaps.scopeCenterSpans.at(i).cstype);
            }
            plot->yAxis->setRange(QCPRange(prefs.plotFloor, prefs.plotCeiling));
            colorMap->setDataRange(QCPRange(prefs.plotFloor, prefs.plotCeiling));
        } else {
            ui->scopeBWCombo->setHidden(true);
        }
        ui->scopeBWCombo->blockSignals(false);

        setBandButtons();

        ui->tuneEnableChk->setEnabled(rigCaps.hasATU);
        ui->tuneNowBtn->setEnabled(rigCaps.hasATU);

        ui->useRTSforPTTchk->setChecked(prefs.forceRTSasPTT);

        ui->audioSystemCombo->setEnabled(false);
        ui->audioSystemServerCombo->setEnabled(false);

        ui->connectBtn->setText("Disconnect from Radio"); // We must be connected now.

        prepareWf(ui->wfLengthSlider->value());
        if(usingLAN)
        {
            ui->afGainSlider->setValue(prefs.localAFgain);
        }
        // Adding these here because clearly at this point we have valid
        // rig comms. In the future, we should establish comms and then
        // do all the initial grabs. For now, this hack of adding them here and there:
        issueDelayedCommand(cmdGetFreq);
        issueDelayedCommand(cmdGetMode);
        // recalculate command timing now that we know the rig better:
        if(prefs.polling_ms != 0)
        {
            changePollTiming(prefs.polling_ms, true);
        } else {
            calculateTimingParameters();
        }
        initPeriodicCommands();
        
        // Set the second meter here as I suspect we need to be connected for it to work?
        for (int i = 0; i < ui->meter2selectionCombo->count(); i++)
        {
            if (static_cast<meterKind>(ui->meter2selectionCombo->itemData(i).toInt()) == prefs.meter2Type)
            {
                // I thought that setCurrentIndex() would call the activated() function for the combobox
                // but it doesn't, so call it manually.
                ui->meter2selectionCombo->setCurrentIndex(i);
                on_meter2selectionCombo_activated(i); 
            }
        }
    }
    updateSizes(ui->tabWidget->currentIndex());
}

void wfmain::initPeriodicCommands()
{
    // This function places periodic polling commands into a queue.
    // The commands are run using a timer,
    // and the timer is started by the delayed command cmdStartPeriodicTimer.

    insertPeriodicCommand(cmdGetTxRxMeter, 128);

    insertSlowPeriodicCommand(cmdGetFreq, 128);
    insertSlowPeriodicCommand(cmdGetMode, 128);
    if(rigCaps.hasTransmit)
        insertSlowPeriodicCommand(cmdGetPTT, 128);
    insertSlowPeriodicCommand(cmdGetTxPower, 128);
    insertSlowPeriodicCommand(cmdGetRxGain, 128);
    if(rigCaps.hasAttenuator)
        insertSlowPeriodicCommand(cmdGetAttenuator, 128);
    if(rigCaps.hasTransmit)
        insertSlowPeriodicCommand(cmdGetPTT, 128);
    if(rigCaps.hasPreamp)
        insertSlowPeriodicCommand(cmdGetPreamp, 128);
    if (rigCaps.hasRXAntenna) {
        insertSlowPeriodicCommand(cmdGetAntenna, 128);
    }
    insertSlowPeriodicCommand(cmdGetDuplexMode, 128); // split and repeater
    if(rigCaps.hasRepeaterModes)
    {
        insertSlowPeriodicCommand(cmdGetRptDuplexOffset, 128);
    }

    rapidPollCmdQueueEnabled = false;
    rapidPollCmdQueue.clear();
    rapidPollCmdQueueEnabled = true;
}

void wfmain::insertPeriodicRapidCmd(cmds cmd)
{
    rapidPollCmdQueue.push_back(cmd);
}

void wfmain::insertPeriodicCommand(cmds cmd, unsigned char priority=100)
{
    // TODO: meaningful priority
    // These commands get run at the fastest pace possible
    // Typically just metering.
    if(priority < 10)
    {
        periodicCmdQueue.push_front(cmd);
    } else {
        periodicCmdQueue.push_back(cmd);
    }
}

void wfmain::insertPeriodicRapidCmdUnique(cmds cmd)
{
    removePeriodicRapidCmd(cmd);
    rapidPollCmdQueue.push_back(cmd);
}

void wfmain::insertPeriodicCommandUnique(cmds cmd)
{
    // Use this function to insert a non-duplicate command
    // into the fast periodic polling queue, typically
    // meter commands where high refresh rates are desirable.

    removePeriodicCommand(cmd);
    periodicCmdQueue.push_front(cmd);
}

void wfmain::removePeriodicRapidCmd(cmds cmd)
{

    qDebug() << "Removing" << cmd << "From periodic queue, len" << slowPollCmdQueue.size();
    periodicCmdQueue.erase(std::remove_if(periodicCmdQueue.begin(), periodicCmdQueue.end(), [cmd](const cmds& c) {  return (c == cmd); }), periodicCmdQueue.end());
    qDebug() << "Removed" << cmd << "From periodic queue, len" << slowPollCmdQueue.size();

    /*
    while(true)
    {
        auto it = std::find(this->rapidPollCmdQueue.begin(), this->rapidPollCmdQueue.end(), cmd);
        if(it != rapidPollCmdQueue.end())
        {
            rapidPollCmdQueue.erase(it);
        } else {
            break;
        }
    }
    */
}

void wfmain::removePeriodicCommand(cmds cmd)
{

    qDebug() << "Removing" << cmd << "From periodic queue, len" << slowPollCmdQueue.size();
    periodicCmdQueue.erase(std::remove_if(periodicCmdQueue.begin(), periodicCmdQueue.end(), [cmd](const cmds& c) {  return (c == cmd); }), periodicCmdQueue.end());
    qDebug() << "Removed" << cmd << "From periodic queue, len" << slowPollCmdQueue.size();

/*    while (true)
    {
        auto it = std::find(this->periodicCmdQueue.begin(), this->periodicCmdQueue.end(), cmd);
        if(it != periodicCmdQueue.end())
        {
            periodicCmdQueue.erase(it);
        } else {
            break;
        }
    }
    */
}


void wfmain::insertSlowPeriodicCommand(cmds cmd, unsigned char priority=100)
{
    // TODO: meaningful priority
    // These commands are run every 20 "ticks" of the primary radio command loop
    // Basically 20 times less often than the standard periodic command
    qDebug() << "Inserting" << cmd << "To slow queue, priority" << priority << "len" << slowPollCmdQueue.size();
    if(priority < 10)
    {
        slowPollCmdQueue.push_front(cmd);
    } else {
        slowPollCmdQueue.push_back(cmd);
    }
    qDebug() << "Inserted" << cmd << "To slow queue, priority" << priority << "len" << slowPollCmdQueue.size();
}

void wfmain::removeSlowPeriodicCommand(cmds cmd)
{
    qDebug() << "Removing" << cmd << "From slow queue, len" << slowPollCmdQueue.size();
    slowPollCmdQueue.erase(std::remove_if(slowPollCmdQueue.begin(), slowPollCmdQueue.end(), [cmd](const cmds& c) {  return (c == cmd); }), slowPollCmdQueue.end());
    qDebug() << "Removed" << cmd << "From slow queue, len" << slowPollCmdQueue.size();
}

void wfmain::receiveFreq(freqt freqStruct)
{

    qint64 tnow_ms = QDateTime::currentMSecsSinceEpoch();
    if(tnow_ms - lastFreqCmdTime_ms > delayedCommand->interval() * 2)
    {
        ui->freqLabel->setText(QString("%1").arg(freqStruct.MHzDouble, 0, 'f'));
        freq = freqStruct;
        rpt->handleUpdateCurrentMainFrequency(freqStruct);
    } else {
        qDebug(logSystem()) << "Rejecting stale frequency: " << freqStruct.Hz << " Hz, delta time ms = " << tnow_ms - lastFreqCmdTime_ms\
                            << ", tnow_ms " << tnow_ms << ", last: " << lastFreqCmdTime_ms;
    }
}

void wfmain::receivePTTstatus(bool pttOn)
{
    // This is the only place where amTransmitting and the transmit button text should be changed:
    //qInfo(logSystem()) << "PTT status: " << pttOn;
    if (pttOn && !amTransmitting)
    {
        pttLed->setState(QLedLabel::State::StateError);

    }
    else if (!pttOn && amTransmitting)
    {
        pttLed->setState(QLedLabel::State::StateOk);
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


void wfmain::receiveSpectrumData(QByteArray spectrum, double startFreq, double endFreq)
{
    if(!haveRigCaps)
    {
        qDebug(logSystem()) << "Spectrum received, but RigID incomplete.";
        return;
    }

    QElapsedTimer performanceTimer;
    bool updateRange = false;

    if((startFreq != oldLowerFreq) || (endFreq != oldUpperFreq))
    {
        // If the frequency changed and we were drawing peaks, now is the time to clearn them
        if(underlayMode == underlayPeakHold)
        {
            // TODO: create non-button function to do this
            // This will break if the button is ever moved or renamed.
            on_clearPeakBtn_clicked();
        } else {
            plasmaPrepared = false;
            preparePlasma();
        }
        // Inform other threads (cluster) that the frequency range has changed.
        emit setFrequencyRange(startFreq, endFreq);
    }

    oldLowerFreq = startFreq;
    oldUpperFreq = endFreq;

    //qInfo(logSystem()) << "start: " << startFreq << " end: " << endFreq;
    quint16 specLen = spectrum.length();
    //qInfo(logSystem()) << "Spectrum data received at UI! Length: " << specLen;
    //if( (specLen != 475) || (specLen!=689) )

    if( specLen != rigCaps.spectLenMax )
    {
        qDebug(logSystem()) << "-------------------------------------------";
        qDebug(logSystem()) << "------ Unusual spectrum received, length: " << specLen;
        qDebug(logSystem()) << "------ Expected spectrum length: " << rigCaps.spectLenMax;
        qDebug(logSystem()) << "------ This should happen once at most. ";
        return; // safe. Using these unusual length things is a problem.
    }

    QVector <double> x(spectWidth), y(spectWidth), y2(spectWidth);

    // TODO: Keep x around unless the frequency range changes. Should save a little time.
    for(int i=0; i < spectWidth; i++)
    {
        x[i] = (i * (endFreq-startFreq)/spectWidth) + startFreq;
    }

    for(int i=0; i<specLen; i++)
    {
        //x[i] = (i * (endFreq-startFreq)/specLen) + startFreq;
        y[i] = (unsigned char)spectrum.at(i);
        if(underlayMode == underlayPeakHold)
        {
            if((unsigned char)spectrum.at(i) > (unsigned char)spectrumPeaks.at(i))
            {
                spectrumPeaks[i] = spectrum.at(i);
            }
            y2[i] = (unsigned char)spectrumPeaks.at(i);
        }
    }
    plasmaMutex.lock();
    spectrumPlasma.push_front(spectrum);
    if(spectrumPlasma.size() > (int)spectrumPlasmaSize)
    {
        spectrumPlasma.pop_back();
    }
    plasmaMutex.unlock();


    if (!spectrumDrawLock)
    {
        if ((plotFloor != oldPlotFloor) || (plotCeiling != oldPlotCeiling)){
            updateRange = true;
        }
#if QCUSTOMPLOT_VERSION < 0x020000
        plot->graph(0)->setData(x, y);
#else
        plot->graph(0)->setData(x, y, true);
#endif

        if((freq.MHzDouble < endFreq) && (freq.MHzDouble > startFreq))
        {
            freqIndicatorLine->start->setCoords(freq.MHzDouble, 0);
            freqIndicatorLine->end->setCoords(freq.MHzDouble, rigCaps.spectAmpMax);

            double pbStart = 0.0;
            double pbEnd = 0.0;

            switch (currentModeInfo.mk)
            {
            case modeLSB:
            case modeRTTY:
            case modePSK_R:
                pbStart = freq.MHzDouble - passbandCenterFrequency - (passbandWidth / 2);
                pbEnd = freq.MHzDouble - passbandCenterFrequency + (passbandWidth / 2);
                break;
            case modeCW:
                if (passbandWidth < 0.0006) {
                    pbStart = freq.MHzDouble - (passbandWidth / 2);
                    pbEnd = freq.MHzDouble + (passbandWidth / 2);
                }
                else {
                    pbStart = freq.MHzDouble + passbandCenterFrequency - passbandWidth;
                    pbEnd = freq.MHzDouble + passbandCenterFrequency;
                }
                break;
            case modeCW_R:
                if (passbandWidth < 0.0006) {
                    pbStart = freq.MHzDouble - (passbandWidth / 2);
                    pbEnd = freq.MHzDouble + (passbandWidth / 2);
                }
                else {
                    pbStart = freq.MHzDouble - passbandCenterFrequency;
                    pbEnd = freq.MHzDouble + passbandWidth - passbandCenterFrequency;
                }
                break;
            default:
                pbStart = freq.MHzDouble + passbandCenterFrequency - (passbandWidth / 2);
                pbEnd = freq.MHzDouble + passbandCenterFrequency + (passbandWidth / 2);
                break;
            }

            passbandIndicator->topLeft->setCoords(pbStart, 0);
            passbandIndicator->bottomRight->setCoords(pbEnd, rigCaps.spectAmpMax);

            if ((currentModeInfo.mk == modeCW || currentModeInfo.mk == modeCW_R) && passbandWidth > 0.0006)
            {
                pbtDefault = round((passbandWidth - (cwPitch / 1000000.0)) * 200000.0) / 200000.0;
            }
            else 
            {
                pbtDefault = 0.0;
            }

            if ((TPBFInner - pbtDefault || TPBFOuter - pbtDefault) && passbandAction != passbandResizing && currentModeInfo.mk != modeFM)
            {
                pbtIndicator->setVisible(true);
            }
            else
            {
                pbtIndicator->setVisible(false);
            }

            /*
                pbtIndicator displays the intersection between TPBFInner and TPBFOuter
            */
            pbtIndicator->topLeft->setCoords(qMax(pbStart + (TPBFInner / 2) - (pbtDefault /2), pbStart + (TPBFOuter / 2) - (pbtDefault /2)), 0);

            pbtIndicator->bottomRight->setCoords(qMin(pbStart + (TPBFInner / 2) - (pbtDefault /2) + passbandWidth,
                pbStart + (TPBFOuter / 2) - (pbtDefault/2) + passbandWidth), rigCaps.spectAmpMax);

            //qDebug() << "Default" << pbtDefault << "Inner" << TPBFInner << "Outer" << TPBFOuter << "Pass" << passbandWidth << "Center" << passbandCenterFrequency << "CW" << cwPitch;
        }

        if (underlayMode == underlayPeakHold)
        {
#if QCUSTOMPLOT_VERSION < 0x020000
            plot->graph(1)->setData(x, y2); // peaks
#else
            plot->graph(1)->setData(x, y2, true); // peaks
#endif
        }
        else if (underlayMode != underlayNone) {
            computePlasma();
#if QCUSTOMPLOT_VERSION < 0x020000
            plot->graph(1)->setData(x, spectrumPlasmaLine);
#else
            plot->graph(1)->setData(x, spectrumPlasmaLine, true);
#endif
        }
        else {
#if QCUSTOMPLOT_VERSION < 0x020000
            plot->graph(1)->setData(x, y2); // peaks, but probably cleared out
#else
            plot->graph(1)->setData(x, y2, true); // peaks, but probably cleared out
#endif
        }

        if(updateRange)
            plot->yAxis->setRange(prefs.plotFloor, prefs.plotCeiling);

        plot->xAxis->setRange(startFreq, endFreq);
        plot->replot();

        if(specLen == spectWidth)
        {
            wfimage.prepend(spectrum);
            wfimage.pop_back();
            QByteArray wfRow;
            // Waterfall:
            for(int row = 0; row < wfLength; row++)
            {
                wfRow = wfimage.at(row);
                for(int col = 0; col < spectWidth; col++)
                {
                    colorMap->data()->setCell( col, row, (unsigned char)wfRow.at(col));
                }
            }
            if(updateRange)
            {
                colorMap->setDataRange(QCPRange(wfFloor, wfCeiling));
            }
            wf->yAxis->setRange(0,wfLength - 1);
            wf->xAxis->setRange(0, spectWidth-1);
            wf->replot();
        }
        oldPlotFloor = plotFloor;
        oldPlotCeiling = plotCeiling;
    }
}

void wfmain::preparePlasma()
{
    if(plasmaPrepared)
        return;

    if(spectrumPlasmaSize == 0)
        spectrumPlasmaSize = 128;

    plasmaMutex.lock();
    spectrumPlasma.clear();


    spectrumPlasma.squeeze();
    plasmaMutex.unlock();
    plasmaPrepared = true;
}

void wfmain::computePlasma()
{
    plasmaMutex.lock();
    spectrumPlasmaLine.clear();
    spectrumPlasmaLine.resize(spectWidth);
    int specPlasmaSize = spectrumPlasma.size();
    if(underlayMode == underlayAverageBuffer)
    {
        for(int col=0; col < spectWidth; col++)
        {
            for(int pos=0; pos < specPlasmaSize; pos++)
            {
                spectrumPlasmaLine[col] += (unsigned char)spectrumPlasma[pos][col];
            }
            spectrumPlasmaLine[col] = spectrumPlasmaLine[col] / specPlasmaSize;
        }
    } else if (underlayMode == underlayPeakBuffer){
        // peak mode, running peak display
        for(int col=0; col < spectWidth; col++)
        {
            for(int pos=0; pos < specPlasmaSize; pos++)
            {
                if((double)((unsigned char)spectrumPlasma[pos][col]) > spectrumPlasmaLine[col])
                    spectrumPlasmaLine[col] = (unsigned char)spectrumPlasma[pos][col];
            }
        }
    }
    plasmaMutex.unlock();
}

void wfmain::receiveSpectrumMode(spectrumMode spectMode)
{
    for (int i = 0; i < ui->spectrumModeCombo->count(); i++)
    {
        if (static_cast<spectrumMode>(ui->spectrumModeCombo->itemData(i).toInt()) == spectMode)
        {
            ui->spectrumModeCombo->blockSignals(true);
            ui->spectrumModeCombo->setCurrentIndex(i);
            ui->spectrumModeCombo->blockSignals(false);
        }
    }
    setUISpectrumControlsToMode(spectMode);
}


void wfmain::handlePlotDoubleClick(QMouseEvent *me)
{
    if (me->button() == Qt::LeftButton) {

        double x;
        freqt freqGo;
        //double y;
        //double px;
        if (!freqLock)
        {
            //y = plot->yAxis->pixelToCoord(me->pos().y());
            x = plot->xAxis->pixelToCoord(me->pos().x());
            freqGo.Hz = x * 1E6;

            freqGo.Hz = roundFrequency(freqGo.Hz, tsWfScrollHz);
            freqGo.MHzDouble = (float)freqGo.Hz / 1E6;

            //emit setFrequency(0,freq);
            issueCmd(cmdSetFreq, freqGo);
            freq = freqGo;
            setUIFreq();
            //issueDelayedCommand(cmdGetFreq);
            showStatusBarText(QString("Going to %1 MHz").arg(x));
        }
    } else if (me->button() == Qt::RightButton) {
        QCPAbstractItem* item = plot->itemAt(me->pos(), true);
        QCPItemRect* rectItem = dynamic_cast<QCPItemRect*> (item);
        if (rectItem != nullptr) {
            double pbFreq = (pbtDefault / passbandWidth) * 127.0;
            qint16 newFreq = pbFreq + 128;
            issueCmdUniquePriority(cmdSetTPBFInner, (unsigned char)newFreq);
            issueCmdUniquePriority(cmdSetTPBFOuter, (unsigned char)newFreq);
            issueDelayedCommandUnique(cmdGetTPBFInner);
            issueDelayedCommandUnique(cmdGetTPBFOuter);

        }
    }
}

void wfmain::handleWFDoubleClick(QMouseEvent *me)
{
    double x;
    freqt freqGo;
    //double y;
    //x = wf->xAxis->pixelToCoord(me->pos().x());
    //y = wf->yAxis->pixelToCoord(me->pos().y());
    // cheap trick until I figure out how the axis works on the WF:
    if(!freqLock)
    {
        x = plot->xAxis->pixelToCoord(me->pos().x());
        freqGo.Hz = x*1E6;

        freqGo.Hz = roundFrequency(freqGo.Hz, tsWfScrollHz);
        freqGo.MHzDouble = (float)freqGo.Hz / 1E6;

        //emit setFrequency(0,freq);
        issueCmd(cmdSetFreq, freqGo);
        freq = freqGo;
        setUIFreq();
        showStatusBarText(QString("Going to %1 MHz").arg(x));
    }
}

void wfmain::handlePlotClick(QMouseEvent* me)
{
    QCPAbstractItem* item = plot->itemAt(me->pos(), true);
    QCPItemText* textItem = dynamic_cast<QCPItemText*> (item);
    QCPItemRect* rectItem = dynamic_cast<QCPItemRect*> (item);
#if QCUSTOMPLOT_VERSION < 0x020000
    int leftPix = (int)passbandIndicator->left->pixelPoint().x();
    int rightPix = (int)passbandIndicator->right->pixelPoint().x();
    int pbtLeftPix = (int)pbtIndicator->left->pixelPoint().x();
    int pbtRightPix = (int)pbtIndicator->right->pixelPoint().x();
#else
    int leftPix = (int)passbandIndicator->left->pixelPosition().x();
    int rightPix = (int)passbandIndicator->right->pixelPosition().x();
    int pbtLeftPix = (int)pbtIndicator->left->pixelPosition().x();
    int pbtRightPix = (int)pbtIndicator->right->pixelPosition().x();
#endif
    int pbtCenterPix = pbtLeftPix + ((pbtRightPix - pbtLeftPix) / 2);
    int cursor = me->pos().x();

    if (me->button() == Qt::LeftButton) {
        if (textItem != nullptr)
        {
            QMap<QString, spotData*>::iterator spot = clusterSpots.find(textItem->text());
            if (spot != clusterSpots.end() && spot.key() == textItem->text())
            {
                qInfo(logGui()) << "Clicked on spot:" << textItem->text();
                freqt freqGo;
                freqGo.Hz = (spot.value()->frequency) * 1E6;
                freqGo.MHzDouble = spot.value()->frequency;
                issueCmdUniquePriority(cmdSetFreq, freqGo);
            }
        }
        else if (passbandAction == passbandStatic && rectItem != nullptr)
        {
            if ((cursor <= leftPix && cursor > leftPix - 10) || (cursor >= rightPix && cursor < rightPix + 10))
            {
                passbandAction = passbandResizing;
            }
        }
        else if (prefs.clickDragTuningEnable)
        {
            this->mousePressFreq = plot->xAxis->pixelToCoord(cursor);
            showStatusBarText(QString("Selected %1 MHz").arg(this->mousePressFreq));
        }
    } 
    else if (me->button() == Qt::RightButton) 
    {
        if (textItem != nullptr) {
            QMap<QString, spotData*>::iterator spot = clusterSpots.find(textItem->text());
            if (spot != clusterSpots.end() && spot.key() == textItem->text()) {
                /* parent and children are destroyed on close */
                QDialog* spotDialog = new QDialog();
                QVBoxLayout* vlayout = new QVBoxLayout;
                //spotDialog->setFixedSize(240, 100);
                spotDialog->setBaseSize(1, 1);
                spotDialog->setWindowTitle(spot.value()->dxcall);
                QLabel* dxcall = new QLabel(QString("DX:%1").arg(spot.value()->dxcall));
                QLabel* spotter = new QLabel(QString("Spotter:%1").arg(spot.value()->spottercall));
                QLabel* frequency = new QLabel(QString("Frequency:%1 MHz").arg(spot.value()->frequency));
                QLabel* comment = new QLabel(QString("Comment:%1").arg(spot.value()->comment));
                QAbstractButton* bExit = new QPushButton("Close");
                vlayout->addWidget(dxcall);
                vlayout->addWidget(spotter);
                vlayout->addWidget(frequency);
                vlayout->addWidget(comment);
                vlayout->addWidget(bExit);
                spotDialog->setLayout(vlayout);
                spotDialog->show();
                spotDialog->connect(bExit, SIGNAL(clicked()), spotDialog, SLOT(close()));
            }
        }
        else if (passbandAction == passbandStatic && rectItem != nullptr)
        {
            if (cursor <= pbtLeftPix && cursor > pbtLeftPix - 10)
            {
                passbandAction = pbtInnerMove;
            }
            else if (cursor >= pbtRightPix && cursor < pbtRightPix + 10)
            {
                passbandAction = pbtOuterMove;
            }
            else if (cursor > pbtCenterPix - 20 && cursor < pbtCenterPix + 20)
            {
                passbandAction = pbtMoving;
            }
            this->mousePressFreq = plot->xAxis->pixelToCoord(cursor);
        }
    }
}

void wfmain::handlePlotMouseRelease(QMouseEvent* me)
{
    QCPAbstractItem* item = plot->itemAt(me->pos(), true);
    QCPItemText* textItem = dynamic_cast<QCPItemText*> (item);

    if (textItem == nullptr && prefs.clickDragTuningEnable) {
        this->mouseReleaseFreq = plot->xAxis->pixelToCoord(me->pos().x());
        double delta = mouseReleaseFreq - mousePressFreq;
        qInfo(logGui()) << "Mouse release delta: " << delta;

    }
    if (passbandAction != passbandStatic) {
        passbandAction = passbandStatic;
    }
}

void wfmain::handlePlotMouseMove(QMouseEvent* me)
{
    QCPAbstractItem* item = plot->itemAt(me->pos(), true);
    QCPItemText* textItem = dynamic_cast<QCPItemText*> (item);
    QCPItemRect* rectItem = dynamic_cast<QCPItemRect*> (item);
#if QCUSTOMPLOT_VERSION < 0x020000
    int leftPix = (int)passbandIndicator->left->pixelPoint().x();
    int rightPix = (int)passbandIndicator->right->pixelPoint().x();
    int pbtLeftPix = (int)pbtIndicator->left->pixelPoint().x();
    int pbtRightPix = (int)pbtIndicator->right->pixelPoint().x();
#else
    int leftPix = (int)passbandIndicator->left->pixelPosition().x();
    int rightPix = (int)passbandIndicator->right->pixelPosition().x();
    int pbtLeftPix = (int)pbtIndicator->left->pixelPosition().x();
    int pbtRightPix = (int)pbtIndicator->right->pixelPosition().x();
#endif
    int pbtCenterPix = pbtLeftPix + ((pbtRightPix - pbtLeftPix) / 2);
    int cursor = me->pos().x();
    double movedFrequency = plot->xAxis->pixelToCoord(cursor) - mousePressFreq;
    if (passbandAction == passbandStatic && rectItem != nullptr)
    {                
        if ((cursor <= leftPix && cursor > leftPix - 10) ||
            (cursor >= rightPix && cursor < rightPix + 10) ||
            (cursor <= pbtLeftPix && cursor > pbtLeftPix - 10) ||
            (cursor >= pbtRightPix && cursor < pbtRightPix + 10))
        {
            setCursor(Qt::SizeHorCursor);
        }
        else if (cursor > pbtCenterPix - 20 && cursor < pbtCenterPix + 20) {
            setCursor(Qt::OpenHandCursor);
        }
    }
    else if (passbandAction == passbandResizing)
    {
        static double lastFreq = movedFrequency;
        if (lastFreq - movedFrequency > 0.000049 || movedFrequency - lastFreq > 0.000049) {

            // We are currently resizing the passband.
            double pb = 0.0;
            double origin = 0.0;
            switch (currentModeInfo.mk)
            {
            case modeCW:
            case modeCW_R:
                origin = 0.0;
                break;
            case modeLSB:
                origin = -passbandCenterFrequency;
                break;
            default:
                origin = passbandCenterFrequency;
                break;
            }

            if (plot->xAxis->pixelToCoord(cursor) >= freq.MHzDouble + origin) {
                pb = plot->xAxis->pixelToCoord(cursor) - passbandIndicator->topLeft->coords().x();
            }
            else {
                pb = passbandIndicator->bottomRight->coords().x() - plot->xAxis->pixelToCoord(cursor);
            }
            issueCmdUniquePriority(cmdSetPassband, (quint16)(pb * 1000000));
            issueDelayedCommandUnique(cmdGetPassband);
            lastFreq = movedFrequency;
        }
    }
    else if (passbandAction == pbtMoving) {

        //qint16 shift = (qint16)(level - 128);
        //TPBFInner = (double)(shift / 127.0) * (passbandWidth);
        // Only move if more than 100Hz
        static double lastFreq = movedFrequency;
        if (lastFreq - movedFrequency > 0.000049 || movedFrequency - lastFreq > 0.000049) {

            double innerFreq = ((double)(TPBFInner + movedFrequency) / passbandWidth) * 127.0;
            double outerFreq = ((double)(TPBFOuter + movedFrequency) / passbandWidth) * 127.0;

            qint16 newInFreq = innerFreq + 128;
            qint16 newOutFreq = outerFreq + 128;

            if (newInFreq >= 0 && newInFreq <= 255 && newOutFreq >= 0 && newOutFreq <= 255) {
                qDebug() << QString("Moving passband by %1 Hz (Inner %2) (Outer %3) Mode:%4").arg((qint16)(movedFrequency * 1000000))
                    .arg(newInFreq).arg(newOutFreq).arg(currentModeInfo.mk);

                issueCmdUniquePriority(cmdSetTPBFInner, (unsigned char)newInFreq);
                issueCmdUniquePriority(cmdSetTPBFOuter, (unsigned char)newOutFreq);
                issueDelayedCommandUnique(cmdGetTPBFInner);
                issueDelayedCommandUnique(cmdGetTPBFOuter);
            }
            lastFreq = movedFrequency;
        }
    }
    else if (passbandAction == pbtInnerMove) {
        static double lastFreq = movedFrequency;
        if (lastFreq - movedFrequency > 0.000049 || movedFrequency - lastFreq > 0.000049) {
            double pbFreq = ((double)(TPBFInner + movedFrequency) / passbandWidth) * 127.0;
            qint16 newFreq = pbFreq + 128;
            if (newFreq >= 0 && newFreq <= 255) {
                issueCmdUniquePriority(cmdSetTPBFInner, (unsigned char)newFreq);
                issueDelayedCommandUnique(cmdGetTPBFInner);
            }
            lastFreq = movedFrequency;
        }
    }
    else if (passbandAction == pbtOuterMove) {
        static double lastFreq = movedFrequency;
        if (lastFreq - movedFrequency > 0.000049 || movedFrequency - lastFreq > 0.000049) {
            double pbFreq = ((double)(TPBFOuter + movedFrequency) / passbandWidth) * 127.0;
            qint16 newFreq = pbFreq + 128;
            if (newFreq >= 0 && newFreq <= 255) {
                issueCmdUniquePriority(cmdSetTPBFOuter, (unsigned char)newFreq);
                issueDelayedCommandUnique(cmdGetTPBFOuter);
            }
            lastFreq = movedFrequency;
        }
    }
    else  if (passbandAction == passbandStatic && me->buttons() == Qt::LeftButton && textItem == nullptr && prefs.clickDragTuningEnable)
    {
        double delta = plot->xAxis->pixelToCoord(cursor) - mousePressFreq;
        qDebug(logGui()) << "Mouse moving delta: " << delta;
        if( (( delta < -0.0001 ) || (delta > 0.0001)) && ((delta < 0.501) && (delta > -0.501)) )
        {
            freqt freqGo;
            freqGo.Hz = (freq.MHzDouble + delta) * 1E6;
            //freqGo.Hz = roundFrequency(freqGo.Hz, tsWfScrollHz);
            freqGo.MHzDouble = (float)freqGo.Hz / 1E6;
            issueCmdUniquePriority(cmdSetFreq, freqGo);
        }
    } else {
        setCursor(Qt::ArrowCursor);
    }

}

void wfmain::handleWFClick(QMouseEvent *me)
{
    double x = plot->xAxis->pixelToCoord(me->pos().x());
    showStatusBarText(QString("Selected %1 MHz").arg(x));
}

void wfmain::handleWFScroll(QWheelEvent *we)
{
    // The wheel event is typically
    // .y() and is +/- 120.
    // We will click the dial once for every 120 received.
    //QPoint delta = we->angleDelta();

    if(freqLock)
        return;

    freqt f;
    f.Hz = 0;
    f.MHzDouble = 0;

    int clicks = we->angleDelta().y() / 120;

    if(!clicks)
        return;

    unsigned int stepsHz = tsWfScrollHz;

    Qt::KeyboardModifiers key=  we->modifiers();

    if ((key == Qt::ShiftModifier) && (stepsHz !=1))
    {
        stepsHz /= 10;
    } else if (key == Qt::ControlModifier)
    {
        stepsHz *= 10;
    }

    f.Hz = roundFrequencyWithStep(freq.Hz, clicks, stepsHz);
    f.MHzDouble = f.Hz / (double)1E6;
    freq = f;

    //emit setFrequency(0,f);
    issueCmdUniquePriority(cmdSetFreq, f);
    ui->freqLabel->setText(QString("%1").arg(f.MHzDouble, 0, 'f'));
    //issueDelayedCommandUnique(cmdGetFreq);
}

void wfmain::handlePlotScroll(QWheelEvent *we)
{
    handleWFScroll(we);
}

void wfmain::on_scopeEnableWFBtn_clicked(bool checked)
{
    if(checked)
    {
        emit spectOutputEnable();
    } else {
        emit spectOutputDisable();
    }
}



void wfmain::receiveMode(unsigned char mode, unsigned char filter)
{
    //qInfo(logSystem()) << __func__ << "Received mode " << mode << " current mode: " << currentModeIndex;

    bool found=false;

    if(mode < 0x23)
    {

        // Update mode information if mode/filter has changed
        if (currentModeInfo.mk != (mode_kind)mode || currentModeInfo.filter != filter)
        {

            removePeriodicRapidCmd(cmdGetCwPitch);
            removeSlowPeriodicCommand(cmdGetPassband);
            removeSlowPeriodicCommand(cmdGetTPBFInner);
            removeSlowPeriodicCommand(cmdGetTPBFOuter);

            quint16 maxPassbandHz = 0;
            switch ((mode_kind)mode) {
            case modeFM:
                if (filter == 1)
                    passbandWidth = 0.015;
                else if (filter == 2)
                    passbandWidth = 0.010;
                else
                    passbandWidth = 0.007;
                passbandCenterFrequency = 0.0;
                maxPassbandHz = 10E3;
                break;
            case modeCW:
            case modeCW_R:
                insertPeriodicRapidCmdUnique(cmdGetCwPitch);
                issueDelayedCommandUnique(cmdGetCwPitch);
                maxPassbandHz = 3600;
                break;
            case modeAM:
                passbandCenterFrequency = 0.0;
                maxPassbandHz = 10E3;
                break;
            case modeLSB:
            case modeUSB:
                passbandCenterFrequency = 0.0015;
                maxPassbandHz = 3600;
                break;
            default:
                passbandCenterFrequency = 0.0;
                maxPassbandHz = 3600;
                break;
            }

            for (int i = 0; i < ui->modeSelectCombo->count(); i++)
            {
                if (ui->modeSelectCombo->itemData(i).toInt() == mode)
                {
                    ui->modeSelectCombo->blockSignals(true);
                    ui->modeSelectCombo->setCurrentIndex(i);
                    ui->modeSelectCombo->blockSignals(false);
                    found = true;
                }
            }

            if ((filter) && (filter < 4)) {
                ui->modeFilterCombo->blockSignals(true);
                ui->modeFilterCombo->setCurrentIndex(filter - 1);
                ui->modeFilterCombo->blockSignals(false);
            }

            currentModeIndex = mode;
            currentModeInfo.mk = (mode_kind)mode;
            currentMode = (mode_kind)mode;
            currentModeInfo.filter = filter;
            currentModeInfo.reg = mode;
            rpt->handleUpdateCurrentMainMode(currentModeInfo);
            cw->handleCurrentModeUpdate(currentMode);
            if (!found)
            {
                qWarning(logSystem()) << __func__ << "Received mode " << mode << " but could not match to any index within the modeSelectCombo. ";
                return;
            }

            if (maxPassbandHz != 0)
            {
                trxadj->setMaxPassband(maxPassbandHz);
            }

            if (currentModeInfo.mk != modeFM) 
            {
                insertSlowPeriodicCommand(cmdGetPassband, 128);
                insertSlowPeriodicCommand(cmdGetTPBFInner, 128);
                insertSlowPeriodicCommand(cmdGetTPBFOuter, 128);
                issueDelayedCommandUnique(cmdGetPassband);
                issueDelayedCommandUnique(cmdGetTPBFInner);
                issueDelayedCommandUnique(cmdGetTPBFOuter);
            }
            
            // Note: we need to know if the DATA mode is active to reach mode-D
            // some kind of queued query:
            if (rigCaps.hasDataModes && rigCaps.hasTransmit)
            {
                issueDelayedCommand(cmdGetDataMode);
            }

        }

    } else {
        qCritical(logSystem()) << __func__ << "Invalid mode " << mode << " received. ";
    }
}

void wfmain::receiveDataModeStatus(bool dataEnabled)
{
    ui->dataModeBtn->blockSignals(true);
    ui->dataModeBtn->setChecked(dataEnabled);
    ui->dataModeBtn->blockSignals(false);

    usingDataMode = dataEnabled;
}

void wfmain::on_clearPeakBtn_clicked()
{
    if(haveRigCaps)
    {
        spectrumPeaks = QByteArray( (int)spectWidth, '\x01' );
        clearPlasmaBuffer();
    }
    return;
}

void wfmain::on_fullScreenChk_clicked(bool checked)
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

void wfmain::on_goFreqBtn_clicked()
{
    freqt f;
    bool ok = false;
    double freqDbl = 0;
    int KHz = 0;

    if(ui->freqMhzLineEdit->text().contains("."))
    {

        freqDbl = ui->freqMhzLineEdit->text().toDouble(&ok);
        if(ok)
        {
            f.Hz = freqDbl*1E6;
        }
    } else {
        KHz = ui->freqMhzLineEdit->text().toInt(&ok);
        if(ok)
        {
            f.Hz = KHz*1E3;
        }
    }
    if(ok)
    {
        mode_info m;
        issueCmd(cmdSetFreq, f);
        m.mk = sidebandChooser::getMode(f, currentMode);
        m.reg = (unsigned char) m.mk;
        m.filter = ui->modeFilterCombo->currentData().toInt();

        if((m.mk != currentMode) && !usingDataMode && prefs.automaticSidebandSwitching)
        {
            issueCmd(cmdSetMode, m);
            currentMode = m.mk;
        }

        f.MHzDouble = (float)f.Hz / 1E6;
        freq = f;
        setUIFreq();
    }

    ui->freqMhzLineEdit->selectAll();
    freqTextSelected = true;
    ui->tabWidget->setCurrentIndex(0);
}

void wfmain::checkFreqSel()
{
    if(freqTextSelected)
    {
        freqTextSelected = false;
        ui->freqMhzLineEdit->clear();
    }
}

void wfmain::on_f0btn_clicked()
{
    checkFreqSel();
    ui->freqMhzLineEdit->setText(ui->freqMhzLineEdit->text().append("0"));
}
void wfmain::on_f1btn_clicked()
{
    checkFreqSel();
    ui->freqMhzLineEdit->setText(ui->freqMhzLineEdit->text().append("1"));
}

void wfmain::on_f2btn_clicked()
{
    checkFreqSel();
    ui->freqMhzLineEdit->setText(ui->freqMhzLineEdit->text().append("2"));

}
void wfmain::on_f3btn_clicked()
{
    ui->freqMhzLineEdit->setText(ui->freqMhzLineEdit->text().append("3"));

}
void wfmain::on_f4btn_clicked()
{
    checkFreqSel();
    ui->freqMhzLineEdit->setText(ui->freqMhzLineEdit->text().append("4"));

}
void wfmain::on_f5btn_clicked()
{
    checkFreqSel();
    ui->freqMhzLineEdit->setText(ui->freqMhzLineEdit->text().append("5"));

}
void wfmain::on_f6btn_clicked()
{
    checkFreqSel();
    ui->freqMhzLineEdit->setText(ui->freqMhzLineEdit->text().append("6"));

}
void wfmain::on_f7btn_clicked()
{
    checkFreqSel();
    ui->freqMhzLineEdit->setText(ui->freqMhzLineEdit->text().append("7"));

}
void wfmain::on_f8btn_clicked()
{
    checkFreqSel();
    ui->freqMhzLineEdit->setText(ui->freqMhzLineEdit->text().append("8"));

}
void wfmain::on_f9btn_clicked()
{
    checkFreqSel();
    ui->freqMhzLineEdit->setText(ui->freqMhzLineEdit->text().append("9"));

}
void wfmain::on_fDotbtn_clicked()
{
    checkFreqSel();
    ui->freqMhzLineEdit->setText(ui->freqMhzLineEdit->text().append("."));

}


void wfmain::on_fBackbtn_clicked()
{
    QString currentFreq = ui->freqMhzLineEdit->text();
    currentFreq.chop(1);
    ui->freqMhzLineEdit->setText(currentFreq);
}

void wfmain::on_fCEbtn_clicked()
{
    ui->freqMhzLineEdit->clear();
    freqTextSelected = false;
}

void wfmain::on_spectrumModeCombo_currentIndexChanged(int index)
{
    spectrumMode smode = static_cast<spectrumMode>(ui->spectrumModeCombo->itemData(index).toInt());
    emit setScopeMode(smode);
    setUISpectrumControlsToMode(smode);
}

void wfmain::setUISpectrumControlsToMode(spectrumMode smode)
{
    if((smode==spectModeCenter) || (smode==spectModeScrollC))
    {
        ui->specEdgeLabel->hide();
        ui->scopeEdgeCombo->hide();
        ui->customEdgeBtn->hide();
        ui->toFixedBtn->show();
        ui->specSpanLabel->show();
        ui->scopeBWCombo->show();
    } else {
        ui->specEdgeLabel->show();
        ui->scopeEdgeCombo->show();
        ui->customEdgeBtn->show();
        ui->toFixedBtn->hide();
        ui->specSpanLabel->hide();
        ui->scopeBWCombo->hide();
    }
}

void wfmain::on_fEnterBtn_clicked()
{
    // TODO: do not jump to main tab on enter, only on return
    // or something.
    // Maybe this should be an option in settings->
    on_goFreqBtn_clicked();
}

void wfmain::on_scopeBWCombo_currentIndexChanged(int index)
{
    emit setScopeSpan((char)index);
}

void wfmain::on_scopeEdgeCombo_currentIndexChanged(int index)
{
    emit setScopeEdge((char)index+1);
}

void wfmain::changeMode(mode_kind mode)
{
    bool dataOn = false;
    if(((unsigned char) mode >> 4) == 0x08)
    {
        dataOn = true;
        mode = (mode_kind)((int)mode & 0x0f);
    }

    changeMode(mode, dataOn);
}

void wfmain::changeMode(mode_kind mode, bool dataOn)
{
    int filter = ui->modeFilterCombo->currentData().toInt();
    mode_info m;
    m.filter = (unsigned char) filter;
    m.reg = (unsigned char) mode;
    issueCmd(cmdSetMode, m);

    currentMode = mode;

    if(dataOn && (!usingDataMode))
    {
        issueDelayedCommand(cmdSetDataModeOn);
        ui->dataModeBtn->blockSignals(true);
        ui->dataModeBtn->setChecked(true);
        ui->dataModeBtn->blockSignals(false);
    } else if ((!dataOn) && usingDataMode) {
        issueDelayedCommand(cmdSetDataModeOff);
        ui->dataModeBtn->blockSignals(true);
        ui->dataModeBtn->setChecked(false);
        ui->dataModeBtn->blockSignals(false);
    }
    issueDelayedCommand(cmdGetMode);
}

void wfmain::on_modeSelectCombo_activated(int index)
{
    // The "acticvated" signal means the user initiated a mode change.
    // This function is not called if code initiated the change.

    mode_info mode;
    unsigned char newMode = static_cast<unsigned char>(ui->modeSelectCombo->itemData(index).toUInt());
    currentModeIndex = newMode;
    mode.reg = newMode;


    int filterSelection = ui->modeFilterCombo->currentData().toInt();
    if(filterSelection == 99)
    {
        // oops, we forgot to reset the combo box
        return;
    } else {
        //qInfo(logSystem()) << __func__ << " at index " << index << " has newMode: " << newMode;
        currentMode = (mode_kind)newMode;
        mode.filter = filterSelection;
        mode.name = ui->modeSelectCombo->currentText(); // for debug

        for(unsigned int i=0; i < rigCaps.modes.size(); i++)
        {
            if(rigCaps.modes.at(i).reg == newMode)
            {
                mode.mk = rigCaps.modes.at(i).mk;
                break;
            }
        }

        issueCmd(cmdSetMode, mode);
        issueDelayedCommand(cmdGetMode);

        //currentModeInfo = mode;
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
    delta = delta / ui->freqDial->singleStep();

    // With the number of steps and direction of steps established,
    // we can now adjust the frequency:

    f.Hz = roundFrequencyWithStep(freq.Hz, delta, tsKnobHz);
    f.MHzDouble = f.Hz / (double)1E6;
    if (f.Hz > 0)
    {
        freq = f;
        oldFreqDialVal = value;
        ui->freqLabel->setText(QString("%1").arg(f.MHzDouble, 0, 'f'));
        issueCmdUniquePriority(cmdSetFreq, f);
    } else {
        ui->freqDial->blockSignals(true);
        ui->freqDial->setValue(oldFreqDialVal);
        ui->freqDial->blockSignals(false);
        return;
    }
}

void wfmain::receiveBandStackReg(freqt freqGo, char mode, char filter, bool dataOn)
{
    // read the band stack and apply by sending out commands

    qInfo(logSystem()) << __func__ << "BSR received into main: Freq: " << freqGo.Hz << ", mode: " << (unsigned int)mode << ", filter: " << (unsigned int)filter << ", data mode: " << dataOn;
    //emit setFrequency(0,freq);
    issueCmd(cmdSetFreq, freqGo);
    setModeVal = (unsigned char) mode;
    setFilterVal = (unsigned char) filter;

    issueDelayedCommand(cmdSetModeFilter);
    freq = freqGo;
    setUIFreq();

    if(dataOn)
    {
        issueDelayedCommand(cmdSetDataModeOn);
    } else {
        issueDelayedCommand(cmdSetDataModeOff);
    }
    //issueDelayedCommand(cmdGetFreq);
    //issueDelayedCommand(cmdGetMode);
    ui->tabWidget->setCurrentIndex(0);

    receiveMode((unsigned char) mode, (unsigned char) filter); // update UI
}

void wfmain::bandStackBtnClick()
{
    bandStkRegCode = ui->bandStkPopdown->currentIndex() + 1;
    waitingForBandStackRtn = true; // so that when the return is parsed we jump to this frequency/mode info
    emit getBandStackReg(bandStkBand, bandStkRegCode);
}

void wfmain::setBand(int band)
{
    issueCmdUniquePriority(cmdGetBandStackReg, (char)band);
    //bandStackBtnClick();
}

void wfmain::on_band23cmbtn_clicked()
{
    bandStkBand = rigCaps.bsr[band23cm]; // 23cm
    bandStackBtnClick();
}

void wfmain::on_band70cmbtn_clicked()
{
    bandStkBand = rigCaps.bsr[band70cm]; // 70cm
    bandStackBtnClick();
}

void wfmain::on_band2mbtn_clicked()
{
    bandStkBand = rigCaps.bsr[band2m]; // 2m
    bandStackBtnClick();
}

void wfmain::on_bandAirbtn_clicked()
{
    bandStkBand = rigCaps.bsr[bandAir]; // VHF Aircraft
    bandStackBtnClick();
}

void wfmain::on_bandWFMbtn_clicked()
{
    bandStkBand = rigCaps.bsr[bandWFM]; // Broadcast FM
    bandStackBtnClick();
}

void wfmain::on_band4mbtn_clicked()
{
    // There isn't a BSR for this one:
    freqt f;
    if ((currentMode == modeAM) || (currentMode == modeFM))
    {
        f.Hz = (70.260) * 1E6;
    } else {
        f.Hz = (70.200) * 1E6;
    }
    issueCmd(cmdSetFreq, f);
    //emit setFrequency(0,f);
    issueDelayedCommandUnique(cmdGetFreq);
    ui->tabWidget->setCurrentIndex(0);
}

void wfmain::on_band6mbtn_clicked()
{
    bandStkBand = 0x10; // 6 meters
    bandStackBtnClick();
}

void wfmain::on_band10mbtn_clicked()
{
    bandStkBand = 0x09; // 10 meters
    bandStackBtnClick();
}

void wfmain::on_band12mbtn_clicked()
{
    bandStkBand = 0x08; // 12 meters
    bandStackBtnClick();
}

void wfmain::on_band15mbtn_clicked()
{
    bandStkBand = 0x07; // 15 meters
    bandStackBtnClick();
}

void wfmain::on_band17mbtn_clicked()
{
    bandStkBand = 0x06; // 17 meters
    bandStackBtnClick();
}

void wfmain::on_band20mbtn_clicked()
{
    bandStkBand = 0x05; // 20 meters
    bandStackBtnClick();
}

void wfmain::on_band30mbtn_clicked()
{
    bandStkBand = 0x04; // 30 meters
    bandStackBtnClick();
}

void wfmain::on_band40mbtn_clicked()
{
    bandStkBand = 0x03; // 40 meters
    bandStackBtnClick();
}

void wfmain::on_band60mbtn_clicked()
{
    // This one is tricky. There isn't a band stack register on the
    // 7300 for 60 meters, so we just drop to the middle of the band:
    // Channel 1: 5330.5 kHz
    // Channel 2: 5346.5 kHz
    // Channel 3: 5357.0 kHz
    // Channel 4: 5371.5 kHz
    // Channel 5: 5403.5 kHz
    // Really not sure what the best strategy here is, don't want to
    // clutter the UI with 60M channel buttons...
    freqt f;
    f.Hz = (5.3305) * 1E6;
    issueCmd(cmdSetFreq, f);
    //emit setFrequency(0,f);
    issueDelayedCommandUnique(cmdGetFreq);
    ui->tabWidget->setCurrentIndex(0);
}

void wfmain::on_band80mbtn_clicked()
{
    bandStkBand = 0x02; // 80 meters
    bandStackBtnClick();
}

void wfmain::on_band160mbtn_clicked()
{
    bandStkBand = 0x01; // 160 meters
    bandStackBtnClick();
}

void wfmain::on_band630mbtn_clicked()
{
    freqt f;
    f.Hz = 475 * 1E3;
    //emit setFrequency(0,f);
    issueCmd(cmdSetFreq, f);
    issueDelayedCommandUnique(cmdGetFreq);
    ui->tabWidget->setCurrentIndex(0);
}

void wfmain::on_band2200mbtn_clicked()
{
    freqt f;
    f.Hz = 136 * 1E3;
    //emit setFrequency(0,f);
    issueCmd(cmdSetFreq, f);
    issueDelayedCommandUnique(cmdGetFreq);
    ui->tabWidget->setCurrentIndex(0);
}

void wfmain::on_bandGenbtn_clicked()
{
    // "GENE" general coverage frequency outside the ham bands
    // which does probably include any 60 meter frequencies used.
    bandStkBand = rigCaps.bsr[bandGen]; // GEN
    bandStackBtnClick();
}

void wfmain::on_aboutBtn_clicked()
{
    abtBox->show();
}

void wfmain::on_fStoBtn_clicked()
{
    // sequence:
    // type frequency
    // press Enter or Go
    // change mode if desired
    // type in index number 0 through 99
    // press STO

    bool ok;
    int preset_number = ui->freqMhzLineEdit->text().toInt(&ok);

    if(ok && (preset_number >= 0) && (preset_number < 100))
    {
        // TODO: keep an enum around with the current mode
        mem.setPreset(preset_number, freq.MHzDouble, (mode_kind)ui->modeSelectCombo->currentData().toInt() );
        showStatusBarText( QString("Storing frequency %1 to memory location %2").arg( freq.MHzDouble ).arg(preset_number) );
    } else {
        showStatusBarText(QString("Could not store preset to %1. Valid preset numbers are 0 to 99").arg(preset_number));
    }
}

void wfmain::on_fRclBtn_clicked()
{
    // Sequence:
    // type memory location 0 through 99
    // press RCL

    // Program recalls data stored in vector at position specified
    // drop contents into text box, press go button
    // add delayed command for mode and data mode

    preset_kind temp;
    bool ok;
    QString freqString;
    int preset_number = ui->freqMhzLineEdit->text().toInt(&ok);

    if(ok && (preset_number >= 0) && (preset_number < 100))
    {
        temp = mem.getPreset(preset_number);
        // TODO: change to int hz
        // TODO: store filter setting as well.
        freqString = QString("%1").arg(temp.frequency);
        ui->freqMhzLineEdit->setText( freqString );
        ui->goFreqBtn->click();
        setModeVal = temp.mode;
        setFilterVal = ui->modeFilterCombo->currentIndex()+1; // TODO, add to memory
        issueDelayedCommand(cmdSetModeFilter);
        issueDelayedCommand(cmdGetMode);
    } else {
        qInfo(logSystem()) << "Could not recall preset. Valid presets are 0 through 99.";
    }

}

void wfmain::on_rfGainSlider_valueChanged(int value)
{
    issueCmdUniquePriority(cmdSetRxRfGain, (unsigned char) value);
}

void wfmain::on_afGainSlider_valueChanged(int value)
{
    issueCmdUniquePriority(cmdSetAfGain, (unsigned char)value);
    if(usingLAN)
    {
        rxSetup.localAFgain = (unsigned char)(value);
        prefs.localAFgain = (unsigned char)(value);
    }
}

void wfmain::receiveRfGain(unsigned char level)
{
    // qInfo(logSystem()) << "Receive RF  level of" << (int)level << " = " << 100*level/255.0 << "%";
    ui->rfGainSlider->blockSignals(true);
    ui->rfGainSlider->setValue(level);
    ui->rfGainSlider->blockSignals(false);
}

void wfmain::receiveAfGain(unsigned char level)
{
    // qInfo(logSystem()) << "Receive AF  level of" << (int)level << " = " << 100*level/255.0 << "%";
    ui->afGainSlider->blockSignals(true);
    ui->afGainSlider->setValue(level);
    ui->afGainSlider->blockSignals(false);
}

void wfmain::receiveSql(unsigned char level)
{
    ui->sqlSlider->setValue(level);
}

void wfmain::receiveIFShift(unsigned char level)
{
    trxadj->updateIFShift(level);
}

void wfmain::receiveTBPFInner(unsigned char level)
{
    trxadj->updateTPBFInner(level);
}

void wfmain::receiveTBPFOuter(unsigned char level)
{
    trxadj->updateTPBFOuter(level);
}

void wfmain::on_tuneNowBtn_clicked()
{
    issueDelayedCommand(cmdStartATU);
    showStatusBarText("Starting ATU tuning cycle...");
    issueDelayedCommand(cmdGetATUStatus);
}

void wfmain::on_tuneEnableChk_clicked(bool checked)
{
    issueCmd(cmdSetATU, checked);
    if(checked)
    {
        showStatusBarText("Turning on ATU");
    } else {
        showStatusBarText("Turning off ATU");
    }
}

void wfmain::on_exitBtn_clicked()
{
    // Are you sure?
    QApplication::exit();
}

void wfmain::on_pttOnBtn_clicked()
{
    // is it enabled?

    if(!ui->pttEnableChk->isChecked())
    {
        showStatusBarText("PTT is disabled, not sending command. Change under Settings tab.");
        return;
    }

    // Are we already PTT? Not a big deal, just send again anyway.
    showStatusBarText("Sending PTT ON command. Use Control-R to receive.");
    issueCmdUniquePriority(cmdSetPTT, true);

    // send PTT
    // Start 3 minute timer
    pttTimer->start();
    issueDelayedCommand(cmdGetPTT);
}

void wfmain::on_pttOffBtn_clicked()
{
    // Send the PTT OFF command (more than once?)
    showStatusBarText("Sending PTT OFF command");
    issueCmdUniquePriority(cmdSetPTT, false);

    // Stop the 3 min timer
    pttTimer->stop();
    issueDelayedCommand(cmdGetPTT);
}

void wfmain::handlePttLimit()
{
    // transmission time exceeded!
    showStatusBarText("Transmit timeout at 3 minutes. Sending PTT OFF command now.");
    issueDelayedCommand(cmdGetPTT);
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
            issueDelayedCommand(cmdGetATUStatus); // Sometimes the first hit seems to be missed.
            issueDelayedCommand(cmdGetATUStatus);
            break;
        default:
            qInfo(logSystem()) << "Did not understand ATU status: " << (unsigned int) atustatus;
            break;
    }
}

void wfmain::on_pttEnableChk_clicked(bool checked)
{
    prefs.enablePTT = checked;
}

void wfmain::on_serialEnableBtn_clicked(bool checked)
{
    prefs.enableLAN = !checked;
    ui->serialDeviceListCombo->setEnabled(checked);

    ui->connectBtn->setEnabled(true);
    ui->ipAddressTxt->setEnabled(!checked);
    ui->controlPortTxt->setEnabled(!checked);
    ui->usernameTxt->setEnabled(!checked);
    ui->passwordTxt->setEnabled(!checked);
    ui->audioRXCodecCombo->setEnabled(!checked);
    ui->audioTXCodecCombo->setEnabled(!checked);
    ui->audioSampleRateCombo->setEnabled(!checked);
    ui->rxLatencySlider->setEnabled(!checked);
    ui->txLatencySlider->setEnabled(!checked);
    ui->rxLatencyValue->setEnabled(!checked);
    ui->txLatencyValue->setEnabled(!checked);
    ui->audioOutputCombo->setEnabled(!checked);
    ui->audioInputCombo->setEnabled(!checked);
    ui->baudRateCombo->setEnabled(checked);
    ui->serialDeviceListCombo->setEnabled(checked);
    ui->serverRXAudioInputCombo->setEnabled(checked);
    ui->serverTXAudioOutputCombo->setEnabled(checked);

}

void wfmain::on_lanEnableBtn_clicked(bool checked)
{
    prefs.enableLAN = checked;
    ui->connectBtn->setEnabled(true);
    ui->ipAddressTxt->setEnabled(checked);
    ui->controlPortTxt->setEnabled(checked);
    ui->usernameTxt->setEnabled(checked);
    ui->passwordTxt->setEnabled(checked);
    ui->audioRXCodecCombo->setEnabled(checked);
    ui->audioTXCodecCombo->setEnabled(checked);
    ui->audioSampleRateCombo->setEnabled(checked);
    ui->rxLatencySlider->setEnabled(checked);
    ui->txLatencySlider->setEnabled(checked);
    ui->rxLatencyValue->setEnabled(checked);
    ui->txLatencyValue->setEnabled(checked);
    ui->audioOutputCombo->setEnabled(checked);
    ui->audioInputCombo->setEnabled(checked);
    ui->baudRateCombo->setEnabled(!checked);
    ui->serialDeviceListCombo->setEnabled(!checked);
    ui->serverRXAudioInputCombo->setEnabled(!checked);
    ui->serverTXAudioOutputCombo->setEnabled(!checked);
    if(checked)
    {
        showStatusBarText("After filling in values, press Save Settings.");
    }
}

void wfmain::on_ipAddressTxt_textChanged(QString text)
{
    udpPrefs.ipAddress = text;
}

void wfmain::on_controlPortTxt_textChanged(QString text)
{
    udpPrefs.controlLANPort = text.toUInt();
}

void wfmain::on_usernameTxt_textChanged(QString text)
{
    udpPrefs.username = text;
}

void wfmain::on_passwordTxt_textChanged(QString text)
{
    udpPrefs.password = text;
}

void wfmain::on_audioDuplexCombo_currentIndexChanged(int value)
{
    udpPrefs.halfDuplex = (bool)value;
}

void wfmain::on_audioOutputCombo_currentIndexChanged(int value)
{

    if (value>=0) {
        if (prefs.audioSystem == qtAudio) {
            rxSetup.port = audioDev->getOutputDeviceInfo(value);
        }
        else {
            rxSetup.portInt = audioDev->getOutputDeviceInt(value);
        }

        rxSetup.name = audioDev->getOutputName(value);
    }
    qDebug(logGui()) << "Changed audio output to:" << rxSetup.name;
}

void wfmain::on_audioInputCombo_currentIndexChanged(int value)
{
    if (value >=0) {
        if (prefs.audioSystem == qtAudio) {
            txSetup.port = audioDev->getInputDeviceInfo(value);
        }
        else {
            txSetup.portInt = audioDev->getInputDeviceInt(value);
        }

        txSetup.name = audioDev->getInputName(value);
    }
    qDebug(logGui()) << "Changed audio input to:" << txSetup.name;
}

void wfmain::on_audioSampleRateCombo_currentIndexChanged(int value)
{
    rxSetup.sampleRate= ui->audioSampleRateCombo->itemText(value).toInt();
    txSetup.sampleRate= ui->audioSampleRateCombo->itemText(value).toInt();
}

void wfmain::on_audioRXCodecCombo_currentIndexChanged(int value)
{
    rxSetup.codec = ui->audioRXCodecCombo->itemData(value).toInt();
}

void wfmain::on_audioTXCodecCombo_currentIndexChanged(int value)
{
    txSetup.codec = ui->audioTXCodecCombo->itemData(value).toInt();
}

void wfmain::on_rxLatencySlider_valueChanged(int value)
{
    rxSetup.latency = value;
    ui->rxLatencyValue->setText(QString::number(value));
    emit sendChangeLatency(value);
}

void wfmain::on_txLatencySlider_valueChanged(int value)
{
    txSetup.latency = value;
    ui->txLatencyValue->setText(QString::number(value));
}

void wfmain::on_vspCombo_currentIndexChanged(int value) 
{
    Q_UNUSED(value);
    prefs.virtualSerialPort = ui->vspCombo->currentText();
}

void wfmain::on_toFixedBtn_clicked()
{
    int currentEdge = ui->scopeEdgeCombo->currentIndex();
    bool dialogOk = false;
    bool numOk = false;

    QStringList edges;
    edges << "1" << "2" << "3" << "4";

    QString item = QInputDialog::getItem(this, "Select Edge", "Edge to replace:", edges, currentEdge, false, &dialogOk);

    if(dialogOk)
    {
        int edge = QString(item).toInt(&numOk,10);
        if(numOk)
        {
            emit setScopeFixedEdge(oldLowerFreq, oldUpperFreq, edge);
            emit setScopeEdge(edge);
            ui->scopeEdgeCombo->blockSignals(true);
            ui->scopeEdgeCombo->setCurrentIndex(edge-1);
            ui->scopeEdgeCombo->blockSignals(false);
            issueDelayedCommand(cmdScopeFixedMode);
        }
    }
}


void wfmain::on_connectBtn_clicked()
{
    this->rigStatus->setText(""); // Clear status

    if (ui->connectBtn->text() == "Connect to Radio") {
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
    issueCmd(cmdSetSql, (unsigned char)value);
    //emit setSql((unsigned char)value);
}
// These three are from the transceiver adjustment window:
void wfmain::changeIFShift(unsigned char level)
{
    //issueCmd(cmdSetIFShift, level);
    issueCmdUniquePriority(cmdSetIFShift, level);
}
void wfmain::changeTPBFInner(unsigned char level)
{
    issueCmdUniquePriority(cmdSetTPBFInner, level);
}
void wfmain::changeTPBFOuter(unsigned char level)
{
    issueCmdUniquePriority(cmdSetTPBFOuter, level);
}

void wfmain::on_modeFilterCombo_activated(int index)
{

    int filterSelection = ui->modeFilterCombo->itemData(index).toInt();
    mode_info m;
    if(filterSelection == 99)
    {
        // TODO:
        // Bump the filter selected back to F1, F2, or F3
        // possibly track the filter in the class. Would make this easier.
        // filterSetup.show();
        //

    } else {
        unsigned char newMode = static_cast<unsigned char>(ui->modeSelectCombo->currentData().toUInt());
        currentModeIndex = newMode; // we track this for other functions
        if(ui->dataModeBtn->isChecked())
        {
            emit setDataMode(true, (unsigned char)filterSelection);
            issueDelayedCommand(cmdSetDataModeOn);
        } else {
            m.filter = (unsigned char)filterSelection;
            m.mk = (mode_kind)newMode;
            m.reg = newMode;
            issueCmd(cmdSetMode, m);

            //emit setMode(newMode, (unsigned char)filterSelection);
        }
    }

}

void wfmain::on_dataModeBtn_toggled(bool checked)
{
    emit setDataMode(checked, (unsigned char)ui->modeFilterCombo->currentData().toInt());
    usingDataMode = checked;
    if(usingDataMode)
    {
        changeModLabelAndSlider(currentModDataSrc);
    } else {
        changeModLabelAndSlider(currentModSrc);
    }
}

void wfmain::on_transmitBtn_clicked()
{
    if(!amTransmitting)
    {
        // Currently receiving
        if(!ui->pttEnableChk->isChecked())
        {
            showStatusBarText("PTT is disabled, not sending command. Change under Settings tab.");
            return;
        }

        // Are we already PTT? Not a big deal, just send again anyway.
        showStatusBarText("Sending PTT ON command. Use Control-R to receive.");
        issueCmdUniquePriority(cmdSetPTT, true);
        // send PTT
        // Start 3 minute timer
        pttTimer->start();
        issueDelayedCommand(cmdGetPTT);
        //changeTxBtn();

    } else {
        // Currently transmitting
        issueCmdUniquePriority(cmdSetPTT, false);
        pttTimer->stop();
        issueDelayedCommand(cmdGetPTT);
    }
}

void wfmain::on_adjRefBtn_clicked()
{
    cal->show();
}

void wfmain::on_satOpsBtn_clicked()
{
    sat->show();
}

void wfmain::setRadioTimeDatePrep()
{
    if(!waitingToSetTimeDate)
    {
        // 1: Find the current time and date
        QDateTime now;
        if(ui->useUTCChk->isChecked())
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

    issueCmd(cmdSetTime, timesetpoint);
    issueCmd(cmdSetDate, datesetpoint);
    issueCmd(cmdSetUTCOffset, utcsetting);
    waitingToSetTimeDate = false;
}

void wfmain::changeSliderQuietly(QSlider *slider, int value)
{
    slider->blockSignals(true);
    slider->setValue(value);
    slider->blockSignals(false);
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
}

void wfmain::receiveMicGain(unsigned char gain)
{
    processModLevel(inputMic, gain);
}

void wfmain::processModLevel(rigInput source, unsigned char level)
{
    rigInput currentIn;
    if(usingDataMode)
    {
        currentIn = currentModDataSrc;
    } else {
        currentIn = currentModSrc;
    }

    switch(source)
    {
        case inputMic:
            micGain = level;
            break;
        case inputACC:
            accGain = level;
            break;

        case inputACCA:
            accAGain = level;
            break;

        case inputACCB:
            accBGain = level;
            break;

        case inputUSB:
            usbGain = level;
            break;

        case inputLAN:
            lanGain = level;
            break;

        default:
            break;
    }
    if(currentIn == source)
    {
        changeSliderQuietly(ui->micGainSlider, level);
    }

}

void wfmain::receiveModInput(rigInput input, bool dataOn)
{
    QComboBox *box;
    QString inputName;
    bool found;
    bool foundCurrent = false;

    if(dataOn)
    {
        box = ui->modInputDataCombo;
        currentModDataSrc = input;
        if(usingDataMode)
            foundCurrent = true;
    } else {
        box = ui->modInputCombo;
        currentModSrc = input;
        if(!usingDataMode)
            foundCurrent = true;
    }

    for(int i=0; i < box->count(); i++)
    {
        if(box->itemData(i).toInt() == (int)input)
        {
            box->blockSignals(true);
            box->setCurrentIndex(i);
            box->blockSignals(false);
            found = true;
        }
    }

    if(foundCurrent)
    {
        changeModLabel(input);
    }
    if(!found)
        qInfo(logSystem()) << "Could not find modulation input: " << (int)input;
}

void wfmain::receiveACCGain(unsigned char level, unsigned char ab)
{
    if(ab==1)
    {
    processModLevel(inputACCB, level);
    } else {
        if(rigCaps.model == model7850)
        {
            processModLevel(inputACCA, level);
        } else {
            processModLevel(inputACC, level);
        }
    }
}

void wfmain::receiveUSBGain(unsigned char level)
{
    processModLevel(inputUSB, level);
}

void wfmain::receiveLANGain(unsigned char level)
{
    processModLevel(inputLAN, level);
}

void wfmain::receivePassband(quint16 pass)
{
    if (passbandWidth != (double)(pass / 1000000.0)) {
        passbandWidth = (double)(pass / 1000000.0);
        trxadj->updatePassband(pass);
        showStatusBarText(QString("IF filter width %1 Hz").arg(pass));
    }
}

void wfmain::receiveCwPitch(unsigned char pitch) {
    if (currentModeInfo.mk == modeCW || currentModeInfo.mk == modeCW_R) {
        cwPitch = round((((600.0 / 255.0) * pitch) + 300) / 5.0) * 5.0;
        passbandCenterFrequency = cwPitch / 2000000.0;
    }
    //qDebug() << "CW" << pitch << "Pitch" << cwPitch;
}

void wfmain::receiveTPBFInner(unsigned char level) {
    /*
    * This is written like this as although PBT is supposed to be sent in 25Hz steps,
    * sometimes it sends the 'nearest' value. This results in math errors.
    * In CW mode, the value is also dependant on the CW Pitch setting
    */
    qint16 shift = (qint16)(level - 128);
    double tempVar = ceil((shift / 127.0) * passbandWidth * 20000.0) / 20000.0;
    // tempVar now contains value to the nearest 50Hz If CW mode, add/remove the cwPitch.
    double pitch = 0.0;
    if ((currentModeInfo.mk == modeCW || currentModeInfo.mk == modeCW_R) && passbandWidth > 0.0006)
    {
        pitch = (600.0 - cwPitch) / 1000000.0;
    }
    TPBFInner = round((tempVar + pitch) * 200000.0) / 200000.0; // Nearest 5Hz.
    //qDebug() << "Inner" << level << "TPBFInner" << TPBFInner;
}

void wfmain::receiveTPBFOuter(unsigned char level) {
    /*
    * This is written like this as although PBT is supposed to be sent in 25Hz steps,
    * sometimes it sends the 'nearest' value. This results in math errors.
    * In CW mode, the value is also dependant on the CW Pitch setting
    */
    qint16 shift = (qint16)(level - 128);
    double tempVar = ceil((shift / 127.0) * passbandWidth * 20000.0) / 20000.0;
    // tempVar now contains value to the nearest 50Hz If CW mode, add/remove the cwPitch.
    double pitch = 0.0;
    if ((currentModeInfo.mk == modeCW || currentModeInfo.mk == modeCW_R) && passbandWidth > 0.0006)
    {
        pitch = (600.0 - cwPitch) / 1000000.0;
    }
    TPBFOuter = round((tempVar + pitch) * 200000.0) / 200000.0; // Nearest 5Hz.
    //qDebug() << "Outer" << level << "TPBFOuter" << TPBFOuter;
}


void wfmain::receiveMeter(meterKind inMeter, unsigned char level)
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
    (void)compLevel;
}

void wfmain::receiveMonitorGain(unsigned char monitorGain)
{
    (void)monitorGain;
}

void wfmain::receiveVoxGain(unsigned char voxGain)
{
    (void)voxGain;
}

void wfmain::receiveAntiVoxGain(unsigned char antiVoxGain)
{
    (void)antiVoxGain;
}

void wfmain::on_txPowerSlider_valueChanged(int value)
{
    issueCmdUniquePriority(cmdSetTxPower, (unsigned char)value);
    //emit setTxPower(value);
}

void wfmain::on_micGainSlider_valueChanged(int value)
{
    processChangingCurrentModLevel((unsigned char) value);
}

void wfmain::on_scopeRefLevelSlider_valueChanged(int value)
{
    value = (value/5) * 5; // rounded to "nearest 5"
    emit setSpectrumRefLevel(value);
}

void wfmain::receiveSpectrumRefLevel(int level)
{
    changeSliderQuietly(ui->scopeRefLevelSlider, level);
}

void wfmain::on_modInputCombo_activated(int index)
{
    emit setModInput( (rigInput)ui->modInputCombo->currentData().toInt(), false );
    currentModSrc = (rigInput)ui->modInputCombo->currentData().toInt();
    issueDelayedCommand(cmdGetCurrentModLevel);
    if(!usingDataMode)
    {
        changeModLabel(currentModSrc);
    }
    (void)index;
}

void wfmain::on_modInputDataCombo_activated(int index)
{
    emit setModInput( (rigInput)ui->modInputDataCombo->currentData().toInt(), true );
    currentModDataSrc = (rigInput)ui->modInputDataCombo->currentData().toInt();
    issueDelayedCommand(cmdGetCurrentModLevel);
    if(usingDataMode)
    {
        changeModLabel(currentModDataSrc);
    }
    (void)index;
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
    QString inputName;
    unsigned char gain = 0;

    switch(input)
    {
        case inputMic:
            inputName = "Mic";
            gain = micGain;
            break;
        case inputACC:
            inputName = "ACC";
            gain = accGain;
            break;
        case inputACCA:
            inputName = "ACCA";
            gain = accAGain;
            break;
        case inputACCB:
            inputName = "ACCB";
            gain = accBGain;
            break;
        case inputUSB:
            inputName = "USB";
            gain = usbGain;
            break;
        case inputLAN:
            inputName = "LAN";
            gain = lanGain;
            break;
        default:
            inputName = "UNK";
            gain=0;
            break;
    }
    ui->modSliderLbl->setText(inputName);
    if(updateLevel)
    {
        changeSliderQuietly(ui->micGainSlider, gain);
    }
}

void wfmain::processChangingCurrentModLevel(unsigned char level)
{
    // slider moved, so find the current mod and issue the level set command.
    issueCmd(cmdSetModLevel, level);
}

void wfmain::on_tuneLockChk_clicked(bool checked)
{
    freqLock = checked;
}

void wfmain::on_serialDeviceListCombo_textActivated(const QString &arg1)
{
    QString manualPort;
    bool ok;
    if(arg1==QString("Manual..."))
    {
        manualPort = QInputDialog::getText(this, tr("Manual port assignment"),
                                           tr("Enter serial port assignment:"),
                                           QLineEdit::Normal,
                                           tr("/dev/device"), &ok);
        if(manualPort.isEmpty() || !ok)
        {
            ui->serialDeviceListCombo->blockSignals(true);
            ui->serialDeviceListCombo->setCurrentIndex(0);
            ui->serialDeviceListCombo->blockSignals(false);
            return;
        } else {
            prefs.serialPortRadio = manualPort;
            showStatusBarText("Setting preferences to use manually-assigned serial port: " + manualPort);
            ui->serialEnableBtn->setChecked(true);
            return;
        }
    }
    if(arg1==QString("Auto"))
    {
        prefs.serialPortRadio = "auto";
        showStatusBarText("Setting preferences to automatically find rig serial port.");
        ui->serialEnableBtn->setChecked(true);
        return;
    }

    prefs.serialPortRadio = arg1;
    showStatusBarText("Setting preferences to use manually-assigned serial port: " + arg1);
    ui->serialEnableBtn->setChecked(true);
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
    unsigned char att = (unsigned char)ui->attSelCombo->itemData(index).toInt();
    emit setAttenuator(att);
    issueDelayedCommand(cmdGetPreamp);
}

void wfmain::on_preampSelCombo_activated(int index)
{
    unsigned char pre = (unsigned char)ui->preampSelCombo->itemData(index).toInt();
    emit setPreamp(pre);
    issueDelayedCommand(cmdGetAttenuator);
}

void wfmain::on_antennaSelCombo_activated(int index)
{
    unsigned char ant = (unsigned char)ui->antennaSelCombo->itemData(index).toInt();
    emit setAntenna(ant,ui->rxAntennaCheck->isChecked());
}

void wfmain::on_rxAntennaCheck_clicked(bool value)
{
    unsigned char ant = (unsigned char)ui->antennaSelCombo->itemData(ui->antennaSelCombo->currentIndex()).toInt();
    emit setAntenna(ant, value);
}
void wfmain::on_wfthemeCombo_activated(int index)
{
    colorMap->setGradient(static_cast<QCPColorGradient::GradientPreset>(ui->wfthemeCombo->itemData(index).toInt()));
    prefs.wftheme = ui->wfthemeCombo->itemData(index).toInt();
}

void wfmain::receivePreamp(unsigned char pre)
{
    int preindex = ui->preampSelCombo->findData(pre);
    ui->preampSelCombo->setCurrentIndex(preindex);
}

void wfmain::receiveAttenuator(unsigned char att)
{
    int attindex = ui->attSelCombo->findData(att);
    ui->attSelCombo->setCurrentIndex(attindex);
}

void wfmain::receiveAntennaSel(unsigned char ant, bool rx)
{
    ui->antennaSelCombo->setCurrentIndex(ant);
    ui->rxAntennaCheck->setChecked(rx);
}

void wfmain::receiveSpectrumSpan(freqt freqspan, bool isSub)
{
    if(!isSub)
    {
       switch((int)(freqspan.MHzDouble * 1000000.0))
       {
           case(2500):
               ui->scopeBWCombo->setCurrentIndex(0);
               break;
           case(5000):
               ui->scopeBWCombo->setCurrentIndex(1);
               break;
           case(10000):
               ui->scopeBWCombo->setCurrentIndex(2);
               break;
           case(25000):
               ui->scopeBWCombo->setCurrentIndex(3);
               break;
           case(50000):
               ui->scopeBWCombo->setCurrentIndex(4);
               break;
           case(100000):
               ui->scopeBWCombo->setCurrentIndex(5);
               break;
           case(250000):
               ui->scopeBWCombo->setCurrentIndex(6);
               break;
           case(500000):
               ui->scopeBWCombo->setCurrentIndex(7);
               break;
           case(1000000):
               ui->scopeBWCombo->setCurrentIndex(8);
               break;
           case(2500000):
               ui->scopeBWCombo->setCurrentIndex(9);
               break;
           default:
               qInfo(logSystem()) << __func__ << "Could not match: " << freqspan.MHzDouble << " to anything like: " << (int)(freqspan.MHzDouble*1E6);
               break;
       }

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

    if(haveRigCaps && rigCaps.hasFDcomms)
    {
        delayedCommand->setInterval( msMinTiming); // 20 byte message
    } else {
        delayedCommand->setInterval( msMinTiming * 3); // 20 byte message
    }


    qInfo(logSystem()) << "Delay command interval timing: " << delayedCommand->interval() << "ms";

    ui->pollTimeMsSpin->blockSignals(true);
    ui->pollTimeMsSpin->setValue(delayedCommand->interval());
    ui->pollTimeMsSpin->blockSignals(false);

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
    if(ui->scopeEnableWFBtn->isChecked())
    {
        issueDelayedCommand(cmdDispEnable);
        issueDelayedCommand(cmdQueNormalSpeed);
        issueDelayedCommand(cmdSpecOn);
        issueDelayedCommand(cmdStartRegularPolling); // s-meter, etc
    } else {
        issueDelayedCommand(cmdQueNormalSpeed);
        issueDelayedCommand(cmdStartRegularPolling); // s-meter, etc
    }
    delayedCommand->start();
}

void wfmain::powerRigOff()
{
    delayedCommand->stop();
    delayedCmdQue.clear();

    emit sendPowerOff();
}

void wfmain::on_ritTuneDial_valueChanged(int value)
{
    emit setRitValue(value);
}

void wfmain::on_ritEnableChk_clicked(bool checked)
{
    emit setRitEnable(checked);
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

void wfmain::setBandButtons()
{
    // Turn off each button first:
    hideButton(ui->band23cmbtn);
    hideButton(ui->band70cmbtn);
    hideButton(ui->band2mbtn);
    hideButton(ui->bandAirbtn);
    hideButton(ui->bandWFMbtn);
    hideButton(ui->band4mbtn);
    hideButton(ui->band6mbtn);

    hideButton(ui->band10mbtn);
    hideButton(ui->band12mbtn);
    hideButton(ui->band15mbtn);
    hideButton(ui->band17mbtn);
    hideButton(ui->band20mbtn);
    hideButton(ui->band30mbtn);
    hideButton(ui->band40mbtn);
    hideButton(ui->band60mbtn);
    hideButton(ui->band80mbtn);
    hideButton(ui->band160mbtn);

    hideButton(ui->band630mbtn);
    hideButton(ui->band2200mbtn);
    hideButton(ui->bandGenbtn);

    bandType bandSel;

    //for (auto band = rigCaps.bands.begin(); band != rigCaps.bands.end(); ++band) // no worky
    for(unsigned int i=0; i < rigCaps.bands.size(); i++)
    {
        bandSel = rigCaps.bands.at(i);
        switch(bandSel.band)
        {
            case(band23cm):
                showButton(ui->band23cmbtn);
                break;
            case(band70cm):
                showButton(ui->band70cmbtn);
                break;
            case(band2m):
                showButton(ui->band2mbtn);
                break;
            case(bandAir):
                showButton(ui->bandAirbtn);
                break;
            case(bandWFM):
                showButton(ui->bandWFMbtn);
                break;
            case(band4m):
                showButton(ui->band4mbtn);
                break;
            case(band6m):
                showButton(ui->band6mbtn);
                break;

            case(band10m):
                showButton(ui->band10mbtn);
                break;
            case(band12m):
                showButton(ui->band12mbtn);
                break;
            case(band15m):
                showButton(ui->band15mbtn);
                break;
            case(band17m):
                showButton(ui->band17mbtn);
                break;
            case(band20m):
                showButton(ui->band20mbtn);
                break;
            case(band30m):
                showButton(ui->band30mbtn);
                break;
            case(band40m):
                showButton(ui->band40mbtn);
                break;
            case(band60m):
                showButton(ui->band60mbtn);
                break;
            case(band80m):
                showButton(ui->band80mbtn);
                break;
            case(band160m):
                showButton(ui->band160mbtn);
                break;

            case(band630m):
                showButton(ui->band630mbtn);
                break;
            case(band2200m):
                showButton(ui->band2200mbtn);
                break;
            case(bandGen):
                showButton(ui->bandGenbtn);
                break;

            default:
                break;
        }
    }
}

void wfmain::on_rigCIVManualAddrChk_clicked(bool checked)
{
    if(checked)
    {
        ui->rigCIVaddrHexLine->setEnabled(true);
        ui->rigCIVaddrHexLine->setText(QString("%1").arg(prefs.radioCIVAddr, 2, 16));
    } else {
        ui->rigCIVaddrHexLine->setText("auto");
        ui->rigCIVaddrHexLine->setEnabled(false);
        prefs.radioCIVAddr = 0; // auto
        showStatusBarText("Setting radio CI-V address to: 'auto'. Make sure CI-V Transceive is enabled on the radio.");
    }
}

void wfmain::on_rigCIVaddrHexLine_editingFinished()
{
    bool okconvert=false;

    unsigned char propCIVAddr = (unsigned char) ui->rigCIVaddrHexLine->text().toUInt(&okconvert, 16);

    if(okconvert && (propCIVAddr < 0xe0) && (propCIVAddr != 0))
    {
        prefs.radioCIVAddr = propCIVAddr;
        emit setCIVAddr(propCIVAddr);
        showStatusBarText(QString("Setting radio CI-V address to: 0x%1. Press Save Settings to retain.").arg(propCIVAddr, 2, 16));
    } else {
        showStatusBarText(QString("Could not use provided CI-V address. Address must be < 0xE0"));
    }

}

void wfmain::on_baudRateCombo_activated(int index)
{
    bool ok = false;
    quint32 baud = ui->baudRateCombo->currentData().toUInt(&ok);
    if(ok)
    {
        prefs.serialPortBaud = baud;
        serverConfig.baudRate = baud;
        showStatusBarText(QString("Changed baud rate to %1 bps. Press Save Settings to retain.").arg(baud));
    }
    (void)index;
}

void wfmain::on_useRTSforPTTchk_clicked(bool checked)
{
    emit setRTSforPTT(checked);
    prefs.forceRTSasPTT = checked;
}

void wfmain::on_wfLengthSlider_valueChanged(int value)
{
    prefs.wflength = (unsigned int)(value);
    prepareWf(value);
}

void wfmain::on_wfAntiAliasChk_clicked(bool checked)
{
    colorMap->setAntialiased(checked);
    prefs.wfAntiAlias = checked;
}

void wfmain::on_wfInterpolateChk_clicked(bool checked)
{
    colorMap->setInterpolate(checked);
    prefs.wfInterpolate = checked;
}

cmds wfmain::meterKindToMeterCommand(meterKind m)
{
    cmds c;
    switch(m)
    {
        case meterNone:
            c = cmdNone;
            break;
        case meterS:
            c = cmdGetSMeter;
            break;
        case meterCenter:
            c = cmdGetCenterMeter;
            break;
        case meterPower:
            c = cmdGetPowerMeter;
            break;
        case meterSWR:
            c = cmdGetSWRMeter;
            break;
        case meterALC:
            c = cmdGetALCMeter;
            break;
        case meterComp:
            c = cmdGetCompMeter;
            break;
        case meterCurrent:
            c = cmdGetIdMeter;
            break;
        case meterVoltage:
            c = cmdGetVdMeter;
            break;
        default:
            c = cmdNone;
            break;
    }

    return c;
}


void wfmain::on_meter2selectionCombo_activated(int index)
{
    meterKind newMeterType;
    meterKind oldMeterType;
    newMeterType = static_cast<meterKind>(ui->meter2selectionCombo->currentData().toInt());
    oldMeterType = ui->meter2Widget->getMeterType();
    if(newMeterType == oldMeterType)
        return;

    cmds newCmd = meterKindToMeterCommand(newMeterType);
    cmds oldCmd = meterKindToMeterCommand(oldMeterType);

    removePeriodicCommand(oldCmd);

    if(newMeterType==meterNone)
    {
        ui->meter2Widget->hide();
        ui->meter2Widget->setMeterType(newMeterType);
    } else {
        ui->meter2Widget->show();
        ui->meter2Widget->setMeterType(newMeterType);
        if((newMeterType!=meterRxAudio) && (newMeterType!=meterTxMod) && (newMeterType!=meterAudio))
            insertPeriodicCommandUnique(newCmd);
    }
    prefs.meter2Type = newMeterType;

    (void)index;
}

void wfmain::on_enableRigctldChk_clicked(bool checked)
{
    if (rigCtl != Q_NULLPTR)
    {
        rigCtl->disconnect();
        delete rigCtl;
        rigCtl = Q_NULLPTR;
    }

    if (checked) {
        // Start rigctld
        rigCtl = new rigCtlD(this);
        rigCtl->startServer(prefs.rigCtlPort);
        connect(this, SIGNAL(sendRigCaps(rigCapabilities)), rigCtl, SLOT(receiveRigCaps(rigCapabilities)));
        if (rig != Q_NULLPTR) {
            // We are already connected to a rig.
            connect(rig, SIGNAL(stateInfo(rigstate*)), rigCtl, SLOT(receiveStateInfo(rigstate*)));
            connect(rigCtl, SIGNAL(stateUpdated()), rig, SLOT(stateUpdated()));

            emit sendRigCaps(rigCaps);
            emit requestRigState();
        }
    }    
    prefs.enableRigCtlD = checked;
}

void wfmain::on_rigctldPortTxt_editingFinished()
{

    bool okconvert = false;
    unsigned int port = ui->rigctldPortTxt->text().toUInt(&okconvert);
    if (okconvert)
    {
        prefs.rigCtlPort = port;
    }
}

void wfmain::on_tcpServerPortTxt_editingFinished()
{

    bool okconvert = false;
    unsigned int port = ui->tcpServerPortTxt->text().toUInt(&okconvert);
    if (okconvert)
    {
        prefs.tcpPort = port;
    }
}

void wfmain::on_waterfallFormatCombo_activated(int index)
{
    prefs.waterfallFormat = index;
}

void wfmain::on_moreControlsBtn_clicked()
{
    if(trxadj->isMinimized())
    {
        trxadj->raise();
        trxadj->activateWindow();
        return;
    }
    trxadj->show();
    trxadj->raise();
    trxadj->activateWindow();
}

void wfmain::on_useCIVasRigIDChk_clicked(bool checked)
{
    prefs.CIVisRadioModel = checked;
}

void wfmain::receiveStateInfo(rigstate* state)
{
    qInfo("Setting rig state for wfmain");
    rigState = state;
}

void wfmain::on_setClockBtn_clicked()
{
    setRadioTimeDatePrep();
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
    qInfo() << "Looking for inputs";
    ui->audioInputCombo->blockSignals(true);
    ui->audioInputCombo->clear();
    ui->audioInputCombo->addItems(audioDev->getInputs());
    ui->audioInputCombo->setCurrentIndex(-1);
    ui->audioInputCombo->setStyleSheet(QString("QComboBox QAbstractItemView {min-width: %1px;}").arg(audioDev->getNumCharsIn() + 30));
    ui->audioInputCombo->blockSignals(false);
    ui->audioInputCombo->setCurrentIndex(audioDev->findInput("Client", txSetup.name));

    qInfo() << "Looking for outputs";
    ui->audioOutputCombo->blockSignals(true);
    ui->audioOutputCombo->clear();
    ui->audioOutputCombo->addItems(audioDev->getOutputs());
    ui->audioOutputCombo->setCurrentIndex(-1);
    ui->audioOutputCombo->setStyleSheet(QString("QComboBox QAbstractItemView {min-width: %1px;}").arg(audioDev->getNumCharsOut() + 30));
    ui->audioOutputCombo->blockSignals(false);
    ui->audioOutputCombo->setCurrentIndex(audioDev->findOutput("Client", rxSetup.name));

    ui->serverTXAudioOutputCombo->blockSignals(true);
    ui->serverTXAudioOutputCombo->clear();
    ui->serverTXAudioOutputCombo->addItems(audioDev->getOutputs());
    ui->serverTXAudioOutputCombo->setCurrentIndex(-1);
    ui->serverTXAudioOutputCombo->setStyleSheet(QString("QComboBox QAbstractItemView {min-width: %1px;}").arg(audioDev->getNumCharsOut() + 30));
    ui->serverTXAudioOutputCombo->blockSignals(false);

    ui->serverRXAudioInputCombo->blockSignals(true);
    ui->serverRXAudioInputCombo->clear();
    ui->serverRXAudioInputCombo->addItems(audioDev->getInputs());
    ui->serverRXAudioInputCombo->setCurrentIndex(-1);
    ui->serverRXAudioInputCombo->setStyleSheet(QString("QComboBox QAbstractItemView {min-width: %1px;}").arg(audioDev->getNumCharsIn()+30));
    ui->serverRXAudioInputCombo->blockSignals(false);

    rxSetup.type = prefs.audioSystem;
    txSetup.type = prefs.audioSystem;

    if (!serverConfig.rigs.isEmpty())
    {
        serverConfig.rigs.first()->rxAudioSetup.type = prefs.audioSystem;
        serverConfig.rigs.first()->txAudioSetup.type = prefs.audioSystem;

        ui->serverRXAudioInputCombo->setCurrentIndex(audioDev->findInput("Server", serverConfig.rigs.first()->rxAudioSetup.name));
        ui->serverTXAudioOutputCombo->setCurrentIndex(audioDev->findOutput("Server", serverConfig.rigs.first()->txAudioSetup.name));
    }


    qDebug(logSystem()) << "Audio devices done.";
}

void wfmain::on_audioSystemCombo_currentIndexChanged(int value) 
{
    prefs.audioSystem = static_cast<audioType>(value);
    audioDev->setAudioType(prefs.audioSystem);
    audioDev->enumerate();
    ui->audioSystemServerCombo->blockSignals(true);
    ui->audioSystemServerCombo->setCurrentIndex(value);
    ui->audioSystemServerCombo->blockSignals(false);
}

void wfmain::on_audioSystemServerCombo_currentIndexChanged(int value)
{
    prefs.audioSystem = static_cast<audioType>(value);
    audioDev->setAudioType(prefs.audioSystem);
    audioDev->enumerate();
    ui->audioSystemCombo->blockSignals(true);
    ui->audioSystemCombo->setCurrentIndex(value);
    ui->audioSystemCombo->blockSignals(false);
}

void wfmain::on_topLevelSlider_valueChanged(int value)
{
    wfCeiling = value;
    plotCeiling = value;
    prefs.plotCeiling = value;
    plot->yAxis->setRange(QCPRange(plotFloor, plotCeiling));
    colorMap->setDataRange(QCPRange(wfFloor, wfCeiling));
}

void wfmain::on_botLevelSlider_valueChanged(int value)
{
    wfFloor = value;
    plotFloor = value;
    prefs.plotFloor = value;
    plot->yAxis->setRange(QCPRange(plotFloor, plotCeiling));
    colorMap->setDataRange(QCPRange(wfFloor, wfCeiling));
}

void wfmain::on_underlayBufferSlider_valueChanged(int value)
{
    resizePlasmaBuffer(value);
    prefs.underlayBufferSize = value;
    spectrumPlasmaSize = value;
}

void wfmain::resizePlasmaBuffer(int newSize)
{

    QByteArray empty((int)spectWidth, '\x01');
    plasmaMutex.lock();

    int oldSize = spectrumPlasma.size();

    if(oldSize < newSize)
    {
        spectrumPlasma.resize(newSize);
        for(int p=oldSize; p < newSize; p++)
        {
            spectrumPlasma.append(empty);
        }
    } else if (oldSize > newSize){
        for(int p = oldSize; p > newSize; p--)
        {
            spectrumPlasma.pop_back();
        }
    }

    spectrumPlasma.squeeze();
    plasmaMutex.unlock();
}

void wfmain::clearPlasmaBuffer()
{
    QByteArray empty((int)spectWidth, '\x01');
    plasmaMutex.lock();
    int pSize = spectrumPlasma.size();
    for(int i=0; i < pSize; i++)
    {
        spectrumPlasma[i] = empty;
    }
    plasmaMutex.unlock();
}

void wfmain::on_underlayNone_toggled(bool checked)
{
    ui->underlayBufferSlider->setDisabled(checked);
    if(checked)
    {
        underlayMode = underlayNone;
        prefs.underlayMode = underlayMode;
        on_clearPeakBtn_clicked();
    }
}

void wfmain::on_underlayPeakHold_toggled(bool checked)
{
    ui->underlayBufferSlider->setDisabled(checked);
    if(checked)
    {
        underlayMode = underlayPeakHold;
        prefs.underlayMode = underlayMode;
        on_clearPeakBtn_clicked();
    }
}

void wfmain::on_underlayPeakBuffer_toggled(bool checked)
{
    ui->underlayBufferSlider->setDisabled(!checked);
    if(checked)
    {
        underlayMode = underlayPeakBuffer;
        prefs.underlayMode = underlayMode;
    }
}

void wfmain::on_underlayAverageBuffer_toggled(bool checked)
{
    ui->underlayBufferSlider->setDisabled(!checked);
    if(checked)
    {
        underlayMode = underlayAverageBuffer;
        prefs.underlayMode = underlayMode;
    }
}

// --- DEBUG FUNCTION ---
void wfmain::on_debugBtn_clicked()
{
    qInfo(logSystem()) << "Debug button pressed.";
    qDebug(logSystem()) << "Query for repeater duplex offset 0x0C headed out";
    issueDelayedCommand(cmdGetRptDuplexOffset);
}

// ----------   color helper functions:   ---------- //

void wfmain::setColorElement(QColor color,
                             QLedLabel *led,
                             QLabel *label,
                             QLineEdit *lineText)
{
    if(led != Q_NULLPTR)
    {
        led->setColor(color, true);
    }
    if(label != Q_NULLPTR)
    {
        label->setText(color.name(QColor::HexArgb));
    }
    if(lineText != Q_NULLPTR)
    {
        lineText->setText(color.name(QColor::HexArgb));
    }
}

void wfmain::setColorElement(QColor color, QLedLabel *led, QLabel *label)
{
    setColorElement(color, led, label, Q_NULLPTR);
}

void wfmain::setColorElement(QColor color, QLedLabel *led, QLineEdit *lineText)
{
    setColorElement(color, led, Q_NULLPTR, lineText);
}

QColor wfmain::getColorFromPicker(QColor initialColor)
{
    QColorDialog::ColorDialogOptions options;
    options.setFlag(QColorDialog::ShowAlphaChannel, true);
    options.setFlag(QColorDialog::DontUseNativeDialog, true);
    QColor selColor = QColorDialog::getColor(initialColor, this, "Select Color", options);
    int alphaVal = 0;
    bool ok = false;

    if(selColor.isValid())
    {
        if(selColor.alpha() == 0)
        {
            alphaVal = QInputDialog::getInt(this, tr("Specify Opacity"),
                                            tr("You specified an opacity value of 0. \nDo you want to change it? (0=transparent, 255=opaque)"), 0, 0, 255, 1,
                                            &ok);
            if(!ok)
            {
                return selColor;
            } else {
                selColor.setAlpha(alphaVal);
                return selColor;
            }
        }
        return selColor;
    }
    else
        return initialColor;
}

void wfmain::getSetColor(QLedLabel *led, QLabel *label)
{
    QColor selColor = getColorFromPicker(led->getColor());
    setColorElement(selColor, led, label);
}

void wfmain::getSetColor(QLedLabel *led, QLineEdit *line)
{
    QColor selColor = getColorFromPicker(led->getColor());
    setColorElement(selColor, led, line);
}

QString wfmain::setColorFromString(QString colorstr, QLedLabel *led)
{
    if(led==Q_NULLPTR)
        return "ERROR";

    if(!colorstr.startsWith("#"))
    {
        colorstr.prepend("#");
    }
    if(colorstr.length() != 9)
    {
        // TODO: Tell user about AA RR GG BB
        return led->getColor().name(QColor::HexArgb);
    }
    led->setColor(colorstr, true);
    return led->getColor().name(QColor::HexArgb);
}

void wfmain::useCurrentColorPreset()
{
    int pos = ui->colorPresetCombo->currentIndex();
    useColorPreset(&colorPreset[pos]);
}

void wfmain::useColorPreset(colorPrefsType *cp)
{
    // Apply the given preset to the UI elements
    // prototyped from setPlotTheme()
    if(cp == Q_NULLPTR)
        return;

    //qInfo(logSystem()) << "Setting plots to color preset number " << cp->presetNum << ", with name " << *(cp->presetName);

    plot->setBackground(cp->plotBackground);

    plot->xAxis->grid()->setPen(cp->gridColor);
    plot->yAxis->grid()->setPen(cp->gridColor);

    plot->legend->setTextColor(cp->textColor);
    plot->legend->setBorderPen(cp->gridColor);
    plot->legend->setBrush(cp->gridColor);

    plot->xAxis->setTickLabelColor(cp->textColor);
    plot->xAxis->setLabelColor(cp->gridColor);
    plot->yAxis->setTickLabelColor(cp->textColor);
    plot->yAxis->setLabelColor(cp->gridColor);

    plot->xAxis->setBasePen(cp->axisColor);
    plot->xAxis->setTickPen(cp->axisColor);
    plot->yAxis->setBasePen(cp->axisColor);
    plot->yAxis->setTickPen(cp->axisColor);

    freqIndicatorLine->setPen(QPen(cp->tuningLine));

    passbandIndicator->setPen(QPen(cp->passband));
    passbandIndicator->setBrush(QBrush(cp->passband));

    pbtIndicator->setPen(QPen(cp->pbt));
    pbtIndicator->setBrush(QBrush(cp->pbt));

    plot->graph(0)->setPen(QPen(cp->spectrumLine));
    plot->graph(0)->setBrush(QBrush(cp->spectrumFill));

    plot->graph(1)->setPen(QPen(cp->underlayLine));
    plot->graph(1)->setBrush(QBrush(cp->underlayFill));

    wf->yAxis->setBasePen(cp->wfAxis);
    wf->yAxis->setTickPen(cp->wfAxis);
    wf->xAxis->setBasePen(cp->wfAxis);
    wf->xAxis->setTickPen(cp->wfAxis);

    wf->xAxis->setLabelColor(cp->wfGrid);
    wf->yAxis->setLabelColor(cp->wfGrid);

    wf->xAxis->setTickLabelColor(cp->wfText);
    wf->yAxis->setTickLabelColor(cp->wfText);

    wf->setBackground(cp->wfBackground);

    ui->meterSPoWidget->setColors(cp->meterLevel, cp->meterPeakScale, cp->meterPeakLevel, cp->meterAverage, cp->meterLowerLine, cp->meterLowText);
    ui->meter2Widget->setColors(cp->meterLevel, cp->meterPeakScale, cp->meterPeakLevel, cp->meterAverage, cp->meterLowerLine, cp->meterLowText);

    clusterColor = cp->clusterSpots;
}

void wfmain::setColorButtonOperations(QColor *colorStore,
                            QLineEdit *e, QLedLabel *d)
{
    // Call this function with a pointer into the colorPreset color you
    // wish to edit.

    if(colorStore==Q_NULLPTR)
    {
        qInfo(logSystem()) << "ERROR, invalid pointer to color received.";
        return;
    }
    getSetColor(d, e);
    QColor t = d->getColor();
    colorStore->setNamedColor(t.name(QColor::HexArgb));
    useCurrentColorPreset();
}

void wfmain::setColorLineEditOperations(QColor *colorStore,
                                QLineEdit *e, QLedLabel *d)
{
    // Call this function with a pointer into the colorPreset color you
    // wish to edit.
    if(colorStore==Q_NULLPTR)
    {
        qInfo(logSystem()) << "ERROR, invalid pointer to color received.";
        return;
    }

    QString colorStrValidated = setColorFromString(e->text(), d);
    e->setText(colorStrValidated);
    colorStore->setNamedColor(colorStrValidated);
    useCurrentColorPreset();
}

void wfmain::on_colorPopOutBtn_clicked()
{

    if (settingsTabisAttached)
    {
        settingsTab = ui->tabWidget->currentWidget();
        ui->tabWidget->removeTab(ui->tabWidget->indexOf(settingsTab));
        settingsWidgetTab->addTab(settingsTab, "Settings");
        settingsWidgetWindow->show();
        ui->colorPopOutBtn->setText("Re-attach");
        ui->clusterPopOutBtn->setText("Re-attach");
        ui->tabWidget->setCurrentIndex(0);
        settingsTabisAttached = false;
    }
    else {
        settingsTab = settingsWidgetTab->currentWidget();

        settingsWidgetTab->removeTab(settingsWidgetTab->indexOf(settingsTab));
        ui->tabWidget->addTab(settingsTab, "Settings");
        settingsWidgetWindow->close();

        ui->colorPopOutBtn->setText("Pop-Out");
        ui->clusterPopOutBtn->setText("Pop-Out");
        ui->tabWidget->setCurrentIndex(3);
        settingsTabisAttached = true;
    }
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

void wfmain::setEditAndLedFromColor(QColor c, QLineEdit *e, QLedLabel *d)
{
    bool blockSignals = true;
    if(e != Q_NULLPTR)
    {
        e->blockSignals(blockSignals);
        e->setText(c.name(QColor::HexArgb));
        e->blockSignals(false);
    }
    if(d != Q_NULLPTR)
    {
        d->setColor(c);
    }
}

void wfmain::loadColorPresetToUIandPlots(int presetNumber)
{
    if(presetNumber >= numColorPresetsTotal)
    {
        qDebug(logSystem()) << "WARNING: asked for preset number [" << presetNumber << "], which is out of range.";
        return;
    }

    colorPrefsType p = colorPreset[presetNumber];
    //qInfo(logSystem()) << "color preset number [" << presetNumber << "] requested for UI load, which has internal index of [" << p.presetNum << "]";
    setEditAndLedFromColor(p.gridColor, ui->colorEditGrid, ui->colorSwatchGrid);
    setEditAndLedFromColor(p.axisColor, ui->colorEditAxis, ui->colorSwatchAxis);
    setEditAndLedFromColor(p.textColor, ui->colorEditText, ui->colorSwatchText);
    setEditAndLedFromColor(p.spectrumLine, ui->colorEditSpecLine, ui->colorSwatchSpecLine);
    setEditAndLedFromColor(p.spectrumFill, ui->colorEditSpecFill, ui->colorSwatchSpecFill);
    setEditAndLedFromColor(p.underlayLine, ui->colorEditUnderlayLine, ui->colorSwatchUnderlayLine);
    setEditAndLedFromColor(p.underlayFill, ui->colorEditUnderlayFill, ui->colorSwatchUnderlayFill);
    setEditAndLedFromColor(p.plotBackground, ui->colorEditPlotBackground, ui->colorSwatchPlotBackground);
    setEditAndLedFromColor(p.tuningLine, ui->colorEditTuningLine, ui->colorSwatchTuningLine);
    setEditAndLedFromColor(p.passband, ui->colorEditPassband, ui->colorSwatchPassband);

    setEditAndLedFromColor(p.meterLevel, ui->colorEditMeterLevel, ui->colorSwatchMeterLevel);
    setEditAndLedFromColor(p.meterAverage, ui->colorEditMeterAvg, ui->colorSwatchMeterAverage);
    setEditAndLedFromColor(p.meterPeakLevel, ui->colorEditMeterPeakLevel, ui->colorSwatchMeterPeakLevel);
    setEditAndLedFromColor(p.meterPeakScale, ui->colorEditMeterPeakScale, ui->colorSwatchMeterPeakScale);
    setEditAndLedFromColor(p.meterLowerLine, ui->colorEditMeterScale, ui->colorSwatchMeterScale);
    setEditAndLedFromColor(p.meterLowText, ui->colorEditMeterText, ui->colorSwatchMeterText);

    setEditAndLedFromColor(p.wfBackground, ui->colorEditWfBackground, ui->colorSwatchWfBackground);
    setEditAndLedFromColor(p.wfGrid, ui->colorEditWfGrid, ui->colorSwatchWfGrid);
    setEditAndLedFromColor(p.wfAxis, ui->colorEditWfAxis, ui->colorSwatchWfAxis);
    setEditAndLedFromColor(p.wfText, ui->colorEditWfText, ui->colorSwatchWfText);

    setEditAndLedFromColor(p.clusterSpots, ui->colorEditClusterSpots, ui->colorSwatchClusterSpots);

    useColorPreset(&p);
}

void wfmain::on_colorRenamePresetBtn_clicked()
{
    int p = ui->colorPresetCombo->currentIndex();
    QString newName;
    QMessageBox msgBox;

    bool ok = false;
    newName = QInputDialog::getText(this, tr("Rename Preset"),
                                    tr("Preset Name (10 characters max):"), QLineEdit::Normal,
                                    ui->colorPresetCombo->currentText(), &ok);
    if(!ok)
        return;

    if(ok && (newName.length() < 11) && !newName.isEmpty())
    {
        colorPreset[p].presetName->clear();
        colorPreset[p].presetName->append(newName);
        ui->colorPresetCombo->setItemText(p, *(colorPreset[p].presetName));
    } else {
        if(newName.isEmpty() || (newName.length() > 10))
        {
            msgBox.setText("Error, name must be at least one character and not exceed 10 characters.");
            msgBox.exec();
        }
    }
}

void wfmain::on_colorPresetCombo_currentIndexChanged(int index)
{
    prefs.currentColorPresetNumber = index;
    loadColorPresetToUIandPlots(index);
}

void wfmain::on_colorRevertPresetBtn_clicked()
{
    int pn = ui->colorPresetCombo->currentIndex();
    setDefaultColors(pn);
    loadColorPresetToUIandPlots(pn);
}

// ---------- end color helper functions ---------- //

// ----------       Color UI slots        ----------//

// Grid:
void wfmain::on_colorSetBtnGrid_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].gridColor);
    setColorButtonOperations(c, ui->colorEditGrid, ui->colorSwatchGrid);
}
void wfmain::on_colorEditGrid_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].gridColor);
    setColorLineEditOperations(c, ui->colorEditGrid, ui->colorSwatchGrid);
}

// Axis:
void wfmain::on_colorSetBtnAxis_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].axisColor);
    setColorButtonOperations(c, ui->colorEditAxis, ui->colorSwatchAxis);
}
void wfmain::on_colorEditAxis_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].axisColor);
    setColorLineEditOperations(c, ui->colorEditAxis, ui->colorSwatchAxis);
}

// Text:
void wfmain::on_colorSetBtnText_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].textColor);
    setColorButtonOperations(c, ui->colorEditText, ui->colorSwatchText);
}
void wfmain::on_colorEditText_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].textColor);
    setColorLineEditOperations(c, ui->colorEditText, ui->colorSwatchText);
}

// SpecLine:
void wfmain::on_colorEditSpecLine_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].spectrumLine);
    setColorLineEditOperations(c, ui->colorEditSpecLine, ui->colorSwatchSpecLine);
}
void wfmain::on_colorSetBtnSpecLine_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].spectrumLine);
    setColorButtonOperations(c, ui->colorEditSpecLine, ui->colorSwatchSpecLine);
}

// SpecFill:
void wfmain::on_colorSetBtnSpecFill_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].spectrumFill);
    setColorButtonOperations(c, ui->colorEditSpecFill, ui->colorSwatchSpecFill);
}
void wfmain::on_colorEditSpecFill_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].spectrumFill);
    setColorLineEditOperations(c, ui->colorEditSpecFill, ui->colorSwatchSpecFill);
}

// PlotBackground:
void wfmain::on_colorEditPlotBackground_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].plotBackground);
    setColorLineEditOperations(c, ui->colorEditPlotBackground, ui->colorSwatchPlotBackground);
}
void wfmain::on_colorSetBtnPlotBackground_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].plotBackground);
    setColorButtonOperations(c, ui->colorEditPlotBackground, ui->colorSwatchPlotBackground);
}

// Underlay Line:
void wfmain::on_colorSetBtnUnderlayLine_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].underlayLine);
    setColorButtonOperations(c, ui->colorEditUnderlayLine, ui->colorSwatchUnderlayLine);
}

void wfmain::on_colorEditUnderlayLine_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].underlayLine);
    setColorLineEditOperations(c, ui->colorEditUnderlayLine, ui->colorSwatchUnderlayLine);
}

// Underlay Fill:
void wfmain::on_colorSetBtnUnderlayFill_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].underlayFill);
    setColorButtonOperations(c, ui->colorEditUnderlayFill, ui->colorSwatchUnderlayFill);
}
void wfmain::on_colorEditUnderlayFill_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].underlayFill);
    setColorLineEditOperations(c, ui->colorEditUnderlayFill, ui->colorSwatchUnderlayFill);
}

// WF Background:
void wfmain::on_colorSetBtnwfBackground_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].wfBackground);
    setColorButtonOperations(c, ui->colorEditWfBackground, ui->colorSwatchWfBackground);
}
void wfmain::on_colorEditWfBackground_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].wfBackground);
    setColorLineEditOperations(c, ui->colorEditWfBackground, ui->colorSwatchWfBackground);
}

// WF Grid:
void wfmain::on_colorSetBtnWfGrid_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].wfGrid);
    setColorButtonOperations(c, ui->colorEditWfGrid, ui->colorSwatchWfGrid);
}
void wfmain::on_colorEditWfGrid_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].wfGrid);
    setColorLineEditOperations(c, ui->colorEditWfGrid, ui->colorSwatchWfGrid);
}

// WF Axis:
void wfmain::on_colorSetBtnWfAxis_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].wfAxis);
    setColorButtonOperations(c, ui->colorEditWfAxis, ui->colorSwatchWfAxis);
}
void wfmain::on_colorEditWfAxis_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].wfAxis);
    setColorLineEditOperations(c, ui->colorEditWfAxis, ui->colorSwatchWfAxis);
}

// WF Text:
void wfmain::on_colorSetBtnWfText_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor* c = &(colorPreset[pos].wfText);
    setColorButtonOperations(c, ui->colorEditWfText, ui->colorSwatchWfText);
}

void wfmain::on_colorEditWfText_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor* c = &(colorPreset[pos].wfText);
    setColorLineEditOperations(c, ui->colorEditWfText, ui->colorSwatchWfText);
}

// Tuning Line:
void wfmain::on_colorSetBtnTuningLine_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor* c = &(colorPreset[pos].tuningLine);
    setColorButtonOperations(c, ui->colorEditTuningLine, ui->colorSwatchTuningLine);
}
void wfmain::on_colorEditTuningLine_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor* c = &(colorPreset[pos].tuningLine);
    setColorLineEditOperations(c, ui->colorEditTuningLine, ui->colorSwatchTuningLine);
}

// Passband:
void wfmain::on_colorSetBtnPassband_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor* c = &(colorPreset[pos].passband);
    setColorButtonOperations(c, ui->colorEditPassband, ui->colorSwatchPassband);
}
void wfmain::on_colorEditPassband_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor* c = &(colorPreset[pos].passband);
    setColorLineEditOperations(c, ui->colorEditPassband, ui->colorSwatchPassband);
}

// Meter Level:
void wfmain::on_colorSetBtnMeterLevel_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterLevel);
    setColorButtonOperations(c, ui->colorEditMeterLevel, ui->colorSwatchMeterLevel);
}
void wfmain::on_colorEditMeterLevel_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterLevel);
    setColorLineEditOperations(c, ui->colorEditMeterLevel, ui->colorSwatchMeterLevel);
}

// Meter Average:
void wfmain::on_colorSetBtnMeterAvg_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterAverage);
    setColorButtonOperations(c, ui->colorEditMeterAvg, ui->colorSwatchMeterAverage);
}
void wfmain::on_colorEditMeterAvg_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterAverage);
    setColorLineEditOperations(c, ui->colorEditMeterAvg, ui->colorSwatchMeterAverage);
}

// Meter Peak Level:
void wfmain::on_colorSetBtnMeterPeakLevel_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterPeakLevel);
    setColorButtonOperations(c, ui->colorEditMeterPeakLevel, ui->colorSwatchMeterPeakLevel);
}
void wfmain::on_colorEditMeterPeakLevel_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterPeakLevel);
    setColorLineEditOperations(c, ui->colorEditMeterPeakLevel, ui->colorSwatchMeterPeakLevel);
}

// Meter Peak Scale:
void wfmain::on_colorSetBtnMeterPeakScale_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterPeakScale);
    setColorButtonOperations(c, ui->colorEditMeterPeakScale, ui->colorSwatchMeterPeakScale);
}
void wfmain::on_colorEditMeterPeakScale_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterPeakScale);
    setColorLineEditOperations(c, ui->colorEditMeterPeakScale, ui->colorSwatchMeterPeakScale);
}

// Meter Scale (line):
void wfmain::on_colorSetBtnMeterScale_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterLowerLine);
    setColorButtonOperations(c, ui->colorEditMeterScale, ui->colorSwatchMeterScale);
}
void wfmain::on_colorEditMeterScale_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterLowerLine);
    setColorLineEditOperations(c, ui->colorEditMeterScale, ui->colorSwatchMeterScale);
}

// Meter Text:
void wfmain::on_colorSetBtnMeterText_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterLowText);
    setColorButtonOperations(c, ui->colorEditMeterText, ui->colorSwatchMeterText);
}
void wfmain::on_colorEditMeterText_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor *c = &(colorPreset[pos].meterLowText);
    setColorLineEditOperations(c, ui->colorEditMeterText, ui->colorSwatchMeterText);
}

// Cluster Spots:
void wfmain::on_colorSetBtnClusterSpots_clicked()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor* c = &(colorPreset[pos].clusterSpots);
    setColorButtonOperations(c, ui->colorEditClusterSpots, ui->colorSwatchClusterSpots);
}
void wfmain::on_colorEditClusterSpots_editingFinished()
{
    int pos = ui->colorPresetCombo->currentIndex();
    QColor* c = &(colorPreset[pos].clusterSpots);
    setColorLineEditOperations(c, ui->colorEditClusterSpots, ui->colorSwatchClusterSpots);
}

// ----------   End color UI slots        ----------//

void wfmain::on_colorSavePresetBtn_clicked()
{
    int pn = ui->colorPresetCombo->currentIndex();

    settings->beginGroup("ColorPresets");
    settings->setValue("currentColorPresetNumber", prefs.currentColorPresetNumber);
    settings->beginWriteArray("ColorPreset", numColorPresetsTotal);

    colorPrefsType *p;
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

    settings->endArray();
    settings->endGroup();
    settings->sync();
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

void wfmain::on_customEdgeBtn_clicked()
{
    double lowFreq = oldLowerFreq;
    double highFreq = oldUpperFreq;
    QString freqstring = QString("%1, %2").arg(lowFreq).arg(highFreq);
    bool ok;

    QString userFreq = QInputDialog::getText(this, "Scope Edges",
                          "Please enter desired scope edges, in MHz, \
with a comma between the low and high range.",
    QLineEdit::Normal, freqstring, &ok);
    if(!ok)
        return;

    QString clean = userFreq.trimmed().replace(" ", "");
    QStringList freqs = clean.split(",");
    if(freqs.length() == 2)
    {
        lowFreq = QString(freqs.at(0)).toDouble(&ok);
        if(ok)
        {
            highFreq = QString(freqs.at(1)).toDouble(&ok);
            if(ok)
            {
                qDebug(logGui()) << "setting edge to: " << lowFreq << ", " << highFreq << ", edge num: " << ui->scopeEdgeCombo->currentIndex() + 1;
                emit setScopeFixedEdge(lowFreq, highFreq, ui->scopeEdgeCombo->currentIndex() + 1);
                return;
            }
        }
        goto errMsg;
    } else {
        goto errMsg;
    }

errMsg:
    {
        QMessageBox URLmsgBox;
        URLmsgBox.setText("Error, could not interpret your input.\
                          <br/>Please make sure to place a comma between the frequencies.\
                          <br/>For example: '7.200, 7.300'");
        URLmsgBox.exec();

        return;
    }


}


void wfmain::receiveClusterOutput(QString text) {
    ui->clusterOutputTextEdit->moveCursor(QTextCursor::End);
    ui->clusterOutputTextEdit->insertPlainText(text);
    ui->clusterOutputTextEdit->moveCursor(QTextCursor::End);
}

void wfmain::on_clusterUdpEnable_clicked(bool enable)
{
    prefs.clusterUdpEnable = enable;
    emit setClusterEnableUdp(enable);
}

void wfmain::on_clusterTcpEnable_clicked(bool enable)
{
    prefs.clusterTcpEnable = enable;
    emit setClusterEnableTcp(enable);
}

void wfmain::on_clusterUdpPortLineEdit_editingFinished()
{
    prefs.clusterUdpPort = ui->clusterUdpPortLineEdit->text().toInt();
    emit setClusterUdpPort(prefs.clusterUdpPort);
}

void wfmain::on_clusterServerNameCombo_currentIndexChanged(int index) 
{
    if (index < 0)
        return;

    QString text = ui->clusterServerNameCombo->currentText();

    if (clusters.size() <= index)
    {
        qInfo(logGui) << "Adding Cluster server" << text;
        clusterSettings c;
        c.server = text;
        clusters.append(c);
        ui->clusterTcpPortLineEdit->setEnabled(true);
        ui->clusterUsernameLineEdit->setEnabled(true);
        ui->clusterPasswordLineEdit->setEnabled(true);
        ui->clusterTimeoutLineEdit->setEnabled(true);

    }
    else {
        qInfo(logGui) << "Editing Cluster server" << text;
        clusters[index].server = text;
    }
    ui->clusterUsernameLineEdit->blockSignals(true);
    ui->clusterPasswordLineEdit->blockSignals(true);
    ui->clusterTimeoutLineEdit->blockSignals(true);
    ui->clusterTcpPortLineEdit->setText(QString::number(clusters[index].port));
    ui->clusterUsernameLineEdit->setText(clusters[index].userName);
    ui->clusterPasswordLineEdit->setText(clusters[index].password);
    ui->clusterTimeoutLineEdit->setText(QString::number(clusters[index].timeout));
    ui->clusterUsernameLineEdit->blockSignals(false);
    ui->clusterPasswordLineEdit->blockSignals(false);
    ui->clusterTimeoutLineEdit->blockSignals(false);


    for (int i = 0; i < clusters.size(); i++) {
        if (i == index)
            clusters[i].isdefault = true;
        else
            clusters[i].isdefault = false;
    }

    emit setClusterServerName(clusters[index].server);
    emit setClusterTcpPort(clusters[index].port);
    emit setClusterUserName(clusters[index].userName);
    emit setClusterPassword(clusters[index].password);
    emit setClusterTimeout(clusters[index].timeout);

}

void wfmain::on_clusterServerNameCombo_currentTextChanged(QString text)
{
    if (text.isEmpty()) {
        int index = ui->clusterServerNameCombo->currentIndex();
        ui->clusterServerNameCombo->removeItem(index);
        clusters.removeAt(index);
        if (clusters.size() == 0)
        {
            ui->clusterTcpPortLineEdit->setEnabled(false);
            ui->clusterUsernameLineEdit->setEnabled(false);
            ui->clusterPasswordLineEdit->setEnabled(false);
            ui->clusterTimeoutLineEdit->setEnabled(false);
        }
    }
}

void wfmain::on_clusterTcpPortLineEdit_editingFinished()
{
    int index = ui->clusterServerNameCombo->currentIndex();
    if (index < clusters.size())
    {
        clusters[index].port = ui->clusterTcpPortLineEdit->displayText().toInt();
        emit setClusterTcpPort(clusters[index].port);
    }
}

void wfmain::on_clusterUsernameLineEdit_editingFinished()
{
    int index = ui->clusterServerNameCombo->currentIndex();
    if (index < clusters.size())
    {
        clusters[index].userName = ui->clusterUsernameLineEdit->text();
        emit setClusterUserName(clusters[index].userName);
    }
}

void wfmain::on_clusterPasswordLineEdit_editingFinished()
{
    int index = ui->clusterServerNameCombo->currentIndex();
    if (index < clusters.size())
    {
        clusters[index].password = ui->clusterPasswordLineEdit->text();
        emit setClusterPassword(clusters[index].password);
    }

}

void wfmain::on_clusterTimeoutLineEdit_editingFinished()
{
    int index = ui->clusterServerNameCombo->currentIndex();
    if (index < clusters.size())
    {
        clusters[index].timeout = ui->clusterTimeoutLineEdit->displayText().toInt();
        emit setClusterTimeout(clusters[index].timeout);
    }
}


void wfmain::receiveSpots(QList<spotData> spots)
{
    //QElapsedTimer timer;
    //timer.start();

    bool current = false;
    
    if (clusterSpots.size() > 0) {
        current=clusterSpots.begin().value()->current;
    }

    foreach(spotData s, spots)
    {
        bool found = false;
        QMap<QString, spotData*>::iterator spot = clusterSpots.find(s.dxcall);

        while (spot != clusterSpots.end() && spot.key() == s.dxcall && spot.value()->frequency == s.frequency) {
            spot.value()->current = !current;
            found = true;
            ++spot;
        }

        if (!found)
        {
            spotData* sp = new spotData(s);

            //qDebug(logCluster()) << "ADD:" << sp->dxcall;
            sp->current = !current;
            bool conflict = true;
            double left = sp->frequency;
            QCPRange range=plot->yAxis->range();
            double top = range.upper-15.0;
            sp->text = new QCPItemText(plot);
            sp->text->setAntialiased(true);
            sp->text->setColor(clusterColor);
            sp->text->setText(sp->dxcall);
            sp->text->setFont(QFont(font().family(), 10));
            sp->text->setPositionAlignment(Qt::AlignVCenter | Qt::AlignHCenter);
            sp->text->position->setType(QCPItemPosition::ptPlotCoords);
            sp->text->setSelectable(true);
            QMargins margin;
            int width = (sp->text->right - sp->text->left) / 2;
            margin.setLeft(width);
            margin.setRight(width);
            sp->text->setPadding(margin);
            sp->text->position->setCoords(left, top);
            sp->text->setVisible(false);
            while (conflict) {
#if QCUSTOMPLOT_VERSION < 0x020000
                QCPAbstractItem* item = plot->itemAt(sp->text->position->pixelPoint(), true);
#else
                QCPAbstractItem* item = plot->itemAt(sp->text->position->pixelPosition(), true);
#endif

                QCPItemText* textItem = dynamic_cast<QCPItemText*> (item);
                if (textItem != nullptr && sp->text != textItem) {
                    top = top - 5.0;
                }
                else {
                    conflict = false;
                }
                sp->text->position->setCoords(left, top);
            }

            sp->text->setVisible(true);
            clusterSpots.insert(sp->dxcall, sp);
        }
    }

    QMap<QString, spotData*>::iterator spot2 = clusterSpots.begin();
    while (spot2 != clusterSpots.end()) {
        if (spot2.value()->current == current) {
            plot->removeItem(spot2.value()->text);
            //qDebug(logCluster()) << "REMOVE:" << spot2.value()->dxcall;
            delete spot2.value(); // Stop memory leak?
            spot2 = clusterSpots.erase(spot2);
        }
        else {
            ++spot2;
        }

    }

    //qDebug(logCluster()) << "Processing took" << timer.nsecsElapsed() / 1000 << "us";
}

void wfmain::on_clusterPopOutBtn_clicked()
{

    if (settingsTabisAttached)
    {
        settingsTab = ui->tabWidget->currentWidget();
        ui->tabWidget->removeTab(ui->tabWidget->indexOf(settingsTab));
        settingsWidgetTab->addTab(settingsTab, "Settings");
        settingsWidgetWindow->show();
        ui->clusterPopOutBtn->setText("Re-attach");
        ui->colorPopOutBtn->setText("Re-attach");
        ui->tabWidget->setCurrentIndex(0);
        settingsTabisAttached = false;
    }
    else {
        settingsTab = settingsWidgetTab->currentWidget();

        settingsWidgetTab->removeTab(settingsWidgetTab->indexOf(settingsTab));
        ui->tabWidget->addTab(settingsTab, "Settings");
        settingsWidgetWindow->close();

        ui->clusterPopOutBtn->setText("Pop-Out");
        ui->colorPopOutBtn->setText("Pop-Out");
        ui->tabWidget->setCurrentIndex(3);
        settingsTabisAttached = true;
    }
}

void wfmain::on_clusterSkimmerSpotsEnable_clicked(bool enable)
{
    prefs.clusterSkimmerSpotsEnable = enable;
    emit setClusterSkimmerSpots(enable);
}

void wfmain::on_clickDragTuningEnableChk_clicked(bool checked)
{
    prefs.clickDragTuningEnable = checked;
}

void wfmain::on_usbControllerBtn_clicked()
{
    if (shut != Q_NULLPTR) {
        if (shut->isVisible()) {
            shut->hide();
        }
        else {
            shut->show();
            shut->raise();
        }
    }
}

void wfmain::on_autoPollBtn_clicked(bool checked)
{
    ui->pollTimeMsSpin->setEnabled(!checked);
    if(checked)
    {
        prefs.polling_ms = 0;
        qInfo(logSystem()) << "User set radio polling interval to automatic.";
        calculateTimingParameters();
    }
}

void wfmain::on_manualPollBtn_clicked(bool checked)
{
    ui->pollTimeMsSpin->setEnabled(checked);
    if(checked)
    {
        prefs.polling_ms = ui->pollTimeMsSpin->value();
        changePollTiming(prefs.polling_ms);
    }
}

void wfmain::on_pollTimeMsSpin_valueChanged(int timing_ms)
{
    if(ui->manualPollBtn->isChecked())
    {
        changePollTiming(timing_ms);
    }
}

void wfmain::changePollTiming(int timing_ms, bool setUI)
{
    delayedCommand->setInterval(timing_ms);
    qInfo(logSystem()) << "User changed radio polling interval to " << timing_ms << "ms.";
    showStatusBarText("User changed radio polling interval to " + QString("%1").arg(timing_ms) + "ms.");
    prefs.polling_ms = timing_ms;
    if(setUI)
    {
        ui->pollTimeMsSpin->blockSignals(true);
        ui->pollTimeMsSpin->setValue(timing_ms);
        ui->pollTimeMsSpin->blockSignals(false);
    }
}

void wfmain::connectionHandler(bool connect) 
{

    if (!connect) {
        emit sendCloseComm();
        ui->connectBtn->setText("Connect to Radio");
        ui->audioSystemCombo->setEnabled(true);
        ui->audioSystemServerCombo->setEnabled(true);
        haveRigCaps = false;
        rigName->setText("NONE");
    }
    else
    {
        emit sendCloseComm(); // Just in case there is a failed connection open.
        if (ui->connectBtn->text() != "Cancel connection") {
            openRig();
        }
        else {
            ui->connectBtn->setText("Connect to Radio");
            ui->audioSystemCombo->setEnabled(true);
            ui->audioSystemServerCombo->setEnabled(true);
        }
    }
}

void wfmain::on_autoSSBchk_clicked(bool checked)
{
    prefs.automaticSidebandSwitching = checked;
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
