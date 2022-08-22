#ifndef COLORPREFS_H
#define COLORPREFS_H

#include <QColor>
#include <QString>

struct colorPrefsType{
    int presetNum = -1;
    QString presetName = QString("uninitialized");

    QColor gridColor;
    QColor axisColor;
    QColor textColor;
    QColor spectrumLine;
    QColor spectrumFill;
    QColor underlayLine;
    QColor underlayFill;
    QColor plotBackground;
    QColor tuningLine;

    QColor meterLevel;
    QColor meterAverage;
    QColor meterPeak;
    QColor meterLowerLine;
    QColor meterLowText;


    QColor wfBackground;
    QColor wfGrid;
    QColor wfAxis;
    QColor wfText;
};


#endif // COLORPREFS_H
