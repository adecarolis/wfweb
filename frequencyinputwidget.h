#ifndef FREQUENCYINPUTWIDGET_H
#define FREQUENCYINPUTWIDGET_H

#include <QWidget>
#include <QDoubleValidator>

#include "sidebandchooser.h"
#include "wfviewtypes.h"
#include "logcategories.h"

namespace Ui {
class frequencyinputwidget;
}

class frequencyinputwidget : public QWidget
{
    Q_OBJECT

public:
    explicit frequencyinputwidget(QWidget *parent = nullptr);
    ~frequencyinputwidget();

signals:
    void issueCmdF(cmds cmd, freqt f);
    void issueCmdM(cmds cmd, mode_info m);
    void updateUIMode(mode_kind mode);
    void updateUIFrequency(freqt f);
    void gotoMemoryPreset(int presetNumber);
    void saveMemoryPreset(int presetNumber);

public slots:
    void updateCurrentMode(mode_kind mode);
    void updateFilterSelection(int filter);
    void setAutomaticSidebandSwitching(bool autossb);

private slots:
    void showEvent(QShowEvent *event);
    void on_f1btn_clicked();
    void on_f2btn_clicked();
    void on_f3btn_clicked();
    void on_f4btn_clicked();
    void on_f5btn_clicked();
    void on_f6btn_clicked();
    void on_f7btn_clicked();
    void on_f8btn_clicked();
    void on_f9btn_clicked();
    void on_fDotbtn_clicked();
    void on_f0btn_clicked();
    void on_fCEbtn_clicked();
    void on_fStoBtn_clicked();
    void on_fRclBtn_clicked();
    void on_fEnterBtn_clicked();
    void on_fBackbtn_clicked();
    void on_goFreqBtn_clicked();
    void on_freqMhzLineEdit_returnPressed();

private:
    Ui::frequencyinputwidget *ui;
    bool freqTextSelected = false;
    bool usingDataMode = false;
    bool automaticSidebandSwitching = true;
    mode_kind currentMode;
    freqt currentFrequency;
    int currentFilter = 1;
    void checkFreqSel();
};

#endif // FREQUENCYINPUTWIDGET_H
