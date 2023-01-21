#ifndef CWSENDER_H
#define CWSENDER_H

#include <QMainWindow>
#include <QString>
#include <QFont>
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
signals:
    void sendCW(QString cwMessage);
    void stopCW();
    void setKeySpeed(unsigned char wpm);
    void setBreakInMode(unsigned char b);

private slots:
    void on_sendBtn_clicked();

    void on_stopBtn_clicked();

    void on_textToSendEdit_returnPressed();

    void on_breakinCombo_activated(int index);

    void on_wpmSpin_valueChanged(int arg1);

private:
    Ui::cwSender *ui;
};

#endif // CWSENDER_H
