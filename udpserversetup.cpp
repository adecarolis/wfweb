#include "udpserversetup.h"
#include "ui_udpserversetup.h"
#include "logcategories.h"

extern void passcode(QString in,QByteArray& out);

udpServerSetup::udpServerSetup(QWidget* parent) :
    QDialog(parent),
    ui(new Ui::udpServerSetup)
{
    ui->setupUi(this);
    addUserLine("", "", 0); // Create a blank row if we never receive config.

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
    qInfo() << "Getting server config";

    ui->enableCheckbox->setChecked(conf.enabled);
    ui->controlPortText->setText(QString::number(conf.controlPort));
    ui->civPortText->setText(QString::number(conf.civPort));
    ui->audioPortText->setText(QString::number(conf.audioPort));

    int row = 0;
  
    for (int i = 0; i < ui->usersTable->rowCount(); i++)
    {
        ui->usersTable->removeRow(i);
    }

    foreach  (SERVERUSER user, conf.users)
    {
        if (user.username != "" && user.password != "")
        {
            addUserLine(user.username, user.password, user.userType);
            row++;
        }
    }

    if (row == 0) {
        addUserLine("", "", 0);
    }

}

void udpServerSetup::accept() 
{
    qInfo() << "Server config stored";
    SERVERCONFIG config;
    config.enabled = ui->enableCheckbox->isChecked();
    config.controlPort = ui->controlPortText->text().toInt();
    config.civPort = ui->civPortText->text().toInt();
    config.audioPort = ui->audioPortText->text().toInt();

    config.users.clear();

    for (int row = 0; row < ui->usersTable->model()->rowCount(); row++)
    {
        if (ui->usersTable->item(row, 0) != NULL)
        {
            SERVERUSER user;
            user.username = ui->usersTable->item(row, 0)->text();
            QLineEdit* password = (QLineEdit*)ui->usersTable->cellWidget(row, 1);
            user.password = password->text();
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
    qInfo() << "Clicked on " << row << "," << col;
    if (row == ui->usersTable->model()->rowCount() - 1 && ui->usersTable->item(row, 0) != NULL) {
        addUserLine("", "", 0);
    }
}

void udpServerSetup::onPasswordChanged()
{
    int row = sender()->property("row").toInt();
    QLineEdit* password = (QLineEdit*)ui->usersTable->cellWidget(row, 1);
    QByteArray pass;
    passcode(password->text(), pass);
    password->setText(pass);
    qInfo() << "password row" << row << "changed";
}

void udpServerSetup::addUserLine(const QString& user, const QString& pass, const int& type)
{
    ui->usersTable->insertRow(ui->usersTable->rowCount());
    ui->usersTable->setItem(ui->usersTable->rowCount() - 1, 0, new QTableWidgetItem(user));
    ui->usersTable->setItem(ui->usersTable->rowCount() - 1, 1, new QTableWidgetItem());
    ui->usersTable->setItem(ui->usersTable->rowCount() - 1, 2, new QTableWidgetItem());

    QLineEdit* password = new QLineEdit();
    password->setProperty("row", (int)ui->usersTable->rowCount() - 1);
    password->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    password->setText(pass);
    connect(password, SIGNAL(editingFinished()), this, SLOT(onPasswordChanged()));
    ui->usersTable->setCellWidget(ui->usersTable->rowCount() - 1, 1, password);

    QComboBox* comboBox = new QComboBox();
    comboBox->insertItems(0, { "Full User","Full with no TX","Monitor only" });
    comboBox->setCurrentIndex(type);
    ui->usersTable->setCellWidget(ui->usersTable->rowCount() - 1, 2, comboBox);
    
}
