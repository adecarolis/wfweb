#ifndef RIGIDENTITIES_H
#define RIGIDENTITIES_H

#include <QtNumeric>
#include <QString>
#include <QList>
#include <vector>

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
    model910h = 0x60,
    model9100 = 0x7C,
    modelUnknown = 0xFF
};

enum rigInput{ inputMic=0,
               inputACC=1,
               inputUSB=3,
               inputLAN=5,
               inputACCA,
               inputACCB,
               inputNone,
               inputUnknown=0xff
};

enum availableBands { band23cm=0,
                band70cm,
                band2m,
                bandAir,
                bandWFM,
                band4m,
                band6m,
                band10m,
                band12m,
                band15m,
                band17m,
                band20m,
                band30m,
                band40m,
                band60m,
                band80m,
                band160m,
                band630m,
                band2200m,
                bandGen
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
    cs2p5M = 9
};

struct centerSpanData {
    centerSpansType cstype;
    QString name;
};

struct bandType {
    bandType(availableBands band, quint32 lowFreq, quint32 highFreq, mode_kind defaultMode) :
        band(band), lowFreq(lowFreq), highFreq(highFreq), defaultMode(defaultMode) {}

    bandType() {}

    availableBands band;
    quint32 lowFreq;
    quint32 highFreq;
    mode_kind defaultMode;
};

model_kind determineRadioModel(unsigned char rigID);

struct rigCapabilities {
    model_kind model;
    quint8 civ;
    quint8 modelID;
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

    std::vector <unsigned char> attenuators;
    std::vector <unsigned char> preamps;
    std::vector <unsigned char> antennas;
    std::vector <centerSpanData> scopeCenterSpans;
    std::vector <bandType> bands;
    unsigned char bsr[20] = {0};

    std::vector <mode_info> modes;

    QByteArray transceiveCommand;
    quint8 guid[GUIDLEN] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
    quint32 baudRate;
};


#endif // RIGIDENTITIES_H
