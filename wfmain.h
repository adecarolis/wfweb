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
#include "rigstate.h"
#include "freqmemory.h"
#include "rigidentities.h"
#include "repeaterattributes.h"
#include "memories.h"

#include "packettypes.h"
#include "calibrationwindow.h"
#include "repeatersetup.h"
#include "satellitesetup.h"
#include "transceiveradjustments.h"
#include "cwsender.h"
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

#define numColorPresetsTotal (5)

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
    void handleLogText(QString text);

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
    void sendLevel(cmds cmd, unsigned char level);
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
    void setFrequency(unsigned char vfo, freqt freq);
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
    void requestRigState();
    void stateUpdated();
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

    // Signals to forward incoming data onto other areas
    void haveMemory(memoryType mem);

private slots:
    void receiveValue(cacheItem val);
    void setAudioDevicesUI();
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
    void receiveFreq(freqt);
    void receiveMode(modeInfo mode);
    void receiveSpectrumData(scopeData data);
    void receivespectrumMode_t(spectrumMode_t spectMode);
    void receiveSpectrumSpan(freqt freqspan, bool isSub);
    void receivePTTstatus(bool pttOn);
    void receiveDataModeStatus(unsigned char data, unsigned char filter);
    void receiveBandStackReg(freqt f, char mode, char filter, bool dataOn); // freq, mode, (filter,) datamode
    void receiveRITStatus(bool ritEnabled);
    void receiveRITValue(int ritValHz);
    void receiveModInput(rigInput input, unsigned char data);
    //void receiveDuplexMode(duplexMode_t dm);
    void receivePassband(quint16 pass);
    void receiveMonitorGain(unsigned char pass);
    void receiveNBLevel(unsigned char pass);
    void receiveNRLevel(unsigned char pass);
    void receiveCwPitch(unsigned char pitch);
    void receivePBTInner(unsigned char level);
    void receivePBTOuter(unsigned char level);
    void receiveVox(bool en);
    void receiveMonitor(bool en);
    void receiveComp(bool en);
    void receiveNB(bool en);
    void receiveNR(bool en);
    void receiveTuningStep(unsigned char step);

    // Levels:
    void receiveRfGain(unsigned char level);
    void receiveAfGain(unsigned char level);
    void receiveSql(unsigned char level);
    void receiveIFShift(unsigned char level);

    // 'change' from data in transceiver controls window:
    void changeIFShift(unsigned char level);
    void changePBTInner(unsigned char level);
    void changePBTOuter(unsigned char level);
    void receiveTxPower(unsigned char power);
    void receiveMicGain(unsigned char gain);
    void receiveCompLevel(unsigned char compLevel);
    void receiveVoxGain(unsigned char voxGain);
    void receiveAntiVoxGain(unsigned char antiVoxGain);
    void receiveSpectrumRefLevel(int level);

    // Meters:
    void receiveMeter(meter_t meter, unsigned char level);
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
    void receivePortError(errorType err);
    void receiveStatusUpdate(networkStatus status);
    void receiveNetworkAudioLevels(networkAudioLevels l);
    void handlePlotClick(QMouseEvent *);
    void handlePlotMouseRelease(QMouseEvent *);
    void handlePlotMouseMove(QMouseEvent *);
    void handlePlotDoubleClick(QMouseEvent *);
    void handleWFClick(QMouseEvent *);
    void handleWFDoubleClick(QMouseEvent *);
    void handleWFScroll(QWheelEvent *);
    void handlePlotScroll(QWheelEvent *);
    void showStatusBarText(QString text);
    void receiveBaudRate(quint32 baudrate);
    void radioSelection(QList<radio_cap_packet> radios);


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


    // void on_getFreqBtn_clicked();

    // void on_getModeBtn_clicked();

    // void on_debugBtn_clicked();

    void on_clearPeakBtn_clicked();

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
    void on_usbControllerBtn_clicked();

    void on_usbControllersResetBtn_clicked();

    void on_enableUsbChk_clicked(bool checked);

    void on_scopeBWCombo_currentIndexChanged(int index);

    void on_scopeEdgeCombo_currentIndexChanged(int index);

    void on_modeSelectCombo_activated(int index);

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

    void on_monitorSlider_valueChanged(int value);

    void on_monitorCheck_clicked(bool checked);

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

    void on_audioDuplexCombo_currentIndexChanged(int value);

    void on_audioOutputCombo_currentIndexChanged(int value);

    void on_audioInputCombo_currentIndexChanged(int value);

    void on_toFixedBtn_clicked();

    void on_connectBtn_clicked();

    void on_rxLatencySlider_valueChanged(int value);

    void on_txLatencySlider_valueChanged(int value);

    void on_audioRXCodecCombo_currentIndexChanged(int value);

    void on_audioTXCodecCombo_currentIndexChanged(int value);

    void on_audioSampleRateCombo_currentIndexChanged(int value);

    void on_vspCombo_currentIndexChanged(int value);

    void on_scopeEnableWFBtn_stateChanged(int state);

    void on_sqlSlider_valueChanged(int value);

    void on_modeFilterCombo_activated(int index);

    void on_datamodeCombo_activated(int index);

    void on_transmitBtn_clicked();

    void on_adjRefBtn_clicked();

    void on_satOpsBtn_clicked();

    void on_txPowerSlider_valueChanged(int value);

    void on_micGainSlider_valueChanged(int value);

    void on_scopeRefLevelSlider_valueChanged(int value);

    void on_useSystemThemeChk_clicked(bool checked);

    void on_modInputCombo_activated(int index);

    void on_modInputData1Combo_activated(int index);
    void on_modInputData2Combo_activated(int index);
    void on_modInputData3Combo_activated(int index);

    void on_tuneLockChk_clicked(bool checked);

    void on_spectrumMode_tCombo_currentIndexChanged(int index);

    void on_serialEnableBtn_clicked(bool checked);

    void on_tuningStepCombo_currentIndexChanged(int index);

    void on_serialDeviceListCombo_textActivated(const QString &arg1);

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

    void on_wfAntiAliasChk_clicked(bool checked);

    void on_wfInterpolateChk_clicked(bool checked);

    void on_meter2selectionCombo_activated(int index);

    void on_waterfallFormatCombo_activated(int index);

    void on_enableRigctldChk_clicked(bool checked);

    void on_rigctldPortTxt_editingFinished();

    void on_tcpServerPortTxt_editingFinished();

    void on_moreControlsBtn_clicked();

    void on_memoriesBtn_clicked();

    void on_useCIVasRigIDChk_clicked(bool checked);

    void receiveStateInfo(rigstate* state);

    void on_settingsList_currentRowChanged(int currentRow);

    void on_setClockBtn_clicked();

    void on_serverEnableCheckbox_clicked(bool checked);
    void on_serverControlPortText_textChanged(QString text);
    void on_serverCivPortText_textChanged(QString text);
    void on_serverAudioPortText_textChanged(QString text);
    void on_serverTXAudioOutputCombo_currentIndexChanged(int value);
    void on_serverRXAudioInputCombo_currentIndexChanged(int value);
    void onServerUserFieldChanged();

    void on_serverAddUserBtn_clicked();

    void on_useRTSforPTTchk_clicked(bool checked);

    void on_radioStatusBtn_clicked();

    void on_audioSystemCombo_currentIndexChanged(int value);

    void on_topLevelSlider_valueChanged(int value);

    void on_botLevelSlider_valueChanged(int value);

    void on_underlayBufferSlider_valueChanged(int value);

    void on_underlayNone_toggled(bool checked);

    void on_underlayPeakHold_toggled(bool checked);

    void on_underlayPeakBuffer_toggled(bool checked);

    void on_underlayAverageBuffer_toggled(bool checked);

    void on_colorSetBtnGrid_clicked();

    void on_colorSetBtnPlotBackground_clicked();

    void on_colorSetBtnText_clicked();

    void on_colorSetBtnSpecLine_clicked();

    void on_colorSetBtnSpecFill_clicked();

    void on_colorEditPlotBackground_editingFinished();

    void on_colorPopOutBtn_clicked();

    void on_colorPresetCombo_currentIndexChanged(int index);

    void on_colorEditSpecLine_editingFinished();

    void on_colorEditGrid_editingFinished();

    void on_colorEditText_editingFinished();

    void on_colorEditSpecFill_editingFinished();

    void on_colorSetBtnAxis_clicked();

    void on_colorEditAxis_editingFinished();

    void on_colorSetBtnUnderlayLine_clicked();

    void on_colorEditUnderlayLine_editingFinished();

    void on_colorSetBtnUnderlayFill_clicked();

    void on_colorEditUnderlayFill_editingFinished();

    void on_colorSetBtnwfBackground_clicked();

    void on_colorEditWfBackground_editingFinished();

    void on_colorSetBtnWfGrid_clicked();

    void on_colorEditWfGrid_editingFinished();

    void on_colorSetBtnWfAxis_clicked();

    void on_colorEditWfAxis_editingFinished();

    void on_colorSetBtnWfText_clicked();

    void on_colorEditWfText_editingFinished();

    void on_colorSetBtnTuningLine_clicked();

    void on_colorEditTuningLine_editingFinished();

    void on_colorSetBtnPassband_clicked();

    void on_colorEditPassband_editingFinished();

    void on_colorSetBtnPBT_clicked();

    void on_colorEditPBT_editingFinished();

    void on_colorSetBtnMeterLevel_clicked();

    void on_colorEditMeterLevel_editingFinished();

    void on_colorSetBtnMeterAvg_clicked();

    void on_colorEditMeterAvg_editingFinished();

    void on_colorSetBtnMeterScale_clicked();

    void on_colorEditMeterScale_editingFinished();

    void on_colorSetBtnMeterText_clicked();

    void on_colorEditMeterText_editingFinished();

    void on_colorSetBtnClusterSpots_clicked();

    void on_colorEditClusterSpots_editingFinished();

    void on_colorRenamePresetBtn_clicked();

    void on_colorRevertPresetBtn_clicked();

    void on_colorSetBtnMeterPeakLevel_clicked();

    void on_colorEditMeterPeakLevel_editingFinished();

    void on_colorSetBtnMeterPeakScale_clicked();

    void on_colorEditMeterPeakScale_editingFinished();

    void on_colorSavePresetBtn_clicked();

    void on_showLogBtn_clicked();

    void on_audioSystemServerCombo_currentIndexChanged(int index);

    void on_customEdgeBtn_clicked();

    void on_clusterUdpEnable_clicked(bool enable);
    void on_clusterTcpEnable_clicked(bool enable);
    void on_clusterUdpPortLineEdit_editingFinished();
    void on_clusterServerNameCombo_currentTextChanged(QString text);
    void on_clusterServerNameCombo_currentIndexChanged(int index);
    void on_clusterTcpPortLineEdit_editingFinished();
    void on_clusterUsernameLineEdit_editingFinished();
    void on_clusterPasswordLineEdit_editingFinished();
    void on_clusterTimeoutLineEdit_editingFinished();
    void on_clusterPopOutBtn_clicked();
    void on_clusterSkimmerSpotsEnable_clicked(bool enable);

    void on_clickDragTuningEnableChk_clicked(bool checked);

    void receiveClusterOutput(QString text);
    void receiveSpots(QList<spotData> spots);

    void on_autoPollBtn_clicked(bool checked);

    void on_manualPollBtn_clicked(bool checked);

    void on_pollTimeMsSpin_valueChanged(int arg1);

    void on_autoSSBchk_clicked(bool checked);

    void on_cwButton_clicked();

    void on_rigCreatorBtn_clicked();

private:
    Ui::wfmain *ui;
    void closeEvent(QCloseEvent *event);
    QString logFilename;
    bool debugMode;
    QString version;
    QSettings *settings=Q_NULLPTR;
    void loadSettings();
    void saveSettings();

    void createSettingsListItems();
    void connectSettingsList();

    void initLogging();
    QTimer logCheckingTimer;
    int logCheckingOldPosition = 0;

    QCustomPlot *plot; // line plot
    QCustomPlot *wf; // waterfall image
    QCPItemLine * freqIndicatorLine;
    QCPItemRect* passbandIndicator;
    QCPItemRect* pbtIndicator;
    QCPItemText* oorIndicator;
    void setAppTheme(bool isCustom);
    void prepareWf();
    void prepareWf(unsigned int wfLength);
    void preparePlasma();
    bool plasmaPrepared = false;
    void computePlasma();
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

    void setupPlots();
    void makeRig();
    void rigConnections();
    void removeRig();
    void findSerialPort();

    void setupKeyShortcuts();
    void setupMainUI();
    void setUIToPrefs();
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

    quint16 spectWidth;
    quint16 wfLength;
    bool spectrumDrawLock;

    QByteArray spectrumPeaks;
    QVector <double> spectrumPlasmaLine;
    QVector <QByteArray> spectrumPlasma;
    unsigned int spectrumPlasmaSize = 64;
    underlay_t underlayMode = underlayNone;
    QMutex plasmaMutex;
    void resizePlasmaBuffer(int newSize);
    void clearPlasmaBuffer();

    double plotFloor = 0;
    double plotCeiling = 160;
    double wfFloor = 0;
    double wfCeiling = 160;
    double oldPlotFloor = -1;
    double oldPlotCeiling = 999;
    double passbandWidth = 0.0;

    double mousePressFreq = 0.0;
    double mouseReleaseFreq = 0.0;

    QVector <QByteArray> wfimage;
    unsigned int wfLengthMax;

    bool onFullscreen;
    bool freqTextSelected;
    void checkFreqSel();
    void setUISpectrumControlsToMode(spectrumMode_t smode);

    double oldLowerFreq;
    double oldUpperFreq;
    freqt freq;
    freqt freqb;
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

    colorPrefsType colorPreset[numColorPresetsTotal];

    preferences prefs;
    preferences defPrefs;
    udpPreferences udpPrefs;
    udpPreferences udpDefPrefs;

    // Configuration for audio output and input.
    audioSetup rxSetup;
    audioSetup txSetup;

    void setDefaultColors(int presetNumber); // populate with default values

    void useColors(); // set the plot up
    void setDefPrefs(); // populate default values to default prefs
    void setTuningSteps();
    void setColorElement(QColor color, QLedLabel *led, QLabel *label);
    void setColorElement(QColor color, QLedLabel *led, QLineEdit *lineText);
    void setColorElement(QColor color, QLedLabel *led, QLabel *label, QLineEdit *lineText);
    QColor getColorFromPicker(QColor initialColor);
    void getSetColor(QLedLabel *led, QLabel *label);
    void getSetColor(QLedLabel *led, QLineEdit *line);
    QString setColorFromString(QString aarrggbb, QLedLabel *led);
    void setDefaultColorPresets();
    void loadColorPresetToUIandPlots(int presetNumber);
    void useColorPreset(colorPrefsType *cp);
    void useCurrentColorPreset();
    void setEditAndLedFromColor(QColor c, QLineEdit *e, QLedLabel *d);
    void setColorButtonOperations(QColor *colorStore, QLineEdit *e, QLedLabel *d);
    void setColorLineEditOperations(QColor *colorStore, QLineEdit *e, QLedLabel *d);

    void calculateTimingParameters();
    void initPeriodicCommands();
    void changePollTiming(int timing_ms, bool setUI=false);


    void detachSettingsTab();
    void reattachSettingsTab();
    void prepareSettingsWindow();
    QWidget *settingsWidgetWindow;
    QWidget *settingsTab;
    QGridLayout *settingsWidgetLayout;
    QTabWidget *settingsWidgetTab;
    bool settingsTabisAttached = true;

    quint64 roundFrequency(quint64 frequency, unsigned int tsHz);
    quint64 roundFrequencyWithStep(quint64 oldFreq, int steps,\
                                   unsigned int tsHz);

    void setUIFreq(double frequency);
    void setUIFreq();

    void changeTxBtn();
    void changeSliderQuietly(QSlider *slider, int value);
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

    int oldFreqDialVal;

    QHash<unsigned char,QString> rigList;
    rigCapabilities rigCaps;

    rigInput currentModDataOffSrc = rigInput(inputUnknown);
    rigInput currentModData1Src = rigInput(inputUnknown);
    rigInput currentModData2Src = rigInput(inputUnknown);
    rigInput currentModData3Src = rigInput(inputUnknown);

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
    transceiverAdjustments *trxadj = Q_NULLPTR;
    cwSender *cw = Q_NULLPTR;
    controllerSetup* usbWindow = Q_NULLPTR;
    aboutbox *abtBox = Q_NULLPTR;
    selectRadio *selRad = Q_NULLPTR;
    loggingWindow *logWindow = Q_NULLPTR;
    rigCreator *creator = Q_NULLPTR;

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

    passbandActions passbandAction = passbandStatic;
    double PBTInner = 0.0;
    double PBTOuter = 0.0;
    double passbandCenterFrequency = 0.0;
    double pbtDefault = 0.0;
    quint16 cwPitch = 600;

    availableBands lastRequestedBand=bandGen;

    SERVERCONFIG serverConfig;
    void serverAddUserLine(const QString& user, const QString& pass, const int& type);

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
    QList<clusterSettings> clusters;
    QMutex clusterMutex;
    QColor clusterColor;
    audioDevices* audioDev = Q_NULLPTR;
    QImage lcdImage;
};

Q_DECLARE_METATYPE(udpPreferences)
Q_DECLARE_METATYPE(audioPacket)
Q_DECLARE_METATYPE(audioSetup)
Q_DECLARE_METATYPE(SERVERCONFIG)
Q_DECLARE_METATYPE(networkStatus)
Q_DECLARE_METATYPE(networkAudioLevels)
Q_DECLARE_METATYPE(spotData)
Q_DECLARE_METATYPE(radio_cap_packet)
Q_DECLARE_METATYPE(rigstate*)
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
