#ifdef BUILD_WFSERVER
#include "servermain.h"
#else

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
#include <QThread>
#include <QMetaType>
#include <QMutex>
#include <QMutexLocker>
#include <QColorDialog>
#include <QColor>
#include <QMap>

#include "logcategories.h"
#include "wfviewtypes.h"
#include "prefs.h"
#include "commhandler.h"
#include "rigcommander.h"
#include "freqmemory.h"
#include "rigidentities.h"
#include "repeaterattributes.h"
#include "memories.h"
#include "firsttimesetup.h"

#include "packettypes.h"
#include "calibrationwindow.h"
#include "repeatersetup.h"
#include "satellitesetup.h"
//#include "transceiveradjustments.h"
#include "cwsender.h"
#include "bandbuttons.h"
#include "frequencyinputwidget.h"
#include "settingswidget.h"
#include "udpserver.h"
#include "qledlabel.h"
#include "rigctld.h"
#include "aboutbox.h"
#include "selectradio.h"
#include "colorprefs.h"
#include "loggingwindow.h"
#include "cluster.h"
#include "audiodevices.h"
#include "sidebandchooser.h"
#include "debugwindow.h"
#include "spectrumscope.h"
#include "tciserver.h"

#include <qcustomplot.h>
#include <qserialportinfo.h>
#include "usbcontroller.h"
#include "controllersetup.h"
#include "rigcreator.h"
#include "cachingqueue.h"

#include <deque>
#include <memory>

#include <portaudio.h>
#ifndef Q_OS_LINUX
#include "RtAudio.h"
#else
#include "rtaudio/RtAudio.h"
#endif

#ifdef USB_CONTROLLER
    #ifdef Q_OS_WIN
        #include <windows.h>
        #include <dbt.h>
        #define USB_HOTPLUG
    #elif defined(Q_OS_LINUX)
        #include <QSocketNotifier>
        #include <libudev.h>
        #define USB_HOTPLUG
    #endif
#endif

namespace Ui {
class wfmain;
}

class wfmain : public QMainWindow
{
    Q_OBJECT

public:
    explicit wfmain(const QString settingsFile, const QString logFile, bool debugMode, QWidget *parent = 0);
    ~wfmain();
    static void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);
    void handleLogText(QPair<QtMsgType,QString> logMessage);

#ifdef USB_HOTPLUG
    #if defined(Q_OS_WIN)
        protected:
            virtual bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result);
    #elif defined(Q_OS_LINUX)
        private slots:
           void uDevEvent();
    #endif
#endif

signals:
    // Signal levels received to other parts of wfview
    void sendLevel(funcs cmd, unsigned char level);
    void usbHotplug();
    // Basic to rig:
    void setCIVAddr(unsigned char newRigCIVAddr);
    void setRigID(unsigned char rigID);
    void setRTSforPTT(bool enabled);

    
    void sendPowerOn();
    void sendPowerOff();

    // Frequency, mode, band:
    void getFrequency();
    void getFrequency(unsigned char);
    void setFrequency(unsigned char receiver, freqt freq);
    void getMode();
    void setMode(unsigned char modeIndex, unsigned char modeFilter);
    void setMode(modeInfo);
    void selectVFO(vfo_t vfo);
    void sendVFOSwap();
    void sendVFOEqualAB();
    void sendVFOEqualMS();
    void setDataMode(bool dataOn, unsigned char filter);
    void getDataMode();
    void getModInput(bool dataOn);
    void setModInput(inputTypes input, bool dataOn);
    void getBandStackReg(char band, char regCode);
    void getDebug();
    void getRitEnabled();
    void getRitValue();
    void setRitValue(int ritValue);
    void setRitEnable(bool ritEnabled);
    void getTuningStep();
    void setTuningStep(unsigned char);

    // Repeater:
    void getDuplexMode();
    void setQuickSplit(bool qsOn);
    void getTone();
    void getTSQL();
    void getDTCS();
    void getRptAccessMode();
    void setRepeaterAccessMode(rptrAccessData rd);
    void setTone(toneInfo t);
    void setTSQL(toneInfo t);
    void getToneEnabled();
    void getTSQLEnabled();
    void setToneEnabled(bool enabled);
    void setTSQLEnabled(bool enabled);
    void setRptDuplexOffset(freqt f);
    void getRptDuplexOffset();
    void setMemory(memoryType mem);
    void getMemory(quint32 mem);
    void getSatMemory(quint32 mem);
    void recallMemory(quint32 mem);
    void clearMemory(quint32 mem);
    void setMemoryMode();
    void setSatelliteMode(bool en);

    // Level get:
    void getLevels(); // get all levels
    void getRfGain();
    void getAfGain();
    void getSql();
    void getIfShift();
    void getPBTInner();
    void getPBTOuter();
    void getTxPower();
    void getMicGain();
    void getSpectrumRefLevel();
    void getModInputLevel(inputTypes input);
    void getMeters(meter_t meter);
    void getPassband();
    void getVoxGain();
    void getAntiVoxGain();
    void getMonitorGain();
    void getNBLevel();
    void getNRLevel();
    void getCompLevel();
    void getCwPitch();

    void getVox();
    void getMonitor();
    void getCompressor();
    void getNB();
    void getNR();

    void getDashRatio();
    void getPskTone();
    void getRttyMark();

    // Level set:
    void setRfGain(unsigned char level);
    void setAfGain(unsigned char level);
    void setSql(unsigned char level);
    void setIFShift(unsigned char level);
    void setPBTInner(unsigned char level);
    void setPBTOuter(unsigned char level);
  
    void setIFShiftWindow(unsigned char level);
    void setPBTInnerWindow(unsigned char level);
    void setPBTOuterWindow(unsigned char level);
    void setMicGain(unsigned char);
    void setCompLevel(unsigned char);
    void setTxPower(unsigned char);
    void setMonitorGain(unsigned char);
    void setVoxGain(unsigned char);
    void setAntiVoxGain(unsigned char);
    void setNBLevel(unsigned char level);
    void setNRLevel(unsigned char level);

    void setVox(bool en);
    void setMonitor(bool en);
    void setCompressor(bool en);
    void setNB(bool en);
    void setNR(bool en);

    void setSpectrumRefLevel(int);

    void setModLevel(inputTypes input, unsigned char level);
    void setACCGain(unsigned char level);
    void setACCAGain(unsigned char level);
    void setACCBGain(unsigned char level);
    void setUSBGain(unsigned char level);
    void setLANGain(unsigned char level);

    void setPassband(quint16 pass);

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

    // CW Keying:
    void sendCW(QString message);
    void stopCW();
    void getKeySpeed();
    void setKeySpeed(unsigned char wpm);
    void setCwPitch(unsigned char pitch);
    void setDashRatio(unsigned char ratio);
    void setCWBreakMode(unsigned char breakMode);
    void getCWBreakMode();

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
    void setScopeMode(spectrumMode_t spectMode);
    void setScopeSpan(char span);
    void setScopeEdge(char edge);
    void setScopeFixedEdge(double startFreq, double endFreq, unsigned char edgeNumber);
    void getScopeMode();
    void getScopeEdge();
    void getScopeSpan();
    void sayFrequency();
    void sayMode();
    void sayAll();
    void sendCommSetup(rigTypedef rigList, unsigned char rigCivAddr, QString rigSerialPort, quint32 rigBaudRate,QString vsp, quint16 tcp, quint8 wf);
    void sendCommSetup(rigTypedef rigList, unsigned char rigCivAddr, udpPreferences prefs, audioSetup rxSetup, audioSetup txSetup, QString vsp, quint16 tcp);
    void sendCloseComm();
    void sendChangeLatency(quint16 latency);
    void initServer();
    void sendRigCaps(rigCapabilities caps);
    void openShuttle();

    void initUsbController(QMutex* mutex,usbDevMap* devs ,QVector<BUTTON>* buts,QVector<KNOB>* knobs);
    void setClusterUdpPort(int port);
    void setClusterEnableUdp(bool udp);
    void setClusterEnableTcp(bool tcp);
    void setClusterServerName(QString name);
    void setClusterTcpPort(int port);
    void setClusterUserName(QString name);
    void setClusterPassword(QString pass);
    void setClusterTimeout(int timeout);
    void setClusterSkimmerSpots(bool enable);
    void setFrequencyRange(double low, double high);
    void sendControllerRequest(USBDEVICE* dev, usbFeatureType request, int val=0, QString text="", QImage* img=Q_NULLPTR, QColor* color=Q_NULLPTR);
    void tciInit(quint16 port);

    // Signals to forward incoming data onto other areas
    void haveMemory(memoryType mem);
    void connectionStatus(bool conn);

private slots:
    // Triggered from external preference changes:
    void extChangedIfPrefs(quint64 items);
    void extChangedColPrefs(quint64 items);
    void extChangedRaPrefs(quint64 items);
    void extChangedRsPrefs(quint64 items);
    void extChangedCtPrefs(quint64 items);
    void extChangedLanPrefs(quint64 items);
    void extChangedClusterPrefs(quint64 items);
    void extChangedUdpPrefs(quint64 items);
    void extChangedServerPrefs(quint64 items);

    void extChangedIfPref(prefIfItem i);
    void extChangedColPref(prefColItem i);
    void extChangedRaPref(prefRaItem i);
    void extChangedRsPref(prefRsItem i);
    void extChangedCtPref(prefCtItem i);
    void extChangedLanPref(prefLanItem i);
    void extChangedClusterPref(prefClusterItem i);
    void extChangedUdpPref(prefUDPItem i);
    void extChangedServerPref(prefServerItem i);

    void handleExtConnectBtn();
    void handleRevertSettingsBtn();

    void receiveScopeSettings(uchar receiver, int theme, quint16 len, int floor, int ceiling);
    void receiveValue(cacheItem val);
    void setAudioDevicesUI();
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
    void shortcutControlP();
    void shortcutControlI();
    void shortcutControlU();

    void shortcutStar();
    void shortcutSlash();
    void shortcutMinus();
    void shortcutPlus();
    void shortcutStepMinus();
    void shortcutStepPlus();
    void shortcutShiftMinus();
    void shortcutShiftPlus();
    void shortcutControlMinus();
    void shortcutControlPlus();

    void shortcutPageUp();
    void shortcutPageDown();

    void shortcutF();
    void shortcutM();

    void handlePttLimit(); // hit at 3 min transmit length

    void doShuttle(bool up, unsigned char level);

    void receiveCommReady();

    void receivePTTstatus(bool pttOn);
    void receiveRITStatus(bool ritEnabled);
    void receiveRITValue(int ritValHz);
    void receiveModInput(rigInput input, unsigned char data);
    //void receiveDuplexMode(duplexMode_t dm);
    void receivePassband(quint16 pass);
    void receiveCwPitch(unsigned char pitch);
    void receiveVox(bool en);
    void receiveMonitor(bool en);
    void receiveComp(bool en);
    void receiveNB(bool en);
    void receiveNR(bool en);
    void receiveTuningStep(unsigned char step);

    // Levels:
    void receiveIFShift(unsigned char level);

    // Meters:
    void receiveMeter(meter_t meter, unsigned char level, unsigned char receiver=0);
//    void receiveSMeter(unsigned char level);
//    void receivePowerMeter(unsigned char level);
//    void receiveALCMeter(unsigned char level);
//    void receiveCompMeter(unsigned char level);


    void receiveATUStatus(unsigned char atustatus);
    void receivePreamp(unsigned char pre, uchar receiver);
    void receiveAttenuator(unsigned char att, uchar receiver);
    void receiveAntennaSel(unsigned char ant, bool rx,uchar receiver);

    void receivePortError(errorType err);
    void receiveStatusUpdate(networkStatus status);
    void receiveNetworkAudioLevels(networkAudioLevels l);

    void showStatusBarText(QString text);
    void receiveBaudRate(quint32 baudrate);
    void radioSelection(QList<radio_cap_packet> radios);

    void dataModeChanged(modeInfo m);

    // Added for RC28/Shuttle support
    void pttToggle(bool);
    void stepUp();
    void stepDown();
    void changeFrequency(int value);
    void setBand(int band);

    void setRadioTimeDateSend();
    void logCheck();
    void setDebugLogging(bool debugModeOn);

    void buttonControl(const COMMAND* cmd);

    void changeFullScreenMode(bool checked);

    void on_freqDial_valueChanged(int value);
    void on_aboutBtn_clicked();

    void on_rfGainSlider_valueChanged(int value);
    void on_afGainSlider_valueChanged(int value);

    void on_monitorSlider_valueChanged(int value);

    void on_monitorLabel_linkActivated(const QString&);

    void on_tuneNowBtn_clicked();
    void on_tuneEnableChk_clicked(bool checked);
    bool on_exitBtn_clicked();
    void on_saveSettingsBtn_clicked();
    void debugBtn_clicked();

    void on_connectBtn_clicked();

    void on_sqlSlider_valueChanged(int value);

    void on_transmitBtn_clicked();
    void on_txPowerSlider_valueChanged(int value);
    void on_micGainSlider_valueChanged(int value);
    void useSystemTheme(bool checked);

    void on_tuneLockChk_clicked(bool checked);

    void on_tuningStepCombo_currentIndexChanged(int index);
    void on_rptSetupBtn_clicked();
    void on_attSelCombo_activated(int index);
    void on_preampSelCombo_activated(int index);
    void on_antennaSelCombo_activated(int index);
    void on_rxAntennaCheck_clicked(bool value);
    void on_rigPowerOnBtn_clicked();
    void on_rigPowerOffBtn_clicked();
    void on_ritTuneDial_valueChanged(int value);
    void on_ritEnableChk_clicked(bool checked);

    void changeMeterType(meter_t m, int meterNum);
    void enableRigCtl(bool enabled);

    void on_memoriesBtn_clicked();

    void on_radioStatusBtn_clicked();

    void on_showLogBtn_clicked();

    void receiveClusterOutput(QString text);

    void on_cwButton_clicked();

    void on_showBandsBtn_clicked();

    void on_showFreqBtn_clicked();

    void on_showSettingsBtn_clicked();

    void on_rigCreatorBtn_clicked();

    void on_scopeMainSubBtn_clicked();
    void on_scopeDualBtn_toggled(bool en);
    void on_dualWatchBtn_toggled(bool en);

    void receiveElapsed(bool sub, qint64 us);
    void connectionTimeout();
    void receiveRigCaps(rigCapabilities* caps);


private:
    Ui::wfmain *ui; // Main UI
    QVector<spectrumScope*>receivers;   // Spectrum Scope items.
    void closeEvent(QCloseEvent *event);
    QString logFilename;
    bool debugMode;
    QString version;
    QSettings *settings=Q_NULLPTR;
    void loadSettings();
    void saveSettings();
    void connectSettingsWidget();

    void initLogging();
    QTimer logCheckingTimer;
    int logCheckingOldPosition = 0;
    QTimer ATUCheckTimer;
    QTimer ConnectionTimer;

    QCustomPlot *plot; // line plot
    QCustomPlot *wf; // waterfall image
    QCPItemLine * freqIndicatorLine;
    QCPItemRect* passbandIndicator;
    QCPItemRect* pbtIndicator;
    QCPItemText* oorIndicator;
    QCPItemText* ovfIndicator;
    void setAppTheme(bool isCustom);

    void getInitialRigState();
    void showButton(QPushButton *btn);
    void hideButton(QPushButton *btn);

    FirstTimeSetup *fts = Q_NULLPTR;
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

    QShortcut *keyH;
    QShortcut *keyK; // alternate +
    QShortcut *keyJ; // alternate -
    QShortcut *keyL;

    QShortcut *keyShiftK;
    QShortcut *keyShiftJ;

    QShortcut *keyControlK;
    QShortcut *keyControlJ;

    QShortcut *keyDebug;


    rigCommander * rig=Q_NULLPTR;
    QThread* rigThread = Q_NULLPTR;
    QCPColorMap * colorMap;
    QCPColorMapData * colorMapData;
    QCPColorScale * colorScale;
    QTimer * delayedCommand;
    QTimer * pttTimer;
    uint16_t loopTickCounter;
    uint16_t slowCmdNum=0;
    uint16_t rapidCmdNum=0;

    void makeRig();
    void rigConnections();
    void removeRig();
    void findSerialPort();

    void setupKeyShortcuts();
    void setupMainUI();
    void configureVFOs();
    void setSerialDevicesUI();
    void setServerToPrefs();
    void setupUsbControllerDevice();
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

    double passbandWidth = 0.0;

    double mousePressFreq = 0.0;
    double mouseReleaseFreq = 0.0;

    QVector <QByteArray> wfimage;

    bool onFullscreen;
    bool freqTextSelected;

    double oldLowerFreq;
    double oldUpperFreq;
    //freqt freq;
    //freqt freqb;
    float tsKnobMHz;

    unsigned char setModeVal=0;
    unsigned char setFilterVal=0;
    qint64 lastFreqCmdTime_ms;

    int pCmdNum = 0;
    int delayedCmdIntervalLAN_ms = 100;
    int delayedCmdIntervalSerial_ms = 100;
    int delayedCmdStartupInterval_ms = 100;
    bool runPeriodicCommands;
    bool usingLAN = false;

    cachingQueue* queue;
    // Radio time sync:
    QTimer *timeSync;
    bool waitingToSetTimeDate;
    void setRadioTimeDatePrep();
    timekind timesetpoint;
    timekind utcsetting;
    datekind datesetpoint;

    freqMemory mem;
    void gotoMemoryPreset(int presetNumber);
    void saveMemoryPreset(int presetNumber);

    colorPrefsType colorPreset[numColorPresetsTotal];

    preferences prefs;
    preferences defPrefs;
    udpPreferences udpPrefs;
    udpPreferences udpDefPrefs;

    void setDefaultColors(int presetNumber); // populate with default values
    void setDefaultColorPresets();
    void useColorPreset(colorPrefsType *cp);

    void useColors(); // set the plot up
    void setDefPrefs(); // populate default values to default prefs
    void setTuningSteps();

    void calculateTimingParameters();
    void initPeriodicCommands();
    void changePollTiming(int timing_ms, bool setUI=false);

    quint64 roundFrequency(quint64 frequency, unsigned int tsHz);
    quint64 roundFrequencyWithStep(quint64 oldFreq, int steps,\
                                   unsigned int tsHz);

    void changeTxBtn();
    void changePrimaryMeter(bool transmitOn);
    void changeSliderQuietly(QSlider *slider, int value);
    void showAndRaiseWidget(QWidget *w);
    void statusFromSliderPercent(QString name, int percentValue);
    void statusFromSliderRaw(QString name, int rawValue);

    void processModLevel(inputTypes source, unsigned char level);

    void processChangingCurrentModLevel(unsigned char level);

    void changeModLabel(rigInput source);
    void changeModLabel(rigInput source, bool updateLevel);

    void changeModLabelAndSlider(rigInput source);

    void changeMode(rigMode_t mode);
    void changeMode(rigMode_t mode, unsigned char data);

    void connectionHandler(bool connect);

    funcs meter_tToMeterCommand(meter_t m);

    void updateUsbButtons();

    void displayReceiver(uchar rx, bool active, bool swtch);


    int oldFreqDialVal;

    QHash<unsigned char,QString> rigList;
    rigCapabilities* rigCaps = Q_NULLPTR;

    rigInput currentModSrc[4];

    rigMode_t currentMode = modeUnknown;
    modeInfo currentModeInfo = modeInfo();

    bool haveRigCaps;
    bool amTransmitting = false;
    bool splitModeEnabled = false;
    unsigned char usingDataMode = 99; // Set to invalid value initially

    // Widgets and Special Windows:
    calibrationWindow *cal = Q_NULLPTR;
    repeaterSetup *rpt = Q_NULLPTR;
    satelliteSetup *sat = Q_NULLPTR;
    //transceiverAdjustments *trxadj = Q_NULLPTR;
    cwSender *cw = Q_NULLPTR;
    controllerSetup* usbWindow = Q_NULLPTR;
    aboutbox *abtBox = Q_NULLPTR;
    selectRadio *selRad = Q_NULLPTR;
    loggingWindow *logWindow = Q_NULLPTR;
    rigCreator *creator = Q_NULLPTR;
    bandbuttons* bandbtns;
    frequencyinputwidget* finputbtns;
    settingswidget* setupui;


    udpServer* udp = Q_NULLPTR;
    QThread* serverThread = Q_NULLPTR;
    rigCtlD* rigCtl = Q_NULLPTR;
    tciServer* tci = Q_NULLPTR;
    QThread* tciThread = Q_NULLPTR;

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

    passbandActions passbandAction = passbandStatic;
    double PBTInner = 0.0;
    double PBTOuter = 0.0;
    double passbandCenterFrequency = 0.0;
    double pbtDefault = 0.0;
    quint16 cwPitch = 600;

    bool subScope = false;

    availableBands lastRequestedBand=bandGen;

    SERVERCONFIG serverConfig;

    quint64 mainElapsed=0;
    quint64 subElapsed=0;
    colorPrefsType* colorPrefs=Q_NULLPTR;

    funcs getInputTypeCommand(inputTypes input);

#if defined (USB_CONTROLLER)
    usbController *usbControllerDev = Q_NULLPTR;
    QThread *usbControllerThread = Q_NULLPTR;
    QString typeName;
    QVector<BUTTON> usbButtons;
    QVector<KNOB> usbKnobs;
    usbDevMap usbDevices;
    QMutex usbMutex;
    qint64 lastUsbNotify=0;

    #if defined (Q_OS_LINUX)
	struct udev* uDev = nullptr;
        struct udev_monitor* uDevMonitor = nullptr;
        QSocketNotifier* uDevNotifier = nullptr;
    #endif
#endif
    memories* memWindow = Q_NULLPTR;
    dxClusterClient* cluster = Q_NULLPTR;
    QThread* clusterThread = Q_NULLPTR;
    QMap<QString, spotData*> clusterSpots;
    QTimer clusterTimer;
    QCPItemText* text=Q_NULLPTR;
    QMutex clusterMutex;
    QColor clusterColor;
    audioDevices* audioDev = Q_NULLPTR;
    QImage lcdImage;
    connectionStatus_t connStatus = connDisconnected;
    uchar currentReceiver = 0;

};

Q_DECLARE_METATYPE(udpPreferences)
Q_DECLARE_METATYPE(audioPacket)
Q_DECLARE_METATYPE(audioSetup)
Q_DECLARE_METATYPE(SERVERCONFIG)
Q_DECLARE_METATYPE(networkStatus)
Q_DECLARE_METATYPE(networkAudioLevels)
Q_DECLARE_METATYPE(spotData)
Q_DECLARE_METATYPE(radio_cap_packet)
Q_DECLARE_METATYPE(BUTTON)
Q_DECLARE_METATYPE(QVector<BUTTON>*)
Q_DECLARE_METATYPE(KNOB)
Q_DECLARE_METATYPE(QVector<KNOB>*)
Q_DECLARE_METATYPE(COMMAND)
Q_DECLARE_METATYPE(QVector<COMMAND>*)
Q_DECLARE_METATYPE(const COMMAND*)
Q_DECLARE_METATYPE(USBDEVICE)
Q_DECLARE_METATYPE(const USBDEVICE*)
Q_DECLARE_METATYPE(codecType)
Q_DECLARE_METATYPE(errorType)
Q_DECLARE_METATYPE(rigTypedef)

//void (*wfmain::logthistext)(QString text) = NULL;

#endif // WFMAIN_H
#endif
