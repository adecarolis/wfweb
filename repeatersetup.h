#ifndef REPEATERSETUP_H
#define REPEATERSETUP_H

#include <QMainWindow>

#include "repeaterattributes.h"

namespace Ui {
class repeaterSetup;
}

class repeaterSetup : public QMainWindow
{
    Q_OBJECT

public:
    explicit repeaterSetup(QWidget *parent = 0);
    ~repeaterSetup();

signals:
    void getDuplexMode();
    void setDuplexMode(duplexMode dm);

public slots:
    void receiveDuplexMode(duplexMode dm);

private slots:


    void on_rptSimplexBtn_clicked();

    void on_rptDupPlusBtn_clicked();

    void on_rptDupMinusBtn_clicked();

    void on_rptAutoBtn_clicked();

    void on_rptReadRigBtn_clicked();

private:
    Ui::repeaterSetup *ui;

    duplexMode currentdm;
};

#endif // REPEATERSETUP_H
