#ifndef FIRSTTIMESETUP_H
#define FIRSTTIMESETUP_H

// include <QWidget>
#include <QDialog>

namespace Ui {
class FirstTimeSetup;
}

class FirstTimeSetup : public QDialog
{
    Q_OBJECT

public:
    explicit FirstTimeSetup(QWidget *parent = nullptr);
    ~FirstTimeSetup();

private:
    Ui::FirstTimeSetup *ui;

    enum setupState_t {
        setupInitial,
        setupStep2
    } setupState;
    bool isNetwork = true;
    QString serialText1;
    QString serialText2;
    QString serialText3;

    QString networkText1;
    QString networkText2;
    QString networkText3;

signals:
    void exitProgram();
    void showSettings(bool isNetwork);
    void skipSetup();
private slots:
    void on_exitProgramBtn_clicked();
    void on_nextBtn_clicked();
    void on_onMyOwnBtn_clicked();
    void on_ethernetNetwork_clicked(bool checked);
    void on_WiFiNetwork_clicked(bool checked);
    void on_USBPortThisPC_clicked(bool checked);
    void on_serialPortThisPC_clicked(bool checked);
    void on_backBtn_clicked();
};

#endif // FIRSTTIMESETUP_H
