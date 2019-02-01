#ifndef RIGCOMMANDER_H
#define RIGCOMMANDER_H

#include <QObject>

#include "commhandler.h"
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
    rigCommander(unsigned char rigCivAddr, QString rigSerialPort);
    ~rigCommander();

public slots:
    void process();

    void enableSpectOutput();
    void disableSpectOutput();
    void enableSpectrumDisplay();
    void disableSpectrumDisplay();
    void setSpectrumBounds();
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
    void setCIVAddr(unsigned char civAddr);
    void handleNewData(const QByteArray &data);
    void sayFrequency();
    void sayMode();
    void sayAll();
    void getDebug();

signals:
    void haveSpectrumData(QByteArray spectrum, double startFreq, double endFreq); // pass along data to UI
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
    commHandler * comm;
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

    model_kind model;

    double frequencyMhz;
    unsigned char civAddr; // 0x94 is default = 148decimal
    //const unsigned char compCivAddr = 0xE1; // 0xE1 is new default, 0xE0 was before.
    bool pttAllowed;



};

#endif // RIGCOMMANDER_H
