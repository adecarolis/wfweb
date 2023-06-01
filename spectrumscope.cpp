#include "spectrumscope.h"

#include "logcategories.h"

spectrumScope::spectrumScope(QWidget *parent)
    : QWidget{parent}
{
    QMutexLocker locker(&mutex);

    spectrum = new QCustomPlot();
    layout = new QVBoxLayout(this);
    splitter = new QSplitter(this);
    layout->addWidget(splitter);
    splitter->setOrientation(Qt::Vertical);


    spectrum = new QCustomPlot();
    waterfall = new QCustomPlot();

    splitter->addWidget(spectrum);
    splitter->addWidget(waterfall);
    splitter->setHandleWidth(5);

    spectrum->axisRect()->setMargins(QMargins(0,0,0,0));
    waterfall->axisRect()->setMargins(QMargins(0,0,0,0));

    // Spectrum Plot setup
    passbandIndicator = new QCPItemRect(spectrum);
    passbandIndicator->setAntialiased(true);
    passbandIndicator->setPen(QPen(Qt::red));
    passbandIndicator->setBrush(QBrush(Qt::red));
    passbandIndicator->setSelectable(true);

    pbtIndicator = new QCPItemRect(spectrum);
    pbtIndicator->setAntialiased(true);
    pbtIndicator->setPen(QPen(Qt::red));
    pbtIndicator->setBrush(QBrush(Qt::red));
    pbtIndicator->setSelectable(true);
    pbtIndicator->setVisible(false);

    freqIndicatorLine = new QCPItemLine(spectrum);
    freqIndicatorLine->setAntialiased(true);
    freqIndicatorLine->setPen(QPen(Qt::blue));

    oorIndicator = new QCPItemText(spectrum);
    oorIndicator->setVisible(false);
    oorIndicator->setAntialiased(true);
    oorIndicator->setPen(QPen(Qt::red));
    oorIndicator->setBrush(QBrush(Qt::red));
    oorIndicator->setFont(QFont(font().family(), 14));
    oorIndicator->setPositionAlignment(Qt::AlignVCenter | Qt::AlignHCenter);
    oorIndicator->position->setType(QCPItemPosition::ptAxisRectRatio); // Positioned relative to the current plot rect
    oorIndicator->setText("SCOPE OUT OF RANGE");
    oorIndicator->position->setCoords(0.5f,0.5f);

    ovfIndicator = new QCPItemText(spectrum);
    ovfIndicator->setVisible(false);
    ovfIndicator->setAntialiased(true);
    ovfIndicator->setPen(QPen(Qt::red));
    ovfIndicator->setColor(Qt::red);
    ovfIndicator->setFont(QFont(font().family(), 10));
    ovfIndicator->setPositionAlignment(Qt::AlignLeft | Qt::AlignTop);
    ovfIndicator->position->setType(QCPItemPosition::ptAxisRectRatio); // Positioned relative to the current plot rect
    ovfIndicator->setText(" OVF ");
    ovfIndicator->position->setCoords(0.01f,0.0f);

    spectrum->addGraph(); // primary
    spectrum->addGraph(0, 0); // secondary, peaks, same axis as first.
    spectrum->addLayer( "Top Layer", spectrum->layer("main"));
    spectrum->graph(0)->setLayer("Top Layer");

    QColor color(20+200/4.0*1,70*(1.6-1/4.0), 150, 150);
    spectrum->graph(1)->setLineStyle(QCPGraph::lsLine);
    spectrum->graph(1)->setPen(QPen(color.lighter(200)));
    spectrum->graph(1)->setBrush(QBrush(color));

    freqIndicatorLine->start->setCoords(0.5, 0);
    freqIndicatorLine->end->setCoords(0.5, 160);

    passbandIndicator->topLeft->setCoords(0.5, 0);
    passbandIndicator->bottomRight->setCoords(0.5, 160);

    pbtIndicator->topLeft->setCoords(0.5, 0);
    pbtIndicator->bottomRight->setCoords(0.5, 160);

    // Waterfall setup
    waterfall->addGraph();
    colorMap = new QCPColorMap(waterfall->xAxis, waterfall->yAxis);
    colorMapData = NULL;
#if QCUSTOMPLOT_VERSION < 0x020001
    this->addPlottable(colorMap);
#endif

}


bool spectrumScope::prepareWf(uint wf)
{
    QMutexLocker locker(&mutex);
    bool ret=true;

    this->wfLength = wf;
    this->wfLengthMax = 1024;

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
    }


    wfimage.squeeze();
    //colorMap->clearData();
    colorMap->data()->clear();

    colorMap->data()->setValueRange(QCPRange(0, wfLength-1));
    colorMap->data()->setKeyRange(QCPRange(0, spectWidth-1));
    colorMap->setDataRange(QCPRange(plotFloor, plotCeiling));
    colorMap->setGradient(static_cast<QCPColorGradient::GradientPreset>(currentTheme));

    if(colorMapData != Q_NULLPTR)
    {
        delete colorMapData;
    }
    colorMapData = new QCPColorMapData(spectWidth, wfLength, QCPRange(0, spectWidth-1), QCPRange(0, wfLength-1));

    colorMap->setData(colorMapData);

    waterfall->yAxis->setRangeReversed(true);
    waterfall->xAxis->setVisible(false);

    scopePrepared = true;
    return ret;
}

void spectrumScope::setRange(int floor, int ceiling)
{
    maxAmp = ceiling;
    if (spectrum != Q_NULLPTR)
        spectrum->yAxis->setRange(QCPRange(floor, ceiling));
    if (colorMap != Q_NULLPTR)
        colorMap->setDataRange(QCPRange(floor,ceiling));
}

void spectrumScope::colorPreset(colorPrefsType *cp)
{
    if (cp == Q_NULLPTR)
    {
        return;
    }

    colors = *cp;

    spectrum->setBackground(cp->plotBackground);

    spectrum->xAxis->grid()->setPen(cp->gridColor);
    spectrum->yAxis->grid()->setPen(cp->gridColor);

    spectrum->legend->setTextColor(cp->textColor);
    spectrum->legend->setBorderPen(cp->gridColor);
    spectrum->legend->setBrush(cp->gridColor);

    spectrum->xAxis->setTickLabelColor(cp->textColor);
    spectrum->xAxis->setLabelColor(cp->gridColor);
    spectrum->yAxis->setTickLabelColor(cp->textColor);
    spectrum->yAxis->setLabelColor(cp->gridColor);

    spectrum->xAxis->setBasePen(cp->axisColor);
    spectrum->xAxis->setTickPen(cp->axisColor);
    spectrum->yAxis->setBasePen(cp->axisColor);
    spectrum->yAxis->setTickPen(cp->axisColor);

    freqIndicatorLine->setPen(QPen(cp->tuningLine));

    passbandIndicator->setPen(QPen(cp->passband));
    passbandIndicator->setBrush(QBrush(cp->passband));

    pbtIndicator->setPen(QPen(cp->pbt));
    pbtIndicator->setBrush(QBrush(cp->pbt));

    spectrum->graph(0)->setPen(QPen(cp->spectrumLine));
    spectrum->graph(0)->setBrush(QBrush(cp->spectrumFill));

    spectrum->graph(1)->setPen(QPen(cp->underlayLine));
    spectrum->graph(1)->setBrush(QBrush(cp->underlayFill));

    waterfall->yAxis->setBasePen(cp->wfAxis);
    waterfall->yAxis->setTickPen(cp->wfAxis);
    waterfall->xAxis->setBasePen(cp->wfAxis);
    waterfall->xAxis->setTickPen(cp->wfAxis);

    waterfall->xAxis->setLabelColor(cp->wfGrid);
    waterfall->yAxis->setLabelColor(cp->wfGrid);

    waterfall->xAxis->setTickLabelColor(cp->wfText);
    waterfall->yAxis->setTickLabelColor(cp->wfText);

    waterfall->setBackground(cp->wfBackground);

}

bool spectrumScope::update(scopeData data)
{
    if (!scopePrepared )
    {
        return false;
    }
    bool updateRange = false;

    if (data.startFreq != lowerFreq || data.endFreq != upperFreq)
    {
        if(underlayMode == underlayPeakHold)
        {
            // TODO: create non-button function to do this
            // This will break if the button is ever moved or renamed.
            clearPeaks();
        } else {
            plasmaPrepared = false;
            preparePlasma();
        }
        // Inform other threads (cluster) that the frequency range has changed.
        //emit frequencyRange(data.startFreq, data.endFreq);
    }

    lowerFreq = data.startFreq;
    upperFreq = data.endFreq;

    //qInfo(logSystem()) << "start: " << data.startFreq << " end: " << data.endFreq;
    quint16 specLen = data.data.length();

    QVector <double> x(spectWidth), y(spectWidth), y2(spectWidth);

    for(int i=0; i < spectWidth; i++)
    {
        x[i] = (i * (data.endFreq-data.startFreq)/spectWidth) + data.startFreq;
    }

    for(int i=0; i< specLen; i++)
    {
        y[i] = (unsigned char)data.data.at(i);
        if(underlayMode == underlayPeakHold)
        {
            if((unsigned char)data.data.at(i) > (unsigned char)spectrumPeaks.at(i))
            {
                spectrumPeaks[i] = data.data.at(i);
            }
            y2[i] = (unsigned char)spectrumPeaks.at(i);
        }
    }
    plasmaMutex.lock();
    spectrumPlasma.push_front(data.data);
    if(spectrumPlasma.size() > (int)spectrumPlasmaSize)
    {
        spectrumPlasma.pop_back();
    }
    plasmaMutex.unlock();


    QMutexLocker locker(&mutex);
    if ((plotFloor != oldPlotFloor) || (plotCeiling != oldPlotCeiling)){
        updateRange = true;
    }
#if QCUSTOMPLOT_VERSION < 0x020000
    spectrum->graph(0)->setData(x, y);
#else
    spectrum->graph(0)->setData(x, y, true);
#endif

    if((freq.MHzDouble < data.endFreq) && (freq.MHzDouble > data.startFreq))
    {
        freqIndicatorLine->start->setCoords(freq.MHzDouble, 0);
        freqIndicatorLine->end->setCoords(freq.MHzDouble, maxAmp);

        double pbStart = 0.0;
        double pbEnd = 0.0;

        switch (rigMode.mk)
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
        passbandIndicator->bottomRight->setCoords(pbEnd, maxAmp);

        if ((rigMode.mk == modeCW || rigMode.mk == modeCW_R) && passbandWidth > 0.0006)
        {
            pbtDefault = round((passbandWidth - (cwPitch / 1000000.0)) * 200000.0) / 200000.0;
        }
        else
        {
            pbtDefault = 0.0;
        }

        if ((PBTInner - pbtDefault || PBTOuter - pbtDefault) && passbandAction != passbandResizing && rigMode.mk != modeFM)
        {
            pbtIndicator->setVisible(true);
        }
        else
        {
            pbtIndicator->setVisible(false);
        }

        /*
            pbtIndicator displays the intersection between PBTInner and PBTOuter
        */
        if (rigMode.mk == modeLSB || rigMode.mk == modeCW || rigMode.mk == modeRTTY) {
            pbtIndicator->topLeft->setCoords(qMax(pbStart - (PBTInner / 2) + (pbtDefault / 2), pbStart - (PBTOuter / 2) + (pbtDefault / 2)), 0);

            pbtIndicator->bottomRight->setCoords(qMin(pbStart - (PBTInner / 2) + (pbtDefault / 2) + passbandWidth,
                                                      pbStart - (PBTOuter / 2) + (pbtDefault / 2) + passbandWidth), maxAmp);
        }
        else
        {
            pbtIndicator->topLeft->setCoords(qMax(pbStart + (PBTInner / 2) - (pbtDefault / 2), pbStart + (PBTOuter / 2) - (pbtDefault / 2)), 0);

            pbtIndicator->bottomRight->setCoords(qMin(pbStart + (PBTInner / 2) - (pbtDefault / 2) + passbandWidth,
                                                      pbStart + (PBTOuter / 2) - (pbtDefault / 2) + passbandWidth), maxAmp);
        }

        //qDebug() << "Default" << pbtDefault << "Inner" << PBTInner << "Outer" << PBTOuter << "Pass" << passbandWidth << "Center" << passbandCenterFrequency << "CW" << cwPitch;
    }

#if QCUSTOMPLOT_VERSION < 0x020000
    if (underlayMode == underlayPeakHold) {
        spectrum->graph(1)->setData(x, y2); // peaks
    }
    else if (underlayMode != underlayNone) {
        computePlasma();
        spectrum->graph(1)->setData(x, spectrumPlasmaLine);
    }
    else {
        spectrum->graph(1)->setData(x, y2); // peaks, but probably cleared out
    }

#else
    if (underlayMode == underlayPeakHold) {
        spectrum->graph(1)->setData(x, y2, true); // peaks
    }
    else if (underlayMode != underlayNone) {
        computePlasma();
        spectrum->graph(1)->setData(x, spectrumPlasmaLine, true);
    }
    else {
        spectrum->graph(1)->setData(x, y2, true); // peaks, but probably cleared out
    }
#endif

    if(updateRange)
        spectrum->yAxis->setRange(plotFloor, plotCeiling);

    spectrum->xAxis->setRange(data.startFreq, data.endFreq);
    spectrum->replot();

    if(specLen == spectWidth)
    {
        wfimage.prepend(data.data);
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

        waterfall->yAxis->setRange(0,wfLength - 1);
        waterfall->xAxis->setRange(0, spectWidth-1);
        waterfall->replot();

/*
#if defined (USB_CONTROLLER)
        // Send to USB Controllers if requested
        auto i = usbDevices.begin();
        while (i != usbDevices.end())
        {
            if (i.value().connected && i.value().type.model == usbDeviceType::StreamDeckPlus && i.value().lcd == funcLCDWaterfall )
            {
                lcdImage = waterfall->toPixmap().toImage();
                emit sendControllerRequest(&i.value(), usbFeatureType::featureLCD, 0, "", &lcdImage);
            }
            else if (i.value().connected && i.value().type.model == usbDeviceType::StreamDeckPlus && i.value().lcd == funcLCDSpectrum)
            {
                lcdImage = spectrum->toPixmap().toImage();
                emit sendControllerRequest(&i.value(), usbFeatureType::featureLCD, 0, "", &lcdImage);
            }
            ++i;
        }
#endif
  */
    }

    oldPlotFloor = plotFloor;
    oldPlotCeiling = plotCeiling;

    if (data.oor && !oorIndicator->visible()) {
        oorIndicator->setVisible(true);
        //oorIndicator->position->setCoords((oldLowerFreq+oldUpperFreq)/2,ui->topLevelSlider->value() - 20);
        qInfo(logSystem()) << "Scope out of range";
    } else if (!data.oor && oorIndicator->visible()) {
        oorIndicator->setVisible(false);
    }

    //ovfIndicator->setVisible(true);

    return true;
}




// Plasma functions
void spectrumScope::preparePlasma()
{
    QMutexLocker locker(&plasmaMutex);

    if(plasmaPrepared)
        return;

    if(spectrumPlasmaSize == 0)
        spectrumPlasmaSize = 128;

    spectrumPlasma.clear();
    spectrumPlasma.squeeze();
    plasmaPrepared = true;
}

void spectrumScope::resizePlasmaBuffer(int size) {
    QMutexLocker locker(&plasmaMutex);
    QByteArray empty((int)spectWidth, '\x01');

    int oldSize = spectrumPlasma.size();

    if(oldSize < size)
    {
        spectrumPlasma.resize(size);
        for(int p=oldSize; p < size; p++)
        {
            spectrumPlasma.append(empty);
        }
    } else if (oldSize > size){
        for(int p = oldSize; p > size; p--)
        {
            spectrumPlasma.pop_back();
        }
    }

    spectrumPlasma.squeeze();
}

void spectrumScope::clearPeaks()
{
    spectrumPeaks = QByteArray( (int)spectWidth, '\x01' );
    clearPlasma();
}

void spectrumScope::clearPlasma()
{
    QMutexLocker locker(&plasmaMutex);
    QByteArray empty((int)spectWidth, '\x01');
    int pSize = spectrumPlasma.size();
    for(int i=0; i < pSize; i++)
    {
        spectrumPlasma[i] = empty;
    }
}

void spectrumScope::computePlasma()
{
    QMutexLocker locker(&plasmaMutex);
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
}
