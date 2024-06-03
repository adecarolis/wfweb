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
#include <QSlider>
#include <QSpacerItem>
#include <QElapsedTimer>
#include <qcustomplot.h>
#include "freqctrl.h"
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
    explicit spectrumScope(bool scope, uchar receiver = 0, uchar vfo = 1, QWidget *parent = nullptr);
    ~spectrumScope();

    bool prepareWf(uint wfLength);
    void prepareScope(uint ampMap, uint spectWidth);
    bool updateScope(scopeData spectrum);
    void setRange(int floor, int ceiling);
    void wfInterpolate(bool en) { colorMap->setInterpolate(en); }
    void wfAntiAliased(bool en) { colorMap->setAntialiased(en); }
    void wfTheme(int num);
    void setUnderlayMode(underlay_t un) { underlayMode = un; clearPeaks();}
    void overflow(bool en) {ovfIndicator->setVisible(en);}
    void resizePlasmaBuffer(int size);
    void colorPreset(colorPrefsType *p);

    void setCenterFreq (double hz) { passbandCenterFrequency = hz;}
    double getCenterFreq () { return passbandCenterFrequency;}

    void setPassbandWidth(double hz) { passbandWidth = hz;}
    double getPassbandWidth() { configFilterWidth->setValue(passbandWidth*1E6); return passbandWidth;}

    void setIdentity(QString name) {this->setTitle(name);}
    bool getVfo() { return receiver;}

    void setTuningFloorZeros(bool tf) {this->tuningFloorZeros = tf;}
    void setClickDragTuning(bool cg) { this->clickDragTuning = cg;}
    void setScrollSpeedXY(int clicksX, int clicksY) { this->scrollXperClick = clicksX; this->scrollYperClick = clicksY;}

    void enableScope(bool en);

    void receiveCwPitch(uchar p);
    quint16 getCwPitch() { return cwPitch;}
    void receivePassband(quint16 pass);

    double getPBTInner () { return PBTInner;}
    void setPBTInner (uchar val);

    double getPBTOuter () { return PBTOuter;}
    void setPBTOuter (uchar val);

    quint16 getStepSize () { return stepSize;}
    void setStepSize (quint16 hz) { stepSize = hz;}

    freqt getFrequency () { return freq;}
    void setFrequency (freqt f,uchar vfo=0);

    uchar getNumVFO () { return numVFO;}

    void receiveMode (modeInfo m, uchar vfo=0);
    modeInfo currentMode() {return mode;}
    void clearSpans() { spanCombo->clear();}
    void clearMode() { modeCombo->clear();}
    void clearData() { dataCombo->clear();}
    void clearFilter() { filterCombo->clear();}

    void addSpan(QString text, QVariant data) {spanCombo->blockSignals(true); spanCombo->addItem(text,data); spanCombo->blockSignals(false);}
    void addMode(QString text, QVariant data) {modeCombo->blockSignals(true); modeCombo->addItem(text,data); modeCombo->blockSignals(false);}
    void addData(QString text, QVariant data) {dataCombo->blockSignals(true); dataCombo->addItem(text,data); dataCombo->blockSignals(false);}
    void addFilter(QString text, QVariant data) {filterCombo->blockSignals(true); filterCombo->addItem(text,data); filterCombo->blockSignals(false);}

    void selected(bool);
    bool isSelected() {return isActive;}
    void setHold(bool h);
    void setSpeed(uchar s);
    void displaySettings(int NumDigits, qint64 Minf, qint64 Maxf, int MinStep,FctlUnit unit,std::vector<bandType>* bands = Q_NULLPTR);
    void setUnit(FctlUnit unit);
    void setRefLimits(int lower, int upper);
    void setRef(int ref);
    unsigned char getDataMode() { return static_cast<unsigned char>(dataCombo->currentIndex()); }

public slots: // Can be called directly or updated via signal/slot
    void selectScopeMode(spectrumMode_t m);
    void selectSpan(centerSpanData s);
    void receiveSpots(uchar receiver, QList<spotData> spots);

signals:    
    void frequencyRange(uchar receiver, double start, double end);
    void updateScopeMode(spectrumMode_t index);
    void updateSpan(centerSpanData s);
    void showStatusBarText(QString text);
    void updateSettings(uchar receiver, int value, quint16 len, int floor, int ceiling);
    void elapsedTime(uchar receiver, qint64 ns);
    void dataChanged(modeInfo m);

private slots:
    void detachScope(bool state);
    void updatedScopeMode(int index);
    void updatedSpan(int index);
    void updatedEdge(int index);
    void updatedMode(int index);
    void holdPressed(bool en);
    void toFixedPressed();
    void customSpanPressed();
    void configPressed();

    // Mouse interaction with scope/waterfall
    void scopeClick(QMouseEvent *);
    void scopeMouseRelease(QMouseEvent *);
    void scopeMouseMove(QMouseEvent *);
    void doubleClick(QMouseEvent *); // used for both scope and wf
    void waterfallClick(QMouseEvent *);
    void scroll(QWheelEvent *);

    void clearPeaks();
    void newFrequency(qint64 freq,uchar i=0);

private:
    void clearPlasma();
    void computePlasma();
    void showHideControls(spectrumMode_t mode);
    quint64 roundFrequency(quint64 frequency, unsigned int tsHz);
    quint64 roundFrequency(quint64 frequency, int steps, unsigned int tsHz);

    QString defaultStyleSheet;

    QMutex mutex;
    QWidget* originalParent = Q_NULLPTR;
    QLabel* windowLabel = Q_NULLPTR;
    QCustomPlot* spectrum = Q_NULLPTR;
    QCustomPlot* waterfall = Q_NULLPTR;
    QLinearGradient spectrumGradient;
    QLinearGradient underlayGradient;
    freqCtrl* freqDisplay[2];
    QSpacerItem* displaySpacer;
    QGroupBox* group;
    QSplitter* splitter;
    QHBoxLayout* mainLayout;
    QVBoxLayout* layout;
    QVBoxLayout* rhsLayout;
    QHBoxLayout* displayLayout;
    QHBoxLayout* controlLayout;
    QPushButton* detachButton;
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
    QSlider* dummySlider;

    // Config screen

    QSpacerItem* rhsTopSpacer;
    QSpacerItem* rhsBottomSpacer;
    QGroupBox* configGroup;
    QFormLayout* configLayout;
    QSlider* configRef;
    QSlider* configLength;
    QSlider* configBottom;
    QSlider* configTop;
    QComboBox* configSpeed;
    QComboBox* configTheme;
    QSlider* configPbtInner;
    QSlider* configPbtOuter;
    QSlider* configIfShift;
    QSlider* configFilterWidth;


    // These parameters relate to scroll wheel response:
    int scrollYperClick = 24;
    int scrollXperClick = 24;
    float scrollWheelOffsetAccumulated=0;

    QCheckBox* rxCheckBox;

    QPushButton* confButton;
    QSpacerItem* controlSpacer;
    QSpacerItem* midSpacer;
    QCPColorGradient::GradientPreset currentTheme = QCPColorGradient::gpSpectrum;
    int currentRef = 0;
    uchar currentSpeed = 0;
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
    QCPItemText* redrawSpeed;
    QByteArray spectrumPeaks;
    QVector <double> spectrumPlasmaLine;
    QVector <QByteArray> spectrumPlasma;
    unsigned int spectrumPlasmaSizeCurrent = 64;
    unsigned int spectrumPlasmaSizeMax = 128;
    unsigned int spectrumPlasmaPosition = 0;

    underlay_t underlayMode = underlayNone;
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
    int refLower=0;
    int refUpper=0;

    // Waterfall items;
    QCPColorMap * colorMap = Q_NULLPTR;
    QCPColorMapData * colorMapData = Q_NULLPTR;
    QCPColorScale * colorScale = Q_NULLPTR;
    QVector <QByteArray> wfimage;

    cachingQueue* queue;
    uchar receiver=0;
    double startFrequency;
    QMap<QString, spotData*> clusterSpots;

    // Assorted settings
    bool tuningFloorZeros=false;
    bool clickDragTuning=false;
    bool isActive;
    uchar numVFO=1;
    bool hasScope=true;
};

#endif // SPECTRUMSCOPE_H
