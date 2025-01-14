#ifndef KENWOODCOMMANDER_H
#define KENWOODCOMMANDER_H

#include "rigcommander.h"
#include <QSerialPort>

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
    void receiveSerialData();

    void parseData(QByteArray dataInput);

signals:
    // All signals are defined in rigcommander.h as they should be common for all rig types.

private:
    void commonSetup();
    void determineRigCaps();
    funcType getCommand(funcs func, QByteArray &payload, int value, uchar receiver=0);
    bool parseMemory(QByteArray data, QVector<memParserFormat>* memParser, memoryType* mem);

    // Serial port
    mutable QMutex serialMutex;
    QSerialPort *serialPort=Q_NULLPTR;
    bool portConnected=false;
    bool isTransmitting = false;
    QByteArray lastSentCommand;

    pttyHandler* ptty = Q_NULLPTR;
    tcpServer* tcp = Q_NULLPTR;

    const ushort ctcssTones[42]{670,693,719,744,770,797,825,854,885,915,958,974,1000,1035,1072,1109,1148,1188,1230,1273,1318,1365,1413,1462,
                          1514,1567,1622,1679,1738,1799,1862,1928,2035,2065,2107,2181,2267,2291,2336,2418,2503,2541};
};

#endif // KENWOODCOMMANDER_H
