#ifndef SHUTTLESETUP_H
#define SHUTTLESETUP_H

#include <QDialog>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsPixmapItem>
#include <QPoint>
#include <QGraphicsSceneMouseEvent>
#include <QVector>
#include <QRect>
#include <QComboBox>
#include <QLabel>
#include <QGraphicsProxyWidget>

#include <QDebug>
#include <QObject>

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

signals:


public slots:
    void newDevice(unsigned char devType, QVector<BUTTON>* but, QVector<COMMAND>* cmd);
    void mousePressed(QPoint p);
    void onEventIndexChanged(int index);
    void offEventIndexChanged(int index);

private:
    enum { NONE=0, shuttleXpress, shuttlePro2, RC28 } usbDevice;

    Ui::shuttleSetup* ui;
    QGraphicsScene* scene;
    QGraphicsTextItem* textItem;
    QGraphicsItem* bgImage = Q_NULLPTR;
    QLabel* imgLabel;
    unsigned char currentDevice = 0;
    QVector<BUTTON>* buttons;
    QVector<COMMAND>* commands;
    BUTTON* currentButton=Q_NULLPTR;
    QComboBox onEvent;
    QComboBox offEvent;
    QGraphicsProxyWidget* onEventProxy=Q_NULLPTR;
    QGraphicsProxyWidget* offEventProxy=Q_NULLPTR;
    QString deviceName;

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