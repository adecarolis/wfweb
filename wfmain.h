#ifndef WFMAIN_H
#define WFMAIN_H

#include <QMainWindow>
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

#include "calibrationwindow.h"
#include "satellitesetup.h"
#include "udpserversetup.h"
#include "udpserver.h"

#include <qcustomplot.h>
#include <qserialportinfo.h>

namespace Ui {
class wfmain;
}

class wfmain : public QMainWindow
{
    Q_OBJECT

public:
    explicit wfmain(const QString serialPortCL, const QString hostCL, QWidget *parent = 0);
    QString serialPortCL;
    QString hostCL;
    ~wfmain();

signals:
    void getFrequency();
    void setFrequency(double freq);
    void getMode();
    void setMode(unsigned char modeIndex, unsigned char modeFilter);
    void setDataMode(bool dataOn);
    void getDataMode();
    void getPTT();
    void setPTT(bool pttOn);
    void getBandStackReg(char band, char regCode);
    void getRfGain();
    void getAfGain();
    void getSql();
    void getDebug();
    void setRfGain(unsigned char level);
    void setAfGain(unsigned char level);
    void setSql(unsigned char level);
    void startATU();
    void setATU(bool atuEnabled);
    void getATUStatus();
    void getRigID(); // this is the model of the rig
    void getRigCIV(); // get the rig's CIV addr
    void spectOutputEnable();
    void spectOutputDisable();
    void scopeDisplayEnable();
    void scopeDisplayDisable();
    void setScopeCenterMode(bool centerEnable);
    void setScopeSpan(char span);
    void setScopeEdge(char edge);
    void setScopeFixedEdge(double startFreq, double endFreq, unsigned char edgeNumber);
    void getScopeMode();
    void getScopeEdge();
    void getScopeSpan();
    void sayFrequency();
    void sayMode();
    void sayAll();
    void sendCommSetup(unsigned char rigCivAddr, QString rigSerialPort, quint32 rigBaudRate);
    void sendCommSetup(unsigned char rigCivAddr, QString ip, quint16 cport, quint16 sport, quint16 aport, 
        QString username, QString password, quint16 buffer, quint16 rxsample, quint8 rxcodec, quint16 txsample, quint8 txcodec);
    void sendCloseComm();
    void sendChangeBufferSize(quint16 value);
    void initServer();
    void sendServerConfig(SERVERCONFIG conf);

private slots:
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

    void on_startBtn_clicked();
    void receiveCommReady();
    void receiveFreq(double);
    void receiveMode(unsigned char mode, unsigned char filter);
    void receiveSpectrumData(QByteArray spectrum, double startFreq, double endFreq);
    void receiveSpectrumFixedMode(bool isFixed);
    void receivePTTstatus(bool pttOn);
    void receiveDataModeStatus(bool dataOn);
    void receiveBandStackReg(float freq, char mode, bool dataOn); // freq, mode, (filter,) datamode
    void receiveRfGain(unsigned char level);
    void receiveAfGain(unsigned char level);
    void receiveSql(unsigned char level);
    void receiveATUStatus(unsigned char atustatus);
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
    void runDelayedCommand();
    void showStatusBarText(QString text);
    void serverConfigRequested(SERVERCONFIG conf, bool store);

    // void on_getFreqBtn_clicked();

    // void on_getModeBtn_clicked();

    // void on_debugBtn_clicked();

    void on_stopBtn_clicked();

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


    void on_scopeCenterModeChk_clicked(bool checked);

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

    void on_aboutQtBtn_clicked();

    void on_fStoBtn_clicked();

    void on_fRclBtn_clicked();

    void on_rfGainSlider_valueChanged(int value);

    void on_afGainSlider_valueChanged(int value);

    void on_drawTracerChk_toggled(bool checked);

    void on_tuneNowBtn_clicked();

    void on_tuneEnableChk_clicked(bool checked);

    void on_exitBtn_clicked();

    void on_pttOnBtn_clicked();

    void on_pttOffBtn_clicked();

    void on_saveSettingsBtn_clicked();


    void on_debugBtn_clicked();

    void on_pttEnableChk_clicked(bool checked);

    void on_lanEnableChk_clicked(bool checked);

    void on_ipAddressTxt_textChanged(QString text);

    void on_controlPortTxt_textChanged(QString text);

    void on_serialPortTxt_textChanged(QString text);

    void on_audioPortTxt_textChanged(QString text);

    void on_usernameTxt_textChanged(QString text);

    void on_passwordTxt_textChanged(QString text);

    void on_audioOutputCombo_currentIndexChanged(QString text);

    void on_audioInputCombo_currentIndexChanged(QString text);

    void on_toFixedBtn_clicked();

    void on_connectBtn_clicked();

    void on_audioBufferSizeSlider_valueChanged(int value);

    void on_audioRXCodecCombo_currentIndexChanged(int value);

    void on_audioTXCodecCombo_currentIndexChanged(int value);

    void on_audioSampleRateCombo_currentIndexChanged(QString text);

    void on_scopeEnableWFBtn_clicked(bool checked);

    void on_sqlSlider_valueChanged(int value);

    void on_modeFilterCombo_activated(int index);

    void on_dataModeBtn_toggled(bool checked);

    void on_udpServerSetupBtn_clicked();

private:
    Ui::wfmain *ui;
    QSettings settings;
    void loadSettings();
    void saveSettings();
    QCustomPlot *plot; // line plot
    QCustomPlot *wf; // waterfall image
    QCPItemTracer * tracer; // marker of current frequency
    //commHandler *comm;
    void setAppTheme(bool isDark);
    void setPlotTheme(QCustomPlot *plot, bool isDark);
    void prepareWf();
    void getInitialRigState();
    void openRig();
    QWidget * theParent;
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


    rigCommander * rig=Q_NULLPTR;
    QThread* rigThread = Q_NULLPTR;
    QCPColorMap * colorMap;
    QCPColorMapData * colorMapData;
    QCPColorScale * colorScale;
    QTimer * delayedCommand;
    QTimer * pttTimer;


    QStringList modes;
    int currentModeIndex;
    QStringList spans;
    QStringList edges;
    QStringList commPorts;
    QLabel* rigStatus;

    quint16 spectWidth;
    quint16 wfLength;

    quint16 spectRowCurrent;

    QByteArray spectrumPeaks;

    QVector <QByteArray> wfimage;

    bool onFullscreen;
    bool drawPeaks;
    bool freqTextSelected;
    void checkFreqSel();

    double oldLowerFreq;
    double oldUpperFreq;
    double freqMhz;
    double knobFreqMhz;
    enum cmds {cmdNone, cmdGetRigID, cmdGetRigCIV, cmdGetFreq, cmdGetMode, cmdGetDataMode, cmdSetDataModeOn, cmdSetDataModeOff,
              cmdSpecOn, cmdSpecOff, cmdDispEnable, cmdDispDisable, cmdGetRxGain, cmdGetAfGain,
              cmdGetSql, cmdGetATUStatus, cmdScopeCenterMode, cmdScopeFixedMode};
    cmds cmdOut;
    QVector <cmds> cmdOutQue;
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
        QColor Dark_PlotFreqTracer;

        QColor Light_PlotBackground;
        QColor Light_PlotAxisPen;
        QColor Light_PlotLegendTextColor;
        QColor Light_PlotLegendBorderPen;
        QColor Light_PlotLegendBrush;
        QColor Light_PlotTickLabel;
        QColor Light_PlotBasePen;
        QColor Light_PlotTickPen;
        QColor Light_PlotFreqTracer;

    } colorScheme;

    struct preferences {
        bool useFullScreen;
        bool useDarkMode;
        bool drawPeaks;
        bool drawTracer;
        QString stylesheetPath;
        unsigned char radioCIVAddr;
        QString serialPortRadio;
        quint32 serialPortBaud;
        bool enablePTT;
        bool niceTS;
        bool enableLAN;
        QString ipAddress;
        quint16 controlLANPort;
        quint16 serialLANPort;
        quint16 audioLANPort;
        QString username;
        QString password;
        QString audioOutput;
        QString audioInput;
        quint16 audioRXBufferSize;
        quint16 audioRXSampleRate;
        quint8 audioRXCodec;
        quint16 audioTXSampleRate;
        quint8 audioTXCodec;
    } prefs;

    preferences defPrefs;
    colors defaultColors;

    void setDefaultColors(); // populate with default values
    void useColors(); // set the plot up
    void setDefPrefs(); // populate default values to default prefs

    int oldFreqDialVal;

    rigCapabilities rigCaps;
    bool haveRigCaps;

    calibrationWindow *cal;
    satelliteSetup *sat;
    udpServerSetup *srv;
    udpServer *udp;
    QThread *serverThread = Q_NULLPTR;

    void bandStackBtnClick();
    bool waitingForBandStackRtn;
    char bandStkBand;
    char bandStkRegCode;

    SERVERCONFIG serverConfig;

};

Q_DECLARE_METATYPE(struct rigCapabilities) ;


#endif // WFMAIN_H
