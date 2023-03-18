#ifndef usbController_H
#define usbController_H

#include <iostream>
#include <QThread>
#include <QCoreApplication>
#include <QTimer>
#include <QDateTime>
#include <QRect>
#include <QGraphicsTextItem>
#include <QColor>
#include <QVector>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QIODevice>
#include <QtEndian>
#include <QUuid>
#include <QLabel>

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

struct USBDEVICE {
    usbDeviceType usbDevice = usbNone;
    bool remove = false;
    bool connected = false;
    bool uiCreated = false;
    bool disabled = false;
    hid_device* handle = NULL;
    QString product = "";
    QString manufacturer = "";
    QString serial = "<none>";
    QString deviceId = "";
    QString path = "";
    quint16 vendorId = 0;
    quint16 productId = 0;
    int sensitivity = 1;
    unsigned char jogpos=0;
    unsigned char shutpos=0;
    unsigned char shutMult = 0;
    int jogCounter = 0;
    quint32 buttons = 0;
    quint32 knobs = 0;
    QList<int> knobValues;
    QList<int> knobPrevious;
    QList<quint8> knobSend;
    QTime lastusbController = QTime::currentTime();
    QByteArray lastData = QByteArray(8,0x0);
    unsigned char lastDialPos=0;
    QUuid uuid;
    QLabel* message;
};

struct COMMAND {
    COMMAND() {}

    COMMAND(int index, QString text, usbCommandType cmdType, int command, unsigned char suffix) :
        index(index), text(text), cmdType(cmdType), command(command), suffix(suffix) {}
    COMMAND(int index, QString text, usbCommandType cmdType, int command, availableBands band) :
        index(index), text(text), cmdType(cmdType), command(command), band(band) {}
    COMMAND(int index, QString text, usbCommandType cmdType, int command, mode_kind mode) :
        index(index), text(text), cmdType(cmdType), command(command), mode(mode) {}

    int index=0;
    QString text;
    usbCommandType cmdType = commandButton;
    int command=0;
    unsigned char suffix=0x0;
    int value=0;
    availableBands band=bandGen;
    mode_kind mode=modeLSB;
};

struct BUTTON {
    BUTTON() {}

    BUTTON(usbDeviceType dev, int num, QRect pos, const QColor textColour, COMMAND* on, COMMAND* off) :
        dev(dev), num(num), name(""), pos(pos), textColour(textColour), onCommand(on), offCommand(off) {}
    BUTTON(usbDeviceType dev, QString name, QRect pos, const QColor textColour, COMMAND* on, COMMAND* off) :
        dev(dev), num(-1), name(name), pos(pos), textColour(textColour), onCommand(on), offCommand(off) {}

    usbDeviceType dev;
    int num;
    QString name;
    QRect pos;
    QColor textColour;
    const COMMAND* onCommand = Q_NULLPTR;
    const COMMAND* offCommand = Q_NULLPTR;
    QGraphicsTextItem* onText;
    QGraphicsTextItem* offText;
    QString on;
    QString off;
    QString devicePath;
};


struct KNOB {
    KNOB() {}

    KNOB(usbDeviceType dev, int num, QRect pos, const QColor textColour, COMMAND* command) :
        dev(dev), num(num), name(""), pos(pos), textColour(textColour), command(command) {}

    usbDeviceType dev;
    int num;
    QString name;
    QRect pos;
    QColor textColour;
    const COMMAND* command = Q_NULLPTR;
    QGraphicsTextItem* text;
    QString cmd;
    QString devicePath;
};

struct CONTROLLER {
    bool disabled=false;
    int sensitivity=1;
    quint8 speed=2;
    quint8 timeout=30;
    quint8 brightness=2;
    quint8 orientation=0;
    QColor color=Qt::white;
};
typedef QMap<QString,CONTROLLER> usbMap;


#if defined(USB_CONTROLLER)
class usbController : public QObject
{
    Q_OBJECT

public:
    usbController();
    ~usbController();

public slots:
    void init(QMutex* mut,usbMap* prefs ,QVector<BUTTON>* buts,QVector<KNOB>* knobs);
    void run();
    void runTimer();
    void ledControl(bool on, unsigned char num);
    void receivePTTStatus(bool on);
    void getVersion();
    void programButton(QString path, quint8 val, QString text);
    void programSensitivity(QString path, quint8 val);
    void programBrightness(QString path, quint8 val);
    void programOrientation(QString path, quint8 val);
    void programSpeed(QString path, quint8 val);
    void programWheelColour(QString path, quint8 r, quint8 g, quint8 b);
    void programOverlay(QString path, quint8 duration, QString text);
    void programTimeout(QString path, quint8 val);
    void programDisable(QString path, bool disabled);


signals:
    void jogPlus();
    void jogMinus();
    void sendJog(int counter);
    void doShuttle(bool plus, quint8 level);
    void setBand(int band);
    void button(const COMMAND* cmd);
    void newDevice(USBDEVICE* dev, CONTROLLER* cntrl, QVector<BUTTON>* but, QVector<KNOB>* kb, QVector<COMMAND>* cmd, QMutex* mut);
    void removeDevice(USBDEVICE* dev);

private:
    void loadButtons();
    void loadKnobs();
    void loadCommands();
    int hidStatus = 1;
    bool isOpen=false;
    int devicesConnected=0;
    QVector<BUTTON>* buttonList;
    QVector<KNOB>* knobList;

    QList<QVector<KNOB>*> deviceKnobs;
    QList<QVector<BUTTON>*> deviceButtons;

    QVector<BUTTON> defaultButtons;
    QVector<KNOB> defaultKnobs;
    QVector<COMMAND> commands;
    QMap<QString,USBDEVICE> usbDevices;
    usbMap *controllers;
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QGamepad* gamepad=Q_NULLPTR;
#endif
    void buttonState(QString but, bool val);
    void buttonState(QString but, double val);
    QColor currentColour;

    QMutex* mutex=Q_NULLPTR;
    COMMAND sendCommand;

    unsigned short knownUsbDevices[6][5] = {
    {shuttleXpress,0x0b33,0x0020,0x0000,0x0000},
    {shuttlePro2,0x0b33,0x0030,0x0000,0x0000},
    {RC28,0x0c26,0x001e,0x0000,0x0000},
    {eCoderPlus, 0x1fc9, 0x0003,0x0000,0x0000},
    {QuickKeys, 0x28bd, 0x5202,0x0001,0xff0a},
    {QuickKeys, 0x28bd, 0x5203,0x0001,0xff0a}
    };

protected:
};


class usbControllerDev : public QObject
{
    Q_OBJECT

};



#endif

#endif
