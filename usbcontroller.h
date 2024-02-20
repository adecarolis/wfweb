#ifndef usbController_H
#define usbController_H

#include <iostream>
#include <QThread>
#include <QCoreApplication>
#include <QTimer>
#include <QDateTime>
#include <QRect>
#include <QGraphicsTextItem>
#include <QSpinBox>
#include <QColor>
#include <QVector>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QIODevice>
#include <QtEndian>
#include <QUuid>
#include <QLabel>
#include <QImage>
#include <QPainter>
#include <QImageWriter>
#include <QBuffer>
#include <QSettings>
#include <QMessageBox>
#include <memory>


#if defined(USB_CONTROLLER) && QT_VERSION < QT_VERSION_CHECK(6,0,0)
#include <QGamepad>
#endif

#if defined(USB_CONTROLLER)
    #ifndef Q_OS_WIN
        #include "hidapi/hidapi.h"
    #else
        #include "hidapi.h"
    #endif

    #ifdef HID_API_VERSION_MAJOR
        #ifndef HID_API_MAKE_VERSION
        #define HID_API_MAKE_VERSION(mj, mn, p) (((mj) << 24) | ((mn) << 8) | (p))
        #endif
        #ifndef HID_API_VERSION
            #define HID_API_VERSION HID_API_MAKE_VERSION(HID_API_VERSION_MAJOR, HID_API_VERSION_MINOR, HID_API_VERSION_PATCH)
        #endif

        #if defined(__APPLE__) && HID_API_VERSION >= HID_API_MAKE_VERSION(0, 12, 0)
            #include <hidapi/hidapi_darwin.h>
        #endif

        #if defined(USING_HIDAPI_LIBUSB) && HID_API_VERSION >= HID_API_MAKE_VERSION(0, 12, 0)
            #include <hidapi_libusb.h>
        #endif
    #endif
#endif

#ifndef Q_OS_WIN
#include <unistd.h>
#endif

// Include these so we have the various enums
#include "rigidentities.h"

using namespace std;

#define HIDDATALENGTH 64
#define MAX_STR 255

struct USBTYPE {
    USBTYPE() {}
    USBTYPE(usbDeviceType model,quint32 manufacturerId, quint32 productId , quint32 usage, quint32 usagePage, int buttons, int cols, int knobs, int leds,  int maxPayload, int iconSize) :
        model(model), manufacturerId(manufacturerId), productId(productId), usage(usage), usagePage(usagePage), buttons(buttons), cols(cols), knobs(knobs), leds(leds), maxPayload(maxPayload), iconSize(iconSize) {}

    usbDeviceType model = usbNone;
    quint32 manufacturerId=0;
    quint32 productId=0;
    quint32 usage=0;
    quint32 usagePage=0;
    int buttons=0; // How many buttons
    int cols=0; // How many columns of buttons
    int knobs=0;    // How many knobs
    int leds=0;     // how many leds
    int maxPayload=0;   // Max allowed payload
    int iconSize=0;
};


struct KNOBVALUE {
    int value=0;
    int previous=0;
    quint8 send=0;
    qint64 lastChanged=0;
    QString name="";
};

struct USBDEVICE {
    USBDEVICE() {}
    USBDEVICE(USBTYPE type) : type(type) {}
    USBTYPE type;
    bool detected = false;
    bool remove = false;
    bool connected = false;
    bool uiCreated = false;
    bool disabled = false;
    quint8 speed=2;
    quint8 timeout=30;
    quint8 brightness=2;
    quint8 orientation=0;
    QColor color=Qt::darkGray;
    cmds lcd=cmdNone;

    hid_device* handle = NULL;
    QString product = "";
    QString manufacturer = "";
    QString serial = "<none>";
    QString deviceId = "";
    QString path = "";
    int sensitivity = 1;
    unsigned char jogpos=0;
    unsigned char shutpos=0;
    unsigned char shutMult = 0;
    int jogCounter = 0;
    quint32 buttons = 0;
    quint32 knobs = 0;
    QList<KNOBVALUE> knobValues;

    QTime lastusbController = QTime::currentTime();
    QByteArray lastData = QByteArray(8,0x0);
    unsigned char lastDialPos=0;
    QUuid uuid;
    QLabel *message;
    int pages=1;
    int currentPage=0;
    QGraphicsScene* scene = Q_NULLPTR;
    QSpinBox* pageSpin = Q_NULLPTR;
    QImage image;
    quint8 ledStatus=0x07;
};

struct COMMAND {
    COMMAND() {}
    COMMAND(int index, QString text, usbCommandType cmdType, int command, int value) :
        index(index), text(text), cmdType(cmdType), command(command), value(value) {}
    COMMAND(int index, QString text, usbCommandType cmdType, int command, unsigned char suffix) :
        index(index), text(text), cmdType(cmdType), command(command), suffix(suffix) {}
    COMMAND(int index, QString text, usbCommandType cmdType, int command, int getCommand, unsigned char suffix) :
        index(index), text(text), cmdType(cmdType), command(command), getCommand(getCommand), suffix(suffix) {}
    COMMAND(int index, QString text, usbCommandType cmdType, int command, availableBands band) :
        index(index), text(text), cmdType(cmdType), command(command), band(band) {}
    COMMAND(int index, QString text, usbCommandType cmdType, int command, mode_kind mode) :
        index(index), text(text), cmdType(cmdType), command(command), mode(mode) {}

    int index=0;
    QString text;
    usbCommandType cmdType = commandButton;
    int command=0;
    int getCommand=0;
    unsigned char suffix=0x0;
    int value=0;
    availableBands band=bandGen;
    mode_kind mode=modeLSB;
};

struct BUTTON {
    BUTTON() {}

    BUTTON(usbDeviceType dev, int num, QRect pos, const QColor textColour, COMMAND* on, COMMAND* off, bool graphics=false, int led=0) :
        dev(dev), num(num), name(""), pos(pos), textColour(textColour), onCommand(on), offCommand(off), on(onCommand->text), off(offCommand->text), graphics(graphics), led(led){}
    BUTTON(usbDeviceType dev, QString name, QRect pos, const QColor textColour, COMMAND* on, COMMAND* off) :
        dev(dev), num(-1), name(name), pos(pos), textColour(textColour), onCommand(on), offCommand(off), on(onCommand->text), off(offCommand->text) {}

    usbDeviceType dev;
    USBDEVICE* parent = Q_NULLPTR;
    int page=1;
    int num;
    QString name;
    QRect pos;
    QColor textColour;
    const COMMAND* onCommand = Q_NULLPTR;
    const COMMAND* offCommand = Q_NULLPTR;
    QGraphicsRectItem* bgRect = Q_NULLPTR;
    QGraphicsTextItem* text = Q_NULLPTR;
    QString on;
    QString off;
    QString path;
    QColor backgroundOn = Qt::lightGray;
    QColor backgroundOff = Qt::blue;
    QString iconName = "";
    QImage* icon = Q_NULLPTR;
    bool toggle = false;
    bool isOn = false;
    bool graphics = false;
    int led = 0;
};


struct KNOB {
    KNOB() {}

    KNOB(usbDeviceType dev, int num, QRect pos, const QColor textColour, COMMAND* command) :
        dev(dev), num(num), name(""), pos(pos), textColour(textColour), command(command), cmd(command->text) {}

    usbDeviceType dev;
    USBDEVICE* parent = Q_NULLPTR;
    int page=1;
    int num;
    QString name;
    QRect pos;
    QColor textColour;
    const COMMAND* command = Q_NULLPTR;
    QGraphicsTextItem* text = Q_NULLPTR;
    QString cmd;
    QString path;
};


typedef QMap<QString,USBDEVICE> usbDevMap;


#if defined(USB_CONTROLLER)
class usbController : public QObject
{
    Q_OBJECT

public:
    usbController();
    ~usbController();
    bool hotPlugEvent(const QByteArray & eventType, void * message, long * result);

public slots:
    void init(QMutex* mut,usbDevMap* prefs ,QVector<BUTTON>* buts,QVector<KNOB>* knobs);
    void run();
    void runTimer();
    void receivePTTStatus(bool on);
    void receiveLevel(cmds cmd, unsigned char level);
    void programPages(USBDEVICE* dev, int pages);
    void programDisable(USBDEVICE* dev, bool disabled);

    void sendRequest(USBDEVICE *dev, usbFeatureType feature, int val=0, QString text="", QImage* img=Q_NULLPTR, QColor* color=Q_NULLPTR);
    void sendToLCD(QImage *img);
    void backupController(USBDEVICE* dev, QString file);
    void restoreController(USBDEVICE* dev, QString file);

signals:
    void jogPlus();
    void jogMinus();
    void sendJog(int counter);
    void doShuttle(bool plus, quint8 level);
    void setBand(int band);
    void button(const COMMAND* cmd);
    void initUI(usbDevMap* devs, QVector<BUTTON>* but, QVector<KNOB>* kb, QVector<COMMAND>* cmd, QMutex* mut);
    void newDevice(USBDEVICE* dev);
    void removeDevice(USBDEVICE* dev);
    void setConnected(USBDEVICE* dev);
    void changePage(USBDEVICE* dev, int page);

private:
    void loadButtons();
    void loadKnobs();
    void loadCommands();


    int hidStatus = 1;
    bool isOpen=false;
    int devicesConnected=0;
    QVector<BUTTON>* buttonList;
    QVector<KNOB>* knobList;

    QVector<BUTTON> defaultButtons;
    QVector<KNOB> defaultKnobs;
    QVector<USBTYPE> knownDevices;
    QVector<COMMAND> commands;
    usbDevMap* devices;    

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QGamepad* gamepad=Q_NULLPTR;
#endif
    void buttonState(QString but, bool val);
    void buttonState(QString but, double val);
    QColor currentColour;

    QMutex* mutex=Q_NULLPTR;
    COMMAND sendCommand;

    QTimer* dataTimer = Q_NULLPTR;
protected:
};


class usbControllerDev : public QObject
{
    Q_OBJECT

};



#endif

#endif
