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

    meterType = meterS;

    currentColor.setNamedColor("#148CD2");
    currentColor = currentColor.darker();

    peakColor.setNamedColor("#3CA0DB");
    peakColor = peakColor.lighter();

    averageColor.setNamedColor("#3FB7CD");

    lowTextColor.setNamedColor("#eff0f1");
    lowLineColor = lowTextColor;

    avgLevels.resize(averageBalisticLength, 0);
    peakLevels.resize(peakBalisticLength, 0);

}

void meter::setMeterType(meterKind type)
{
    if(type == meterType)
        return;

    meterType = type;
    // clear average and peak vectors:
    avgLevels.clear();
    peakLevels.clear();
    avgLevels.resize(averageBalisticLength, 0);
    peakLevels.resize(peakBalisticLength, 0);

    peakPosition = 0;
    avgPosition = 0;
    // re-draw scale:
}

meterKind meter::getMeterType()
{
    return meterType;
}

void meter::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    // This next line sets up a canvis within the
    // space of the widget, and gives it corrdinates.
    // The end effect, is that the drawing functions will all
    // scale to the window size.

    painter.setWindow(QRect(0, 0, 255+mXstart, widgetWindowHeight));
    switch(meterType)
    {
        case meterS:
            peakRedLevel = 120; // S9+
            drawScaleS(&painter);
            break;
        case meterPower:
            peakRedLevel = 210; // 100%
            drawScalePo(&painter);
            break;
        case meterALC:
            peakRedLevel = 100;
            drawScaleALC(&painter);
            break;
        case meterSWR:
            peakRedLevel = 100; // SWR 2.5
            drawScaleSWR(&painter);
            break;
        case meterCenter:
            peakRedLevel = 256; // No need for red here
            drawScaleCenter(&painter);
            break;
        default:
            peakRedLevel = 200;
            drawScaleRaw(&painter);
            break;
    }

    // Current: the most-current value.
    // Draws a bar from start to value.
    painter.setPen(currentColor);
    painter.setBrush(currentColor);

    if(meterType == meterCenter)
    {
        painter.drawRect(mXstart+128,mYstart,current-128,barHeight);

        // Average:
        painter.setPen(averageColor);
        painter.setBrush(averageColor);
        painter.drawRect(mXstart+average-1,mYstart,1,barHeight); // bar is 1 pixel wide, height = meter start?

        // Peak:
        painter.setPen(peakColor);
        painter.setBrush(peakColor);
        if((peak > 191) || (peak < 63))
        {
            painter.setBrush(Qt::red);
            painter.setPen(Qt::red);
        }

        painter.drawRect(mXstart+peak-1,mYstart,1,barHeight);

    } else {

        // X, Y, Width, Height
        painter.drawRect(mXstart,mYstart,current,barHeight);

        // Average:
        painter.setPen(averageColor);
        painter.setBrush(averageColor);
        painter.drawRect(mXstart+average-1,mYstart,1,barHeight); // bar is 1 pixel wide, height = meter start?

        // Peak:
        painter.setPen(peakColor);
        painter.setBrush(peakColor);
        if(peak > peakRedLevel)
        {
            painter.setBrush(Qt::red);
            painter.setPen(Qt::red);
        }

        painter.drawRect(mXstart+peak-1,mYstart,2,barHeight);
    }

}

void meter::setLevels(int current, int peak, int average)
{
    this->current = current;
    this->peak = peak;
    this->average = average;

    avgLevels[(avgPosition++)%averageBalisticLength] = current;
    peakLevels[(peakPosition++)%peakBalisticLength] = current;

    // TODO: only average up to clamp(position, size) that way we don't average in
    // zeros for the first couple of seconds. We might have to not use the accumulate function
    // if we want to specify positions.

    int sum=0;

    for(unsigned int i=0; i < (unsigned int)std::min(avgPosition, (int)avgLevels.size()); i++)
    {
        sum += avgLevels.at(i);
    }
    this->average = sum / std::min(avgPosition, (int)avgLevels.size());

    // this->average = std::accumulate(avgLevels.begin(), std::min(avgLevels.begin() + avgPosition, avgLevels.begin()+avgLevels.size())) / averageBalisticLength;
    // this->peak = std::max_element(peakLevels.begin(), peakLevels.end());

    this->peak = 0;

    for(unsigned int i=0; i < peakLevels.size(); i++)
    {
        if( peakLevels.at(i) >  this->peak)
            this->peak = peakLevels.at(i);
    }

    this->update();
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
    qp->setFont(QFont("Arial", fontSize));
    int i=mXstart;
    for(; i<mXstart+256; i+=20)
    {
        qp->drawText(i,scaleTextYstart, QString("%1").arg(i) );
    }

    // Now the lines:
    qp->setPen(lowLineColor);

    // Line: X1, Y1 -->to--> X2, Y2
    qp->drawLine(mXstart,scaleLineYstart,peakRedLevel+mXstart,scaleLineYstart);
    qp->setPen(Qt::red);
    qp->drawLine(peakRedLevel+mXstart,scaleLineYstart,255+mXstart,scaleLineYstart);

}

void meter::drawScaleCenter(QPainter *qp)
{
    // No known units
    qp->setPen(lowLineColor);
    qp->drawText(60+mXstart,scaleTextYstart, QString("-"));

    qp->setPen(Qt::green);
    // Attempt to draw the zero at the actual center
    qp->drawText(128-2+mXstart,scaleTextYstart, QString("0"));

    qp->setPen(lowLineColor);
    qp->drawText(195+mXstart,scaleTextYstart, QString("+"));


    qp->setPen(lowLineColor);
    qp->drawLine(mXstart,scaleLineYstart,128-32+mXstart,scaleLineYstart);

    qp->setPen(Qt::green);
    qp->drawLine(128-32+mXstart,scaleLineYstart,128+32+mXstart,scaleLineYstart);

    qp->setPen(lowLineColor);
    qp->drawLine(128+32+mXstart,scaleLineYstart,255+mXstart,scaleLineYstart);
}


void meter::drawScalePo(QPainter *qp)
{
    //From the manual: "0000=0% to 0143=50% to 0213=100%"
    float dnPerWatt = 143.0 / 50.0;

    qp->setPen(lowTextColor);
    qp->setFont(QFont("Arial", fontSize));
    int i=mXstart;
    // 13.3 DN per s-unit:
    int p=0;
    for(; i<mXstart+143; i+=(int)(10*dnPerWatt))
    {
        // Stop just before the next 10w spot
        if(i<mXstart+140)
            qp->drawText(i,scaleTextYstart, QString("%1").arg(10*(p++)) );
    }

    // Modify current scale position:

    // Here, P is now 60 watts:
    // Higher scale:
    i = i - (int)(10*dnPerWatt); // back one tick first. Otherwise i starts at 178.
    qDebug() << "meter i: " << i;
    dnPerWatt = (213-143.0) / 50.0; // 1.4 dn per watt

    qp->setPen(Qt::yellow);
    for(i=mXstart+143; i<mXstart+213; i+=(10*dnPerWatt))
    {
        qp->drawText(i,scaleTextYstart, QString("%1").arg(10*(p++)) );
    }

    // Now we're out past 100:
    qp->setPen(Qt::red);

    for(i=mXstart+213; i<mXstart+255; i+=(10*dnPerWatt))
    {
        qp->drawText(i,scaleTextYstart, QString("%1").arg(10*(p++)) );
    }

    // Now the lines:
    qp->setPen(lowLineColor);

    // Line: X1, Y1 -->to--> X2, Y2
    qp->drawLine(mXstart,scaleLineYstart,213+mXstart,scaleLineYstart);
    qp->setPen(Qt::red);
    qp->drawLine(213+mXstart,scaleLineYstart,255+mXstart,scaleLineYstart);

    (void)qp;
}

void meter::drawScaleRxdB(QPainter *qp)
{
    (void)qp;
}

void meter::drawScaleALC(QPainter *qp)
{
    // From the manual: 0000=Minimum to 0120=Maximum

    qp->setPen(lowTextColor);
    qp->setFont(QFont("Arial", fontSize));
    int i=mXstart;
    int alc=0;
    for(; i<mXstart+100; i += (20))
    {
        qp->drawText(i,scaleTextYstart, QString("%1").arg(alc) );
        alc +=20;
    }

    qp->setPen(Qt::red);

    for(; i<mXstart+120; i+=(int)(10*i))
    {
        qp->drawText(i,scaleTextYstart, QString("+%1").arg(alc) );
        alc +=10;
    }

    qp->setPen(lowLineColor);
    qp->drawLine(mXstart,scaleLineYstart,100+mXstart,scaleLineYstart);
    qp->setPen(Qt::red);
    qp->drawLine(100+mXstart,scaleLineYstart,255+mXstart,scaleLineYstart);

    (void)qp;
}

void meter::drawScaleSWR(QPainter *qp)
{
    // From the manual:
    // 0000=SWR1.0,
    // 0048=SWR1.5,
    // 0080=SWR2.0,
    // 0120=SWR3.0

    qp->drawText(mXstart,scaleTextYstart, QString("1.0"));
    qp->drawText(24+mXstart,scaleTextYstart, QString("1.3"));
    qp->drawText(48+mXstart,scaleTextYstart, QString("1.5"));
    qp->drawText(80+mXstart,scaleTextYstart, QString("2.0"));
    qp->drawText(100+mXstart,scaleTextYstart, QString("2.5"));
    qp->drawText(120+mXstart,scaleTextYstart, QString("3.0"));

    qp->setPen(lowLineColor);
    qp->drawLine(mXstart,scaleLineYstart,100+mXstart,scaleLineYstart);
    qp->setPen(Qt::red);
    qp->drawLine(100+mXstart,scaleLineYstart,255+mXstart,scaleLineYstart);

    (void)qp;
}

void meter::drawScaleId(QPainter *qp)
{
    (void)qp;
}

void meter::drawScaleS(QPainter *qp)
{
    qp->setPen(lowTextColor);
    qp->setFont(QFont("Arial", fontSize));
    int i=mXstart;
    // 13.3 DN per s-unit:
    int s=0;
    for(; i<mXstart+120; i+=13)
    {
        qp->drawText(i,mXstart, QString("%1").arg(s++) );
    }

    // 2 DN per 1 dB now:
    // 20 DN per 10 dB
    // 40 DN per 20 dB

    // Modify current scale position:
    s = 20;
    i+=20;

    qp->setPen(Qt::red);

    for(; i<mXstart+255; i+=40)
    {
        qp->drawText(i,mXstart, QString("+%1").arg(s) );
        s = s + 20;
    }

    qp->setPen(lowLineColor);

    qp->drawLine(mXstart,scaleLineYstart,130,scaleLineYstart);
    qp->setPen(Qt::red);
    qp->drawLine(130,scaleLineYstart,255,scaleLineYstart);

}
