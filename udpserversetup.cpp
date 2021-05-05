#include "udpserversetup.h"
#include "ui_udpserversetup.h"
#include "logcategories.h"

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
        if (user.username != "" && user.password != "")
        {
            if (ui->usersTable->rowCount() <= row) {
                ui->usersTable->insertRow(ui->usersTable->rowCount());
            }
            ui->usersTable->setItem(row, 0, new QTableWidgetItem(user.username));
            ui->usersTable->setItem(row, 1, new QTableWidgetItem(user.password));
            QComboBox* comboBox = new QComboBox();
            comboBox->insertItems(0, { "Full User","Full with no TX","Monitor only" });
            comboBox->setCurrentIndex(user.userType);
            ui->usersTable->setCellWidget(row, 2, comboBox);
            row++;
        }
    }
    // Delete any rows no longer needed
    int count=0;

    for (count = row; count <= ui->usersTable->rowCount(); count++)
    {
        if (count == conf.users.count()) {
            ui->usersTable->insertRow(ui->usersTable->rowCount());
            QComboBox* comboBox = new QComboBox();
            comboBox->insertItems(0, { "Full User","Full with no TX","Monitor only" });
            ui->usersTable->setCellWidget(count, 2, comboBox);
        }
        else if (count > conf.users.count()) {
            ui->usersTable->removeRow(count);
        }
    }

    if (count == 0) {
        ui->usersTable->insertRow(ui->usersTable->rowCount());
        QComboBox* comboBox = new QComboBox();
        comboBox->insertItems(0, { "Full User","Full with no TX","Monitor only" });
        ui->usersTable->setCellWidget(count, 2, comboBox);
    }

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
            QComboBox* comboBox = (QComboBox*)ui->usersTable->cellWidget(row, 2);
            user.userType = comboBox->currentIndex();
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
        QComboBox* comboBox = new QComboBox();
        comboBox->insertItems(0, { "Full User","Full with no TX","Monitor only" });
        userTypes.append(comboBox);
        ui->usersTable->setCellWidget(ui->usersTable->rowCount() - 1, 2, comboBox);

    }

}