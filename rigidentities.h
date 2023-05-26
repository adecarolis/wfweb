#ifndef RIGIDENTITIES_H
#define RIGIDENTITIES_H

#include <QtNumeric>
#include <QString>
#include <QList>
#include <vector>
#include <QHash>

#include "freqmemory.h"
#include "packettypes.h"

// Credit for parts of CIV list:
// http://www.docksideradio.com/Icom%20Radio%20Hex%20Addresses.htm

// 7850 and 7851 have the same commands and are essentially identical


enum model_kind {
    model7100 = 0x88,
    model7200 = 0x76,
    model7300 = 0x94,
    modelR8600 = 0x96,
    model7600 = 0x7A,
    model7610 = 0x98,
    model7700 = 0x74,
    model7800 = 0x6A,
    model7000 = 0x70,
    model7410 = 0x80,
    model7850 = 0x8E,
    model9700 = 0xA2,
    model703 = 0x68,
    model705 = 0xA4,
    model706 = 0x58,
    model718 = 0x5E,
    model736 = 0x40,
    model737 = 0x3C,
    model738 = 0x44,
    model746 = 0x56,
    model756 = 0x50,
    model756pro = 0x5C,
    model756proii = 0x64,
    model756proiii = 0x6E,
    model905 = 0xAC,
    model910h = 0x60,
    model9100 = 0x7C,
    modelUnknown = 0xFF
};



enum inputTypes{ inputMic=0,
                  inputACCA=1,
                  inputACCB=2,
                  inputUSB=3,
                  inputLAN=4,
                  inputMICACCA=5,
                  inputMICACCB=6,
                  inputACCAACCB=7,
                  inputMICACCAACCB=8,
                  inputSPDIF=9,
                  inputMICUSB=10,
                  inputNone,
                  inputUnknown=0xff
};

struct rigInput {
    rigInput() {}
    rigInput(inputTypes type) : type(type) {}
    rigInput(inputTypes type, uchar reg, QString name) : type(type), reg(reg), name(name) {}
    inputTypes type = inputUnknown;
    uchar reg = 0;
    QString name = "";
    uchar level = 0;
};



enum availableBands {
                band3cm = 0,
                band6cm,   //1
                band9cm,   //2
                band13cm,  //3
                band23cm,  //4
                band70cm,  //5
                band2m,    //6
                bandAir,    //7
                bandWFM,    //8
                band4m,     //9
                band6m,     //10
                band10m,    //11
                band12m,    //12
                band15m,    //13
                band17m,    //14
                band20m,    //15
                band30m,    //16
                band40m,    //17
                band60m,    //18
                band80m,    //19
                band160m,   //20
                band630m,   //21
                band2200m,  //22
                bandGen     //23
};

enum centerSpansType {
    cs2p5k = 0,
    cs5k = 1,
    cs10k = 2,
    cs25k = 3,
    cs50k = 4,
    cs100k = 5,
    cs250k = 6,
    cs500k = 7,
    cs1M = 8,
    cs2p5M = 9,
    cs5M = 10,
    cs10M = 11,
    cs25M = 12,
};

struct centerSpanData {
    centerSpanData() {}
    centerSpanData(centerSpansType cstype, QString name, unsigned int freq) :
        cstype(cstype), name(name), freq(freq){}
    centerSpansType cstype;
    QString name;
    unsigned int freq;
};

struct bandType {
    bandType() {}
    bandType(availableBands band, quint64 lowFreq, quint64 highFreq, double range, int memGroup) :
        band(band), lowFreq(lowFreq), highFreq(highFreq), range(range), memGroup(memGroup) {}

    availableBands band;
    quint64 lowFreq;
    quint64 highFreq;
    rigMode_t defaultMode;
    double range;
    int memGroup;
};

struct filterType {
    filterType() {}
    filterType(unsigned char num, QString name, unsigned int modes) :
        num(num), name(name), modes(modes) {}

    unsigned char num;
    QString name;
    unsigned int modes;
};

struct genericType {
    genericType() {}
    genericType(unsigned char num, QString name) :
        num(num), name(name) {}
    unsigned char num;
    QString name;
};


struct bandStackType {
    bandStackType() {}
    bandStackType(uchar band, uchar regCode): band(band),regCode(regCode), freq(), data(0), mode(0), filter(0) {}
    bandStackType(uchar band, uchar regCode, freqt freq, uchar data, uchar mode, uchar filter):
        band(band), regCode(regCode), freq(freq), data(data), mode(mode), filter(filter) {};
    uchar band;
    uchar regCode;
    freqt freq;
    uchar data;
    uchar mode;
    uchar filter;
};


//model_kind determineRadioModel(unsigned char rigID);

struct rigCapabilities {
    quint8 model;
    quint8 civ;
    quint8 modelID = 0;
    QString filename;
    int rigctlModel;
    QString modelName;

    bool hasLan; // OEM ethernet or wifi connection
    bool hasEthernet;
    bool hasWiFi;
    bool hasFDcomms;

    QVector<rigInput> inputs;

    bool hasSpectrum=true;
    quint8 spectSeqMax;
    quint16 spectAmpMax;
    quint16 spectLenMax;

    bool hasNB = false;
    QByteArray nbCommand;

    bool hasDD;
    bool hasDV;
    bool hasATU;

    bool hasCTCSS;
    bool hasDTCS;
    bool hasRepeaterModes = false;

    bool hasTransmit;
    bool hasPTTCommand;
    bool useRTSforPTT;
    bool hasAttenuator;
    bool hasPreamp;
    bool hasAntennaSel;
    bool hasDataModes;
    bool hasIFShift;
    bool hasTBPF;

    bool hasRXAntenna;

    bool hasSpecifyMainSubCmd = false; // 0x29
    bool hasVFOMS = false;
    bool hasVFOAB = true; // 0x07 [00||01]

    bool hasAdvancedRptrToneCmds = false;
    bool hasQuickSplitCommand = false;

    bool hasCommand29 = false;

    QByteArray quickSplitCommand;
    QHash<funcs,funcType> commands;
    QHash<QByteArray,funcs> commandsReverse;

    std::vector <unsigned char> attenuators;
    std::vector <genericType> preamps;
    std::vector <genericType> antennas;
    std::vector <filterType> filters;
    std::vector <centerSpanData> scopeCenterSpans;
    std::vector <bandType> bands;
    //std::vector <spanType> spans;
    std::vector <stepType> steps;
    unsigned char bsr[24] = {0};

    std::vector <modeInfo> modes;

    QByteArray transceiveCommand;
    quint8 guid[GUIDLEN] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
    quint32 baudRate;
    quint16 memGroups;
    quint16 memories;
    quint16 memStart;
    QString memFormat;
    QVector<memParserFormat> memParser;
    quint16 satMemories;
    QString satFormat;
    QVector<memParserFormat> satParser;
};

Q_DECLARE_METATYPE(rigCapabilities)
Q_DECLARE_METATYPE(modeInfo)
Q_DECLARE_METATYPE(rigInput)
Q_DECLARE_METATYPE(filterType)
Q_DECLARE_METATYPE(inputTypes)
Q_DECLARE_METATYPE(genericType)
Q_DECLARE_METATYPE(bandType)
Q_DECLARE_METATYPE(bandStackType)
Q_DECLARE_METATYPE(centerSpanData)

#endif // RIGIDENTITIES_H
