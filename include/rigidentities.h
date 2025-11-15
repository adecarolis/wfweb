#ifndef RIGIDENTITIES_H
#define RIGIDENTITIES_H

#include <QtNumeric>
#include <QString>
#include <QList>
#include <vector>
#include <QHash>
#include <QColor>

#include "freqmemory.h"
#include "packettypes.h"
#include "icomudpbase.h"

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
                  inputAV=11,
                  inputMICAV=12,
                  inputACCUSB=13,
                  inputLINE=14,
                  inputMICUSBACC=15,
                  inputMICLINEACC=16,
                  inputMICLINE=17,
                  inputNone,
                  inputUnknown=0xff
};

struct rigInput {
    rigInput() : type(inputUnknown),reg(0), name(""), level(0) {}
    //rigInput(rigInput const &r): type(r.type), reg(r.reg), name(r.name), level(r.level) {};
    rigInput(inputTypes type) : type(type),reg(0) ,name(""),level(0) {}
    rigInput(inputTypes type, qint8 reg, QString name) : type(type), reg(reg), name(name), level(0){}
    inputTypes type;
    qint8 reg;
    QString name;
    uchar level;
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
                bandGen,     //23
                bandUnknown // 24
};

/*
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
*/

struct centerSpanData {
    centerSpanData() {}
    centerSpanData(centerSpanData const &c): reg(c.reg), name(c.name), freq(c.freq)  {}
    centerSpanData(uchar reg, QString name, unsigned int freq) :
        reg(reg), name(name), freq(freq){}
    uchar reg;
    QString name;
    unsigned int freq;

    centerSpanData &operator=(const centerSpanData &i) {
        this->reg=i.reg;
        this->name=i.name;
        this->freq=i.freq;
        return *this;
    }
};

struct bandType {
    bandType() {band=bandUnknown;}
    bandType(bandType const &b): region(b.region), band(b.band), bsr(b.bsr), lowFreq(b.lowFreq), highFreq(b.highFreq), range(b.range), memGroup(b.memGroup), bytes(b.bytes), ants(b.ants), power(b.power), color(b.color), name(b.name), offset(b.offset){};
    bandType(QString region, availableBands band, uchar bsr, quint64 lowFreq, quint64 highFreq, double range, int memGroup, qint8 bytes, bool ants, float power, QColor color, QString name, int offset) :
        region(region), band(band), bsr(bsr), lowFreq(lowFreq), highFreq(highFreq), range(range), memGroup(memGroup), bytes(bytes), ants(ants), power(power), color(color), name(name), offset(offset) {}

    QString region;
    availableBands band;
    uchar bsr;
    quint64 lowFreq;
    quint64 highFreq;
    rigMode_t defaultMode;
    double range;
    int memGroup;
    qint8 bytes;
    bool ants;
    float power;
    QColor color;
    QString name;
    qint64 newFreq=0;
    qint64 offset;
    bandType &operator=(const bandType &i) {
        this->region=i.region;
        this->band=i.band;
        this->bsr=i.bsr;
        this->lowFreq=i.highFreq;
        this->defaultMode=i.defaultMode;
        this->range=i.range;
        this->memGroup=i.memGroup;
        this->bytes=i.bytes;
        this->ants=i.ants;
        this->power=i.power;
        this->color=i.color;
        this->name=i.name;
        this->newFreq=i.newFreq;
        this->offset=i.offset;
        return *this;
    }
};

// Used for setting/retrieving BSR information
struct bandStackType {
    bandStackType(): band(0),reg(0),freq(freqt()),data(0),mode(0),filter(0) {}
    bandStackType(bandStackType const &b): band(b.band),reg(b.reg),freq(b.freq),data(b.data),
        mode(b.mode),filter(b.filter), sql(b.sql), tone(b.tone), tsql(b.tsql) {}
    bandStackType(uchar band, uchar reg): band(band),reg(reg), freq(), data(0), mode(0), filter(0) {}
    bandStackType(uchar band, uchar reg, freqt freq, uchar data, uchar mode, uchar filter):
        band(band), reg(reg), freq(freq), data(data), mode(mode), filter(filter) {};
    uchar band;
    uchar reg;
    freqt freq;
    uchar data;
    uchar mode;
    uchar filter;
    uchar sql;
    toneInfo tone;
    toneInfo tsql;
    bandStackType &operator=(const bandStackType &i) {
        this->band=i.band;
        this->reg=i.reg;
        this->freq=i.freq;
        this->data=i.data;
        this->mode=i.mode;
        this->filter=i.filter;
        this->sql=i.sql;
        this->tone=i.tone;
        this->tsql=i.tsql;
        return *this;
    }

};


struct filterType {
    filterType():num(0),name(""),modes(0) {}
    filterType(filterType const &f): num(f.num),name(f.name),modes(f.modes) {}
    filterType(quint8 num, QString name, unsigned int modes) :
        num(num), name(name), modes(modes) {}

    quint8 num;
    QString name;
    unsigned int modes;
};

struct genericType {
    genericType():num(0),name("") {}
    genericType(genericType const &g):num(g.num),name(g.name) {}
    genericType(quint8 num, QString name) :
        num(num), name(name) {}
    quint8 num;
    QString name;
};


struct widthsType {
    widthsType():bands(0),num(0),hz(0) {}
    widthsType(widthsType const &w):bands(w.bands),num(w.num),hz(w.hz) {}
    widthsType(ushort bands, uchar num, ushort hz) : bands(bands),num(num),hz(hz) {}
    ushort bands;
    uchar num;
    ushort hz;
};

//model_kind determineRadioModel(quint8 rigID);

struct bsrRequest {
    availableBands band;
    int bsrPosition=1;
};

struct rigCapabilities {
    quint16 model;
    quint16 modelID = 0; //CIV address
    manufacturersType_t manufacturer=manufIcom;
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
    quint8 numReceiver;
    quint8 numVFO;

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
    bool hasAttenuator;
    bool hasPreamp;
    bool hasAntennaSel;
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

    std::vector <genericType> attenuators;
    std::vector <genericType> preamps;
    std::vector <genericType> antennas;
    std::vector <filterType> filters;
    std::vector <centerSpanData> scopeCenterSpans;
    std::vector <bandType> bands;
    std::vector <toneInfo> ctcss;
    std::vector <toneInfo> dtcs;
    std::vector <genericType> roofing;
    std::vector <genericType> scopeModes;
    std::vector <stepType> steps;
    std::vector <widthsType> widths;
    quint8 bsr[24] = {0};

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
    QVector<periodicType> periodic;
    QMap<int,double> meters[meterUnknown];
    double meterLines[meterUnknown];
};

Q_DECLARE_METATYPE(manufacturersType_t)
Q_DECLARE_METATYPE(connectionType_t)
Q_DECLARE_METATYPE(udpPreferences)
Q_DECLARE_METATYPE(rigCapabilities)
Q_DECLARE_METATYPE(modeInfo)
Q_DECLARE_METATYPE(rigInput)
Q_DECLARE_METATYPE(filterType)
Q_DECLARE_METATYPE(inputTypes)
Q_DECLARE_METATYPE(genericType)
Q_DECLARE_METATYPE(bandType)
Q_DECLARE_METATYPE(bandStackType)
Q_DECLARE_METATYPE(widthsType)
Q_DECLARE_METATYPE(centerSpanData)
Q_DECLARE_METATYPE(yaesu_scope_data)



#endif // RIGIDENTITIES_H
