#ifndef WFMAIN_H
#define WFMAIN_H

#include <QMainWindow>
#include <QCloseEvent>
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
#include "freqmemory.h"
#include "rigidentities.h"
#include "repeaterattributes.h"

#include "calibrationwindow.h"
#include "repeatersetup.h"
#include "satellitesetup.h"
#include "transceiveradjustments.h"
#include "udpserversetup.h"
#include "udpserver.h"
#include "qledlabel.h"
#include "rigctld.h"
#include "aboutbox.h"

#include <qcustomplot.h>
#include <qserialportinfo.h>

#include <deque>
#include <memory>

namespace Ui {
class wfmain;
}

class wfmain : public QMainWindow
{
    Q_OBJECT

public:
    explicit wfmain(const QString serialPortCL, const QString hostCL, const QString settingsFile, QWidget *parent = 0);
    QString serialPortCL;
    QString hostCL;
    ~wfmain();

signals:
    // Basic to rig:
    void setCIVAddr(unsigned char newRigCIVAddr);

    // Power
    void sendPowerOn();
    void sendPowerOff();

    // Frequency, mode, band:
    void getFrequency();
    void setFrequency(freqt freq);
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
    void getTxPower();
    void getMicGain();
    void getSpectrumRefLevel();
    void getModInputLevel(rigInput input);

    // Level set:
    void setRfGain(unsigned char level);
    void setAfGain(unsigned char level);
    void setSql(unsigned char level);
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
    void sendServerConfig(SERVERCONFIG conf);
    void sendRigCaps(rigCapabilities caps);

private slots:
    void updateSizes(int tabIndex);
    void shortcutF1();
    void shortcutF2();
    void shortcutF3();
    void shortcutF4();
    void shortcutF5();

    void shortcutF6();
    void shortcutF7();
    void shortcutF8();
    void shortcutF9();
    void shortcutF10();
    void shortcutF11();
    void shortcutF12();

    void shortcutControlT();
    void shortcutControlR();
    void shortcutControlI();
    void shortcutControlU();

    void shortcutStar();
    void shortcutSlash();
    void shortcutMinus();
    void shortcutPlus();
    void shortcutShiftMinus();
    void shortcutShiftPlus();
    void shortcutControlMinus();
    void shortcutControlPlus();

    void shortcutPageUp();
    void shortcutPageDown();

    void shortcutF();
    void shortcutM();

    void handlePttLimit(); // hit at 3 min transmit length

    void receiveCommReady();
    void receiveFreq(freqt);
    void receiveMode(unsigned char mode, unsigned char filter);
    void receiveSpectrumData(QByteArray spectrum, double startFreq, double endFreq);
    void receiveSpectrumMode(spectrumMode spectMode);
    void receiveSpectrumSpan(freqt freqspan, bool isSub);
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


    void receiveATUStatus(unsigned char atustatus);
    void receivePreamp(unsigned char pre);
    void receiveAttenuator(unsigned char att);
    void receiveAntennaSel(unsigned char ant, bool rx);
    void receiveRigID(rigCapabilities rigCaps);
    void receiveFoundRigID(rigCapabilities rigCaps);
    void receiveSerialPortError(QString port, QString errorText);
    void receiveStatusUpdate(QString errorText);
    void handlePlotClick(QMouseEvent *);
    void handlePlotDoubleClick(QMouseEvent *);
    void handleWFClick(QMouseEvent *);
    void handleWFDoubleClick(QMouseEvent *);
    void handleWFScroll(QWheelEvent *);
    void handlePlotScroll(QWheelEvent *);
    void sendRadioCommandLoop();
    void showStatusBarText(QString text);
    void serverConfigRequested(SERVERCONFIG conf, bool store);
    void receiveBaudRate(quint32 baudrate);

    void setRadioTimeDateSend();


    // void on_getFreqBtn_clicked();

    // void on_getModeBtn_clicked();

    // void on_debugBtn_clicked();

    void on_clearPeakBtn_clicked();

    void on_drawPeakChk_clicked(bool checked);

    void on_fullScreenChk_clicked(bool checked);

    void on_goFreqBtn_clicked();

    void on_f0btn_clicked();
    void on_f1btn_clicked();
    void on_f2btn_clicked();
    void on_f3btn_clicked();
    void on_f4btn_clicked();
    void on_f5btn_clicked();
    void on_f6btn_clicked();
    void on_f7btn_clicked();
    void on_f8btn_clicked();
    void on_f9btn_clicked();
    void on_fDotbtn_clicked();



    void on_fBackbtn_clicked();

    void on_fCEbtn_clicked();

    void on_fEnterBtn_clicked();

    void on_scopeBWCombo_currentIndexChanged(int index);

    void on_scopeEdgeCombo_currentIndexChanged(int index);

    // void on_modeSelectCombo_currentIndexChanged(int index);

    void on_useDarkThemeChk_clicked(bool checked);

    void on_modeSelectCombo_activated(int index);

    // void on_freqDial_actionTriggered(int action);

    void on_freqDial_valueChanged(int value);

    void on_band6mbtn_clicked();

    void on_band10mbtn_clicked();

    void on_band12mbtn_clicked();

    void on_band15mbtn_clicked();

    void on_band17mbtn_clicked();

    void on_band20mbtn_clicked();

    void on_band30mbtn_clicked();

    void on_band40mbtn_clicked();

    void on_band60mbtn_clicked();

    void on_band80mbtn_clicked();

    void on_band160mbtn_clicked();

    void on_bandGenbtn_clicked();

    void on_aboutBtn_clicked();

    void on_fStoBtn_clicked();

    void on_fRclBtn_clicked();

    void on_rfGainSlider_valueChanged(int value);

    void on_afGainSlider_valueChanged(int value);

    void on_tuneNowBtn_clicked();

    void on_tuneEnableChk_clicked(bool checked);

    void on_exitBtn_clicked();

    void on_pttOnBtn_clicked();

    void on_pttOffBtn_clicked();

    void on_saveSettingsBtn_clicked();


    void on_debugBtn_clicked();

    void on_pttEnableChk_clicked(bool checked);

    void on_lanEnableBtn_clicked(bool checked);

    void on_ipAddressTxt_textChanged(QString text);

    void on_controlPortTxt_textChanged(QString text);

    void on_usernameTxt_textChanged(QString text);

    void on_passwordTxt_textChanged(QString text);

    void on_audioOutputCombo_currentIndexChanged(int value);

    void on_audioInputCombo_currentIndexChanged(int value);

    void on_toFixedBtn_clicked();

    void on_connectBtn_clicked();

    void on_rxLatencySlider_valueChanged(int value);

    void on_txLatencySlider_valueChanged(int value);

    void on_audioRXCodecCombo_currentIndexChanged(int value);

    void on_audioTXCodecCombo_currentIndexChanged(int value);

    void on_audioSampleRateCombo_currentIndexChanged(QString text);

    void on_vspCombo_currentIndexChanged(int value);

    void on_scopeEnableWFBtn_clicked(bool checked);

    void on_sqlSlider_valueChanged(int value);

    void on_modeFilterCombo_activated(int index);

    void on_dataModeBtn_toggled(bool checked);

    void on_udpServerSetupBtn_clicked();

    void on_transmitBtn_clicked();

    void on_adjRefBtn_clicked();

    void on_satOpsBtn_clicked();

    void on_txPowerSlider_valueChanged(int value);

    void on_micGainSlider_valueChanged(int value);

    void on_scopeRefLevelSlider_valueChanged(int value);

    void on_useSystemThemeChk_clicked(bool checked);

    void on_modInputCombo_activated(int index);

    void on_modInputDataCombo_activated(int index);

    void on_tuneLockChk_clicked(bool checked);

    void on_spectrumModeCombo_currentIndexChanged(int index);

    void on_serialEnableBtn_clicked(bool checked);

    void on_tuningStepCombo_currentIndexChanged(int index);

    void on_serialDeviceListCombo_activated(const QString &arg1);

    void on_rptSetupBtn_clicked();

    void on_attSelCombo_activated(int index);

    void on_preampSelCombo_activated(int index);

    void on_antennaSelCombo_activated(int index);

    void on_rxAntennaCheck_clicked(bool value);

    void on_wfthemeCombo_activated(int index);

    void on_rigPowerOnBtn_clicked();

    void on_rigPowerOffBtn_clicked();

    void on_ritTuneDial_valueChanged(int value);

    void on_ritEnableChk_clicked(bool checked);

    void on_band23cmbtn_clicked();

    void on_band70cmbtn_clicked();

    void on_band2mbtn_clicked();

    void on_band4mbtn_clicked();

    void on_band630mbtn_clicked();

    void on_band2200mbtn_clicked();

    void on_bandAirbtn_clicked();

    void on_bandWFMbtn_clicked();

    void on_rigCIVManualAddrChk_clicked(bool checked);

    void on_rigCIVaddrHexLine_editingFinished();

    void on_baudRateCombo_activated(int);

    void on_wfLengthSlider_valueChanged(int value);

    void on_pollingBtn_clicked();

    void on_wfAntiAliasChk_clicked(bool checked);

    void on_wfInterpolateChk_clicked(bool checked);

    void on_meter2selectionCombo_activated(int index);

private:
    Ui::wfmain *ui;
    void closeEvent(QCloseEvent *event);
    QSettings *settings=Q_NULLPTR;
    void loadSettings();
    void saveSettings();
    QCustomPlot *plot; // line plot
    QCustomPlot *wf; // waterfall image
    QCPItemLine * freqIndicatorLine;
    //commHandler *comm;
    void setAppTheme(bool isCustom);
    void setPlotTheme(QCustomPlot *plot, bool isDark);
    void prepareWf();
    void prepareWf(unsigned int wfLength);
    void showHideSpectrum(bool show);
    void getInitialRigState();
    void setBandButtons();
    void showButton(QPushButton *btn);
    void hideButton(QPushButton *btn);

    void openRig();
    void powerRigOff();
    void powerRigOn();
    QStringList portList;
    QString serialPortRig;

    QShortcut *keyF1;
    QShortcut *keyF2;
    QShortcut *keyF3;
    QShortcut *keyF4;
    QShortcut *keyF5;

    QShortcut *keyF6;
    QShortcut *keyF7;
    QShortcut *keyF8;
    QShortcut *keyF9;
    QShortcut *keyF10;
    QShortcut *keyF11;
    QShortcut *keyF12;

    QShortcut *keyControlT;
    QShortcut *keyControlR;
    QShortcut *keyControlI;
    QShortcut *keyControlU;

    QShortcut *keyStar;
    QShortcut *keySlash;
    QShortcut *keyMinus;
    QShortcut *keyPlus;

    QShortcut *keyShiftMinus;
    QShortcut *keyShiftPlus;
    QShortcut *keyControlMinus;
    QShortcut *keyControlPlus;
    QShortcut *keyQuit;

    QShortcut *keyPageUp;
    QShortcut *keyPageDown;

    QShortcut *keyF;
    QShortcut *keyM;

    QShortcut *keyDebug;


    rigCommander * rig=Q_NULLPTR;
    QThread* rigThread = Q_NULLPTR;
    QCPColorMap * colorMap;
    QCPColorMapData * colorMapData;
    QCPColorScale * colorScale;
    QTimer * delayedCommand;
    QTimer * pttTimer;
    uint16_t loopTickCounter;
    uint16_t slowCmdNum;

    void setupPlots();
    void makeRig();
    void rigConnections();
    void removeRig();
    void findSerialPort();

    void setupKeyShortcuts();
    void setupMainUI();
    void setUIToPrefs();
    void setSerialDevicesUI();
    void setAudioDevicesUI();
    void setServerToPrefs();
    void setInitialTiming();
    void getSettingsFilePath(QString settingsFile);

    QStringList modes;
    int currentModeIndex;
    QStringList spans;
    QStringList edges;
    QStringList commPorts;
    QLabel* rigStatus;
    QLabel* rigName;
    QLedLabel* pttLed;
    QLedLabel* connectedLed;

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

    enum cmds {cmdNone, cmdGetRigID, cmdGetRigCIV, cmdGetFreq, cmdSetFreq, cmdGetMode, cmdSetMode, cmdGetDataMode, cmdSetModeFilter,
              cmdSetDataModeOn, cmdSetDataModeOff, cmdGetRitEnabled, cmdGetRitValue,
              cmdSpecOn, cmdSpecOff, cmdDispEnable, cmdDispDisable, cmdGetRxGain, cmdSetRxRfGain, cmdGetAfGain, cmdSetAfGain,
              cmdGetSql, cmdSetSql, cmdGetATUStatus, cmdSetATU, cmdStartATU, cmdGetSpectrumMode, cmdGetSpectrumSpan, cmdScopeCenterMode, cmdScopeFixedMode, cmdGetPTT, cmdSetPTT,
              cmdGetTxPower, cmdSetTxPower, cmdGetMicGain, cmdSetMicGain, cmdSetModLevel, cmdGetSpectrumRefLevel, cmdGetDuplexMode, cmdGetModInput, cmdGetModDataInput,
              cmdGetCurrentModLevel, cmdStartRegularPolling, cmdStopRegularPolling, cmdQueNormalSpeed,
              cmdGetVdMeter, cmdGetIdMeter, cmdGetSMeter, cmdGetCenterMeter, cmdGetPowerMeter, cmdGetSWRMeter, cmdGetALCMeter, cmdGetCompMeter, cmdGetTxRxMeter,
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
        // plot scheme
    } prefs;

    preferences defPrefs;
    udpPreferences udpPrefs;
    udpPreferences udpDefPrefs;

    // Configuration for audio output and input.
    audioSetup rxSetup;
    audioSetup txSetup;

    colors defaultColors;

    void setDefaultColors(); // populate with default values
    void useColors(); // set the plot up
    void setDefPrefs(); // populate default values to default prefs
    void setTuningSteps();

    quint64 roundFrequency(quint64 frequency, unsigned int tsHz);
    quint64 roundFrequencyWithStep(quint64 oldFreq, int steps,\
                                   unsigned int tsHz);

    void setUIFreq(double frequency);
    void setUIFreq();

    void changeTxBtn();
    void issueDelayedCommand(cmds cmd);
    void issueDelayedCommandPriority(cmds cmd);
    void issueDelayedCommandUnique(cmds cmd);
    void changeSliderQuietly(QSlider *slider, int value);
    void statusFromSliderPercent(QString name, int percentValue);
    void statusFromSliderRaw(QString name, int rawValue);

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

    calibrationWindow *cal;
    repeaterSetup *rpt;
    satelliteSetup *sat;
    transceiverAdjustments *trxadj;
    udpServerSetup *srv;
    aboutbox *abtBox;


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


    SERVERCONFIG serverConfig;
};

Q_DECLARE_METATYPE(struct rigCapabilities)
Q_DECLARE_METATYPE(struct freqt)
Q_DECLARE_METATYPE(struct mode_info)
Q_DECLARE_METATYPE(struct udpPreferences)
Q_DECLARE_METATYPE(struct rigStateStruct)
Q_DECLARE_METATYPE(struct audioPacket)
Q_DECLARE_METATYPE(struct audioSetup)
Q_DECLARE_METATYPE(struct timekind)
Q_DECLARE_METATYPE(struct datekind)
Q_DECLARE_METATYPE(enum rigInput)
Q_DECLARE_METATYPE(enum meterKind)
Q_DECLARE_METATYPE(enum spectrumMode)


#endif // WFMAIN_H
