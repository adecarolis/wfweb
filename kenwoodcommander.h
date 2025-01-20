#ifndef KENWOODCOMMANDER_H
#define KENWOODCOMMANDER_H

#include "rigcommander.h"
#include "rtpaudio.h"
#include <QSerialPort>

#define audioLevelBufferSize (4)

// This file figures out what to send to the comm and also
// parses returns into useful things.

#define NUMTOASCII  48

class kenwoodCommander : public rigCommander
{
    Q_OBJECT

public:
    explicit kenwoodCommander(rigCommander* parent=nullptr);
    explicit kenwoodCommander(quint8 guid[GUIDLEN], rigCommander* parent = nullptr);
    ~kenwoodCommander();

public slots:
    void process() override;
    void commSetup(rigTypedef rigList, quint8 rigCivAddr, QString rigSerialPort, quint32 rigBaudRate, QString vsp, quint16 tcp, quint8 wf) override;
    void commSetup(rigTypedef rigList, quint8 rigCivAddr, udpPreferences prefs, audioSetup rxSetup, audioSetup txSetup, QString vsp, quint16 tcp) override;

    void lanConnected();
    void lanDisconnected();

    void closeComm() override;
    void setPTTType(pttType_t) override;

    // Power:
    void powerOn() override;
    void powerOff() override;


    // Rig ID and CIV:
    void setRigID(quint8 rigID) override;
    void setCIVAddr(quint8 civAddr) override;

    // UDP:
    void handleNewData(const QByteArray& data) override;
    void receiveBaudRate(quint32 baudrate) override;

    // Housekeeping:
    void receiveCommand(funcs func, QVariant value, uchar receiver) override;
    void setAfGain(quint8 level) override;

    // Serial:
    void serialPortError(QSerialPort::SerialPortError err);
    void receiveDataFromRig();

    void parseData(QByteArray dataInput);

    void getTxLevels(quint16 amplitudePeak, quint16 amplitudeRMS ,quint16 latency, quint16 current, bool under, bool over);
    void getRxLevels(quint16 amplitudePeak, quint16 amplitudeRMS,quint16 latency,quint16 current, bool under, bool over);


signals:
    // All signals are defined in rigcommander.h as they should be common for all rig types.
    void initRtpAudio();
    void haveSetVolume(quint8 level);
    void haveNetworkError(errorType);
    void haveChangeLatency(quint16 value);
    void haveNetworkStatus(networkStatus);
    void haveNetworkAudioLevels(networkAudioLevels);

private:
    void commonSetup();
    void determineRigCaps();
    funcType getCommand(funcs func, QByteArray &payload, int value, uchar receiver=0);
    bool parseMemory(QByteArray data, QVector<memParserFormat>* memParser, memoryType* mem);

    mutable QMutex serialMutex;
    QIODevice *port=Q_NULLPTR;
    bool portConnected=false;
    bool isTransmitting = false;
    QByteArray lastSentCommand;

    pttyHandler* ptty = Q_NULLPTR;
    tcpServer* tcp = Q_NULLPTR;
    rtpAudio* rtp = Q_NULLPTR;
    QThread* rtpThread = Q_NULLPTR;

    QHash<quint8,rigInfo> rigList;
    quint8 rigCivAddr;
    QString vsp;
    quint16 tcpPort;
    quint8 wf;

    QString rigSerialPort;
    quint32 rigBaudRate;

    udpPreferences prefs;
    audioSetup rxSetup;
    audioSetup txSetup;
    scopeData currentScope;
    bool loginRequired = false;
    bool audioStarted = false;

    bool network = false;

    QByteArray partial;

    connectionType_t connType = connectionUSB;

    bool aiModeEnabled=false;
    ushort scopeSplit=0;

    networkStatus status;

    quint8 audioLevelsTxPeak[audioLevelBufferSize];
    quint8 audioLevelsRxPeak[audioLevelBufferSize];

    quint8 audioLevelsTxRMS[audioLevelBufferSize];
    quint8 audioLevelsRxRMS[audioLevelBufferSize];

    quint8 audioLevelsTxPosition = 0;
    quint8 audioLevelsRxPosition = 0;

    quint8 findMean(quint8 *d);
    quint8 findMax(quint8 *d);
};

#endif // KENWOODCOMMANDER_H
