#ifndef ICOMCOMMANDER_H
#define ICOMCOMMANDER_H

#include "rigcommander.h"

// This file figures out what to send to the comm and also
// parses returns into useful things.

// 0xE1 is new default, 0xE0 was before.
// note: using a define because switch case doesn't even work with const quint8. Surprised me.
#define compCivAddr 0xE1

//#define DEBUG_PARSE // Enable to output Info messages every 10s with command parse timing.

class icomCommander : public rigCommander
{
    Q_OBJECT

public:
    explicit icomCommander(rigCommander* parent=nullptr);
    explicit icomCommander(quint8 guid[GUIDLEN], rigCommander* parent = nullptr);
    ~icomCommander();

public slots:
    void process() override;
    void commSetup(rigTypedef rigList, quint8 rigCivAddr, QString rigSerialPort, quint32 rigBaudRate, QString vsp, quint16 tcp, quint8 wf) override;
    void commSetup(rigTypedef rigList, quint8 rigCivAddr, udpPreferences prefs, audioSetup rxSetup, audioSetup txSetup, QString vsp, quint16 tcp) override;
    void closeComm() override;
    void setPTTType(pttType_t) override;

    // Power:
    void powerOn() override;
    void powerOff() override;


    // Rig ID and CIV:
    void getRigID() override;
    void findRigs() override;
    void setRigID(quint8 rigID) override;
    void setCIVAddr(quint8 civAddr) override;

    // UDP:
    void handleNewData(const QByteArray& data) override;
    void receiveBaudRate(quint32 baudrate) override;

    // Housekeeping:
    void receiveCommand(funcs func, QVariant value, uchar receiver) override;
    void setAfGain(quint8 level) override;

signals:
    // All signals are defined in rigcommander.h as they should be common for all rig types.

private:
    void commonSetup();

    void parseData(QByteArray data); // new data come here
    void parseCommand(); // Entry point for complete commands
    quint8 bcdHexToUChar(quint8 in);
    quint8 bcdHexToUChar(quint8 hundreds, quint8 tensunits);
    unsigned int bcdHexToUInt(quint8 hundreds, quint8 tensunits);
    unsigned int bcdHexToUInt(quint8 tenthou, quint8 hundreds, quint8 tensunits);

    QByteArray bcdEncodeChar(quint8 num);
    QByteArray bcdEncodeInt(quint16 num);
    QByteArray bcdEncodeInt(unsigned int num);
    QByteArray setMemory(memoryType mem);
    freqt parseFrequency();
    freqt parseFrequency(QByteArray data, quint8 lastPosition); // supply index where Mhz is found

    freqt parseFreqData(QByteArray data, uchar receiver);
    quint64 parseFreqDataToInt(QByteArray data);
    freqt parseFrequencyRptOffset(QByteArray data);
    bool parseMemory(QVector<memParserFormat>* memParser, memoryType* mem);
    QByteArray makeFreqPayloadRptOffset(freqt freq);
    QByteArray makeFreqPayload(double frequency);
    QByteArray makeFreqPayload(freqt freq,uchar numchars=5);
    QByteArray encodeTone(quint16 tone, bool tinv, bool rinv);
    QByteArray encodeTone(quint16 tone);

    toneInfo decodeTone(QByteArray eTone);
    //quint16 decodeTone(QByteArray eTone, bool &tinv, bool &rinv);
    uchar makeFilterWidth(ushort width, uchar receiver);

    uchar convertNumberToHex(uchar num);

    quint8 audioLevelRxMean[50];
    quint8 audioLevelRxPeak[50];
    quint8 audioLevelTxMean[50];
    quint8 audioLevelTxPeak[50];

    modeInfo parseMode(quint8 mode, quint8 filter, uchar receiver=0,uchar vfo=0);
    bool parseSpectrum(scopeData& d, uchar receiver);
    funcType getCommand(funcs func, QByteArray& payload, int value=INT_MIN, uchar receiver=0);

    QByteArray getLANAddr();
    QByteArray getUSBAddr();
    QByteArray getACCAddr(quint8 ab);
    void sendDataOut();
    void prepDataAndSend(QByteArray data);
    void debugMe();

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

    bool haveRigCaps=false;
    quint8 model = 0; // Was model_kind but that makes no sense when users can create their own rigs!
    quint8 spectSeqMax;
    quint16 spectAmpMax;
    quint16 spectLenMax;
    spectrumMode_t oldScopeMode;

    bool usingNativeLAN; // indicates using OEM LAN connection (705,7610,9700,7850)
    bool lookingForRig;
    bool foundRig;

    bool warnedAboutFA=false;
    double frequencyMhz;
    quint8 civAddr;
    quint8 incomingCIVAddr; // place to store the incoming CIV.
    bool pttAllowed;

    scopeData mainScopeData;
    scopeData subScopeData;

    QString rigSerialPort;
    quint32 rigBaudRate;

    QByteArray lastCommandToRig;

    QString ip;
    int cport;
    int sport;
    int aport;
    QString username;
    QString password;

    QString serialPortError;
    quint8 localVolume=0;

    QHash<quint8,rigInfo> rigList;

    quint64 pow10[12] = {
        1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000, 10000000000, 100000000000
    };

#ifdef DEBUG_PARSE
    quint64 averageParseTime=0;
    int numParseSamples = 0;
    int lowParse=9999;
    int highParse=0;
    QTime lastParseReport = QTime::currentTime();
#endif
};

#endif // ICOMCOMMANDER_H
