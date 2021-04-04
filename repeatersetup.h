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
    void setTone(quint16 tone);
    void setTSQL(quint16 tsql);
    void setDTCS(quint16 dcode, bool tinv, bool rinv);
    void getTone();
    void getTSQL();
    void getDTCS();
    void setRptAccessMode(rptAccessTxRx tmode);
    void getRptAccessMode();

public slots:
    void receiveDuplexMode(duplexMode dm);
    void handleRptAccessMode(rptAccessTxRx tmode);
    void handleTone(quint16 tone);
    void handleTSQL(quint16 tsql);
    void handleDTCS(quint16 dcscode, bool tinv, bool rinv);

private slots:
    void on_rptSimplexBtn_clicked();
    void on_rptDupPlusBtn_clicked();
    void on_rptDupMinusBtn_clicked();
    void on_rptAutoBtn_clicked();
    void on_rptReadRigBtn_clicked();
    void on_rptToneCombo_activated(int index);
    void on_rptDTCSCombo_activated(int index);

    void on_debugBtn_clicked();

private:
    Ui::repeaterSetup *ui;

    void populateTones();
    void populateDTCS();

    duplexMode currentdm;
};

#endif // REPEATERSETUP_H
