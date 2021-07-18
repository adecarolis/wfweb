#ifndef RIGCOMMANDER_H
#define RIGCOMMANDER_H

#include <QObject>
#include <QDebug>

#include "commhandler.h"
#include "pttyhandler.h"
#include "udphandler.h"
#include "rigidentities.h"
#include "repeaterattributes.h"
#include "freqmemory.h"

// This file figures out what to send to the comm and also
// parses returns into useful things.

// 0xE1 is new default, 0xE0 was before.
// note: using a define because switch case doesn't even work with const unsigned char. Surprised me.
#define compCivAddr 0xE1

enum meterKind {
    meterNone=0,
    meterS,
    meterSWR,
    meterPower,
    meterALC,
    meterComp,
    meterVoltage,
    meterCurrent,
    meterRxdB,
    meterTxMod,
    meterRxAudio,
    meterLatency
};

enum spectrumMode {
    spectModeCenter=0x00,
    spectModeFixed=0x01,
    spectModeScrollC=0x02,
    spectModeScrollF=0x03,
    spectModeUnknown=0xff
};

struct freqt {
    quint64 Hz;
    double MHzDouble;
};

struct datekind {
    uint16_t year;
    unsigned char month;
    unsigned char day;
};

struct timekind {
    unsigned char hours;
    unsigned char minutes;
    bool isMinus;
};

struct rigStateStruct {
    freqt vfoAFreq;
    freqt vfoBFreq;
    unsigned char ptt;
    unsigned char mode;
    unsigned char filter;
    bool datamode;
    // Tones
    quint16 ctcss;
    quint16 tsql;
    quint16 dtcs;
    quint16 csql;
    // Levels
    unsigned char preamp;
    unsigned char modInput;
    unsigned char afGain;
    unsigned char rfGain;
    unsigned char squelch;
    unsigned char txPower;
    unsigned char micGain;
    unsigned char compLevel;
    unsigned char monitorLevel;
    unsigned char voxGain;
    unsigned char antiVoxGain;
    // Meters
    unsigned char sMeter;
    unsigned char powerMeter;
    unsigned char swrMeter;
    unsigned char alcMeter;
    unsigned char compMeter;
    unsigned char voltageMeter;
    unsigned char currentMeter;
};

class rigCommander : public QObject
{
    Q_OBJECT

public:
    rigCommander();
    ~rigCommander();

    bool usingLAN();

public slots:
    void process();
    void commSetup(unsigned char rigCivAddr, QString rigSerialPort, quint32 rigBaudRate,QString vsp);
    void commSetup(unsigned char rigCivAddr, udpPreferences prefs, audioSetup rxSetup, audioSetup txSetup, QString vsp);
    void closeComm();

    // Power:
    void powerOn();
    void powerOff();

    // Spectrum:
    void enableSpectOutput();
    void disableSpectOutput();
    void enableSpectrumDisplay();
    void disableSpectrumDisplay();
    void setSpectrumBounds(double startFreq, double endFreq, unsigned char edgeNumber);
    void setSpectrumMode(spectrumMode spectMode);
    void getSpectrumCenterMode();
    void getSpectrumMode();
    void setSpectrumRefLevel(int level);
    void getSpectrumRefLevel();
    void getSpectrumRefLevel(unsigned char mainSub);
    void setScopeSpan(char span);
    void getScopeSpan(bool isSub);
    void getScopeSpan();

    void setScopeEdge(char edge);
    void getScopeEdge();
    void getScopeMode();

    // Frequency, Mode, BSR:
    void setFrequency(freqt freq);
    void getFrequency();
    void setMode(unsigned char mode, unsigned char modeFilter);
    void setMode(mode_info);
    void getMode();
    void setDataMode(bool dataOn, unsigned char filter);
    void getDataMode();
    void getBandStackReg(char band, char regCode);
    void getRitEnabled();
    void getRitValue();
    void setRitValue(int ritValue);
    void setRitEnable(bool ritEnabled);

    // PTT, ATU, ATT, Antenna, and Preamp:
    void getPTT();
    void setPTT(bool pttOn);
    void startATU();
    void setATU(bool enabled);
    void getATUStatus();
    void getAttenuator();
    void getPreamp();
    void getAntenna();
    void setAttenuator(unsigned char att);
    void setPreamp(unsigned char pre);
    void setAntenna(unsigned char ant);

    // Repeater:
    void setDuplexMode(duplexMode dm);
    void getDuplexMode();
    void getTransmitFrequency();
    void setTone(quint16 tone);
    void setTSQL(quint16 tsql);
    void getTSQL();
    void getTone();
    void setDTCS(quint16 dcscode, bool tinv, bool rinv);
    void getDTCS();
    void setRptAccessMode(rptAccessTxRx ratr);
    void getRptAccessMode();

    // Get Levels:
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
    void getModInput(bool dataOn);
    void getModInputLevel(rigInput input);

    // Set Levels:
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
    void setModInput(rigInput input, bool dataOn);
    void setModInputLevel(rigInput input, unsigned char level);

    // NB, NR, IP+:
    void setIPP(bool enabled);
    void getIPP();
    // Maybe put some of these into a struct?
    // setReceiverDSPParam(dspParam param);
    //void getNRLevel();
    //void getNREnabled();
    //void getNBLevel();
    //void getNBEnabled();
    //void getNotchEnabled();
    //void getNotchLevel();
    //void setNotchEnabled(bool enabled);
    //void setNotchLevel(unsigned char level);


    // Meters:
    void getSMeter();
    void getRFPowerMeter();
    void getSWRMeter();
    void getALCMeter();
    void getCompReductionMeter();
    void getVdMeter();
    void getIDMeter();
    void getMeters(meterKind meter); // all supported meters per transmit or receive

    // Rig ID and CIV:
    void getRigID();
    void findRigs();
    void setCIVAddr(unsigned char civAddr);

    // Calibration:
    void getRefAdjustCourse();
    void getRefAdjustFine();
    void setRefAdjustCourse(unsigned char level);
    void setRefAdjustFine(unsigned char level);

    // Time and Date:
    void setTime(timekind t);
    void setDate(datekind d);
    void setUTCOffset(timekind t);


    // Satellite:
    void setSatelliteMode(bool enabled);
    void getSatelliteMode();

    // UDP:
    void handleNewData(const QByteArray& data);
    void receiveAudioData(const audioPacket& data);
    void handleSerialPortError(const QString port, const QString errorText);
    void changeLatency(const quint16 value);
    void dataFromServer(QByteArray data);
    void receiveBaudRate(quint32 baudrate);

    // Speech:
    void sayFrequency();
    void sayMode();
    void sayAll();

    // Housekeeping:
    void handleStatusUpdate(const QString text);
    void getDebug();

signals:
    // Communication:
    void commReady();
    void haveSerialPortError(const QString port, const QString errorText);
    void haveStatusUpdate(const QString text);
    void dataForComm(const QByteArray &outData);

    // UDP:
    void haveChangeLatency(quint16 value);
    void haveDataForServer(QByteArray outData);
    void haveAudioData(audioPacket data);
    void initUdpHandler();
    void haveSetVolume(unsigned char level);
    void haveBaudRate(quint32 baudrate);

    // Spectrum:
    void haveSpectrumData(QByteArray spectrum, double startFreq, double endFreq); // pass along data to UI
    void haveSpectrumBounds();
    void haveScopeSpan(freqt span, bool isSub);
    void haveSpectrumMode(spectrumMode spectmode);
    void haveScopeEdge(char edge);
    void haveSpectrumRefLevel(int level);

    // Rig ID:
    void haveRigID(rigCapabilities rigCaps);
    void discoveredRigID(rigCapabilities rigCaps);

    // Frequency, Mode, data, and bandstack:
    void haveFrequency(freqt freqStruct);
    void haveMode(unsigned char mode, unsigned char filter);
    void haveDataMode(bool dataModeEnabled);
    void haveBandStackReg(freqt f, char mode, char filter, bool dataOn);
    void haveRitEnabled(bool ritEnabled);
    void haveRitFrequency(int ritHz);

    // Repeater:
    void haveDuplexMode(duplexMode);
    void haveRptAccessMode(rptAccessTxRx ratr);
    void haveTone(quint16 tone);
    void haveTSQL(quint16 tsql);
    void haveDTCS(quint16 dcscode, bool tinv, bool rinv);

    // Levels:
    void haveRfGain(unsigned char level);
    void haveAfGain(unsigned char level);
    void haveSql(unsigned char level);
    void haveTxPower(unsigned char level);
    void haveMicGain(unsigned char level);
    void haveCompLevel(unsigned char level);
    void haveMonitorLevel(unsigned char level);
    void haveVoxGain(unsigned char gain);
    void haveAntiVoxGain(unsigned char gain);

    // Modulation source and gain:
    void haveModInput(rigInput input, bool isData);
    void haveLANGain(unsigned char gain);
    void haveUSBGain(unsigned char gain);
    void haveACCGain(unsigned char gain, unsigned char ab);
    void haveModSrcGain(rigInput input, unsigned char gain);

    // Meters:
    void haveMeter(meterKind meter, unsigned char level);
    void haveSMeter(unsigned char level);
    void haveRFMeter(unsigned char level);
    void haveSWRMeter(unsigned char);
    void haveALCMeter(unsigned char);
    void haveCompMeter(unsigned char dbreduction);
    void haveVdMeter(unsigned char voltage);
    void haveIdMeter(unsigned char current);

    // Calibration:
    void haveRefAdjustCourse(unsigned char level);
    void haveRefAdjustFine(unsigned char level);

    // PTT and ATU:
    void havePTTStatus(bool pttOn);
    void haveATUStatus(unsigned char status);
    void haveAttenuator(unsigned char att);
    void havePreamp(unsigned char pre);
    void haveAntenna(unsigned char ant);

    // Rig State
    void stateInfo(rigStateStruct* state);

    // Housekeeping:
    void getMoreDebug();
    void finished();

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
    freqt parseFrequency(QByteArray data, unsigned char lastPosition); // supply index where Mhz is found
    QByteArray makeFreqPayload(double frequency);
    QByteArray makeFreqPayload(freqt freq);
    QByteArray encodeTone(quint16 tone, bool tinv, bool rinv);
    QByteArray encodeTone(quint16 tone);
    unsigned char convertNumberToHex(unsigned char num);
    quint16 decodeTone(QByteArray eTone);
    quint16 decodeTone(QByteArray eTone, bool &tinv, bool &rinv);

    void parseMode();
    void parseSpectrum();
    void parseWFData();
    void parseSpectrumRefLevel();
    void parseDetailedRegisters1A05();
    void parseRegisters1A();
    void parseRegister1B();
    void parseRegisters1C();
    void parseRegister16();
    void parseRegister21();
    void parseBandStackReg();
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
    mode_info createMode(mode_kind m, unsigned char reg, QString name);
    centerSpanData createScopeCenter(centerSpansType s, QString name);

    commHandler* comm = Q_NULLPTR;
    pttyHandler* ptty = Q_NULLPTR;
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
    
    rigStateStruct rigState;

    bool haveRigCaps;
    model_kind model;
    quint8 spectSeqMax;
    quint16 spectAmpMax;
    quint16 spectLenMax;
    spectrumMode oldScopeMode;

    bool usingNativeLAN; // indicates using OEM LAN connection (705,7610,9700,7850)
    bool lookingForRig;
    bool foundRig;

    double frequencyMhz;
    unsigned char civAddr;
    unsigned char incomingCIVAddr; // place to store the incoming CIV.
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
