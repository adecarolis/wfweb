#include "wfmain.h"
#include "ui_wfmain.h"

#include "commhandler.h"
#include "rigidentities.h"

// This code is copyright 2017-2020 Elliott H. Liggett
// All rights reserved

wfmain::wfmain(const QString serialPortCL, const QString hostCL, QWidget *parent ) :
    QMainWindow(parent),
    ui(new Ui::wfmain)
{
    QGuiApplication::setApplicationDisplayName("wfview");
    QGuiApplication::setApplicationName(QString("wfview"));

    setWindowIcon(QIcon( QString(":resources/wfview.png")));
    ui->setupUi(this);
    theParent = parent;

    setWindowTitle(QString("wfview"));

    this->serialPortCL = serialPortCL;
    this->hostCL = hostCL;

    cal = new calibrationWindow();

    haveRigCaps = false;

    ui->bandStkLastUsedBtn->setVisible(false);
    ui->bandStkVoiceBtn->setVisible(false);
    ui->bandStkDataBtn->setVisible(false);
    ui->bandStkCWBtn->setVisible(false);

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

    // Enumerate audio devices, need to do before settings are loaded.
    const auto audioOutputs = QAudioDeviceInfo::availableDevices(QAudio::AudioOutput);
    for (const QAudioDeviceInfo& deviceInfo : audioOutputs) {
        ui->audioOutputCombo->addItem(deviceInfo.deviceName());
    }
    const auto audioInputs = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
    for (const QAudioDeviceInfo& deviceInfo : audioInputs) {
        ui->audioInputCombo->addItem(deviceInfo.deviceName());
    }

    setDefaultColors(); // set of UI colors with defaults populated
    setDefPrefs(); // other default options
    loadSettings(); // Look for saved preferences

    // if setting for serial port is "auto" then...
//    if(prefs.serialPortRadio == QString("auto"))
//    {
//        // Find the ICOM IC-7300.
//        qDebug() << "Searching for serial port...";
//        QDirIterator it("/dev/serial", QStringList() << "*IC-7300*", QDir::Files, QDirIterator::Subdirectories);

//        while (it.hasNext())
//            qDebug() << it.next();
//        // if (it.isEmpty()) // fail or default to ttyUSB0 if present
//        // iterator might not make sense
//        serialPortRig = it.filePath(); // first? last?
//        if(serialPortRig.isEmpty())
//        {
//            qDebug() << "Cannot find IC-7300 serial port. Trying /dev/ttyUSB0";
//            serialPortRig = QString("/dev/ttyUSB0");
//        }
//        // end finding the 7300 code
//    } else {
//        serialPortRig = prefs.serialPortRadio;
//    }


    plot = ui->plot; // rename it waterfall.
    wf = ui->waterfall;
    tracer = new QCPItemTracer(plot);
    //tracer->setGraphKey(5.5);
    tracer->setInterpolating(true);
    tracer->setStyle(QCPItemTracer::tsCrosshair);

    tracer->setPen(QPen(Qt::green));
    tracer->setBrush(Qt::green);
    tracer->setSize(30);

//    spectWidth = 475; // fixed for now
//    wfLength = 160; // fixed for now, time-length of waterfall

//    // Initialize before use!

//    QByteArray empty((int)spectWidth, '\x01');
//    spectrumPeaks = QByteArray( (int)spectWidth, '\x01' );
//    for(quint16 i=0; i<wfLength; i++)
//    {
//        wfimage.append(empty);
//    }

    //          0      1        2         3       4
    modes << "LSB" << "USB" << "AM" << "CW" << "RTTY";
    //          5      6          7           8          9
    modes << "FM" << "CW-R" << "RTTY-R" << "LSB-D" << "USB-D";
    // TODO: Add FM-D and AM-D and where applicable D-Star hich seem to exist
    ui->modeSelectCombo->insertItems(0, modes);

    QStringList filters;
    filters << "1" << "2" << "3" << "Setup...";
    ui->modeFilterCombo->addItems(filters);


    spans << "2.5k" << "5.0k" << "10k" << "25k";
    spans << "50k" << "100k" << "250k" << "500k";
    ui->scopeBWCombo->insertItems(0, spans);

    edges << "1" << "2" << "3"; // yep
    ui->scopeEdgeCombo->insertItems(0,edges);

    ui->splitter->setHandleWidth(5);

    ui->rfGainSlider->setTickInterval(100);
    ui->rfGainSlider->setSingleStep(100);
    ui->afGainSlider->setSingleStep(100);
    ui->afGainSlider->setSingleStep(100);

    rigStatus = new QLabel(this);
    ui->statusBar->addPermanentWidget(rigStatus);
    ui->statusBar->showMessage("Connecting to rig...", 1000);

    delayedCommand = new QTimer(this);
    delayedCommand->setInterval(250); // 250ms until we find rig civ and id, then 100ms.
    delayedCommand->setSingleShot(true);
    connect(delayedCommand, SIGNAL(timeout()), this, SLOT(runDelayedCommand()));

    openRig();

    qRegisterMetaType<rigCapabilities>();

    connect(rig, SIGNAL(haveFrequency(double)), this, SLOT(receiveFreq(double)));
    connect(this, SIGNAL(getFrequency()), rig, SLOT(getFrequency()));
    connect(this, SIGNAL(getMode()), rig, SLOT(getMode()));
    connect(this, SIGNAL(getDataMode()), rig, SLOT(getDataMode()));
    connect(this, SIGNAL(setDataMode(bool)), rig, SLOT(setDataMode(bool)));
    connect(this, SIGNAL(getBandStackReg(char,char)), rig, SLOT(getBandStackReg(char,char)));
    connect(rig, SIGNAL(havePTTStatus(bool)), this, SLOT(receivePTTstatus(bool)));
    connect(this, SIGNAL(setPTT(bool)), rig, SLOT(setPTT(bool)));
    connect(rig, SIGNAL(haveBandStackReg(float,char,bool)), this, SLOT(receiveBandStackReg(float,char,bool)));
    connect(this, SIGNAL(getDebug()), rig, SLOT(getDebug()));

    connect(this, SIGNAL(spectOutputDisable()), rig, SLOT(disableSpectOutput()));
    connect(this, SIGNAL(spectOutputEnable()), rig, SLOT(enableSpectOutput()));
    connect(this, SIGNAL(scopeDisplayDisable()), rig, SLOT(disableSpectrumDisplay()));
    connect(this, SIGNAL(scopeDisplayEnable()), rig, SLOT(enableSpectrumDisplay()));
    connect(rig, SIGNAL(haveMode(QString)), this, SLOT(receiveMode(QString)));
    connect(rig, SIGNAL(haveDataMode(bool)), this, SLOT(receiveDataModeStatus(bool)));
    connect(rig, SIGNAL(haveSpectrumData(QByteArray, double, double)), this, SLOT(receiveSpectrumData(QByteArray, double, double)));
    connect(rig, SIGNAL(haveSpectrumFixedMode(bool)), this, SLOT(receiveSpectrumFixedMode(bool)));
    connect(this, SIGNAL(setFrequency(double)), rig, SLOT(setFrequency(double)));
    connect(this, SIGNAL(setScopeCenterMode(bool)), rig, SLOT(setSpectrumCenteredMode(bool)));
    connect(this, SIGNAL(setScopeEdge(char)), rig, SLOT(setScopeEdge(char)));
    connect(this, SIGNAL(setScopeSpan(char)), rig, SLOT(setScopeSpan(char)));
    connect(this, SIGNAL(getScopeMode()), rig, SLOT(getScopeMode()));
    connect(this, SIGNAL(getScopeEdge()), rig, SLOT(getScopeEdge()));
    connect(this, SIGNAL(getScopeSpan()), rig, SLOT(getScopeSpan()));
    connect(this, SIGNAL(setScopeFixedEdge(double,double,unsigned char)), rig, SLOT(setSpectrumBounds(double,double,unsigned char)));

    connect(this, SIGNAL(setMode(char)), rig, SLOT(setMode(char)));
    connect(this, SIGNAL(getRfGain()), rig, SLOT(getRfGain()));
    connect(this, SIGNAL(getAfGain()), rig, SLOT(getAfGain()));
    connect(this, SIGNAL(setRfGain(unsigned char)), rig, SLOT(setRfGain(unsigned char)));
    connect(this, SIGNAL(setAfGain(unsigned char)), rig, SLOT(setAfGain(unsigned char)));
    connect(rig, SIGNAL(haveRfGain(unsigned char)), this, SLOT(receiveRfGain(unsigned char)));
    connect(rig, SIGNAL(haveAfGain(unsigned char)), this, SLOT(receiveAfGain(unsigned char)));
    connect(this, SIGNAL(getSql()), rig, SLOT(getSql()));
    connect(rig, SIGNAL(haveSql(unsigned char)), this, SLOT(receiveSql(unsigned char)));
    connect(this, SIGNAL(setSql(unsigned char)), rig, SLOT(setSquelch(unsigned char)));
    connect(this, SIGNAL(startATU()), rig, SLOT(startATU()));
    connect(this, SIGNAL(setATU(bool)), rig, SLOT(setATU(bool)));
    connect(this, SIGNAL(getATUStatus()), rig, SLOT(getATUStatus()));
    connect(this, SIGNAL(getRigID()), rig, SLOT(getRigID()));
    connect(rig, SIGNAL(haveATUStatus(unsigned char)), this, SLOT(receiveATUStatus(unsigned char)));
    connect(rig, SIGNAL(haveRigID(rigCapabilities)), this, SLOT(receiveRigID(rigCapabilities)));

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


    // Plot user interaction
    connect(plot, SIGNAL(mouseDoubleClick(QMouseEvent*)), this, SLOT(handlePlotDoubleClick(QMouseEvent*)));
    connect(wf, SIGNAL(mouseDoubleClick(QMouseEvent*)), this, SLOT(handleWFDoubleClick(QMouseEvent*)));
    connect(plot, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(handlePlotClick(QMouseEvent*)));
    connect(wf, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(handleWFClick(QMouseEvent*)));
    connect(wf, SIGNAL(mouseWheel(QWheelEvent*)), this, SLOT(handleWFScroll(QWheelEvent*)));
    connect(plot, SIGNAL(mouseWheel(QWheelEvent*)), this, SLOT(handlePlotScroll(QWheelEvent*)));


    ui->plot->addGraph(); // primary
    ui->plot->addGraph(0, 0); // secondary, peaks, same axis as first?
    ui->waterfall->addGraph();
    tracer->setGraph(plot->graph(0));



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

    drawPeaks = false;

    ui->freqMhzLineEdit->setValidator( new QDoubleValidator(0, 100, 6, this));


    pttTimer = new QTimer(this);
    pttTimer->setInterval(180*1000); // 3 minute max transmit time in ms
    pttTimer->setSingleShot(true);
    connect(pttTimer, SIGNAL(timeout()), this, SLOT(handlePttLimit()));

    // Not needed since we automate this now.
    /*
    foreach (const QSerialPortInfo &serialPortInfo, QSerialPortInfo::availablePorts())
    {
        portList.append(serialPortInfo.portName());
        // ui->commPortDrop->addItem(serialPortInfo.portName());
    }
    */


#ifdef QT_DEBUG
    qDebug() << "Running with debugging options enabled.";
    ui->debugBtn->setVisible(true);
#else
    ui->debugBtn->setVisible(false);
#endif

    // Initial state of UI:
    ui->fullScreenChk->setChecked(prefs.useFullScreen);
    on_fullScreenChk_clicked(prefs.useFullScreen);

    ui->useDarkThemeChk->setChecked(prefs.useDarkMode);
    on_useDarkThemeChk_clicked(prefs.useDarkMode);

    ui->drawPeakChk->setChecked(prefs.drawPeaks);
    on_drawPeakChk_clicked(prefs.drawPeaks);
    drawPeaks = prefs.drawPeaks;

    //getInitialRigState();
    oldFreqDialVal = ui->freqDial->value();
}

wfmain::~wfmain()
{
    rigThread->quit();
    rigThread->wait();
    delete ui;
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
#ifdef QT_DEBUG
    if(!serialPortCL.isEmpty())
    {
        qDebug() << "Serial port specified by user: " << serialPortCL;
    } else {
        qDebug() << "Serial port not specified. ";
    }

    if(!hostCL.isEmpty())
    {
        qDebug() << "Remote host name specified by user: " << hostCL;
    }
#endif

    if (rigThread == Q_NULLPTR)
    {
        rig = new rigCommander();
        rigThread = new QThread(this);

        rig->moveToThread(rigThread);

        connect(rigThread, SIGNAL(started()), rig, SLOT(process()));
        connect(rigThread, SIGNAL(finished()), rig, SLOT(deleteLater()));
        rigThread->start();
        connect(rig, SIGNAL(haveSerialPortError(QString, QString)), this, SLOT(receiveSerialPortError(QString, QString)));
        connect(rig, SIGNAL(haveStatusUpdate(QString)), this, SLOT(receiveStatusUpdate(QString)));
        
        connect(this, SIGNAL(sendCommSetup(unsigned char, QString, quint16, quint16, quint16, QString, QString,quint16,quint16,quint8,quint16,quint8)), rig, SLOT(commSetup(unsigned char, QString, quint16, quint16, quint16, QString, QString,quint16,quint16,quint8,quint16,quint8)));
        connect(this, SIGNAL(sendCommSetup(unsigned char, QString, quint32)), rig, SLOT(commSetup(unsigned char, QString, quint32)));

        connect(this, SIGNAL(sendCloseComm()), rig, SLOT(closeComm()));
        connect(this, SIGNAL(sendChangeBufferSize(quint16)), rig, SLOT(changeBufferSize(quint16)));
        connect(this, SIGNAL(getRigCIV()), rig, SLOT(findRigs()));
        connect(rig, SIGNAL(discoveredRigID(rigCapabilities)), this, SLOT(receiveFoundRigID(rigCapabilities)));
        connect(rig, SIGNAL(commReady()), this, SLOT(receiveCommReady()));
    }

    if (prefs.enableLAN)
    {
        emit sendCommSetup(prefs.radioCIVAddr, prefs.ipAddress, prefs.controlLANPort, 
            prefs.serialLANPort, prefs.audioLANPort, prefs.username, prefs.password,prefs.audioRXBufferSize,prefs.audioRXSampleRate,prefs.audioRXCodec,prefs.audioTXSampleRate,prefs.audioTXCodec);
    } else {

        if( (prefs.serialPortRadio == QString("auto")) && (serialPortCL.isEmpty()))
        {
            // Find the ICOM
            // qDebug() << "Searching for serial port...";
            QDirIterator it73("/dev/serial", QStringList() << "*IC-7300*", QDir::Files, QDirIterator::Subdirectories);
            QDirIterator it97("/dev/serial", QStringList() << "*IC-9700*A*", QDir::Files, QDirIterator::Subdirectories);
            QDirIterator it785x("/dev/serial", QStringList() << "*IC-785*A*", QDir::Files, QDirIterator::Subdirectories);
            QDirIterator it705("/dev/serial", QStringList() << "*IC-705*A", QDir::Files, QDirIterator::Subdirectories);


            if(!it73.filePath().isEmpty())
            {
                // use
                serialPortRig = it73.filePath(); // first
            } else if(!it97.filePath().isEmpty())
            {
                // IC-9700 port
                serialPortRig = it97.filePath();
            } else if(!it785x.filePath().isEmpty())
            {
                // IC-785x port
                serialPortRig = it785x.filePath();
            } else if(!it705.filePath().isEmpty())
            {
                // IC-705
                serialPortRig = it705.filePath();
            } else {
                //fall back:
                qDebug() << "Could not find Icom serial port. Falling back to OS default. Use --port to specify, or modify preferences.";
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

        } else {
            if(serialPortCL.isEmpty())
            {
                serialPortRig = prefs.serialPortRadio;
            } else {
                serialPortRig = serialPortCL;
            }
        }

        // Here, the radioCIVAddr is being set from a default preference, which is for the 7300.
        // However, we will not use it initially. OTOH, if it is set explicitedly to a value in the prefs,
        // then we skip auto detection.
        emit sendCommSetup(prefs.radioCIVAddr, serialPortRig, prefs.serialPortBaud);
    }

    ui->statusBar->showMessage(QString("Connecting to rig using serial port ").append(serialPortRig), 1000);

/*
    if(prefs.radioCIVAddr == 0)
    {
        // tell rigCommander to broadcast a request for all rig IDs.
        // qDebug() << "Beginning search from wfview for rigCIV (auto-detection broadcast)";
        ui->statusBar->showMessage(QString("Searching CIV bus for connected radios."), 1000);
        emit getRigCIV();
        cmdOutQue.append(cmdGetRigCIV);
        delayedCommand->start();
    } else {
        // don't bother, they told us the CIV they want, stick with it.
        // We still query the rigID to find the model, but at least we know the CIV.
        qDebug() << "Skipping automatic CIV, using user-supplied value of " << prefs.radioCIVAddr;
        getInitialRigState();
    }
*/

}

void wfmain::receiveCommReady()
{
    qDebug() << "Received CommReady!! ";
    // taken from above:
    if(prefs.radioCIVAddr == 0)
    {
        // tell rigCommander to broadcast a request for all rig IDs.
        // qDebug() << "Beginning search from wfview for rigCIV (auto-detection broadcast)";
        ui->statusBar->showMessage(QString("Searching CIV bus for connected radios."), 1000);
        emit getRigCIV();
        cmdOutQue.append(cmdGetRigCIV);
        delayedCommand->start();
    } else {
        // don't bother, they told us the CIV they want, stick with it.
        // We still query the rigID to find the model, but at least we know the CIV.
        qDebug() << "Skipping automatic CIV, using user-supplied value of " << prefs.radioCIVAddr;
        getInitialRigState();
    }

}


void wfmain::receiveFoundRigID(rigCapabilities rigCaps)
{
    // Entry point for unknown rig being identified at the start of the program.
    //now we know what the rig ID is:
    //qDebug() << "In wfview, we now have a reply to our request for rig identity sent to CIV BROADCAST.";

    // We have to be careful here:
    // If we enter this a second time, we will get two sets of DV and DD modes
    // Also, if ever there is a rig with DV but without DV, we'll be off by one.
    // A better solution is to translate the combo selection to a shared type
    // such as an enum or even the actual CIV mode byte.

    if(rigCaps.hasDV)
    {
        ui->modeSelectCombo->addItem("DV");
    }
    if(rigCaps.hasDD)
    {
        ui->modeSelectCombo->addItem("DD");
    }

    delayedCommand->setInterval(100); // faster polling is ok now.
    receiveRigID(rigCaps);
    getInitialRigState();

    QString message = QString("Found model: ").append(rigCaps.modelName);

    ui->statusBar->showMessage(message, 1500);

    return;
}

void wfmain::receiveSerialPortError(QString port, QString errorText)
{
    qDebug() << "wfmain: received serial port error for port: " << port << " with message: " << errorText;
    ui->statusBar->showMessage(QString("ERROR: using port ").append(port).append(": ").append(errorText), 10000);

    // TODO: Dialog box, exit, etc
}

void wfmain::receiveStatusUpdate(QString text)
{
    this->rigStatus->setText(text);
}

void wfmain::setDefPrefs()
{
    defPrefs.useFullScreen = false;
    defPrefs.useDarkMode = true;
    defPrefs.drawPeaks = true;
    defPrefs.stylesheetPath = QString("qdarkstyle/style.qss");
    defPrefs.radioCIVAddr = 0x00; // previously was 0x94 for 7300.
    defPrefs.serialPortRadio = QString("auto");
    defPrefs.serialPortBaud = 115200;
    defPrefs.enablePTT = false;
    defPrefs.niceTS = true;

    defPrefs.enableLAN = false;
    defPrefs.ipAddress = QString("");
    defPrefs.controlLANPort = 50001;
    defPrefs.serialLANPort = 50002;
    defPrefs.audioLANPort = 50003;
    defPrefs.username = QString("");
    defPrefs.password = QString("");
    defPrefs.audioOutput = QAudioDeviceInfo::defaultOutputDevice().deviceName();
    defPrefs.audioInput = QAudioDeviceInfo::defaultInputDevice().deviceName();
    defPrefs.audioRXBufferSize = 12000;
    defPrefs.audioRXSampleRate = 48000;
    defPrefs.audioRXCodec = 4;
    defPrefs.audioTXSampleRate = 48000;
    defPrefs.audioTXCodec = 4;


}

void wfmain::loadSettings()
{
    qDebug() << "Loading settings from " << settings.fileName();

    // Basic things to load:
    // UI: (full screen, dark theme, draw peaks, colors, etc)
    settings.beginGroup("Interface");
    prefs.useFullScreen = settings.value("UseFullScreen", defPrefs.useFullScreen).toBool();
    prefs.useDarkMode = settings.value("UseDarkMode", defPrefs.useDarkMode).toBool();
    prefs.drawPeaks = settings.value("DrawPeaks", defPrefs.drawPeaks).toBool();
    prefs.stylesheetPath = settings.value("StylesheetPath", defPrefs.stylesheetPath).toString();
    ui->splitter->restoreState(settings.value("splitter").toByteArray());

    restoreGeometry(settings.value("windowGeometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
    setWindowState(Qt::WindowActive); // Works around QT bug to returns window+keyboard focus.
    settings.endGroup();

    // Radio and Comms: C-IV addr, port to use
    settings.beginGroup("Radio");
    prefs.radioCIVAddr = (unsigned char) settings.value("RigCIVuInt", defPrefs.radioCIVAddr).toInt();
    prefs.serialPortRadio = settings.value("SerialPortRadio", defPrefs.serialPortRadio).toString();
    prefs.serialPortBaud = (quint32) settings.value("SerialPortBaud", defPrefs.serialPortBaud).toInt();
    settings.endGroup();

    // Misc. user settings (enable PTT, draw peaks, etc)
    settings.beginGroup("Controls");
    prefs.enablePTT = settings.value("EnablePTT", defPrefs.enablePTT).toBool();
    ui->pttEnableChk->setChecked(prefs.enablePTT);
    prefs.niceTS = settings.value("NiceTS", defPrefs.niceTS).toBool();
    settings.endGroup();

    settings.beginGroup("LAN");
    prefs.enableLAN = settings.value("EnableLAN", defPrefs.enableLAN).toBool();
    ui->lanEnableChk->setChecked(prefs.enableLAN);
    
    prefs.ipAddress = settings.value("IPAddress", defPrefs.ipAddress).toString();
    ui->ipAddressTxt->setEnabled(ui->lanEnableChk->isChecked());
    ui->ipAddressTxt->setText(prefs.ipAddress);
    
    prefs.controlLANPort = settings.value("ControlLANPort", defPrefs.controlLANPort).toInt();
    ui->controlPortTxt->setEnabled(ui->lanEnableChk->isChecked());
    ui->controlPortTxt->setText(QString("%1").arg(prefs.controlLANPort));
    
    prefs.serialLANPort = settings.value("SerialLANPort", defPrefs.serialLANPort).toInt();
    ui->serialPortTxt->setEnabled(ui->lanEnableChk->isChecked());
    ui->serialPortTxt->setText(QString("%1").arg(prefs.serialLANPort));
    
    prefs.audioLANPort = settings.value("AudioLANPort", defPrefs.audioLANPort).toInt();
    ui->audioPortTxt->setEnabled(ui->lanEnableChk->isChecked());
    ui->audioPortTxt->setText(QString("%1").arg(prefs.audioLANPort));

    prefs.username = settings.value("Username", defPrefs.username).toString();
    ui->usernameTxt->setEnabled(ui->lanEnableChk->isChecked());
    ui->usernameTxt->setText(QString("%1").arg(prefs.username));
    
    prefs.password = settings.value("Password", defPrefs.password).toString();
    ui->passwordTxt->setEnabled(ui->lanEnableChk->isChecked());
    ui->passwordTxt->setText(QString("%1").arg(prefs.password));

    prefs.audioRXBufferSize = settings.value("AudioRXBufferSize", defPrefs.audioRXBufferSize).toInt();
    ui->audioBufferSizeSlider->setEnabled(ui->lanEnableChk->isChecked());
    ui->audioBufferSizeSlider->setValue(prefs.audioRXBufferSize);
    ui->audioBufferSizeSlider->setTracking(false); // Stop it sending value on every change.

    prefs.audioRXSampleRate = settings.value("AudioRXSampleRate", defPrefs.audioRXSampleRate).toInt();
    prefs.audioTXSampleRate = settings.value("AudioTXSampleRate", defPrefs.audioTXSampleRate).toInt();
    ui->audioSampleRateCombo->setEnabled(ui->lanEnableChk->isChecked());
    int audioSampleRateIndex = ui->audioSampleRateCombo->findText(QString::number(prefs.audioRXSampleRate));
    if (audioSampleRateIndex != -1) {
        ui->audioOutputCombo->setCurrentIndex(audioSampleRateIndex);
    }

    // Add codec combobox items here so that we can add userdata!
    ui->audioRXCodecCombo->addItem("LPCM 1ch 16bit", 4);
    ui->audioRXCodecCombo->addItem("LPCM 1ch 8bit", 1);
    ui->audioRXCodecCombo->addItem("uLaw 1ch 8bit", 2);
    ui->audioRXCodecCombo->addItem("LPCM 2ch 16bit", 16);
    ui->audioRXCodecCombo->addItem("uLaw 2ch 8bit", 32);
    ui->audioRXCodecCombo->addItem("PCM 2ch 8bit", 8);

    prefs.audioRXCodec = settings.value("AudioRXCodec", defPrefs.audioRXCodec).toInt();
    ui->audioRXCodecCombo->setEnabled(ui->lanEnableChk->isChecked());
    for (int f = 0; f < ui->audioRXCodecCombo->count(); f++)
        if (ui->audioRXCodecCombo->itemData(f).toInt() == prefs.audioRXCodec)
            ui->audioRXCodecCombo->setCurrentIndex(f);

    ui->audioTXCodecCombo->addItem("LPCM 1ch 16bit", 4);
    ui->audioTXCodecCombo->addItem("LPCM 1ch 8bit", 1);
    ui->audioTXCodecCombo->addItem("uLaw 1ch 8bit", 2);

    prefs.audioTXCodec = settings.value("AudioTXCodec", defPrefs.audioTXCodec).toInt();
    ui->audioTXCodecCombo->setEnabled(ui->lanEnableChk->isChecked());
    for (int f = 0; f < ui->audioTXCodecCombo->count(); f++)
        if (ui->audioTXCodecCombo->itemData(f).toInt() == prefs.audioTXCodec)
            ui->audioTXCodecCombo->setCurrentIndex(f);

    prefs.audioOutput = settings.value("AudioOutput", defPrefs.audioOutput).toString();
    ui->audioOutputCombo->setEnabled(ui->lanEnableChk->isChecked());
    int audioOutputIndex = ui->audioOutputCombo->findText(prefs.audioOutput);
    if (audioOutputIndex != -1)
        ui->audioOutputCombo->setCurrentIndex(audioOutputIndex);

    prefs.audioInput = settings.value("AudioInput", defPrefs.audioInput).toString();
    ui->audioInputCombo->setEnabled(ui->lanEnableChk->isChecked());
    int audioInputIndex = ui->audioInputCombo->findText(prefs.audioInput);
    if (audioInputIndex != - 1)
        ui->audioOutputCombo->setCurrentIndex(audioInputIndex);

    settings.endGroup();
    // Memory channels

    settings.beginGroup("Memory");
    int size = settings.beginReadArray("Channel");
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
        settings.setArrayIndex(i);
        chan = settings.value("chan", 0).toInt();
        freq = settings.value("freq", 12.345).toDouble();
        mode = settings.value("mode", 0).toInt();
        isSet = settings.value("isSet", false).toBool();

        if(isSet)
        {
            mem.setPreset(chan, freq, (mode_kind)mode);
        }
    }

    settings.endArray();
    settings.endGroup();
}



void wfmain::saveSettings()
{
    qDebug() << "Saving settings to " << settings.fileName();
    // Basic things to load:

    // UI: (full screen, dark theme, draw peaks, colors, etc)
    settings.beginGroup("Interface");
    settings.setValue("UseFullScreen", prefs.useFullScreen);
    settings.setValue("UseDarkMode", prefs.useDarkMode);
    settings.setValue("DrawPeaks", prefs.drawPeaks);
    settings.setValue("StylesheetPath", prefs.stylesheetPath);
    settings.setValue("splitter", ui->splitter->saveState());
    settings.setValue("windowGeometry", saveGeometry());
    settings.setValue("windowState", saveState());
    settings.endGroup();

    // Radio and Comms: C-IV addr, port to use
    settings.beginGroup("Radio");
    settings.setValue("RigCIVuInt", prefs.radioCIVAddr);
    settings.setValue("SerialPortRadio", prefs.serialPortRadio);
    settings.setValue("SerialPortBaud", prefs.serialPortBaud);
    settings.endGroup();

    // Misc. user settings (enable PTT, draw peaks, etc)
    settings.beginGroup("Controls");
    settings.setValue("EnablePTT", prefs.enablePTT);
    settings.setValue("NiceTS", prefs.niceTS);
    settings.endGroup();

    settings.beginGroup("LAN");
    settings.setValue("EnableLAN", prefs.enableLAN);
    settings.setValue("IPAddress", prefs.ipAddress);
    settings.setValue("ControlLANPort", prefs.controlLANPort);
    settings.setValue("SerialLANPort", prefs.serialLANPort);
    settings.setValue("AudioLANPort", prefs.audioLANPort);
    settings.setValue("Username", prefs.username);
    settings.setValue("Password", prefs.password);
    settings.setValue("AudioRXBufferSize", prefs.audioRXBufferSize);
    settings.setValue("AudioRXSampleRate", prefs.audioRXSampleRate);
    settings.setValue("AudioRXCodec", prefs.audioRXCodec);
    settings.setValue("AudioTXBufferSize", prefs.audioRXBufferSize);
    settings.setValue("AudioTXSampleRate", prefs.audioRXSampleRate);
    settings.setValue("AudioTXCodec", prefs.audioTXCodec);
    settings.setValue("AudioOutput", prefs.audioOutput);
    settings.setValue("AudioInput", prefs.audioInput);
    settings.endGroup();

    // Memory channels
    settings.beginGroup("Memory");
    settings.beginWriteArray("Channel", (int)mem.getNumPresets());

    preset_kind temp;
    for(int i=0; i < (int)mem.getNumPresets(); i++)
    {
        temp = mem.getPreset((int)i);
        settings.setArrayIndex(i);
        settings.setValue("chan", i);
        settings.setValue("freq", temp.frequency);
        settings.setValue("mode", temp.mode);
        settings.setValue("isSet", temp.isSet);
    }

    settings.endArray();
    settings.endGroup();

    // Note: X and Y get the same colors. See setPlotTheme() function

    settings.beginGroup("DarkColors");

    settings.setValue("Dark_PlotBackground", QColor(0,0,0,255));
    settings.setValue("Dark_PlotAxisPen", QColor(75,75,75,255));

    settings.setValue("Dark_PlotLegendTextColor", QColor(255,255,255,255));
    settings.setValue("Dark_PlotLegendBorderPen", QColor(255,255,255,255));
    settings.setValue("Dark_PlotLegendBrush", QColor(0,0,0,200));

    settings.setValue("Dark_PlotTickLabel", QColor(Qt::white));
    settings.setValue("Dark_PlotBasePen", QColor(Qt::white));
    settings.setValue("Dark_PlotTickPen", QColor(Qt::white));
    settings.setValue("Dark_PlotFreqTracer", QColor(Qt::yellow));

    settings.endGroup();

    settings.beginGroup("LightColors");

    settings.setValue("Light_PlotBackground", QColor(255,255,255,255));
    settings.setValue("Light_PlotAxisPen", QColor(200,200,200,255));

    settings.setValue("Light_PlotLegendTextColor", QColor(0,0,0,255));
    settings.setValue("Light_PlotLegendBorderPen", QColor(0,0,0,255));
    settings.setValue("Light_PlotLegendBrush", QColor(255,255,255,200));

    settings.setValue("Light_PlotTickLabel", QColor(Qt::black));
    settings.setValue("Light_PlotBasePen", QColor(Qt::black));
    settings.setValue("Light_PlotTickPen", QColor(Qt::black));
    settings.setValue("Light_PlotFreqTracer", QColor(Qt::blue));

    settings.endGroup();

    // This is a reference to see how the preference file is encoded.
    settings.beginGroup("StandardColors");

    settings.setValue("white", QColor(Qt::white));
    settings.setValue("black", QColor(Qt::black));

    settings.setValue("red_opaque", QColor(Qt::red));
    settings.setValue("red_translucent", QColor(255,0,0,128));
    settings.setValue("green_opaque", QColor(Qt::green));
    settings.setValue("green_translucent", QColor(0,255,0,128));
    settings.setValue("blue_opaque", QColor(Qt::blue));
    settings.setValue("blue_translucent", QColor(0,0,255,128));

    settings.endGroup();


    settings.sync(); // Automatic, not needed (supposedly)
}

void wfmain::prepareWf()
{
    // All this code gets moved in from the constructor of wfmain.

    if(haveRigCaps)
    {
        // do things
        spectWidth = rigCaps.spectLenMax; // was fixed at 475
        wfLength = 160; // fixed for now, time-length of waterfall

        // Initialize before use!

        QByteArray empty((int)spectWidth, '\x01');
        spectrumPeaks = QByteArray( (int)spectWidth, '\x01' );
        for(quint16 i=0; i<wfLength; i++)
        {
            wfimage.append(empty);
        }

        // from line 305-313:
        colorMap->data()->setValueRange(QCPRange(0, wfLength-1));
        colorMap->data()->setKeyRange(QCPRange(0, spectWidth-1));
        colorMap->setDataRange(QCPRange(0, rigCaps.spectAmpMax));
        colorMap->setGradient(QCPColorGradient::gpJet); // TODO: Add preference
        colorMapData = new QCPColorMapData(spectWidth, wfLength, QCPRange(0, spectWidth-1), QCPRange(0, wfLength-1));
        colorMap->setData(colorMapData);
        spectRowCurrent = 0;
        wf->yAxis->setRangeReversed(true);
        wf->xAxis->setVisible(false);

    } else {
        qDebug() << "Cannot prepare WF view without rigCaps. Waiting on this.";
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
    ui->modeSelectCombo->setCurrentIndex(0);
    on_modeSelectCombo_activated(0);
}

void wfmain::shortcutF6()
{
    // USB
    ui->modeSelectCombo->setCurrentIndex(1);
    on_modeSelectCombo_activated(1);
}

void wfmain::shortcutF7()
{
    // AM
    ui->modeSelectCombo->setCurrentIndex(2);
    on_modeSelectCombo_activated(2);
}

void wfmain::shortcutF8()
{
    // CW
    ui->modeSelectCombo->setCurrentIndex(3);
    on_modeSelectCombo_activated(3);
}

void wfmain::shortcutF9()
{
    // USB-D
    ui->modeSelectCombo->setCurrentIndex(9);
    on_modeSelectCombo_activated(9);
}

void wfmain::shortcutF10()
{
    // Build information, debug, whatever you wish
    QString buildInfo = QString("Build " + QString(GITSHORT) + " on " + QString(__DATE__) + " at " + __TIME__ + " by " + UNAME + "@" + HOST);
    showStatusBarText(buildInfo);
}

void wfmain::shortcutF12()
{
    // Speak current frequency and mode via IC-7300
    showStatusBarText("Sending speech command to radio.");
    emit sayAll();
}

void wfmain::shortcutControlT()
{
    // Transmit
    qDebug() << "Activated Control-T shortcut";
    showStatusBarText(QString("Transmitting. Press Control-R to receive."));
    ui->pttOnBtn->click();
}

void wfmain::shortcutControlR()
{
    // Receive
    ui->pttOffBtn->click();
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

void wfmain::shortcutMinus()
{
    ui->freqDial->setValue( ui->freqDial->value() - ui->freqDial->singleStep() );
}

void wfmain::shortcutPlus()
{
    ui->freqDial->setValue( ui->freqDial->value() + ui->freqDial->singleStep() );
}

void wfmain::shortcutShiftMinus()
{
    ui->freqDial->setValue( ui->freqDial->value() - ui->freqDial->pageStep() );
}

void wfmain::shortcutShiftPlus()
{
    ui->freqDial->setValue( ui->freqDial->value() + ui->freqDial->pageStep() );
}

void wfmain::shortcutControlMinus()
{
    ui->freqDial->setValue( ui->freqDial->value() - ui->freqDial->pageStep() );
}

void wfmain::shortcutControlPlus()
{
    ui->freqDial->setValue( ui->freqDial->value() + ui->freqDial->pageStep() );
}

void wfmain::shortcutPageUp()
{
    emit setFrequency(this->freqMhz + 1.0);
    cmdOutQue.append(cmdGetFreq);
    //cmdOutQue.append(cmdGetMode); // maybe not really needed.
    delayedCommand->start();
}

void wfmain::shortcutPageDown()
{
    emit setFrequency(this->freqMhz - 1.0);
    cmdOutQue.append(cmdGetFreq);
    //cmdOutQue.append(cmdGetMode); // maybe not really needed.
    delayedCommand->start();
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


void wfmain:: getInitialRigState()
{
    // Initial list of queries to the radio.
    // These are made when the program starts up
    // and are used to adjust the UI to match the radio settings
    // the polling interval is set at 100ms. Faster is possible but slower
    // computers will glitch occassionally.

    //cmdOutQue.append(cmdGetRigID);

    cmdOutQue.append(cmdGetFreq);
    cmdOutQue.append(cmdGetMode);

    cmdOutQue.append(cmdNone);

    cmdOutQue.append(cmdGetFreq);
    cmdOutQue.append(cmdGetMode);

    cmdOutQue.append(cmdGetRxGain);
    cmdOutQue.append(cmdGetAfGain);
    cmdOutQue.append(cmdGetSql); // implimented but not used
    // TODO:
    // get TX level
    // get Scope reference Level

    //cmdOutQue.append(cmdNone);
    //cmdOutQue.append(cmdGetRigID);
    //cmdOutQue.append(cmdNone);
    //cmdOutQue.append(cmdGetRigID);

    cmdOutQue.append(cmdDispEnable);
    cmdOutQue.append(cmdSpecOn);

    // get spectrum mode (center or edge)
    // get spectrum span or edge limit number [1,2,3], update UI

    cmdOutQue.append(cmdNone);

    cmdOutQue.append(cmdGetATUStatus);

    cmdOut = cmdNone;
    delayedCommand->start();
}

void wfmain::showStatusBarText(QString text)
{
    ui->statusBar->showMessage(text, 5000);
}

void wfmain::on_useDarkThemeChk_clicked(bool checked)
{
    setAppTheme(checked);
    setPlotTheme(wf, checked);
    setPlotTheme(plot, checked);
    prefs.useDarkMode = checked;
}

void wfmain::setAppTheme(bool isDark)
{
    if(isDark)
    {
        // QFile f(":qdarkstyle/style.qss"); // built-in resource
        QFile f("/usr/share/wfview/stylesheets/" + prefs.stylesheetPath);
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
    defaultColors.Dark_PlotFreqTracer = QColor(Qt::yellow);

    defaultColors.Light_PlotBackground = QColor(255,255,255,255);
    defaultColors.Light_PlotAxisPen = QColor(200,200,200,255);
    defaultColors.Light_PlotLegendTextColor = QColor(0,0,0,255);
    defaultColors.Light_PlotLegendBorderPen = QColor(0,0,0,255);
    defaultColors.Light_PlotLegendBrush = QColor(255,255,255,200);
    defaultColors.Light_PlotTickLabel = QColor(Qt::black);
    defaultColors.Light_PlotBasePen = QColor(Qt::black);
    defaultColors.Light_PlotTickPen = QColor(Qt::black);
    defaultColors.Light_PlotFreqTracer = QColor(Qt::blue);
}

void wfmain::setPlotTheme(QCustomPlot *plot, bool isDark)
{
    if(isDark)
    {
        plot->setBackground(QColor(0,0,0,255));
        plot->xAxis->grid()->setPen(QColor(75,75,75,255));
        plot->yAxis->grid()->setPen(QColor(75,75,75,255));

        plot->legend->setTextColor(QColor(255,255,255,255));
        plot->legend->setBorderPen(QColor(255,255,255,255));
        plot->legend->setBrush(QColor(0,0,0,200));

        plot->xAxis->setTickLabelColor(Qt::white);
        plot->xAxis->setLabelColor(Qt::white);
        plot->yAxis->setTickLabelColor(Qt::white);
        plot->yAxis->setLabelColor(Qt::white);
        plot->xAxis->setBasePen(QPen(Qt::white));
        plot->xAxis->setTickPen(QPen(Qt::white));
        plot->yAxis->setBasePen(QPen(Qt::white));
        plot->yAxis->setTickPen(QPen(Qt::white));
        plot->graph(0)->setPen(QPen(Qt::yellow)); // magenta, yellow, green, lightGray
    } else {
        //color = ui->groupBox->palette().color(QPalette::Button);

        plot->setBackground(QColor(255,255,255,255));
        //plot->setBackground(color);

        plot->xAxis->grid()->setPen(QColor(200,200,200,255));
        plot->yAxis->grid()->setPen(QColor(200,200,200,255));

        plot->legend->setTextColor(QColor(0,0,0,255));
        plot->legend->setBorderPen(QColor(0,0,0,255));
        plot->legend->setBrush(QColor(255,255,255,200));

        plot->xAxis->setTickLabelColor(Qt::black);
        plot->xAxis->setLabelColor(Qt::black);
        plot->yAxis->setTickLabelColor(Qt::black);
        plot->yAxis->setLabelColor(Qt::black);
        plot->xAxis->setBasePen(QPen(Qt::black));
        plot->xAxis->setTickPen(QPen(Qt::black));
        plot->yAxis->setBasePen(QPen(Qt::black));
        plot->yAxis->setTickPen(QPen(Qt::black));
        plot->graph(0)->setPen(QPen(Qt::blue));
    }
}

void wfmain::runDelayedCommand()
{
    cmds qdCmd;
    // Note: This cmdOut queue will be removed entirely soon and only the cmdOutQue will be available.
    switch (cmdOut)
    {
        case cmdGetFreq:
            emit getFrequency();
            break;
        case cmdGetMode:
            emit getMode();
            break;
        default:
            break;
    }
    cmdOut = cmdNone; // yep. Hope this wasn't called twice in a row rapidly.

    // Note: All command should use this queue. There is no need to use the above system.

    if(!cmdOutQue.isEmpty())
    {
        qdCmd = cmdOutQue.takeFirst();
        switch(qdCmd)
        {
            case cmdNone:
                //qDebug() << "NOOP";
                break;
            case cmdGetRigID:
                emit getRigID();
                break;
            case cmdGetRigCIV:
                // if(!know rig civ already)
                if(!haveRigCaps)
                {
                    emit getRigCIV();
                    cmdOutQue.append(cmdGetRigCIV); // This way, we stay here until we get an answer.
                }
                break;
            case cmdGetFreq:
                emit getFrequency();
                break;
            case cmdGetMode:
                emit getMode();
                break;
            case cmdGetDataMode:
                // qDebug() << "Sending query for data mode";
                emit getDataMode();
                break;
            case cmdSetDataModeOff:
                emit setDataMode(false);
                break;
            case cmdSetDataModeOn:
                emit setDataMode(true);
                break;
            case cmdDispEnable:
                emit scopeDisplayEnable();
                break;
            case cmdDispDisable:
                emit scopeDisplayDisable();
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
            case cmdGetATUStatus:
                emit getATUStatus();
                break;
            case cmdScopeCenterMode:
                emit setScopeCenterMode(true);
                break;
            case cmdScopeFixedMode:
                emit setScopeCenterMode(false);
                break;
            default:
                break;
        }
    }
    if(cmdOutQue.isEmpty())
    {
        // done
    } else {
        // next
        // TODO: If we always do ->start, then it will not be necessary for
        // every command insertion to include a ->start.... probably worth doing.
        delayedCommand->start();
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
#ifdef QT_DEBUG
        qDebug() << "Rig name: " << rigCaps.modelName;
        qDebug() << "Has LAN capabilities: " << rigCaps.hasLan;
        qDebug() << "Rig ID received into wfmain: spectLenMax: " << rigCaps.spectLenMax;
        qDebug() << "Rig ID received into wfmain: spectAmpMax: " << rigCaps.spectAmpMax;
        qDebug() << "Rig ID received into wfmain: spectSeqMax: " << rigCaps.spectSeqMax;
        qDebug() << "Rig ID received into wfmain: hasSpectrum: " << rigCaps.hasSpectrum;
#endif
        this->rigCaps = rigCaps;
        this->spectWidth = rigCaps.spectLenMax; // used once haveRigCaps is true.
        haveRigCaps = true;
        ui->connectBtn->setText("Disconnect"); // We must be connected now.
        prepareWf();
        // Adding these here because clearly at this point we have valid
        // rig comms. In the future, we should establish comms and then
        // do all the initial grabs. For now, this hack of adding them here and there:
        cmdOutQue.append(cmdGetFreq);
        cmdOutQue.append(cmdGetMode);
    }
}

void wfmain::receiveFreq(double freqMhz)
{
    //qDebug() << "HEY WE GOT A Frequency: " << freqMhz;
    ui->freqLabel->setText(QString("%1").arg(freqMhz, 0, 'f'));
    this->freqMhz = freqMhz;
    this->knobFreqMhz = freqMhz;
    //showStatusBarText(QString("Frequency: %1").arg(freqMhz));
}

void wfmain::receivePTTstatus(bool pttOn)
{
    // NOTE: This will only show up if we actually receive a PTT status
    qDebug() << "PTT status: " << pttOn;
}

void wfmain::receiveSpectrumData(QByteArray spectrum, double startFreq, double endFreq)
{
    if(!haveRigCaps)
    {
#ifdef QT_DEBUG
        qDebug() << "Spectrum received, but RigID incomplete.";
#endif
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

    //qDebug() << "start: " << startFreq << " end: " << endFreq;
    quint16 specLen = spectrum.length();
    //qDebug() << "Spectrum data received at UI! Length: " << specLen;
    //if( (specLen != 475) || (specLen!=689) )

    if( specLen != rigCaps.spectLenMax )
    {
#ifdef QT_DEBUG
        qDebug() << "-------------------------------------------";
        qDebug() << "------ Unusual spectrum received, length: " << specLen;
        qDebug() << "------ Expected spectrum length: " << rigCaps.spectLenMax;
        qDebug() << "------ This should happen once at most. ";
#endif
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
        y[i] = spectrum.at(i);
        if(drawPeaks)
        {
            if(spectrum.at(i) > spectrumPeaks.at(i))
            {
                spectrumPeaks[i] = spectrum.at(i);
            }
            y2[i] = spectrumPeaks.at(i);
        }

    }

    //ui->qcp->addGraph();
    plot->graph(0)->setData(x,y);
    if((freqMhz < endFreq) && (freqMhz > startFreq))
    {
        // tracer->setGraphKey(freqMhz);
        tracer->setGraphKey(knobFreqMhz);

    }
    if(drawPeaks)
    {
        plot->graph(1)->setData(x,y2); // peaks
    }
    plot->yAxis->setRange(0, 160);
    plot->xAxis->setRange(startFreq, endFreq);
    plot->replot();

    if(specLen == spectWidth)
    {
        wfimage.prepend(spectrum);
        if(wfimage.length() >  wfLength)
        {
            wfimage.remove(wfLength);
        }

        // Waterfall:
        for(int row = 0; row < wfLength; row++)
        {
            for(int col = 0; col < spectWidth; col++)
            {
                //colorMap->data()->cellToCoord(xIndex, yIndex, &x, &y)
                // Very fast but doesn't roll downward:
                //colorMap->data()->setCell( col, spectRowCurrent, spectrum.at(col) );
                // Slow but rolls:
                colorMap->data()->setCell( col, row, wfimage.at(row).at(col));
            }
        }

        //colorMap->data()->setRange(QCPRange(startFreq, endFreq), QCPRange(0,wfLength-1));
        wf->yAxis->setRange(0,wfLength - 1);
        wf->xAxis->setRange(0, spectWidth-1);
        wf->replot();
        spectRowCurrent = (spectRowCurrent + 1) % wfLength;
        //qDebug() << "updating spectrum, new row is: " << spectRowCurrent;

    }
}

void wfmain::receiveSpectrumFixedMode(bool isFixed)
{
    ui->scopeCenterModeChk->blockSignals(true);
    ui->scopeCenterModeChk->setChecked(!isFixed);
    ui->scopeCenterModeChk->blockSignals(false);
}

void wfmain::handlePlotDoubleClick(QMouseEvent *me)
{
    double x;
    //double y;
    //double px;
    x = plot->xAxis->pixelToCoord(me->pos().x());
    //y = plot->yAxis->pixelToCoord(me->pos().y());
    emit setFrequency(x);
    cmdOut = cmdGetFreq;
    delayedCommand->start();
    showStatusBarText(QString("Going to %1 MHz").arg(x));
}

void wfmain::handleWFDoubleClick(QMouseEvent *me)
{
    double x;
    //double y;
    //x = wf->xAxis->pixelToCoord(me->pos().x());
    //y = wf->yAxis->pixelToCoord(me->pos().y());
    // cheap trick until I figure out how the axis works on the WF:
    x = plot->xAxis->pixelToCoord(me->pos().x());
    emit setFrequency(x);
    cmdOut = cmdGetFreq;
    delayedCommand->start();
    showStatusBarText(QString("Going to %1 MHz").arg(x));
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

    // TODO: Use other method, knob has too few positions to be useful for large steps.

    int steps = we->angleDelta().y() / 120;
    Qt::KeyboardModifiers key=  we->modifiers();

    if (key == Qt::ShiftModifier)
    {
        steps *=20;
    } else if (key == Qt::ControlModifier)
    {
        steps *=10;
    }

    ui->freqDial->setValue( ui->freqDial->value() + (steps)*ui->freqDial->singleStep() );
}

void wfmain::handlePlotScroll(QWheelEvent *we)
{
    int steps = we->angleDelta().y() / 120;
    Qt::KeyboardModifiers key=  we->modifiers();

    if (key == Qt::ShiftModifier)
    {
        // TODO: Zoom
    } else if (key == Qt::ControlModifier)
    {
        steps *=10;
    }

    ui->freqDial->setValue( ui->freqDial->value() + (steps)*ui->freqDial->singleStep() );
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

void wfmain::on_startBtn_clicked()
{
    emit spectOutputEnable();
}

void wfmain::on_stopBtn_clicked()
{
    emit spectOutputDisable();
    //emit scopeDisplayDisable();
}

void wfmain::receiveMode(QString mode)
{
    //ui->modeLabel->setText(mode);
    int index;
    //bool ok;
    index = modes.indexOf(QRegExp(mode)); // find the number corresponding to the mode
    // qDebug() << "Received mode " << mode << " current mode: " << currentModeIndex << " search index: " << index;
    if( currentModeIndex == index)
    {
        // do nothing, no need to change the selected mode and fire more events off.
        // TODO/NOTE: This will not check the DATA mode status, may be worth re-thinking this.
        // Do not update UI.
        // return;
    } else if((index >= 0) && (index < 9))
    {
        ui->modeSelectCombo->blockSignals(true);
        ui->modeSelectCombo->setCurrentIndex(index);
        ui->modeSelectCombo->blockSignals(false);
        currentModeIndex = index;
    }
    // Note: we need to know if the DATA mode is active to reach mode-D
    // some kind of queued query:
    cmdOutQue.append(cmdGetDataMode);
    delayedCommand->start(); // why was that commented out?
}

void wfmain::receiveDataModeStatus(bool dataEnabled)
{
    // qDebug() << "Received data mode " << dataEnabled << "\n";
    if(dataEnabled)
    {
        if(currentModeIndex == 0)
        {
            // LSB
            ui->modeSelectCombo->setCurrentIndex(8);
            //ui->modeLabel->setText( "LSB-D" );
        } else if (currentModeIndex == 1)
        {
            // USB
            ui->modeSelectCombo->setCurrentIndex(9);
            //ui->modeLabel->setText( "USB-D" );
        } 
    } else {
        // update to _not_ have the -D
        ui->modeSelectCombo->setCurrentIndex(currentModeIndex);
        // No need to update status label?
    }
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
    bool ok = false;
    double freq = ui->freqMhzLineEdit->text().toDouble(&ok);
    if(ok)
    {
        emit setFrequency(freq);
        // TODO: change to cmdQueue
        cmdOut = cmdGetFreq;
        delayedCommand->start();
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

void wfmain::on_scopeCenterModeChk_clicked(bool checked)
{
    emit setScopeCenterMode(checked);
}

void wfmain::on_fEnterBtn_clicked()
{
    // TODO: do not jump to main tab on enter, only on return
    // or something.
    // Maybe this should be an option in settings.
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

void wfmain::on_modeSelectCombo_activated(int index)
{
    // Reference:
    //          0      1        2         3       4
    //modes << "LSB" << "USB" << "AM" << "CW" << "RTTY";
    //          5      6          7           8          9
    //modes << "FM" << "CW-R" << "RTTY-R" << "LSB-D" << "USB-D";

    // The "acticvated" signal means the user initiated a mode change.
    // This function is not called if code initated the change.
    if(index < 10)
    {
        // qDebug() << "Mode selection changed. index: " << index;

        if(index > 7)
        {
            // set data mode on
            // emit setDataMode(true);
            cmdOutQue.append(cmdSetDataModeOn);
            delayedCommand->start();
            index = index - 8;
        } else {
            // set data mode off
            //emit setDataMode(false);
            cmdOutQue.append(cmdSetDataModeOff);
            delayedCommand->start();
        }

        emit setMode(index);
    }

}

//void wfmain::on_freqDial_actionTriggered(int action)
//{
    //qDebug() << "Action: " << action; // "7" == changed?
    // TODO: remove this
//}

void wfmain::on_freqDial_valueChanged(int value)
{
    // qDebug() << "Old value: " << oldFreqDialVal << " New value: " << value ;
    double stepSize = 0.000100; // 100 Hz steps
    double newFreqMhz = 0;
    volatile int delta = 0;
    int maxVal = ui->freqDial->maximum();

    int directPath = 0;
    int crossingPath = 0;
    
    int distToMaxNew = 0;
    int distToMaxOld = 0;
    
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
        // mnow calculate the direction:
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


    newFreqMhz = knobFreqMhz + (delta  * stepSize);

    // qDebug() << "old freq: " << knobFreqMhz << " new freq: " << newFreqMhz << "knobDelta: " << delta << " freq delta: " << newFreqMhz - knobFreqMhz;

    if(ui->tuningFloorZerosChk->isChecked())
    {
        newFreqMhz = (double)round(newFreqMhz*10000) / 10000.0;
    }

    this->knobFreqMhz = newFreqMhz; // the frequency we think we should be on.

    oldFreqDialVal = value;

    ui->freqLabel->setText(QString("%1").arg(knobFreqMhz, 0, 'f'));

    emit setFrequency(newFreqMhz);
    //emit getFrequency();

}

void wfmain::receiveBandStackReg(float freq, char mode, bool dataOn)
{
    // read the band stack and apply by sending out commands

    setFrequency(freq);
    setMode(mode); // make sure this is what you think it is

    // setDataMode(dataOn); // signal out
    if(dataOn)
    {
        cmdOutQue.append(cmdSetDataModeOn);
    } else {
        cmdOutQue.append(cmdSetDataModeOff);
    }
    cmdOutQue.append(cmdGetFreq);
    cmdOutQue.append(cmdGetMode);
    ui->tabWidget->setCurrentIndex(0);

    delayedCommand->start();
}

void wfmain::bandStackBtnClick()
{
    bandStkRegCode = ui->bandStkPopdown->currentIndex() + 1;
    waitingForBandStackRtn = true; // so that when the return is parsed we jump to this frequency/mode info
    emit getBandStackReg(bandStkBand, bandStkRegCode);
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
    setFrequency(5.3305);
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

void wfmain::on_bandGenbtn_clicked()
{
    // "GENE" general coverage frequency outside the ham bands
    // which does probably include any 60 meter frequencies used.
    bandStkBand = 0x11; // GEN
    bandStackBtnClick();
}

void wfmain::on_aboutBtn_clicked()
{
    // Show.....
    // Build date, time, git checksum (short)
    // QT library version
    // stylesheet credit
    // contact information

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Abou wfview");
    msgBox.setTextFormat(Qt::RichText);
    msgBox.setWindowIcon(QIcon(":resources/wfview.png"));
    // TODO: change style of link color based on current CSS sheet.

    QString copyright = QString("Copyright 2017-2020 Elliott H. Liggett. All rights reserved.");
    QString ssCredit = QString("<br/>Stylesheet qdarkstyle used under MIT license, stored in /usr/share/wfview/stylesheets/.");
    QString contact = QString("<br/>email the author: kilocharlie8@gmail.com or W6EL on the air!");
    QString website = QString("<br/><br/>Get the latest version from our gitlab repo: <a href='https://gitlab.com/eliggett/wfview' style='color: cyan;'>https://gitlab.com/eliggett/wfview</a>");
    QString docs = QString("<br/>Also see the <a href='https://gitlab.com/eliggett/wfview/-/wikis/home'  style='color: cyan;'>wiki</a> for the <a href='https://gitlab.com/eliggett/wfview/-/wikis/User-FAQ' style='color: cyan;'>FAQ</a>, <a href='https://gitlab.com/eliggett/wfview/-/wikis/Keystrokes' style='color: cyan;'>Keystrokes</a>, and more.");
    QString buildInfo = QString("<br/><br/>Build " + QString(GITSHORT) + " on " + QString(__DATE__) + " at " + __TIME__ + " by " + UNAME + "@" + HOST);

    QString aboutText = copyright + "\n" + ssCredit + "\n";
    aboutText.append(contact + "\n" + website + "\n"+ docs +"\n" + buildInfo);

    msgBox.setText(aboutText);
    msgBox.exec();

}

void wfmain::on_aboutQtBtn_clicked()
{
    QMessageBox::aboutQt(this, "wfview");
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
    QString freqString;
    int preset_number = ui->freqMhzLineEdit->text().toInt(&ok);

    if(ok && (preset_number >= 0) && (preset_number < 100))
    {
        // TODO: keep an enum around with the current mode
        mem.setPreset(preset_number, freqMhz, (mode_kind)ui->modeSelectCombo->currentIndex());
        showStatusBarText( QString("Storing frequency %1 to memory location %2").arg( freqMhz ).arg(preset_number) );
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
        freqString = QString("%1").arg(temp.frequency);
        ui->freqMhzLineEdit->setText( freqString );
        ui->goFreqBtn->click();

    } else {
        qDebug() << "Could not recall preset. Valid presets are 0 through 99.";
    }

}

void wfmain::on_rfGainSlider_valueChanged(int value)
{
    emit setRfGain((unsigned char) value);
}

void wfmain::on_afGainSlider_valueChanged(int value)
{
    // qDebug() << "Setting AF gain to " << value;
    emit setAfGain((unsigned char) value);
}

void wfmain::receiveRfGain(unsigned char level)
{
    // qDebug() << "Receive RF  level of" << (int)level << " = " << 100*level/255.0 << "%";
    ui->rfGainSlider->blockSignals(true);
    ui->rfGainSlider->setValue(level);
    ui->rfGainSlider->blockSignals(false);
}

void wfmain::receiveAfGain(unsigned char level)
{
    // qDebug() << "Receive AF  level of" << (int)level << " = " << 100*level/255.0 << "%";
    ui->afGainSlider->blockSignals(true);
    ui->afGainSlider->setValue(level);
    ui->afGainSlider->blockSignals(false);
}

void wfmain::receiveSql(unsigned char level)
{
    qDebug() << "Receive SQL level of                   " << (int)level << " = " << 100*level/255.0 << "%";
    ui->sqlSlider->setValue(level);
    (void)level;
}

void wfmain::on_drawTracerChk_toggled(bool checked)
{
    tracer->setVisible(checked);
    prefs.drawTracer = checked;
}

void wfmain::on_tuneNowBtn_clicked()
{
    emit startATU();
    showStatusBarText("Starting ATU tuning cycle...");
    cmdOutQue.append(cmdGetATUStatus);
    delayedCommand->start();
}

void wfmain::on_tuneEnableChk_clicked(bool checked)
{
    emit setATU(checked);
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
    emit setPTT(true);
    // send PTT
    // Start 3 minute timer
    pttTimer->start();
}

void wfmain::on_pttOffBtn_clicked()
{
    // Send the PTT OFF command (more than once?)
    showStatusBarText("Sending PTT OFF command");
    emit setPTT(false);

    // Stop the 3 min timer
    pttTimer->stop();

}

void wfmain::handlePttLimit()
{
    // transmission time exceeded!
    showStatusBarText("Transmit timeout at 3 minutes. Sending PTT OFF command now.");
    emit setPTT(false);
}

void wfmain::on_saveSettingsBtn_clicked()
{
    saveSettings(); // save memory, UI, and radio settings
}

void wfmain::receiveATUStatus(unsigned char atustatus)
{
    // qDebug() << "Received ATU status update: " << (unsigned int) atustatus;
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
            // qDebug() << "Received ATU status update that *tuning* is taking place";
            showStatusBarText("ATU is Tuning...");
            cmdOutQue.append(cmdGetATUStatus); // Sometimes the first hit seems to be missed.
            cmdOutQue.append(cmdGetATUStatus);
            delayedCommand->start();
            break;
        default:
            qDebug() << "Did not understand ATU status: " << (unsigned int) atustatus;
            break;
    }
}

void wfmain::on_pttEnableChk_clicked(bool checked)
{
    prefs.enablePTT = checked;
}

void wfmain::on_lanEnableChk_clicked(bool checked)
{
    prefs.enableLAN = checked;
    ui->ipAddressTxt->setEnabled(checked);
    ui->controlPortTxt->setEnabled(checked);
    ui->serialPortTxt->setEnabled(checked);
    ui->audioPortTxt->setEnabled(checked);
    ui->usernameTxt->setEnabled(checked);
    ui->passwordTxt->setEnabled(checked);
    if(checked)
    {
        showStatusBarText("After filling in values, press Save Settings and re-start wfview.");
    }
}

void wfmain::on_ipAddressTxt_textChanged(QString text)
{
    prefs.ipAddress = text;
}

void wfmain::on_controlPortTxt_textChanged(QString text)
{
    prefs.controlLANPort = text.toUInt();
}

void wfmain::on_serialPortTxt_textChanged(QString text)
{
    prefs.serialLANPort = text.toUInt();
}

void wfmain::on_audioPortTxt_textChanged(QString text)
{
    prefs.audioLANPort = text.toUInt();
}

void wfmain::on_usernameTxt_textChanged(QString text)
{
    prefs.username = text;
}

void wfmain::on_passwordTxt_textChanged(QString text)
{
    prefs.password = text;
}

void wfmain::on_audioOutputCombo_currentIndexChanged(QString text)
{
    prefs.audioOutput = text;
}

void wfmain::on_audioInputCombo_currentIndexChanged(QString text)
{
    prefs.audioInput = text;
}

void wfmain::on_audioSampleRateCombo_currentIndexChanged(QString text)
{
    prefs.audioRXSampleRate = text.toInt();
    prefs.audioTXSampleRate = text.toInt();
}

void wfmain::on_audioRXCodecCombo_currentIndexChanged(int value)
{
    prefs.audioRXCodec = ui->audioRXCodecCombo->itemData(value).toInt();
}

void wfmain::on_audioTXCodecCombo_currentIndexChanged(int value)
{
    prefs.audioTXCodec = ui->audioTXCodecCombo->itemData(value).toInt();
}

void wfmain::on_audioBufferSizeSlider_valueChanged(int value)
{
    prefs.audioRXBufferSize = value;
    ui->bufferValue->setText(QString::number(value));
    emit sendChangeBufferSize(value);
}

void wfmain::on_toFixedBtn_clicked()
{
    emit setScopeFixedEdge(oldLowerFreq, oldUpperFreq, ui->scopeEdgeCombo->currentIndex()+1);
    emit setScopeEdge(ui->scopeEdgeCombo->currentIndex()+1);
    cmdOutQue.append(cmdScopeFixedMode);
    delayedCommand->start();
}

void wfmain::on_connectBtn_clicked()
{
    this->rigStatus->setText(""); // Clear status

    if (haveRigCaps) {
        emit sendCloseComm();
        ui->connectBtn->setText("Connect");
        haveRigCaps = false;
    }
    else
    {
        emit sendCloseComm(); // Just in case there is a failed connection open.
        openRig();
    }
}

void wfmain::on_sqlSlider_valueChanged(int value)
{
    emit setSql((unsigned char)value);
}

void wfmain::on_modeFilterCombo_activated(int index)
{
    //TODO:
    if(index >2)
    {
        //filterSetup->show();
    }

    // emit setFilterSel((unsigned char)index);
}

// --- DEBUG FUNCTION ---
void wfmain::on_debugBtn_clicked()
{
    qDebug() << "Debug button pressed.";

    // TODO: Why don't these commands work?!
    //emit getScopeMode();
    //emit getScopeEdge(); // 1,2,3 only in "fixed" mode
    //emit getScopeSpan(); // in khz, only in "center" mode
    //qDebug() << "Debug: finding rigs attached. Let's see if this works. ";
    //rig->findRigs();
    cal->show();
}




