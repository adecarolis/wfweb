#include "wfmain.h"
#include "ui_wfmain.h"

#include "commhandler.h"
#include "rigidentities.h"
#include "logcategories.h"

// This code is copyright 2017-2020 Elliott H. Liggett
// All rights reserved

wfmain::wfmain(const QString serialPortCL, const QString hostCL, const QString settingsFile, QWidget *parent ) :
    QMainWindow(parent),
    ui(new Ui::wfmain)
{
    QGuiApplication::setApplicationDisplayName("wfview");
    QGuiApplication::setApplicationName(QString("wfview"));

    setWindowIcon(QIcon( QString(":resources/wfview.png")));
    ui->setupUi(this);

    setWindowTitle(QString("wfview"));

    this->serialPortCL = serialPortCL;
    this->hostCL = hostCL;

    cal = new calibrationWindow();
    rpt = new repeaterSetup();
    sat = new satelliteSetup();
    trxadj = new transceiverAdjustments();
    srv = new udpServerSetup();
    abtBox = new aboutbox();

    connect(this, SIGNAL(sendServerConfig(SERVERCONFIG)), srv, SLOT(receiveServerConfig(SERVERCONFIG)));
    connect(srv, SIGNAL(serverConfig(SERVERCONFIG, bool)), this, SLOT(serverConfigRequested(SERVERCONFIG, bool)));
       
    qRegisterMetaType<udpPreferences>(); // Needs to be registered early.
    qRegisterMetaType<rigCapabilities>();
    qRegisterMetaType<duplexMode>();
    qRegisterMetaType<rptAccessTxRx>();
    qRegisterMetaType<rigInput>();
    qRegisterMetaType<meterKind>();
    qRegisterMetaType<spectrumMode>();
    qRegisterMetaType<freqt>();
    qRegisterMetaType<mode_info>();
    qRegisterMetaType<audioPacket>();
    qRegisterMetaType <audioSetup>();
    qRegisterMetaType <timekind>();
    qRegisterMetaType <datekind>();


    haveRigCaps = false;

    setupKeyShortcuts();

    setupMainUI();

    setSerialDevicesUI();

    setAudioDevicesUI();

    setDefaultColors();
    setDefPrefs();

    getSettingsFilePath(settingsFile);

    setupPlots();
    loadSettings(); // Look for saved preferences
    setTuningSteps(); // TODO: Combine into preferences

    setUIToPrefs();

    setServerToPrefs();

    setInitialTiming();

    openRig();

    rigConnections();

    if (serverConfig.enabled && udp != Q_NULLPTR) {
        // Server
        connect(rig, SIGNAL(haveAudioData(audioPacket)), udp, SLOT(receiveAudioData(audioPacket)));
        connect(rig, SIGNAL(haveDataForServer(QByteArray)), udp, SLOT(dataForServer(QByteArray)));
        connect(udp, SIGNAL(haveDataFromServer(QByteArray)), rig, SLOT(dataFromServer(QByteArray)));
    }

    amTransmitting = false;

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
    if (rigCtl != Q_NULLPTR) {
        delete rigCtl;
    }
    delete rpt;
    delete ui;
    delete settings;
}

void wfmain::closeEvent(QCloseEvent *event)
{
    // Are you sure?
    if (!prefs.confirmExit) {
        QApplication::exit();
    }
    QCheckBox *cb = new QCheckBox("Don't ask me again");
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


    // TODO: Use these if they are found
    if(!serialPortCL.isEmpty())
    {
        qDebug(logSystem()) << "Serial port specified by user: " << serialPortCL;
    } else {
        qDebug(logSystem()) << "Serial port not specified. ";
    }

    if(!hostCL.isEmpty())
    {
        qDebug(logSystem()) << "Remote host name specified by user: " << hostCL;
    }


    makeRig();

    if (prefs.enableLAN)
    {
        ui->lanEnableBtn->setChecked(true);
        usingLAN = true;
        // We need to setup the tx/rx audio:
        emit sendCommSetup(prefs.radioCIVAddr, udpPrefs, rxSetup, txSetup, prefs.virtualSerialPort);
    } else {
        ui->serialEnableBtn->setChecked(true);
        if( (prefs.serialPortRadio.toLower() == QString("auto")) && (serialPortCL.isEmpty()))
        {
            findSerialPort();

        } else {
            if(serialPortCL.isEmpty())
            {
                serialPortRig = prefs.serialPortRadio;
            } else {
                serialPortRig = serialPortCL;
            }
        }
        usingLAN = false;
        emit sendCommSetup(prefs.radioCIVAddr, serialPortRig, prefs.serialPortBaud,prefs.virtualSerialPort);
        ui->statusBar->showMessage(QString("Connecting to rig using serial port ").append(serialPortRig), 1000);
    }



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

    connect(rpt, SIGNAL(getDuplexMode()), rig, SLOT(getDuplexMode()));
    connect(rpt, SIGNAL(setDuplexMode(duplexMode)), rig, SLOT(setDuplexMode(duplexMode)));
    connect(rig, SIGNAL(haveDuplexMode(duplexMode)), rpt, SLOT(receiveDuplexMode(duplexMode)));
    connect(rpt, SIGNAL(getTone()), rig, SLOT(getTone()));
    connect(rpt, SIGNAL(getTSQL()), rig, SLOT(getTSQL()));
    connect(rpt, SIGNAL(getDTCS()), rig, SLOT(getDTCS()));
    connect(rpt, SIGNAL(setTone(quint16)), rig, SLOT(setTone(quint16)));
    connect(rpt, SIGNAL(setTSQL(quint16)), rig, SLOT(setTSQL(quint16)));
    connect(rpt, SIGNAL(setDTCS(quint16,bool,bool)), rig, SLOT(setDTCS(quint16,bool,bool)));
    connect(rpt, SIGNAL(getRptAccessMode()), rig, SLOT(getRptAccessMode()));
    connect(rpt, SIGNAL(setRptAccessMode(rptAccessTxRx)), rig, SLOT(setRptAccessMode(rptAccessTxRx)));
    connect(rig, SIGNAL(haveTone(quint16)), rpt, SLOT(handleTone(quint16)));
    connect(rig, SIGNAL(haveTSQL(quint16)), rpt, SLOT(handleTSQL(quint16)));
    connect(rig, SIGNAL(haveDTCS(quint16,bool,bool)), rpt, SLOT(handleDTCS(quint16,bool,bool)));
    connect(rig, SIGNAL(haveRptAccessMode(rptAccessTxRx)), rpt, SLOT(handleRptAccessMode(rptAccessTxRx)));


    connect(this, SIGNAL(getDuplexMode()), rig, SLOT(getDuplexMode()));
    connect(this, SIGNAL(getTone()), rig, SLOT(getTone()));
    connect(this, SIGNAL(getTSQL()), rig, SLOT(getTSQL()));
    connect(this, SIGNAL(getRptAccessMode()), rig, SLOT(getRptAccessMode()));
    //connect(this, SIGNAL(setDuplexMode(duplexMode)), rig, SLOT(setDuplexMode(duplexMode)));
    //connect(rig, SIGNAL(haveDuplexMode(duplexMode)), this, SLOT(receiveDuplexMode(duplexMode)));

    connect(this, SIGNAL(getModInput(bool)), rig, SLOT(getModInput(bool)));
    connect(rig, SIGNAL(haveModInput(rigInput,bool)), this, SLOT(receiveModInput(rigInput, bool)));
    connect(this, SIGNAL(setModInput(rigInput, bool)), rig, SLOT(setModInput(rigInput,bool)));

    connect(rig, SIGNAL(haveSpectrumData(QByteArray, double, double)), this, SLOT(receiveSpectrumData(QByteArray, double, double)));
    connect(rig, SIGNAL(haveSpectrumMode(spectrumMode)), this, SLOT(receiveSpectrumMode(spectrumMode)));
    connect(this, SIGNAL(setScopeMode(spectrumMode)), rig, SLOT(setSpectrumMode(spectrumMode)));
    connect(this, SIGNAL(getScopeMode()), rig, SLOT(getScopeMode()));

    connect(this, SIGNAL(setFrequency(freqt)), rig, SLOT(setFrequency(freqt)));
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
    connect(this, SIGNAL(getTxPower()), rig, SLOT(getTxLevel()));
    connect(this, SIGNAL(getMicGain()), rig, SLOT(getMicGain()));
    connect(this, SIGNAL(getSpectrumRefLevel()), rig, SLOT(getSpectrumRefLevel()));
    connect(this, SIGNAL(getModInputLevel(rigInput)), rig, SLOT(getModInputLevel(rigInput)));


    // Levels: Set:
    connect(this, SIGNAL(setRfGain(unsigned char)), rig, SLOT(setRfGain(unsigned char)));
    connect(this, SIGNAL(setAfGain(unsigned char)), rig, SLOT(setAfGain(unsigned char)));
    connect(this, SIGNAL(setSql(unsigned char)), rig, SLOT(setSquelch(unsigned char)));
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

        // Thread:
        rig->moveToThread(rigThread);
        connect(rigThread, SIGNAL(started()), rig, SLOT(process()));
        connect(rigThread, SIGNAL(finished()), rig, SLOT(deleteLater()));
        rigThread->start();

        // Rig status and Errors:
        connect(rig, SIGNAL(haveSerialPortError(QString, QString)), this, SLOT(receiveSerialPortError(QString, QString)));
        connect(rig, SIGNAL(haveStatusUpdate(QString)), this, SLOT(receiveStatusUpdate(QString)));

        // Rig comm setup:
        connect(this, SIGNAL(sendCommSetup(unsigned char, udpPreferences, audioSetup, audioSetup, QString)), rig, SLOT(commSetup(unsigned char, udpPreferences, audioSetup, audioSetup, QString)));
        connect(this, SIGNAL(sendCommSetup(unsigned char, QString, quint32,QString)), rig, SLOT(commSetup(unsigned char, QString, quint32,QString)));


        connect(rig, SIGNAL(haveBaudRate(quint32)), this, SLOT(receiveBaudRate(quint32)));

        connect(this, SIGNAL(sendCloseComm()), rig, SLOT(closeComm()));
        connect(this, SIGNAL(sendChangeLatency(quint16)), rig, SLOT(changeLatency(quint16)));
        connect(this, SIGNAL(getRigCIV()), rig, SLOT(findRigs()));
        connect(rig, SIGNAL(discoveredRigID(rigCapabilities)), this, SLOT(receiveFoundRigID(rigCapabilities)));
        connect(rig, SIGNAL(commReady()), this, SLOT(receiveCommReady()));

        if (rigCtl != Q_NULLPTR) {
            connect(rig, SIGNAL(stateInfo(rigStateStruct*)), rigCtl, SLOT(receiveStateInfo(rigStateStruct*)));
            connect(this, SIGNAL(requestRigState()), rig, SLOT(sendState()));
            connect(rigCtl, SIGNAL(setFrequency(freqt)), rig, SLOT(setFrequency(freqt)));
            connect(rigCtl, SIGNAL(setMode(unsigned char, unsigned char)), rig, SLOT(setMode(unsigned char, unsigned char)));
            connect(rigCtl, SIGNAL(setDataMode(bool, unsigned char)), rig, SLOT(setDataMode(bool, unsigned char)));
            connect(rigCtl, SIGNAL(setPTT(bool)), rig, SLOT(setPTT(bool)));
            connect(rigCtl, SIGNAL(sendPowerOn()), rig, SLOT(powerOn()));
            connect(rigCtl, SIGNAL(sendPowerOff()), rig, SLOT(powerOff()));

            // Levels: Set:
            connect(rigCtl, SIGNAL(setRfGain(unsigned char)), rig, SLOT(setRfGain(unsigned char)));
            connect(rigCtl, SIGNAL(setAfGain(unsigned char)), rig, SLOT(setAfGain(unsigned char)));
            connect(rigCtl, SIGNAL(setSql(unsigned char)), rig, SLOT(setSquelch(unsigned char)));
            connect(rigCtl, SIGNAL(setTxPower(unsigned char)), rig, SLOT(setTxPower(unsigned char)));
            connect(rigCtl, SIGNAL(setMicGain(unsigned char)), rig, SLOT(setMicGain(unsigned char)));
            connect(rigCtl, SIGNAL(setMonitorLevel(unsigned char)), rig, SLOT(setMonitorLevel(unsigned char)));
            connect(rigCtl, SIGNAL(setVoxGain(unsigned char)), rig, SLOT(setVoxGain(unsigned char)));
            connect(rigCtl, SIGNAL(setAntiVoxGain(unsigned char)), rig, SLOT(setAntiVoxGain(unsigned char)));
            connect(rigCtl, SIGNAL(setSpectrumRefLevel(int)), rig, SLOT(setSpectrumRefLevel(int)));

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
        emit getRigID();
        getInitialRigState();
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

void wfmain::receiveSerialPortError(QString port, QString errorText)
{
    qInfo(logSystem()) << "wfmain: received serial port error for port: " << port << " with message: " << errorText;
    ui->statusBar->showMessage(QString("ERROR: using port ").append(port).append(": ").append(errorText), 10000);

    // TODO: Dialog box, exit, etc
}

void wfmain::receiveStatusUpdate(QString text)
{
    this->rigStatus->setText(text);
}

void wfmain::setupPlots()
{

// Line 290--
    spectrumDrawLock = true;
    plot = ui->plot; // rename it waterfall.

    wf = ui->waterfall;

    freqIndicatorLine = new QCPItemLine(plot);
    freqIndicatorLine->setAntialiased(true);
    freqIndicatorLine->setPen(QPen(Qt::blue));
//

    ui->plot->addGraph(); // primary
    ui->plot->addGraph(0, 0); // secondary, peaks, same axis as first?
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

    freqIndicatorLine->start->setCoords(0.5,0);
    freqIndicatorLine->end->setCoords(0.5,160);

    // Plot user interaction
    connect(plot, SIGNAL(mouseDoubleClick(QMouseEvent*)), this, SLOT(handlePlotDoubleClick(QMouseEvent*)));
    connect(wf, SIGNAL(mouseDoubleClick(QMouseEvent*)), this, SLOT(handleWFDoubleClick(QMouseEvent*)));
    connect(plot, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(handlePlotClick(QMouseEvent*)));
    connect(wf, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(handleWFClick(QMouseEvent*)));
    connect(wf, SIGNAL(mouseWheel(QWheelEvent*)), this, SLOT(handleWFScroll(QWheelEvent*)));
    connect(plot, SIGNAL(mouseWheel(QWheelEvent*)), this, SLOT(handlePlotScroll(QWheelEvent*)));
    spectrumDrawLock = false;
}

void wfmain::setupMainUI()
{
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
    ui->tuningStepCombo->addItem("1 kHz",     (unsigned int)    1000);
    ui->tuningStepCombo->addItem("2.5 kHz",   (unsigned int)    2500);
    ui->tuningStepCombo->addItem("5 kHz",     (unsigned int)    5000);
    ui->tuningStepCombo->addItem("6.125 kHz", (unsigned int)    6125);	// PMR
    ui->tuningStepCombo->addItem("8.333 kHz", (unsigned int)    8333);	// airband stepsize
    ui->tuningStepCombo->addItem("9 kHz",     (unsigned int)    9000);	// European medium wave stepsize
    ui->tuningStepCombo->addItem("10 kHz",    (unsigned int)   10000);
    ui->tuningStepCombo->addItem("12.5 kHz",  (unsigned int)   12500);
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
    ui->meter2Widget->hide();

#ifdef QT_DEBUG
    // Experimental feature:
    ui->meter2selectionCombo->show();
    ui->secondaryMeterSelectionLabel->show();
#else
    ui->meter2selectionCombo->hide();
    ui->secondaryMeterSelectionLabel->hide();
#endif

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
    ui->statusBar->addPermanentWidget(rigName);
    rigName->setText("NONE");
    rigName->setFixedWidth(50);

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

}

void wfmain::updateSizes(int tabIndex)
{
    if(!haveRigCaps)
        return;
    // This function does nothing unless you are using a rig without spectrum.
    // This is a hack. It is not great, but it seems to work ok.
    if(!rigCaps.hasSpectrum)
    {
        // Set "ignore" size policy for non-selected tabs:
        for(int i=0;i<ui->tabWidget->count();i++)
            if((i!=tabIndex) && tabIndex != 0)
                ui->tabWidget->widget(i)->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored); // allows size to be any size that fits the tab bar

        if(tabIndex==0 && !rigCaps.hasSpectrum)
        {

            ui->tabWidget->widget(0)->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
            ui->tabWidget->widget(0)->setMaximumSize(ui->tabWidget->widget(0)->minimumSizeHint());
            ui->tabWidget->widget(0)->adjustSize(); // tab
            this->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
            this->setMaximumSize(QSize(929, 270));
            this->setMinimumSize(QSize(929, 270));

            resize(minimumSize());
            adjustSize(); // main window
            adjustSize();

        } else if(tabIndex==0 && rigCaps.hasSpectrum) {
            // At main tab (0) and we have spectrum:
            ui->tabWidget->widget(0)->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);

            resize(minimumSizeHint());
            adjustSize(); // Without this call, the window retains the size of the previous tab.
        } else {
            // At some other tab, with or without spectrum:
            ui->tabWidget->widget(tabIndex)->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
            this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            this->setMinimumSize(QSize(994, 455)); // not large enough for settings tab
            this->setMaximumSize(QSize(65535,65535));
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

        qInfo(logSystem()) << "Loading settings from:" << path + file;
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

    // TODO: Remove this:
//    periodicPollingTimer = new QTimer(this);
//    periodicPollingTimer->setInterval(10);
//    periodicPollingTimer->setSingleShot(false);
    //connect(periodicPollingTimer, SIGNAL(timeout()), this, SLOT(sendRadioCommandLoop()));

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
    if (serverConfig.enabled) {
        serverConfig.lan = prefs.enableLAN;

        udp = new udpServer(serverConfig,rxSetup,txSetup);

        serverThread = new QThread(this);

        udp->moveToThread(serverThread);

        connect(this, SIGNAL(initServer()), udp, SLOT(init()));
        connect(serverThread, SIGNAL(finished()), udp, SLOT(deleteLater()));

        if (!prefs.enableLAN && udp != Q_NULLPTR) {
            connect(udp, SIGNAL(haveNetworkStatus(QString)), this, SLOT(receiveStatusUpdate(QString)));
        }

        serverThread->start();

        emit initServer();

        connect(this, SIGNAL(sendRigCaps(rigCapabilities)), udp, SLOT(receiveRigCaps(rigCapabilities)));

    }
}

void wfmain::setUIToPrefs()
{
    ui->fullScreenChk->setChecked(prefs.useFullScreen);
    on_fullScreenChk_clicked(prefs.useFullScreen);

    ui->useDarkThemeChk->setChecked(prefs.useDarkMode);
    on_useDarkThemeChk_clicked(prefs.useDarkMode);

    ui->useSystemThemeChk->setChecked(prefs.useSystemTheme);
    on_useSystemThemeChk_clicked(prefs.useSystemTheme);

    ui->drawPeakChk->setChecked(prefs.drawPeaks);
    on_drawPeakChk_clicked(prefs.drawPeaks);
    drawPeaks = prefs.drawPeaks;

    ui->wfAntiAliasChk->setChecked(prefs.wfAntiAlias);
    on_wfAntiAliasChk_clicked(prefs.wfAntiAlias);

    ui->wfInterpolateChk->setChecked(prefs.wfInterpolate);
    on_wfInterpolateChk_clicked(prefs.wfInterpolate);

    ui->wfLengthSlider->setValue(prefs.wflength);
    prepareWf(prefs.wflength);

    ui->wfthemeCombo->setCurrentIndex(ui->wfthemeCombo->findData(prefs.wftheme));
    colorMap->setGradient(static_cast<QCPColorGradient::GradientPreset>(prefs.wftheme));
}

void wfmain::setAudioDevicesUI()
{

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

    for (unsigned int i = 1; i < devices; i++) {
        info = audio->getDeviceInfo(i);
        if (info.outputChannels > 0) {
            qInfo(logAudio()) << (info.isDefaultOutput ? "*" : " ") << "(" << i << ") Output Device : " << QString::fromStdString(info.name);
            ui->audioOutputCombo->addItem(QString::fromStdString(info.name), i);
        }
        if (info.inputChannels > 0) {
            qInfo(logAudio()) << (info.isDefaultInput ? "*" : " ") << "(" << i << ") Input Device  : " << QString::fromStdString(info.name);
            ui->audioInputCombo->addItem(QString::fromStdString(info.name), i);
        }
    }

    delete audio;

#elif defined(PORTAUDIO)
    // Use PortAudio device enumeration
#else

// If no external library is configured, use QTMultimedia
    // Enumerate audio devices, need to do before settings are loaded.
    const auto audioOutputs = QAudioDeviceInfo::availableDevices(QAudio::AudioOutput);
    for (const QAudioDeviceInfo& deviceInfo : audioOutputs) {
        ui->audioOutputCombo->addItem(deviceInfo.deviceName(), QVariant::fromValue(deviceInfo));
    }

    const auto audioInputs = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
    for (const QAudioDeviceInfo& deviceInfo : audioInputs) {
        ui->audioInputCombo->addItem(deviceInfo.deviceName(), QVariant::fromValue(deviceInfo));
    }
    // Set these to default audio devices initially.
    rxSetup.port = QAudioDeviceInfo::defaultOutputDevice();
    txSetup.port = QAudioDeviceInfo::defaultInputDevice();
#endif
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
        ui->serialDeviceListCombo->addItem(QString("/dev/")+serialPortInfo.portName(), i++);
#else
        ui->serialDeviceListCombo->addItem(serialPortInfo.portName(), i++);
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
    QString vspName=QDir::homePath()+"/rig-pty";
#endif
    for (i=1;i<8;i++) {
        ui->vspCombo->addItem(vspName + QString::number(i));

        if (QFile::exists(vspName+QString::number(i))) {
            auto * model = qobject_cast<QStandardItemModel*>(ui->vspCombo->model());
            auto *item = model->item(ui->vspCombo->count()-1);
            item->setEnabled(false);
        }
    }
    ui->vspCombo->addItem(vspName+QString::number(i));
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

    keyDebug = new QShortcut(this);
    keyDebug->setKey(Qt::CTRL + Qt::SHIFT + Qt::Key_D);
    connect(keyDebug, SIGNAL(activated()), this, SLOT(on_debugBtn_clicked()));
}

void wfmain::setDefPrefs()
{
    defPrefs.useFullScreen = false;
    defPrefs.useDarkMode = true;
    defPrefs.useSystemTheme = false;
    defPrefs.drawPeaks = true;
    defPrefs.wfAntiAlias = false;
    defPrefs.wfInterpolate = true;
    defPrefs.stylesheetPath = QString("qdarkstyle/style.qss");
    defPrefs.radioCIVAddr = 0x00; // previously was 0x94 for 7300.
    defPrefs.serialPortRadio = QString("auto");
    defPrefs.serialPortBaud = 115200;
    defPrefs.enablePTT = false;
    defPrefs.niceTS = true;
    defPrefs.enableRigCtlD = false;
    defPrefs.rigCtlPort = 4533;
    defPrefs.virtualSerialPort = QString("none");
    defPrefs.localAFgain = 255;
    defPrefs.wflength = 160;
    defPrefs.wftheme = static_cast<int>(QCPColorGradient::gpJet);
    defPrefs.confirmExit = true;
    defPrefs.confirmPowerOff = true;

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

    // Basic things to load:
    // UI: (full screen, dark theme, draw peaks, colors, etc)
    settings->beginGroup("Interface");
    prefs.useFullScreen = settings->value("UseFullScreen", defPrefs.useFullScreen).toBool();
    prefs.useDarkMode = settings->value("UseDarkMode", defPrefs.useDarkMode).toBool();
    prefs.useSystemTheme = settings->value("UseSystemTheme", defPrefs.useSystemTheme).toBool();
    prefs.wftheme = settings->value("WFTheme", defPrefs.wftheme).toInt();
    prefs.drawPeaks = settings->value("DrawPeaks", defPrefs.drawPeaks).toBool();
    prefs.wfAntiAlias = settings->value("WFAntiAlias", defPrefs.wfAntiAlias).toBool();
    prefs.wfInterpolate = settings->value("WFInterpolate", defPrefs.wfInterpolate).toBool();
    prefs.wflength = (unsigned int) settings->value("WFLength", defPrefs.wflength).toInt();
    prefs.stylesheetPath = settings->value("StylesheetPath", defPrefs.stylesheetPath).toString();
    ui->splitter->restoreState(settings->value("splitter").toByteArray());

    restoreGeometry(settings->value("windowGeometry").toByteArray());
    restoreState(settings->value("windowState").toByteArray());
    setWindowState(Qt::WindowActive); // Works around QT bug to returns window+keyboard focus.
    prefs.confirmExit = settings->value("ConfirmExit", defPrefs.confirmExit).toBool();
    prefs.confirmPowerOff = settings->value("ConfirmPowerOff", defPrefs.confirmPowerOff).toBool();

    settings->endGroup();

    // Load color schemes:
    // Per this bug: https://forum.qt.io/topic/24725/solved-qvariant-will-drop-alpha-value-when-save-qcolor/5
    // the alpha channel is dropped when converting raw qvariant of QColor. Therefore, we are storing as unsigned int and converting back.

    settings->beginGroup("DarkColors");
    prefs.colorScheme.Dark_PlotBackground = QColor::fromRgba(settings->value("Dark_PlotBackground", defaultColors.Dark_PlotBackground.rgba()).toUInt());
    prefs.colorScheme.Dark_PlotAxisPen = QColor::fromRgba(settings->value("Dark_PlotAxisPen", defaultColors.Dark_PlotAxisPen.rgba()).toUInt());

    prefs.colorScheme.Dark_PlotLegendTextColor = QColor::fromRgba(settings->value("Dark_PlotLegendTextColor", defaultColors.Dark_PlotLegendTextColor.rgba()).toUInt());
    prefs.colorScheme.Dark_PlotLegendBorderPen = QColor::fromRgba(settings->value("Dark_PlotLegendBorderPen", defaultColors.Dark_PlotLegendBorderPen.rgba()).toUInt());
    prefs.colorScheme.Dark_PlotLegendBrush = QColor::fromRgba(settings->value("Dark_PlotLegendBrush", defaultColors.Dark_PlotLegendBrush.rgba()).toUInt());

    prefs.colorScheme.Dark_PlotTickLabel = QColor::fromRgba(settings->value("Dark_PlotTickLabel", defaultColors.Dark_PlotTickLabel.rgba()).toUInt());
    prefs.colorScheme.Dark_PlotBasePen = QColor::fromRgba(settings->value("Dark_PlotBasePen", defaultColors.Dark_PlotBasePen.rgba()).toUInt());
    prefs.colorScheme.Dark_PlotTickPen = QColor::fromRgba(settings->value("Dark_PlotTickPen", defaultColors.Dark_PlotTickPen.rgba()).toUInt());

    prefs.colorScheme.Dark_PeakPlotLine = QColor::fromRgba(settings->value("Dark_PeakPlotLine", defaultColors.Dark_PeakPlotLine.rgba()).toUInt());
    prefs.colorScheme.Dark_TuningLine = QColor::fromRgba(settings->value("Dark_TuningLine", defaultColors.Dark_TuningLine.rgba()).toUInt());
    settings->endGroup();

    settings->beginGroup("LightColors");
    prefs.colorScheme.Light_PlotBackground = QColor::fromRgba(settings->value("Light_PlotBackground", defaultColors.Light_PlotBackground.rgba()).toUInt());
    prefs.colorScheme.Light_PlotAxisPen = QColor::fromRgba(settings->value("Light_PlotAxisPen", defaultColors.Light_PlotAxisPen.rgba()).toUInt());
    prefs.colorScheme.Light_PlotLegendTextColor = QColor::fromRgba(settings->value("Light_PlotLegendTextColo", defaultColors.Light_PlotLegendTextColor.rgba()).toUInt());
    prefs.colorScheme.Light_PlotLegendBorderPen = QColor::fromRgba(settings->value("Light_PlotLegendBorderPen", defaultColors.Light_PlotLegendBorderPen.rgba()).toUInt());
    prefs.colorScheme.Light_PlotLegendBrush = QColor::fromRgba(settings->value("Light_PlotLegendBrush", defaultColors.Light_PlotLegendBrush.rgba()).toUInt());
    prefs.colorScheme.Light_PlotTickLabel = QColor::fromRgba(settings->value("Light_PlotTickLabel", defaultColors.Light_PlotTickLabel.rgba()).toUInt());
    prefs.colorScheme.Light_PlotBasePen = QColor::fromRgba(settings->value("Light_PlotBasePen", defaultColors.Light_PlotBasePen.rgba()).toUInt());
    prefs.colorScheme.Light_PlotTickPen = QColor::fromRgba(settings->value("Light_PlotTickPen", defaultColors.Light_PlotTickPen.rgba()).toUInt());
    prefs.colorScheme.Light_PeakPlotLine = QColor::fromRgba(settings->value("Light_PeakPlotLine", defaultColors.Light_PeakPlotLine.rgba()).toUInt());
    prefs.colorScheme.Light_TuningLine = QColor::fromRgba(settings->value("Light_TuningLine", defaultColors.Light_TuningLine.rgba()).toUInt());
    settings->endGroup();


    // Radio and Comms: C-IV addr, port to use
    settings->beginGroup("Radio");
    prefs.radioCIVAddr = (unsigned char) settings->value("RigCIVuInt", defPrefs.radioCIVAddr).toInt();
    if(prefs.radioCIVAddr!=0)
    {
        ui->rigCIVManualAddrChk->setChecked(true);
        ui->rigCIVaddrHexLine->blockSignals(true);
        ui->rigCIVaddrHexLine->setText(QString("%1").arg(prefs.radioCIVAddr, 2, 16));
        ui->rigCIVaddrHexLine->setEnabled(true);
        ui->rigCIVaddrHexLine->blockSignals(false);
    } else {
        ui->rigCIVManualAddrChk->setChecked(false);
        ui->rigCIVaddrHexLine->setEnabled(false);
    }
    prefs.serialPortRadio = settings->value("SerialPortRadio", defPrefs.serialPortRadio).toString();
    int serialIndex = ui->serialDeviceListCombo->findText(prefs.serialPortRadio);
    if (serialIndex != -1) {
        ui->serialDeviceListCombo->setCurrentIndex(serialIndex);
    }

    prefs.serialPortBaud = (quint32) settings->value("SerialPortBaud", defPrefs.serialPortBaud).toInt();
    ui->baudRateCombo->blockSignals(true);
    ui->baudRateCombo->setCurrentIndex( ui->baudRateCombo->findData(prefs.serialPortBaud) );
    ui->baudRateCombo->blockSignals(false);

    if (prefs.serialPortBaud > 0)
    {
        serverConfig.baudRate = prefs.serialPortBaud;
    }

    prefs.virtualSerialPort = settings->value("VirtualSerialPort", defPrefs.virtualSerialPort).toString();
    int vspIndex = ui->vspCombo->findText(prefs.virtualSerialPort);
    if (vspIndex != -1) {
        ui->vspCombo->setCurrentIndex(vspIndex);
    }
    else
    {
        ui->vspCombo->addItem(prefs.virtualSerialPort);
        ui->vspCombo->setCurrentIndex(ui->vspCombo->count()-1);
    }

    prefs.localAFgain = (unsigned char) settings->value("localAFgain", defPrefs.localAFgain).toUInt();
    rxSetup.localAFgain = prefs.localAFgain;
    settings->endGroup();

    // Misc. user settings (enable PTT, draw peaks, etc)
    settings->beginGroup("Controls");
    prefs.enablePTT = settings->value("EnablePTT", defPrefs.enablePTT).toBool();
    ui->pttEnableChk->setChecked(prefs.enablePTT);
    prefs.niceTS = settings->value("NiceTS", defPrefs.niceTS).toBool();
    settings->endGroup();

    settings->beginGroup("LAN");

    prefs.enableLAN = settings->value("EnableLAN", defPrefs.enableLAN).toBool();
    if(prefs.enableLAN)
    {
        ui->baudRateCombo->setEnabled(false);
        ui->serialDeviceListCombo->setEnabled(false);
        //ui->udpServerSetupBtn->setEnabled(false);
    } else {
        ui->baudRateCombo->setEnabled(true);
        ui->serialDeviceListCombo->setEnabled(true);
        //ui->udpServerSetupBtn->setEnabled(true);
    }

    ui->lanEnableBtn->setChecked(prefs.enableLAN);
    ui->connectBtn->setEnabled(true);

    prefs.enableRigCtlD = settings->value("EnableRigCtlD", defPrefs.enableRigCtlD).toBool();
    ui->enableRigctldChk->setChecked(prefs.enableRigCtlD);
    prefs.rigCtlPort = settings->value("RigCtlPort", defPrefs.rigCtlPort).toInt();
    ui->rigctldPortTxt->setText(QString("%1").arg(prefs.rigCtlPort));
    // Call the function to start rigctld if enabled.
    on_enableRigctldChk_clicked(prefs.enableRigCtlD);

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
    rxSetup.samplerate = settings->value("AudioRXSampleRate", "48000").toInt();
    txSetup.samplerate = rxSetup.samplerate;
    ui->audioSampleRateCombo->setEnabled(ui->lanEnableBtn->isChecked());
    int audioSampleRateIndex = ui->audioSampleRateCombo->findText(QString::number(rxSetup.samplerate));
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

    ui->audioRXCodecCombo->blockSignals(true);
    txSetup.codec = settings->value("AudioTXCodec", "4").toInt();
    ui->audioTXCodecCombo->setEnabled(ui->lanEnableBtn->isChecked());
    for (int f = 0; f < ui->audioTXCodecCombo->count(); f++)
        if (ui->audioTXCodecCombo->itemData(f).toInt() == txSetup.codec)
            ui->audioTXCodecCombo->setCurrentIndex(f);
    ui->audioRXCodecCombo->blockSignals(false);

    ui->audioOutputCombo->blockSignals(true);
    rxSetup.name = settings->value("AudioOutput", "").toString();
    qInfo(logGui()) << "Got Audio Output: " << rxSetup.name;
    int audioOutputIndex = ui->audioOutputCombo->findText(rxSetup.name);
    if (audioOutputIndex != -1) {
        ui->audioOutputCombo->setCurrentIndex(audioOutputIndex);
#if defined(RTAUDIO)
        rxSetup.port = ui->audioOutputCombo->itemData(audioOutputIndex).toInt();
#elif defined(PORTAUDIO)
#else
        QVariant v = ui->audioOutputCombo->currentData();
        rxSetup.port = v.value<QAudioDeviceInfo>();
#endif
    }
    ui->audioOutputCombo->blockSignals(false);

    ui->audioInputCombo->blockSignals(true);
    txSetup.name = settings->value("AudioInput", "").toString();
    qInfo(logGui()) << "Got Audio Input: " << txSetup.name;
    int audioInputIndex = ui->audioInputCombo->findText(txSetup.name);
    if (audioInputIndex != -1) {
        ui->audioInputCombo->setCurrentIndex(audioInputIndex);
#if defined(RTAUDIO)
        txSetup.port = ui->audioInputCombo->itemData(audioInputIndex).toInt();
#elif defined(PORTAUDIO)
#else
        QVariant v = ui->audioInputCombo->currentData();
        txSetup.port = v.value<QAudioDeviceInfo>();
#endif
    }
    ui->audioInputCombo->blockSignals(false);

    rxSetup.resampleQuality = settings->value("ResampleQuality", "4").toInt();
    txSetup.resampleQuality = rxSetup.resampleQuality;

    udpPrefs.clientName = settings->value("ClientName", udpDefPrefs.clientName).toString();

    settings->endGroup();

    settings->beginGroup("Server");
    serverConfig.enabled = settings->value("ServerEnabled", false).toBool();
    serverConfig.controlPort = settings->value("ServerControlPort", 50001).toInt();
    serverConfig.civPort = settings->value("ServerCivPort", 50002).toInt();
    serverConfig.audioPort = settings->value("ServerAudioPort", 50003).toInt();
    int numUsers = settings->value("ServerNumUsers", 2).toInt();
    serverConfig.users.clear();
    for (int f = 0; f < numUsers; f++)
    {
        SERVERUSER user;
        user.username = settings->value("ServerUsername_" + QString::number(f), "").toString();
        user.password = settings->value("ServerPassword_" + QString::number(f), "").toString();
        user.userType = settings->value("ServerUserType_" + QString::number(f), 0).toInt();
        serverConfig.users.append(user);
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

    for(int i=0; i < size; i++)
    {
        settings->setArrayIndex(i);
        chan = settings->value("chan", 0).toInt();
        freq = settings->value("freq", 12.345).toDouble();
        mode = settings->value("mode", 0).toInt();
        isSet = settings->value("isSet", false).toBool();

        if(isSet)
        {
            mem.setPreset(chan, freq, (mode_kind)mode);
        }
    }

    settings->endArray();
    settings->endGroup();

    emit sendServerConfig(serverConfig);

}



void wfmain::saveSettings()
{
    qInfo(logSystem()) << "Saving settings to " << settings->fileName();
    // Basic things to load:

    // UI: (full screen, dark theme, draw peaks, colors, etc)
    settings->beginGroup("Interface");
    settings->setValue("UseFullScreen", prefs.useFullScreen);
    settings->setValue("UseSystemTheme", prefs.useSystemTheme);
    settings->setValue("UseDarkMode", prefs.useDarkMode);
    settings->setValue("DrawPeaks", prefs.drawPeaks);
    settings->setValue("WFAntiAlias", prefs.wfAntiAlias);
    settings->setValue("WFInterpolate", prefs.wfInterpolate);
    settings->setValue("WFTheme", prefs.wftheme);
    settings->setValue("StylesheetPath", prefs.stylesheetPath);
    settings->setValue("splitter", ui->splitter->saveState());
    settings->setValue("windowGeometry", saveGeometry());
    settings->setValue("windowState", saveState());
    settings->setValue("WFLength", prefs.wflength);
    settings->setValue("ConfirmExit", prefs.confirmExit);
    settings->setValue("ConfirmPowerOff", prefs.confirmPowerOff);
    settings->endGroup();

    // Radio and Comms: C-IV addr, port to use
    settings->beginGroup("Radio");
    settings->setValue("RigCIVuInt", prefs.radioCIVAddr);
    settings->setValue("SerialPortRadio", prefs.serialPortRadio);
    settings->setValue("SerialPortBaud", prefs.serialPortBaud);
    settings->setValue("VirtualSerialPort", prefs.virtualSerialPort);
    settings->setValue("localAFgain", prefs.localAFgain);
    settings->endGroup();

    // Misc. user settings (enable PTT, draw peaks, etc)
    settings->beginGroup("Controls");
    settings->setValue("EnablePTT", prefs.enablePTT);
    settings->setValue("NiceTS", prefs.niceTS);
    settings->endGroup();

    settings->beginGroup("LAN");
    settings->setValue("EnableLAN", prefs.enableLAN);
    settings->setValue("EnableRigCtlD", prefs.enableRigCtlD);
    settings->setValue("RigCtlPort", prefs.rigCtlPort);
    settings->setValue("IPAddress", udpPrefs.ipAddress);
    settings->setValue("ControlLANPort", udpPrefs.controlLANPort);
    settings->setValue("SerialLANPort", udpPrefs.serialLANPort);
    settings->setValue("AudioLANPort", udpPrefs.audioLANPort);
    settings->setValue("Username", udpPrefs.username);
    settings->setValue("Password", udpPrefs.password);
    settings->setValue("AudioRXLatency", rxSetup.latency);
    settings->setValue("AudioTXLatency", txSetup.latency);
    settings->setValue("AudioRXSampleRate", rxSetup.samplerate);
    settings->setValue("AudioRXCodec", rxSetup.codec);
    settings->setValue("AudioTXSampleRate", txSetup.samplerate);
    settings->setValue("AudioTXCodec", txSetup.codec);
    settings->setValue("AudioOutput", rxSetup.name);
    settings->setValue("AudioInput", txSetup.name);
    settings->setValue("ResampleQuality", rxSetup.resampleQuality);
    settings->setValue("ClientName", udpPrefs.clientName);
    settings->endGroup();

    // Memory channels
    settings->beginGroup("Memory");
    settings->beginWriteArray("Channel", (int)mem.getNumPresets());

    preset_kind temp;
    for(int i=0; i < (int)mem.getNumPresets(); i++)
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

    // Note: X and Y get the same colors. See setPlotTheme() function

    settings->beginGroup("DarkColors");
    settings->setValue("Dark_PlotBackground", prefs.colorScheme.Dark_PlotBackground.rgba());
    settings->setValue("Dark_PlotAxisPen", prefs.colorScheme.Dark_PlotAxisPen.rgba());
    settings->setValue("Dark_PlotLegendTextColor", prefs.colorScheme.Dark_PlotLegendTextColor.rgba());
    settings->setValue("Dark_PlotLegendBorderPen", prefs.colorScheme.Dark_PlotLegendBorderPen.rgba());
    settings->setValue("Dark_PlotLegendBrush", prefs.colorScheme.Dark_PlotLegendBrush.rgba());
    settings->setValue("Dark_PlotTickLabel", prefs.colorScheme.Dark_PlotTickLabel.rgba());
    settings->setValue("Dark_PlotBasePen", prefs.colorScheme.Dark_PlotBasePen.rgba());
    settings->setValue("Dark_PlotTickPen", prefs.colorScheme.Dark_PlotTickPen.rgba());
    settings->setValue("Dark_PeakPlotLine", prefs.colorScheme.Dark_PeakPlotLine.rgba());
    settings->setValue("Dark_TuningLine", prefs.colorScheme.Dark_TuningLine.rgba());
    settings->endGroup();

    settings->beginGroup("LightColors");
    settings->setValue("Light_PlotBackground", prefs.colorScheme.Light_PlotBackground.rgba());
    settings->setValue("Light_PlotAxisPen", prefs.colorScheme.Light_PlotAxisPen.rgba());
    settings->setValue("Light_PlotLegendTextColor", prefs.colorScheme.Light_PlotLegendTextColor.rgba());
    settings->setValue("Light_PlotLegendBorderPen", prefs.colorScheme.Light_PlotLegendBorderPen.rgba());
    settings->setValue("Light_PlotLegendBrush", prefs.colorScheme.Light_PlotLegendBrush.rgba());
    settings->setValue("Light_PlotTickLabel", prefs.colorScheme.Light_PlotTickLabel.rgba());
    settings->setValue("Light_PlotBasePen", prefs.colorScheme.Light_PlotBasePen.rgba());
    settings->setValue("Light_PlotTickPen", prefs.colorScheme.Light_PlotTickPen.rgba());
    settings->setValue("Light_PeakPlotLine", prefs.colorScheme.Light_PeakPlotLine.rgba());
    settings->setValue("Light_TuningLine", prefs.colorScheme.Light_TuningLine.rgba());

    settings->endGroup();

    // This is a reference to see how the preference file is encoded.
    settings->beginGroup("StandardColors");

    settings->setValue("white", QColor(Qt::white).rgba());
    settings->setValue("black", QColor(Qt::black).rgba());

    settings->setValue("red_opaque", QColor(Qt::red).rgba());
    settings->setValue("red_translucent", QColor(255,0,0,128).rgba());
    settings->setValue("green_opaque", QColor(Qt::green).rgba());
    settings->setValue("green_translucent", QColor(0,255,0,128).rgba());
    settings->setValue("blue_opaque", QColor(Qt::blue).rgba());
    settings->setValue("blue_translucent", QColor(0,0,255,128).rgba());
    settings->setValue("cyan", QColor(Qt::cyan).rgba());
    settings->setValue("magenta", QColor(Qt::magenta).rgba());
    settings->setValue("yellow", QColor(Qt::yellow).rgba());
    settings->endGroup();

    settings->beginGroup("Server");

    settings->setValue("ServerEnabled", serverConfig.enabled);
    settings->setValue("ServerControlPort", serverConfig.controlPort);
    settings->setValue("ServerCivPort", serverConfig.civPort);
    settings->setValue("ServerAudioPort", serverConfig.audioPort);
    settings->setValue("ServerNumUsers", serverConfig.users.count());
    for (int f = 0; f < serverConfig.users.count(); f++)
    {
        settings->setValue("ServerUsername_" + QString::number(f), serverConfig.users[f].username);
        settings->setValue("ServerPassword_" + QString::number(f), serverConfig.users[f].password);
        settings->setValue("ServerUserType_" + QString::number(f), serverConfig.users[f].userType);
    }

    settings->endGroup();



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

        //wfimage.resize(wfLengthMax);

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
        colorMap->setDataRange(QCPRange(0, rigCaps.spectAmpMax));
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
    if(freqLock) return;

    freqt f;
    f.Hz = roundFrequencyWithStep(freq.Hz, -1, tsPlusHz);

    f.MHzDouble = f.Hz / (double)1E6;
    setUIFreq();
    //emit setFrequency(f);
    issueCmd(cmdSetFreq, f);
    issueDelayedCommandUnique(cmdGetFreq);
}

void wfmain::shortcutPlus()
{
    if(freqLock) return;

    freqt f;
    f.Hz = roundFrequencyWithStep(freq.Hz, 1, tsPlusHz);

    f.MHzDouble = f.Hz / (double)1E6;
    setUIFreq();
    //emit setFrequency(f);
    issueCmd(cmdSetFreq, f);
    issueDelayedCommandUnique(cmdGetFreq);
}

void wfmain::shortcutShiftMinus()
{
    if(freqLock) return;

    freqt f;
    f.Hz = roundFrequencyWithStep(freq.Hz, -1, tsPlusShiftHz);

    f.MHzDouble = f.Hz / (double)1E6;
    setUIFreq();
    //emit setFrequency(f);
    issueCmd(cmdSetFreq, f);
    issueDelayedCommandUnique(cmdGetFreq);
}

void wfmain::shortcutShiftPlus()
{
    if(freqLock) return;

    freqt f;
    f.Hz = roundFrequencyWithStep(freq.Hz, 1, tsPlusShiftHz);

    f.MHzDouble = f.Hz / (double)1E6;
    setUIFreq();
    //emit setFrequency(f);
    issueCmd(cmdSetFreq, f);
    issueDelayedCommandUnique(cmdGetFreq);
}

void wfmain::shortcutControlMinus()
{
    if(freqLock) return;

    freqt f;
    f.Hz = roundFrequencyWithStep(freq.Hz, -1, tsPlusControlHz);

    f.MHzDouble = f.Hz / (double)1E6;
    setUIFreq();
    //emit setFrequency(f);
    issueCmd(cmdSetFreq, f);
    issueDelayedCommandUnique(cmdGetFreq);
}

void wfmain::shortcutControlPlus()
{
    if(freqLock) return;

    freqt f;
    f.Hz = roundFrequencyWithStep(freq.Hz, 1, tsPlusControlHz);

    f.MHzDouble = f.Hz / (double)1E6;
    setUIFreq();
    //emit setFrequency(f);
    issueCmd(cmdSetFreq, f);
    issueDelayedCommandUnique(cmdGetFreq);
}

void wfmain::shortcutPageUp()
{
    if(freqLock) return;

    freqt f;
    f.Hz = freq.Hz + tsPageHz;

    f.MHzDouble = f.Hz / (double)1E6;
    setUIFreq();
    //emit setFrequency(f);
    issueCmd(cmdSetFreq, f);
    issueDelayedCommandUnique(cmdGetFreq);
}

void wfmain::shortcutPageDown()
{
    if(freqLock) return;

    freqt f;
    f.Hz = freq.Hz - tsPageHz;

    f.MHzDouble = f.Hz / (double)1E6;
    setUIFreq();
    //emit setFrequency(f);
    issueCmd(cmdSetFreq, f);
    issueDelayedCommandUnique(cmdGetFreq);
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
    // computers will glitch occassionally.

    issueDelayedCommand(cmdGetFreq);
    issueDelayedCommand(cmdGetMode);

    issueDelayedCommand(cmdNone);

    issueDelayedCommand(cmdGetFreq);
    issueDelayedCommand(cmdGetMode);

    // From left to right in the UI:
    issueDelayedCommand(cmdGetDataMode);
    issueDelayedCommand(cmdGetModInput);
    issueDelayedCommand(cmdGetModDataInput);
    issueDelayedCommand(cmdGetRxGain);
    issueDelayedCommand(cmdGetAfGain);
    issueDelayedCommand(cmdGetSql);
    issueDelayedCommand(cmdGetTxPower);
    issueDelayedCommand(cmdGetCurrentModLevel); // level for currently selected mod sources
    issueDelayedCommand(cmdGetSpectrumRefLevel);
    issueDelayedCommand(cmdGetDuplexMode);

    if(rigCaps.hasSpectrum)
    {
        issueDelayedCommand(cmdDispEnable);
        issueDelayedCommand(cmdSpecOn);
    }
    issueDelayedCommand(cmdGetModInput);
    issueDelayedCommand(cmdGetModDataInput);

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

void wfmain::on_useDarkThemeChk_clicked(bool checked)
{
    //setAppTheme(checked);
    setPlotTheme(wf, checked);
    setPlotTheme(plot, checked);
    prefs.useDarkMode = checked;
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
        QFile f("/usr/share/wfview/stylesheets/" + prefs.stylesheetPath);
#endif
        if (!f.exists())
        {
            printf("Unable to set stylesheet, file not found\n");
            printf("Tried to load: [%s]\n", QString( QString("/usr/share/wfview/stylesheets/") + prefs.stylesheetPath).toStdString().c_str() );
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

void wfmain::setDefaultColors()
{
    defaultColors.Dark_PlotBackground = QColor(0,0,0,255);
    defaultColors.Dark_PlotAxisPen = QColor(75,75,75,255);
    defaultColors.Dark_PlotLegendTextColor = QColor(255,255,255,255);
    defaultColors.Dark_PlotLegendBorderPen = QColor(255,255,255,255);
    defaultColors.Dark_PlotLegendBrush = QColor(0,0,0,200);
    defaultColors.Dark_PlotTickLabel = QColor(Qt::white);
    defaultColors.Dark_PlotBasePen = QColor(Qt::white);
    defaultColors.Dark_PlotTickPen = QColor(Qt::white);
    defaultColors.Dark_PeakPlotLine = QColor(Qt::yellow);
    defaultColors.Dark_TuningLine = QColor(Qt::cyan);

    defaultColors.Light_PlotBackground = QColor(255,255,255,255);
    defaultColors.Light_PlotAxisPen = QColor(200,200,200,255);
    defaultColors.Light_PlotLegendTextColor = QColor(0,0,0,255);
    defaultColors.Light_PlotLegendBorderPen = QColor(0,0,0,255);
    defaultColors.Light_PlotLegendBrush = QColor(255,255,255,200);
    defaultColors.Light_PlotTickLabel = QColor(Qt::black);
    defaultColors.Light_PlotBasePen = QColor(Qt::black);
    defaultColors.Light_PlotTickPen = QColor(Qt::black);
    defaultColors.Light_PeakPlotLine = QColor(Qt::blue);
    defaultColors.Light_TuningLine = QColor(Qt::blue);
}

void wfmain::setPlotTheme(QCustomPlot *plot, bool isDark)
{
    if(isDark)
    {
        plot->setBackground(prefs.colorScheme.Dark_PlotBackground);
        //plot->setBackground(QColor(0,0,0,255));

        plot->xAxis->grid()->setPen(prefs.colorScheme.Dark_PlotAxisPen);
        plot->yAxis->grid()->setPen(prefs.colorScheme.Dark_PlotAxisPen);

        plot->legend->setTextColor(prefs.colorScheme.Dark_PlotLegendTextColor);
        plot->legend->setBorderPen(prefs.colorScheme.Dark_PlotLegendBorderPen);
        plot->legend->setBrush(prefs.colorScheme.Dark_PlotLegendBrush);

        plot->xAxis->setTickLabelColor(prefs.colorScheme.Dark_PlotTickLabel);
        plot->xAxis->setLabelColor(prefs.colorScheme.Dark_PlotTickLabel);
        plot->yAxis->setTickLabelColor(prefs.colorScheme.Dark_PlotTickLabel);
        plot->yAxis->setLabelColor(prefs.colorScheme.Dark_PlotTickLabel);

        plot->xAxis->setBasePen(prefs.colorScheme.Dark_PlotBasePen);
        plot->xAxis->setTickPen(prefs.colorScheme.Dark_PlotTickPen);
        plot->yAxis->setBasePen(prefs.colorScheme.Dark_PlotBasePen);
        plot->yAxis->setTickPen(prefs.colorScheme.Dark_PlotTickPen);
        plot->graph(0)->setPen(prefs.colorScheme.Dark_PeakPlotLine);
        freqIndicatorLine->setPen(prefs.colorScheme.Dark_TuningLine);
    } else {
        //color = ui->groupBox->palette().color(QPalette::Button);

        plot->setBackground(prefs.colorScheme.Light_PlotBackground);
        plot->xAxis->grid()->setPen(prefs.colorScheme.Light_PlotAxisPen);
        plot->yAxis->grid()->setPen(prefs.colorScheme.Light_PlotAxisPen);

        plot->legend->setTextColor(prefs.colorScheme.Light_PlotLegendTextColor);
        plot->legend->setBorderPen(prefs.colorScheme.Light_PlotLegendBorderPen);
        plot->legend->setBrush(prefs.colorScheme.Light_PlotLegendBrush);

        plot->xAxis->setTickLabelColor(prefs.colorScheme.Light_PlotTickLabel);
        plot->xAxis->setLabelColor(prefs.colorScheme.Light_PlotTickLabel);
        plot->yAxis->setTickLabelColor(prefs.colorScheme.Light_PlotTickLabel);
        plot->yAxis->setLabelColor(prefs.colorScheme.Light_PlotTickLabel);

        plot->xAxis->setBasePen(prefs.colorScheme.Light_PlotBasePen);
        plot->xAxis->setTickPen(prefs.colorScheme.Light_PlotTickPen);
        plot->yAxis->setBasePen(prefs.colorScheme.Light_PlotBasePen);
        plot->yAxis->setTickPen(prefs.colorScheme.Light_PlotTickLabel);
        plot->graph(0)->setPen(prefs.colorScheme.Light_PeakPlotLine);
        freqIndicatorLine->setPen(prefs.colorScheme.Light_TuningLine);
    }
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
            emit setFrequency(f);
            break;
        }
        case cmdSetMode:
        {
            mode_info m = (*std::static_pointer_cast<mode_info>(data));
            emit setMode(m);
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
        case cmdSetPTT:
        {
            bool pttrequest = (*std::static_pointer_cast<bool>(data));
            emit setPTT(pttrequest);
            if(pttrequest)
            {
                ui->meterSPoWidget->setMeterType(meterPower);
            } else {
                ui->meterSPoWidget->setMeterType(meterS);
            }
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
            emit getRigID();
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
            emit getATUStatus();
            break;
        case cmdStartATU:
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
            if(rigCaps.hasPTTCommand)
            {
                emit getPTT();
            }
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

                int nCmds = slowPollCmdQueue.size();
                cmds sCmd = slowPollCmdQueue[(slowCmdNum++)%nCmds];
                doCmd(sCmd);
            }
        }
    } else {
        // odd-number ticks:
        // s-meter or other metering
        if(haveRigCaps && !periodicCmdQueue.empty())
        {
            int nCmds = periodicCmdQueue.size();
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

    // The following is both expensive and not that great,
    // since it does not check if the arguments are the same.
    bool found = false;
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

void wfmain::issueCmdUniquePriority(cmds cmd, bool b)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<bool>(new bool(b));
    delayedCmdQue.push_front(cmddata);
    removeSimilarCommand(cmd);
}

void wfmain::issueCmdUniquePriority(cmds cmd, unsigned char c)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<unsigned char>(new unsigned char(c));
    delayedCmdQue.push_front(cmddata);
    removeSimilarCommand(cmd);
}

void wfmain::issueCmdUniquePriority(cmds cmd, char c)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<char>(new char(c));
    delayedCmdQue.push_front(cmddata);
    removeSimilarCommand(cmd);
}

void wfmain::issueCmdUniquePriority(cmds cmd, freqt f)
{
    commandtype cmddata;
    cmddata.cmd = cmd;
    cmddata.data = std::shared_ptr<freqt>(new freqt(f));
    delayedCmdQue.push_front(cmddata);
    removeSimilarCommand(cmd);
}

void wfmain::removeSimilarCommand(cmds cmd)
{
    // pop anything out that is of the same kind of command:
    // pop anything out that is of the same kind of command:
    // Start at 1 since we put one in at zero that we want to keep.
    for(unsigned int i=1; i < delayedCmdQue.size(); i++)
    {
        if(delayedCmdQue.at(i).cmd == cmd)
        {
            //delayedCmdQue[i].cmd = cmdNone;
            delayedCmdQue.erase(delayedCmdQue.begin()+i);
            // i -= 1;
        }
    }
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
        setWindowTitle(rigCaps.modelName);
        this->spectWidth = rigCaps.spectLenMax; // used once haveRigCaps is true.
        haveRigCaps = true;
        // Added so that server receives rig capabilities.
        emit sendRigCaps(rigCaps);
        rpt->setRig(rigCaps);

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
                inName = QString("%1").arg(rigCaps.antennas.at(i)+1, 0, 16);		// adding 1 to have the combobox start with ant 1 insted of 0
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
        } else {
            ui->scopeBWCombo->setHidden(true);
        }
        ui->scopeBWCombo->blockSignals(false);

        setBandButtons();

        ui->tuneEnableChk->setEnabled(rigCaps.hasATU);
        ui->tuneNowBtn->setEnabled(rigCaps.hasATU);

        ui->connectBtn->setText("Disconnect"); // We must be connected now.
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
        calculateTimingParameters();
        initPeriodicCommands();
    }
}

void wfmain::initPeriodicCommands()
{
    // This function places periodic polling commands into a queue.
    // The commands are run using a timer,
    // and the timer is started by the delayed command cmdStartPeriodicTimer.

    insertPeriodicCommand(cmdGetTxRxMeter, 128);

    insertSlowPeriodicCommand(cmdGetFreq, 128);
    insertSlowPeriodicCommand(cmdGetMode, 128);
    insertSlowPeriodicCommand(cmdGetPTT, 128);
    insertSlowPeriodicCommand(cmdGetTxPower, 128);
    insertSlowPeriodicCommand(cmdGetRxGain, 128);
    insertSlowPeriodicCommand(cmdGetAttenuator, 128);
    insertSlowPeriodicCommand(cmdGetPTT, 128);
    insertSlowPeriodicCommand(cmdGetPreamp, 128);
    if (rigCaps.hasRXAntenna) {
        insertSlowPeriodicCommand(cmdGetAntenna, 128);
    }
}

void wfmain::insertPeriodicCommand(cmds cmd, unsigned char priority)
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

void wfmain::insertPeriodicCommandUnique(cmds cmd)
{
    // Use this function to insert a non-duplicate command
    // into the fast periodic polling queue, typically
    // meter commands where high refresh rates are desirable.

    removePeriodicCommand(cmd);
    periodicCmdQueue.push_front(cmd);
}

void wfmain::removePeriodicCommand(cmds cmd)
{
    while(true)
    {
        auto it = std::find(this->periodicCmdQueue.begin(), this->periodicCmdQueue.end(), cmd);
        if(it != periodicCmdQueue.end())
        {
            periodicCmdQueue.erase(it);
        } else {
            break;
        }
    }
}


void wfmain::insertSlowPeriodicCommand(cmds cmd, unsigned char priority)
{
    // TODO: meaningful priority
    // These commands are run every 20 "ticks" of the primary radio command loop
    // Basically 20 times less often than the standard peridic command
    if(priority < 10)
    {
        slowPollCmdQueue.push_front(cmd);
    } else {
        slowPollCmdQueue.push_back(cmd);
    }
}

void wfmain::receiveFreq(freqt freqStruct)
{

    qint64 tnow_ms = QDateTime::currentMSecsSinceEpoch();
    if(tnow_ms - lastFreqCmdTime_ms > delayedCommand->interval() * 2)
    {
        ui->freqLabel->setText(QString("%1").arg(freqStruct.MHzDouble, 0, 'f'));
        freq = freqStruct;
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

    if((startFreq != oldLowerFreq) || (endFreq != oldUpperFreq))
    {
        // If the frequency changed and we were drawing peaks, now is the time to clearn them
        if(drawPeaks)
        {
            // TODO: create non-button function to do this
            // This will break if the button is ever moved or renamed.
            on_clearPeakBtn_clicked();
        }
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

    for(int i=0; i < spectWidth; i++)
    {
        x[i] = (i * (endFreq-startFreq)/spectWidth) + startFreq;
    }

    for(int i=0; i<specLen; i++)
    {
        //x[i] = (i * (endFreq-startFreq)/specLen) + startFreq;
        y[i] = (unsigned char)spectrum.at(i);
        if(drawPeaks)
        {
            if((unsigned char)spectrum.at(i) > (unsigned char)spectrumPeaks.at(i))
            {
                spectrumPeaks[i] = spectrum.at(i);
            }
            y2[i] = (unsigned char)spectrumPeaks.at(i);
        }

    }

    if(!spectrumDrawLock)
    {
        //ui->qcp->addGraph();
        plot->graph(0)->setData(x,y);
        if((freq.MHzDouble < endFreq) && (freq.MHzDouble > startFreq))
        {
            freqIndicatorLine->start->setCoords(freq.MHzDouble,0);
            freqIndicatorLine->end->setCoords(freq.MHzDouble,160);
        }
        if(drawPeaks)
        {
            plot->graph(1)->setData(x,y2); // peaks
        }
        plot->yAxis->setRange(0, rigCaps.spectAmpMax+1);
        plot->xAxis->setRange(startFreq, endFreq);
        plot->replot();

        if(specLen == spectWidth)
        {
            wfimage.prepend(spectrum);
            wfimage.resize(wfLengthMax);
            wfimage.squeeze();

            // Waterfall:
            for(int row = 0; row < wfLength; row++)
            {
                for(int col = 0; col < spectWidth; col++)
                {
                    colorMap->data()->setCell( col, row, wfimage.at(row).at(col));
                }
            }

           wf->yAxis->setRange(0,wfLength - 1);
           wf->xAxis->setRange(0, spectWidth-1);
           wf->replot();
        }
    }
}

void wfmain::receiveSpectrumMode(spectrumMode spectMode)
{
    for(int i=0; i < ui->spectrumModeCombo->count(); i++)
    {
        if(static_cast<spectrumMode>(ui->spectrumModeCombo->itemData(i).toInt()) == spectMode)
        {
            ui->spectrumModeCombo->blockSignals(true);
            ui->spectrumModeCombo->setCurrentIndex(i);
            ui->spectrumModeCombo->blockSignals(false);
        }
    }
}


void wfmain::handlePlotDoubleClick(QMouseEvent *me)
{
    double x;
    freqt freqGo;
    //double y;
    //double px;
    if(!freqLock)
    {
        //y = plot->yAxis->pixelToCoord(me->pos().y());
        x = plot->xAxis->pixelToCoord(me->pos().x());
        freqGo.Hz = x*1E6;

        freqGo.Hz = roundFrequency(freqGo.Hz, tsWfScrollHz);
        freqGo.MHzDouble = (float)freqGo.Hz / 1E6;

        //emit setFrequency(freq);
        issueCmd(cmdSetFreq, freqGo);
        freq = freqGo;
        setUIFreq();
        //issueDelayedCommand(cmdGetFreq);
        showStatusBarText(QString("Going to %1 MHz").arg(x));
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

        //emit setFrequency(freq);
        issueCmd(cmdSetFreq, freqGo);
        freq = freqGo;
        setUIFreq();
        showStatusBarText(QString("Going to %1 MHz").arg(x));
    }
}

void wfmain::handlePlotClick(QMouseEvent *me)
{
    double x = plot->xAxis->pixelToCoord(me->pos().x());
    showStatusBarText(QString("Selected %1 MHz").arg(x));
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

    //emit setFrequency(f);
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

        for(int i=0; i < ui->modeSelectCombo->count(); i++)
        {
            if(ui->modeSelectCombo->itemData(i).toInt() == mode)
            {
                ui->modeSelectCombo->blockSignals(true);
                ui->modeSelectCombo->setCurrentIndex(i);
                ui->modeSelectCombo->blockSignals(false);
                found = true;
            }
        }
        currentModeIndex = mode;
    } else {
        qInfo(logSystem()) << __func__ << "Invalid mode " << mode << " received. ";
    }

    if(!found)
    {
        qInfo(logSystem()) << __func__ << "Received mode " << mode << " but could not match to any index within the modeSelectCombo. ";
    }

    if( (filter) && (filter < 4)){
        ui->modeFilterCombo->blockSignals(true);
        ui->modeFilterCombo->setCurrentIndex(filter-1);
        ui->modeFilterCombo->blockSignals(false);
    }

    (void)filter;


    // Note: we need to know if the DATA mode is active to reach mode-D
    // some kind of queued query:
    if(rigCaps.hasDataModes)
        issueDelayedCommand(cmdGetDataMode);
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
    }
    return;
}

void wfmain::on_drawPeakChk_clicked(bool checked)
{
    if(checked)
    {
        on_clearPeakBtn_clicked(); // clear
        drawPeaks = true;

    } else {
        drawPeaks = false;

#if QCUSTOMPLOT_VERSION >= 0x020000
        plot->graph(1)->data()->clear();
#else
        plot->graph(1)->clearData();
#endif

    }
    prefs.drawPeaks = checked;
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
            issueCmd(cmdSetFreq, f);            
        }
    } else {
        KHz = ui->freqMhzLineEdit->text().toInt(&ok);
        if(ok)
        {
            f.Hz = KHz*1E3;
            issueCmd(cmdSetFreq, f);
        }
    }
    if(ok)
    {
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
    emit setScopeMode(static_cast<spectrumMode>(ui->spectrumModeCombo->itemData(index).toInt()));
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
    emit setMode((unsigned char)mode, (unsigned char)filter);

    currentMode = mode;

    if(dataOn)
    {
        issueDelayedCommand(cmdSetDataModeOn);
        ui->dataModeBtn->blockSignals(true);
        ui->dataModeBtn->setChecked(true);
        ui->dataModeBtn->blockSignals(false);
    } else {
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
    // This function is not called if code initated the change.

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
        currentModeInfo = mode;
        //emit setMode(newMode, filterSelection);
    }
}

void wfmain::on_freqDial_valueChanged(int value)
{
    int maxVal = ui->freqDial->maximum();

    freqt f;
    f.Hz = 0;
    f.MHzDouble = 0;

    volatile int delta = 0;

    int directPath = 0;
    int crossingPath = 0;

    int distToMaxNew = 0;
    int distToMaxOld = 0;

    if(freqLock)
    {
        ui->freqDial->blockSignals(true);
        ui->freqDial->setValue(oldFreqDialVal);
        ui->freqDial->blockSignals(false);
        return;
    }
    
    if(value == 0)
    {
        distToMaxNew = 0;
    } else {
        distToMaxNew = maxVal - value;
    }

    if(oldFreqDialVal != 0)
    {
        distToMaxOld = maxVal - oldFreqDialVal;
    } else {
        distToMaxOld = 0;
    }
    
    directPath = abs(value - oldFreqDialVal);
    if(value < maxVal / 2)
    {
        crossingPath = value + distToMaxOld;
    } else {
        crossingPath = distToMaxNew + oldFreqDialVal;
    }
    
    if(directPath > crossingPath)
    {
        // use crossing path, it is shorter
        delta = crossingPath;
        // now calculate the direction:
        if( value > oldFreqDialVal)
        {
            // CW
            delta = delta;
        } else {
            // CCW
            delta *= -1;
        }

    } else {
        // use direct path
        // crossing path is larger than direct path, use direct path
        //delta = directPath;
        // now calculate the direction
        delta = value - oldFreqDialVal;
    }

    // With the number of steps and direction of steps established,
    // we can now adjust the frequeny:

    f.Hz = roundFrequencyWithStep(freq.Hz, delta, tsKnobHz);
    f.MHzDouble = f.Hz / (double)1E6;
    if(f.Hz > 0)
    {
        freq = f;

        oldFreqDialVal = value;

        ui->freqLabel->setText(QString("%1").arg(f.MHzDouble, 0, 'f'));

        //emit setFrequency(f);
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
    //emit setFrequency(freq);
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
    if((currentMode == modeAM) || (currentMode == modeFM))
    {
        f.Hz = (70.260) * 1E6;
    } else {
        f.Hz = (70.200) * 1E6;
    }
    issueCmd(cmdSetFreq, f);
    //emit setFrequency(f);
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
    //emit setFrequency(f);
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
    //emit setFrequency(f);
    issueCmd(cmdSetFreq, f);
    issueDelayedCommandUnique(cmdGetFreq);
    ui->tabWidget->setCurrentIndex(0);
}

void wfmain::on_band2200mbtn_clicked()
{
    freqt f;
    f.Hz = 136 * 1E3;
    //emit setFrequency(f);
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
    issueCmdUniquePriority(cmdSetPTT, false);
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
    ui->baudRateCombo->setEnabled(checked);
    ui->serialDeviceListCombo->setEnabled(checked);
    //ui->udpServerSetupBtn->setEnabled(true);
}

void wfmain::on_lanEnableBtn_clicked(bool checked)
{
    prefs.enableLAN = checked;
    ui->connectBtn->setEnabled(true);
    ui->ipAddressTxt->setEnabled(checked);
    ui->controlPortTxt->setEnabled(checked);
    ui->usernameTxt->setEnabled(checked);
    ui->passwordTxt->setEnabled(checked);
    ui->baudRateCombo->setEnabled(!checked);
    ui->serialDeviceListCombo->setEnabled(!checked);
    //ui->udpServerSetupBtn->setEnabled(false);
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

void wfmain::on_audioOutputCombo_currentIndexChanged(int value)
{
#if defined(RTAUDIO)
    rxSetup.port = ui->audioOutputCombo->itemData(value).toInt();
#elif defined(PORTAUDIO)
#else
    QVariant v = ui->audioOutputCombo->itemData(value);
    rxSetup.port = v.value<QAudioDeviceInfo>();
#endif
    rxSetup.name = ui->audioOutputCombo->itemText(value);
    qDebug(logGui()) << "Changed default audio output to:" << rxSetup.name;
}

void wfmain::on_audioInputCombo_currentIndexChanged(int value)
{
#if defined(RTAUDIO)
    txSetup.port = ui->audioInputCombo->itemData(value).toInt();
#elif defined(PORTAUDIO)
#else
    QVariant v = ui->audioInputCombo->itemData(value);
    txSetup.port = v.value<QAudioDeviceInfo>();
#endif
    txSetup.name = ui->audioInputCombo->itemText(value);
    qDebug(logGui()) << "Changed default audio input to:" << txSetup.name;
}

void wfmain::on_audioSampleRateCombo_currentIndexChanged(QString text)
{
    //udpPrefs.audioRXSampleRate = text.toInt();
    //udpPrefs.audioTXSampleRate = text.toInt();
    rxSetup.samplerate = text.toInt();
    txSetup.samplerate = text.toInt();
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
    emit setScopeFixedEdge(oldLowerFreq, oldUpperFreq, ui->scopeEdgeCombo->currentIndex()+1);
    emit setScopeEdge(ui->scopeEdgeCombo->currentIndex()+1);
    issueDelayedCommand(cmdScopeFixedMode);
}


void wfmain::on_connectBtn_clicked()
{
    this->rigStatus->setText(""); // Clear status

    if (haveRigCaps) {
        emit sendCloseComm();
        ui->connectBtn->setText("Connect");
        haveRigCaps = false;
        rigName->setText("NONE");
    }
    else
    {
        emit sendCloseComm(); // Just in case there is a failed connection open.
        openRig();
    }
}

void wfmain::on_udpServerSetupBtn_clicked()
{
    srv->show();
}
void wfmain::on_sqlSlider_valueChanged(int value)
{
    issueCmd(cmdSetSql, (unsigned char)value);
    //emit setSql((unsigned char)value);
}

void wfmain::on_modeFilterCombo_activated(int index)
{

    int filterSelection = ui->modeFilterCombo->itemData(index).toInt();
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
        } else {
            emit setMode(newMode, (unsigned char)filterSelection);
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
        QDateTime now = QDateTime::currentDateTime();
        now.setTime(QTime::currentTime());

        int second = now.time().second();

        // 2: Find how many mseconds until next minute
        int msecdelay = QTime::currentTime().msecsTo( QTime::currentTime().addSecs(60-second) );

        // 3: Compute time and date at one minute later
        QDateTime setpoint = now.addMSecs(msecdelay); // at HMS or posibly HMS + some ms. Never under though.

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

// Slot to send/receive server config.
// If store is true then write to config otherwise send current config by signal
void wfmain::serverConfigRequested(SERVERCONFIG conf, bool store)
{

    if (!store) {
        emit sendServerConfig(serverConfig);
    }
    else {
        // Store config in file!
        qInfo(logSystem()) << "Storing server config";
        serverConfig = conf;
    }

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

void wfmain::on_serialDeviceListCombo_activated(const QString &arg1)
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
    rpt->show();
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
        switch(bandSel)
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

void wfmain::on_wfLengthSlider_valueChanged(int value)
{
    prefs.wflength = (unsigned int)(value);
    prepareWf(value);
}

void wfmain::on_pollingBtn_clicked()
{
    bool ok;
    int timing = 0;
    timing = QInputDialog::getInt(this, "wfview Radio Polling Setup", "Poll Timing Interval (ms)", delayedCommand->interval(), 1, 200, 1, &ok );

    if(ok && timing)
    {
        delayedCommand->setInterval( timing );
        qInfo(logSystem()) << "User changed radio polling interval to " << timing << "ms.";
        showStatusBarText("User changed radio polling interval to " + QString("%1").arg(timing) + "ms.");
    }
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

wfmain::cmds wfmain::meterKindToMeterCommand(meterKind m)
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
    } else {
        ui->meter2Widget->show();
        ui->meter2Widget->setMeterType(newMeterType);
        insertPeriodicCommandUnique(newCmd);
    }
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
            connect(rig, SIGNAL(stateInfo(rigStateStruct*)), rigCtl, SLOT(receiveStateInfo(rigStateStruct*)));
            connect(rigCtl, SIGNAL(setFrequency(freqt)), rig, SLOT(setFrequency(freqt)));
            connect(rigCtl, SIGNAL(setMode(unsigned char, unsigned char)), rig, SLOT(setMode(unsigned char, unsigned char)));
            connect(rigCtl, SIGNAL(setDataMode(bool, unsigned char)), rig, SLOT(setDataMode(bool, unsigned char)));
            connect(rigCtl, SIGNAL(setPTT(bool)), rig, SLOT(setPTT(bool)));
            connect(rigCtl, SIGNAL(sendPowerOn()), rig, SLOT(powerOn()));
            connect(rigCtl, SIGNAL(sendPowerOff()), rig, SLOT(powerOff()));

            // Levels: Set:
            connect(rigCtl, SIGNAL(setRfGain(unsigned char)), rig, SLOT(setRfGain(unsigned char)));
            connect(rigCtl, SIGNAL(setAfGain(unsigned char)), rig, SLOT(setAfGain(unsigned char)));
            connect(rigCtl, SIGNAL(setSql(unsigned char)), rig, SLOT(setSquelch(unsigned char)));
            connect(rigCtl, SIGNAL(setTxPower(unsigned char)), rig, SLOT(setTxPower(unsigned char)));
            connect(rigCtl, SIGNAL(setMicGain(unsigned char)), rig, SLOT(setMicGain(unsigned char)));
            connect(rigCtl, SIGNAL(setMonitorLevel(unsigned char)), rig, SLOT(setMonitorLevel(unsigned char)));
            connect(rigCtl, SIGNAL(setVoxGain(unsigned char)), rig, SLOT(setVoxGain(unsigned char)));
            connect(rigCtl, SIGNAL(setAntiVoxGain(unsigned char)), rig, SLOT(setAntiVoxGain(unsigned char)));
            connect(rigCtl, SIGNAL(setSpectrumRefLevel(int)), rig, SLOT(setSpectrumRefLevel(int)));

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

// --- DEBUG FUNCTION ---
void wfmain::on_debugBtn_clicked()
{
    qInfo(logSystem()) << "Debug button pressed.";
    //trxadj->show();
    //setRadioTimeDatePrep();
    //wf->setInteraction(QCP::iRangeZoom, true);
    //wf->setInteraction(QCP::iRangeDrag, true);

}
