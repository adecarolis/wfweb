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
