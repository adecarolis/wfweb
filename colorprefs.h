#ifndef COLORPREFS_H
#define COLORPREFS_H

#include <QColor>
#include <QString>

#define numColorPresetsTotal (5)

struct colorPrefsType{
    int presetNum = -1;
    QString *presetName = Q_NULLPTR;

    // Spectrum line plot:
    QColor gridColor;
    QColor axisColor;
    QColor textColor;
    QColor spectrumLine;
    QColor spectrumFill;
    QColor spectrumFillTop;
    QColor spectrumFillBot;
    bool useSpectrumFillGradient = false;
    QColor underlayLine;
    QColor underlayFill;
    bool useUnderlayFillGradient = false;
    QColor underlayFillTop;
    QColor underlayFillBot;
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
    QColor meterScale;
    QColor meterLowerLine;
    QColor meterLowText;

    // Assorted
    QColor clusterSpots;

};


#endif // COLORPREFS_H
