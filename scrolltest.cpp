#include "scrolltest.h"

scrolltest::scrolltest(QWidget *parent)
    : QWidget{parent}
{
}


void scrolltest::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    widgetWindowHeight = this->height();

    fontSize = 12;

    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setFont(QFont(this->fontInfo().family(), fontSize));
    painter.setWindow(QRect(0, 0, this->width(), widgetWindowHeight));

    painter.setPen(Qt::red);
    painter.drawText(0,widgetWindowHeight, resultText );
}

void scrolltest::wheelEvent(QWheelEvent *we)
{
    int clicksX = we->angleDelta().x();
    int clicksY = we->angleDelta().y();

    if(abs(clicksX) > maxX)
        maxX = clicksX;

    if(abs(clicksY) > maxY)
        maxY = clicksY;

    if( (clicksX !=0) && (abs(clicksX) < minX))
        minX = abs(clicksX);

    if( (clicksY !=0) && (abs(clicksY) < minY))
        minY = abs(clicksY);

    resultText = QString("X: %1, Y: %2, maxX: %3, maxY: %4")
             .arg(clicksX).arg(clicksY).arg(maxX).arg(maxY);

    if(minX != 999999)
        resultText.append(QString(", minX: %1").arg(minX));

    if(minY != 999999)
        resultText.append(QString(", minY: %1").arg(minY));

    //QFontMetrics fm(this->font());
    //int neededWidth = fm.horizontalAdvance(resultText);
    //this->resize(this->height(), neededWidth);

    emit haveRawClicksXY(clicksX, clicksY);
    update();
}
