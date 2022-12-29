#ifndef METER_H
#define METER_H

#include <QWidget>
#include <QPainter>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>

#include "rigcommander.h" // for meter types
#include "audiotaper.h"

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
    void setLevels(int current, int peak); // calculate avg
    void setLevel(int current);
    void clearMeterOnPTTtoggle();
    void clearMeter();
    void setMeterType(meterKind type);
    void setMeterShortString(QString);
    QString getMeterShortString();
    meterKind getMeterType();
    void setColors(QColor current, QColor peakScale, QColor peakLevel,
                   QColor average, QColor lowLine,
                   QColor lowText);


private:
    //QPainter painter;
    meterKind meterType;
    QString meterShortString;
    int fontSize = 10;
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
    bool drawLabels = true;
    int mXstart = 0; // Starting point for S=0.
    int mYstart = 14; // height, down from top, where the drawing starts
    int barHeight = 10; // Height of meter "bar" indicators
    int scaleLineYstart = 12;
    int scaleTextYstart = 10;

    int widgetWindowHeight = mYstart + barHeight + 0; // height of drawing canvis.

    void drawScaleS(QPainter *qp);
    void drawScaleCenter(QPainter *qp);
    void drawScalePo(QPainter *qp);
    void drawScaleRxdB(QPainter *qp);
    void drawScaleALC(QPainter *qp);
    void drawScaleSWR(QPainter *qp);
    void drawScaleVd(QPainter *qp);
    void drawScaleId(QPainter *qp);
    void drawScaleComp(QPainter *qp);
    void drawScale_dBFs(QPainter *qp);
    void drawScaleRaw(QPainter *qp);

    void drawLabel(QPainter *qp);

    QString label;

    QColor currentColor;
    QColor averageColor;
    QColor peakColor;
    // S0-S9:
    QColor lowTextColor;
    QColor lowLineColor;
    // S9+:
    QColor highTextColor;
    QColor highLineColor;

    QColor midScaleColor;
    QColor centerTuningColor;

};

#endif // METER_H
