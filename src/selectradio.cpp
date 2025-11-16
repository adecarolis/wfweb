#include "logcategories.h"
#include "selectradio.h"
#include "ui_selectradio.h"


selectRadio::selectRadio(QWidget* parent) :
    QDialog(parent),
    ui(new Ui::selectRadio)
{
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    f.setBold(true);
    f.setPointSize(8);
    ui->setupUi(this);
    ui->timeDifference->plotLayout()->insertRow(0);
    QCPTextElement *title = new QCPTextElement(ui->timeDifference, "UDP time difference", f);
    ui->timeDifference->plotLayout()->addElement(0, 0, title);
    ui->timeDifference->addGraph(0);
    ui->timeDifference->yAxis->setLabel("ms");
    ui->timeDifference->yAxis->setRange(-10,10);
    ui->timeDifference->xAxis->setRange(0,100);
    ui->timeDifference->xAxis->setTickLabels(false);

    ui->waterfall->plotLayout()->insertRow(0);
    QCPTextElement *wfTitle = new QCPTextElement(ui->waterfall, "Waterfall plot time", f);
    ui->waterfall->plotLayout()->addElement(0, 0, wfTitle);
    ui->waterfall->addGraph(0);
    ui->waterfall->yAxis->setLabel("ms");
    ui->waterfall->yAxis->setRange(-10,10);
    ui->waterfall->xAxis->setRange(0,1000);
    ui->waterfall->xAxis->setTickLabels(false);

    ui->spectrum->plotLayout()->insertRow(0);
    QCPTextElement *spTitle = new QCPTextElement(ui->spectrum, "Spectrum plot time", f);
    ui->spectrum->plotLayout()->addElement(0, 0, spTitle);
    ui->spectrum->addGraph(0);
    ui->spectrum->yAxis->setLabel("ms");
    ui->spectrum->yAxis->setRange(0,50);
    ui->spectrum->xAxis->setRange(0,1000);
    ui->spectrum->xAxis->setTickLabels(false);
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
        ui->table->setItem(row, 1, new QTableWidgetItem(QString("0x%1").arg((uchar)radios[row].civ, 2, 16, QLatin1Char('0')).toUpper()));
        ui->table->setItem(row, 2, new QTableWidgetItem(QString::number(qFromBigEndian(radios[row].baudrate))));
    }
    if (radios.count() > 1) {
        this->setVisible(true);
    }
}

void selectRadio::setInUse(quint8 radio, bool admin, quint8 busy, QString user, QString ip)
{
    Q_UNUSED(admin)
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
    //qInfo() << "Clicked on " << row << "," << col;
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

void selectRadio::addTimeDifference(qint64 time) {
    auto data = ui->timeDifference->graph(0)->data();

    static int counter=0;
    if (data->size() >= 100) {
        data->remove(counter-100);
        ui->timeDifference->xAxis->rescale();
    }

    ui->timeDifference->graph(0)->addData(counter,time);
    ui->timeDifference->yAxis->rescale();

    if (this->isVisible()) {
        ui->timeDifference->replot();
    }
    counter++;
}

void selectRadio::waterfallTime(double time) {
    auto data = ui->waterfall->graph(0)->data();

    static int counter=0;
    if (data->size() >= 1000) {
        data->remove(counter-1000);
        ui->waterfall->xAxis->rescale();
    }

    ui->waterfall->graph(0)->addData(counter,time);

    if (this->isVisible()) {
        ui->waterfall->yAxis->rescale();
        ui->waterfall->replot();
    }
    counter++;
}

void selectRadio::spectrumTime(double time) {
    //qInfo() << "Adding time difference" << time;
    auto data = ui->spectrum->graph(0)->data();

    static int counter=0;
    if (data->size() >= 1000) {
        data->remove(counter-1000);
        ui->spectrum->xAxis->rescale();
    }

    ui->spectrum->graph(0)->addData(counter,time);

    if (this->isVisible()) {
        ui->spectrum->yAxis->rescale();
        ui->spectrum->replot();
    }
    counter++;
}

