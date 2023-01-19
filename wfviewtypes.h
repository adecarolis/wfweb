#ifndef WFVIEWTYPES_H
#define WFVIEWTYPES_H

#include <QString>
#include <QtGlobal>
#include <stdint.h>

enum underlay_t { underlayNone, underlayPeakHold, underlayPeakBuffer, underlayAverageBuffer };

enum meterKind {
    meterNone=0,
    meterS,
    meterCenter,
    meterSWR,
    meterPower,
    meterALC,
    meterComp,
    meterVoltage,
    meterCurrent,
    meterRxdB,
    meterTxMod,
    meterRxAudio,
    meterAudio,
    meterLatency
};

enum spectrumMode {
    spectModeCenter=0x00,
    spectModeFixed=0x01,
    spectModeScrollC=0x02,
    spectModeScrollF=0x03,
    spectModeUnknown=0xff
};

enum mode_kind {
    modeLSB=0x00,
    modeUSB=0x01,
    modeAM=0x02,
    modeCW=0x03,
    modeRTTY=0x04,
    modeFM=0x05,
    modeCW_R=0x07,
    modeRTTY_R=0x08,
    modePSK = 0x12,
    modePSK_R = 0x13,
    modeLSB_D=0x80,
    modeUSB_D=0x81,
    modeDV=0x17,
    modeDD=0x27,
    modeWFM,
    modeS_AMD,
    modeS_AML,
    modeS_AMU,
    modeP25,
    modedPMR,
    modeNXDN_VN,
    modeNXDN_N,
    modeDCR
};

struct freqt {
    quint64 Hz;
    double MHzDouble;
};

struct datekind {
    uint16_t year;
    unsigned char month;
    unsigned char day;
};

struct timekind {
    unsigned char hours;
    unsigned char minutes;
    bool isMinus;
};

struct errorType {
    errorType() : alert(false) {};
    errorType(bool alert, QString message) : alert(alert), message(message) {};
    errorType(bool alert, QString device, QString message) : alert(alert), device(device), message(message) {};
    errorType(QString device, QString message) : alert(false), device(device), message(message) {};
    errorType(QString message) : alert(false), message(message) {};

    bool alert;
    QString device;
    QString message;
};

enum audioType {qtAudio,portAudio,rtAudio};
enum codecType { LPCM, PCMU, OPUS };


#endif // WFVIEWTYPES_H
