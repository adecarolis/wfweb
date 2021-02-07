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


class rigCommander : public QObject
{
    Q_OBJECT

public:
    rigCommander(unsigned char rigCivAddr, QString rigSerialPort, quint32 rigBaudRate);
    rigCommander(unsigned char rigCivAddr, QHostAddress ip, int cport, int sport, int aport, QString username, QString password);
    ~rigCommander();

public slots:
    void process();

    void enableSpectOutput();
    void disableSpectOutput();
    void enableSpectrumDisplay();
    void disableSpectrumDisplay();
    void setSpectrumBounds(double startFreq, double endFreq, unsigned char edgeNumber);
    void setSpectrumCenteredMode(bool centerEnable); // centered or band-wise
    void getSpectrumCenterMode();
    void setScopeSpan(char span);
    void getScopeSpan();
    void setScopeEdge(char edge);
    void getScopeEdge();
    void getScopeMode();
    void setFrequency(double freq);
    void setMode(char mode);
    void getFrequency();
    void getBandStackReg(char band, char regCode);
    void getMode();
    void getPTT();
    void setPTT(bool pttOn);
    void setDataMode(bool dataOn);
    void getDataMode();
    void getRfGain();
    void getAfGain();
    void getSql();
    void setRfGain(unsigned char level);
    void setAfGain(unsigned char level);
    void startATU();
    void setATU(bool enabled);
    void getATUStatus();
    void getRigID();
    void findRigs();
    void setCIVAddr(unsigned char civAddr);
    void handleNewData(const QByteArray &data);
    void handleSerialPortError(const QString port, const QString errorText);
    void handleStatusUpdate(const QString text);
    void sayFrequency();
    void sayMode();
    void sayAll();
    void getDebug();

signals:
    void commReady();
    void haveSpectrumData(QByteArray spectrum, double startFreq, double endFreq); // pass along data to UI
    void haveRigID(rigCapabilities rigCaps);
    void discoveredRigID(rigCapabilities rigCaps);
    void haveSerialPortError(const QString port, const QString errorText);
    void haveStatusUpdate(const QString text);
    void haveFrequency(double frequencyMhz);
    void haveMode(QString mode);
    void haveDataMode(bool dataModeEnabled);
    void haveBandStackReg(float freq, char mode, bool dataOn);
    void haveSpectrumBounds();
    void haveScopeSpan(char span);
    void haveSpectrumFixedMode(bool fixedEnabled);
    void haveScopeEdge(char edge);
    void haveRfGain(unsigned char level);
    void haveAfGain(unsigned char level);
    void haveSql(unsigned char level);
    void haveTxPower(unsigned char level);
    void dataForComm(const QByteArray &outData);
    void getMoreDebug();
    void finished();
    void havePTTStatus(bool pttOn);
    void haveATUStatus(unsigned char status);


private:
    void setup();
    QByteArray stripData(const QByteArray &data, unsigned char cutPosition);
    void parseData(QByteArray data); // new data come here
    void parseCommand();
    unsigned char bcdHexToDecimal(unsigned char in);
    void parseFrequency();
    float parseFrequency(QByteArray data, unsigned char lastPosition); // supply index where Mhz is found
    QByteArray makeFreqPayload(double frequency);
    void parseMode();
    void parseSpectrum();
    void parseWFData();
    void parseDetailedRegisters1A05();
    void parseRegisters1A();
    void parseBandStackReg();
    void parseRegisters1C();
    void parsePTT();
    void parseATU();
    void parseLevels(); // register 0x14
    void sendLevelCmd(unsigned char levAddr, unsigned char level);
    void sendDataOut();
    void prepDataAndSend(QByteArray data);
    void debugMe();
    void printHex(const QByteArray &pdata, bool printVert, bool printHoriz);
    commHandler * comm=nullptr;
    udpHandler* udp=nullptr;
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

    QHostAddress ip;
    int cport;
    int sport;
    int aport;
    QString username;
    QString password;

    QString serialPortError;


};

#endif // RIGCOMMANDER_H
