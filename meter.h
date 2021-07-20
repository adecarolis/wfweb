#ifndef METER_H
#define METER_H

#include <QWidget>
#include <QPainter>
#include <vector>
#include <algorithm>
#include <numeric>

#include "rigcommander.h" // for meter types

class meter : public QWidget
{
    Q_OBJECT
public:
    explicit meter(QWidget *parent = nullptr);

signals:

public slots:
    void paintEvent(QPaintEvent *);

    void updateDrawing(int num);
    void setLevels(int current, int peak, int average);
    void setLevel(int current);
    void setMeterType(meterKind type);
    meterKind getMeterType();


private:
    //QPainter painter;
    meterKind meterType;
    int fontSize = 5;
    int length=30;
    int current=0;
    int peak = 0;
    int average = 0;

    int averageBalisticLength = 30;
    int peakBalisticLength = 30;
    int avgPosition=0;
    int peakPosition=0;
    std::vector<unsigned char> avgLevels;
    std::vector<unsigned char> peakLevels;



    int peakRedLevel=0;

    int mXstart = 10; // Starting point for S=0.
    int mYstart = 14; // height, down from top, where the drawing starts
    int barHeight = 10; // Height of meter "bar" indicators
    int scaleLineYstart = 12;
    int scaleTextYstart = 10;

    int widgetWindowHeight = mYstart + barHeight + 10; // height of drawing canvis.

    void drawScaleS(QPainter *qp);
    void drawScaleCenter(QPainter *qp);
    void drawScalePo(QPainter *qp);
    void drawScaleRxdB(QPainter *qp);
    void drawScaleALC(QPainter *qp);
    void drawScaleSWR(QPainter *qp);
    void drawScaleVd(QPainter *qp);
    void drawScaleId(QPainter *qp);
    void drawScaleRaw(QPainter *qp);

    QColor currentColor;
    QColor averageColor;
    QColor peakColor;
    // S0-S9:
    QColor lowTextColor;
    QColor lowLineColor;
    // S9+:
    QColor highTextColor;
    QColor highLineColor;

};

#endif // METER_H
