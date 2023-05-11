
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
    modeLSB=0,          //0
    modeUSB,            //1
    modeAM,             //2
    modeCW,             //3
    modeRTTY,           //4
    modeFM,             //5
    modeCW_R,           //6
    modeRTTY_R,         //7
    modePSK,            //8
    modePSK_R,          //9
    modeLSB_D,          //10
    modeUSB_D,          //11
    modeDV,             //12
    modeATV,            //13
    modeDD,             //14
    modeWFM,            //15
    modeS_AMD,          //16
    modeS_AML,          //17
    modeS_AMU,          //18
    modeP25,            //19
    modedPMR,           //20
    modeNXDN_VN,        //21
    modeNXDN_N,         //22
    modeDCR,            //23
    modeUnknown         //24
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
    mode_info() {};
    mode_info(mode_kind mk, quint8 reg, QString name, bool bw): mk(mk), reg(reg), name(name),bw(bw) {};
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
    cmdGetCompLevel, cmdSetCompLevel, cmdGetTuningStep, cmdSetTuningStep,
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
    cmdGetMemory, cmdGetSatMemory, cmdSetMemory, cmdClearMemory,cmdRecallMemory, cmdSetVFOMode, cmdSetMemoryMode, cmdSetSatelliteMode,
    // Below Only used for USB Controller at the moment.
    cmdSetBandUp, cmdSetBandDown, cmdSetModeUp, cmdSetModeDown, cmdSetStepUp, cmdSetStepDown,
    cmdSetSpanUp, cmdSetSpanDown, cmdIFFilterUp, cmdIFFilterDown, cmdPageDown, cmdPageUp,
    cmdLCDWaterfall, cmdLCDSpectrum, cmdLCDNothing, cmdSeparator
};


// funcs and funcString MUST match exactly (and NUMFUNCS must be updated)
#define NUMFUNCS 188

enum funcs { funcNone,
funcfreqTR,             funcModeTR,             funcBandEdgeFreq,           funcFreqGet,        	funcModeGet,        	funcFreqSet,			// \x00
funcModeSet,            funcVFOSwapAB,          funcVFOSwapMS,              funcVFOEqualAB,     	funcVFOEqualMS,     	funcVFODualWatchOff,	// \x06
funcVFODualWatchOn,     funcVFODualWatch,       funcVFOMainSelect,          funcVFOSubSelect,   	funcVFOASelect,     	funcVFOBSelect,			// \x07
funcVFOBandMS,          funcMemoryMode,         funcMemoryWrite,            funcMemoryToVFO,    	funcMemoryClear,    	funcReadFreqOffset,
funcSendFreqOffset,		funcScanning,			funcSplitStatus,            funcDuplexStatus, 		funcTuningStep,         funcAttenuator,
funcAntenna,        	funcSpeech,        		funcAfGain,                 funcRfGain,             funcSquelch,            funcAPFLevel,
funcNRLevel,       		funcIFShift,            funcPBTInner,               funcPBTOuter,			funcCwPitch,            funcRFPower,
funcMicGain,            funcKeySpeed,			funcNotchFilter,            funcCompressorLevel,	funcBreakInDelay,		funcNBLevel,
funcDigiSelShift,		funcDriveGain,			funcMonitorGain,            funcVoxGain,			funcAntiVoxGain,		funcBackLight,
funcSMeterSqlStatus,    funcSMeter,             funcCenterMeter,            funcVariousSql,         funcOverflowStatus,     funcPowerMeter,
funcSWRMeter,			funcALCMeter,          	funcCompMeter,              funcVdMeter,            funcIdMeter,			funcPreamp,
funcAGCTime,			funcNoiseBlanker,       funcAudioPeakFilter,        funcNoiseReduction,		funcAutoNotch,          funcRepeaterTone,
funcRepeaterTSQL,		funcRepeaterDTCS,       funcRepeaterCSQL,           funcCompressor,			funcMonitor,            funcVox,
funcBreakIn,			funcManualNotch,        funcDigiSel,                funcTwinPeakFilter,		funcDialLock,			funcRXAntenna,
funcDSPIFFilter,		funcNotchWidth,         funcSSBBandwidth,           funcMainSubTracking,	funcSatelliteMode,      funcDSQLSetting,
funcToneSquelchType,    funcIPPlus,				funcSendCW,                 funcPowerControl,		funcTransceiverId,		funcFilterWidth,
funcMemoryContents,		funcBandStackReg,		funcMemoryKeyer,            funcIFFilterWidth,      funcQuickDualWatch,		funcQuickSplit,
funcAutoRepeater,		funcTunerStatus,		funcTransverter,            funcTransverterOffset,  funcLockFunction,		funcREFAdjust,
funcREFAdjustFine,		funcACC1ModLevel,		funcACC2ModLevel,           funcUSBModLevel,		funcLANModLevel,		funcDATAOffMod,
funcDATA1Mod,			funcDATA2Mod,			funcDATA3Mod,               funcCIVTransceive,		funcTime,           	funcDate,
funcUTCOffset,			funcCLOCK2,				funcCLOCK2UTCOffset,        funcCLOCK2Name,			funcDashRatio,			funcScanSpeed,
funcScanResume,			funcRecorderMode,		funcRecorderTX,             funcRecorderRX,			funcRecorderSplit,		funcRecorderPTTAuto,
funcRecorderPreRec,		funcRXAntConnector,		funcAntennaSelectMode,      funcNBDepth,			funcNBWidth,			funcVOXDelay,
funcVOXVoiceDelay,		funcAPFType,			funcAPFTypeLevel,           funcPSKTone,            funcRTTYMarkTone,       funcDataModeWithFilter,
funcAFMute,				funcToneFreq,           funcTSQLFreq,               funcDTCSCode,           funcCSQLCode,           funcTransceiverStatus,
funcXFCStatus,			funcReadTXFreq,			funcCIVOutput,              funcReadTXFreqs,        funcReadUserTXFreqs,	funcUserTXBandEdgeFreq,
funcRITFreq,			funcRitStatus,			funcRitTXStatus,            funcMainSubFreq,		funcMainSubMode,		funcScopeWaveData,
funcScopeOnOff,			funcScopeDataOutput,	funcScopeMainSub,           funcScopeSingleDual,	funcScopeCenterFixed,	funcScopeCenterSpan,
funcScopeEdgeNumber,  	funcScopeHold,      	funcScopeRef,               funcScopeSpeed,			funcScopeDuringTX,      funcScopeCenterType,
funcScopeVBW,       	funcScopeFixedFreq, 	funcScopeRBW,               funcVoiceTX,			funcMainSubPrefix,		funcAFCSetting,
funcGPSTXMode,          funcSatelliteMemory,    funcGPSPosition,            funcMemoryGroup,        funcSelectVFO,          funcFA,
funcFB
};
             

// Any changes to these strings WILL break rig definitions, add new ones to end. **Missing commas concatenate strings!**
static QString funcString[] { "None",
"Freq (TRX)",           "Mode (TRX)",           "Band Edge Freq",           "Freq Get",             "Mode Get",             "Freq Set",
"Mode Set",             "VFO Swap A/B",         "VFO Swap M/S",             "VFO Equal AB",         "VFO Equal MS",         "VFO Dual Watch Off",
"VFO Dual Watch On",	"VFO Dual Watch",       "VFO Main Select",          "VFO Sub Select",       "VFO A Select",         "VFO B Select",
"VFO Main/Sub Band",    "Memory Mode",          "Memory Write",             "Memory to VFO",        "Memory Clear",         "Read Freq Offset",
"Send Freq Offset",		"Scanning",				"Split Operation",          "Duplex Operation",     "Tuning Step",          "Attenuator Status",
"Antenna",          	"Speech",           	"AF Gain",                  "RF Gain",              "Squelch",              "APF Level",
"NR Level",  			"IF Shift",             "PBT Inner",                "PBT Outer",            "CW Pitch",             "RF Power",
"Mic Gain",             "Key Speed",			"Notch Filter",             "Compressor Level",     "Break-In Delay",       "NB Level",
"DIGI-SEL Shift",		"Drive Gain",			"Monitor Gain",             "Vox Gain",             "Anti-Vox Gain",        "Backlight Level",
"S Meter Sql Status",   "S Meter",				"Center Meter",             "Various Squelch",      "Overflow Status",      "Power Meter",
"SWR Meter",            "ALC Meter",            "Comp Meter",               "Vd Meter",             "Id Meter",             "Preamp Status",
"AGC Time Constant",    "Noise Blanker",        "Audio Peak Filter",        "Noise Reduction",      "Auto Notch",           "Repeater Tone",
"Repeater TSQL",        "Repeater DTCS",        "Repeater CSQL",            "Compressor Status",    "Monitor Status",       "Vox Status",
"Break-In Status",      "Manual Notch",         "DIGI-Sel Status",          "Twin Peak Filter",     "Dial Lock Status",     "RX Antenna",
"DSP IF Filter",        "Manual Notch Width",   "SSB Bandwidth",            "Main/Sub Tracking",    "Satellite Mode",       "DSQL Setting",
"Tone Squelch Type",    "IP Plus Status",       "Send CW",                  "Power Control",        "Transceiver ID",       "Filter Width",
"Memory Contents",      "Band Stacking Reg",    "Memory Keyer",             "IF Filter Width",      "Quick Dual Watch",     "Quick Split",
"Auto Repeater Mode",   "Tuner/ATU Status",     "Transverter Function",     "Transverter Offset",   "Lock Function",        "REF Adjust",
"REF Adjust Fine",      "ACC1 Mod Level",       "ACC2 Mod Level",           "USB Mod Level",        "LAN Mod Level",        "Data Off Mod Input",
"DATA1 Mod Input",      "DATA2 Mod Input",  	"DATA3 Mod Input",          "CIV Transceive",       "System Time",          "System Date",
"UTC Offset",           "CLOCK2 Setting",       "CLOCK2 UTC Offset",        "CLOCK 2 Name",         "Dash Ratio",           "Scanning Speed",
"Scanning Resume",      "Recorder Mode",        "Recorder TX",              "Recorder RX",          "Recorder Split",       "Recorder PTT Auto",
"Recorder Pre Rec",     "RX Ant Connector",     "Antenna Select Mode",      "NB Depth",             "NB Width",             "VOX Delay",
"VOX Voice Delay",      "APF Type",             "APF Type Level",           "PSK Tone",             "RTTY Mark Tone",       "Data Mode Filter",
"AF Mute Status",       "Tone Frequency",       "TSQL Frequency",           "DTCS Code/Polarity",   "CSQL Code",           "Transceiver Status",
"XFC Status",           "Read TX Freq",         "CI-V Output",              "Read TX Freqs",        "Read User TX Freqs",   "User TX Band Edge Freq",
"RIT Frequency",        "RIT Status",           "RIT TX Status",            "Main/Sub Freq",        "Main/Sub Mode",        "Scope Wave Data",
"Scope On/Off",         "Scope Data Output",    "Scope Main/Sub",           "Scope Single/Dual",    "Scope Center Fixed",   "Scope Center Span",
"Scope Edge Number",    "Scope Hold",           "Scope Ref",                "Scope Speed",          "Scope During TX",      "Scope Center Type",
"Scope VBW",            "Scope Fixed Freq",     "Scope RBW",                "Voice TX",             "Main/Sub Prefix",      "AFC Function",
"GPS TX Mode",          "Satellite Memory",     "GPS Position",             "Memory Group",         "(int)Select VFO",      "Command Error FA",
"Command OK FB"
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

struct stepType {
    stepType(){};
    stepType(unsigned char num, QString name, quint64 hz) : num(num), name(name), hz(hz) {};
    unsigned char num;
    QString name;
    quint64 hz;
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


struct memoryType {
    quint16 group=0;
    quint16 channel=0;
    quint8 split=0;
    quint8 scan=0;
    quint8 vfo=0;
    quint8 vfoB=0;
    freqt frequency;
    freqt frequencyB;
    quint8 mode=0;
    quint8 modeB=0;
    quint8 filter=0;
    quint8 filterB=0;
    quint8 datamode=0;
    quint8 datamodeB=0;
    quint8 duplex=0;
    quint8 duplexB=0;
    quint8 tonemode=0;
    quint8 tonemodeB=0;
    quint16 tone=670;
    quint16 toneB=670;
    quint16 tsql=670;
    quint16 tsqlB=670;
    quint8 dsql=0;
    quint8 dsqlB=0;
    quint16 dtcs=0;
    quint16 dtcsB=0;
    quint8 dtcsp=0;
    quint8 dtcspB=0;
    quint8 dvsql=0;
    quint8 dvsqlB=0;
    freqt duplexOffset;
    freqt duplexOffsetB;
    char UR[9];
    char URB[9];
    char R1[9];
    char R2[9];
    char R1B[9];
    char R2B[9];
    char name[24]; // 1 more than the absolute max
    bool sat=false;
    bool del=false;
};


struct memParserFormat{
    memParserFormat(char spec, int pos, int len) : spec(spec), pos(pos), len(len) {};
    char spec;
    int pos;
    int len;
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
