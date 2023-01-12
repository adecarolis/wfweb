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
    for (int row = ui->table->rowCount() - 1;row>=0; row--) {
        ui->table->removeRow(row);
    }

    for (int row = 0; row < radios.count(); row++) {
        ui->table->insertRow(ui->table->rowCount());
        ui->table->setItem(row, 0, new QTableWidgetItem(QString(radios[row].name)));
        ui->table->setItem(row, 1, new QTableWidgetItem(QString("%1").arg((unsigned char)radios[row].civ, 2, 16, QLatin1Char('0')).toUpper()));
        ui->table->setItem(row, 2, new QTableWidgetItem(QString::number(qFromBigEndian(radios[row].baudrate))));
    }
    if (radios.count() > 1) {
        this->setVisible(true);
    }
}

void selectRadio::setInUse(quint8 radio, quint8 busy, QString user, QString ip)
{
    //if ((radio > 0)&& !this->isVisible()) {
    //    qInfo() << "setInUse: radio:" << radio <<"busy" << busy << "user" << user << "ip"<<ip;
    //    this->setVisible(true);
    //}
    ui->table->setItem(radio, 3, new QTableWidgetItem(user));
    ui->table->setItem(radio, 4, new QTableWidgetItem(ip));
    for (int f = 0; f < 5; f++) {
        if (busy == 1) 
        {
            ui->table->item(radio, f)->setBackground(Qt::darkGreen);
        }
        else if (busy == 2) 
        {
            ui->table->item(radio, f)->setBackground(Qt::red);
        }
        else
        {
            ui->table->item(radio, f)->setBackground(Qt::black);
        }
    }

}

void selectRadio::on_table_cellClicked(int row, int col) {
    qInfo() << "Clicked on " << row << "," << col;
#if (QT_VERSION < QT_VERSION_CHECK(5,11,0))
    if (ui->table->item(row, col)->backgroundColor() != Qt::darkGreen) {
#else
    if (ui->table->item(row, col)->background() != Qt::darkGreen) {
#endif
        ui->table->selectRow(row);
        emit selectedRadio(row);
        this->setVisible(false);
    }
}


void selectRadio::on_cancelButton_clicked() {
    this->setVisible(false);
}

void selectRadio::audioOutputLevel(quint16 level) {
    ui->afLevel->setValue(level);
}

void selectRadio::audioInputLevel(quint16 level) {
    ui->modLevel->setValue(level);
}
