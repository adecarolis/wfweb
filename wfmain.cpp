#include "wfmain.h"
#include "ui_wfmain.h"

#include "commhandler.h"

wfmain::wfmain(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::wfmain)
{
    ui->setupUi(this);
    theParent = parent;
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
    ui->modeSelectCombo->insertItems(0, modes);

    spans << "2.5k" << "5.0k" << "10k" << "25k";
    spans << "50k" << "100k" << "250k" << "500k";
    ui->scopeBWCombo->insertItems(0, spans);

    edges << "1" << "2" << "3"; // yep
    ui->scopeEdgeCombo->insertItems(0,edges);

    ui->splitter->setHandleWidth(5);
    ui->statusBar->showMessage("Ready", 2000);

    // comm = new commHandler();
    rig = new rigCommander();
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
    connect(this, SIGNAL(setMode(char)), rig, SLOT(setMode(char)));
    connect(this, SIGNAL(getRfGain()), rig, SLOT(getRfGain()));
    connect(this, SIGNAL(getAfGain()), rig, SLOT(getAfGain()));
    connect(this, SIGNAL(setRfGain(unsigned char)), rig, SLOT(setRfGain(unsigned char)));
    connect(this, SIGNAL(setAfGain(unsigned char)), rig, SLOT(setAfGain(unsigned char)));
    connect(rig, SIGNAL(haveRfGain(unsigned char)), this, SLOT(receiveRfGain(unsigned char)));
    connect(rig, SIGNAL(haveAfGain(unsigned char)), this, SLOT(receiveAfGain(unsigned char)));
    connect(this, SIGNAL(startATU()), rig, SLOT(startATU()));

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
    colorMap->setGradient(QCPColorGradient::gpJet);
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
    ui->drawPeakChk->setChecked(false);

    ui->freqMhzLineEdit->setValidator( new QDoubleValidator(0, 100, 6, this));

    delayedCommand = new QTimer(this);
    delayedCommand->setInterval(100); // ms. 250 was fine.
    delayedCommand->setSingleShot(true);
    connect(delayedCommand, SIGNAL(timeout()), this, SLOT(runDelayedCommand()));

    foreach (const QSerialPortInfo &serialPortInfo, QSerialPortInfo::availablePorts())
    {
        portList.append(serialPortInfo.portName());
        ui->commPortDrop->addItem(serialPortInfo.portName());
    }

    getInitialRigState();
    oldFreqDialVal = ui->freqDial->value();

    //tracer->visible();
}

wfmain::~wfmain()
{
    // rigThread->quit();
    delete ui;
}

void wfmain::getInitialRigState()
{
    // Things to get:
    // Freq, Mode, Scope cent/fixed, scope span, edge setting
    // data mode (may be combined with scope mode)

    cmdOutQue.append(cmdGetFreq);
    cmdOutQue.append(cmdGetMode);

    cmdOutQue.append(cmdDispEnable);
    cmdOutQue.append(cmdSpecOn);

    cmdOutQue.append(cmdGetFreq);
    cmdOutQue.append(cmdGetMode);

    cmdOutQue.append(cmdGetRxGain);
    cmdOutQue.append(cmdGetAfGain);

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
            case cmdGetFreq:
                emit getFrequency();
                break;
            case cmdGetMode:
                emit getMode();
                break;
            case cmdGetDataMode:
                qDebug() << "Sending query for data mode";
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
            default:
                break;
        }
    }
    if(cmdOutQue.isEmpty())
    {
        // done
    } else {
        // next
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
        qDebug () << "Unusual spectrum: length: " << specLen;
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
    //emit scopeDisplayEnable(); // TODO: need a little delay between these two
    emit spectOutputEnable();
}

void wfmain::on_getFreqBtn_clicked()
{
    emit getFrequency();
}

void wfmain::on_getModeBtn_clicked()
{
    emit getMode();
}

void wfmain::on_debugBtn_clicked()
{
    // Temporary place to try code
    // emit getDebug();
    // emit getBandStackReg(0x11,1); // 20M, latest
    emit getRfGain();
}

void wfmain::on_stopBtn_clicked()
{
    emit spectOutputDisable();
    //emit scopeDisplayDisable();
}

void wfmain::receiveMode(QString mode)
{
    ui->modeLabel->setText(mode);
    int index;
    //bool ok;
    index = modes.indexOf(QRegExp(mode));
    if( currentModeIndex == index)
    {
        // do nothing, no need to change the selected mode and fire more events off.
        return;
    }
    if((index >= 0) && (index < 9))
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
    qDebug() << "Received data mode " << dataEnabled << "\n";
    if(dataEnabled)
    {
        if(currentModeIndex == 0)
        {
            // USB
            ui->modeSelectCombo->setCurrentIndex(8);
        } else if (currentModeIndex == 1)
        {
            // LSB
            ui->modeSelectCombo->setCurrentIndex(9);
        }
        ui->modeLabel->setText( ui->modeLabel->text() + "-D" );
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
}


void wfmain::on_fullScreenChk_clicked(bool checked)
{
    if(checked)
        this->showFullScreen();
    else
        this->showNormal();


}

void wfmain::on_goFreqBtn_clicked()
{
    bool ok = false;
    double freq = ui->freqMhzLineEdit->text().toDouble(&ok);
    if(ok)
    {
        emit setFrequency(freq);
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
    // press STO
    // type memory location 0 through 99
    // press Enter
}

void wfmain::on_fRclBtn_clicked()
{
    // Sequence:
    // type memory location 0 through 99
    // press RCL

    // Program recalls data stored in vector at position specified
    // drop contents into text box, press go button
    // add delayed command for mode and data mode

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
    qDebug() << "Setting RF Gain value to " << (int)level;
    ui->rfGainSlider->setValue(level);
}

void wfmain::receiveAfGain(unsigned char level)
{
    qDebug() << "Setting AF Gain value to " << (int)level;
    ui->afGainSlider->setValue(level);
}

void wfmain::on_drawTracerChk_toggled(bool checked)
{
    tracer->setVisible(checked);
}

void wfmain::on_tuneNowBtn_clicked()
{
    emit startATU();
    showStatusBarText("Starting ATU cycle...");

}
