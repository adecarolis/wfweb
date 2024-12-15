#ifndef REPEATERSETUP_H
#define REPEATERSETUP_H

#include <QMainWindow>
#include <QDebug>

#include "cachingqueue.h"
#include "repeaterattributes.h"
#include "rigidentities.h"
#include "logcategories.h"

namespace Ui {
class repeaterSetup;
}

class repeaterSetup : public QMainWindow
{
    Q_OBJECT

public:
    explicit repeaterSetup(QWidget *parent = 0);
    ~repeaterSetup();
    void setRig(rigCapabilities rig);

signals:
    void getDuplexMode();
    void setDuplexMode(duplexMode_t dm);
    void setTone(toneInfo tone);
    void setTSQL(toneInfo tsql);
    void setDTCS(toneInfo tsql);
    void getTone();
    void getTSQL();
    void getDTCS();
    void getRptAccessMode();
    void setRptDuplexOffset(freqt f);
    void getRptDuplexOffset();
    // Split:
    void getSplitModeEnabled();
    void setQuickSplit(bool qsOn);
    void getTransmitFrequency();
    // Use the duplexMode_t to communicate split.
    // void setSplitModeEnabled(bool splitEnabled);
    void setTransmitFrequency(freqt transmitFreq);
    void setTransmitMode(modeInfo m);
    // VFO:
    void selectVFO(vfo_t v); // A,B,M,S
    void equalizeVFOsAB();
    void equalizeVFOsMS();
    void swapVFOs();

public slots:
    void receiveDuplexMode(duplexMode_t dm);
    void handleRptAccessMode(rptAccessTxRx_t tmode);
    void handleTone(quint16 tone);
    void handleTSQL(quint16 tsql);
    void handleDTCS(quint16 dcscode, bool tinv, bool rinv);
    // void handleSplitMode(bool splitEnabled);
    // void handleSplitFrequency(freqt transmitFreq);
    void handleUpdateCurrentMainFrequency(freqt mainfreq);
    void handleUpdateCurrentMainMode(modeInfo m);
    void handleTransmitStatus(bool amTransmitting);
    void handleRptOffsetFrequency(freqt f);
    void receiveRigCaps(rigCapabilities* caps);
    void receiveQuickSplit(bool on);

private slots:
    void showEvent(QShowEvent *event);
    void on_rptSimplexBtn_clicked();
    void on_rptDupPlusBtn_clicked();
    void on_rptDupMinusBtn_clicked();
    void on_rptAutoBtn_clicked();
    void on_rptToneCombo_activated(int index);
    void on_rptDTCSCombo_activated(int index);
    void on_toneNone_clicked();
    void on_toneTone_clicked();
    void on_toneTSQL_clicked();
    void on_toneDTCS_clicked();    
    void on_splitPlusButton_clicked();
    void on_splitMinusBtn_clicked();
    void on_splitTxFreqSetBtn_clicked();
    void on_selABtn_clicked();
    void on_selBBtn_clicked();
    void on_aEqBBtn_clicked();
    void on_swapABBtn_clicked();
    void on_selMainBtn_clicked();
    void on_selSubBtn_clicked();
    void on_mEqSBtn_clicked();
    void on_swapMSBtn_clicked();
    void on_setToneSubVFOBtn_clicked();
    void on_setRptrSubVFOBtn_clicked();
    void on_rptrOffsetSetBtn_clicked();
    void on_splitOffBtn_clicked();
    void on_splitEnableChk_clicked();
    void on_rptrOffsetEdit_returnPressed();
    void on_splitTransmitFreqEdit_returnPressed();
    void on_setSplitRptrToneChk_clicked(bool checked);
    void on_quickSplitChk_clicked(bool checked);

private:
    Ui::repeaterSetup *ui;
    freqt currentMainFrequency;
    void populateTones();
    void populateDTCS();
    quint64 getFreqHzFromKHzString(QString khz);
    quint64 getFreqHzFromMHzString(QString MHz);
    void setRptAccessMode(rptrAccessData rd);

    rigCapabilities rig;
    bool haveRig = false;
    bool haveReceivedFrequency = false;
    duplexMode_t currentdm;
    modeInfo currentModeMain;
    modeInfo modeTransmitVFO;
    freqt currentOffset;
    bool usedPlusSplit = false;
    bool amTransmitting = false;
    cachingQueue* queue = Q_NULLPTR;
    rigCapabilities* rigCaps = Q_NULLPTR;
};

#endif // REPEATERSETUP_H
