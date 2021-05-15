#ifndef RIGIDENTITIES_H
#define RIGIDENTITIES_H

#include <QtNumeric>
#include <QString>
#include <QList>
#include <vector>

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
    model7850 = 0x8E,
    model9700 = 0xA2,
    model705 = 0xA4,
    model706 = 0x58,
    model910h = 0x60,
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

enum bandType { band23cm=0,
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

model_kind determineRadioModel(unsigned char rigID);

struct rigCapabilities {
    model_kind model;

    quint8 civ;
    quint8 modelID;

    QString modelName;

    bool hasLan; // OEM ethernet or wifi connection
    bool hasEthernet;
    bool hasWiFi;

    QVector<rigInput> inputs;

    bool hasSpectrum;
    quint8 spectSeqMax;
    quint16 spectAmpMax;
    quint16 spectLenMax;

    bool hasDD;
    bool hasDV;
    bool hasATU;

    bool hasCTCSS;
    bool hasDTCS;

    bool hasTransmit;
    bool hasAttenuator;
    bool hasPreamp;
    bool hasAntennaSel;

    std::vector <unsigned char> attenuators;
    std::vector <unsigned char> preamps;
    std::vector <unsigned char> antennas;
    std::vector <bandType> bands;
    unsigned char bsr[20] = {0};
};


#endif // RIGIDENTITIES_H
