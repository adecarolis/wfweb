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
    void setRTSforPTT(bool enabled);

    // Power:
    void powerOn();
    void powerOff();


    // Rig ID and CIV:
    void getRigID();
    void findRigs();
    void setRigID(unsigned char rigID);
    void setCIVAddr(unsigned char civAddr);

    // UDP:
    void handleNewData(const QByteArray& data);
    void receiveAudioData(const audioPacket& data);
    void handlePortError(errorType err);
    void changeLatency(const quint16 value);
    void dataFromServer(QByteArray data);
    void receiveBaudRate(quint32 baudrate);

    // Housekeeping:
    void handleStatusUpdate(const networkStatus status);
    void handleNetworkAudioLevels(networkAudioLevels);
    void radioSelection(QList<radio_cap_packet> radios);
    void radioUsage(quint8 radio, bool admin, quint8 busy, QString name, QString ip);
    void setCurrentRadio(quint8 radio);
    void getDebug();
    void receiveCommand(funcs func, QVariant value, uchar vfo);
    void setAfGain(unsigned char level);

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

    // Rig ID:
    void haveRigID(rigCapabilities rigCaps);
    void discoveredRigID(rigCapabilities rigCaps);

    // Repeater:
    void haveDuplexMode(duplexMode_t);
    void haveRptAccessMode(rptAccessTxRx_t ratr);
    void haveTone(quint16 tone);
    void haveTSQL(quint16 tsql);
    void haveDTCS(quint16 dcscode, bool tinv, bool rinv);
    void haveRptOffsetFrequency(freqt f);
    void haveMemory(memoryType mem);

    // Levels:
    void haveAfGain(unsigned char level);
    // Housekeeping:
    void requestRadioSelection(QList<radio_cap_packet> radios);
    void setRadioUsage(quint8 radio, bool admin, quint8 busy, QString user, QString ip);
    void selectedRadio(quint8 radio);
    void getMoreDebug();
    void finished();
    void haveReceivedValue(funcs func, QVariant value);

private:
    void commonSetup();

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

    freqt parseFreqData(QByteArray data, bool sub);
    quint64 parseFreqDataToInt(QByteArray data);
    freqt parseFrequencyRptOffset(QByteArray data);
    bool parseMemory(QVector<memParserFormat>* memParser, memoryType* mem);
    QByteArray makeFreqPayloadRptOffset(freqt freq);
    QByteArray makeFreqPayload(double frequency);
    QByteArray makeFreqPayload(freqt freq);
    QByteArray encodeTone(quint16 tone, bool tinv, bool rinv);
    QByteArray encodeTone(quint16 tone);

    toneInfo decodeTone(QByteArray eTone);
    //quint16 decodeTone(QByteArray eTone, bool &tinv, bool &rinv);
    uchar makeFilterWidth(ushort width, bool sub);


    unsigned char audioLevelRxMean[50];
    unsigned char audioLevelRxPeak[50];
    unsigned char audioLevelTxMean[50];
    unsigned char audioLevelTxPeak[50];

    modeInfo parseMode(quint8 mode, quint8 filter, bool sub);
    bool parseSpectrum(scopeData& d, bool sub);
    bool getCommand(funcs func, QByteArray& payload, int value=INT_MIN, bool sub=false);

    QByteArray getLANAddr();
    QByteArray getUSBAddr();
    QByteArray getACCAddr(unsigned char ab);
    void sendDataOut();
    void prepDataAndSend(QByteArray data);
    void debugMe();
    void printHex(const QByteArray &pdata);
    void printHex(const QByteArray &pdata, bool printVert, bool printHoriz);

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

    double frequencyMhz;
    unsigned char civAddr;
    unsigned char incomingCIVAddr; // place to store the incoming CIV.
    bool pttAllowed;
    bool useRTSforPTT_isSet = false;
    bool useRTSforPTT_manual = false;

    scopeData mainScopeData;
    scopeData subScopeData;

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
