#ifndef SHUTTLESETUP_H
#define SHUTTLESETUP_H

#include <QDialog>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsPixmapItem>

#include <QDebug>
#include <qobject.h>

#include "shuttle.h"

namespace Ui {
    class shuttleSetup;
}

class shuttleSetup : public QDialog
{
    Q_OBJECT

public:
    explicit shuttleSetup(QWidget* parent = 0);
    ~shuttleSetup();

public slots:
    void newDevice(unsigned char devType);


private:
    enum { NONE, shuttleXpress, shuttlePro2, RC28 }usbDevice;
    Ui::shuttleSetup* ui;
    QGraphicsScene* scene;
    QGraphicsTextItem* textItem;
    QGraphicsPixmapItem* bgImage = Q_NULLPTR;
};

#endif