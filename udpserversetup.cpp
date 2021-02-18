#include "udpserversetup.h"
#include "ui_udpserversetup.h"

udpServerSetup::udpServerSetup(QWidget* parent) :
    QDialog(parent),
    ui(new Ui::udpServerSetup)
{
    ui->setupUi(this);
    // Get any stored config information from the main form.
    SERVERCONFIG config;
    emit serverConfig(config,false); // Just send blank server config.
}

udpServerSetup::~udpServerSetup()
{
    delete ui;
}

// Slot to receive config.
void udpServerSetup::receiveServerConfig(SERVERCONFIG conf)
{
    qDebug() << "Getting server config";

    ui->enableCheckbox->setChecked(conf.enabled);
    ui->controlPortText->setText(QString::number(conf.controlPort));
    ui->civPortText->setText(QString::number(conf.civPort));
    ui->audioPortText->setText(QString::number(conf.audioPort));

    int row = 0;
    foreach  (SERVERUSER user, conf.users)
    {
        if (ui->usersTable->rowCount() <= row) {
            ui->usersTable->insertRow(ui->usersTable->rowCount());
        }
        ui->usersTable->setItem(row, 0, new QTableWidgetItem(user.username));
        ui->usersTable->setItem(row, 1, new QTableWidgetItem(user.password));
        row++;
    }
    // Delete any rows no longer needed
    for (int count = row; count < ui->usersTable->rowCount(); count++) 
    {
        ui->usersTable->removeRow(count);
    }
    ui->usersTable->insertRow(ui->usersTable->rowCount());
    //ui->usersTable->setHorizontalHeaderItem(ui->usersTable->rowCount() - 1, new QTableWidgetItem("User " + QString::number(row + 1)));

}

void udpServerSetup::accept() 
{
    qDebug() << "Server config stored";
    SERVERCONFIG config;
    config.enabled = ui->enableCheckbox->isChecked();
    config.controlPort = ui->controlPortText->text().toInt();
    config.civPort = ui->civPortText->text().toInt();
    config.audioPort = ui->audioPortText->text().toInt();

    config.users.clear();

    for (int row = 0; row < ui->usersTable->model()->rowCount(); row++)
    {
        if (ui->usersTable->item(row, 0) != NULL && ui->usersTable->item(row, 1) != NULL)
        {
            SERVERUSER user;
            user.username = ui->usersTable->item(row, 0)->text();
            user.password = ui->usersTable->item(row, 1)->text();
            config.users.append(user);
            
        }
        else {
            ui->usersTable->removeRow(row);
        }
    }

    emit serverConfig(config,true);
    this->hide();
}


void udpServerSetup::on_usersTable_cellClicked(int row, int col)
{
    qDebug() << "Clicked on " << row << "," << col;
    if (row == ui->usersTable->model()->rowCount() - 1 && ui->usersTable->item(row, 0) != NULL && ui->usersTable->item(row, 1) != NULL) {
        ui->usersTable->insertRow(ui->usersTable->rowCount());
    }

}