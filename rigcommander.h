#ifndef RIGCOMMANDER_H
#define RIGCOMMANDER_H

#include <QObject>
#include <QDebug>

#include "commhandler.h"
#include "udphandler.h"
#include "rigidentities.h"

// This file figures out what to send to the comm and also
// parses returns into useful things.

// 0xE1 is new default, 0xE0 was before.
// note: using a define because switch case doesn't even work with const unsigned char. Surprised me.
#define compCivAddr 0xE1

enum duplexMode {
    dmSplitOff=0x00,
    dmSplitOn=0x01,
    dmSimplex=0x10,
    dmDupMinus=0x11,
    dmDupPlus=0x12,
    dmDupRPS=0x13,
    dmDupAutoOn=0x26,
    dmDupAutoOff=0x36
};

enum meterKind {
    meterS,
    meterSWR,
    meterPower,
    meterALC,
    meterComp,
    meterVoltage,
    meterCurrent
};

class rigCommander : public QObject
{
    Q_OBJECT

public:
    rigCommander();
    ~rigCommander();

public slots:
    void process();
    void commSetup(unsigned char rigCivAddr, QString rigSerialPort, quint32 rigBaudRate);
    void commSetup(unsigned char rigCivAddr, QString ip, quint16 cport, quint16 sport, quint16 aport, 
                    QString username, QString password, quint16 rxlatency,quint16 txlatency, quint16 rxsample, quint8 rxcodec,quint16 txsample, quint8 txcodec);
    void closeComm();

    void enableSpectOutput();
    void disableSpectOutput();
    void enableSpectrumDisplay();
    void disableSpectrumDisplay();
    void setSpectrumBounds(double startFreq, double endFreq, unsigned char edgeNumber);
    void setSpectrumCenteredMode(bool centerEnable); // centered or band-wise
    void getSpectrumCenterMode();
    void setSpectrumRefLevel(int level);
    void getSpectrumRefLevel();
    void getSpectrumRefLevel(unsigned char mainSub);
    void setScopeSpan(char span);
    void getScopeSpan();
    void setScopeEdge(char edge);
    void getScopeEdge();
    void getScopeMode();
    void setFrequency(double freq);
    void setMode(unsigned char mode, unsigned char modeFilter);
    void getFrequency();
    void getBandStackReg(char band, char regCode);
    void getMode();
    void getPTT();
    void setPTT(bool pttOn);
    void setDataMode(bool dataOn);
    void getDataMode();
    void setDuplexMode(duplexMode dm);
    void getDuplexMode();

    void getLevels(); // all supported levels

    void getRfGain();
    void getAfGain();
    void getSql();
    void getTxLevel();
    void getMicGain();
    void getCompLevel();
    void getMonitorLevel();
    void getVoxGain();
    void getAntiVoxGain();
    void getUSBGain();
    void getLANGain();
    void getACCGain();
    void getACCGain(unsigned char ab);


    void getSMeter();
    void getRFPowerMeter();
    void getSWRMeter();
    void getALCMeter();
    void getCompReductionMeter();
    void getVdMeter();
    void getIDMeter();

    void getMeters(meterKind meter); // all supported meters per transmit or receive

    void setSquelch(unsigned char level);
    void setRfGain(unsigned char level);
    void setAfGain(unsigned char level);
    void setTxPower(unsigned char power);
    void setMicGain(unsigned char gain);
    void setUSBGain(unsigned char gain);
    void setLANGain(unsigned char gain);
    void setACCGain(unsigned char gain);
    void setACCGain(unsigned char gain, unsigned char ab);

    void setCompLevel(unsigned char compLevel);
    void setMonitorLevel(unsigned char monitorLevel);
    void setVoxGain(unsigned char gain);
    void setAntiVoxGain(unsigned char gain);

    void getModInput(bool dataOn);
    void setModInput(rigInput input, bool dataOn);

    void setModInputLevel(rigInput input, unsigned char level);
    void getModInputLevel(rigInput input);

    void startATU();
    void setATU(bool enabled);
    void getATUStatus();
    void getRigID();
    void findRigs();
    void setCIVAddr(unsigned char civAddr);
    void getRefAdjustCourse();
    void getRefAdjustFine();
    void setRefAdjustCourse(unsigned char level);
    void setRefAdjustFine(unsigned char level);
    void handleNewData(const QByteArray& data);
    void receiveAudioData(const AUDIOPACKET& data);
    void handleSerialPortError(const QString port, const QString errorText);
    void handleStatusUpdate(const QString text);
    void changeLatency(const quint16 value);
    void sayFrequency();
    void sayMode();
    void sayAll();
    void getDebug();
    void dataFromServer(QByteArray data);

signals:
    void commReady();
    void haveSpectrumData(QByteArray spectrum, double startFreq, double endFreq); // pass along data to UI
    void haveRigID(rigCapabilities rigCaps);
    void discoveredRigID(rigCapabilities rigCaps);
    void haveSerialPortError(const QString port, const QString errorText);
    void haveStatusUpdate(const QString text);
    void haveFrequency(double frequencyMhz);
    void haveMode(unsigned char mode, unsigned char filter);
    void haveDataMode(bool dataModeEnabled);
    void haveDuplexMode(duplexMode);
    void haveBandStackReg(float freq, char mode, bool dataOn);
    void haveSpectrumBounds();
    void haveScopeSpan(char span);
    void haveSpectrumFixedMode(bool fixedEnabled);
    void haveScopeEdge(char edge);
    void haveSpectrumRefLevel(int level);

    void haveRfGain(unsigned char level);
    void haveAfGain(unsigned char level);
    void haveSql(unsigned char level);
    void haveTxPower(unsigned char level);
    void haveMicGain(unsigned char level);
    void haveCompLevel(unsigned char level);
    void haveMonitorLevel(unsigned char level);
    void haveVoxGain(unsigned char gain);
    void haveAntiVoxGain(unsigned char gain);

    void haveModInput(rigInput input, bool isData);
    void haveLANGain(unsigned char gain);
    void haveUSBGain(unsigned char gain);
    void haveACCGain(unsigned char gain, unsigned char ab);
    void haveModSrcGain(rigInput input, unsigned char gain);

    void haveMeter(meterKind meter, unsigned char level);
    void haveSMeter(unsigned char level);
    void haveRFMeter(unsigned char level);
    void haveSWRMeter(unsigned char);
    void haveALCMeter(unsigned char);
    void haveCompMeter(unsigned char dbreduction);
    void haveVdMeter(unsigned char voltage);
    void haveIdMeter(unsigned char current);

    void thing();
    void haveRefAdjustCourse(unsigned char level);
    void haveRefAdjustFine(unsigned char level);
    void dataForComm(const QByteArray &outData);
    void getMoreDebug();
    void finished();
    void havePTTStatus(bool pttOn);
    void haveATUStatus(unsigned char status);
    void haveChangeLatency(quint16 value);
    void haveDataForServer(QByteArray outData);
    void haveAudioData(AUDIOPACKET data);
    void initUdpHandler();

private:
    void setup();
    QByteArray stripData(const QByteArray &data, unsigned char cutPosition);
    void parseData(QByteArray data); // new data come here
    void parseCommand();
    unsigned char bcdHexToUChar(unsigned char in);
    unsigned char bcdHexToUChar(unsigned char hundreds, unsigned char tensunits);
    unsigned int bcdHexToUInt(unsigned char hundreds, unsigned char tensunits);
    QByteArray bcdEncodeInt(unsigned int);
    void parseFrequency();
    float parseFrequency(QByteArray data, unsigned char lastPosition); // supply index where Mhz is found
    QByteArray makeFreqPayload(double frequency);
    void parseMode();
    void parseSpectrum();
    void parseWFData();
    void parseSpectrumRefLevel();
    void parseDetailedRegisters1A05();
    void parseRegisters1A();
    void parseBandStackReg();
    void parseRegisters1C();
    void parsePTT();
    void parseATU();
    void parseLevels(); // register 0x14
    void sendLevelCmd(unsigned char levAddr, unsigned char level);
    QByteArray getLANAddr();
    QByteArray getUSBAddr();
    QByteArray getACCAddr(unsigned char ab);
    void setModInput(rigInput input, bool dataOn, bool isQuery);
    void sendDataOut();
    void prepDataAndSend(QByteArray data);
    void debugMe();
    void printHex(const QByteArray &pdata);
    void printHex(const QByteArray &pdata, bool printVert, bool printHoriz);

    commHandler * comm=Q_NULLPTR;
    udpHandler* udp=Q_NULLPTR;
    QThread* udpHandlerThread = Q_NULLPTR;

    void determineRigCaps();
    QByteArray payloadIn;
    QByteArray echoPerfix;
    QByteArray replyPrefix;
    QByteArray genericReplyPrefix;

    QByteArray payloadPrefix;
    QByteArray payloadSuffix;

    QByteArray rigData;

    QByteArray spectrumLine;
    double spectrumStartFreq;
    double spectrumEndFreq;

    struct rigCapabilities rigCaps;
    bool haveRigCaps;
    model_kind model;
    quint8 spectSeqMax;
    quint16 spectAmpMax;
    quint16 spectLenMax;
    unsigned char oldScopeMode;

    bool usingNativeLAN; // indicates using OEM LAN connection (705,7610,9700,7850)
    bool lookingForRig;
    bool foundRig;

    double frequencyMhz;
    unsigned char civAddr; // IC-7300: 0x94 is default = 148decimal
    unsigned char incomingCIVAddr; // place to store the incoming CIV.
    //const unsigned char compCivAddr = 0xE1; // 0xE1 is new default, 0xE0 was before.
    bool pttAllowed;

    QString rigSerialPort;
    quint32 rigBaudRate;

    QString ip;
    int cport;
    int sport;
    int aport;
    QString username;
    QString password;

    QString serialPortError;


};

#endif // RIGCOMMANDER_H
