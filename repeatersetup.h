#ifndef REPEATERSETUP_H
#define REPEATERSETUP_H

#include <QMainWindow>
#include <QDebug>

#include "repeaterattributes.h"
#include "rigidentities.h"

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
    void setDuplexMode(duplexMode dm);
    void setTone(rptrTone_t tone);
    void setTSQL(rptrTone_t tsql);
    void setDTCS(quint16 dcode, bool tinv, bool rinv);
    void getTone();
    void getTSQL();
    void getDTCS();
    void setRptAccessMode(rptrAccessData_t rd);
    void getRptAccessMode();
    void setRptDuplexOffset(freqt f);
    void getRptDuplexOffset();
    // Split:
    void getSplitModeEnabled();
    void setQuickSplit(bool qsOn);
    void getTransmitFrequency();
    // Use the duplexMode to communicate split.
    // void setSplitModeEnabled(bool splitEnabled);
    void setTransmitFrequency(freqt transmitFreq);
    void setTransmitMode(mode_info m);
    // VFO:
    void selectVFO(vfo_t v); // A,B,M,S
    void equalizeVFOsAB();
    void equalizeVFOsMS();
    void swapVFOs();

public slots:
    void receiveDuplexMode(duplexMode dm);
    void handleRptAccessMode(rptAccessTxRx tmode);
    void handleTone(quint16 tone);
    void handleTSQL(quint16 tsql);
    void handleDTCS(quint16 dcscode, bool tinv, bool rinv);
    // void handleSplitMode(bool splitEnabled);
    // void handleSplitFrequency(freqt transmitFreq);
    void handleUpdateCurrentMainFrequency(freqt mainfreq);
    void handleUpdateCurrentMainMode(mode_info m);
    void handleTransmitStatus(bool amTransmitting);
    void handleRptOffsetFrequency(freqt f);

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

    rigCapabilities rig;
    bool haveRig = false;
    duplexMode currentdm;
    mode_info currentModeMain;
    mode_info modeTransmitVFO;
    freqt currentOffset;
    bool usedPlusSplit = false;
    bool amTransmitting = false;
};

#endif // REPEATERSETUP_H
