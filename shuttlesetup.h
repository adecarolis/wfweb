#ifndef SHUTTLESETUP_H
#define SHUTTLESETUP_H

#include <QDialog>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsPixmapItem>
#include <QPoint>
#include <QGraphicsSceneMouseEvent>
#include <QVector.h>
#include <QRect.h>
#include <QComboBox.h>
#include <QLabel.h>
#include <QGraphicsProxyWidget.h>

#include <QDebug>
#include <qobject.h>

#include "usbcontroller.h"



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
    void newDevice(unsigned char devType, QVector<BUTTON>* but);
    void mousePressed(QPoint p);
    void onEventIndexChanged(int index);
    void offEventIndexChanged(int index);

private:
    enum { NONE, shuttleXpress, shuttlePro2, RC28 }usbDevice;
    Ui::shuttleSetup* ui;
    QGraphicsScene* scene;
    QGraphicsTextItem* textItem;
    QGraphicsItem* bgImage = Q_NULLPTR;
    QLabel* imgLabel;
    unsigned char currentDevice = 0;
    QVector<BUTTON>* buttons;
    BUTTON* currentButton=Q_NULLPTR;
    QComboBox onEvent;
    QComboBox offEvent;
    QGraphicsProxyWidget* onEventProxy=Q_NULLPTR;
    QGraphicsProxyWidget* offEventProxy=Q_NULLPTR;


    QStringList onEventCommands = {"None", "PTT Toggle", "PTT On", "Tune","Step+","Step-", "NR","NB","AGC","Mode+","Mode-","Band+", "Band-",
        "23cm","70cm","2m","AIR","WFM","4m","6m","10m","12m","15m","17m","20m","30m","40m","60m","80m","160m","630m","2200m","GEN" };

    QStringList offEventCommands = { "None", "PTT Toggle", "PTT Off",
    "23cm", "70cm", "2m", "AIR","WFM","4m", "6m", "10m", "12m", "15m","17m", "20m", "30m", "40m", "60m", "80m", "160m","630m","2200m","GEN"};

};



class shuttleScene : public QGraphicsScene
{
    Q_OBJECT
        QGraphicsLineItem* item = Q_NULLPTR;

signals:
    void mousePressed(QPoint p);
protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) {

        if (event->button() == Qt::RightButton)
        {
            emit mousePressed(event->scenePos().toPoint());
        }
        else
        {
            QGraphicsScene::mousePressEvent(event);
        }
    }

    /*
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) {

    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {

    }
    */

};



#endif