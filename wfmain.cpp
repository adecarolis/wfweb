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
    tracer->setStyle(QCPItemTracer::tsPlus);

    tracer->setPen(QPen(Qt::green));
    tracer->setBrush(Qt::green);
    tracer->setSize(20);

    spectWidth = 475; // fixed for now
    wfLength = 160; // fixed for now

    // Initialize before use!

    QByteArray empty((int)spectWidth, '\x01');
    spectrumPeaks = QByteArray( (int)spectWidth, '\x01' );
    for(quint16 i=0; i<wfLength; i++)
    {
        wfimage.append(empty);
    }

    // TODO: FM is missing, should be where CW is, all other modes get +1?
    //          0      1        2         3       4
    modes << "LSB" << "USB" << "AM" << "CW" << "RTTY";
    //          5      6        7         8       9
    modes << "CW-R" << "RTTY-R" << "LSB-D" << "USB-D";
    ui->modeSelectCombo->insertItems(0, modes);

    spans << "2.5k" << "5.0k" << "10k" << "25k";
    spans << "50k" << "100k" << "250k" << "500k";
    ui->scopeBWCombo->insertItems(0, spans);

    edges << "1" << "2" << "3"; // yep
    ui->scopeEdgeCombo->insertItems(0,edges);

    ui->splitter->setHandleWidth(5);

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
    connect(this, SIGNAL(getDebug()), rig, SLOT(getDebug()));
    connect(this, SIGNAL(spectOutputDisable()), rig, SLOT(disableSpectOutput()));
    connect(this, SIGNAL(spectOutputEnable()), rig, SLOT(enableSpectOutput()));
    connect(this, SIGNAL(scopeDisplayDisable()), rig, SLOT(disableSpectrumDisplay()));
    connect(this, SIGNAL(scopeDisplayEnable()), rig, SLOT(enableSpectrumDisplay()));
    connect(rig, SIGNAL(haveMode(QString)), this, SLOT(receiveMode(QString)));
    connect(rig, SIGNAL(haveSpectrumData(QByteArray, double, double)), this, SLOT(receiveSpectrumData(QByteArray, double, double)));
    connect(this, SIGNAL(setFrequency(double)), rig, SLOT(setFrequency(double)));
    connect(this, SIGNAL(setScopeCenterMode(bool)), rig, SLOT(setSpectrumCenteredMode(bool)));
    connect(this, SIGNAL(setScopeEdge(char)), rig, SLOT(setScopeEdge(char)));
    connect(this, SIGNAL(setScopeSpan(char)), rig, SLOT(setScopeSpan(char)));
    connect(this, SIGNAL(setMode(char)), rig, SLOT(setMode(char)));


    // Plot user interaction
    connect(plot, SIGNAL(mouseDoubleClick(QMouseEvent*)), this, SLOT(handlePlotDoubleClick(QMouseEvent*)));
    connect(wf, SIGNAL(mouseDoubleClick(QMouseEvent*)), this, SLOT(handleWFDoubleClick(QMouseEvent*)));


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
    delayedCommand->setInterval(250);
    delayedCommand->setSingleShot(true);
    connect(delayedCommand, SIGNAL(timeout()), this, SLOT(runDelayedCommand()));

    getInitialRigState();

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

    cmdOutQue.append(cmdGetFreq);
    cmdOutQue.append(cmdGetMode);

    cmdOut = cmdNone;
    delayedCommand->start();
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
        plot->graph(0)->setPen(QPen(Qt::lightGray)); // magenta, yellow, green, lightGray
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
        tracer->setGraphKey(freqMhz);
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
    double y;
    //double px;
    x = plot->xAxis->pixelToCoord(me->pos().x());
    y = plot->yAxis->pixelToCoord(me->pos().y());
    emit setFrequency(x);
    cmdOut = cmdGetFreq;
    delayedCommand->start();

    qDebug() << "PLOT double click: " << x << ", " << y;
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

    //qDebug() << "WF double click: " << x << ", " << y;

}

void wfmain::handlePlotClick(QMouseEvent *me)
{

}

void wfmain::handleWFClick(QMouseEvent *me)
{

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
    emit getDebug();
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
    cmdOut = cmdGetDataMode;
    //delayedCommand->start();
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

void wfmain::on_modeSelectCombo_currentIndexChanged(int index)
{
    if(index < 10)
    {
        qDebug() << "Mode selection changed. index: " << index;
        emit setMode(index);

        if(index > 7)
        {
            // set data mode on
        } else {
            // set data mode off
        }
    }
}


