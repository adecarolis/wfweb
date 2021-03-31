#include "repeatersetup.h"
#include "ui_repeatersetup.h"

repeaterSetup::repeaterSetup(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::repeaterSetup)
{
    ui->setupUi(this);
}

repeaterSetup::~repeaterSetup()
{
    delete ui;
}

void repeaterSetup::receiveDuplexMode(duplexMode dm)
{
    switch(dm)
    {
        case dmSimplex:
            ui->rptSimplexBtn->setChecked(true);
            break;
        case dmDupPlus:
            ui->rptDupPlusBtn->setChecked(true);
            break;
        case dmDupMinus:
            ui->rptDupMinusBtn->setChecked(true);
            break;
        default:
            break;
    }
}

void repeaterSetup::on_rptSimplexBtn_clicked()
{
    // Simplex
    emit setDuplexMode(dmDupAutoOff);
    emit setDuplexMode(dmSimplex);
}

void repeaterSetup::on_rptDupPlusBtn_clicked()
{
    // DUP+
    emit setDuplexMode(dmDupAutoOff);
    emit setDuplexMode(dmDupPlus);
}

void repeaterSetup::on_rptDupMinusBtn_clicked()
{
    // DUP-
    emit setDuplexMode(dmDupAutoOff);
    emit setDuplexMode(dmDupMinus);
}

void repeaterSetup::on_rptAutoBtn_clicked()
{
    // Auto Rptr (enable this feature)
    // TODO: Hide an AutoOff button somewhere for non-US users
    emit setDuplexMode(dmDupAutoOn);
}
