#ifndef WFMAIN_H
#define WFMAIN_H

#include <QMainWindow>
#include <QThread>
#include <QString>
#include <QVector>
#include <QTimer>


#include "commhandler.h"
#include "rigcommander.h"
#include <qcustomplot.h>
#include<qserialportinfo.h>

namespace Ui {
class wfmain;
}

class wfmain : public QMainWindow
{
    Q_OBJECT

public:
    explicit wfmain(QWidget *parent = 0);
    ~wfmain();

signals:
    void getFrequency();
    void setFrequency(double freq);
    void getMode();
    void setMode(char modeIndex);
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
    void startATU();
    void setATU(bool atuEnabled);
    void getRigID();
    void spectOutputEnable();
    void spectOutputDisable();
    void scopeDisplayEnable();
    void scopeDisplayDisable();
    void setScopeCenterMode(bool centerEnable);
    void setScopeSpan(char span);
    void setScopeEdge(char edge);

private slots:
    void on_startBtn_clicked();
    void receiveFreq(double);
    void receiveMode(QString);
    void receiveSpectrumData(QByteArray spectrum, double startFreq, double endFreq);
    void receivePTTstatus(bool pttOn);
    void receiveDataModeStatus(bool dataOn);
    void receiveBandStackReg(float freq, char mode, bool dataOn); // freq, mode, (filter,) datamode
    void receiveRfGain(unsigned char level);
    void receiveAfGain(unsigned char level);
    void receiveSql(unsigned char level);

    void handlePlotClick(QMouseEvent *);
    void handlePlotDoubleClick(QMouseEvent *);
    void handleWFClick(QMouseEvent *);
    void handleWFDoubleClick(QMouseEvent *);
    void runDelayedCommand();
    void showStatusBarText(QString text);

    void on_getFreqBtn_clicked();

    void on_getModeBtn_clicked();

    void on_debugBtn_clicked();

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

private:
    Ui::wfmain *ui;
    QCustomPlot *plot; // line plot
    QCustomPlot *wf; // waterfall image
    QCPItemTracer * tracer; // marker of current frequency
    //commHandler *comm;
    void setAppTheme(bool isDark);
    void setPlotTheme(QCustomPlot *plot, bool isDark);
    void getInitialRigState();
    QWidget * theParent;
    QStringList portList;

    rigCommander * rig;
    QThread * rigThread;
    QCPColorMap * colorMap;
    QCPColorMapData * colorMapData;
    QCPColorScale * colorScale;
    QTimer * delayedCommand;

    QStringList modes;
    int currentModeIndex;
    QStringList spans;
    QStringList edges;
    QStringList commPorts;

    quint16 spectWidth;
    quint16 wfLength;

    quint16 spectRowCurrent;

    QByteArray spectrumPeaks;

    QVector <QByteArray> wfimage;

    bool drawPeaks;
    bool freqTextSelected;
    void checkFreqSel();

    double oldLowerFreq;
    double oldUpperFreq;
    double freqMhz;
    double knobFreqMhz;
    enum cmds {cmdNone, cmdGetRigID, cmdGetFreq, cmdGetMode, cmdGetDataMode, cmdSetDataModeOn, cmdSetDataModeOff,
              cmdSpecOn, cmdSpecOff, cmdDispEnable, cmdDispDisable, cmdGetRxGain, cmdGetAfGain,
              cmdGetSql};
    cmds cmdOut;
    QVector <cmds> cmdOutQue;
    int oldFreqDialVal;

    void bandStackBtnClick();
    bool waitingForBandStackRtn;
    char bandStkBand;
    char bandStkRegCode;
};

#endif // WFMAIN_H
