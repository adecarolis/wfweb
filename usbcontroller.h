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

#if defined(USB_CONTROLLER) && QT_VERSION < QT_VERSION_CHECK(6,0,0)
#include <QGamepad>
#endif

#if defined(USB_CONTROLLER)
#ifndef Q_OS_WIN
#include "hidapi/hidapi.h"
#else
#include "hidapi.h"
#endif

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

#ifndef Q_OS_WIN
//Headers needed for sleeping.
#include <unistd.h>
#endif

// Include these so we have the various enums
#include "rigidentities.h"

using namespace std;


#define HIDDATALENGTH 64
#define MAX_STR 255

struct COMMAND {
    COMMAND() {}

    COMMAND(int index, QString text, int command, char suffix) :
        index(index), text(text), command(command), suffix(suffix) {}
    COMMAND(int index, QString text, int command, availableBands band) :
        index(index), text(text), command(command), band(band) {}
    COMMAND(int index, QString text, int command, mode_kind mode) :
        index(index), text(text), command(command), mode(mode) {}

    int index=0;
    QString text;
    int command=0;
    unsigned char suffix=0x0;
    availableBands band=bandGen;
    mode_kind mode=modeLSB;
};

struct BUTTON {
    BUTTON() {}

    BUTTON(quint8 dev, int num, QRect pos, const QColor textColour, COMMAND* on, COMMAND* off) :
        dev(dev), num(num), name(""), pos(pos), textColour(textColour), onCommand(on), offCommand(off) {}
    BUTTON(quint8 dev, QString name, QRect pos, const QColor textColour, COMMAND* on, COMMAND* off) :
        dev(dev), num(-1), name(name), pos(pos), textColour(textColour), onCommand(on), offCommand(off) {}

    quint8 dev;
    int num;
    QString name;
    QRect pos;
    QColor textColour;
    int onEvent = 0;
    int offEvent = 0;
    const COMMAND* onCommand=Q_NULLPTR;
    const COMMAND* offCommand=Q_NULLPTR;
    QGraphicsTextItem* onText;
    QGraphicsTextItem* offText;

};

#if defined(USB_CONTROLLER)
class usbController : public QObject
{
    Q_OBJECT

public:
    usbController();
    ~usbController();

public slots:
    void init();
    void run();
    void runTimer();
    void ledControl(bool on, unsigned char num);
    void receiveCommands(QVector<COMMAND>*);
    void receiveButtons(QVector<BUTTON>*);
    void getVersion();

signals:
    void jogPlus();
    void jogMinus();
    void sendJog(int counter);
    void doShuttle(bool plus, quint8 level);
    void setBand(int band);
    void button(const COMMAND* cmd);
    void newDevice(unsigned char devType, QVector<BUTTON>* but,QVector<COMMAND>* cmd);

private:
    hid_device* handle=NULL;
    int hidStatus = 1;
    bool isOpen=false;
    unsigned int buttons=0;
    unsigned char jogpos=0;
    unsigned char shutpos=0;
    unsigned char shutMult = 0;
    int jogCounter = 0;
    QTime	lastusbController = QTime::currentTime();
    QByteArray lastData = QByteArray(8,0x0);
    unsigned char lastDialPos=0;
    QVector<BUTTON>* buttonList;
    QVector<COMMAND>* commands = Q_NULLPTR;
    QString product="";
    QString manufacturer="";
    QString serial="<none>";
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QGamepad* gamepad=Q_NULLPTR;
#endif
    void buttonState(QString but, bool val);
    void buttonState(QString but, double val);
    usbDeviceType usbDevice = usbNone;
protected:
};

#endif

#endif
