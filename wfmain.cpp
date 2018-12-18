#include "wfmain.h"
#include "ui_wfmain.h"

#include "commhandler.h"
#include "rigidentities.h"

wfmain::wfmain(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::wfmain)
{
    ui->setupUi(this);
    theParent = parent;

    keyF11 = new QShortcut(this);
    keyF11->setKey(Qt::Key_F11);
    connect(keyF11, SIGNAL(activated()), this, SLOT(shortcutF11()));

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

    keyStar = new QShortcut(this);
    keyStar->setKey(Qt::Key_Asterisk);
    connect(keyStar, SIGNAL(activated()), this, SLOT(shortcutStar()));


    setDefaultColors(); // set of UI colors with defaults populated
    setDefPrefs(); // other default options
    loadSettings(); // Look for saved preferences

    // if setting for serial port is "auto" then...
    if(prefs.serialPortRadio == QString("auto"))
    {
        // Find the ICOM IC-7300.
        qDebug() << "Searching for serial port...";
        QDirIterator it("/dev/serial", QStringList() << "*IC-7300*", QDir::Files, QDirIterator::Subdirectories);

        while (it.hasNext())
            qDebug() << it.next();
        // if (it.isEmpty()) // fail or default to ttyUSB0 if present
        // iterator might not make sense
        serialPortRig = it.filePath(); // first? last?
        if(serialPortRig.isEmpty())
        {
            qDebug() << "Cannot find valid serial port. Trying /dev/ttyUSB0";
            serialPortRig = QString("/dev/ttyUSB0");
        }
        // end finding the 7300 code
    } else {
        serialPortRig = prefs.serialPortRadio;
    }


    plot = ui->plot; // rename it waterfall.
    wf = ui->waterfall;
    tracer = new QCPItemTracer(plot);
    //tracer->setGraphKey(5.5);
    tracer->setInterpolating(true);
    tracer->setStyle(QCPItemTracer::tsCrosshair);

    tracer->setPen(QPen(Qt::green));
    tracer->setBrush(Qt::green);
    tracer->setSize(30);

    spectWidth = 475; // fixed for now
    wfLength = 160; // fixed for now

    // Initialize before use!

    QByteArray empty((int)spectWidth, '\x01');
    spectrumPeaks = QByteArray( (int)spectWidth, '\x01' );
    for(quint16 i=0; i<wfLength; i++)
    {
        wfimage.append(empty);
    }

    //          0      1        2         3       4
    modes << "LSB" << "USB" << "AM" << "CW" << "RTTY";
    //          5      6          7           8          9
    modes << "FM" << "CW-R" << "RTTY-R" << "LSB-D" << "USB-D";
    // TODO: Add FM-D and AM-D which seem to exist
    ui->modeSelectCombo->insertItems(0, modes);

    spans << "2.5k" << "5.0k" << "10k" << "25k";
    spans << "50k" << "100k" << "250k" << "500k";
    ui->scopeBWCombo->insertItems(0, spans);

    edges << "1" << "2" << "3"; // yep
    ui->scopeEdgeCombo->insertItems(0,edges);

    ui->splitter->setHandleWidth(5);
    ui->statusBar->showMessage("Ready", 2000);

    rig = new rigCommander(prefs.radioCIVAddr, serialPortRig  );
    // rig = new rigCommander(0x94, serialPortRig  );

    rigThread = new QThread(this);

    rig->moveToThread(rigThread);
    connect(rigThread, SIGNAL(started()), rig, SLOT(process()));
    connect(rig, SIGNAL(finished()), rigThread, SLOT(quit()));
    rigThread->start();


    connect(rig, SIGNAL(haveFrequency(double)), this, SLOT(receiveFreq(double)));
    connect(this, SIGNAL(getFrequency()), rig, SLOT(getFrequency()));
    connect(this, SIGNAL(getMode()), rig, SLOT(getMode()));
    connect(this, SIGNAL(getDataMode()), rig, SLOT(getDataMode()));
    connect(this, SIGNAL(setDataMode(bool)), rig, SLOT(setDataMode(bool)));
    connect(this, SIGNAL(getBandStackReg(char,char)), rig, SLOT(getBandStackReg(char,char)));
    connect(rig, SIGNAL(havePTTStatus(bool)), this, SLOT(receivePTTstatus(bool)));
    connect(rig, SIGNAL(haveBandStackReg(float,char,bool)), this, SLOT(receiveBandStackReg(float,char,bool)));
    connect(this, SIGNAL(getDebug()), rig, SLOT(getDebug()));

    connect(this, SIGNAL(spectOutputDisable()), rig, SLOT(disableSpectOutput()));
    connect(this, SIGNAL(spectOutputEnable()), rig, SLOT(enableSpectOutput()));
    connect(this, SIGNAL(scopeDisplayDisable()), rig, SLOT(disableSpectrumDisplay()));
    connect(this, SIGNAL(scopeDisplayEnable()), rig, SLOT(enableSpectrumDisplay()));
    connect(rig, SIGNAL(haveMode(QString)), this, SLOT(receiveMode(QString)));
    connect(rig, SIGNAL(haveDataMode(bool)), this, SLOT(receiveDataModeStatus(bool)));
    connect(rig, SIGNAL(haveSpectrumData(QByteArray, double, double)), this, SLOT(receiveSpectrumData(QByteArray, double, double)));
    connect(this, SIGNAL(setFrequency(double)), rig, SLOT(setFrequency(double)));
    connect(this, SIGNAL(setScopeCenterMode(bool)), rig, SLOT(setSpectrumCenteredMode(bool)));
    connect(this, SIGNAL(setScopeEdge(char)), rig, SLOT(setScopeEdge(char)));
    connect(this, SIGNAL(setScopeSpan(char)), rig, SLOT(setScopeSpan(char)));
    connect(this, SIGNAL(getScopeMode()), rig, SLOT(getScopeMode()));
    connect(this, SIGNAL(getScopeEdge()), rig, SLOT(getScopeEdge()));
    connect(this, SIGNAL(getScopeSpan()), rig, SLOT(getScopeSpan()));

    connect(this, SIGNAL(setMode(char)), rig, SLOT(setMode(char)));
    connect(this, SIGNAL(getRfGain()), rig, SLOT(getRfGain()));
    connect(this, SIGNAL(getAfGain()), rig, SLOT(getAfGain()));
    connect(this, SIGNAL(setRfGain(unsigned char)), rig, SLOT(setRfGain(unsigned char)));
    connect(this, SIGNAL(setAfGain(unsigned char)), rig, SLOT(setAfGain(unsigned char)));
    connect(rig, SIGNAL(haveRfGain(unsigned char)), this, SLOT(receiveRfGain(unsigned char)));
    connect(rig, SIGNAL(haveAfGain(unsigned char)), this, SLOT(receiveAfGain(unsigned char)));
    connect(this, SIGNAL(getSql()), rig, SLOT(getSql()));
    connect(rig, SIGNAL(haveSql(unsigned char)), this, SLOT(receiveSql(unsigned char)));
    connect(this, SIGNAL(startATU()), rig, SLOT(startATU()));
    connect(this, SIGNAL(setATU(bool)), rig, SLOT(setATU(bool)));
    connect(this, SIGNAL(getRigID()), rig, SLOT(getRigID()));


    // Plot user interaction
    connect(plot, SIGNAL(mouseDoubleClick(QMouseEvent*)), this, SLOT(handlePlotDoubleClick(QMouseEvent*)));
    connect(wf, SIGNAL(mouseDoubleClick(QMouseEvent*)), this, SLOT(handleWFDoubleClick(QMouseEvent*)));
    connect(plot, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(handlePlotClick(QMouseEvent*)));
    connect(wf, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(handleWFClick(QMouseEvent*)));

    ui->plot->addGraph(); // primary
    ui->plot->addGraph(0, 0); // secondary, peaks, same axis as first?
    ui->waterfall->addGraph();
    tracer->setGraph(plot->graph(0));



    colorMap = new QCPColorMap(wf->xAxis, wf->yAxis);
    colorMapData = NULL;
    wf->addPlottable(colorMap);
    colorScale = new QCPColorScale(wf);
    colorMap->data()->setValueRange(QCPRange(0, wfLength-1));
    colorMap->data()->setKeyRange(QCPRange(0, spectWidth-1));
    colorMap->setDataRange(QCPRange(0, 160));
    colorMap->setGradient(QCPColorGradient::gpJet); // TODO: Add preference
    colorMapData = new QCPColorMapData(spectWidth, wfLength, QCPRange(0, spectWidth-1), QCPRange(0, wfLength-1));
    colorMap->setData(colorMapData);
    spectRowCurrent = 0;
    wf->yAxis->setRangeReversed(true);

    ui->tabWidget->setCurrentIndex(0);

    QColor color(20+200/4.0*1,70*(1.6-1/4.0), 150, 150);
    plot->graph(1)->setLineStyle(QCPGraph::lsLine);
    plot->graph(1)->setPen(QPen(color.lighter(200)));
    plot->graph(1)->setBrush(QBrush(color));

    drawPeaks = false;

    ui->freqMhzLineEdit->setValidator( new QDoubleValidator(0, 100, 6, this));

    delayedCommand = new QTimer(this);
    delayedCommand->setInterval(100); // ms. 250 was fine. TODO: Find practical maximum with margin on pi
    delayedCommand->setSingleShot(true);
    connect(delayedCommand, SIGNAL(timeout()), this, SLOT(runDelayedCommand()));

    foreach (const QSerialPortInfo &serialPortInfo, QSerialPortInfo::availablePorts())
    {
        portList.append(serialPortInfo.portName());
        ui->commPortDrop->addItem(serialPortInfo.portName());
    }

    // Initial state of UI:
    ui->fullScreenChk->setChecked(prefs.useFullScreen);
    on_fullScreenChk_clicked(prefs.useFullScreen);

    ui->useDarkThemeChk->setChecked(prefs.useDarkMode);
    on_useDarkThemeChk_clicked(prefs.useDarkMode);

    ui->drawPeakChk->setChecked(prefs.drawPeaks);
    on_drawPeakChk_clicked(prefs.drawPeaks);
    drawPeaks = prefs.drawPeaks;

    getInitialRigState();
    oldFreqDialVal = ui->freqDial->value();


}

wfmain::~wfmain()
{
    // rigThread->quit();
    delete ui;
}

void wfmain::setDefPrefs()
{
    defPrefs.useFullScreen = true;
    defPrefs.useDarkMode = true;
    defPrefs.drawPeaks = true;
    defPrefs.radioCIVAddr = 0x94;
    defPrefs.serialPortRadio = QString("auto");
    defPrefs.enablePTT = false;
    defPrefs.niceTS = true;

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
    settings.endGroup();

    // Radio and Comms: C-IV addr, port to use
    settings.beginGroup("Radio");
    prefs.radioCIVAddr = (unsigned char) settings.value("RigCIVuInt", defPrefs.radioCIVAddr).toInt();
    prefs.serialPortRadio = settings.value("SerialPortRadio", defPrefs.serialPortRadio).toString();
    settings.endGroup();

    // Misc. user settings (enable PTT, draw peaks, etc)
    settings.beginGroup("Controls");
    prefs.enablePTT = settings.value("EnablePTT", defPrefs.enablePTT).toBool();
    prefs.niceTS = settings.value("NiceTS", defPrefs.niceTS).toBool();
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
    settings.endGroup();

    // Radio and Comms: C-IV addr, port to use
    settings.beginGroup("Radio");
    settings.setValue("RigCIVuInt", prefs.radioCIVAddr);
    settings.setValue("SerialPortRadio", prefs.serialPortRadio);
    settings.endGroup();

    // Misc. user settings (enable PTT, draw peaks, etc)
    settings.beginGroup("Controls");
    settings.setValue("EnablePTT", prefs.enablePTT);
    settings.setValue("NiceTS", prefs.niceTS);
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

void wfmain::shortcutF5()
{

}

void wfmain::shortcutStar()
{
    // Jump to frequency tab from Asterisk key on keypad
    ui->tabWidget->setCurrentIndex(2);
    ui->freqMhzLineEdit->clear();
    ui->freqMhzLineEdit->setFocus();
}


void wfmain::getInitialRigState()
{
    // Initial list of queries to the radio.
    // These are made when the program starts up
    // and are used to adjust the UI to match the radio settings
    // the polling interval is set at 100ms. Faster is possible but slower
    // computers will glitch occassionally.

    cmdOutQue.append(cmdGetRigID); // This may be used in the future.

    cmdOutQue.append(cmdGetFreq);
    cmdOutQue.append(cmdGetMode);

    cmdOutQue.append(cmdNone);

    cmdOutQue.append(cmdGetFreq);
    cmdOutQue.append(cmdGetMode);

    cmdOutQue.append(cmdGetRxGain);
    cmdOutQue.append(cmdGetAfGain);
    cmdOutQue.append(cmdGetSql);
    // get TX level
    // get Scope reference Level

    cmdOutQue.append(cmdDispEnable);
    cmdOutQue.append(cmdSpecOn);

    // get spectrum mode (center or edge)
    // get spectrum span or edge limit number [1,2,3], update UI

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
        QFile f(":qdarkstyle/style.qss");
        if (!f.exists())
        {
            printf("Unable to set stylesheet, file not found\n");
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
    QColor color;
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
    // switch case on enum
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

    // Note: Commands that need a specific order should use this queue.
    // Commands that do not need a speific order should probably just
    // go through the signal-slot queue mechanism... which should be more clearly defined.

    // Prototype vector queue:

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

void wfmain::receiveFreq(double freqMhz)
{
    //qDebug() << "Frequency: " << freqMhz;
    ui->freqLabel->setText(QString("%1").arg(freqMhz, 0, 'f'));
    this->freqMhz = freqMhz;
    this->knobFreqMhz = freqMhz;
    showStatusBarText(QString("Frequency: %1").arg(freqMhz));
}

void wfmain::receivePTTstatus(bool pttOn)
{
    // NOTE: This will only show up if we actually receive a PTT status
    qDebug() << "PTT status: " << pttOn;
}

void wfmain::receiveSpectrumData(QByteArray spectrum, double startFreq, double endFreq)
{
    if((startFreq != oldLowerFreq) || (endFreq != oldUpperFreq))
    {
        if(drawPeaks)
            on_clearPeakBtn_clicked();
    }

    oldLowerFreq = startFreq;
    oldUpperFreq = endFreq;

    //qDebug() << "start: " << startFreq << " end: " << endFreq;
    quint16 specLen = spectrum.length();
    //qDebug() << "Spectrum data received at UI! Length: " << specLen;
    if(specLen != 475)
    {
        // qDebug () << "Unusual spectrum: length: " << specLen;
        if(specLen > 475)
        {
            specLen = 475;
        } else {
            // as-is
        }
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


void wfmain::on_startBtn_clicked()
{
    emit spectOutputEnable();
}

//void wfmain::on_getFreqBtn_clicked()
//{
//    emit getFrequency();
//}

//void wfmain::on_getModeBtn_clicked()
//{
//    emit getMode();
//}

//void wfmain::on_debugBtn_clicked()
//{
//    // Temporary place to try code
//    // emit getDebug();
//    // emit getBandStackReg(0x11,1); // 20M, latest
//    // emit getRfGain();

////    for(int a=0; a<100; a++)
////    {
////    cmdOutQue.append(cmdGetRxGain);
////    cmdOutQue.append(cmdGetSql);
////    }
////    delayedCommand->start();

//   // emit getRigID();

//    //mem.dumpMemory();

//    saveSettings();

//}

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
        ui->modeSelectCombo->setCurrentIndex(index);
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
        // TODO: be more intelligent here to avoid -D-D-D.
        // include the text above.
        // ui->modeLabel->setText( ui->modeLabel->text() + "-D" );
        // Remove if works.
    } else {
        // update to _not_ have the -D
        ui->modeSelectCombo->setCurrentIndex(currentModeIndex);
        // No need to update status label?

    }

}


void wfmain::on_clearPeakBtn_clicked()
{
    spectrumPeaks = QByteArray( (int)spectWidth, '\x01' );
}

void wfmain::on_drawPeakChk_clicked(bool checked)
{
    if(checked)
    {
        on_clearPeakBtn_clicked(); // clear
        drawPeaks = true;

    } else {
        drawPeaks = false;
        plot->graph(1)->clearData();

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

//void wfmain::on_modeSelectCombo_currentIndexChanged(int index)
//{
    // do nothing. The change may be from receiving a mode status update or the user. Can't tell which is which here.
//}



void wfmain::on_modeSelectCombo_activated(int index)
{
    // Reference:
    //          0      1        2         3       4
    //modes << "LSB" << "USB" << "AM" << "CW" << "RTTY";
    //          5      6          7           8          9
    //modes << "FM" << "CW-R" << "RTTY-R" << "LSB-D" << "USB-D";

    // the user initiated a mode change.
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
    QString copyright = QString("Copyright 2017, 2018 Elliott H. Liggett. All rights reserved.");
    QString ssCredit = QString("Stylesheet qdarkstyle used under MIT license, stored in application directory.");
    QString contact = QString("email the author: kilocharlie8@gmail.com or W6EL on the air!");
    QString buildInfo = QString("Build " + QString(GITSHORT) + " on " + QString(__DATE__) + " at " + __TIME__ + " by " + UNAME + "@" + HOST);

    QString aboutText = copyright + "\n" + ssCredit + "\n";
    aboutText.append(contact + "\n" + buildInfo);

    QMessageBox::about(this, "RigView", aboutText);

    // note: should set parent->Icon() and window titles
}

void wfmain::on_aboutQtBtn_clicked()
{
    QMessageBox::aboutQt(this, "Rig View");
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
    } else {
        qDebug() << "Could not store preset. Valid presets are 0 through 99.";
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
    emit setAfGain((unsigned char) value);
}

void wfmain::receiveRfGain(unsigned char level)
{
    // qDebug() << "Receive RF  level of" << (int)level << " = " << 100*level/255.0 << "%";
    ui->rfGainSlider->setValue(level);
}

void wfmain::receiveAfGain(unsigned char level)
{
    //qDebug() << "Receive AF  level of" << (int)level << " = " << 100*level/255.0 << "%";
    ui->afGainSlider->setValue(level);
}

void wfmain::receiveSql(unsigned char level)
{
    // qDebug() << "Receive SQL level of                   " << (int)level << " = " << 100*level/255.0 << "%";
    // ui->sqlSlider->setValue(level); // No SQL control so far
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
    showStatusBarText("Starting ATU cycle...");
    // TODO: place commands in a timer queue to check for completion and success

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

    // Are we already PTT?

    // send PTT
    // Start 3 minute timer
}

void wfmain::on_pttOffBtn_clicked()
{
    // Send the PTT OFF command (more than once?)

    // Stop the 3 min timer

}

void wfmain::on_saveSettingsBtn_clicked()
{
    saveSettings(); // save memory, UI, and radio settings
}

// --- DEBUG FUNCTION ---
void wfmain::on_debugBtn_clicked()
{
    // TODO: Remove function on release build
    // emit getScopeMode();
    // emit getScopeEdge();
    // emit getScopeSpan();
}
