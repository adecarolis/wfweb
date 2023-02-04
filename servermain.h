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
#include "audiodevices.h"

#include "udpserver.h"
#include "rigctld.h"
#include "signal.h"

#include <qserialportinfo.h>

#include <deque>
#include <memory>

#include <portaudio.h>
#ifdef Q_OS_WIN
#include "RtAudio.h"
#else
#include "rtaudio/RtAudio.h"
#endif

namespace Ui {
class wfmain;
}

class servermain : public QObject
{
    Q_OBJECT

public:
    servermain(const QString logFile);
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
    void sendCommSetup(unsigned char rigCivAddr, QString rigSerialPort, quint32 rigBaudRate,QString vsp, quint16 tcp, quint8 wf);
    void sendCommSetup(unsigned char rigCivAddr, udpPreferences prefs, audioSetup rxSetup, audioSetup txSetup, QString vsp, quint16 tcp);
    void sendCloseComm();
    void sendChangeLatency(quint16 latency);
    void initServer();
    void sendRigCaps(rigCapabilities caps);
    void requestRigState();
    void stateUpdated();

private slots:

    void receiveCommReady();
    void receivePTTstatus(bool pttOn);

    void receiveFoundRigID(rigCapabilities rigCaps);
    void receivePortError(errorType err);
    void receiveBaudRate(quint32 baudrate);

    void handlePttLimit();
    void receiveStatusUpdate(networkStatus status);
    void receiveStateInfo(rigstate* state);
    void connectToRig(RIGCONFIG* rig);
    void updateAudioDevices();

private:
    QSettings *settings=Q_NULLPTR;
    void loadSettings();

    void openRig();
    void powerRigOff();
    void powerRigOn();
    QStringList portList;
    QString serialPortRig;

    QTimer * delayedCommand;
    QTimer * pttTimer;
    uint16_t loopTickCounter;
    uint16_t slowCmdNum;
    QString lastMessage="";

    void makeRig();
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

    bool usingLAN = false;

    struct preferences {
        unsigned char radioCIVAddr;
        bool CIVisRadioModel;
        bool forceRTSasPTT;
        QString serialPortRadio;
        quint32 serialPortBaud;
        unsigned char localAFgain;
        audioSetup rxAudio;
        audioSetup txAudio;
        rigCapabilities rigCaps;
        bool haveRigCaps = false;
        quint16 tcpPort;
        audioType audioSystem;
    } prefs;

    preferences defPrefs;
    udpPreferences udpPrefs;
    udpPreferences udpDefPrefs;

    // Configuration for audio output and input.
    audioSetup rxSetup;
    audioSetup txSetup;

    audioSetup serverRxSetup;
    audioSetup serverTxSetup;


    void setDefPrefs(); // populate default values to default prefs

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

    rigstate* rigState = Q_NULLPTR;

    audioDevices* audioDev = Q_NULLPTR;

    SERVERCONFIG serverConfig;
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
Q_DECLARE_METATYPE(struct networkStatus)
Q_DECLARE_METATYPE(enum rigInput)
Q_DECLARE_METATYPE(QList<radio_cap_packet>)
Q_DECLARE_METATYPE(enum meterKind)
Q_DECLARE_METATYPE(enum spectrumMode)
Q_DECLARE_METATYPE(rigstate*)
Q_DECLARE_METATYPE(codecType)
Q_DECLARE_METATYPE(errorType)
Q_DECLARE_METATYPE(enum duplexMode)
Q_DECLARE_METATYPE(enum rptAccessTxRx)
Q_DECLARE_METATYPE(struct rptrTone_t)
Q_DECLARE_METATYPE(struct rptrAccessData_t)


#endif // WFMAIN_H
