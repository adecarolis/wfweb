#include "logcategories.h"
#include "selectradio.h"
#include "ui_selectradio.h"


selectRadio::selectRadio(QWidget* parent) :
    QDialog(parent),
    ui(new Ui::selectRadio)
{
    ui->setupUi(this);
}

selectRadio::~selectRadio()
{
    delete ui;
}

void selectRadio::populate(QList<radio_cap_packet> radios)
{
    ui->table->clearContents();
    for (int row = 0; row < radios.count(); row++) {
        ui->table->insertRow(ui->table->rowCount());
        ui->table->setItem(ui->table->rowCount() - 1, 0, new QTableWidgetItem(QString(radios[row].name)));
        ui->table->setItem(ui->table->rowCount() - 1, 1, new QTableWidgetItem(QString("%1").arg((unsigned char)radios[row].civ, 2, 16, QLatin1Char('0')).toUpper()));
        ui->table->setItem(ui->table->rowCount() - 1, 2, new QTableWidgetItem(QString::number(qFromBigEndian(radios[row].baudrate))));
    }
}

void selectRadio::setInUse(int radio, QString user, QString ip)
{
    ui->table->setItem(radio, 3, new QTableWidgetItem(user));
    ui->table->setItem(radio, 4, new QTableWidgetItem(ip));
}

void selectRadio::on_table_cellClicked(int row, int col) {
    qInfo() << "Clicked on " << row << "," << col;
    ui->table->selectRow(row);
    emit selectedRadio(row);
}
void selectRadio::on_table_sectionClicked(int index) {
    qInfo() << "Section Clicked" << index;
}
