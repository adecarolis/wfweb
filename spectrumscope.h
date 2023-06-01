#ifndef SPECTRUMSCOPE_H
#define SPECTRUMSCOPE_H

#include <QWidget>
#include <QMutex>
#include <QMutexLocker>
#include <QSplitter>
#include <qcustomplot.h>
#include "wfviewtypes.h"
#include "colorprefs.h"

enum scopeTypes {
    scopeSpectrum=0,
    scopeWaterfall,
    scopeNone
};

class spectrumScope : public QWidget
{
    Q_OBJECT
public:
    explicit spectrumScope(QWidget *parent = nullptr);

    bool prepareWf(uint wfLength);
    bool update(scopeData spectrum);
    void preparePlasma();
    void setRange(int floor, int ceiling);
    void wfInterpolate(bool en) { colorMap->setInterpolate(en); }
    void wfAntiAliased(bool en) { colorMap->setAntialiased(en); }
    void wfTheme(int num) { colorMap->setGradient(static_cast<QCPColorGradient::GradientPreset>(num));}
    void setUnderlayMode(underlay_t un) { underlayMode = un; clearPeaks();}
    void overflow(bool en) {ovfIndicator->setVisible(en);}
    void resizePlasmaBuffer(int size);
    void colorPreset(colorPrefsType *p);
    void setCenterFreq (double hz) { passbandCenterFrequency = hz;}
    double getCenterFreq () { return passbandCenterFrequency;}
    void setPassbandWidth (double hz) { passbandWidth = hz;}
    double getPassbandWidth () { return passbandWidth;}
    void setFrequency (freqt f) { freq = f;}
    freqt getFrequency () { return freq;}

signals:
    void frequencyRange(double start, double end);

private:

    void clearPeaks();
    void clearPlasma();
    void computePlasma();

    QMutex mutex;
    QCustomPlot* spectrum = Q_NULLPTR;
    QCustomPlot* waterfall = Q_NULLPTR;
    QSplitter* splitter;
    QVBoxLayout* layout;
    int currentTheme = 1;
    colorPrefsType colors;
    freqt freq;
    modeInfo rigMode;
    bool scopePrepared=false;
    quint16 spectWidth=689;
    quint16 maxAmp=200;
    quint16 wfLength;
    quint16 wfLengthMax;

    double lowerFreq=0.0;
    double upperFreq=0.0;

    // Spectrum items;
    QCPItemLine * freqIndicatorLine;
    QCPItemRect* passbandIndicator;
    QCPItemRect* pbtIndicator;
    QCPItemText* oorIndicator;
    QCPItemText* ovfIndicator;
    QByteArray spectrumPeaks;
    QVector <double> spectrumPlasmaLine;
    QVector <QByteArray> spectrumPlasma;
    unsigned int spectrumPlasmaSize = 64;
    underlay_t underlayMode = underlayNone;
    bool plasmaPrepared = false;
    QMutex plasmaMutex;

    double plotFloor = 0;
    double plotCeiling = 160;
    double wfFloor = 0;
    double wfCeiling = 160;
    double oldPlotFloor = -1;
    double oldPlotCeiling = 999;
    double mousePressFreq = 0.0;
    double mouseReleaseFreq = 0.0;

    passbandActions passbandAction = passbandStatic;

    double PBTInner = 0.0;
    double PBTOuter = 0.0;
    double passbandWidth = 0.0;
    double passbandCenterFrequency = 0.0;
    double pbtDefault = 0.0;
    quint16 cwPitch = 600;

    // Waterfall items;
    QCPColorMap * colorMap = Q_NULLPTR;
    QCPColorMapData * colorMapData = Q_NULLPTR;
    QCPColorScale * colorScale = Q_NULLPTR;
    QVector <QByteArray> wfimage;
};

#endif // SPECTRUMSCOPE_H
