#ifndef CONTROLLERSETUP_H
#define CONTROLLERSETUP_H

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
#include <QAbstractItemView>

#include <QDebug>
#include <QObject>

#include "usbcontroller.h"



namespace Ui {
    class controllerSetup;
}

class controllerSetup : public QDialog
{
    Q_OBJECT

public:
    explicit controllerSetup(QWidget* parent = 0);
    ~controllerSetup();

signals:
    void sendSensitivity(int val);

public slots:
    void newDevice(unsigned char devType, QVector<BUTTON>* but, QVector<KNOB>* kb, QVector<COMMAND>* cmd);
    void mousePressed(QPoint p);
    void onEventIndexChanged(int index);
    void offEventIndexChanged(int index);
    void knobEventIndexChanged(int index);
    void receiveSensitivity(int val);
    void on_sensitivitySlider_valueChanged(int val);

private:

    usbDeviceType usbDevice = usbNone;
    Ui::controllerSetup* ui;
    QGraphicsScene* scene;
    QGraphicsTextItem* textItem;
    QGraphicsItem* bgImage = Q_NULLPTR;
    QLabel* imgLabel;
    unsigned char currentDevice = 0;
    QVector<BUTTON>* buttons;
    QVector<KNOB>* knobs;
    QVector<COMMAND>* commands;
    BUTTON* currentButton = Q_NULLPTR;
    KNOB* currentKnob = Q_NULLPTR;
    QComboBox onEvent;
    QComboBox offEvent;
    QComboBox knobEvent;
    QGraphicsProxyWidget* onEventProxy = Q_NULLPTR;
    QGraphicsProxyWidget* offEventProxy = Q_NULLPTR;
    QGraphicsProxyWidget* knobEventProxy = Q_NULLPTR;
    QString deviceName;

};



class controllerScene : public QGraphicsScene
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