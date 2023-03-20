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
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QScopedPointer>
#include <QCheckBox>

#include <QDebug>
#include <QObject>
#include <QColorDialog>
#include <QWidget>
#include <QSpinBox>

#include "usbcontroller.h"


class controllerScene : public QGraphicsScene
{
    Q_OBJECT
        QGraphicsLineItem* item = Q_NULLPTR;

signals:
    void mousePressed(controllerScene* scene, QPoint p);
protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) {

        if (event->button() == Qt::RightButton)
        {
            emit mousePressed(this, event->scenePos().toPoint());
        }
        else
        {
            QGraphicsScene::mousePressEvent(event);
        }
    }
};


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
    void programButton(QString path, quint8 but, QString text);
    void programSensitivity(QString path, quint8 level);
    void programBrightness(QString path, quint8 level);
    void programWheelColour(QString path, quint8 r, quint8 g, quint8 b);
    void programOverlay(QString path, quint8 duration, QString text);
    void programOrientation(QString path, quint8 value);
    void programSpeed(QString path, quint8 value);
    void programTimeout(QString path, quint8 value);
    void programDisable(QString path, bool disable);

public slots:
    void newDevice(USBDEVICE* dev, CONTROLLER* cntrl, QVector<BUTTON>* but, QVector<KNOB>* kb, QVector<COMMAND>* cmd, QMutex* mut);
    void removeDevice(USBDEVICE* dev);
    void mousePressed(controllerScene *scene,QPoint p);
    void onEventIndexChanged(int index);
    void offEventIndexChanged(int index);
    void knobEventIndexChanged(int index);
    void sensitivityMoved(QString path, int val);
    void brightnessChanged(QString path, int index);
    void orientationChanged(QString path, int index);
    void speedChanged(QString path, int index);
    void colorPicker(QString path);
    void timeoutChanged(QString path, int val);
    void disableClicked(QString path, bool clicked, QWidget* widget);

private:

    usbDeviceType usbDevice = usbNone;
    Ui::controllerSetup* ui;
    QGraphicsTextItem* textItem;
    QLabel* imgLabel;
    unsigned char currentDevice = 0;
    QVector<BUTTON>* buttons;
    QVector<KNOB>* knobs;
    QVector<COMMAND>* commands;
    usbMap* controllers;
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

    QLabel* noControllersText;

    int numTabs=0;

    // Below are used for each tab:
    /*
    QList<QWidget *> tabs;
    QList<QVBoxLayout *> layouts;
    QList<QWidget *> widgets;
    QList<QGraphicsView *> graphicsViews;
    QList<QGraphicsScene*> scenes;
    QList<QGraphicsItem*> bgImages;
    QList<QSlider *>sensitivitys;

    // Just used for QuickKeys device
    QList<QComboBox *>brightCombos;
    QList<QComboBox *>speedCombos;
    QList<QComboBox *>orientCombos;
    QList<QPushButton *>colorButtons;
    QList<QSpinBox *>timeoutSpins;
*/
};





#endif
