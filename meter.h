#ifndef METER_H
#define METER_H

#include <QDebug>
#include <QStandardItemModel>
#include <QComboBox>
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
    void configureMeterSignal(meter_t type);

public slots:
    void paintEvent(QPaintEvent *);

    void updateDrawing(int num);
    void setLevels(double current, double peak, double average);
    void setLevels(double current, double peak); // calculate avg
    void setLevel(double current);
    void setCompReverse(bool reverse);
    void clearMeterOnPTTtoggle();
    void clearMeter();
    void setMeterType(meter_t type);
    void setMeterShortString(QString);
    QString getMeterShortString();
    meter_t getMeterType();
    void setColors(QColor current, QColor peakScale, QColor peakLevel,
                   QColor average, QColor lowLine,
                   QColor lowText);
    void blockMeterType(meter_t type);

    void enableCombo(bool en) { combo->setEnabled(en); }
    void setMeterExtremities(double min, double max, double redline);

private slots:
    void acceptComboItem(int item);

private:
    //QPainter painter;
    bool eventFilter(QObject *object, QEvent *event);
    void handleDoubleClick();
    bool freezeDrawing = false;
    QComboBox *combo = NULL;
    meter_t meterType;
    meter_t lastDrawMeterType;
    bool recentlyChangedParameters = false;
    QString meterShortString;
    double scaleMin = 0;
    double scaleMax = 255;
    double scaleRedline = 128;
    bool haveExtremities = false;
    bool haveUpdatedData = false;
    bool haveReceivedSomeData = false;
    int fontSize = 10;
    int length=30;
    double current=0.0;
    double peak = 0.0;
    double average = 0.0;
    int currentRect = 0;
    int averageRect = 0;
    int peakRect = 0;

    bool reverseCompMeter = true;

    int averageBalisticLength = 30;
    int peakBalisticLength = 30;
    int avgPosition=0;
    int peakPosition=0;
    std::vector<double_t> avgLevels;
    std::vector<double_t> peakLevels;

    int peakRedLevel=0;
    bool drawLabels = true;
    int mXstart = 0; // Starting point for S=0.
    int mYstart = 14; // height, down from top, where the drawing starts
    int barHeight = 10; // Height of meter "bar" indicators
    int scaleLineYstart = 12;
    int scaleTextYstart = 10;

    int widgetWindowHeight = mYstart + barHeight + 0; // height of drawing canvis.

    // These functions scale the data to fit the meter:
    void prepareValue_dBuEMFdBm();
    void scaleLinearNumbersForDrawing(); // input = current, average, peak, output = currentRect, averageRect, peakRect
    void scaleLogNumbersForDrawing(); // for audio
    double getValueFromPixelScale(int p); // pixel (0-255) scale to values
    int getPixelScaleFromValue(double v); // scale value to pixel (0-255)

    // These functions draw the rectangular bars
    // for the level(s) to be represented:
    void drawValue_Linear(QPainter *qp, bool reverse); // anything you wish 0-255
    void drawValue_Log(QPainter *qp);
    void drawValue_Center(QPainter *qp);


    // These functions draw the meter scale:
    void drawScaleS(QPainter *qp);
    void drawScaleCenter(QPainter *qp);
    void drawScalePo(QPainter *qp);
    void drawScaleRxdB(QPainter *qp);
    void drawScaleALC(QPainter *qp);
    void drawScaleSWR(QPainter *qp);
    void drawScaleVd(QPainter *qp);
    void drawScaleId(QPainter *qp);
    void drawScaleComp(QPainter *qp);
    void drawScaleCompInverted(QPainter *qp);
    void drawScale_dBFs(QPainter *qp);
    void drawScaleRaw(QPainter *qp);
    void drawScaledB(QPainter *qp, int start=-100, int end=-20,int interval=20);

    void drawLabel(QPainter *qp);
    void drawValueText(QPainter *qp,float value);
    void muteSingleComboItem(QComboBox *comboBox, int index);
    void enableAllComboBoxItems(QComboBox *combobox, bool en=true);
    void setComboBoxItemEnabled(QComboBox * comboBox, int index, bool enabled);

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
