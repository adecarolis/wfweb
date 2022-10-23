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
#include <QGamepad>

#ifndef Q_OS_WIN
#include "hidapi/hidapi.h"
#else
#include "hidapi.h"
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

enum commandType {normal,bandswitch,modeswitch};

struct COMMAND {
    COMMAND() {}

    COMMAND(int index, QString text, int command, char suffix) :
        index(index), text(text), command(command), suffix(suffix), type(normal){}
    COMMAND(int index, QString text, int command, bandType band) :
        index(index), text(text), command(command), band(band), type(bandswitch) {}
    COMMAND(int index, QString text, int command, mode_kind mode) :
        index(index), text(text), command(command), mode(mode), type(modeswitch) {}

    int index;
    QString text;
    int command;
    unsigned char suffix;
    bandType band;
    mode_kind mode;
    commandType type;
};

struct BUTTON {
    BUTTON() {}

    BUTTON(quint8 dev, int num, QRect pos, const QColor textColour, COMMAND* on, COMMAND* off) :
        dev(dev), name(""), num(num), pos(pos), textColour(textColour), onCommand(on), offCommand(off) {}
    BUTTON(quint8 dev, QString name, QRect pos, const QColor textColour, COMMAND* on, COMMAND* off) :
        dev(dev), num(-1),name(name), pos(pos), textColour(textColour), onCommand(on), offCommand(off) {}

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

class usbController : public QObject
{
    Q_OBJECT

public:
    usbController();
    ~usbController();
    int hidApiWrite(unsigned char* data, unsigned char length);

public slots:
    void init();
    void run();
    void runTimer();
    void ledControl(bool on, unsigned char num);
    void receiveCommands(QVector<COMMAND>*);
    void receiveButtons(QVector<BUTTON>*);

signals:
    void jogPlus();
    void jogMinus();
    void sendJog(int counter);
    void doShuttle(bool plus, quint8 level);
    void setBand(int band);
    void button(const COMMAND* cmd);
    void newDevice(unsigned char devType, QVector<BUTTON>* but,QVector<COMMAND>* cmd);

private:
    hid_device* handle;
    enum { NONE=0, shuttleXpress, shuttlePro2, RC28, xBoxGamepad, unknownGamepad }usbDevice;
    bool isOpen=false;
    unsigned int buttons=0;
    unsigned char jogpos=0;
    unsigned char shutpos=0;
    unsigned char shutMult = 0;
    int jogCounter = 0;
    QTime	lastusbController = QTime::currentTime();
    QByteArray lastData="";
    unsigned char lastDialPos=0;
    QVector<BUTTON>* buttonList;
    QVector<COMMAND>* commands = Q_NULLPTR;
    QString product="";
    QString manufacturer="";
    QString serial="<none>";
    QGamepad* gamepad=Q_NULLPTR;
    void buttonState(QString but, bool val);

protected:
};


#endif
