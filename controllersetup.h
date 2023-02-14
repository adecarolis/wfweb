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
#include <QColorDialog>

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
    void programButton(quint8 but, QString text);
    void programBrightness(quint8 level);
    void programWheelColour(quint8 r, quint8 g, quint8 b);
    void programOverlay(quint8 duration, QString text);
    void programOrientation(quint8 value);
    void programSpeed(quint8 value);
    void programTimeout(quint8 value);
    void updateSettings(quint8 bright, quint8 orient, quint8 speed, quint8 timeout, QColor color);

public slots:
    void newDevice(unsigned char devType, QVector<BUTTON>* but, QVector<KNOB>* kb, QVector<COMMAND>* cmd, QMutex* mut);
    void mousePressed(QPoint p);
    void onEventIndexChanged(int index);
    void offEventIndexChanged(int index);
    void knobEventIndexChanged(int index);
    void receiveSensitivity(int val);
    void on_sensitivitySlider_valueChanged(int val);
    void on_qkBrightCombo_currentIndexChanged(int index);
    void on_qkOrientCombo_currentIndexChanged(int index);
    void on_qkSpeedCombo_currentIndexChanged(int index);
    void on_qkColorButton_clicked();
    void on_qkTimeoutSpin_valueChanged(int arg1);
    void setDefaults(quint8 bright, quint8 orient, quint8 speed, quint8 timeout, QColor color);

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
    QComboBox* onEvent = Q_NULLPTR;
    QComboBox* offEvent = Q_NULLPTR;
    QComboBox* knobEvent = Q_NULLPTR;
    QComboBox* qkBright = Q_NULLPTR;
    QGraphicsProxyWidget* onEventProxy = Q_NULLPTR;
    QGraphicsProxyWidget* offEventProxy = Q_NULLPTR;
    QGraphicsProxyWidget* knobEventProxy = Q_NULLPTR;
    QGraphicsProxyWidget* qkBrightProxy = Q_NULLPTR;
    QString deviceName;
    QMutex* mutex;
    QColor initialColor = Qt::white;
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
};



#endif