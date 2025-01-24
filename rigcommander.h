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
// note: using a define because switch case doesn't even work with const quint8. Surprised me.
#define compCivAddr 0xE1

//#define DEBUG_PARSE // Enable to output Info messages every 10s with command parse timing.

typedef QHash<quint8, rigInfo> rigTypedef;

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
    // Most slots are virtual, but some simply pass data along so are common.

    // These slots are handled by the parent class:
    void receiveAudioData(const audioPacket& data);
    void handlePortError(errorType err);
    void handleStatusUpdate(const networkStatus status);
    void handleNetworkAudioLevels(networkAudioLevels);
    void changeLatency(const quint16 value);
    void radioSelection(QList<radio_cap_packet> radios);
    void radioUsage(quint8 radio, bool admin, quint8 busy, QString name, QString ip);
    void setCurrentRadio(quint8 radio);
    void getDebug();

    void dataFromServer(QByteArray data);
    virtual void process();
    virtual void commSetup(rigTypedef rigList, quint8 rigCivAddr, QString rigSerialPort, quint32 rigBaudRate, QString vsp, quint16 tcp, quint8 wf);
    virtual void commSetup(rigTypedef rigList, quint8 rigCivAddr, udpPreferences prefs, audioSetup rxSetup, audioSetup txSetup, QString vsp, quint16 tcp);
    virtual void closeComm();

    virtual void setPTTType(pttType_t ptt);

    // Power:
    virtual void powerOn();
    virtual void powerOff();


    // Rig ID and CIV:
    virtual void setRigID(quint8 rigID);
    virtual void setCIVAddr(quint8 civAddr);

    // UDP:
    virtual void handleNewData(const QByteArray& data);
    virtual void receiveBaudRate(quint32 baudrate);

    // Housekeeping:
    virtual void receiveCommand(funcs func, QVariant value, uchar receiver);
    virtual void setAfGain(quint8 level);

signals:
    // Right now, all signals are defined here as they should be rig agnostic.
    // Communication:
    void commReady();

    void havePortError(errorType err);
    void haveStatusUpdate(const networkStatus status);

    void haveNetworkAudioLevels(const networkAudioLevels l);
    void dataForComm(const QByteArray &outData);
    void haveDataFromRig(const QByteArray &outData);

    void setHalfDuplex(bool en);

    // UDP:
    void haveChangeLatency(quint16 value);
    void haveDataForServer(QByteArray outData);
    void haveAudioData(audioPacket data);
    void initUdpHandler();
    void haveSetVolume(quint8 level);
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
    void haveTone(quint16 tone);
    void haveTSQL(quint16 tsql);
    void haveDTCS(quint16 dcscode, bool tinv, bool rinv);
    void haveRptOffsetFrequency(freqt f);
    void haveMemory(memoryType mem);

    // Levels:
    void haveAfGain(quint8 level);
    // Housekeeping:
    void requestRadioSelection(QList<radio_cap_packet> radios);
    void setRadioUsage(quint8 radio, bool admin, quint8 busy, QString user, QString ip);
    void selectedRadio(quint8 radio);
    void getMoreDebug();
    void finished();
    void haveReceivedValue(funcs func, QVariant value);

protected:
    // These are common between all sub-classes, so define here
    cachingQueue* queue;
    void printHex(const QByteArray &pdata);
    void printHex(const QByteArray &pdata, bool printVert, bool printHoriz);

    quint8 guid[GUIDLEN] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
    bool usingNativeLAN;
    bool rigPoweredOn = false; // Assume the radio is not powered-on until we know otherwise.

    struct rigCapabilities rigCaps;
    bool haveRigCaps=false;
    QHash<quint8,rigInfo> rigList;
    bool isRadioAdmin = true;

private:
    // rigCommander should have no private vars as it is only ever subclassed.
};

#endif // RIGCOMMANDER_H
