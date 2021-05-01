#include "satellitesetup.h"
#include "ui_satellitesetup.h"
#include "logcategories.h"

satelliteSetup::satelliteSetup(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::satelliteSetup)
{
    ui->setupUi(this);
}

satelliteSetup::~satelliteSetup()
{
    delete ui;
}
