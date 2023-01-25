#ifndef CWSENDER_H
#define CWSENDER_H

#include <QMainWindow>
#include <QString>
#include <QFont>
#include <QInputDialog>
#include <QMessageBox>
#include "wfviewtypes.h"
#include "logcategories.h"


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
signals:
    void sendCW(QString cwMessage);
    void stopCW();
    void setKeySpeed(unsigned char wpm);
    void setPitch(unsigned char pitch);
    void setBreakInMode(unsigned char b);
    void getCWSettings();

public slots:
    void handleKeySpeed(unsigned char wpm);
    void handlePitch(unsigned char pitch);
    void handleBreakInMode(unsigned char b);
    void handleCurrentModeUpdate(mode_kind mode);

private slots:
    void on_sendBtn_clicked();
    void showEvent(QShowEvent* event);

    void on_stopBtn_clicked();

    void on_textToSendEdit_returnPressed();

    void on_breakinCombo_activated(int index);

    void on_wpmSpin_valueChanged(int arg1);

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

private:
    Ui::cwSender *ui;
    QString macroText[11];
    int sequenceNumber = 1;
    mode_kind currentMode;
    void processMacroButton(int buttonNumber, QPushButton *btn);
    void runMacroButton(int buttonNumber);
    void editMacroButton(int buttonNumber, QPushButton *btn);
    void setMacroButtonText(QString btnText, QPushButton *btn);
};

#endif // CWSENDER_H
