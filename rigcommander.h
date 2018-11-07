#ifndef RIGCOMMANDER_H
#define RIGCOMMANDER_H

#include <QObject>

#include "commhandler.h"

// This file figures out what to send to the comm and also
// parses returns into useful things.

class rigCommander : public QObject
{
    Q_OBJECT

public:
    rigCommander();
    ~rigCommander();

public slots:
    void process();

    void enableSpectOutput();
    void disableSpectOutput();
    void enableSpectrumDisplay();
    void disableSpectrumDisplay();
    void setSpectrumBounds();
    void setSpectrumCenteredMode(bool centerEnable); // centered or band-wise
    void setScopeSpan(char span);
    void setScopeEdge(char edge);
    void setFrequency(double freq);
    void setMode(char mode);
    void getFrequency();
    void getMode();
    void getPTT();
    void setPTT(bool pttOn);
    void setDataMode(bool dataOn);
    void getDataMode();
    void setCIVAddr(unsigned char civAddr);
    void handleNewData(const QByteArray &data);
    void getDebug();

signals:
    void haveSpectrumData(QByteArray spectrum, double startFreq, double endFreq); // pass along data to UI
    void haveFrequency(double frequencyMhz);
    void haveMode(QString mode);
    void haveDataMode(bool dataModeEnabled);
    void haveSpectrumBounds();
    void dataForComm(const QByteArray &outData);
    void getMoreDebug();
    void finished();
    void havePTTStatus(bool pttOn);


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
    void parseDetailedRegisters1A05();
    void parseRegisters1A();
    void parseRegisters1C();
    void parsePTT();
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


    double frequencyMhz;
    unsigned char civAddr; // 0x94 is default = 148decimal
    bool pttAllowed;



};

#endif // RIGCOMMANDER_H
