
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
    modeATV=0x23,
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
    bool data = false;
    QString name;
    bool bw; // Can the bandwidth of the current filter be changed?
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
    cmdNone, cmdGetRigID, cmdGetRigCIV, cmdGetFreq, cmdGetFreqB, cmdSetFreq, cmdGetMode, cmdSetMode,
    cmdGetDataMode, cmdSetModeFilter, cmdSetDataModeOn, cmdSetDataModeOff, cmdGetRitEnabled, cmdGetRitValue,
    cmdSpecOn, cmdSpecOff, cmdDispEnable, cmdDispDisable, cmdGetRxGain, cmdSetRxRfGain, cmdGetAfGain, cmdSetAfGain,
    cmdGetSql, cmdSetSql, cmdGetIFShift, cmdSetIFShift, cmdGetNRLevel, cmdSetNRLevel, cmdGetTPBFInner, cmdSetTPBFInner,
    cmdGetTPBFOuter, cmdSetTPBFOuter, cmdGetPassband, cmdSetPassband, cmdGetNBLevel, cmdSetNBLevel,
    cmdGetCompLevel, cmdSetCompLevel,
    cmdGetMonitorGain, cmdSetMonitorGain, cmdGetVoxGain, cmdSetVoxGain, cmdGetAntiVoxGain, cmdSetAntiVoxGain,
    cmdGetCwPitch, cmdGetPskTone, cmdGetRttyMark, cmdSetCwPitch, cmdSetPskTone, cmdSetRttyMark,
    cmdGetVox,cmdSetVox, cmdGetMonitor,cmdSetMonitor, cmdGetComp, cmdSetComp, cmdGetNB, cmdSetNB, cmdGetNR, cmdSetNR,
    cmdSetATU, cmdStartATU, cmdGetATUStatus,
    cmdGetSpectrumMode, cmdGetSpectrumSpan, cmdScopeCenterMode, cmdScopeFixedMode,
    cmdGetPTT, cmdSetPTT,cmdPTTToggle,
    cmdGetTxPower, cmdSetTxPower, cmdGetMicGain, cmdSetMicGain, cmdGetModLevel, cmdSetModLevel,
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
    cmdGetTransceive, cmdSetTransceive,cmdGetPower,cmdSetPower,
    // Below Only used for USB Controller at the moment.
    cmdSetBandUp, cmdSetBandDown, cmdSetModeUp, cmdSetModeDown, cmdSetStepUp, cmdSetStepDown,
    cmdSetSpanUp, cmdSetSpanDown, cmdIFFilterUp, cmdIFFilterDown, cmdPageDown, cmdPageUp,
    cmdLCDWaterfall, cmdLCDSpectrum, cmdLCDNothing, cmdSeparator
};


// funcs and funcString MUST match exactly (and NUMFUNCS must be updated)
#define NUMFUNCS 145

enum funcs { funcNone,
    funcPower,              funcScopeData,          funcScopeOnOff,     funcScopeWaveData,  funcScopeMainSub,   funcScopeSingleDual,
    funcScopeCenterFixed,   funcScopeSpan,          funcScopeEdgeFreq,  funcScopeHold,      funcScopeRef,       funcScopeSpeed,
    funcScopeDuringTX,      funcScopeCenterFreq,    funcScopeFixedEdge, funcScopeVBW,       funcScopeFixedFreq, funcScopeRBW,
    funcSplit,              funcRigID,              funcRigCIV,         funcFA,             funcFB,             funcNotch,
    funcfreqTR,             funcFreq,               funcFreqGet,        funcFreqSet,        funcModeTR,         funcMode,
    funcModeGet,            funcModeSet,            funcTuningStep,     funcDataMode,       funcModeFilter,     funcRit,
    funcRitValue,           funcRfGain,             funcRxRfGain,       funcAfGain,         funcNRLevel,        funcSql,
    funcIFShift,            funcPBTInner,           funcPBTOuter,       funcPassband,       funcCwPitch,        funcPskTone,
    funcRttyMark,           funcATUStatus,          funcTXStatus,       funcTxPower,        funcMicGain,        funcModLevel,
    funcSpeech,             funcDuplexMode,         funcModInput,       funcModData1Input,  funcModData2Input,  funcModData3Input,
    funcVdMeter,            funcIdMeter,            funcSMeter,         funcCenterMeter,    funcPowerMeter,     funcSWRMeter,
    funcALCMeter,           funcCompMeter,          funcTxRxMeter,      funcTone,           funcTSQL,           funcDTCS,
    funcRptAccessMode,      funcRptDuplexOffset,    funcVFOASelect,     funcVFOBSelect,     funcVFOSwap,        funcVFOEqualAB,
    funcDualWatchOn,        funcDualWatchOff,       funcVFOEqualMS,     funcMemoryMode,     funcMemoryRead,     funcMemoryWrite,
    funcMemoryToVFO,        funcMemoryClear,        funcReadOffsetFreq, funcWriteOffsetFreq,funcScanning,       funcVFOMainSelect,
    funcVFOSubSelect,       funcQuickSplit,         funcPreamp,         funcAttenuator,     funcAntenna,        funcBandStackReg,
    funcKeySpeed,           funcBreakMode,          funcSendCW,         funcDashRatio,      funcTime,           funcDate,
    funcUTCOffset,          funcTransceive,         funcCompLevel,      funcBreakInDelay,   funcNBLevel,        funcMonitorGain,
    funcVoxGain,            funcAntiVoxGain,        funcBrightLevel,    funcNB,             funcNR,             funcAutoNotch,
    funcCompressor,         funcMonitor,            funcVox,            FuncBKIN,           funcDialLock,       funcDSPFilterType,
    funcTransceiverId,      funcTxFreq,             funcAfMute,         funcUSBGain,        funcLANGain,        funcACC1Gain,
    funcACC2Gain,           funcRefCoarse,          funcRefFine,        funcNoiseLevel,     funcMainSubCmd,     funcIPP,
    funcSatelliteMode,      funcManualNotch,        funcMemoryContents, funcAGC,            funcRepeaterTone,   funcRepeaterTSQL,
    funcRepeaterDCS,        funcRepeaterCSQL,       funcXFC,            funcCIVOutput,      funcRXAntenna,      funcVoiceTX,

};

// Any changes to these strings WILL break rig definitions, add new ones to end. **Missing commas concatenate strings!**
static QString funcString[] { "None",
    "Power",                "Scope Data",           "Scope On/Off",     "Scope Wave Data",  "Scope Main/Sub",   "Scope Single/Dual",
    "Scope Center/Fixed",   "Scope Span",           "Scope Edge Freq",  "Scope Hold",       "Scope Ref",        "Scope Speed",
    "Scope During TX",      "Scope Center Freq",    "Scope Fixed Edge", "Scope VBW",        "Scope Fixed Freq", "Scope RBW",
    "Split",                "Rig ID",               "Rig CIV",          "Command Error FA", "Command OK FB",    "Notch Filter",
    "Freq (TRX)",           "Frequency (VFO)",      "Freq Get",         "Freq Set",         "Mode (TRX)",       "Mode (VFO)",
    "Mode Get",             "Mode Set",             "Tuning Step",      "Data Mode",        "Mode Filter",      "RIT",
    "RIT Value",            "RF Gain",              "Rx RF Gain",       "AF Gain",          "NR Level",         "Squelch",
    "IF Shift",             "PBT Inner",            "PBT Outer",        "Passband",         "CW Pitch",         "PSK Tone",
    "RTTY Mark",            "ATU Status",           "TX Status",        "Tx Power",         "Mic Gain",         "Mod Level",
    "Speech",               "Duplex Mode",          "Mod Input",        "Mod DATA1 Input",  "Mod DATA2 Input",  "Mod DATA3 Input",
    "Vd Meter",             "Id Meter",             "S Meter",          "Center Meter",     "Power Meter",      "SWR Meter",
    "ALC Meter",            "Comp Meter",           "TxRx Meter",       "Tone",             "TSQL",             "DTCS",
    "Rpt Access Mode",      "Rpt Duplex Offset",    "VFO A Select",     "VFO B Select",     "VFO Swap",         "VFO Equal AB",
    "VFO Dual Watch On",    "VFO Dual Watch Off",   "VFO Equal MS",     "Memory Mode",      "Memory Read",      "Memory Write",
    "Memory to VFO",        "Memory Clear",         "Read Offset Freq", "Write Offset Freq","Scanning",         "VFO Main Select",
    "VFO Sub Select",       "Quick Split",          "Preamp",           "Attenuator",       "Antenna",          "BandStack Reg",
    "Key Speed",            "Break Mode",           "Send CW",          "Dash Ratio",       "Time",             "Date",
    "UTC Offset",           "Transceive",           "Compressor Level", "Break-In Delay",   "NB Level",         "Monitor Gain",
    "Vox Gain",             "Anti-Vox Gain",        "Brightness Level", "Noise Blanker",    "Noise Reduction",  "Auto Notch",
    "Compressor",           "Monitor",              "Vox",              "Break-In",         "Dial Lock",        "DSP Filter Type",
    "Transceiver ID",       "Transmit Frequency",   "AF Mute",          "USB Gain",         "LAN Gain",         "ACC1 Gain",
    "ACC2 Gain",            "Adjust REF Coarse",    "Adjust Ref Fine",  "Noise Level",      "VFO Main/Sub",     "IPP",
    "Satellite Mode",       "Manual Notch",         "Memory Contents",  "AGC Setting",      "Repeater Tone",    "Repeater TSQL",
    "Repeater DCS",         "Repeater CSQL",        "XFC Status",       "CI-V Output",      "RX Antenna",       "Voice TX Memory"
};

struct spanType {
    spanType() {}
    spanType(int num, QString name, unsigned int freq) : num(num), name(name), freq(freq) {}
    int num;
    QString name;
    unsigned int freq;
};

struct funcType {
    funcType() {}
    funcType(funcs cmd, QString name, QByteArray data, int minVal, int maxVal) : cmd(cmd), name(name), data(data), minVal(minVal), maxVal(maxVal) {}
    funcs cmd;
    QString name;
    QByteArray data;
    int minVal;
    int maxVal;
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

enum usbDeviceType { usbNone = 0, shuttleXpress, shuttlePro2,
                     RC28, xBoxGamepad, unknownGamepad, eCoderPlus, QuickKeys,
                     StreamDeckMini,StreamDeckMiniV2,StreamDeckOriginal,StreamDeckOriginalV2,
                     StreamDeckOriginalMK2,StreamDeckXL,StreamDeckXLV2,StreamDeckPedal, StreamDeckPlus
                   };

enum usbCommandType{ commandButton, commandKnob, commandAny };
enum usbFeatureType { featureReset,featureResetKeys, featureEventsA, featureEventsB, featureFirmware, featureSerial, featureButton, featureSensitivity, featureBrightness,
                      featureOrientation, featureSpeed, featureColor, featureOverlay, featureTimeout, featureLCD, featureGraph, featureLEDControl };

#endif // WFVIEWTYPES_H
