#include "spectrumscope.h"
#include "logcategories.h"
#include "rigidentities.h"

spectrumScope::spectrumScope(bool scope, uchar receiver, uchar vfo, QWidget *parent)
    : QGroupBox{parent}, receiver(receiver), numVFO(vfo),hasScope(scope)
{

    QMutexLocker locker(&mutex);

    this->setObjectName("Spectrum Scope");
    this->setTitle("Band");
    this->defaultStyleSheet = this->styleSheet();
    queue = cachingQueue::getInstance();
    //spectrum = new QCustomPlot();
    mainLayout = new QHBoxLayout(this);
    layout = new QVBoxLayout();
    mainLayout->addLayout(layout);
    splitter = new QSplitter(this);
    layout->addWidget(splitter);
    splitter->setOrientation(Qt::Vertical);
    originalParent = parent;

    displayLayout = new QHBoxLayout();

    for (uchar i=0;i<numVFO;i++)
    {
        qInfo() << "****Adding VFO" << i << "on receiver" << receiver;
        freqDisplay[i] = new freqCtrl();
        if (i==0)
        {
            freqDisplay[i]->setMinimumSize(280,30);
            freqDisplay[i]->setMaximumSize(280,30);
            displayLayout->addWidget(freqDisplay[i]);
            displaySpacer = new QSpacerItem(0,0,QSizePolicy::Expanding,QSizePolicy::Fixed);
            displayLayout->addSpacerItem(displaySpacer);
        } else {
            freqDisplay[i]->setMinimumSize(180,20);
            freqDisplay[i]->setMaximumSize(180,20);
            displayLayout->addWidget(freqDisplay[i]);
        }
        connect(this->freqDisplay[i], &freqCtrl::newFrequency, this,
                [=](const qint64 &freq) { this->newFrequency(freq,i);});


        //connect(this->rig, &rigCommander::haveDashRatio,
        //        [=](const unsigned char& ratio) { cw->handleDashRatio(ratio); });

        //connect(freqDisplay[i], SIGNAL(newFrequency(qint64)), this, SLOT(newFrequency(qint64,i)));

    }


    controlLayout = new QHBoxLayout();
    detachButton = new QPushButton(tr("Detach"));
    detachButton->setCheckable(true);
    detachButton->setToolTip(tr("Detach/re-attach scope from main window"));
    detachButton->setChecked(false);
    //scopeModeLabel = new QLabel("Spectrum Mode:");
    scopeModeCombo = new QComboBox();
    scopeModeCombo->setAccessibleDescription(tr("Spectrum Mode"));
    //spanLabel = new QLabel("Span:");
    spanCombo = new QComboBox();
    spanCombo->setAccessibleDescription(tr("Spectrum Span"));
    spanCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    //edgeLabel = new QLabel("Edge:");
    edgeCombo = new QComboBox();
    edgeCombo->setAccessibleDescription(tr("Spectrum Edge"));
    edgeButton = new QPushButton(tr("Custom Edge"));
    edgeButton->setToolTip(tr("Define a custom (fixed) scope edge"));
    toFixedButton = new QPushButton(tr("To Fixed"));
    toFixedButton->setToolTip(tr("&lt;html&gt;&lt;head/&gt;&lt;body&gt;&lt;p&gt;Press button to convert center mode spectrum to fixed mode, preserving the range. This allows you to tune without the spectrum moving, in the same currently-visible range that you see now. &lt;/p&gt;&lt;p&gt;&lt;br/&gt;&lt;/p&gt;&lt;p&gt;The currently-selected edge slot will be overridden.&lt;/p&gt;&lt;/body&gt;&lt;/html&gt;"));

    holdButton = new QPushButton("HOLD");
    holdButton->setCheckable(true);
    holdButton->setFocusPolicy(Qt::NoFocus);

    controlSpacer = new QSpacerItem(0,0,QSizePolicy::Expanding,QSizePolicy::Fixed);
    midSpacer = new QSpacerItem(0,0,QSizePolicy::Expanding,QSizePolicy::Fixed);

    clearPeaksButton = new QPushButton("Clear Peaks");

    confButton = new QPushButton("<");
    confButton->setAccessibleName(tr("Configure Scope"));
    confButton->setAccessibleDescription(tr("Change various settings of the current Scope"));
    confButton->setToolTip(tr("Configure Scope"));
    confButton->setFocusPolicy(Qt::NoFocus);

    modeCombo = new QComboBox();
    dataCombo = new QComboBox();
    filterCombo = new QComboBox();

    spanCombo->setVisible(false);
    edgeCombo->setVisible(false);
    edgeButton->setVisible(false);
    toFixedButton->setVisible(false);

    spectrum = new QCustomPlot();
    spectrum->xAxis->axisRect()->setAutoMargins(QCP::MarginSide::msBottom);
    spectrum->yAxis->axisRect()->setAutoMargins(QCP::MarginSide::msBottom);
    spectrum->xAxis->setPadding(0);
    spectrum->yAxis->setPadding(0);

    waterfall = new QCustomPlot();
    waterfall->xAxis->axisRect()->setAutoMargins(QCP::MarginSide::msNone);
    waterfall->yAxis->axisRect()->setAutoMargins(QCP::MarginSide::msNone);
    waterfall->xAxis->setPadding(0);
    waterfall->yAxis->setPadding(0);

    splitter->addWidget(spectrum);
    splitter->addWidget(waterfall);
    splitter->setHandleWidth(5);

    spectrum->axisRect()->setMargins(QMargins(30,0,0,0));
    waterfall->axisRect()->setMargins(QMargins(30,0,0,0));

    layout->addLayout(displayLayout);
    layout->addLayout(controlLayout);
    controlLayout->addWidget(detachButton);
    controlLayout->addWidget(scopeModeCombo);
    controlLayout->addWidget(spanCombo);
    controlLayout->addWidget(edgeCombo);
    controlLayout->addWidget(edgeButton);
    controlLayout->addWidget(toFixedButton);
    controlLayout->addWidget(holdButton);
    controlLayout->addSpacerItem(controlSpacer);
    controlLayout->addWidget(modeCombo);
    controlLayout->addWidget(dataCombo);
    controlLayout->addWidget(filterCombo);
    controlLayout->addSpacerItem(midSpacer);
    controlLayout->addWidget(clearPeaksButton);
    controlLayout->addWidget(confButton);

    this->layout->setContentsMargins(5,5,5,5);

    scopeModeCombo->addItem(tr("Center Mode"), (spectrumMode_t)spectModeCenter);
    scopeModeCombo->addItem(tr("Fixed Mode"), (spectrumMode_t)spectModeFixed);
    scopeModeCombo->addItem(tr("Scroll-C"), (spectrumMode_t)spectModeScrollC);
    scopeModeCombo->addItem(tr("Scroll-F"), (spectrumMode_t)spectModeScrollF);
    scopeModeCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);

    edgeCombo->insertItems(0, QStringList({tr("Fixed Edge 1"),tr("Fixed Edge 2"),tr("Fixed Edge 3"),tr("Fixed Edge 4")}));
    //edgeCombo->setSizeAdjustPolicy(QComboBox::AdjustToContentsOnFirstShow);

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
    oorIndicator->setText(tr("SCOPE OUT OF RANGE"));
    oorIndicator->position->setCoords(0.5f,0.5f);

    ovfIndicator = new QCPItemText(spectrum);
    ovfIndicator->setVisible(false);
    ovfIndicator->setAntialiased(true);
    ovfIndicator->setPen(QPen(Qt::red));
    ovfIndicator->setColor(Qt::red);
    ovfIndicator->setFont(QFont(font().family(), 10));
    ovfIndicator->setPositionAlignment(Qt::AlignLeft | Qt::AlignTop);
    ovfIndicator->position->setType(QCPItemPosition::ptAxisRectRatio); // Positioned relative to the current plot rect
    ovfIndicator->setText(tr(" OVF "));
    ovfIndicator->position->setCoords(0.01f,0.0f);

    redrawSpeed = new QCPItemText(spectrum);
    redrawSpeed->setVisible(true);
    redrawSpeed->setColor(Qt::gray);
    redrawSpeed->setFont(QFont(font().family(), 8));
    redrawSpeed->setPositionAlignment(Qt::AlignRight | Qt::AlignTop);
    redrawSpeed->position->setType(QCPItemPosition::ptAxisRectRatio);
    redrawSpeed->setText("0ms/0ms");
    redrawSpeed->position->setCoords(1.0f,0.0f);

    spectrum->addGraph(); // primary
    spectrum->addGraph(0, 0); // secondary, peaks, same axis as first.
    spectrum->addLayer( "Top Layer", spectrum->layer("main"));
    spectrum->graph(0)->setLayer("Top Layer");

    spectrumPlasma.resize(spectrumPlasmaSizeMax);
    for(unsigned int p=0; p < spectrumPlasmaSizeMax; p++) {
        spectrumPlasma[p] = 0;
    }

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

    // Config Screen
    rhsLayout = new QVBoxLayout();
    rhsTopSpacer = new QSpacerItem(0,0,QSizePolicy::Fixed,QSizePolicy::Expanding);
    rhsLayout->addSpacerItem(rhsTopSpacer);
    configGroup = new QGroupBox();
    rhsLayout->addWidget(configGroup);
    configLayout = new QFormLayout();
    configGroup->setLayout(configLayout);
    mainLayout->addLayout(rhsLayout);
    rhsBottomSpacer = new QSpacerItem(0,0,QSizePolicy::Fixed,QSizePolicy::Expanding);
    rhsLayout->addSpacerItem(rhsBottomSpacer);

    QFont font = configGroup->font();
    configGroup->setStyleSheet(QString("QGroupBox{border:1px solid gray;} *{padding: 0px 0px 0px 0px; margin: 0px 0px 0px 0px; font-size: %0px;}").arg(font.pointSize()-1));
    configGroup->setMaximumWidth(240);
    configRef = new QSlider(Qt::Orientation::Horizontal);
    configRef->setRange(-200,200);
    configRef->setTickInterval(50);
    configRef->setSingleStep(20);
    configRef->setValue(0);
    configRef->setAccessibleName(tr("Scope display reference"));
    configRef->setAccessibleDescription(tr("Selects the display reference for the Scope display"));
    configRef->setToolTip(tr("Select display reference of scope"));
    configLayout->addRow(tr("Ref"),configRef);

    configLength = new QSlider(Qt::Orientation::Horizontal);
    configLength->setRange(100,1024);
    configLength->setValue(wfLength);
    configLayout->addRow(tr("Length"),configLength);

    configTop = new QSlider(Qt::Orientation::Horizontal);
    configTop->setRange(1,160);
    configTop->setValue(plotCeiling);
    configTop->setAccessibleName(tr("Scope display ceiling"));
    configTop->setAccessibleDescription(tr("Selects the display ceiling for the Scope display"));
    configTop->setToolTip(tr("Select display ceiling of scope"));
    configLayout->addRow(tr("Ceiling"),configTop);

    configBottom = new QSlider(Qt::Orientation::Horizontal);
    configBottom->setRange(0,160);
    configBottom->setValue(plotFloor);
    configBottom->setAccessibleName(tr("Scope display floor"));
    configBottom->setAccessibleDescription(tr("Selects the display floor for the Scope display"));
    configBottom->setToolTip(tr("Select display floor of scope"));
    configLayout->addRow(tr("Floor"),configBottom);

    configSpeed = new QComboBox();
    configSpeed->addItem(tr("Speed Fast"),QVariant::fromValue(uchar(0)));
    configSpeed->addItem(tr("Speed Mid"),QVariant::fromValue(uchar(1)));
    configSpeed->addItem(tr("Speed Slow"),QVariant::fromValue(uchar(2)));
    configSpeed->setCurrentIndex(configSpeed->findData(currentSpeed));
    configSpeed->setAccessibleName(tr("Waterfall display speed"));
    configSpeed->setAccessibleDescription(tr("Selects the speed for the waterfall display"));
    configSpeed->setToolTip(tr("Waterfall Speed"));
    configLayout->addRow(tr("Speed"),configSpeed);

    configTheme = new QComboBox();
    configTheme->setAccessibleName(tr("Waterfall display color theme"));
    configTheme->setAccessibleDescription(tr("Selects the color theme for the waterfall display"));
    configTheme->setToolTip(tr("Waterfall color theme"));
    configTheme->addItem("Jet", QCPColorGradient::gpJet);
    configTheme->addItem("Cold", QCPColorGradient::gpCold);
    configTheme->addItem("Hot", QCPColorGradient::gpHot);
    configTheme->addItem("Therm", QCPColorGradient::gpThermal);
    configTheme->addItem("Night", QCPColorGradient::gpNight);
    configTheme->addItem("Ion", QCPColorGradient::gpIon);
    configTheme->addItem("Gray", QCPColorGradient::gpGrayscale);
    configTheme->addItem("Geo", QCPColorGradient::gpGeography);
    configTheme->addItem("Hues", QCPColorGradient::gpHues);
    configTheme->addItem("Polar", QCPColorGradient::gpPolar);
    configTheme->addItem("Spect", QCPColorGradient::gpSpectrum);
    configTheme->addItem("Candy", QCPColorGradient::gpCandy);
    configTheme->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    configLayout->addRow(tr("Theme"),configTheme);

    configPbtInner = new QSlider(Qt::Orientation::Horizontal);
    configPbtInner->setRange(0,255);
    configLayout->addRow(tr("PBT Inner"),configPbtInner);

    configPbtOuter = new QSlider(Qt::Orientation::Horizontal);
    configPbtOuter->setRange(0,255);
    configLayout->addRow(tr("PBT Outer"),configPbtOuter);

    configIfShift = new QSlider(Qt::Orientation::Horizontal);
    configIfShift->setRange(0,255);
    configLayout->addRow(tr("IF Shift"),configIfShift);

    configFilterWidth = new QSlider(Qt::Orientation::Horizontal);
    configFilterWidth->setRange(0,10000);
    configLayout->addRow(tr("Fill Width"),configFilterWidth);

    connect(configLength, &QSlider::valueChanged, this, [=](const int &val) {
        prepareWf(val);
        emit updateSettings(receiver,currentTheme,wfLength,plotFloor,plotCeiling);
    });
    connect(configBottom, &QSlider::valueChanged, this, [=](const int &val) {
        this->plotFloor = val;
        this->wfFloor = val;
        this->setRange(plotFloor,plotCeiling);
        emit updateSettings(receiver,currentTheme,wfLength,plotFloor,plotCeiling);
    });
    connect(configTop, &QSlider::valueChanged, this, [=](const int &val) {
        this->plotCeiling = val;
        this->wfCeiling = val;
        this->setRange(plotFloor,plotCeiling);
        emit updateSettings(receiver,currentTheme,wfLength,plotFloor,plotCeiling);
    });

    connect(configRef, &QSlider::valueChanged, this, [=](const int &val) {
        currentRef = (val/5) * 5; // rounded to "nearest 5"
        queue->add(priorityImmediate,queueItem(receiver?funcScopeSubRef:funcScopeMainRef,QVariant::fromValue(currentRef),false,this->receiver));
    });


    connect(configSpeed, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](const int &val) {
        queue->add(priorityImmediate,queueItem(receiver?funcScopeSubSpeed:funcScopeMainSpeed,configSpeed->itemData(val),false,this->receiver));
    });

    connect(configTheme, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](const int &val) {
        Q_UNUSED(val)
        currentTheme = configTheme->currentData().value<QCPColorGradient::GradientPreset>();
        colorMap->setGradient(currentTheme);
        emit updateSettings(receiver,currentTheme,wfLength,plotFloor,plotCeiling);
    });



    connect(configPbtInner, &QSlider::valueChanged, this, [=](const int &val) {
        queue->add(priorityImmediate,queueItem(funcPBTInner,QVariant::fromValue<ushort>(val),false,receiver));
    });
    connect(configPbtOuter, &QSlider::valueChanged, this, [=](const int &val) {
        queue->add(priorityImmediate,queueItem(funcPBTInner,QVariant::fromValue<ushort>(val),false,receiver));
    });
    connect(configIfShift, &QSlider::valueChanged, this, [=](const int &val) {
        queue->add(priorityImmediate,queueItem(funcIFShift,QVariant::fromValue<ushort>(val),false,receiver));
    });
    connect(configFilterWidth, &QSlider::valueChanged, this, [=](const int &val) {
        queue->add(priorityImmediate,queueItem(funcFilterWidth,QVariant::fromValue<ushort>(val),false,receiver));
    });

    configGroup->setVisible(false);




    // Connections

    connect(detachButton,SIGNAL(toggled(bool)), this, SLOT(detachScope(bool)));
    connect(scopeModeCombo,SIGNAL(currentIndexChanged(int)),this,SLOT(updatedScopeMode(int)));
    connect(spanCombo,SIGNAL(currentIndexChanged(int)),this,SLOT(updatedSpan(int)));
    connect(confButton,SIGNAL(clicked()), this, SLOT(configPressed()),Qt::QueuedConnection);

    connect(toFixedButton,SIGNAL(clicked()), this, SLOT(toFixedPressed()));
    connect(edgeCombo,SIGNAL(currentIndexChanged(int)),this,SLOT(updatedEdge(int)));
    connect(edgeButton,SIGNAL(clicked()), this, SLOT(customSpanPressed()));

    connect(holdButton,SIGNAL(toggled(bool)), this, SLOT(holdPressed(bool)));

    connect(modeCombo,SIGNAL(currentIndexChanged(int)),this,SLOT(updatedMode(int)));
    connect(filterCombo,SIGNAL(currentIndexChanged(int)),this,SLOT(updatedMode(int)));
    connect(dataCombo,SIGNAL(currentIndexChanged(int)),this,SLOT(updatedMode(int)));
    connect(clearPeaksButton,SIGNAL(clicked()), this, SLOT(clearPeaks()));

    connect(spectrum, SIGNAL(mouseDoubleClick(QMouseEvent*)), this, SLOT(doubleClick(QMouseEvent*)));
    connect(waterfall, SIGNAL(mouseDoubleClick(QMouseEvent*)), this, SLOT(doubleClick(QMouseEvent*)));
    connect(spectrum, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(scopeClick(QMouseEvent*)));
    connect(waterfall, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(waterfallClick(QMouseEvent*)));
    connect(spectrum, SIGNAL(mouseRelease(QMouseEvent*)), this, SLOT(scopeMouseRelease(QMouseEvent*)));
    connect(spectrum, SIGNAL(mouseMove(QMouseEvent*)), this, SLOT(scopeMouseMove(QMouseEvent *)));
    connect(waterfall, SIGNAL(mouseWheel(QWheelEvent*)), this, SLOT(scroll(QWheelEvent*)));
    connect(spectrum, SIGNAL(mouseWheel(QWheelEvent*)), this, SLOT(scroll(QWheelEvent*)));

    showHideControls(spectrumMode_t::spectModeCenter);
}

void spectrumScope::prepareScope(uint maxAmp, uint spectWidth)
{
    this->spectWidth = spectWidth;
    this->maxAmp = maxAmp;
}

bool spectrumScope::prepareWf(uint wf)
{
    QMutexLocker locker(&mutex);
    bool ret=true;

    this->wfLength = wf;
    configLength->blockSignals(true);
    configLength->setValue(this->wfLength);
    configLength->blockSignals(false);

    this->wfLengthMax = 1024;

    // Initialize before use!
    QByteArray empty((int)spectWidth, '\x01');
    spectrumPeaks = QByteArray( (int)spectWidth, '\x01' );

    //unsigned int oldSize = wfimage.size();
    if (!wfimage.empty() && wfimage.first().size() != spectWidth)
    {
        // Width of waterfall has changed!
        wfimage.clear();
    }

    for(unsigned int i = wfimage.size(); i < wfLength; i++)
    {
        wfimage.append(empty);
    }
    wfimage.remove(wfLength, wfimage.size()-wfLength);
    wfimage.squeeze();

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

    colorMap->setData(colorMapData,true); // Copy the colorMap so deleting it won't result in crash!

    waterfall->yAxis->setRangeReversed(true);
    waterfall->xAxis->setVisible(false);

    clearPeaks();
    clearPlasma();


    scopePrepared = true;
    return ret;
}

void spectrumScope::setRange(int floor, int ceiling)
{
    plotFloor = floor;
    plotCeiling = ceiling;
    wfFloor = floor;
    wfCeiling = ceiling;
    maxAmp = ceiling;
    if (spectrum != Q_NULLPTR)
        spectrum->yAxis->setRange(QCPRange(floor, ceiling));
    if (colorMap != Q_NULLPTR)
        colorMap->setDataRange(QCPRange(floor,ceiling));
    configBottom->blockSignals(true);
    configBottom->setValue(floor);
    configBottom->blockSignals(false);
    configTop->blockSignals(true);
    configTop->setValue(ceiling);
    configTop->blockSignals(false);
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
    if(cp->useSpectrumFillGradient) {
        spectrumGradient.setStart(QPointF(0,1));
        spectrumGradient.setFinalStop(QPointF(0,0));
        spectrumGradient.setCoordinateMode(QLinearGradient::ObjectMode);
        spectrumGradient.setColorAt(0, cp->spectrumFillBot);
        spectrumGradient.setColorAt(1, cp->spectrumFillTop);
        spectrum->graph(0)->setBrush(QBrush(spectrumGradient));
    } else {
        spectrum->graph(0)->setBrush(QBrush(cp->spectrumFill));
    }

    spectrum->graph(1)->setPen(QPen(cp->underlayLine));
    if(cp->useUnderlayFillGradient) {
        underlayGradient.setStart(QPointF(0,1));
        underlayGradient.setFinalStop(QPointF(0,0));
        underlayGradient.setCoordinateMode(QLinearGradient::ObjectMode);
        underlayGradient.setColorAt(0, cp->underlayFillBot);
        underlayGradient.setColorAt(1, cp->underlayFillTop);
        spectrum->graph(1)->setBrush(QBrush(underlayGradient));
    } else {
        spectrum->graph(1)->setBrush(QBrush(cp->underlayFill));
    }

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

bool spectrumScope::updateScope(scopeData data)
{
    if (!scopePrepared )
    {
        return false;
    }

    qint64 spectime = 0;

    QElapsedTimer elapsed;
    QElapsedTimer totalUpdateTime;
    totalUpdateTime.start();

    bool updateRange = false;

    if (data.startFreq != lowerFreq || data.endFreq != upperFreq)
    {
        if(underlayMode == underlayPeakHold)
        {
            // TODO: create non-button function to do this
            // This will break if the button is ever moved or renamed.
            clearPeaks();
        }
        // Inform other threads (cluster) that the frequency range has changed.
        emit frequencyRange(receiver, data.startFreq, data.endFreq);
    }

    lowerFreq = data.startFreq;
    upperFreq = data.endFreq;

    //qInfo(logSystem()) << "start: " << data.startFreq << " end: " << data.endFreq;
    quint16 specLen = data.data.length();

    if (specLen != spectWidth)
    {
        qCritical(logSystem()) << "Spectrum length error! Expected" << spectWidth << "got" << specLen;
        return false;
    }

    QVector <double> x(spectWidth), y(spectWidth), y2(spectWidth);

    for(int i=0; i < spectWidth; i++)
    {
        x[i] = (i * (data.endFreq-data.startFreq)/spectWidth) + data.startFreq;
    }

    for(int i=0; i< specLen; i++)
    {
        y[i] = (unsigned char)data.data[i];
        if(underlayMode == underlayPeakHold)
        {
            if((unsigned char)data.data[i] > (unsigned char)spectrumPeaks[i])
            {
                spectrumPeaks[i] = data.data[i];
            }
            y2[i] = (unsigned char)spectrumPeaks[i];
        }
    }
    plasmaMutex.lock();
    spectrumPlasma[spectrumPlasmaPosition] = data.data;
    spectrumPlasmaPosition = (spectrumPlasmaPosition+1) % spectrumPlasmaSizeCurrent;
    //spectrumPlasma.push_front(data.data);
//    if(spectrumPlasma.size() > (int)spectrumPlasmaSizeCurrent)
//    {
//        // If we have pushed_front more than spectrumPlasmaSize,
//        // then we cut one off the back.
//        spectrumPlasma.pop_back();
//    }
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

        switch (mode.mk)
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

        if ((mode.mk == modeCW || mode.mk == modeCW_R) && passbandWidth > 0.0006)
        {
            pbtDefault = round((passbandWidth - (cwPitch / 1000000.0)) * 200000.0) / 200000.0;
        }
        else
        {
            pbtDefault = 0.0;
        }

        if ((PBTInner - pbtDefault || PBTOuter - pbtDefault) && passbandAction != passbandResizing && mode.mk != modeFM)
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
        if (mode.mk == modeLSB || mode.mk == modeCW || mode.mk == modeRTTY) {
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
    elapsed.start();
    spectrum->replot();
    spectime = elapsed.elapsed();

    if(specLen == spectWidth)
    {
        wfimage.prepend(data.data);
        wfimage.pop_back();
        QByteArray wfRow;
        // Waterfall:
        for(int row = 0; row < wfLength; row++)
        {
            wfRow = wfimage[row];
            for(int col = 0; col < spectWidth; col++)
            {
                colorMap->data()->setCell( col, row, (unsigned char)wfRow[col]);
            }
        }
        if(updateRange)
        {
            colorMap->setDataRange(QCPRange(wfFloor, wfCeiling));
        }

        waterfall->yAxis->setRange(0,wfLength - 1);
        waterfall->xAxis->setRange(0, spectWidth-1);
        elapsed.start();
        waterfall->replot();
        redrawSpeed->setText(QString("%0ms/%1ms").arg(spectime).arg(totalUpdateTime.elapsed()));

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
        qInfo(logSystem()) << "Scope out of range";
    } else if (!data.oor && oorIndicator->visible()) {
        oorIndicator->setVisible(false);
    }

    emit elapsedTime(receiver, elapsed.elapsed());

    return true;
}




// Plasma functions

void spectrumScope::resizePlasmaBuffer(int size) {
    // QMutexLocker locker(&plasmaMutex);
    qDebug() << "Resizing plasma buffer via parameter, from oldsize " << spectrumPlasmaSizeCurrent << " to new size: " << size;
    spectrumPlasmaSizeCurrent = size;
    return;
}

void spectrumScope::clearPeaks()
{
    // Clear the spectrum peaks as well as the plasma buffer
    spectrumPeaks = QByteArray( (int)spectWidth, '\x01' );
    //clearPlasma();
}

void spectrumScope::clearPlasma()
{
    // Clear the buffer of spectrum used for peak and average computation.
    // This is only needed one time, when the VFO is created with spectrum size info.
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
    // Spec PlasmaLine is a single line of spectrum, ~~600 pixels or however many the radio provides.
    // This changes width only when we connect to a new radio.
    if(spectrumPlasmaLine.size() != spectWidth) {
        spectrumPlasmaLine.clear();
        spectrumPlasmaLine.resize(spectWidth);
    }

    // spectrumPlasma is the bufffer of spectrum lines to use when computing the average or peak.

    int specPlasmaSize = spectrumPlasmaSizeCurrent; // go only this far in
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
            spectrumPlasmaLine[col] = spectrumPlasma[0][col]; // initial value
            for(int pos=0; pos < specPlasmaSize; pos++)
            {
                if((double)((unsigned char)spectrumPlasma[pos][col]) > spectrumPlasmaLine[col])
                    spectrumPlasmaLine[col] = (unsigned char)spectrumPlasma[pos][col];
            }
        }
    }
}

void spectrumScope::showHideControls(spectrumMode_t mode)
{
    if (!hasScope && spectrum->isVisible()) {
        spectrum->hide();
        waterfall->hide();
        splitter->hide();
        detachButton->hide();
        scopeModeCombo->hide();
        edgeCombo->hide();
        edgeButton->hide();
        holdButton->hide();
        toFixedButton->hide();
        spanCombo->hide();
        clearPeaksButton->hide();
        confButton->hide();
    } else if (hasScope && (mode==spectModeCenter || mode==spectModeScrollC) && !spanCombo->isVisible()) {
        spectrum->show();
        waterfall->show();
        splitter->show();
        detachButton->show();
        scopeModeCombo->show();
        edgeCombo->hide();
        edgeButton->hide();
        toFixedButton->show();
        spanCombo->show();
        clearPeaksButton->show();
        confButton->show();
    } else if (hasScope && (mode==spectModeFixed || mode == spectModeScrollC || mode == spectModeScrollF) && spanCombo->isVisible()) {
        spectrum->show();
        waterfall->show();
        splitter->show();
        detachButton->show();
        scopeModeCombo->show();
        edgeCombo->show();
        edgeButton->show();
        toFixedButton->hide();
        spanCombo->hide();
        clearPeaksButton->show();
        confButton->show();
    }
}

void spectrumScope::enableScope(bool en)
{
    this->splitter->setVisible(en);
    // Hide these controls if disabled
    if (!en) {
        this->edgeCombo->setVisible(en);
        this->edgeButton->setVisible(en);
        this->toFixedButton->setVisible(en);
        this->spanCombo->setVisible(en);
    }
    this->clearPeaksButton->setVisible(en);
    this->holdButton->setVisible(en);
}

void spectrumScope::selectScopeMode(spectrumMode_t m)
{
    scopeModeCombo->blockSignals(true);
    scopeModeCombo->setCurrentIndex(scopeModeCombo->findData(m));
    scopeModeCombo->blockSignals(false);
    showHideControls(m);
}

void spectrumScope::selectSpan(centerSpanData s)
{
    spanCombo->blockSignals(true);
    spanCombo->setCurrentIndex(spanCombo->findText(s.name));
    spanCombo->blockSignals(false);
}

void spectrumScope::updatedScopeMode(int index)
{
    //spectrumMode_t s = static_cast<spectrumMode_t>(scopeModeCombo->itemData(index).toInt());
    spectrumMode_t s = scopeModeCombo->itemData(index).value<spectrumMode_t>();

    queue->add(priorityImmediate,queueItem((receiver?funcScopeSubMode:funcScopeMainMode),QVariant::fromValue(s),false,receiver));

    showHideControls(s);
}

void spectrumScope::updatedSpan(int index)
{
    queue->add(priorityImmediate,queueItem((receiver?funcScopeSubSpan:funcScopeMainSpan),spanCombo->itemData(index),false,receiver));
}

void spectrumScope::updatedMode(int index)
{
    Q_UNUSED(index) // We don't know where it came from!
    modeInfo mi = modeCombo->currentData().value<modeInfo>();
    mi.filter = filterCombo->currentData().toInt();
    if (mi.mk == modeCW || mi.mk == modeCW_R || mi.mk == modeRTTY || mi.mk == modeRTTY_R || mi.mk == modePSK || mi.mk == modePSK_R)
    {
        mi.data = 0;
        dataCombo->setEnabled(false);
    } else {
        mi.data = dataCombo->currentIndex();
        dataCombo->setEnabled(true);
    }
    queue->add(priorityImmediate,queueItem((receiver?funcSubMode:funcMainMode),QVariant::fromValue(mi),false,receiver));

}


void spectrumScope::updatedEdge(int index)
{
    queue->add(priorityImmediate,queueItem((receiver?funcScopeSubEdge:funcScopeMainEdge),QVariant::fromValue<uchar>(index+1),false,receiver));
}

void spectrumScope::toFixedPressed()
{
    int currentEdge = edgeCombo->currentIndex();
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
            edgeCombo->blockSignals(true);
            edgeCombo->setCurrentIndex(edge-1);
            edgeCombo->blockSignals(false);
            queue->add(priorityImmediate,queueItem(funcScopeFixedEdgeFreq,QVariant::fromValue(spectrumBounds(lowerFreq, upperFreq, edge)),false,receiver));
            queue->add(priorityImmediate,queueItem((receiver?funcScopeSubMode:funcScopeMainMode),QVariant::fromValue<uchar>(spectrumMode_t::spectModeFixed),false,receiver));
        }
    }
}

void spectrumScope::customSpanPressed()
{
    double lowFreq = lowerFreq;
    double highFreq = upperFreq;
    QString freqstring = QString("%1, %2").arg(lowFreq).arg(highFreq);
    bool ok;

    QString userFreq = QInputDialog::getText(this, tr("Scope Edges"),
                          tr("Please enter desired scope edges, in MHz,\nwith a comma between the low and high range."),
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
                qDebug(logGui()) << "setting edge to: " << lowFreq << ", " << highFreq << ", edge num: " << edgeCombo->currentIndex() + 1;
                queue->add(priorityImmediate,queueItem(funcScopeFixedEdgeFreq,
                                                        QVariant::fromValue(spectrumBounds(lowFreq, highFreq, edgeCombo->currentIndex() + 1))));
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
        URLmsgBox.setText(tr("Error, could not interpret your input.\
                          <br/>Please make sure to place a comma between the frequencies.\
                          <br/>For example: '7.200, 7.300'"));
        URLmsgBox.exec();

        return;
    }


}


void spectrumScope::doubleClick(QMouseEvent *me)
{
    if (me->button() == Qt::LeftButton)
    {
        double x;
        freqt freqGo;
        if (!lock)
        {
            //y = plot->yAxis->pixelToCoord(me->pos().y());
            x = spectrum->xAxis->pixelToCoord(me->pos().x());
            freqGo.Hz = x * 1E6;
            freqGo.Hz = roundFrequency(freqGo.Hz, stepSize);
            freqGo.MHzDouble = (float)freqGo.Hz / 1E6;
            setFrequency(freqGo);
            queue->add(priorityImmediate,queueItem((receiver?funcSubFreq:funcMainFreq),QVariant::fromValue<freqt>(freqGo),false,receiver));

        }
    }
    else if (me->button() == Qt::RightButton)
    {
        QCPAbstractItem* item = spectrum->itemAt(me->pos(), true);
        QCPItemRect* rectItem = dynamic_cast<QCPItemRect*> (item);
        if (rectItem != nullptr)
        {
            double pbFreq = (pbtDefault / passbandWidth) * 127.0;
            qint16 newFreq = pbFreq + 128;
            queue->add(priorityImmediate,queueItem(funcPBTInner,QVariant::fromValue<ushort>(newFreq),false,receiver));
            queue->add(priorityImmediate,queueItem(funcPBTOuter,QVariant::fromValue<ushort>(newFreq),false,receiver));
        }
    }
}

void spectrumScope::scopeClick(QMouseEvent* me)
{
    QCPAbstractItem* item = spectrum->itemAt(me->pos(), true);
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
        this->mousePressFreq = spectrum->xAxis->pixelToCoord(cursor);
        if (textItem != nullptr)
        {
            QMap<QString, spotData*>::iterator spot = clusterSpots.find(textItem->text());
            if (spot != clusterSpots.end() && spot.key() == textItem->text())
            {
                qInfo(logGui()) << "Clicked on spot:" << textItem->text();
                freqt freqGo;
                freqGo.Hz = (spot.value()->frequency) * 1E6;
                freqGo.MHzDouble = spot.value()->frequency;
                setFrequency(freqGo);
                queue->add(priorityImmediate,queueItem((receiver?funcSubFreq:funcMainFreq),QVariant::fromValue<freqt>(freqGo),false,receiver));
            }
        }
        else if (passbandAction == passbandStatic && rectItem != nullptr)
        {
            if ((cursor <= leftPix && cursor > leftPix - 10) || (cursor >= rightPix && cursor < rightPix + 10))
            {
                passbandAction = passbandResizing;
            }
        }
        // TODO clickdragtuning and sending messages to statusbar

        else if (clickDragTuning)
        {
            emit showStatusBarText(QString("Selected %1 MHz").arg(this->mousePressFreq));
        }
        else {
            emit showStatusBarText(QString("Selected %1 MHz").arg(this->mousePressFreq));
        }

    }
    else if (me->button() == Qt::RightButton)
    {
        if (textItem != nullptr) {
            QMap<QString, spotData*>::iterator spot = clusterSpots.find(textItem->text());
            if (spot != clusterSpots.end() && spot.key() == textItem->text()) {
                // parent and children are destroyed on close
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
            this->mousePressFreq = spectrum->xAxis->pixelToCoord(cursor);
        }
    }
}

void spectrumScope::scopeMouseRelease(QMouseEvent* me)
{

    QCPAbstractItem* item = spectrum->itemAt(me->pos(), true);
    QCPItemText* textItem = dynamic_cast<QCPItemText*> (item);

    if (textItem == nullptr && clickDragTuning) {
        this->mouseReleaseFreq = spectrum->xAxis->pixelToCoord(me->pos().x());
        double delta = mouseReleaseFreq - mousePressFreq;
        qInfo(logGui()) << "Mouse release delta: " << delta;
    }

    if (passbandAction != passbandStatic) {
        passbandAction = passbandStatic;
    }
}

void spectrumScope::scopeMouseMove(QMouseEvent* me)
{
    QCPAbstractItem* item = spectrum->itemAt(me->pos(), true);
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
    double movedFrequency = spectrum->xAxis->pixelToCoord(cursor) - mousePressFreq;

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
            double origin = passbandCenterFrequency;
            if (mode.mk == modeLSB)
            {
                origin = - passbandCenterFrequency;
            }

            if (spectrum->xAxis->pixelToCoord(cursor) >= freq.MHzDouble + origin) {
                pb = spectrum->xAxis->pixelToCoord(cursor) - passbandIndicator->topLeft->coords().x();
            }
            else {
                pb = passbandIndicator->bottomRight->coords().x() - spectrum->xAxis->pixelToCoord(cursor);
            }
            queue->add(priorityImmediate,queueItem(funcFilterWidth,QVariant::fromValue<ushort>(pb * 1000000),false,receiver));
            //qInfo() << "New passband" << uint(pb * 1000000);

            lastFreq = movedFrequency;
        }
    }
    else if (passbandAction == pbtMoving) {

        //qint16 shift = (qint16)(level - 128);
        //TPBFInner = (double)(shift / 127.0) * (passbandWidth);
        // Only move if more than 100Hz
        static double lastFreq = movedFrequency;
        if (lastFreq - movedFrequency > 0.000049 || movedFrequency - lastFreq > 0.000049) {

            double innerFreq = ((double)(PBTInner + movedFrequency) / passbandWidth) * 127.0;
            double outerFreq = ((double)(PBTOuter + movedFrequency) / passbandWidth) * 127.0;

            qint16 newInFreq = innerFreq + 128;
            qint16 newOutFreq = outerFreq + 128;

            if (newInFreq >= 0 && newInFreq <= 255 && newOutFreq >= 0 && newOutFreq <= 255) {
                qDebug() << QString("Moving passband by %1 Hz (Inner %2) (Outer %3) Mode:%4").arg((qint16)(movedFrequency * 1000000))
                                .arg(newInFreq).arg(newOutFreq).arg(mode.mk);
                queue->add(priorityImmediate,queueItem(funcPBTInner,QVariant::fromValue<ushort>(newInFreq),false,receiver));
                queue->add(priorityImmediate,queueItem(funcPBTOuter,QVariant::fromValue<ushort>(newOutFreq),false,receiver));
            }
            lastFreq = movedFrequency;
        }
    }
    else if (passbandAction == pbtInnerMove) {
        static double lastFreq = movedFrequency;
        if (lastFreq - movedFrequency > 0.000049 || movedFrequency - lastFreq > 0.000049) {
            double pbFreq = ((double)(PBTInner + movedFrequency) / passbandWidth) * 127.0;
            qint16 newFreq = pbFreq + 128;
            if (newFreq >= 0 && newFreq <= 255) {
                queue->add(priorityImmediate,queueItem(funcPBTInner,QVariant::fromValue<ushort>(newFreq),false,receiver));
            }
            lastFreq = movedFrequency;
        }
    }
    else if (passbandAction == pbtOuterMove) {
        static double lastFreq = movedFrequency;
        if (lastFreq - movedFrequency > 0.000049 || movedFrequency - lastFreq > 0.000049) {
            double pbFreq = ((double)(PBTOuter + movedFrequency) / passbandWidth) * 127.0;
            qint16 newFreq = pbFreq + 128;
            if (newFreq >= 0 && newFreq <= 255) {
                queue->add(priorityImmediate,queueItem(funcPBTOuter,QVariant::fromValue<ushort>(newFreq),false,receiver));
            }
            lastFreq = movedFrequency;
        }
    }
    else  if (passbandAction == passbandStatic && me->buttons() == Qt::LeftButton && textItem == nullptr && clickDragTuning)
    {
        double delta = spectrum->xAxis->pixelToCoord(cursor) - mousePressFreq;
        qDebug(logGui()) << "Mouse moving delta: " << delta;
        if( (( delta < -0.0001 ) || (delta > 0.0001)) && ((delta < 0.501) && (delta > -0.501)) )
        {
            freqt freqGo;
            freqGo.Hz = (freq.MHzDouble + delta) * 1E6;
            freqGo.Hz = roundFrequency(freqGo.Hz, stepSize);
            freqGo.MHzDouble = (float)freqGo.Hz / 1E6;
            setFrequency(freqGo);
            queue->add(priorityImmediate,queueItem((receiver?funcSubFreq:funcMainFreq),QVariant::fromValue<freqt>(freqGo),false,receiver));
        }
    }
    else {
        setCursor(Qt::ArrowCursor);
    }

}

void spectrumScope::waterfallClick(QMouseEvent *me)
{
        double x = spectrum->xAxis->pixelToCoord(me->pos().x());
        emit showStatusBarText(QString("Selected %1 MHz").arg(x));
}

void spectrumScope::scroll(QWheelEvent *we)
{
    // angleDelta is supposed to be eights of a degree of mouse wheel rotation
    int deltaY = we->angleDelta().y();
    int deltaX = we->angleDelta().x();
    int delta = (deltaX==0)?deltaY:deltaX;

    qreal offset = qreal(delta) / qreal((deltaX==0)?scrollYperClick:scrollXperClick);
    //qreal offset = qreal(delta) / 120;

    qreal stepsToScroll = QApplication::wheelScrollLines() * offset;

    // Did we change direction?
    if( (scrollWheelOffsetAccumulated > 0) && (offset > 0) ) {
        scrollWheelOffsetAccumulated += stepsToScroll;
    } else if ((scrollWheelOffsetAccumulated < 0) && (offset < 0)) {
        scrollWheelOffsetAccumulated += stepsToScroll;
    } else {
        // Changed direction, zap the old accumulation:
        scrollWheelOffsetAccumulated = stepsToScroll;
        //qInfo() << "Scroll changed direction";
    }

    int clicks = int(scrollWheelOffsetAccumulated);

    if (!clicks) {
//        qInfo() << "Rejecting minor scroll motion, too small. Wheel Clicks: " << clicks << ", angleDelta: " << delta;
//        qInfo() << "Accumulator value: " << offsetAccumulated;
//        qInfo() << "stepsToScroll: " << stepsToScroll;
//        qInfo() << "Storing scroll motion for later use.";
        return;
    } else {
//        qInfo() << "Accepted scroll motion. Wheel Clicks: " << clicks << ", angleDelta: " << delta;
//        qInfo() << "Accumulator value: " << offsetAccumulated;
//        qInfo() << "stepsToScroll: " << stepsToScroll;
    }

    // If we made it this far, it's time to scroll and to ultimately
    // clear the scroll accumulator.

    unsigned int stepsHz = stepSize;

    Qt::KeyboardModifiers key = we->modifiers();
    if ((key == Qt::ShiftModifier) && (stepsHz != 1))
    {
        stepsHz /= 10;
    }
    else if (key == Qt::ControlModifier)
    {
        stepsHz *= 10;
    }

    freqt f;
    f.Hz = roundFrequency(freq.Hz, clicks, stepsHz);
    f.MHzDouble = f.Hz / (double)1E6;

    freq = f; // Do we need to do this?

    setFrequency(f);
    queue->add(priorityImmediate,queueItem((receiver?funcSubFreq:funcMainFreq),QVariant::fromValue<freqt>(f),false,receiver));
    //qInfo() << "Moving to freq:" << f.Hz << "step" << stepsHz;
    scrollWheelOffsetAccumulated = 0;
}



void spectrumScope::receiveMode(modeInfo m, uchar vfo)
{
    // Update mode information if mode/filter/data has changed.
    // Not all rigs send data so this "might" need to be updated independantly?
    if (vfo > 0)
        return;

    if (mode.reg != m.reg || m.filter != mode.filter || m.data != mode.data)
    {

        qDebug(logSystem()) << __func__ << QString("Received new mode for %0: %1 (%2) filter:%3 data:%4")
                                              .arg((receiver?"Sub":"Main")).arg(QString::number(m.mk,16)).arg(m.name).arg(m.filter).arg(m.data) ;

        if (mode.mk != m.mk) {
            for (int i=0;i<modeCombo->count();i++)
            {
                modeInfo mi = modeCombo->itemData(i).value<modeInfo>();
                if (mi.mk == m.mk)
                {
                    modeCombo->blockSignals(true);
                    modeCombo->setCurrentIndex(i);
                    modeCombo->blockSignals(false);
                    break;
                }
            }
        }

        if (m.filter && mode.filter != m.filter)
        {
            filterCombo->blockSignals(true);
            filterCombo->setCurrentIndex(filterCombo->findData(m.filter));
            filterCombo->blockSignals(false);
        }

        if (mode.data != m.data)
        {
            emit dataChanged(m); // Signal wfmain that the data mode has been changed.
            dataCombo->blockSignals(true);
            dataCombo->setCurrentIndex(m.data);
            dataCombo->blockSignals(false);
        }

        if (m.mk == modeCW || m.mk == modeCW_R || m.mk == modeRTTY || m.mk == modeRTTY_R || m.mk == modePSK || m.mk == modePSK_R)
        {
            dataCombo->setEnabled(false);
        } else {
            dataCombo->setEnabled(true);
        }

        if (m.mk != mode.mk) {
            // We have changed mode so "may" need to change regular commands

            passbandCenterFrequency = 0.0;

            // Set config specific options)
            configFilterWidth->blockSignals(true);
            configFilterWidth->setMinimum(m.bwMin);
            configFilterWidth->setMaximum(m.bwMax);
            configFilterWidth->blockSignals(false);

            // If new mode doesn't allow bandwidth control, disable filterwidth and pbt.
            if (m.bwMin > 0 && m.bwMax > 0) {
                queue->addUnique(priorityHigh,funcPBTInner,true,receiver);
                queue->addUnique(priorityHigh,funcPBTOuter,true,receiver);
                queue->addUnique(priorityHigh,funcFilterWidth,true,receiver);
            } else{
                queue->del(funcPBTInner,receiver);
                queue->del(funcPBTOuter,receiver);
                queue->del(funcFilterWidth,receiver);
            }

#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif

            switch (m.mk) {
                // M0VSE this needs to be replaced with 1/2 the "actual" width of the RTTY signal+the mark freq.
            case modeRTTY:
            case modeRTTY_R:
                passbandCenterFrequency = 0.00008925;
                queue->del(funcCwPitch,receiver);
                queue->del(funcDashRatio,receiver);
                queue->del(funcKeySpeed,receiver);
                break;
            case modeLSB:
            case modeUSB:
            case modePSK:
            case modePSK_R:
                passbandCenterFrequency = 0.0015;
            case modeAM:
                queue->del(funcCwPitch,receiver);
                queue->del(funcDashRatio,receiver);
                queue->del(funcKeySpeed,receiver);
                break;
            case modeCW:
            case modeCW_R:
                queue->addUnique(priorityLow,funcCwPitch,true,receiver);
                queue->addUnique(priorityLow,funcDashRatio,true,receiver);
                queue->addUnique(priorityLow,funcKeySpeed,true,receiver);
                break;
            default:
                // FM and digital modes are fixed width, not sure about any other modes?
                if (mode.filter == 1)
                    passbandWidth = 0.015;
                else if (mode.filter == 2)
                    passbandWidth = 0.010;
                else
                    passbandWidth = 0.007;
                break;
                queue->del(funcCwPitch,receiver);
                queue->del(funcDashRatio,receiver);
                queue->del(funcKeySpeed,receiver);
                break;
            }
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif

        }
        mode = m;
    }
}

quint64 spectrumScope::roundFrequency(quint64 frequency, unsigned int tsHz)
{
    return roundFrequency(frequency, 0, tsHz);
}

quint64 spectrumScope::roundFrequency(quint64 frequency, int steps, unsigned int tsHz)
{
    if(steps > 0)
    {
        frequency = frequency + (quint64)(steps*tsHz);
    } else if (steps < 0) {
        frequency = frequency - std::min((quint64)(abs(steps)*tsHz), frequency);
    }

    quint64 rounded = frequency;

    if(tuningFloorZeros)
    {
        rounded = ((frequency % tsHz) > tsHz/2) ? frequency + tsHz - frequency%tsHz : frequency - frequency%tsHz;
    }

    return rounded;
}


void spectrumScope::receiveCwPitch(uchar pitch)
{
    if (mode.mk == modeCW || mode.mk == modeCW_R) {
        quint16 p = round((((600.0 / 255.0) * pitch) + 300) / 5.0) * 5.0;
        if (p != this->cwPitch)
        {
            passbandCenterFrequency = p / 2000000.0;
            qDebug(logSystem()) << QString("%0 Received new CW Pitch %1 Hz was %2 (center freq %3 MHz)").arg((receiver?"Sub":"Main")).arg(p).arg(cwPitch).arg(passbandCenterFrequency);
            this->cwPitch = p;
        }
    }
}

void spectrumScope::receivePassband(quint16 pass)
{
    double pb = (double)(pass / 1000000.0);
    if (passbandWidth != pb) {
        passbandWidth = pb;
        //trxadj->updatePassband(pass);
        qDebug(logSystem()) << QString("%0 Received new IF Filter/Passband %1 Hz").arg(receiver?"Sub":"Main").arg(pass);
        emit showStatusBarText(QString("%0 IF filter width %1 Hz (%2 MHz)").arg(receiver?"Sub":"Main").arg(pass).arg(passbandWidth));
    }
}

void spectrumScope::selected(bool en)
{
    isActive = en;
    if (en)
        this->setStyleSheet("QGroupBox { border:1px solid red;}");
    else
        this->setStyleSheet(defaultStyleSheet);
    //this->setStyleSheet("QGroupBox { border:2px solid gray;}");
}

void spectrumScope::holdPressed(bool en)
{
    queue->add(priorityImmediate,queueItem(receiver?funcScopeSubHold:funcScopeMainHold,QVariant::fromValue(en),false,receiver));
}

void spectrumScope::setHold(bool h)
{
    this->holdButton->blockSignals(true);
    this->holdButton->setChecked(h);
    this->holdButton->blockSignals(false);
}

void spectrumScope::setSpeed(uchar s)
{
    this->currentSpeed = s;
    configSpeed->setCurrentIndex(configSpeed->findData(currentSpeed));
}


void spectrumScope::receiveSpots(uchar receiver, QList<spotData> spots)
{
    if (receiver != this->receiver) {
        return;
    }
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

            QCPRange xrange=spectrum->xAxis->range();
            QCPRange yrange=spectrum->yAxis->range();
            double left = s.frequency;
            double top = yrange.upper-10.0;

            bool conflict = true;
            while (conflict) {
#if QCUSTOMPLOT_VERSION < 0x020000
                QCPAbstractItem* absitem = spectrum->itemAt(QPointF(spectrum->xAxis->coordToPixel(left),spectrum->yAxis->coordToPixel(top)), true);
#else
                QCPAbstractItem* absitem = spectrum->itemAt(QPointF(spectrum->xAxis->coordToPixel(left),spectrum->yAxis->coordToPixel(top)), true);
#endif
                QCPItemText* item = dynamic_cast<QCPItemText*> (absitem);
                if (item != nullptr) {
                    top = top - 10.0;
                    if (top < 10.0)
                    {
                        top = yrange.upper-10.0;
                        left = left + (xrange.size()/20);
                    }
                }
                else {
                    conflict = false;
                }
            }
            spotData* sp = new spotData(s);

            //qDebug(logCluster()) << "ADD:" << sp->dxcall;
            sp->current = !current;
            sp->text = new QCPItemText(spectrum);
            sp->text->setAntialiased(true);
            sp->text->setColor(colors.clusterSpots);
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
            sp->text->setVisible(true);
            clusterSpots.insert(sp->dxcall, sp);
        }
    }

    QMap<QString, spotData*>::iterator spot2 = clusterSpots.begin();
    while (spot2 != clusterSpots.end()) {
        if (spot2.value()->current == current) {
            spectrum->removeItem(spot2.value()->text);
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

void spectrumScope::configPressed()
{
    this->configGroup->setVisible(!this->configGroup->isVisible());
    //QTimer::singleShot(200, this, [this]() {
        if (this->configGroup->isVisible())
            this->confButton->setText(">");
        else
            this->confButton->setText("<");
    //});
}


void spectrumScope::wfTheme(int num)
{
    currentTheme = QCPColorGradient::GradientPreset(num);
    colorMap->setGradient(static_cast<QCPColorGradient::GradientPreset>(num));
    configTheme->setCurrentIndex(configTheme->findData(currentTheme));
}

void spectrumScope::setPBTInner (uchar val) {
    qint16 shift = (qint16)(val - 128);
    double tempVar = ceil((shift / 127.0) * passbandWidth * 20000.0) / 20000.0;
    // tempVar now contains value to the nearest 50Hz If CW mode, add/remove the cwPitch.
    double pitch = 0.0;
    if ((this->mode.mk == modeCW || this->mode.mk == modeCW_R) && this->passbandWidth > 0.0006)
    {
        pitch = (600.0 - cwPitch) / 1000000.0;
    }

    double newPBT = round((tempVar + pitch) * 200000.0) / 200000.0; // Nearest 5Hz.

    if (newPBT != this->PBTInner) {
        this->PBTInner = newPBT;
        qInfo(logSystem()) << "New PBT Inner value received" << this->PBTInner << "CW Pitch" << this->cwPitch << "Passband" << this->passbandWidth;
        double pbFreq = ((double)(this->PBTInner) / this->passbandWidth) * 127.0;
        qint16 newFreq = pbFreq + 128;
        if (newFreq >= 0 && newFreq <= 255) {
            configPbtInner->blockSignals(true);
            configPbtInner->setValue(newFreq);
            configPbtInner->blockSignals(false);
        }
    }
}

void spectrumScope::setPBTOuter (uchar val) {
    qint16 shift = (qint16)(val - 128);
    double tempVar = ceil((shift / 127.0) * this->passbandWidth * 20000.0) / 20000.0;
    // tempVar now contains value to the nearest 50Hz If CW mode, add/remove the cwPitch.
    double pitch = 0.0;
    if ((this->mode.mk == modeCW || this->mode.mk == modeCW_R) && this->passbandWidth > 0.0006)
    {
        pitch = (600.0 - cwPitch) / 1000000.0;
    }

    double newPBT = round((tempVar + pitch) * 200000.0) / 200000.0; // Nearest 5Hz.

    if (newPBT != this->PBTOuter) {
        this->PBTOuter = newPBT;
        qInfo(logSystem()) << "New PBT Outer value received" << this->PBTOuter << "CW Pitch" << this->cwPitch << "Passband" << this->passbandWidth;
        double pbFreq = ((double)(this->PBTOuter) / this->passbandWidth) * 127.0;
        qint16 newFreq = pbFreq + 128;
        if (newFreq >= 0 && newFreq <= 255) {
            configPbtOuter->blockSignals(true);
            configPbtOuter->setValue(newFreq);
            configPbtOuter->blockSignals(false);
        }
    }
}

void spectrumScope::setFrequency(freqt f, uchar vfo)
{
    //qInfo() << "Setting Frequency vfo=" << vfo << "Freq:" << f.Hz;

    if (vfo < numVFO)
    {
        freqDisplay[vfo]->blockSignals(true);
        freqDisplay[vfo]->setFrequency(f.Hz);
        freqDisplay[vfo]->blockSignals(false);
    }
    if (vfo==0)
        freq = f;

}

void spectrumScope::displaySettings(int numDigits, qint64 minf, qint64 maxf, int minStep,FctlUnit unit,std::vector<bandType>* bands)
{
    for (uchar i=0;i<numVFO;i++)
        freqDisplay[i]->setup(numDigits, minf, maxf, minStep, unit, bands);
}

void spectrumScope::setUnit(FctlUnit unit)
{
    for (uchar i=0;i<numVFO;i++)
        freqDisplay[i]->setUnit(unit);
}


void spectrumScope::newFrequency(qint64 freq,uchar vfo)
{
    freqt f;
    f.Hz = freq;
    f.MHzDouble = f.Hz / (double)1E6;
    if (f.Hz > 0)
    {
        if (vfo > 0)
        {
            queue->addUnique(priorityImmediate,queueItem((funcUnselectedFreq),QVariant::fromValue<freqt>(f),false,receiver));
        }
        else
        {
            queue->addUnique(priorityImmediate,queueItem((receiver?funcSubFreq:funcMainFreq),QVariant::fromValue<freqt>(f),false,receiver));
        }
    }
}

void spectrumScope::setRef(int ref)
{
    configRef->setValue(ref);
}

void spectrumScope::setRefLimits(int lower, int upper)
{
    configRef->setRange(lower,upper);
}

void spectrumScope::detachScope(bool state)
{
    if (state)
    {
        windowLabel = new QLabel();
        detachButton->setText("Attach");
        qInfo(logGui()) << "Detaching scope" << (receiver?"Sub":"Main");
        this->parentWidget()->layout()->replaceWidget(this,windowLabel);

        QTimer::singleShot(1, [&](){
            if(originalParent) {
                this->originalParent->resize(1,1);
            }
        });

        this->parentWidget()->resize(1,1);
        this->setParent(NULL);

        this-> setWindowFlags(Qt::Window | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint);
        this->move(screen()->geometry().center() - frameGeometry().center());
    } else {
        detachButton->setText("Detach");
        qInfo(logGui()) << "Attaching scope" << (receiver?"Sub":"Main");
        windowLabel->parentWidget()->layout()->replaceWidget(windowLabel,this);

        QTimer::singleShot(1, [&](){
            if(originalParent) {
                this->originalParent->resize(1,1);
            }
        });

        windowLabel->setParent(NULL);
        delete windowLabel;
    }
    // Force a redraw?
    this->show();
}
