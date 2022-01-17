#ifndef WFMAIN_H
#define WFMAIN_H

#include <QtCore/QCoreApplication>
#include <QtCore/QDirIterator>
#include <QtCore/QStandardPaths>

#include <QThread>
#include <QString>
#include <QVector>
#include <QTimer>
#include <QSettings>
#include <QShortcut>
#include <QMetaType>

#include "logcategories.h"
#include "commhandler.h"
#include "rigcommander.h"
#include "rigstate.h"
#include "freqmemory.h"
#include "rigidentities.h"
#include "repeaterattributes.h"

#include "udpserver.h"
#include "rigctld.h"
#include "signal.h"

#include <qserialportinfo.h>

#include <deque>
#include <memory>

namespace Ui {
class wfmain;
}

class servermain : public QObject
{
    Q_OBJECT

public:
    servermain(const QString serialPortCL, const QString hostCL, const QString settingsFile);
    QString serialPortCL;
    QString hostCL;
    ~servermain();

signals:
    // Basic to rig:
    void setCIVAddr(unsigned char newRigCIVAddr);
    void setRigID(unsigned char rigID);
    void setRTSforPTT(bool enabled);

    // Power
    void sendPowerOn();
    void sendPowerOff();

    // Frequency, mode, band:
    void getFrequency();
    void setFrequency(unsigned char vfo, freqt freq);
    void getMode();
    void setMode(unsigned char modeIndex, unsigned char modeFilter);
    void setMode(mode_info);
    void setDataMode(bool dataOn, unsigned char filter);
    void getDataMode();
    void getModInput(bool dataOn);
    void setModInput(rigInput input, bool dataOn);
    void getBandStackReg(char band, char regCode);
    void getDebug();
    void getRitEnabled();
    void getRitValue();
    void setRitValue(int ritValue);
    void setRitEnable(bool ritEnabled);

    // Repeater:
    void getDuplexMode();
    void getTone();
    void getTSQL();
    void getDTCS();
    void getRptAccessMode();

    // Level get:
    void getLevels(); // get all levels
    void getRfGain();
    void getAfGain();
    void getSql();
    void getIfShift();
    void getTPBFInner();
    void getTPBFOuter();
    void getTxPower();
    void getMicGain();
    void getSpectrumRefLevel();
    void getModInputLevel(rigInput input);

    // Level set:
    void setRfGain(unsigned char level);
    void setAfGain(unsigned char level);
    void setSql(unsigned char level);
    void setIFShift(unsigned char level);
    void setTPBFInner(unsigned char level);
    void setTPBFOuter(unsigned char level);

    void setIFShiftWindow(unsigned char level);
    void setTPBFInnerWindow(unsigned char level);
    void setTPBFOuterWindow(unsigned char level);
    void setMicGain(unsigned char);
    void setCompLevel(unsigned char);
    void setTxPower(unsigned char);
    void setMonitorLevel(unsigned char);
    void setVoxGain(unsigned char);
    void setAntiVoxGain(unsigned char);
    void setSpectrumRefLevel(int);

    void setModLevel(rigInput input, unsigned char level);
    void setACCGain(unsigned char level);
    void setACCAGain(unsigned char level);
    void setACCBGain(unsigned char level);
    void setUSBGain(unsigned char level);
    void setLANGain(unsigned char level);

    void getMeters(meterKind meter);


    // PTT, ATU, ATT, Antenna, Preamp:
    void getPTT();
    void setPTT(bool pttOn);
    void getAttenuator();
    void getPreamp();
    void getAntenna();
    void setAttenuator(unsigned char att);
    void setPreamp(unsigned char pre);
    void setAntenna(unsigned char ant, bool rx);
    void startATU();
    void setATU(bool atuEnabled);
    void getATUStatus();

    // Time and date:
    void setTime(timekind t);
    void setDate(datekind d);
    void setUTCOffset(timekind t);

    void getRigID(); // this is the model of the rig
    void getRigCIV(); // get the rig's CIV addr
    void spectOutputEnable();
    void spectOutputDisable();
    void scopeDisplayEnable();
    void scopeDisplayDisable();
    void setScopeMode(spectrumMode spectMode);
    void setScopeSpan(char span);
    void setScopeEdge(char edge);
    void setScopeFixedEdge(double startFreq, double endFreq, unsigned char edgeNumber);
    void getScopeMode();
    void getScopeEdge();
    void getScopeSpan();
    void sayFrequency();
    void sayMode();
    void sayAll();
    void sendCommSetup(unsigned char rigCivAddr, QString rigSerialPort, quint32 rigBaudRate,QString vsp);
    void sendCommSetup(unsigned char rigCivAddr, udpPreferences prefs, audioSetup rxSetup, audioSetup txSetup, QString vsp);
    void sendCloseComm();
    void sendChangeLatency(quint16 latency);
    void initServer();
    void sendRigCaps(rigCapabilities caps);
    void requestRigState();
    void stateUpdated();

private slots:

    void receiveCommReady();
    void receiveFreq(freqt);
    void receivePTTstatus(bool pttOn);
    void receiveDataModeStatus(bool dataOn);
    void receiveBandStackReg(freqt f, char mode, char filter, bool dataOn); // freq, mode, (filter,) datamode
    void receiveRITStatus(bool ritEnabled);
    void receiveRITValue(int ritValHz);
    void receiveModInput(rigInput input, bool dataOn);
    //void receiveDuplexMode(duplexMode dm);



    // Levels:
    void receiveRfGain(unsigned char level);
    void receiveAfGain(unsigned char level);
    void receiveSql(unsigned char level);
    void receiveIFShift(unsigned char level);
    void receiveTBPFInner(unsigned char level);
    void receiveTBPFOuter(unsigned char level);
    // 'change' from data in transceiver controls window:
    void receiveTxPower(unsigned char power);
    void receiveMicGain(unsigned char gain);
    void receiveCompLevel(unsigned char compLevel);
    void receiveMonitorGain(unsigned char monitorGain);
    void receiveVoxGain(unsigned char voxGain);
    void receiveAntiVoxGain(unsigned char antiVoxGain);
    void receiveSpectrumRefLevel(int level);
    void receiveACCGain(unsigned char level, unsigned char ab);
    void receiveUSBGain(unsigned char level);
    void receiveLANGain(unsigned char level);

    // Meters:
    void receiveMeter(meterKind meter, unsigned char level);
//    void receiveSMeter(unsigned char level);
//    void receivePowerMeter(unsigned char level);
//    void receiveALCMeter(unsigned char level);
//    void receiveCompMeter(unsigned char level);


    void receivePreamp(unsigned char pre);
    void receiveAttenuator(unsigned char att);
    void receiveAntennaSel(unsigned char ant, bool rx);
    void receiveRigID(rigCapabilities rigCaps);
    void receiveFoundRigID(rigCapabilities rigCaps);
    void receiveSerialPortError(QString port, QString errorText);
    void sendRadioCommandLoop();
    void receiveBaudRate(quint32 baudrate);

    void setRadioTimeDateSend();


private:
    Ui::wfmain *ui;
    void closeEvent(QCloseEvent *event);
    QSettings *settings=Q_NULLPTR;
    void loadSettings();

    void getInitialRigState();

    void openRig();
    void powerRigOff();
    void powerRigOn();
    QStringList portList;
    QString serialPortRig;

    rigCommander * rig=Q_NULLPTR;
    QThread* rigThread = Q_NULLPTR;
    QTimer * delayedCommand;
    QTimer * pttTimer;
    uint16_t loopTickCounter;
    uint16_t slowCmdNum;

    void makeRig();
    void rigConnections();
    void removeRig();
    void findSerialPort();

    void setServerToPrefs();
    void setInitialTiming();
    void getSettingsFilePath(QString settingsFile);

    QStringList modes;
    int currentModeIndex;
    QStringList spans;
    QStringList edges;
    QStringList commPorts;

    quint16 spectWidth;
    quint16 wfLength;
    bool spectrumDrawLock;

    QByteArray spectrumPeaks;

    QVector <QByteArray> wfimage;
    unsigned int wfLengthMax;

    bool onFullscreen;
    bool drawPeaks;
    bool freqTextSelected;
    void checkFreqSel();

    double oldLowerFreq;
    double oldUpperFreq;
    freqt freq;
    float tsKnobMHz;

    unsigned char setModeVal=0;
    unsigned char setFilterVal=0;

    enum cmds {cmdNone, cmdGetRigID, cmdGetRigCIV, cmdGetFreq, cmdSetFreq, cmdGetMode, cmdSetMode,
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
              cmdSetTime, cmdSetDate, cmdSetUTCOffset};

    struct commandtype {
        cmds cmd;
        std::shared_ptr<void> data;
    };

    std::deque <commandtype> delayedCmdQue;    // rapid que for commands to the radio
    std::deque <cmds> periodicCmdQueue; // rapid que for metering
    std::deque <cmds> slowPollCmdQueue; // slow, regular checking for UI sync
    void doCmd(cmds cmd);
    void doCmd(commandtype cmddata);

    void issueCmd(cmds cmd, freqt f);
    void issueCmd(cmds cmd, mode_info m);
    void issueCmd(cmds cmd, timekind t);
    void issueCmd(cmds cmd, datekind d);
    void issueCmd(cmds cmd, int i);
    void issueCmd(cmds cmd, unsigned char c);
    void issueCmd(cmds cmd, char c);
    void issueCmd(cmds cmd, bool b);

    // These commands pop_front and remove similar commands:
    void issueCmdUniquePriority(cmds cmd, bool b);
    void issueCmdUniquePriority(cmds cmd, unsigned char c);
    void issueCmdUniquePriority(cmds cmd, char c);
    void issueCmdUniquePriority(cmds cmd, freqt f);

    void removeSimilarCommand(cmds cmd);

    qint64 lastFreqCmdTime_ms;

    int pCmdNum = 0;
    int delayedCmdIntervalLAN_ms = 100;
    int delayedCmdIntervalSerial_ms = 100;
    int delayedCmdStartupInterval_ms = 100;
    bool runPeriodicCommands;
    bool usingLAN = false;

    // Radio time sync:
    QTimer *timeSync;
    bool waitingToSetTimeDate;
    void setRadioTimeDatePrep();
    timekind timesetpoint;
    timekind utcsetting;
    datekind datesetpoint;

    freqMemory mem;
    struct colors {
        QColor Dark_PlotBackground;
        QColor Dark_PlotAxisPen;
        QColor Dark_PlotLegendTextColor;
        QColor Dark_PlotLegendBorderPen;
        QColor Dark_PlotLegendBrush;
        QColor Dark_PlotTickLabel;
        QColor Dark_PlotBasePen;
        QColor Dark_PlotTickPen;
        QColor Dark_PeakPlotLine;
        QColor Dark_TuningLine;

        QColor Light_PlotBackground;
        QColor Light_PlotAxisPen;
        QColor Light_PlotLegendTextColor;
        QColor Light_PlotLegendBorderPen;
        QColor Light_PlotLegendBrush;
        QColor Light_PlotTickLabel;
        QColor Light_PlotBasePen;
        QColor Light_PlotTickPen;
        QColor Light_PeakPlotLine;
        QColor Light_TuningLine;

    } colorScheme;

    struct preferences {
        bool useFullScreen;
        bool useDarkMode;
        bool useSystemTheme;
        bool drawPeaks;
        bool wfAntiAlias;
        bool wfInterpolate;
        QString stylesheetPath;
        unsigned char radioCIVAddr;
        bool CIVisRadioModel;
        bool forceRTSasPTT;
        QString serialPortRadio;
        quint32 serialPortBaud;
        bool enablePTT;
        bool niceTS;
        bool enableLAN;
        bool enableRigCtlD;
        quint16 rigCtlPort;
        colors colorScheme;
        QString virtualSerialPort;
        unsigned char localAFgain;
        unsigned int wflength;
        int wftheme;
        bool confirmExit;
        bool confirmPowerOff;
        meterKind meter2Type;
        // plot scheme
    } prefs;

    preferences defPrefs;
    udpPreferences udpPrefs;
    udpPreferences udpDefPrefs;

    // Configuration for audio output and input.
    audioSetup rxSetup;
    audioSetup txSetup;

    audioSetup serverRxSetup;
    audioSetup serverTxSetup;

    colors defaultColors;

    void setDefaultColors(); // populate with default values
    void useColors(); // set the plot up
    void setDefPrefs(); // populate default values to default prefs
    void setTuningSteps();

    quint64 roundFrequency(quint64 frequency, unsigned int tsHz);
    quint64 roundFrequencyWithStep(quint64 oldFreq, int steps,\
                                   unsigned int tsHz);

    void changeTxBtn();
    void issueDelayedCommand(cmds cmd);
    void issueDelayedCommandPriority(cmds cmd);
    void issueDelayedCommandUnique(cmds cmd);

    void processModLevel(rigInput source, unsigned char level);

    void processChangingCurrentModLevel(unsigned char level);

    void changeModLabel(rigInput source);
    void changeModLabel(rigInput source, bool updateLevel);

    void changeModLabelAndSlider(rigInput source);

    // Fast command queue:
    void initPeriodicCommands();
    void insertPeriodicCommand(cmds cmd, unsigned char priority);
    void insertPeriodicCommandUnique(cmds cmd);
    void removePeriodicCommand(cmds cmd);

    void insertSlowPeriodicCommand(cmds cmd, unsigned char priority);
    void calculateTimingParameters();

    void changeMode(mode_kind mode);
    void changeMode(mode_kind mode, bool dataOn);

    cmds meterKindToMeterCommand(meterKind m);

    int oldFreqDialVal;

    rigCapabilities rigCaps;
    rigInput currentModSrc = inputUnknown;
    rigInput currentModDataSrc = inputUnknown;
    mode_kind currentMode = modeUSB;
    mode_info currentModeInfo;

    bool haveRigCaps;
    bool amTransmitting;
    bool usingDataMode = false;

    unsigned char micGain=0;
    unsigned char accAGain=0;
    unsigned char accBGain=0;
    unsigned char accGain=0;
    unsigned char usbGain=0;
    unsigned char lanGain=0;


    udpServer* udp = Q_NULLPTR;
    rigCtlD* rigCtl = Q_NULLPTR;
    QThread* serverThread = Q_NULLPTR;

    void bandStackBtnClick();
    bool waitingForBandStackRtn;
    char bandStkBand;
    char bandStkRegCode;

    bool freqLock;

    float tsPlus;
    float tsPlusShift;
    float tsPlusControl;
    float tsPage;
    float tsPageShift;
    float tsWfScroll;

    unsigned int tsPlusHz;
    unsigned int tsPlusShiftHz;
    unsigned int tsPlusControlHz;
    unsigned int tsPageHz;
    unsigned int tsPageShiftHz;
    unsigned int tsWfScrollHz;
    unsigned int tsKnobHz;

    rigstate* rigState = Q_NULLPTR;

    SERVERCONFIG serverConfig;

    static void handleCtrlC(int sig);
};

Q_DECLARE_METATYPE(struct rigCapabilities)
Q_DECLARE_METATYPE(struct freqt)
Q_DECLARE_METATYPE(struct mode_info)
Q_DECLARE_METATYPE(struct udpPreferences)
Q_DECLARE_METATYPE(struct audioPacket)
Q_DECLARE_METATYPE(struct audioSetup)
Q_DECLARE_METATYPE(struct SERVERCONFIG)
Q_DECLARE_METATYPE(struct timekind)
Q_DECLARE_METATYPE(struct datekind)
Q_DECLARE_METATYPE(enum rigInput)
Q_DECLARE_METATYPE(enum meterKind)
Q_DECLARE_METATYPE(enum spectrumMode)
Q_DECLARE_METATYPE(rigstate*)


#endif // WFMAIN_H
