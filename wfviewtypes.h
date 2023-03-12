
#ifndef WFVIEWTYPES_H
#define WFVIEWTYPES_H

#include <QString>
#include <QtGlobal>
#include <stdint.h>
#include <memory>

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

enum selVFO_t {
    activeVFO = 0,
    inactiveVFO = 1
};

enum vfo_t {
    vfoA=0,
    vfoB=1,
    vfoMain = 0xD0,
    vfoSub = 0xD1
};

struct rptrTone_t {
    quint16 tone = 0;
    bool useSecondaryVFO = false;
};

enum duplexMode {
    dmSplitOff=0x00,
    dmSplitOn=0x01,
    dmSimplex=0x10,
    dmDupMinus=0x11,
    dmDupPlus=0x12,
    dmDupRPS=0x13,
    dmDupAutoOn=0x26,
    dmDupAutoOff=0x36
};

// Here, T=tone, D=DCS, N=none
// And the naming convention order is Transmit Receive
enum rptAccessTxRx {
    ratrNN=0x00,
    ratrTN=0x01, // "TONE" (T only)
    ratrNT=0x02, // "TSQL" (R only)
    ratrDD=0x03, // "DTCS" (TR)
    ratrDN=0x06, // "DTCS(T)"
    ratrTD=0x07, // "TONE(T) / TSQL(R)"
    ratrDT=0x08, // "DTCS(T) / TSQL(R)"
    ratrTT=0x09,  // "TONE(T) / TSQL(R)"
    ratrTONEoff,
    ratrTONEon,
    ratrTSQLoff,
    ratrTSQLon
};

struct rptrAccessData_t {
    rptAccessTxRx accessMode = ratrNN;
    bool useSecondaryVFO = false;
    bool turnOffTone = false;
    bool turnOffTSQL = false;
    bool usingSequence = false;
    int sequence = 0;
};

struct mode_info {
    mode_kind mk;
    unsigned char reg;
    unsigned char filter;
    selVFO_t VFO = activeVFO;
    QString name;
};

enum breakIn_t {
    brkinOff  = 0x00,
    brkinSemi = 0x01,
    brkinFull = 0x02
};

struct freqt {
    quint64 Hz;
    double MHzDouble;
    selVFO_t VFO = activeVFO;
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

enum cmds {
    cmdNone, cmdGetRigID, cmdGetRigCIV, cmdGetFreq, cmdSetFreq, cmdGetMode, cmdSetMode,
    cmdGetDataMode, cmdSetModeFilter, cmdSetDataModeOn, cmdSetDataModeOff, cmdGetRitEnabled, cmdGetRitValue,
    cmdSpecOn, cmdSpecOff, cmdDispEnable, cmdDispDisable, cmdGetRxGain, cmdSetRxRfGain, cmdGetAfGain, cmdSetAfGain,
    cmdGetSql, cmdSetSql, cmdGetIFShift, cmdSetIFShift, cmdGetTPBFInner, cmdSetTPBFInner,
    cmdGetTPBFOuter, cmdSetTPBFOuter, cmdGetPassband, cmdSetPassband,
    cmdGetCwPitch, cmdGetPskTone, cmdGetRttyMark, cmdSetCwPitch, cmdSetPskTone, cmdSetRttyMark,
    cmdSetATU, cmdStartATU, cmdGetATUStatus,
    cmdGetSpectrumMode, cmdGetSpectrumSpan, cmdScopeCenterMode, cmdScopeFixedMode,
    cmdGetPTT, cmdSetPTT,cmdPTTToggle,
    cmdGetTxPower, cmdSetTxPower, cmdGetMicGain, cmdSetMicGain, cmdSetModLevel,
    cmdGetSpectrumRefLevel, cmdGetDuplexMode, cmdGetModInput, cmdGetModDataInput,
    cmdGetCurrentModLevel, cmdStartRegularPolling, cmdStopRegularPolling, cmdQueNormalSpeed,
    cmdGetVdMeter, cmdGetIdMeter, cmdGetSMeter, cmdGetCenterMeter, cmdGetPowerMeter,
    cmdGetSWRMeter, cmdGetALCMeter, cmdGetCompMeter, cmdGetTxRxMeter,
    cmdGetTone, cmdGetTSQL, cmdGetToneEnabled, cmdGetTSQLEnabled, cmdGetDTCS,
    cmdSetToneEnabled, cmdSetTSQLEnabled, cmdGetRptAccessMode, cmdSetTone, cmdSetTSQL,
    cmdSetRptAccessMode, cmdSetRptDuplexOffset, cmdGetRptDuplexOffset,
    cmdSelVFO, cmdVFOSwap, cmdVFOEqualAB, cmdVFOEqualMS, cmdSetQuickSplit,
    cmdGetPreamp, cmdGetAttenuator, cmdGetAntenna,
    cmdGetBandStackReg, cmdGetKeySpeed, cmdSetKeySpeed, cmdGetBreakMode, cmdSetBreakMode, cmdSendCW, cmdStopCW, cmdGetDashRatio, cmdSetDashRatio,
    cmdSetTime, cmdSetDate, cmdSetUTCOffset,
    // Below Only used for USB Controller at the moment.
    cmdSetBandUp, cmdSetBandDown, cmdSetModeUp, cmdSetModeDown, cmdSetStepUp, cmdSetStepDown, cmdSetSpanUp, cmdSetSpanDown, cmdIFFilterUp, cmdIFFilterDown
};

struct commandtype {
    cmds cmd;
    std::shared_ptr<void> data;
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

enum passbandActions {passbandStatic, pbtInnerMove, pbtOuterMove, pbtMoving, passbandResizing};

enum usbDeviceType { usbNone = 0, shuttleXpress, shuttlePro2, RC28, xBoxGamepad, unknownGamepad, eCoderPlus, QuickKeys};
enum usbCommandType{ commandButton, commandKnob };

#endif // WFVIEWTYPES_H
