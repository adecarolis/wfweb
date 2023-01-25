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
    void setTone(quint16 tone);
    void setTSQL(quint16 tsql);
    void setDTCS(quint16 dcode, bool tinv, bool rinv);
    void getTone();
    void getTSQL();
    void getDTCS();
    void setRptAccessMode(rptAccessTxRx tmode);
    void getRptAccessMode();
    // Split:
    void getSplitModeEnabled();
    void getTransmitFrequency();
    void setSplitModeEnabled(bool splitEnabled);
    void setTransmitFrequency(freqt transmitFreq);
    void setTransmitMode(mode_info m);

public slots:
    void receiveDuplexMode(duplexMode dm);
    void handleRptAccessMode(rptAccessTxRx tmode);
    void handleTone(quint16 tone);
    void handleTSQL(quint16 tsql);
    void handleDTCS(quint16 dcscode, bool tinv, bool rinv);
    void handleSplitMode(bool splitEnabled);
    void handleSplitFrequency(freqt transmitFreq);
    void handleUpdateCurrentMainFrequency(freqt mainfreq);
    void handleUpdateCurrentMainMode(mode_info m);

private slots:
    void showEvent(QShowEvent *event);
    void on_rptSimplexBtn_clicked();
    void on_rptDupPlusBtn_clicked();
    void on_rptDupMinusBtn_clicked();
    void on_rptAutoBtn_clicked();
    void on_rptReadRigBtn_clicked();
    void on_rptToneCombo_activated(int index);
    void on_rptDTCSCombo_activated(int index);
    void on_debugBtn_clicked();
    void on_toneNone_clicked();
    void on_toneTone_clicked();
    void on_toneTSQL_clicked();
    void on_toneDTCS_clicked();    
    void on_splitOffsetSetBtn_clicked();
    void on_splitEnableChk_clicked(bool enabled);
    void on_splitPlusButton_clicked();
    void on_splitMinusBtn_clicked();

    void on_splitTxFreqSetBtn_clicked();

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
    mode_info currentMode;
    bool usedPlusSplit = false;
};

#endif // REPEATERSETUP_H
