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
    cmdSetTime, cmdSetDate, cmdSetUTCOffset
};

struct COMMAND {
/*    COMMAND(cmds command, char suffix, bool bandswitch, bandType band) :
        command(command), suffix(suffix), bandswitch(bandswitch), band(band) {} */

    int index;
    cmds command;
    char suffix;
    bool bandswitch;
    bandType band;
    QGraphicsTextItem* text;
};

struct BUTTON {
    BUTTON(char num, QRect pos, const QColor col) :
        num(num), pos(pos), textColour(col) {}

    quint8 num;
    QRect pos;
    int onEvent = 0;
    int offEvent = 0;
    COMMAND onCommand;
    COMMAND offCommand;
    const QColor textColour;
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

    void button(bool,unsigned char num);
    void newDevice(unsigned char devType, QVector<BUTTON>* but);
private:
    hid_device* handle;
    enum { NONE, shuttleXpress, shuttlePro2, RC28 }usbDevice;
    bool isOpen=false;
    unsigned int buttons=0;
    unsigned char jogpos=0;
    unsigned char shutpos=0;
    unsigned char shutMult = 0;
    QTime	lastusbController = QTime::currentTime();
    QByteArray lastData="";
    unsigned char lastDialPos=0;
    int counter = 0;
    QVector<BUTTON> buttonList;


protected:
};


#endif
