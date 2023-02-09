#ifndef BANDBUTTONS_H
#define BANDBUTTONS_H

#include <QWidget>

namespace Ui {
class bandbuttons;
}

class bandbuttons : public QWidget
{
    Q_OBJECT

public:
    explicit bandbuttons(QWidget *parent = nullptr);
    ~bandbuttons();

private slots:
    void on_band2200mbtn_clicked();

    void on_band630mbtn_clicked();

    void on_band160mbtn_clicked();

    void on_band80mbtn_clicked();

    void on_band60mbtn_clicked();

    void on_band40mbtn_clicked();

    void on_band30mbtn_clicked();

    void on_band20mbtn_clicked();

    void on_band17mbtn_clicked();

    void on_band15mbtn_clicked();

    void on_band12mbtn_clicked();

    void on_band10mbtn_clicked();

    void on_band6mbtn_clicked();

    void on_band4mbtn_clicked();

    void on_band2mbtn_clicked();

    void on_band70cmbtn_clicked();

    void on_band23cmbtn_clicked();

    void on_bandWFMbtn_clicked();

    void on_bandAirbtn_clicked();

    void on_bandGenbtn_clicked();

private:
    Ui::bandbuttons *ui;
    void bandStackBtnClick();

    char bandStkBand;
    char bandStkRegCode;
};

#endif // BANDBUTTONS_H
