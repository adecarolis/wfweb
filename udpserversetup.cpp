#include "udpserversetup.h"
#include "ui_udpserversetup.h"

udpServerSetup::udpServerSetup(QWidget* parent) :
    QDialog(parent),
    ui(new Ui::udpServerSetup)
{
    ui->setupUi(this);
    ui->enableCheckbox->setChecked(true);
}

udpServerSetup::~udpServerSetup()
{
    delete ui;
}
