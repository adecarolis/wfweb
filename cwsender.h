#ifndef CWSENDER_H
#define CWSENDER_H

#include <QMainWindow>
#include <QString>
#include <QFont>
#include <QInputDialog>
#include <QMessageBox>
#include <QThread>
#include <QList>
#include <math.h>
#include "cwsidetone.h"
#include "wfviewtypes.h"

namespace Ui {
class cwSender;
}

class cwSender : public QMainWindow
{
    Q_OBJECT

public:
    explicit cwSender(QWidget *parent = 0);
    ~cwSender();
    QStringList getMacroText();
    void setMacroText(QStringList macros);
    void setCutNumbers(bool val);
    void setSendImmediate(bool val);
    void setSidetoneEnable(bool val);
    void setSidetoneLevel(int val);
    bool getCutNumbers();
    bool getSendImmediate();
    bool getSidetoneEnable();
    int getSidetoneLevel();

signals:
    void sendCW(QString cwMessage);
    void stopCW();
    void setKeySpeed(unsigned char wpm);
    void setDashRatio(unsigned char ratio);
    void setPitch(unsigned char pitch);
    void setLevel(int level);
    void setBreakInMode(unsigned char b);
    void getCWSettings();
    void sidetone(QString text);
    void pitchChanged(int val);
    void dashChanged(int val);
    void wpmChanged(int val);

public slots:
    void handleKeySpeed(unsigned char wpm);
    void handleDashRatio(unsigned char ratio);
    void handlePitch(unsigned char pitch);
    void handleBreakInMode(unsigned char b);
    void handleCurrentModeUpdate(mode_kind mode);

private slots:
    void on_sendBtn_clicked();
    void showEvent(QShowEvent* event);

    void on_stopBtn_clicked();

    //void on_textToSendEdit_returnPressed();

    void textChanged(QString text);

    void on_breakinCombo_activated(int index);

    void on_wpmSpin_valueChanged(int arg1);

    void on_dashSpin_valueChanged(double arg1);

    void on_pitchSpin_valueChanged(int arg1);

    void on_macro1btn_clicked();

    void on_macro2btn_clicked();

    void on_macro3btn_clicked();

    void on_macro4btn_clicked();

    void on_macro5btn_clicked();

    void on_macro6btn_clicked();

    void on_macro7btn_clicked();

    void on_macro8btn_clicked();

    void on_macro9btn_clicked();

    void on_macro10btn_clicked();

    void on_sequenceSpin_valueChanged(int arg1);

    void on_sidetoneEnableChk_clicked(bool clicked);

    void on_sidetoneLevelSlider_valueChanged(int val);

private:
    Ui::cwSender *ui;
    QString macroText[11];
    int sequenceNumber = 1;
    int lastSentPos = 0;
    mode_kind currentMode;
    int sidetoneLevel=0;
    void processMacroButton(int buttonNumber, QPushButton *btn);
    void runMacroButton(int buttonNumber);
    void editMacroButton(int buttonNumber, QPushButton *btn);
    void setMacroButtonText(QString btnText, QPushButton *btn);
    cwSidetone* tone=Q_NULLPTR;
    QThread* toneThread = Q_NULLPTR;
    bool sidetoneWasEnabled=false;
    QList<QMetaObject::Connection> connections;
};

#endif // CWSENDER_H
