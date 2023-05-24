#ifndef RIGCOMMANDER_H
#define RIGCOMMANDER_H

#include <QObject>
#include <QMutexLocker>
#include <QSettings>
#include <QRegularExpression>
#include <QDebug>

#include "wfviewtypes.h"
#include "commhandler.h"
#include "pttyhandler.h"
#include "udphandler.h"
#include "rigidentities.h"
#include "repeaterattributes.h"
#include "freqmemory.h"
#include "tcpserver.h"
#include "cachingqueue.h"

#include "rigstate.h"

// This file figures out what to send to the comm and also
// parses returns into useful things.

// 0xE1 is new default, 0xE0 was before.
// note: using a define because switch case doesn't even work with const unsigned char. Surprised me.
#define compCivAddr 0xE1

//#define DEBUG_PARSE // Enable to output Info messages every 10s with command parse timing.

typedef QHash<unsigned char, QString> rigTypedef;

class rigCommander : public QObject
{
    Q_OBJECT

public:
    explicit rigCommander(QObject* parent=nullptr);
    explicit rigCommander(quint8 guid[GUIDLEN], QObject* parent = nullptr);
    ~rigCommander();

    bool usingLAN();

    quint8* getGUID();

public slots:
    void process();
    void commSetup(rigTypedef rigList, unsigned char rigCivAddr, QString rigSerialPort, quint32 rigBaudRate, QString vsp, quint16 tcp, quint8 wf);
    void commSetup(rigTypedef rigList, unsigned char rigCivAddr, udpPreferences prefs, audioSetup rxSetup, audioSetup txSetup, QString vsp, quint16 tcp);
    void closeComm();
    void stateUpdated();
    void setRTSforPTT(bool enabled);

    // Power:
    void powerOn();
    void powerOff();

    // Spectrum:
    void enableSpectOutput();
    void disableSpectOutput();
    void enableSpectrumDisplay();
    void disableSpectrumDisplay();
    void setSpectrumBounds(double startFreq, double endFreq, unsigned char edgeNumber);
    void setspectrumMode_t(spectrumMode_t spectMode);
    void getSpectrumCenterMode();
    void getspectrumMode_t();
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
    void setFrequency(unsigned char vfo, freqt freq);
    void getFrequency(unsigned char vfo);
    void getFrequency();
    void selectVFO(vfo_t vfo);
    void equalizeVFOsAB();
    void equalizeVFOsMS();
    void exchangeVFOs();
    void setMode(unsigned char mode, unsigned char modeFilter);
    void setMode(modeInfo);
    void getMode();
    void getMode(unsigned char vfo);
    void setDataMode(bool dataOn, unsigned char filter);
    void getDataMode();
    void getTuningStep();
    void setTuningStep(unsigned char step);
    void getSplit();
    void setSplit(bool splitEnabled);
    void getBandStackReg(char band, char regCode);
    void getRitEnabled();
    void getRitValue();
    void setRitValue(int ritValue);
    void setRitEnable(bool ritEnabled);
    void setPassband(quint16 pass);

    // PTT, ATU, ATT, Antenna, and Preamp:
    void getPTT();
    void setPTT(bool pttOn);
    void sendCW(QString textToSend);
    void sendStopCW();
    void startATU();
    void setATU(bool enabled);
    void getATUStatus();
    void getAttenuator();
    void getPreamp();
    void getAntenna();
    void setAttenuator(unsigned char att);
    void setPreamp(unsigned char pre);
    void setAntenna(unsigned char ant, bool rx);
    void setNB(bool enabled);
    void getNB();
    void setNR(bool enabled);
    void getNR();
    void setAutoNotch(bool enabled);
    void getAutoNotch();
    void setToneEnabled(bool enabled);
    void getToneEnabled();
    void setToneSql(bool enabled);
    void getToneSqlEnabled();
    void setCompressor(bool enabled);
    void getCompressor();
    void setMonitor(bool enabled);
    void getMonitor();
    void setVox(bool enabled);
    void getVox();
    void setBreakIn(unsigned char type);
    void getBreakIn();
    void setKeySpeed(unsigned char wpm);
    void getKeySpeed();
    void setManualNotch(bool enabled);
    void getManualNotch();

    void getPassband();
    void getNBLevel();
    void getNRLevel();   void getCwPitch();
    void setCwPitch(unsigned char pitch);
    void getDashRatio();
    void setDashRatio(unsigned char ratio);
    void getPskTone();
    void setPskTone(unsigned char tone);
    void getRttyMark();
    void setRttyMark(unsigned char mark);

    // Repeater:
    void setDuplexMode(duplexMode_t dm);
    void getDuplexMode();
    void setQuickSplit(bool qsOn);
    void getTransmitFrequency();
    void setTone(toneInfo t);
    void setTSQL(toneInfo t);
    void setTone(quint16 t);
    void setTSQL(quint16 t);
    void getTSQL();
    void getTone();
    void setDTCS(quint16 dcscode, bool tinv, bool rinv);
    void getDTCS();
    void setRptAccessMode(rptAccessTxRx_t ratr);
    void setRptAccessMode(rptrAccessData ratr);
    void getRptAccessMode();
    void setRptDuplexOffset(freqt f);
    void getRptDuplexOffset();
    void setMemoryMode();
    void getMemory(quint32 mem);
    void getSatMemory(quint32 mem);
    void clearMemory(quint32 mem);
    void recallMemory(quint32 mem);

    // Get Levels:
    void getLevels(); // all supported levels
    void getRfGain();
    void getAfGain();
    void getSql();
    void getIFShift();
    void getPBTInner();
    void getPBTOuter();
    void getTxLevel();
    void getMicGain();
    void getCompLevel();
    void getMonitorGain();
    void getVoxGain();
    void getAntiVoxGain();
    void getUSBGain();
    void getLANGain();
    void getACCGain();
    void getACCGain(unsigned char ab);
    void getModInput(bool dataOn);
    void getModInputLevel(inputTypes input);
    void getAfMute();
    void getDialLock();

    // Set Levels:
    void setSquelch(unsigned char level);
    void setRfGain(unsigned char level);
    void setAfGain(unsigned char level);
    void setIFShift(unsigned char level);
    void setPBTInner(unsigned char level);
    void setPBTOuter(unsigned char level);
    void setTxPower(unsigned char power);
    void setMicGain(unsigned char gain);
    void setUSBGain(unsigned char gain);
    void setLANGain(unsigned char gain);
    void setACCGain(unsigned char gain);
    void setACCGain(unsigned char gain, unsigned char ab);
    void setCompLevel(unsigned char compLevel);
    void setMonitorGain(unsigned char monitorLevel);
    void setVoxGain(unsigned char gain);
    void setAntiVoxGain(unsigned char gain);
    void setModInput(inputTypes input, bool dataOn);
    void setModInputLevel(inputTypes input, unsigned char level);
    void setAfMute(bool muteOn);
    void setDialLock(bool lockOn);
    void setNBLevel(unsigned char level);
    void setNRLevel(unsigned char level);

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
    void getCenterMeter();
    void getRFPowerMeter();
    void getSWRMeter();
    void getALCMeter();
    void getCompReductionMeter();
    void getVdMeter();
    void getIDMeter();
    void getMeters(meter_t meter); // all supported meters per transmit or receive

    // Rig ID and CIV:
    void getRigID();
    void findRigs();
    void setRigID(unsigned char rigID);
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
    void handlePortError(errorType err);
    void changeLatency(const quint16 value);
    void dataFromServer(QByteArray data);
    void receiveBaudRate(quint32 baudrate);

    // Speech:
    void sayFrequency();
    void sayMode();
    void sayAll();

    // Housekeeping:
    void handleStatusUpdate(const networkStatus status);
    void handleNetworkAudioLevels(networkAudioLevels);
    void radioSelection(QList<radio_cap_packet> radios);
    void radioUsage(quint8 radio, quint8 busy, QString name, QString ip);
    void setCurrentRadio(quint8 radio);
    void sendState();
    void getDebug();
    void receiveCommand(queueItemType type, funcs func, QVariant value);

signals:
    // Communication:
    void commReady();
    void havePortError(errorType err);
    void haveStatusUpdate(const networkStatus status);
    void haveNetworkAudioLevels(const networkAudioLevels l);
    void dataForComm(const QByteArray &outData);
    void toggleRTS(bool rtsOn);
    void setHalfDuplex(bool en);

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
    void havespectrumMode_t(spectrumMode_t spectmode);
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
    void havePassband(quint16 pass);
    void haveCwPitch(unsigned char pitch);
    void havePskTone(unsigned char tone);
    void haveRttyMark(unsigned char mark);
    void haveTuningStep(unsigned char step);

    // Repeater:
    void haveDuplexMode(duplexMode_t);
    void haveRptAccessMode(rptAccessTxRx_t ratr);
    void haveTone(quint16 tone);
    void haveTSQL(quint16 tsql);
    void haveDTCS(quint16 dcscode, bool tinv, bool rinv);
    void haveRptOffsetFrequency(freqt f);
    void haveMemory(memoryType mem);

    // Levels:
    void haveRfGain(unsigned char level);
    void haveAfGain(unsigned char level);
    void haveSql(unsigned char level);
    void havePBTInner(unsigned char level);
    void havePBTOuter(unsigned char level);
    void haveIFShift(unsigned char level);
    void haveTxPower(unsigned char level);
    void haveMicGain(unsigned char level);
    void haveCompLevel(unsigned char level);
    void haveMonitorGain(unsigned char level);
    void haveVoxGain(unsigned char gain);
    void haveAntiVoxGain(unsigned char gain);
    void haveNBLevel(unsigned char level);
    void haveNRLevel(unsigned char level);
    void haveVox(bool en);
    void haveMonitor(bool en);
    void haveComp(bool en);
    void haveNB(bool en);
    void haveNR(bool en);

    // Modulation source and gain:
    void haveModInput(inputTypes input, bool isData);
    void haveLANGain(unsigned char gain);
    void haveUSBGain(unsigned char gain);
    void haveACCGain(unsigned char gain, unsigned char ab);
    void haveModSrcGain(inputTypes input, unsigned char gain);

    // Meters:
    void haveMeter(meter_t meter, unsigned char level);
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
    void haveAntenna(unsigned char ant,bool rx);

    // CW:
    void haveKeySpeed(unsigned char wpm);
    void haveCWBreakMode(unsigned char bmode);
    void haveDashRatio(unsigned char ratio);


    // Rig State
    void stateInfo(rigstate* state);

    // Housekeeping:
    void requestRadioSelection(QList<radio_cap_packet> radios);
    void setRadioUsage(quint8 radio, quint8 busy, QString user, QString ip);
    void selectedRadio(quint8 radio);
    void getMoreDebug();
    void finished();
    void haveReceivedValue(funcs func, QVariant value);

private:
    void commonSetup();
    QByteArray stripData(const QByteArray &data, unsigned char cutPosition);
    void parseData(QByteArray data); // new data come here
    void parseCommand(); // Entry point for complete commands
    unsigned char bcdHexToUChar(unsigned char in);
    unsigned char bcdHexToUChar(unsigned char hundreds, unsigned char tensunits);
    unsigned int bcdHexToUInt(unsigned char hundreds, unsigned char tensunits);
    QByteArray bcdEncodeChar(unsigned char num);
    QByteArray bcdEncodeInt(unsigned int);
    QByteArray setMemory(memoryType mem);
    freqt parseFrequency();
    freqt parseFrequency(QByteArray data, unsigned char lastPosition); // supply index where Mhz is found
    quint64 parseFreqDataToInt(QByteArray data);
    freqt parseFrequencyRptOffset(QByteArray data);
    bool parseMemory(QVector<memParserFormat>* memParser, memoryType* mem);
    QByteArray makeFreqPayloadRptOffset(freqt freq);
    QByteArray makeFreqPayload(double frequency);
    QByteArray makeFreqPayload(freqt freq);
    QByteArray encodeTone(quint16 tone, bool tinv, bool rinv);
    QByteArray encodeTone(quint16 tone);
    unsigned char convertNumberToHex(unsigned char num);
    toneInfo decodeTone(QByteArray eTone);
    //quint16 decodeTone(QByteArray eTone, bool &tinv, bool &rinv);
    uchar makeFilterWidth(ushort width);


    unsigned char audioLevelRxMean[50];
    unsigned char audioLevelRxPeak[50];
    unsigned char audioLevelTxMean[50];
    unsigned char audioLevelTxPeak[50];

    modeInfo parseMode(quint8 mode, quint8 filter);
    bool parseSpectrum(scopeData& d);
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
    bool getCommand(funcs func, QByteArray& payload, int value=INT_MIN);
    QByteArray getLANAddr();
    QByteArray getUSBAddr();
    QByteArray getACCAddr(unsigned char ab);
    void sendDataOut();
    void prepDataAndSend(QByteArray data);
    void debugMe();
    void printHex(const QByteArray &pdata);
    void printHex(const QByteArray &pdata, bool printVert, bool printHoriz);
    modeInfo createMode(rigMode_t m, unsigned char reg, QString name, bool bw);
    centerSpanData createScopeCenter(centerSpansType s, QString name);

    commHandler* comm = Q_NULLPTR;
    pttyHandler* ptty = Q_NULLPTR;
    tcpServer* tcp = Q_NULLPTR;
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
    //double spectrumStartFreq;
    //double spectrumEndFreq;

    struct rigCapabilities rigCaps;
    
    rigstate state;

    bool haveRigCaps=false;
    quint8 model = 0; // Was model_kind but that makes no sense when users can create their own rigs!
    quint8 spectSeqMax;
    quint16 spectAmpMax;
    quint16 spectLenMax;
    spectrumMode_t oldScopeMode;

    bool usingNativeLAN; // indicates using OEM LAN connection (705,7610,9700,7850)
    bool lookingForRig;
    bool foundRig;

    double frequencyMhz;
    unsigned char civAddr;
    unsigned char incomingCIVAddr; // place to store the incoming CIV.
    bool pttAllowed;
    bool useRTSforPTT_isSet = false;
    bool useRTSforPTT_manual = false;


    QString rigSerialPort;
    quint32 rigBaudRate;

    QString ip;
    int cport;
    int sport;
    int aport;
    QString username;
    QString password;

    QString serialPortError;
    unsigned char localVolume=0;
    quint8 guid[GUIDLEN] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
    QHash<unsigned char,QString> rigList;

    quint64 pow10[12] = {
        1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000, 10000000000, 100000000000
    };

    cachingQueue* queue;

#ifdef DEBUG_PARSE
    quint64 averageParseTime=0;
    int numParseSamples = 0;
    int lowParse=9999;
    int highParse=0;
    QTime lastParseReport = QTime::currentTime();
#endif
};

#endif // RIGCOMMANDER_H
