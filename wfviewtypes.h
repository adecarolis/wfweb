#ifndef WFVIEWTYPES_H
#define WFVIEWTYPES_H

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
