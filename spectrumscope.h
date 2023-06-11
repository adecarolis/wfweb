#ifndef SPECTRUMSCOPE_H
#define SPECTRUMSCOPE_H

#include <QWidget>
#include <QMutex>
#include <QMutexLocker>
#include <QSplitter>
#include <QComboBox>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QSpacerItem>
#include <qcustomplot.h>
#include "cluster.h"
#include "wfviewtypes.h"
#include "colorprefs.h"
#include "rigidentities.h"
#include "cachingqueue.h"

enum scopeTypes {
    scopeSpectrum=0,
    scopeWaterfall,
    scopeNone
};

class spectrumScope : public QGroupBox
{
    Q_OBJECT
public:
    explicit spectrumScope(QWidget *parent = nullptr);

    bool prepareWf(uint wfLength);
    void prepareScope(uint ampMap, uint spectWidth);
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

    void setPassbandWidth(double hz) { passbandWidth = hz;}
    double getPassbandWidth() { return passbandWidth;}

    void setIdentity(QString name, bool s) {this->setTitle(name), sub = s;}
    bool getSub() { return sub;}

    void setTuningFloorZeros(bool tf) {this->tuningFloorZeros = tf;}
    void setClickDragTuning(bool cg) { this->clickDragTuning = cg;}

    void receiveCwPitch(uchar p);
    quint16 getCwPitch() { return cwPitch;}
    void receivePassband(quint16 pass);

    double getPBTInner () { return PBTInner;}
    void setPBTInner (double hz) { PBTInner = hz;}

    double getPBTOuter () { return PBTOuter;}
    void setPBTOuter (double hz) { PBTOuter = hz;}

    quint16 getStepSize () { return stepSize;}
    void setStepSize (quint16 hz) { stepSize = hz;}

    void setFrequency (freqt f) { freq = f;}
    freqt getFrequency () { return freq;}

    void receiveMode (modeInfo m);

    void clearSpans() { spanCombo->clear();}
    void clearMode() { modeCombo->clear();}
    void clearData() { dataCombo->clear();}
    void clearFilter() { filterCombo->clear();}

    void addSpan(QString text, QVariant data) {spanCombo->blockSignals(true); spanCombo->addItem(text,data); spanCombo->blockSignals(false);}
    void addMode(QString text, QVariant data) {modeCombo->blockSignals(true); modeCombo->addItem(text,data); modeCombo->blockSignals(false);}
    void addData(QString text, QVariant data) {dataCombo->blockSignals(true); dataCombo->addItem(text,data); dataCombo->blockSignals(false);}
    void addFilter(QString text, QVariant data) {filterCombo->blockSignals(true); filterCombo->addItem(text,data); filterCombo->blockSignals(false);}

    void selected(bool);
    void setHold(bool h);
    void setSpeed(uchar s);

public slots: // Can be called directly or updated via signal/slot
    void selectScopeMode(spectrumMode_t m);
    void selectSpan(centerSpanData s);
    void receiveSpots(QList<spotData> spots);

signals:    
    void frequencyRange(bool sub, double start, double end);
    void updateScopeMode(spectrumMode_t index);
    void updateSpan(centerSpanData s);
    void showStatusBarText(QString text);


private slots:
    void updatedScopeMode(int index);
    void updatedSpan(int index);
    void updatedTheme(int index);
    void updatedEdge(int index);
    void updatedMode(int index);
    void updatedSpeed(int index);
    void holdPressed(bool en);
    void toFixedPressed();
    void customSpanPressed();

    // Mouse interaction with scope/waterfall
    void scopeClick(QMouseEvent *);
    void scopeMouseRelease(QMouseEvent *);
    void scopeMouseMove(QMouseEvent *);
    void doubleClick(QMouseEvent *); // used for both scope and wf
    void waterfallClick(QMouseEvent *);
    void scroll(QWheelEvent *);

    void clearPeaks();

private:
    void clearPlasma();
    void computePlasma();
    void showHideControls(spectrumMode_t mode);
    quint64 roundFrequency(quint64 frequency, unsigned int tsHz);
    quint64 roundFrequency(quint64 frequency, int steps, unsigned int tsHz);

    QMutex mutex;
    QCustomPlot* spectrum = Q_NULLPTR;
    QCustomPlot* waterfall = Q_NULLPTR;
    QGroupBox* group;
    QSplitter* splitter;
    QVBoxLayout* mainLayout;
    QVBoxLayout* layout;
    QHBoxLayout* controlLayout;
    QCheckBox* enableCheckBox;
    QLabel* scopeModeLabel;
    QComboBox* scopeModeCombo;
    QLabel* spanLabel;
    QComboBox* spanCombo;
    QLabel* edgeLabel;
    QComboBox* edgeCombo;
    QPushButton* edgeButton;
    QPushButton* toFixedButton;
    QPushButton* clearPeaksButton;
    QLabel* themeLabel;
    QComboBox* modeCombo;
    QComboBox* dataCombo;
    QComboBox* filterCombo;
    QComboBox* antennaCombo;
    QPushButton* holdButton;
    QComboBox* speedCombo;

    QCheckBox* rxCheckBox;
    QComboBox* themeCombo;

    QSpacerItem* controlSpacer;
    QSpacerItem* midSpacer;
    int currentTheme = 1;
    colorPrefsType colors;
    freqt freq;
    modeInfo mode;
    bool lock = false;
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
    quint16 stepSize = 100;

    // Waterfall items;
    QCPColorMap * colorMap = Q_NULLPTR;
    QCPColorMapData * colorMapData = Q_NULLPTR;
    QCPColorScale * colorScale = Q_NULLPTR;
    QVector <QByteArray> wfimage;

    cachingQueue* queue;
    bool sub=false;    
    double startFrequency;
    QMap<QString, spotData*> clusterSpots;

    // Assorted settings
    bool tuningFloorZeros=false;
    bool clickDragTuning=false;
};

#endif // SPECTRUMSCOPE_H
