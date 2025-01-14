#ifndef RECEIVERWIDGET_H
#define RECEIVERWIDGET_H

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

struct bandIndicator {
    QCPItemLine* line;
    QCPItemText* text;
};

class receiverWidget : public QGroupBox
{
    Q_OBJECT
public:
    explicit receiverWidget(bool scope, uchar receiver = 0, uchar vfo = 1, QWidget *parent = nullptr);
    ~receiverWidget();

    bool prepareWf(uint wfLength);
    void prepareScope(uint ampMap, uint spectWidth);
    void changeWfLength(uint wf);
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
    bool getReceiver() { return receiver;}

    void setTuningFloorZeros(bool tf) {this->tuningFloorZeros = tf;}
    void setClickDragTuning(bool cg) { this->clickDragTuning = cg;}
    void setSatMode(bool sm) { this->satMode = sm;}

    void setScrollSpeedXY(int clicksX, int clicksY) { this->scrollXperClick = clicksX; this->scrollYperClick = clicksY;}

    void enableScope(bool en);

    void receiveCwPitch(quint16 p);
    quint16 getCwPitch() { return cwPitch;}
    void receivePassband(quint16 pass);

    double getPBTInner () { return PBTInner;}
    void setPBTInner (uchar val);

    double getPBTOuter () { return PBTOuter;}
    void setPBTOuter (uchar val);

    void setIFShift (uchar val);

    quint16 getStepSize () { return stepSize;}
    void setStepSize (quint64 hz) { stepSize = hz;}

    freqt getFrequency () { return freq;}
    void setFrequency (freqt f,uchar vfo=0);

    uchar getNumVFO () { return numVFO;}

    void receiveMode (modeInfo m, uchar vfo=0);

    void setEdge(uchar index);
    void setHold(bool h);
    void setSpeed(uchar s);
    void setSpan(centerSpanData s);
    void setScopeMode(spectrumMode_t m);
    void setSplit(bool en) { splitButton->setChecked(en); }
    void setTracking(bool en) { tracking=en; }
    void setRef(int ref);
    void setRefLimits(int lower, int upper);

    void setBandIndicators(bool show, QString region, std::vector<bandType>* bands);
    void setUnit(FctlUnit unit);
    void setSeparators(QChar group, QChar decimal);

    modeInfo currentMode() {return mode;}
    uchar currentFilter() {return filterCombo->currentData().toInt();}
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

    void displaySettings(int NumDigits, qint64 Minf, qint64 Maxf, int MinStep,FctlUnit unit,std::vector<bandType>* bands = Q_NULLPTR);
    quint8 getDataMode() { return static_cast<quint8>(dataCombo->currentIndex()); }

    void changeSpan(qint8 val);
    void updateBSR(std::vector<bandType>* bands);
    QImage getSpectrumImage();
    QImage getWaterfallImage();
    bandType getCurrentBand();

public slots: // Can be called directly or updated via signal/slot
    void receiveSpots(uchar receiver, QList<spotData> spots);
    void memoryMode(bool en);


signals:    
    void frequencyRange(uchar receiver, double start, double end);
    void updateScopeMode(spectrumMode_t index);
    void updateSpan(centerSpanData s);
    void showStatusBarText(QString text);
    void updateSettings(uchar receiver, int value, quint16 len, int floor, int ceiling);
    void elapsedTime(uchar receiver, qint64 ns);
    void dataChanged(modeInfo m);
    void bandChanged(uchar receiver, bandType b);
    void spectrumTime(double time);
    void waterfallTime(double time);
    void sendScopeImage(uchar receiver);
    void sendTrack(int f);

private slots:
    void detachScope(bool state);
    void updatedMode(int index);
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
    void receiveTrack(int f);

private:
    void clearPlasma();
    void computePlasma();
    void showHideControls(spectrumMode_t mode);
    void showBandIndicators(bool en);
    void vfoSwap();

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
    QList <freqCtrl*> freqDisplay;
    QSpacerItem* displayLSpacer;
    QPushButton* vfoSelectButton;
    QSpacerItem* displayCSpacer;
    QPushButton* vfoSwapButton;
    QPushButton* vfoEqualsButton;
    QPushButton* splitButton;
    QSpacerItem* displayRSpacer;
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
    QHBoxLayout* configIfLayout;
    QSlider* configRef;
    QSlider* configLength;
    QSlider* configBottom;
    QSlider* configTop;
    QComboBox* configSpeed;
    QComboBox* configTheme;
    QSlider* configPbtInner;
    QSlider* configPbtOuter;
    QSlider* configIfShift;
    QPushButton* configResetIf;
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
    modeInfo unselectedMode;
    freqt unselectedFreq;
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
    QVector<bandIndicator> bandIndicators;
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

    double PBTInner = 1.0; // Invalid value to start.
    double PBTOuter = 1.0; // Invalid value to start.
    double passbandWidth = 0.0;
    double passbandCenterFrequency = 0.0;
    double pbtDefault = 0.0;
    quint16 cwPitch = 600;
    quint64 stepSize = 100;
    uchar ifShift = 0;
    int refLower=0;
    int refUpper=0;

    // Waterfall items;
    QCPColorMap * colorMap = Q_NULLPTR;
    QCPColorMapData * colorMapData = Q_NULLPTR;
    QCPColorScale * colorScale = Q_NULLPTR;
    QVector <QByteArray> wfimage;

    cachingQueue* queue;
    uchar receiver=0;
    uchar rxcmd=0;
    double startFrequency;
    QMap<QString, spotData*> clusterSpots;

    // Assorted settings
    bool tuningFloorZeros=false;
    bool clickDragTuning=false;
    bool isActive;
    uchar numVFO=1;
    uchar selectedVFO=0;
    bool hasScope=true;
    QString currentRegion="1";
    spectrumMode_t currentScopeMode=spectrumMode_t::spectModeCenter;
    bool bandIndicatorsVisible=false;
    rigCapabilities* rigCaps=Q_NULLPTR;
    bandType currentBand;
    QElapsedTimer lastData;
    bool satMode = false;
    bool memMode = false;
    bool tracking = false;
};

#endif // RECEIVERWIDGET_H
