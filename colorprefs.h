#ifndef COLORPREFS_H
#define COLORPREFS_H

#include <QColor>
#include <QString>

struct colorPrefsType{
    int presetNum = -1;
    QString *presetName = Q_NULLPTR;

    // Spectrum line plot:
    QColor gridColor;
    QColor axisColor;
    QColor textColor;
    QColor spectrumLine;
    QColor spectrumFill;
    QColor underlayLine;
    QColor underlayFill;
    QColor plotBackground;
    QColor tuningLine;
    QColor passband;
    QColor pbt;

    // Waterfall:
    QColor wfBackground;
    QColor wfGrid;
    QColor wfAxis;
    QColor wfText;

    // Meters:
    QColor meterLevel;
    QColor meterAverage;
    QColor meterPeakLevel;
    QColor meterPeakScale;
    QColor meterLowerLine;
    QColor meterLowText;

    // Assorted
    QColor clusterSpots;

};


#endif // COLORPREFS_H
