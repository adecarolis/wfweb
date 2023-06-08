#include "debugwindow.h"
#include "ui_debugwindow.h"

#include "logcategories.h"

debugWindow::debugWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::debugWindow)
{
    ui->setupUi(this);
    this->setObjectName("Debug Window");
    qDebug() << "debugWindow() Creating new window";
    queue = cachingQueue::getInstance();
    ui->cacheView->setColumnWidth(0,20);
    ui->queueView->setColumnWidth(0,20);
    ui->cacheView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->queueView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    //QTimer::singleShot(100, this, &debugWindow::getCache);
    //QTimer::singleShot(300, this, &debugWindow::getQueue);
    cacheTimer.setInterval(ui->cacheInterval->text().toInt());
    queueTimer.setInterval(ui->queueInterval->text().toInt());
    connect(&cacheTimer,SIGNAL(timeout()),this,SLOT(getCache()));
    connect(&queueTimer,SIGNAL(timeout()),this,SLOT(getQueue()));
    cacheTimer.start();
    queueTimer.start();
}

debugWindow::~debugWindow()
{
    qDebug() << "debugWindow() destroying window";
    cacheTimer.stop();
    queueTimer.stop();
    delete ui;
}

void debugWindow::getCache()
{
    auto cacheItems = queue->getCacheItems();
    ui->cacheLabel->setText(QString("Current cache items in cachingView(%0)").arg(cacheItems.size()));
    int c=0;
    auto i = cacheItems.cbegin();
    while (i != cacheItems.cend())
    {
        if (c >= ui->cacheView->rowCount())
        {
            ui->cacheView->insertRow(ui->cacheView->rowCount());
            for (int j=0;j< ui->cacheView->columnCount();j++)
            {
                ui->cacheView->setItem(c,j,new QTableWidgetItem());
            }
        }
        //QString::number(i.value().command).rightJustified(3,'0'));
        ui->cacheView->item(c,0)->setText(QString::number(i.value().command).rightJustified(3,'0'));
        ui->cacheView->item(c,1)->setText(funcString[i.value().command]);
        ui->cacheView->item(c,2)->setText(getValue(i.value().value));
        ui->cacheView->item(c,3)->setText((i.value().sub)?"true":"false");
        ui->cacheView->item(c,4)->setText((i.value().req.isValid()?i.value().req.toString("hh:mm:ss.zzz"):"<none>"));
        ui->cacheView->item(c,5)->setText((i.value().reply.isValid()?i.value().reply.toString("hh:mm:ss.zzz"):"<none>"));
        c++;
        i++;
    }
    if (ui->cacheView->rowCount() > c)
        ui->cacheView->model()->removeRows(c,ui->cacheView->rowCount());
    queue->unlockMutex();
}

void debugWindow::getQueue()
{
    auto queueItems = queue->getQueueItems();
    ui->queueLabel->setText(QString("Current queue items in cachingView(%0)").arg(queueItems.size()));
    int c=0;
    auto i = queueItems.cbegin();
    while (i != queueItems.cend())
    {
        if (c >= ui->queueView->rowCount())
        {
            ui->queueView->insertRow(ui->queueView->rowCount());
            for (int j=0;j< ui->queueView->columnCount();j++)
            {
                ui->queueView->setItem(c,j,new QTableWidgetItem());
            }

        }
        ui->queueView->item(c,0)->setText(QString::number(i.value().command).rightJustified(3,'0'));
        ui->queueView->item(c,1)->setText(funcString[i.value().command]);
        ui->queueView->item(c,2)->setText(QString::number(i.key()));
        ui->queueView->item(c,3)->setText((i.value().param.isValid()?"Set":"Get"));
        ui->queueView->item(c,4)->setText(getValue(i.value().param));
        ui->queueView->item(c,5)->setText((i.value().recurring?"True":"False"));
        c++;
        i++;
    }
    ui->queueView->model()->removeRows(c,ui->queueView->rowCount());
    queue->unlockMutex();
}


QString debugWindow::getValue(QVariant val)
{
    QString value="";
    if (val.isValid()) {
        if (!strcmp(val.typeName(),"bool"))
        {
            value = (val.value<bool>()?"True":"False");
        }
        else if (!strcmp(val.typeName(),"uchar"))
        {
            value = QString("uchar: %0").arg(val.value<uchar>());
        }
        else if (!strcmp(val.typeName(),"ushort"))
        {
            value = QString("ushort: %0").arg(val.value<ushort>());
        }
        else if (!strcmp(val.typeName(),"short"))
        {
            value = QString("short: %0").arg(val.value<short>());
        }
        else if (!strcmp(val.typeName(),"uint"))
        {
            value = QString("Gr: %0 Me: %1").arg(val.value<uint>() >> 16 & 0xffff).arg(val.value<uint>() & 0xffff);
        }
        else if (!strcmp(val.typeName(),"modeInfo"))
        {
            modeInfo mi = val.value<modeInfo>();
            value = QString("%0(V:%1) D:%2 F%3").arg(mi.name).arg(mi.VFO).arg(mi.data).arg(mi.filter);
        }
        else if(!strcmp(val.typeName(),"freqt"))
        {
            freqt f = val.value<freqt>();
            value = QString("(V:%0) %1 Mhz").arg(f.VFO).arg(f.MHzDouble);
        }
        else if(!strcmp(val.typeName(),"scopeData"))
        {
            scopeData s = val.value<scopeData>();
            value = QString("(V:%0) %1").arg(s.mainSub).arg((s.valid?"Valid":"Invalid"));
        }
        else if (!strcmp(val.typeName(),"antennaInfo"))
        {
            antennaInfo a = val.value<antennaInfo>();
            value = QString("Ant: %0 RX: %1").arg(a.antenna).arg(a.rx);
        }
        else if (!strcmp(val.typeName(),"memoryType"))
        {
            memoryType m = val.value<memoryType>();
            value = QString("Mem:%0 G:%1  %2").arg(m.name).arg(m.group).arg(m.channel);
        }
        else if (!strcmp(val.typeName(),"rigInput"))
        {
            rigInput i = val.value<rigInput>();
            value = QString("Input:%0 R:%1 (%2)").arg(i.name).arg(i.reg).arg(i.type);
        }
        else
        {
            value = QString("%0: <nosup>").arg(val.typeName());
        }

    }
    return value;
}


void debugWindow::on_cachePause_clicked(bool checked)
{
    if (checked)
        cacheTimer.stop();
    else
        cacheTimer.start();
}

void debugWindow::on_queuePause_clicked(bool checked)
{
    if (checked)
        queueTimer.stop();
    else
        queueTimer.start();
}


void debugWindow::on_cacheInterval_textChanged(QString text)
{
    cacheTimer.setInterval(text.toInt());
}

void debugWindow::on_queueInterval_textChanged(QString text)
{
    queueTimer.setInterval(text.toInt());
}
