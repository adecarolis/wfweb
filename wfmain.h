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
    void getDebug();
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
    void handlePlotClick(QMouseEvent *);
    void handlePlotDoubleClick(QMouseEvent *);
    void handleWFClick(QMouseEvent *);
    void handleWFDoubleClick(QMouseEvent *);
    void runDelayedCommand();

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

    void on_modeSelectCombo_currentIndexChanged(int index);

    void on_useDarkThemeChk_clicked(bool checked);

private:
    Ui::wfmain *ui;
    QCustomPlot *plot; // line plot
    QCustomPlot *wf; // waterfall image
    QCPItemTracer * tracer; // marker of current frequency
    //commHandler *comm;
    void setAppTheme(bool isDark);
    void setPlotTheme(QCustomPlot *plot, bool isDark);
    QWidget * theParent;

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
    enum cmds {cmdGetFreq, cmdGetMode, cmdGetDataMode};
    cmds cmdOut;

};

#endif // WFMAIN_H
