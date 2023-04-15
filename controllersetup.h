#ifndef CONTROLLERSETUP_H
#define CONTROLLERSETUP_H

#include <QDialog>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
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
#include <QFileDialog>
#include <QMessageBox>
#include <QLayoutItem>

#include <QDebug>
#include <QObject>
#include <QColorDialog>
#include <QWidget>
#include <QSpinBox>
#include <QCheckBox>

#include "usbcontroller.h"


class controllerScene : public QGraphicsScene
{
    Q_OBJECT
        QGraphicsLineItem* item = Q_NULLPTR;

signals:
        void showMenu(controllerScene* scene, QPoint p);
        void buttonAction(bool pressed, QPoint p);
protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) {

        if (event->button() == Qt::RightButton)
        {
            emit showMenu(this, event->scenePos().toPoint());
        }
        else if (event->button() == Qt::LeftButton)
        {
            // Simulate a button press
            emit buttonAction(true,event->scenePos().toPoint());
        }
        else
        {
            QGraphicsScene::mousePressEvent(event);
        }
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {

        if (event->button() == Qt::LeftButton)
        {
            // Simulate a button release
            emit buttonAction(false,event->scenePos().toPoint());
        }
        else
        {
            QGraphicsScene::mouseReleaseEvent(event);
        }
    }
};


struct tabContent {
    QWidget* tab;
    QVBoxLayout* mainLayout;
    QHBoxLayout* topLayout;
    QWidget* widget;
    QVBoxLayout* layout;
    QCheckBox* disabled;
    QLabel* message;
    QGraphicsView* view;
    QLabel* pageLabel;
    QSpinBox* page;
    QHBoxLayout* sensLayout;
    QLabel* sensLabel;
    QSlider* sens;
    QImage* image;
    QGraphicsItem* bgImage = Q_NULLPTR;
    controllerScene* scene = Q_NULLPTR;
    QGridLayout* grid;
    QLabel* brightLabel;
    QComboBox* brightness;
    QLabel* speedLabel;
    QComboBox* speed;
    QLabel* orientLabel;
    QComboBox* orientation;
    QLabel* colorLabel;
    QPushButton* color;
    QLabel* timeoutLabel;
    QSpinBox* timeout;
    QLabel* pagesLabel;
    QSpinBox* pages;
    QLabel* helpText;
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
    void started();
    void sendRequest(USBDEVICE* dev, usbFeatureType request, int val=0, QString text="", QImage* img=Q_NULLPTR, QColor* color=Q_NULLPTR);
    void programDisable(USBDEVICE* dev, bool disable);
    void programPages(USBDEVICE* dev, int pages);
    void backup(USBDEVICE* dev, QString path);
    void restore(USBDEVICE *dev, QString path);

public slots:
    void init(usbDevMap* dev, QVector<BUTTON>* but, QVector<KNOB>* kb, QVector<COMMAND>* cmd, QMutex* mut);
    void newDevice(USBDEVICE* dev);
    void removeDevice(USBDEVICE* dev);
    void showMenu(controllerScene *scene,QPoint p);
    void onEventIndexChanged(int index);
    void offEventIndexChanged(int index);
    void knobEventIndexChanged(int index);
    void ledNumberChanged(int index);
    void sensitivityMoved(USBDEVICE* dev, int val);
    void brightnessChanged(USBDEVICE* dev, int index);
    void orientationChanged(USBDEVICE* dev, int index);
    void speedChanged(USBDEVICE* dev, int index);
    void colorPicker(USBDEVICE* dev, QPushButton* btn, QColor color);
    void buttonOnColorClicked();
    void buttonOffColorClicked();
    void buttonIconClicked();
    void latchStateChanged(int state);

    void timeoutChanged(USBDEVICE* dev, int val);
    void pageChanged(USBDEVICE* dev, int val);
    void pagesChanged(USBDEVICE* dev, int val);
    void disableClicked(USBDEVICE* dev, bool clicked, QWidget* widget);
    void setConnected(USBDEVICE* dev);
    void hideEvent(QHideEvent *event);
    void on_tabWidget_currentChanged(int index);
    void on_backupButton_clicked();
    void on_restoreButton_clicked();

private:

    usbDeviceType type = usbNone;
    Ui::controllerSetup* ui;
    QGraphicsTextItem* textItem;
    QLabel* imgLabel;
    unsigned char currentDevice = 0;
    QVector<BUTTON>* buttons;
    QVector<KNOB>* knobs;
    QVector<COMMAND>* commands;
    usbDevMap* devices;

    BUTTON* currentButton = Q_NULLPTR;
    KNOB* currentKnob = Q_NULLPTR;

    // Update Dialog
    QDialog * updateDialog = Q_NULLPTR;
    QComboBox* onEvent;
    QComboBox* offEvent;
    QComboBox* knobEvent;
    QLabel* onLabel;
    QLabel* offLabel;
    QLabel* knobLabel;
    QPushButton* buttonOnColor;
    QPushButton* buttonOffColor;
    QCheckBox *buttonLatch;
    QPushButton* buttonIcon;
    QLabel* iconLabel;
    QSpinBox* ledNumber;

    QString deviceName;
    QMutex* mutex;
    QColor initialColor = Qt::white;

    QLabel* noControllersText;

    int numTabs=0;
    QMap<QString,tabContent*> tabs;

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
