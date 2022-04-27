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
//#include "wfmain.h"
#include "rigidentities.h"

using namespace std;


#define HIDDATALENGTH 64
#define MAX_STR 255


/* Any additions/modifications to cmds enum must also be made in wfmain.h */
enum cmds {
    cmdNone, cmdGetRigID, cmdGetRigCIV, cmdGetFreq, cmdSetFreq, cmdGetMode, cmdSetMode,
    cmdGetDataMode, cmdSetModeFilter, cmdSetDataModeOn, cmdSetDataModeOff, cmdGetRitEnabled, cmdGetRitValue,
    cmdSpecOn, cmdSpecOff, cmdDispEnable, cmdDispDisable, cmdGetRxGain, cmdSetRxRfGain, cmdGetAfGain, cmdSetAfGain,
    cmdGetSql, cmdSetSql, cmdGetIFShift, cmdSetIFShift, cmdGetTPBFInner, cmdSetTPBFInner,
    cmdGetTPBFOuter, cmdSetTPBFOuter, cmdGetATUStatus,
    cmdSetATU, cmdStartATU, cmdGetSpectrumMode,
    cmdGetSpectrumSpan, cmdScopeCenterMode, cmdScopeFixedMode, cmdGetPTT, cmdSetPTT,
    cmdGetTxPower, cmdSetTxPower, cmdGetMicGain, cmdSetMicGain, cmdSetModLevel,
    cmdGetSpectrumRefLevel, cmdGetDuplexMode, cmdGetModInput, cmdGetModDataInput,
    cmdGetCurrentModLevel, cmdStartRegularPolling, cmdStopRegularPolling, cmdQueNormalSpeed,
    cmdGetVdMeter, cmdGetIdMeter, cmdGetSMeter, cmdGetCenterMeter, cmdGetPowerMeter,
    cmdGetSWRMeter, cmdGetALCMeter, cmdGetCompMeter, cmdGetTxRxMeter,
    cmdGetTone, cmdGetTSQL, cmdGetDTCS, cmdGetRptAccessMode, cmdGetPreamp, cmdGetAttenuator, cmdGetAntenna,
    cmdGetBandStackReg,
    cmdSetTime, cmdSetDate, cmdSetUTCOffset
};

struct COMMAND {
    COMMAND() {}

    COMMAND(int index, QString text, cmds command, char suffix) :
        index(index), text(text), command(command), suffix(suffix), bandswitch(false){}
    COMMAND(int index, QString text, cmds command, bandType band) :
        index(index), text(text), command(command),band(band), bandswitch(true) {}

    int index;
    QString text;
    cmds command;
    unsigned char suffix;
    bandType band;
    bool bandswitch;
};

struct BUTTON {
    BUTTON() {}

    BUTTON(char num, QRect pos, const QColor textColour) :
        num(num), pos(pos), textColour(textColour) {}

    quint8 num;
    QRect pos;
    const QColor textColour;
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
    enum { NONE, shuttleXpress, shuttlePro2, RC28 }usbDevice;
    bool isOpen=false;
    unsigned int buttons=0;
    unsigned char jogpos=0;
    unsigned char shutpos=0;
    unsigned char shutMult = 0;
    int jogCounter = 0;
    QTime	lastusbController = QTime::currentTime();
    QByteArray lastData="";
    unsigned char lastDialPos=0;
    QVector<BUTTON> buttonList;
    QVector<COMMAND> commands;
    QString product="";
    QString manufacturer="";
    QString serial="<none>";

protected:
};


#endif
