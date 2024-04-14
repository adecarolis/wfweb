#include "firsttimesetup.h"
#include "ui_firsttimesetup.h"

FirstTimeSetup::FirstTimeSetup(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::FirstTimeSetup)
{
    ui->setupUi(this);

    setWindowFlags(Qt::Dialog
        | Qt::FramelessWindowHint);

    this->setupState = setupInitial;
    ui->step2GroupBox->setHidden(true);
    ui->backBtn->setHidden(true);

    serialText1 = QString("Serial Port Name");
    serialText2 = QString("Baud Rate");
    serialText3 = QString("");

    networkText1 = QString("Radio IP address, UDP Port Numbers");
    networkText2 = QString("Radio Username, Radio Password");
    networkText2 = QString("Mic and Speaker on THIS PC");
}

FirstTimeSetup::~FirstTimeSetup()
{
    delete ui;
}

void FirstTimeSetup::on_exitProgramBtn_clicked()
{
    emit exitProgram();
    this->close();
}


void FirstTimeSetup::on_nextBtn_clicked()
{
    switch (setupState) {
    case setupInitial:
        // go to step 2:
        ui->nextBtn->setText("Finish");
        ui->step1GroupBox->setHidden(true);
        ui->step2GroupBox->setHidden(false);
        if(isNetwork) {
            ui->neededDetailsLabel1->setText(networkText1);
            ui->neededDetailsLabel2->setText(networkText2);
            ui->neededDetailsLabel3->setText(networkText3);
        } else {
            ui->neededDetailsLabel1->setText(serialText1);
            ui->neededDetailsLabel2->setText(serialText2);
            ui->neededDetailsLabel3->setText(serialText3);
        }
        ui->backBtn->setHidden(false);
        setupState = setupStep2;
        break;
    case setupStep2:
        // Done
        emit showSettings(isNetwork);
        this->close();
        break;
    default:
        break;
    }
}

void FirstTimeSetup::on_onMyOwnBtn_clicked()
{
    emit skipSetup();
    this->close();
}


void FirstTimeSetup::on_ethernetNetwork_clicked(bool checked)
{
    this->isNetwork = checked;
}


void FirstTimeSetup::on_WiFiNetwork_clicked(bool checked)
{
    this->isNetwork = checked;
}


void FirstTimeSetup::on_USBPortThisPC_clicked(bool checked)
{
    this->isNetwork = !checked;
}


void FirstTimeSetup::on_serialPortThisPC_clicked(bool checked)
{
    this->isNetwork = !checked;
}


void FirstTimeSetup::on_backBtn_clicked()
{
    setupState = setupInitial;
    ui->nextBtn->setText("Next");
    ui->step1GroupBox->setHidden(false);
    ui->step2GroupBox->setHidden(true);
    ui->backBtn->setHidden(true);
}

