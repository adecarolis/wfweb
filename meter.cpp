#include "meter.h"
#include <QDebug>

meter::meter(QWidget *parent) : QWidget(parent)
{
    //QPainter painter(this);

    // Colors from qdarkstylesheet:
    //    $COLOR_BACKGROUND_LIGHT: #505F69;
    //    $COLOR_BACKGROUND_NORMAL: #32414B;
    //    $COLOR_BACKGROUND_DARK: #19232D;
    //    $COLOR_FOREGROUND_LIGHT: #F0F0F0; // grey
    //    $COLOR_FOREGROUND_NORMAL: #AAAAAA; // grey
    //    $COLOR_FOREGROUND_DARK: #787878; // grey
    //    $COLOR_SELECTION_LIGHT: #148CD2;
    //    $COLOR_SELECTION_NORMAL: #1464A0;
    //    $COLOR_SELECTION_DARK: #14506E;


    // Colors I found that I liked from VFD images:
    //      3FB7CD
    //      3CA0DB
    //
    // Text in qdarkstylesheet seems to be #EFF0F1

    if(drawLabels)
    {
        mXstart = 32;
    } else {
        mXstart = 0;
    }

    meterType = meterNone;

    currentColor = colorFromString("#148CD2");
    currentColor = currentColor.darker();

    peakColor = colorFromString("#3CA0DB");
    peakColor = peakColor.lighter();

    averageColor = colorFromString("#3FB7CD");

    lowTextColor = colorFromString("#eff0f1");
    lowLineColor = lowTextColor;

    avgLevels.resize(averageBalisticLength, 0);
    peakLevels.resize(peakBalisticLength, 0);

    scaleCache = new QImage(QSize(255+mXstart+15, widgetWindowHeight), QImage::Format_ARGB32);
    scaleCache->fill(Qt::transparent);

    combo = new QComboBox(this);
    combo->addItem("None", meterNone);
    combo->addItem("Sub S-Meter", meterSubS);
    combo->addItem("SWR", meterSWR);
    combo->addItem("ALC", meterALC);
    combo->addItem("Compression", meterComp);
    combo->addItem("Voltage", meterVoltage);
    combo->addItem("Current", meterCurrent);
    combo->addItem("Center", meterCenter);
    combo->addItem("TxRxAudio", meterAudio);
    combo->addItem("RxAudio", meterRxAudio);
    combo->addItem("TxAudio", meterTxMod);

    combo->blockSignals(false);
    connect(combo, SIGNAL(activated(int)), this, SLOT(acceptComboItem(int)));
    //connect(combo, SIGNAL(currentIndexChanged(int)), this, SLOT(acceptComboItem(int)));

    this->setToolTip("");
    combo->hide();
    this->installEventFilter(this);
}

void meter::setCompReverse(bool reverse) {
    this->reverseCompMeter = reverse;
    recentlyChangedParameters = true; // force scale redraw
}

void meter::setUseGradients(bool useGrads) {
    this->useGradients = useGrads;
}

void meter::setColors(QColor current, QColor peakScale, QColor peakLevel,
                      QColor average, QColor lowLine,
                      QColor lowText)
{
    currentColor = current;

    peakColor = peakLevel; // color for the peak level indicator
    highLineColor = peakScale; // color for the red side of the scale
    highTextColor = peakScale; // color for the red side of the scale's text

    averageColor = average;

    midScaleColor = QColor(Qt::yellow);
    centerTuningColor = QColor(Qt::green);

    lowLineColor = lowLine;
    lowTextColor = lowText;
    this->update();
}

void meter::clearMeterOnPTTtoggle()
{
    // When a meter changes type, such as the fixed S -- TxPo meter,
    // there is automatic clearing. However, some meters do not switch on their own,
    // and thus we are providing this clearing method. We are careful
    // not to clear meters that don't make sense to clear (such as Vd and Id)


    if( (meterType == meterALC) || (meterType == meterSWR)
            || (meterType == meterComp) || (meterType == meterTxMod)
            || (meterType == meterCenter ))
    {
        clearMeter();
    }
}

void meter::clearMeter()
{
    current = 0;
    average = 0;
    peak = 0;

    avgLevels.clear();
    peakLevels.clear();
    avgLevels.resize(averageBalisticLength, 0);
    peakLevels.resize(peakBalisticLength, 0);

    peakPosition = 0;
    avgPosition = 0;
    // re-draw scale:
    update();
}

void meter::setMeterType(meter_t m_type_req)
{
    if(m_type_req == meterType)
        return;


    this->setToolTip("Double-click to select meter type.");

    combo->blockSignals(true);
    combo->clear();
    switch (m_type_req)
    {
    case meterS:
    case meterPower:
    case meterdBm:
    case meterdBu:
    case meterdBuEMF:
        combo->addItem("Main S-Meter", meterS);
        combo->addItem("dBu Meter", meterdBu);
        combo->addItem("dBu EMF", meterdBuEMF);
        combo->addItem("dBm Meter", meterdBm);
        break;
    default:
        combo->addItem("None", meterNone);
        combo->addItem("Sub S-Meter", meterSubS);
        combo->addItem("SWR", meterSWR);
        combo->addItem("ALC", meterALC);
        combo->addItem("Compression", meterComp);
        combo->addItem("Voltage", meterVoltage);
        combo->addItem("Current", meterCurrent);
        combo->addItem("Center", meterCenter);
        combo->addItem("TxRxAudio", meterAudio);
        combo->addItem("RxAudio", meterRxAudio);
        combo->addItem("TxAudio", meterTxMod);
        break;
    }

    int m_index = combo->findData(m_type_req);
    combo->setCurrentIndex(m_index);
    combo->blockSignals(false);

    meterType = m_type_req;
    this->meterShortString = getMeterDebug(meterType);
    recentlyChangedParameters = true;
    // clear average and peak vectors:
    this->clearMeter();
}

void meter::setMeterExtremities(double min, double max, double redline) {
    this->scaleMin = min;
    this->scaleMax = max;
    this->scaleRedline = redline;
    recentlyChangedParameters = true;
    haveExtremities = true;
    qDebug() << "Meter extremities set: shortString: " << meterShortString
             << ", min: " << min << ", max: " << max
             << ", redline: " << redline << "meter: " << meterType;
}

void meter::blockMeterType(meter_t mtype) {
    enableAllComboBoxItems(combo);
    //qDebug() << "Asked to block meter of type: " << getMeterDebug(mtype);
    if(mtype != meterNone) {
        int m_index = combo->findData(mtype);
        setComboBoxItemEnabled(combo, m_index, false);
    }
}

meter_t meter::getMeterType()
{
    return meterType;
}

void meter::setMeterShortString(QString s)
{
    meterShortString = s;
}

QString meter::getMeterShortString()
{
    return meterShortString;
}

void meter::acceptComboItem(int item) {
    qDebug() << "Meter selected combo item number: " << item;

    meter_t meterTypeRequested = static_cast<meter_t>(combo->itemData(item).toInt());
    if(meterType != meterTypeRequested) {
        qDebug() << "Changing meter to type: " << getMeterDebug(meterTypeRequested) << ", with item index: " << item;
        emit configureMeterSignal(meterTypeRequested);
    }

    combo->hide();
    freezeDrawing = false;
}

void meter::handleDoubleClick() {
    freezeDrawing = true;
    combo->show();
}

bool meter::eventFilter(QObject *object, QEvent *event) {

    if( (freezeDrawing) && (event->type() == QEvent::MouseButtonPress) ) {
        combo->hide();
        freezeDrawing = false;
        return true;
    }

    if( (freezeDrawing) && (event->type() == QEvent::MouseButtonDblClick) ) {
        combo->hide();
        freezeDrawing = false;
        return true;
    }

    if(event->type() == QEvent::MouseButtonDblClick) {
        //if( !(meterType == meterS || meterType == meterPower)) {
        handleDoubleClick();
        //}
        return true;
    }

    (void)object;
    return false;
}

void meter::paintEvent(QPaintEvent *)
{
    if(freezeDrawing)
        return;

    if(!haveExtremities) {
        qWarning() << "Cannot draw meter without scale data!";
        return;
    }

    QPainter painter(this);
    // This next line sets up a canvis within the
    // space of the widget, and gives it coordinates.
    // The end effect, is that the drawing functions will all
    // scale to the window size.
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setFont(QFont(this->fontInfo().family(), fontSize));
    widgetWindowHeight = this->height();
    painter.setWindow(QRect(0, 0, 255+mXstart+15, widgetWindowHeight));
    barHeight = widgetWindowHeight / 2;

    // We regenerate the scale graphics if:
    // 1. The meter type changed, or,
    // 2. The meter extremities changed

    if( (lastDrawMeterType != meterType)
            || (recentlyChangedParameters)) {
        regenerateScale(&painter);
    }

    if(scaleReady) {
        recallScale(&painter);
    }

    // Guard against drawing empty or garbage data:
    if(haveReceivedSomeData) {
        painter.setPen(currentColor);
        painter.setBrush(currentColor);

        if(meterType == meterCenter)
        {
            drawValue_Center(&painter);
        } else if ( (meterType == meterAudio) ||
                    (meterType == meterTxMod) ||
                    (meterType == meterRxAudio))
        {
            drawValue_Log(&painter);
        } else {
            if(meterType==meterComp)
                drawValue_Linear(&painter, this->reverseCompMeter);
            else
                drawValue_Linear(&painter, false);
        }
        haveUpdatedData = false;
    }
}

void meter::regenerateScale(QPainter *screenPainterHints) {
    // draw a scale and save to scaleCache

    QSize sizeHint = screenPainterHints->window().size();

    if(sizeHint != scaleCache->size()) {
        qDebug() << "Meter Widget painter window does not match cache image size.";
        qDebug() << "Meter Scale cache image size: " << scaleCache->size();
        qDebug() << "Meter Widget size: " << this->size();
        qDebug() << "Meter painter window: " << sizeHint;
        delete scaleCache;
        scaleCache = new QImage(sizeHint, QImage::Format_ARGB32);
    }
#ifdef QT_DEBUG
    QString fontName = screenPainterHints->font().family();
    int fontSize = screenPainterHints->font().pointSize();
    qDebug() << "screenPainterHints: Font: " << fontName << ", size: " << fontSize;
#endif


    scaleCache->fill(Qt::transparent);
    QPainter painter(scaleCache);
    painter.setRenderHints(screenPainterHints->renderHints()); // Copy render hints
    //painter.setRenderHint(QPainter::Antialiasing); // fuzz city
    //painter.setRenderHint(QPainter::HighQualityAntialiasing); // depreciated
    //painter.setRenderHint(QPainter::TextAntialiasing);
    //painter.setRenderHint(QPainter::SmoothPixmapTransform);

    painter.setCompositionMode(QPainter::CompositionMode_Source); // Important for correct alpha blending

    painter.setFont(QFont(this->fontInfo().family(), fontSize));
    painter.fillRect(rect(), Qt::transparent); // Clear the image before redrawing

#ifdef QT_DEBUG
    QString fontNameP = painter.font().family();
    int fontSizeP = painter.font().pointSize();
    qDebug() << "cache painter: Font: " << fontNameP << ", size: " << fontSizeP
             << "infoFamily: " << painter.fontInfo().family();
#endif

    QFontMetrics fm = painter.fontMetrics();

    switch(meterType)
    {
    case meterS:
        label = "S";
        labelWidth = fm.boundingRect(label).width();
        drawScaleS(&painter);
        break;
    case meterSubS:
        label = "Sub";
        labelWidth = fm.boundingRect(label).width();
        drawScaleS(&painter);
        break;
    case meterPower:
        label = "PWR";
        labelWidth = fm.boundingRect(label).width();
        drawScalePo(&painter);
        break;
    case meterALC:
        label = "ALC";
        labelWidth = fm.boundingRect(label).width();
        drawScaleALC(&painter);
        break;
    case meterSWR:
        label = "SWR";
        labelWidth = fm.boundingRect(label).width();
        drawScaleSWR(&painter);
        break;
    case meterCenter:
        label = "CTR";
        labelWidth = fm.boundingRect(label).width();
        drawScaleCenter(&painter);
        break;
    case meterVoltage:
        label = "Vd";
        labelWidth = fm.boundingRect(label).width();
        drawScaleVd(&painter);
        break;
    case meterCurrent:
        label = "Id";
        labelWidth = fm.boundingRect(label).width();
        drawScaleId(&painter);
        break;
    case meterComp:
        label = "CMP(dB)";
        labelWidth = fm.boundingRect(label).width();
        if(reverseCompMeter) {
            drawScaleCompInverted(&painter);
        } else {
            drawScaleComp(&painter);
        }
        break;
    case meterNone:
        label = tr("Double-click to set meter");
        labelWidth = fm.boundingRect(label).width();
        drawLabel(&painter);
        return;
        break;
    case meterAudio:
        label = "dBfs";
        peakRedLevel = 241;
        labelWidth = fm.boundingRect(label).width();
        drawScale_dBFs(&painter);
        break;
    case meterRxAudio:
        label = "Rx(dBfs)";
        peakRedLevel = 241;
        labelWidth = fm.boundingRect(label).width();
        drawScale_dBFs(&painter);
        break;
    case meterTxMod:
        label = "Tx(dBfs)";
        labelWidth = fm.boundingRect(label).width();
        peakRedLevel = 241;
        drawScale_dBFs(&painter);
        break;
    case meterdBu:
        label = "dBu";
        labelWidth = fm.boundingRect(label).width();
        peakRedLevel = 241;
        drawScaledBPositive(&painter);
        break;
    case meterdBuEMF:
        label = "dBu(EMF)";
        labelWidth = fm.boundingRect(label).width();
        peakRedLevel = 241;
        drawScaledBPositive(&painter);
        break;
    case meterdBm:
        label = "dBm";
        labelWidth = fm.boundingRect(label).width();
        peakRedLevel = 241;
        drawScaledBNegative(&painter);
        break;
    default:
        label = "DN";
        labelWidth = fm.boundingRect(label).width();
        peakRedLevel = 241;
        drawScaleRaw(&painter);
        break;
    }
    if(drawLabels)
    {
        // We are tracking the drawLabels parameter,
        // because we may some day want to squeeze this widget
        // into a place where the label cannot fit.

        drawLabel(&painter);
    }

    painter.end();
    recentlyChangedParameters = false;
    lastDrawMeterType = meterType;
    scaleReady = true;
}

void meter::recallScale(QPainter *painter) {
    // read the scaleCache onto the canvas
    //painter->drawImage(0, 0, *scaleCache);
    // This should prevent image scaling, which makest the text less clear:
    painter->drawImage(QPoint(0,0), *scaleCache, scaleCache->rect());
}

void meter::drawLabel(QPainter *qp)
{
    qp->setPen(lowTextColor);
    qp->drawText(0,scaleTextYstart, label );
}

void meter::scaleLinearNumbersForDrawing() {
    // input:  current, average, peak (double)
    // output: currentRect, averageRect, peakRect (int)

    currentRect = getPixelScaleFromValue(current);
    averageRect = getPixelScaleFromValue(average);
    peakRect =    getPixelScaleFromValue(peak);
}

double meter::getValueFromPixelScale(int p) {
    // Give a value 0-255 and receive a scaled value in return
    return (p*(scaleMax-scaleMin)/255) + scaleMin;
}

int meter::getPixelScaleFromValue(double v) {
    // give a value within the scale and receive a pixel scale value (0-255)
    return (v-scaleMin) * (255/(scaleMax-scaleMin));
}

int meter::nearestStep(double d, int stepSize) {
    // This function was only tested with negative numbers,
    // and produces the rounding edge at the correct spot for
    // our dBm (R8600) scale.
    bool flipped = false;
    if(d < 0) {
        d *= -1;
        flipped = true;
    }

    int n = round(d/stepSize);
    // 99 becomes 9.9 becomes 10
    // 95 becomes 9.5 becomes 10
    // 94 becomes 9.4 becomes  9

    if(flipped)
        n*=-stepSize;
    else
        n *= stepSize;

    return n;
}

void meter::scaleLogNumbersForDrawing() {
    currentRect = (int)((1-audiopot[255-(int)current])*255);
    averageRect = (int)((1-audiopot[255-(int)average])*255);
    peakRect = (int)((1-audiopot[255-(int)peak])*255);
}

void meter::drawValue_Linear(QPainter *qp, bool reverse) {
    // Draw a rectangle.

    // The parameters current, peak, and average need to be scaled
    // to 0-255.

    // Data input:  scaleMin ---- scaleMax
    // Data output:     0    ----    255
    scaleLinearNumbersForDrawing();

    if(currentRect < 0) {
        return;
    }

    if (this->meterType == meterdBuEMF || this->meterType == meterdBm || this->meterType == meterdBu)
    {
        qp->setPen(lowTextColor);
        uchar prec=1;
        if (current >= 100.0 || current <= -100.0)
            prec=0;
        qp->drawText(0,scaleTextYstart+20, QString("%0").arg(current,0,'f',prec,'0'));

    }
    // And then, we can just plot them, since we already scaled
    // the scales, right?
    if(useGradients) {
        QLinearGradient grad(QPointF(0, 0), QPointF(255, 0));
        if(reverse) {
            grad.setColorAt(1, currentColor.darker().darker());
            grad.setColorAt(0, currentColor.lighter());
        } else {
            grad.setColorAt(0, currentColor.darker().darker());
            grad.setColorAt(1, currentColor.lighter());
        }
        qp->setBrush(grad);
    } else {
        qp->setBrush(currentColor);
    }
    qp->setPen(currentColor);
    if(reverse) {
        qp->drawRect(255+mXstart,mYstart,-currentRect,barHeight);
    } else {
        qp->drawRect(mXstart,mYstart,currentRect,barHeight);
    }

    // Average:
    qp->setPen(averageColor);
    qp->setBrush(averageColor);
    if(reverse) {
        qp->drawRect(255+mXstart-averageRect,mYstart,-1,barHeight); // bar is 1 pixel wide, height = meter start?
    } else {
        qp->drawRect(mXstart+averageRect-1,mYstart,1,barHeight); // bar is 1 pixel wide, height = meter start?
    }

    // Peak:
    qp->setPen(peakColor);
    qp->setBrush(peakColor);
    if(peak > peakRedLevel)
    {
        qp->setBrush(Qt::red);
        qp->setPen(Qt::red);
    }
    if(reverse) {
        qp->drawRect(255+mXstart-peakRect+1,mYstart,-1,barHeight);
    } else {
        qp->drawRect(mXstart+peakRect-1,mYstart,2,barHeight);
    }
}

void meter::drawValue_Center(QPainter *qp) {
    // Draw a center-referenced rectangle

    // First, scale the data for 0-255:
    // Not exactly needed for this since only icom data uses this function currently and that data are already 0-255
    // But we are going to call it anyway to prove that our code is good:
    scaleLinearNumbersForDrawing();

    // Current value:
    // starting at the center (mXstart+128) and offset by (current-128)
    if(useGradients) {
        QLinearGradient grad(QPointF(0, 0), QPointF(255, 0));
        grad.setColorAt(0, currentColor.darker().darker());
        grad.setColorAt(0.5, currentColor.lighter());
        grad.setColorAt(1, currentColor.darker().darker());
        qp->setBrush(grad);
    } else {
        qp->setBrush(currentColor);
    }

    qp->setPen(currentColor);
    qp->drawRect(mXstart+128,mYstart,currentRect-128,barHeight);

    // Average:
    qp->setPen(averageColor);
    qp->setBrush(averageColor);
    qp->drawRect(mXstart+averageRect-1,mYstart,1,barHeight); // bar is 1 pixel wide, height = meter start?

    // Peak: (what does peak mean for off-center deviation?)
    qp->setPen(peakColor);
    qp->setBrush(peakColor);
    if((peak > 191) || (peak < 63))
    {
        qp->setBrush(Qt::red);
        qp->setPen(Qt::red);
    }

    qp->drawRect(mXstart+peakRect-1,mYstart,1,barHeight);
}

void meter::drawValue_Log(QPainter *qp) {
    // Log scale but still 0-255:
    scaleLogNumbersForDrawing();

    // Current value:
    // X, Y, Width, Height
    if(useGradients) {
        QLinearGradient grad(QPointF(0, 0), QPointF(255, 0));
        grad.setColorAt(0, currentColor.darker().darker());
        grad.setColorAt(1, currentColor.lighter());

        qp->setBrush(grad);
    } else {
        qp->setBrush(currentColor);
    }
    qp->setPen(currentColor);

    qp->drawRect(mXstart,mYstart,currentRect,barHeight);

    // Average:
    qp->setPen(averageColor);
    qp->setBrush(averageColor);
    qp->drawRect(mXstart+averageRect-1,mYstart,1,barHeight); // bar is 1 pixel wide, height = meter start?

    // Peak:
    qp->setPen(peakColor);
    qp->setBrush(peakColor);
    if(peak > peakRedLevel)
    {
        qp->setBrush(Qt::red);
        qp->setPen(Qt::red);
    }

    qp->drawRect(mXstart+peakRect-1,mYstart,2,barHeight);
}

void meter::setLevel(double current)
{
    // "current" means "now", ie, the value at this moment.

    this->current = current;

    avgLevels[(avgPosition++)%averageBalisticLength] = current;
    peakLevels[(peakPosition++)%peakBalisticLength] = current;

    double sum=0.0;

    for(unsigned int i=0; i < (unsigned int)std::min(avgPosition, (int)avgLevels.size()); i++)
    {
        sum += avgLevels[i];
    }

    // This never divides by zero because the above section of this code
    // inserts data, thus assuring that we will never have a zero for both
    // the position and the size.
    this->average = sum / std::min(avgPosition, (int)avgLevels.size());

    this->peak = (-1)*UINT16_MAX;

    for(unsigned int i=0; i < peakLevels.size(); i++)
    {
        if( peakLevels[i] >  this->peak)
            this->peak = peakLevels[i];
    }

    haveUpdatedData = true;
    haveReceivedSomeData = true;

    if(meterType==meterS) {
        // This is just so I can catch it in the debugger
        sum++;
        volatile int ooo = 0;
        (void)ooo;
    }

    this->update();
}

void meter::setLevels(double current, double peak)
{
    this->current = current;
    this->peak = peak;

    avgLevels[(avgPosition++)%averageBalisticLength] = current;
    double sum=0;
    for(unsigned int i=0; i < (unsigned int)std::min(avgPosition, (int)avgLevels.size()); i++)
    {
        sum += avgLevels.at(i);
    }
    this->average = sum / std::min(avgPosition, (int)avgLevels.size());

    haveUpdatedData = true;
    haveReceivedSomeData = true;
    this->update(); // place repaint event on the event queue
}

void meter::setLevels(double current, double peak, double average)
{
    this->current = current;
    this->peak = peak;
    this->average = average;

    haveUpdatedData = true;
    haveReceivedSomeData = true;
    this->update(); // place repaint event on the event queue
}

void meter::updateDrawing(int num)
{
    fontSize = num;
    length = num;
}

// The drawScale functions draw the numbers and number unerline for each type of meter

void meter::drawScaleRaw(QPainter *qp)
{
    qp->setPen(lowTextColor);
    //qp->setFont(QFont("Arial", fontSize));
    int i=mXstart;
    for(; i<mXstart+256; i+=25)
    {
        qp->drawText(i,scaleTextYstart, QString("%1").arg(i) );
    }

    // Now the lines:
    qp->setPen(lowLineColor);

    // Line: X1, Y1 -->to--> X2, Y2
    qp->drawLine(mXstart,scaleLineYstart,255/2+mXstart,scaleLineYstart);
    qp->setPen(highLineColor);
    qp->drawLine(255/2+mXstart,scaleLineYstart,255+mXstart,scaleLineYstart);
}

void meter::drawScaledBPositive(QPainter *qp)
{
    // dB scale which is mostly (or all) positive numbers
    // used for dBu EMF and dBu



    int redLevelRect = getPixelScaleFromValue(scaleRedline);

    qp->setPen(lowTextColor);

    int scaleSpan = scaleMax - scaleMin;
    scaleSpan = (scaleSpan/10) * 10;
    int stepDelta = 0;
    if(scaleSpan < 100) {
        stepDelta = 10;
    } else {
        stepDelta = 20;
    }

    QFontMetrics fm = qp->fontMetrics();
    QRect textRect = fm.boundingRect(QString::number(scaleMax));
    int textMaxLength = textRect.width();
    int scaleCacheWidth = scaleCache->size().width();

    int db = 0;
    int dbPrior = -10;
    double val = 0;

    QString formattedString;
    for(int p=mXstart; p < mXstart+255; p++) {
        val = getValueFromPixelScale(p-mXstart);
        db = ((int)round(val)/stepDelta) * stepDelta;
        //db = ((int)(round(val/10.0))*10 / stepDelta) * stepDelta; // round to nearest step

        if(db != dbPrior) {
            if(db > scaleRedline) {
                qp->setPen(highTextColor);
            } else {
                qp->setPen(lowTextColor);
            }
            formattedString = QString::number(db);
//            qDebug() << "p: " << p << "val: " << val
//                     << "dB: " << db << "dBprior: " << dbPrior;
            if( (p + textMaxLength < scaleCacheWidth)
                && (p > labelWidth ) ){
                qp->drawText(p,scaleTextYstart, formattedString );
            }
            qp->drawLine(p,scaleTextYstart, p, scaleTextYstart+5);
            dbPrior = db;
        }
    }

    // Now the lines:
    qp->setPen(lowLineColor);

    // Line: X1, Y1 -->to--> X2, Y2
    qp->drawLine(mXstart,scaleLineYstart,redLevelRect+mXstart,scaleLineYstart);
    qp->setPen(highLineColor);
    qp->drawLine(redLevelRect+mXstart,scaleLineYstart,255+mXstart,scaleLineYstart);

}

void meter::drawScaledBNegative(QPainter *qp)
{
    // dB scale which is mostly (or all) negative numbers
    // used for dBm

    int redLevelRect = getPixelScaleFromValue(scaleRedline);
    QFontMetrics fm = qp->fontMetrics();
    QRect textRect = fm.boundingRect(QString::number(-100));
    int textMaxLength = textRect.width();
    int scaleCacheWidth = scaleCache->size().width();

    qp->setPen(lowTextColor);

    int scaleSpan = scaleMax - scaleMin;
    scaleSpan = (scaleSpan*10) / 10;
    int stepDelta = 0;
    if(scaleSpan < 100) {
        stepDelta = 10;
    } else {
        stepDelta = 20;
    }

    int db = 0;
    int dbPrior = -10;
    double val = 0;

    QString formattedString;
    for(int p=mXstart; p < mXstart+255; p++) {
        val = getValueFromPixelScale(p-mXstart);
        db = nearestStep(val, stepDelta);

        if( (db != dbPrior) && ((int)round(val)%10==0)) {
            if(db > scaleRedline) {
                qp->setPen(highTextColor);
            } else {
                qp->setPen(lowTextColor);
            }
            formattedString = QString::number(db);
    //            qDebug() << "p: " << p << "val: " << val
    //                     << "dB: " << db << "dBprior: " << dbPrior;
            if ( (p + textMaxLength < scaleCacheWidth)
            && (p > labelWidth) ){
                qp->drawText(p,scaleTextYstart, formattedString );
            }
            qp->drawLine(p,scaleTextYstart, p, scaleTextYstart+5);
            dbPrior = db;
        }
    }

    // Now the lines:
    qp->setPen(lowLineColor);

    // Line: X1, Y1 -->to--> X2, Y2
    qp->drawLine(mXstart,scaleLineYstart,redLevelRect+mXstart,scaleLineYstart);
    qp->setPen(highLineColor);
    qp->drawLine(redLevelRect+mXstart,scaleLineYstart,255+mXstart,scaleLineYstart);

}

void meter::drawScale_dBFs(QPainter *qp)
{
    qp->setPen(lowTextColor);
    peakRedLevel = 193;

    if(meterType==meterAudio)
        qp->drawText(20+mXstart,scaleTextYstart, QString("-30"));
    qp->drawText(38+mXstart+2,scaleTextYstart, QString("-24"));
    qp->drawText(71+mXstart,scaleTextYstart, QString("-18"));
    qp->drawText(124+mXstart,scaleTextYstart, QString("-12"));
    qp->drawText(193+mXstart,scaleTextYstart, QString("-6"));
    qp->drawText(255+mXstart,scaleTextYstart, QString("0"));

    // Low ticks:
    qp->setPen(lowLineColor);
    qp->drawLine(20+mXstart,scaleTextYstart, 20+mXstart, scaleTextYstart+5);
    qp->drawLine(38+mXstart,scaleTextYstart, 38+mXstart, scaleTextYstart+5);
    qp->drawLine(71+mXstart,scaleTextYstart, 71+mXstart, scaleTextYstart+5);
    qp->drawLine(124+mXstart,scaleTextYstart, 124+mXstart, scaleTextYstart+5);


    // High ticks:
    qp->setPen(highLineColor);
    qp->drawLine(193+mXstart,scaleTextYstart, 193+mXstart, scaleTextYstart+5);
    qp->drawLine(255+mXstart,scaleTextYstart, 255+mXstart, scaleTextYstart+5);

    // Now the lines:
    qp->setPen(lowLineColor);

    // Line: X1, Y1 -->to--> X2, Y2
    qp->drawLine(mXstart,scaleLineYstart,peakRedLevel+mXstart,scaleLineYstart);
    qp->setPen(highLineColor);
    qp->drawLine(peakRedLevel+mXstart,scaleLineYstart,255+mXstart,scaleLineYstart);
}

void meter::drawScaleVd(QPainter *qp)
{
    int redLevelRect = getPixelScaleFromValue(scaleRedline);

    qp->setPen(lowTextColor);

    int scaleSpan = scaleMax - scaleMin;
    scaleSpan = (scaleSpan*10) / 10;
    int stepDelta = 0;
    if(scaleSpan < 20) {
        stepDelta = 1;
    } else {
        stepDelta = 2;
    }

    int volt = int(scaleMin);
    int voltPrior = int(scaleMin); // causes the zero to be skipped
    double val = 0;

    QString formattedString;
    for(int p=mXstart; p < mXstart+255; p++) {
        val = getValueFromPixelScale(p-mXstart);
        volt = ((int)val / stepDelta) * stepDelta; // round to nearest step

        if(volt != voltPrior) {
            //qDebug() << "val: " << val << ", power: " << power << ", powerPrior: " << powerPrior << " scaleSpan:" << scaleSpan << "stepDelta: " << stepDelta;
            if(volt > scaleRedline) {
                qp->setPen(highTextColor);
            } else {
                qp->setPen(lowTextColor);
            }
            formattedString = QString::number(volt);
            qp->drawText(p,scaleTextYstart, formattedString );
            qp->drawLine(p,scaleTextYstart, p, scaleTextYstart+5);
            voltPrior = volt;

        }
    }

    // Now the lines:
    qp->setPen(lowLineColor);

    // Line: X1, Y1 -->to--> X2, Y2
    qp->drawLine(mXstart,scaleLineYstart,redLevelRect+mXstart,scaleLineYstart);
    qp->setPen(highLineColor);
    qp->drawLine(redLevelRect+mXstart,scaleLineYstart,255+mXstart,scaleLineYstart);
}

void meter::drawScaleCenter(QPainter *qp)
{
    // No known units
    qp->setPen(lowLineColor);
    qp->drawText(60+mXstart,scaleTextYstart, QString("-"));

    qp->setPen(centerTuningColor);
    // Attempt to draw the zero at the actual center
    qp->drawText(128-2+mXstart,scaleTextYstart, QString("0"));

    qp->setPen(lowLineColor);
    qp->drawText(195+mXstart,scaleTextYstart, QString("+"));

    qp->setPen(lowLineColor);
    qp->drawLine(mXstart,scaleLineYstart,128-32+mXstart,scaleLineYstart);

    qp->setPen(centerTuningColor);
    qp->drawLine(128-32+mXstart,scaleLineYstart,128+32+mXstart,scaleLineYstart);

    qp->setPen(lowLineColor);
    qp->drawLine(128+32+mXstart,scaleLineYstart,255+mXstart,scaleLineYstart);
}


void meter::drawScalePo(QPainter *qp)
{
    int redLevelRect = getPixelScaleFromValue(scaleRedline);

    qp->setPen(lowTextColor);

    int scaleSpan = scaleMax - scaleMin;
    scaleSpan = (scaleSpan*10) / 10;
    int stepDelta = 0;
    if(scaleSpan < 20) {
        // Low power radio, show each 1 watt on the scale
        stepDelta = 1;
    } else {
        // High power radio, show every 10 watts on the scale.
        stepDelta = 20;
    }

    int power = 0;
    int powerPrior = 0; // causes the zero to be skipped
    double val = 0;

    QString formattedString;
    for(int p=mXstart; p < mXstart+255; p++) {
        val = getValueFromPixelScale(p-mXstart);
        power = ((int)val / stepDelta) * stepDelta; // round to nearest step


        if(power != powerPrior) {
            //qDebug() << "val: " << val << ", power: " << power << ", powerPrior: " << powerPrior << " scaleSpan:" << scaleSpan << "stepDelta: " << stepDelta;
            if(power > scaleRedline) {
                qp->setPen(highTextColor);
            } else {
                qp->setPen(lowTextColor);
            }
            formattedString = QString::number(power);
            qp->drawText(p,scaleTextYstart, formattedString );
            qp->drawLine(p,scaleTextYstart, p, scaleTextYstart+5);
            powerPrior = power;

        }
    }

    // Now the lines:
    qp->setPen(lowLineColor);

    // Line: X1, Y1 -->to--> X2, Y2
    qp->drawLine(mXstart,scaleLineYstart,redLevelRect+mXstart,scaleLineYstart);
    qp->setPen(highLineColor);
    qp->drawLine(redLevelRect+mXstart,scaleLineYstart,255+mXstart,scaleLineYstart);
}

void meter::drawScaleRxdB(QPainter *qp)
{
    // TODO: remove
    (void)qp;
}

void meter::drawScaleALC(QPainter *qp)
{
    int redLevelRect = getPixelScaleFromValue(scaleRedline);

    qp->setPen(lowTextColor);

    int scaleSpan = scaleMax - scaleMin;
    scaleSpan = (scaleSpan*10) / 10;
    int stepDelta = 25;


    int alc = 0;
    int alcPrior = -1; // causes the zero to be skipped
    double val = 0;

    QString formattedString;
    for(int p=mXstart; p < mXstart+255; p++) {
        val = getValueFromPixelScale(p-mXstart)*100; // values are 0-1-2, with 1 being 100%
        alc = ((int)val / stepDelta) * stepDelta; // round to nearest step


        if(alc != alcPrior) {
            //qDebug() << "val: " << val << ", alc: " << alc << ", alcPrior: " << alcPrior << " scaleSpan:" << scaleSpan << "stepDelta: " << stepDelta;
            if(alc > scaleRedline*99) {
                qp->setPen(highTextColor);
            } else {
                qp->setPen(lowTextColor);
            }
            formattedString = QString::number(alc);
            qp->drawText(p,scaleTextYstart, formattedString );
            qp->drawLine(p,scaleTextYstart, p, scaleTextYstart+5);
            //            qp->drawLine(p,scaleTextYstart, p, scaleTextYstart+5);

            alcPrior = alc;
        }
    }

    // Now the lines:
    qp->setPen(lowLineColor);

    // Line: X1, Y1 -->to--> X2, Y2
    qp->drawLine(mXstart,scaleLineYstart,redLevelRect+mXstart,scaleLineYstart);
    qp->setPen(highLineColor);
    qp->drawLine(redLevelRect+mXstart,scaleLineYstart,255+mXstart,scaleLineYstart);
}

void meter::drawScaleComp(QPainter *qp)
{
    int redLevelRect = getPixelScaleFromValue(scaleRedline);

    qp->setPen(lowTextColor);

    int scaleSpan = scaleMax - scaleMin;
    scaleSpan = (scaleSpan*10) / 10;
    int stepDelta = 0;
    if(scaleSpan <= 30) {
        stepDelta = 3;
    } else {
        stepDelta = 12;
    }

    int comp = 0;
    int compPrior = 0; // causes the zero to be skipped
    double val = 0;

    QString formattedString;
    // Adding stepDelta to the start means we skip the first step.
    for(int p=mXstart; p < mXstart+255; p++) {
        val = getValueFromPixelScale(p-mXstart);
        comp = ((int)val / stepDelta) * stepDelta; // round to nearest step
        if(comp != compPrior) {
            //qDebug() << "val: " << val << ", comp: " << comp << ", compPrior: " << compPrior << " scaleSpan:" << scaleSpan << "stepDelta: " << stepDelta;
            if(compPrior != 0) {
                if(comp > scaleRedline) {
                    qp->setPen(highTextColor);
                } else {
                    qp->setPen(lowTextColor);
                }
                formattedString = QString::number(comp);
                qp->drawText(p,scaleTextYstart, formattedString );
                qp->drawLine(p,scaleTextYstart, p, scaleTextYstart+5);
            }
            compPrior = comp;
        }
    }

    // Now the lines:
    qp->setPen(lowLineColor);

    // Line: X1, Y1 -->to--> X2, Y2
    qp->drawLine(mXstart,scaleLineYstart,redLevelRect+mXstart,scaleLineYstart);
    qp->setPen(highLineColor);
    qp->drawLine(redLevelRect+mXstart,scaleLineYstart,255+mXstart,scaleLineYstart);
}

void meter::drawScaleCompInverted(QPainter *qp) {
    // inverted scale

    int redLevelRect = getPixelScaleFromValue(scaleRedline);

    qp->setPen(lowTextColor);

    int scaleSpan = scaleMax - scaleMin;
    scaleSpan = (scaleSpan*10) / 10;
    int stepDelta = 0;
    if(scaleSpan <= 30) {
        stepDelta = 3;
    } else {
        stepDelta = 12;
    }

    int comp = 0;
    int compPrior = -1; // -1 allows the first value to be drawn
    double val = 0;
    QString formattedString;
    // Adding stepDelta to the start means we skip the first step.
    for(int p=mXstart; p < mXstart+255-64; p++) {
        val = getValueFromPixelScale(p-mXstart);
        comp = ((int)val / stepDelta) * stepDelta; // round to nearest step
        if(comp != compPrior) {
            //qDebug() << "p: " << p << ", val: " << val << ", comp: " << comp << ", compPrior: " << compPrior << " scaleSpan:" << scaleSpan << "stepDelta: " << stepDelta;
            if(comp > scaleRedline-3) {
                qp->setPen(highTextColor);
            } else {
                qp->setPen(lowTextColor);
            }
            formattedString = QString::number(comp);
            qp->drawText(255+mXstart-p,scaleTextYstart, formattedString );
            qp->drawLine(255+mXstart-p,scaleTextYstart, 255+mXstart-p, scaleTextYstart+5);

            compPrior = comp;
        }
    }

    // Now the lines:
    qp->setPen(lowLineColor);

    // Line: X1, Y1 -->to--> X2, Y2
    qp->drawLine(255+mXstart,scaleLineYstart,255-redLevelRect+mXstart,scaleLineYstart);
    qp->setPen(highLineColor);
    qp->drawLine(255-redLevelRect+mXstart,scaleLineYstart,mXstart,scaleLineYstart);
}

void meter::drawScaleSWR(QPainter *qp)
{
    int redLevelRect = getPixelScaleFromValue(scaleRedline);

    int scaleSpan = scaleMax - scaleMin;
    scaleSpan = (scaleSpan*10) / 10;
    bool smallSteps = false;
    // Icom SWR scale goes up to 20:1
    // Kenwood SWR scale goes up to 5:1
    if(scaleSpan <= 6) {
        smallSteps = true;
    }

    // Draw vertical tic marks and scale numbers:
    qp->setPen(lowTextColor);
    int swr = 0;
    int swrPrior = -1;
    double val = 0;
    int fives = 0;
    int tens = 0;
    int decimals = 0;
    QString formattedString;
    for(int p=mXstart; p < mXstart+255; p++) {
        val = getValueFromPixelScale(p-mXstart);
        if(smallSteps) {
            swr = (val * 50)/50;
            swr = swr * 10;
            fives = ((int)(val*10))%5;
            tens = ((int)(val*10))%10;
            if( (fives==0) && (tens!=0)) {
                swr = swr + 5;
                decimals = 1;
            } else {
                decimals = 0;
            }
        } else {
            swr = (val * 50)/50;
            swr *= 10;
        }

        if( (swr != swrPrior) && (swr > swrPrior) ) {
            if( (swr > 50) && (swr - swrPrior < 20) )
                    continue;

            // qDebug() << "SWR meter: p: " << p << ", val: " << val << "swr: " << swr << "swrPrior: " << swrPrior << "fives: " << fives << "tens:" << tens;
            if(val >= scaleRedline) {
                qp->setPen(highTextColor);
            } else {
                qp->setPen(lowTextColor);
            }
            formattedString = QString::number(swr/10.0, 'f', decimals);
            qp->drawText(p,scaleTextYstart, formattedString );
            qp->drawLine(p,scaleTextYstart, p, scaleTextYstart+5);
            swrPrior = swr;
        }
    }

    // Draw horizontal underline:
    qp->setPen(lowLineColor);
    qp->drawLine(mXstart,scaleLineYstart,redLevelRect+mXstart,scaleLineYstart);
    qp->setPen(highLineColor);
    qp->drawLine(redLevelRect+mXstart,scaleLineYstart,255+mXstart,scaleLineYstart);
}

void meter::drawScaleId(QPainter *qp)
{
    int redLevelRect = getPixelScaleFromValue(scaleRedline);

    qp->setPen(lowTextColor);

    int scaleSpan = scaleMax - scaleMin;
    scaleSpan = (scaleSpan*10) / 10;
    int stepDelta = 0;
    if(scaleSpan < 20) {
        stepDelta = 1;
    } else if (scaleSpan < 40 ){
        stepDelta = 2;
    } else {
        stepDelta = 5;
    }

    int current = 0;
    int currentPrior = 0; // causes the zero to be skipped
    double val = 0;

    QString formattedString;
    for(int p=mXstart; p < mXstart+255; p++) {
        val = getValueFromPixelScale(p-mXstart);
        current = ((int)val / stepDelta) * stepDelta; // round to nearest step

        if(current != currentPrior) {
            //qDebug() << "val: " << val << ", power: " << power << ", powerPrior: " << powerPrior << " scaleSpan:" << scaleSpan << "stepDelta: " << stepDelta;
            if(current > scaleRedline) {
                qp->setPen(highTextColor);
            } else {
                qp->setPen(lowTextColor);
            }
            formattedString = QString::number(current);
            qp->drawText(p,scaleTextYstart, formattedString );
            qp->drawLine(p,scaleTextYstart, p, scaleTextYstart+5);
            currentPrior = current;

        }
    }

    // Now the lines:
    qp->setPen(lowLineColor);

    // Line: X1, Y1 -->to--> X2, Y2
    qp->drawLine(mXstart,scaleLineYstart,redLevelRect+mXstart,scaleLineYstart);
    qp->setPen(highLineColor);
    qp->drawLine(redLevelRect+mXstart,scaleLineYstart,255+mXstart,scaleLineYstart);
}

void meter::drawScaleS(QPainter *qp)
{
    // double --> S-Unit:
    // -54 = S0
    // -48 = S1
    // -42 = S2
    // -36 = S3
    // -30 = S4
    // -24 = S5
    // -18 = S6
    // -12 = S4
    // -6 = S8
    // 0 = S9
    // 10 = +10
    // 20 = +20
    // 30 = +30 ... etc
    int redLevelRect = getPixelScaleFromValue(scaleRedline);

    qp->setPen(lowTextColor);

    // We must find the pixel values (0-255) which correspond to those s-units
    // and we are assuming a linear scale because 6dB per s-unit is a constant rate

    int sunit = 0;
    int sunitPrior = -1;
    double val = 0;
    bool plus = false;
    for(int p=mXstart; p < mXstart+255; p++) {
        val = getValueFromPixelScale(p-mXstart);
        // Due to low precision, we have to set this threshold at 1 instead of 0.
        // We also have to be carefuly not to double-write at S9
        if(val <=1) {
            sunit = (val-scaleMin)/6;
        } else if (val >= 10 ){
            sunit = ((int)val/10) * 10; // round to nearest ten
            if(sunit==10) {
                plus = true;
            } else {
                plus = false;
            }
        }

        if(sunit != sunitPrior) {
            if(val >= scaleRedline) {
                qp->setPen(highTextColor);
                //qDebug() << "pval: " << p-mXstart << "val: " << val <<   ", s-unit: " << sunit << ", sunit prior: " << sunitPrior;
            } else {
                qp->setPen(lowTextColor);
            }
            if(plus)
                qp->drawText(p-10,scaleTextYstart, QString("+%1").arg(sunit) );
            else
                qp->drawText(p,scaleTextYstart, QString("%1").arg(sunit) );
            qp->drawLine(p,scaleTextYstart, p, scaleTextYstart+5);
            sunitPrior = sunit;
        }
    }

    // Horizontal lines:
    qp->setPen(lowLineColor);
    qp->drawLine(mXstart,scaleLineYstart,redLevelRect+mXstart,scaleLineYstart);
    qp->setPen(highLineColor);
    qp->drawLine(redLevelRect+mXstart,scaleLineYstart,255+mXstart,scaleLineYstart);
}

void meter::muteSingleComboItem(QComboBox *comboBox, int index) {
    enableAllComboBoxItems(comboBox);
    setComboBoxItemEnabled(comboBox, index, false);
}

void meter::enableAllComboBoxItems(QComboBox *combobox, bool en) {
    for(int i=0; i < combobox->count(); i++) {
        setComboBoxItemEnabled(combobox, i, en);
    }
}

void meter::setComboBoxItemEnabled(QComboBox * comboBox, int index, bool enabled)
{
    auto * model = qobject_cast<QStandardItemModel*>(comboBox->model());
    assert(model);
    if(!model) return;

    auto * item = model->item(index);
    assert(item);
    if(!item) return;
    item->setEnabled(enabled);
}

