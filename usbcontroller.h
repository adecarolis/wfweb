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
#include <QMutex>

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
    int command=0;
    unsigned char suffix=0x0;
    availableBands band=bandGen;
    mode_kind mode=modeLSB;
    usbCommandType cmdType = commandButton;
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

};

#if defined(USB_CONTROLLER)
class usbController : public QObject
{
    Q_OBJECT

public:
    usbController();
    ~usbController();

public slots:
    void init(int sens, QMutex *mut);
    void run();
    void runTimer();
    void ledControl(bool on, unsigned char num);
    void receiveCommands(QVector<COMMAND>*);
    void receiveButtons(QVector<BUTTON>*);
    void receiveKnobs(QVector<KNOB>*);
    void getVersion();
    void receiveSensitivity(int val);
    void programButton(int val, QString text);

signals:
    void jogPlus();
    void jogMinus();
    void sendJog(int counter);
    void doShuttle(bool plus, quint8 level);
    void setBand(int band);
    void button(const COMMAND* cmd);
    void newDevice(unsigned char devType, QVector<BUTTON>* but, QVector<KNOB>* kb, QVector<COMMAND>* cmd, QMutex* mut);
    void sendSensitivity(int val);

private:
    hid_device* handle=NULL;
    int hidStatus = 1;
    bool isOpen=false;
    quint32 buttons = 0;
    quint32 knobs = 0;
    unsigned char jogpos=0;
    unsigned char shutpos=0;
    unsigned char shutMult = 0;
    int jogCounter = 0;
    QTime	lastusbController = QTime::currentTime();
    QByteArray lastData = QByteArray(8,0x0);
    unsigned char lastDialPos=0;
    QVector<BUTTON>* buttonList;
    QVector<KNOB>* knobList;
    QVector<COMMAND>* commands = Q_NULLPTR;
    QString product="";
    QString manufacturer="";
    QString serial="<none>";
    QString path = "";
    int sensitivity = 1;
    QList<int> knobValues;
    QList<quint8> knobSend;
    QMutex* mutex=Q_NULLPTR;
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QGamepad* gamepad=Q_NULLPTR;
#endif
    void buttonState(QString but, bool val);
    void buttonState(QString but, double val);
    usbDeviceType usbDevice = usbNone;

    unsigned short knownUsbDevices[5][3] = {
    {shuttleXpress,0x0b33,0x0020},
    {shuttlePro2,0x0b33,0x0030},
    //{eCoderPlus,0x0c26,0x001e}, // Only enable for testing!
    {RC28,0x0c26,0x001e},
    {eCoderPlus, 0x1fc9, 0x0003},
    {QuickKeys, 0x28bd, 0x5202}
    };

protected:
};

#endif

#endif
