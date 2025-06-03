#ifndef YAESUCOMMANDER_H
#define YAESUCOMMANDER_H
#include "rigcommander.h"
#include "rtpaudio.h"
#include <QSerialPort>
#include <ft4222handler.h>

#include "yaesuudpcontrol.h"
#include "yaesuudpcat.h"
#include "yaesuudpaudio.h"
#include "yaesuudpscope.h"

#define audioLevelBufferSize (4)

// This file figures out what to send to the comm and also
// parses returns into useful things.

#define NUMTOASCII  48

class yaesuCommander : public rigCommander
{
    Q_OBJECT

public:
    explicit yaesuCommander(rigCommander* parent=nullptr);
    explicit yaesuCommander(quint8 guid[GUIDLEN], rigCommander* parent = nullptr);
    ~yaesuCommander();

public slots:
    void process() override;
    void commSetup(rigTypedef rigList, quint16 rigCivAddr, QString rigSerialPort, quint32 rigBaudRate, QString vsp, quint16 tcp, quint8 wf) override;
    void commSetup(rigTypedef rigList, quint16 rigCivAddr, udpPreferences prefs, audioSetup rxSetup, audioSetup txSetup, QString vsp, quint16 tcp) override;

    void lanConnected();
    void lanDisconnected();

    void closeComm() override;
    void setPTTType(pttType_t) override;

    // Power:
    void powerOn() override;
    void powerOff() override;


    // Rig ID and CIV:
    void setRigID(quint16 rigID) override;
    void setCIVAddr(quint16 civAddr) override;

    // UDP:
    void handleNewData(const QByteArray& data) override;
    void receiveBaudRate(quint32 baudrate) override;

    void receiveCatDataFromRig(QByteArray in);
    void receiveScopeDataFromRig(QByteArray in);
    void receiveAudioDataFromRig(QByteArray in);


    // Housekeeping:
    void receiveCommand(funcs func, QVariant value, uchar receiver) override;

    // Serial:
    void serialPortError(QSerialPort::SerialPortError err);
    void receiveDataFromRig();


    void parseData(QByteArray dataInput);

    void getTxLevels(quint16 amplitudePeak, quint16 amplitudeRMS ,quint16 latency, quint16 current, bool under, bool over);
    void getRxLevels(quint16 amplitudePeak, quint16 amplitudeRMS,quint16 latency,quint16 current, bool under, bool over);

    void haveScopeData(QByteArray d);
    void dataForRig(QByteArray data);

signals:
    // All signals are defined in rigcommander.h as they should be common for all rig types.
    void initUdpControl();
    void haveSetVolume(quint8 level);
    void haveNetworkError(errorType);
    void haveChangeLatency(quint16 value);
    void haveNetworkStatus(networkStatus);
    void haveNetworkAudioLevels(networkAudioLevels);
    void sendDataToRig(QByteArray data);
    void initScope();

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

    yaesuUdpControl* control = Q_NULLPTR;
    QThread* controlThread = Q_NULLPTR;
    yaesuUdpCat* cat = Q_NULLPTR;
    QThread* catThread = Q_NULLPTR;
    yaesuUdpAudio* audio = Q_NULLPTR;
    QThread* audioThread = Q_NULLPTR;
    yaesuUdpScope* scope = Q_NULLPTR;
    QThread* scopeThread = Q_NULLPTR;

    QHash<quint16,rigInfo> rigList;
    quint16 rigCivAddr;
    QString vsp;
    quint16 tcpPort;
    quint8 wf;

    QString rigSerialPort;
    quint32 rigBaudRate;

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

    genericType scopeMode;
    centerSpanData scopeSpan;
    ft4222Handler* ft4222 = Q_NULLPTR;
};




#endif // YAESUCOMMANDER_H
