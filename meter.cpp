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

    currentColor.setNamedColor("#148CD2");
    currentColor = currentColor.darker();

    peakColor.setNamedColor("#3CA0DB");
    peakColor = peakColor.lighter();

    averageColor.setNamedColor("#3FB7CD");

    lowTextColor.setNamedColor("#eff0f1");
    lowLineColor = lowTextColor;

}


void meter::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    // This next line sets up a canvis within the
    // space of the widget, and gives it corrdinates.
    // The end effect, is that the drawing functions will all
    // scale to the window size.

    painter.setWindow(QRect(0, 0, 255+mstart, 50));
    drawScale(&painter);

    // Current:
    painter.setPen(currentColor);
    painter.setBrush(currentColor);
    painter.drawRect(mstart,mheight,current,mstart);

    // Average:
    painter.setPen(averageColor);
    painter.setBrush(averageColor);
    painter.drawRect(mstart+average-1,mheight,1,mstart);

    // Peak:
    painter.setPen(peakColor);
    painter.setBrush(peakColor);
    if(peak > 120)
    {
        // 120 = +S9
        painter.setBrush(Qt::red);
        painter.setPen(Qt::red);
    }

    painter.drawRect(mstart+peak-1,mheight,2,mstart);

}

void meter::setLevels(int current, int peak, int average)
{
    this->current = current;
    this->peak = peak;
    this->average = average;
    this->update();
}

void meter::updateDrawing(int num)
{
    fontSize = num;
    length = num;
}


void meter::drawScale(QPainter *qp)
{
    qp->setPen(lowTextColor);
    qp->setFont(QFont("Arial", fontSize));
    int i=mstart;
    // 13.3 DN per s-unit:
    int s=0;
    for(; i<mstart+120; i+=13)
    {
        qp->drawText(i,mstart, QString("%1").arg(s++) );
    }

    // 2 DN per 1 dB now:
    // 20 DN per 10 dB
    // 40 DN per 20 dB

    // Modify current scale position:
    s = 20;
    i+=20;

    qp->setPen(Qt::red);

    for(; i<mstart+255; i+=40)
    {
        qp->drawText(i,mstart, QString("+%1").arg(s) );
        s = s + 20;
    }

    qp->setPen(lowLineColor);

    qp->drawLine(mstart,12,130,12);
    qp->setPen(Qt::red);
    qp->drawLine(130,12,255,12);

}
