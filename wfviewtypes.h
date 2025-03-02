
#ifndef WFVIEWTYPES_H
#define WFVIEWTYPES_H

#include <QObject>
#include <QString>
#include <QtGlobal>
#include <QColor>
#include <stdint.h>
//#include <memory>


enum valueType { typeNone=0, typeFloat, typeFloatDiv, typeFloatDiv5, typeUChar, typeUShort, typeChar, typeShort, typeBinary ,
                 typeFreq, typeMode, typeLevel, typeVFO, typeString, typedB, typeSplitVFO,typeVFOInfo, typeSWR, typeDouble};

enum connectionStatus_t { connDisconnected, connConnecting, connConnected };

enum underlay_t { underlayNone, underlayPeakHold, underlayPeakBuffer, underlayAverageBuffer };

enum connectionType_t { connectionUSB, connectionLAN, connectionWiFi, connectionWAN };

// meterString MUST be updated if any of these are changed, but this might break rigCaps.
enum meter_t {
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
    meterLatency,
    meterdBu,
    meterdBuEMF,
    meterdBm,
    meterSubS,
    meterUnknown
};

static QString meterString[19] {"None","S-Meter","Center","SWR","Power","ALC","Comp","Voltage","Current","RX dB","TX Mod", "RX Audio", "Audio", "Latency", "dBu", "dBu EMF", "dBm", "Sub S", ""};

enum spectrumMode_t {
    spectModeCenter=0x00,
    spectModeFixed=0x01,
    spectModeScrollC=0x02,
    spectModeScrollF=0x03,
    spectModeUnknown=0xff
};

enum rigMode_t {
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
    modeUnknown=0xff
};

enum selVFO_t {
    activeVFO = 0,
    inactiveVFO = 1
};

enum vfo_t {
    vfoA=0,
    vfoB=1,
    vfoMain = 0xD0,
    vfoSub = 0xD1,
    vfoCurrent = 0xfd,
    vfoMem = 0xfe,
    vfoUnknown = 0xff
};

enum duplexMode_t {
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
enum rptAccessTxRx_t {
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

enum pttType_t { pttCIV, pttRTS, pttDTR };

enum vfoModeType_t { vfoModeVfo, vfoModeMem, vfoModeSat };

enum manufacturersType_t {manufIcom=0, manufKenwood, manufFlexRadio};

struct lpfhpf {
    lpfhpf ():lpf(0),hpf(0) {};
    lpfhpf (ushort lpf, ushort hpf):lpf(lpf),hpf(hpf) {};
    ushort lpf=0;
    ushort hpf=0;
};

struct rptrAccessData {
    rptAccessTxRx_t accessMode = ratrNN;
    bool useSecondaryVFO = false;
    bool turnOffTone = false;
    bool turnOffTSQL = false;
    bool usingSequence = false;
    int sequence = 0;
};

struct modeInfo {
    modeInfo ():mk(modeUnknown), reg(0xff), filter(0xff),VFO(activeVFO), data(0xff), name(""), bwMin(0), bwMax(0), pass(0) {};
    modeInfo(rigMode_t mk, quint8 reg, QString name, int bwMin, int bwMax): mk(mk), reg(reg), filter(0xff), VFO(activeVFO), data(0xff), name(name),bwMin(bwMin), bwMax(bwMax), pass(0) {};
    rigMode_t mk;
    quint8 reg;
    quint8 filter; // Default filter is always 1
    selVFO_t VFO;
    quint8 data;
    QString name;
    int bwMin;
    int bwMax;
    int pass;
};

struct rigInfo {
    rigInfo (): civ(0), model(""), path(""),version(0.0) {};
    rigInfo (uchar civ, QString model, QString path, float version): civ(civ), model(model), path(path), version(version) {};
    uchar civ;
    QString model;
    QString path;
    float version;
};

struct antennaInfo {
    quint8 antenna;
    bool rx;
};

struct scopeData {
    bool valid=false;
    QByteArray data;
    uchar receiver;
    spectrumMode_t mode;
    uchar fixedEdge=0;
    bool oor;
    double startFreq;
    double endFreq;
};

struct toneInfo {
    toneInfo ():tone(0), name(""), tinv(false),rinv(false),useSecondaryVFO(false) {};
    toneInfo (short tone): tone(tone), name(""), tinv(false),rinv(false),useSecondaryVFO(false) {};
    toneInfo (short tone, QString name): tone(tone), name(name), tinv(false),rinv(false),useSecondaryVFO(false) {};
    toneInfo (short tone, QString name, bool tinv, bool rinv, bool useSecondaryVFO):tone(tone), name(name), tinv(tinv),rinv(rinv),useSecondaryVFO(useSecondaryVFO) {};
    ushort tone;
    QString name;
    bool tinv;
    bool rinv;
    bool useSecondaryVFO;

};

enum breakIn_t {
    brkinOff  = 0x00,
    brkinSemi = 0x01,
    brkinFull = 0x02
};

struct freqt {
    freqt ():Hz(0), MHzDouble(0.0), VFO(activeVFO) {};
    freqt(quint64 Hz, double MHzDouble, selVFO_t VFO): Hz(Hz), MHzDouble(MHzDouble), VFO(VFO) {};
    quint64 Hz;
    double MHzDouble;
    selVFO_t VFO;

};

struct datekind {
    uint16_t year;
    quint8 month;
    quint8 day;
};

struct timekind {
    quint8 hours;
    quint8 minutes;
    bool isMinus;
};

struct meterkind {
    double value;
    meter_t type;
};

// funcs and funcString MUST be updated at the same time, missing commas concatenate strings!
enum funcs { funcNone,
/* Commands 00-0f VFO Information*/
funcSep,
funcFreqTR,             funcModeTR,             funcBandEdgeFreq,           funcFreqGet,        	funcModeGet,        	funcFreqSet,
funcModeSet,            funcVFOSwapAB,          funcVFOSwapMS,              funcVFOEqualAB,     	funcVFOEqualMS,     	funcVFODualWatchOff,
funcVFODualWatchOn,     funcVFODualWatch,       funcVFOMainSelect,          funcVFOSubSelect,   	funcVFOASelect,     	funcVFOBSelect,
funcVFOBandMS,          funcMemoryMode,         funcMemoryWrite,            funcMemoryToVFO,    	funcMemoryClear,        funcReadFreqOffset,
funcSendFreqOffset,		funcScanning,		    funcVFOModeSelect,          funcSplitStatus,

/* Commands 10-13 Various basic settings*/
funcSepA,
funcTuningStep,         funcAttenuator,         funcAntenna,                funcSpeech,

/* Command 14 Levels */
funcSepB,
funcAfGain,             funcRfGain,             funcSquelch,                funcAPFLevel,           funcNRLevel,       		funcIFShift,
funcPBTInner,           funcPBTOuter,			funcCwPitch,                funcRFPower,            funcMicGain,            funcKeySpeed,
funcNotchFilter,        funcCompressorLevel,	funcBreakInDelay,           funcNBLevel,            funcDigiSelShift,		funcDriveGain,
funcMonitorGain,        funcVoxGain,			funcAntiVoxGain,            funcBackLightLevel,

/* Command 15 meters */
funcSepC,
funcSMeterSqlStatus,    funcSMeter,             funcAbsoluteMeter,          funcMeterType,          funcCenterMeter,        funcVariousSql,
funcPowerMeter,         funcSWRMeter,           funcALCMeter,               funcCompMeter,          funcVdMeter,            funcIdMeter,

/* Command 16 function en/dis */
funcSepD,
funcPreamp,             funcAGC,                funcNoiseBlanker,           funcAudioPeakFilter,    funcNoiseReduction,
funcAutoNotch,          funcRepeaterTone,       funcRepeaterTSQL,           funcRepeaterDTCS,       funcRepeaterCSQL,       funcCompressor,
funcMonitor,            funcVox,                funcBreakIn,                funcManualNotch,        funcDigiSel,            funcTwinPeakFilter,
funcDialLock,           funcRXAntenna,          funcDSPIFFilter,            funcManualNotchWidth,   funcSSBTXBandwidth,     funcMainSubTracking,
funcSatelliteMode,      funcDSQLSetting,        funcToneSquelchType,        funcIPPlus,             funcRoofingFilter,      funcFilterShape,

/* Commands 17-19 CW/power/id */
funcSepE,
funcSendCW,             funcPowerControl,		funcTransceiverId,
/* Commands 1A00-1A04 */
funcSepF,
funcMemoryContents,		funcBandStackReg,       funcMemoryKeyer,            funcFilterWidth,      funcAGCTimeConstant,

/* Command 1A0500 */
funcSepG,
/* Tone controls */
funcSSBRXHPFLPF,        funcSSBRXBass,          funcSSBRXTreble,            FuncAMRXHPFLPF,         funcAMRXBass,           funcAMRXTreble,
funcFMRXHPFLPF,         funcFMRXBass,           funcFMRXTreble,             FuncCWRXHPFLPF,         funcRTTYRXHPFLPF,
funcSSBTXBass,          funcSSBTXTreble,        funcAMTXBass,               funcAMTXTreble,         funcFMTXBass,           funcFMTXTreble,
funcBeepLevel,          funcBeepLevelLimit,     funcBeepConfirmation,       funcBandEdgeBeep,       funcBeepMain,           funcBeepSub,

funcRFSQLControl,       funcTXDelayHF,          funcTXDelay50m,             funcTimeOutTimer,       funcTimeOutCIV,

funcQuickDualWatch,     funcQuickSplit,
funcAutoRepeater,		funcTransverter,        funcTransverterOffset,      funcLockFunction,		funcREFAdjust,
funcREFAdjustFine,		funcACCAModLevel,		funcACCBModLevel,           funcUSBModLevel,		funcLANModLevel,		funcSPDIFModLevel,
funcDATAOffMod,         funcDATA1Mod,			funcDATA2Mod,               funcDATA3Mod,           funcCIVTransceive,		funcTime,
funcDate,               funcUTCOffset,			funcCLOCK2,                 funcCLOCK2UTCOffset,    funcCLOCK2Name,			funcDashRatio,
funcScanSpeed,          funcScanResume,			funcRecorderMode,           funcRecorderTX,         funcRecorderRX,			funcRecorderSplit,
funcRecorderPTTAuto,    funcRecorderPreRec,		funcRXAntConnector,         funcAntennaSelectMode,  funcNBDepth,			funcNBWidth,
funcVOXDelay,           funcVOXVoiceDelay,		funcAPFType,                funcAPFTypeLevel,       funcPSKTone,            funcRTTYMarkTone,
funcToneFreq,           funcTSQLFreq,           funcDTCSCode,               funcCSQLCode,
funcTXFreqMon,          funcReadUserTXFreqs,    funcCIVOutput,              funcVoiceTXLevel,           funcMainSubPrefix,		funcAFCSetting,

funcGPSTXMode,              funcSatelliteMemory,    funcGPSPosition,        funcMemoryGroup,

funcSepG2,
/* Command 1A0501 */
funcMonitorSignalWidth, funcScopeAveraging,     funcSpectrumFillType,        funcSpectrumFillColor,  funcSpectrumLineColor, funcSpectrumPeakColor,
funcWaterfallSet,       funcWaterfallSpeed,     funcWaterfallHeight,         funcWaterfallPeakLevel, funcMarkerAutoHide,

funcSecpG3,
/* Command 1A0502 */

funcScopeEdge1a,    funcScopeEdge2a,    funcScopeEdge3a,    funcScopeEdge4a,    funcScopeEdge1b,    funcScopeEdge2b,    funcScopeEdge3b,    funcScopeEdge4b,
funcScopeEdge1c,    funcScopeEdge2c,    funcScopeEdge3c,    funcScopeEdge4c,    funcScopeEdge1d,    funcScopeEdge2d,    funcScopeEdge3d,    funcScopeEdge4d,
funcScopeEdge1e,    funcScopeEdge2e,    funcScopeEdge3e,    funcScopeEdge4e,    funcScopeEdge1f,    funcScopeEdge2f,    funcScopeEdge3f,    funcScopeEdge4f,
funcScopeEdge1g,    funcScopeEdge2g,    funcScopeEdge3g,    funcScopeEdge4g,    funcScopeEdge1h,    funcScopeEdge2h,    funcScopeEdge3h,    funcScopeEdge4h,
funcScopeEdge1i,    funcScopeEdge2i,    funcScopeEdge3i,    funcScopeEdge4i,    funcScopeEdge1j,    funcScopeEdge2j,    funcScopeEdge3j,    funcScopeEdge4j,
funcScopeEdge1k,    funcScopeEdge2k,    funcScopeEdge3k,    funcScopeEdge4k,    funcScopeEdge1l,    funcScopeEdge2l,    funcScopeEdge3l,    funcScopeEdge4l,
funcScopeEdge1m,    funcScopeEdge2m,    funcScopeEdge3m,    funcScopeEdge4m,    funcScopeEdge1n,    funcScopeEdge2n,    funcScopeEdge3n,    funcScopeEdge4n,
funcScopeEdge1o,    funcScopeEdge2o,    funcScopeEdge3o,    funcScopeEdge4o,    funcScopeEdge1p,    funcScopeEdge2p,    funcScopeEdge3p,    funcScopeEdge4p,
funcScopeEdge1q,    funcScopeEdge2q,    funcScopeEdge3q,    funcScopeEdge4q,    funcScopeEdge1r,    funcScopeEdge2r,    funcScopeEdge3r,    funcScopeEdge4r,
funcScopeEdge1s,    funcScopeEdge2s,    funcScopeEdge3s,    funcScopeEdge4s,

/* Command 1A06-1A0A */
funcSepH,
funcDataModeWithFilter, funcAFMute,             funcOverflowStatus,

/* Command 1C */
funcSepI,
funcTransceiverStatus,  funcTunerStatus,        funcXFCStatus,              funcTXFreq,

/* Command 1E */
funcSepJ,
funcAvailableTXFreq,    funcTXBandEdgeFreq,     funcNumUserTXBandEdgeFreq,  funcUserTxBandEdge,

/* Command 21 */
funcSepK,
funcRitFreq,			funcRitStatus,          funcRitTXStatus,        funcXitFreq,            funcXitStatus,              funcXitTXStatus,

/* Command 25/26 */
funcSepL,
funcSelectedFreq,       funcUnselectedFreq,     funcSelectedMode,       funcUnselectedMode,     funcFreq,                   funcMode,

/* Command 27 */
funcSepM,
funcScopeWaveData,      funcScopeOnOff,
funcScopeDataOutput,    funcScopeMainSub,       funcScopeSingleDual,        funcScopeMode,          funcScopeSpan,          funcScopeEdge,
funcScopeHold,          funcScopeRef,           funcScopeSpeed,             funcScopeVBW,           funcScopeRBW,           funcScopCenterFreq,
funcScopeDuringTX,      funcScopeCenterType,    funcScopeFixedEdgeFreq,
/* Command 28 */
funcSepN,
funcVoiceTX,

/* OK/Error */
funcSepO,
funcFA,                 funcFB,

/* Some Kenwood Specific Commands */
funcSepP,
funcAutoInformation,    funcIFFilter,           funcDataMode,               funcRXFreqAndMode,      funcTXFreqAndMode,      funcTFSetStatus,
funcMemorySelect,       funcSetTransmit,        funcSetReceive,             funcRITDown,            funcRITUp,              funcScopeInfo,
funcScopeRange,         funcCWDecode,           funcScopeClear,             funcUSBScope,           funcTXEqualizer,        funcRXEqualizer,
funcFilterControlSSB,   funcFilterControlData,
// LAN Specific commands
funcConnectionRequest,  funcLogin,              funcVOIP,                   funcVOIPLevel,          funcVOIPBuffer,         funcTXInhibit,
funcLoginEnableDisable,


/* Special Commands (internal use only) */
funcSelectVFO,          funcSeparator,          funcLCDWaterfall,           funcLCDSpectrum,        funcLCDNothing,         funcPageUp,
funcPageDown,           funcVFOFrequency,       funcVFOMode,                funcRigctlFunction,     funcRigctlLevel,        funcRigctlParam,
funcRXAudio,            funcTXAudio,
// This MUST be the last defined func.
funcLastFunc
};


// Any changes to these strings WILL break rig definitions, add new ones to end. **Missing commas concatenate strings.
static QString funcString[funcLastFunc] { "None",
/* Commands 00-0f VFO Information*/
"+<CMD00-0f VFO>",
"Freq (TRX)",           "Mode (TRX)",           "Band Edge Freq",           "Freq Get",             "Mode Get",             "Freq Set",
"Mode Set",             "VFO Swap A/B",         "VFO Swap M/S",             "VFO Equal AB",         "VFO Equal MS",         "VFO Dual Watch Off",
"VFO Dual Watch On",	"VFO Dual Watch",       "VFO Main Select",          "VFO Sub Select",       "VFO A Select",         "VFO B Select",
"VFO Main/Sub Band",    "Memory Mode",          "Memory Write",             "Memory to VFO",        "Memory Clear",         "Read Freq Offset",
"Send Freq Offset",		"Scanning",				"VFO Mode Select",          "Split/Duplex",

/* Commands 10-13 Various basic settings */
"+<CMD10-13 Basic>",
"Tuning Step",          "Attenuator Status",    "Antenna",                  "Speech",

/* Command 14 Levels */
"+<CMD14 Levels>",
"AF Gain",              "RF Gain",              "Squelch",              "APF Level",			"NR Level",  			"IF Shift",
"PBT Inner",            "PBT Outer",            "CW Pitch",             "RF Power",				"Mic Gain",             "Key Speed",
"Notch Filter",         "Compressor Level",     "Break-In Delay",       "NB Level",				"DIGI-SEL Shift",		"Drive Gain",			
"Monitor Gain",         "Vox Gain",             "Anti-Vox Gain",        "Backlight Level",

/* Command 15 meters */
"+<CMD15 - Meters>",
"S Meter Sql Status",   "S Meter",				"Absolute Meter",           "Meter Type",           "Center Meter",         "Various Squelch",
"Power Meter",          "SWR Meter",            "ALC Meter",                "Comp Meter",           "Vd Meter",             "Id Meter",

"+<CMD16 - En/Dis>",
/* Command 16 function en/dis */
"Preamp Status",        "AGC Status",           "Noise Blanker",            "Audio Peak Filter",    "Noise Reduction",
"Auto Notch",           "Repeater Tone",        "Repeater TSQL",            "Repeater DTCS",        "Repeater CSQL",        "Compressor Status",
"Monitor Status",       "Vox Status",           "Break-In Status",          "Manual Notch",         "DIGI-Sel Status",      "Twin Peak Filter",
"Dial Lock Status",     "RX Antenna",           "DSP IF Filter",            "Manual Notch Width",   "SSB TX Bandwidth",     "Main/Sub Tracking",
"Satellite Mode",       "DSQL Setting",         "Tone Squelch Type",        "IP Plus Status",       "Roofing Filter",       "Filter Shape",

/* Commands 17-19 CW/power/id */
"+<CMD17-19>",
"Send CW",              "Power Control",        "Transceiver ID",

/* Commands 1A00-1A04 */
"+CMD1A00-1A04",
"Memory Contents",      "Band Stacking Reg",    "Memory Keyer",             "Filter Width",      "AGC Time Constant",

/* Command 1A05 */
"+<CMD1A0500>",
"SSB RX HPFLPF",        "SSB RX Bass",          "SSB RX Treble",            "AM RX HPFLPF",         "AM RX Bass",           "AM RX Treble",
"FM RX HPFLPF",         "FM RX Bass",           "FM RX Treble",             "CW RX HPFLPF",         "RTTY RX HPFLPF",
"SSB TX Bass",          "SSB TX Treble",         "AM TX Bass",              "AM TX Treble",         "FM TX Bass",           "FM TX Treble",
"Beep Level",           "Beep Level Limit",     "Beep Confirmation",        "Band Edge Beep",       "Beep Main Band",       "Beep Sub Band",

"RF SQL Control",       "TX Delay HF",          "TX Delay 50m",             "Timeout Timer",        "Timeout C-IV",

"Quick Dual Watch",     "Quick Split",
"Auto Repeater Mode",   "Transverter Function", "Transverter Offset",       "Lock Function",        "REF Adjust",
"REF Adjust Fine",      "ACC1 Mod Level",       "ACC2 Mod Level",           "USB Mod Level",        "LAN Mod Level",        "SPDIF Mod Level",
"Data Off Mod Input",   "DATA1 Mod Input",      "DATA2 Mod Input",          "DATA3 Mod Input",      "CIV Transceive",       "System Time",
"System Date",          "UTC Offset",           "CLOCK2 Setting",           "CLOCK2 UTC Offset",    "CLOCK 2 Name",         "Dash Ratio",
"Scanning Speed",       "Scanning Resume",      "Recorder Mode",            "Recorder TX",          "Recorder RX",          "Recorder Split",
"Recorder PTT Auto",    "Recorder Pre Rec",     "RX Ant Connector",         "Antenna Select Mode",  "NB Depth",             "NB Width",
"VOX Delay",            "VOX Voice Delay",      "APF Type",                 "APF Type Level",       "PSK Tone",             "RTTY Mark Tone",
"Tone Frequency",       "TSQL Frequency",       "DTCS Code/Polarity",       "CSQL Code",
"Transmit Freq Mon",    "Read User TX Freqs",   "CI-V Output (ANT)",        "Voice TX Level",       "Main/Sub Prefix",      "AFC Function",
"GPS TX Mode",          "Satellite Memory",     "GPS Position",             "Memory Group",

/* Command 1A05 */
"+<CMD1A0501>",                            
"Monitor Signal Width", "Scope Averaging",      "Spectrum Fill Type",       "Spectrum Fill Color",  "Spectrum Line Color",  "Spectrum Peak Color",
"Waterfall Set",        "Waterfall Speed",      "Waterfall Height",         "Waterfall Peak Level", "Marker Auto Hide",

/* Command 1A05 */
"+<CMD1A0502>",

"Scope Edge1 1.6Mhz",   "Scope Edge2 1.6Mhz",   "Scope Edge3 1.6Mhz",   "Scope Edge4 1.6Mhz",   "Scope Edge1 2Mhz",     "Scope Edge2 2Mhz",     "Scope Edge3 2Mhz",     "Scope Edge4 2Mhz",
"Scope Edge1 6Mhz",     "Scope Edge2 6Mhz",     "Scope Edge3 6Mhz",     "Scope Edge4 6Mhz",     "Scope Edge1 8Mhz",     "Scope Edge2 8Mhz",     "Scope Edge3 8Mhz",     "Scope Edge4 8Mhz",
"Scope Edge1 11Mhz",    "Scope Edge2 11Mhz",    "Scope Edge3 11Mhz",    "Scope Edge4 11Mhz",    "Scope Edge1 15Mhz",    "Scope Edge2 15Mhz",    "Scope Edge3 15Mhz",    "Scope Edge4 15Mhz",
"Scope Edge1 20Mhz",    "Scope Edge2 20Mhz",    "Scope Edge3 20Mhz",    "Scope Edge4 20Mhz",    "Scope Edge1 22Mhz",    "Scope Edge2 22Mhz",    "Scope Edge3 22Mhz",    "Scope Edge4 22Mhz",
"Scope Edge1 26Mhz",    "Scope Edge2 26Mhz",    "Scope Edge3 26Mhz",    "Scope Edge4 26Mhz",    "Scope Edge1 30Mhz",    "Scope Edge2 30Mhz",    "Scope Edge3 30Mhz",    "Scope Edge4 30Mhz",
"Scope Edge1 45Mhz",    "Scope Edge2 45Mhz",    "Scope Edge3 45Mhz",    "Scope Edge4 45Mhz",    "Scope Edge1 60Mhz",    "Scope Edge2 60Mhz",    "Scope Edge3 60Mhz",    "Scope Edge4 60Mhz",
"Scope Edge1 74Mhz",    "Scope Edge2 74Mhz",    "Scope Edge3 74Mhz",    "Scope Edge4 74Mhz",    "Scope Edge1 144Mhz",   "Scope Edge2 144Mhz",   "Scope Edge3 144Mhz",   "Scope Edge4 144Mhz",
"Scope Edge1 430Mhz",   "Scope Edge2 430Mhz",   "Scope Edge3 430Mhz",   "Scope Edge4 430Mhz",   "Scope Edge1 1200Mhz",  "Scope Edge2 1200Mhz",  "Scope Edge3 1200Mhz",  "Scope Edge4 1200Mhz",
"Scope Edge1 2400Mhz",  "Scope Edge2 2400Mhz",  "Scope Edge3 2400Mhz",  "Scope Edge4 2400Mhz",  "Scope Edge1 5600Mhz",  "Scope Edge2 5600Mhz",  "Scope Edge3 5600Mhz",  "Scope Edge4 5600Mhz",
"Scope Edge1 10Ghz",    "Scope Edge2 10Ghz",    "Scope Edge3 10Ghz",    "Scope Edge4 10Ghz",

/* Command 1A06-1A0A */
"+<CMD1A06-1A0A>",
"Data Mode Filter",     "AF Mute Status",       "Overflow Status",

/* Command 1C */
"+  <CMD1C>",
"Transceiver Status",   "Tuner/ATU Status",     "XFC Status",               "TX Freq",

/* Command 1E */
"+<CMD1E>",
"Available TX Freq",    "Read TX Band Edge",    "Read Num User TX Band",    "User TX Band Edge Freq",

/* Command 21 */
"+<CMD21>",
"RIT Frequency",       "RIT Status",               "RIT TX Status",         "XIT Frequency",       "XIT Status",               "XIT TX Status",

/* Command 25/26 */
"+<CMD25/26 Freq>",
"Selected Freq",        "Unselected Freq",      "Selected Mode",        "Unselected Mode",      "RX Frequency",             "RX Mode",

/* Command 27 - Scope */
"+<CMD27 - Scope>",
"Scope Wave Data",      "Scope On/Off",
"Scope Data Output",    "Scope Main/Sub",       "Scope Single/Dual",        "Scope Mode",           "Scope Span",           "Scope Edge",
"Scope Hold",           "Scope Ref",            "Scope Speed",              "Scope VBW",            "Scope RBW",            "Scope Center Freq",
"Scope During TX",      "Scope Center Type",    "Scope Fixed Edge Freq",

/* Command 28 */
"+<CMD28 Voice TX>",
"Voice TX",

"+<Response Codes>",
"Command Error FA",     "Command OK FB",

"+<Kenwood Only>",
"Auto Information",     "IF Filter Only",       "Data Mode Only",           "RX Freq And Mode",     "TX Freq And Mode",      "TF-Set Status",
"Memory Num Select",    "Set Transmit Mode",    "Set Receive Mode",         "RIT Frequency Down",   "RIT Frequency Up",     "Scope Information",
"Scope Range",          "CW Decode",            "Scope Clear",               "USB Scope Data",       "TX Equalizer",         "RX Equalizer",
"Filter Control SSB",   "Filter Control Data",
// LAN Specific commands
"Connection Request",  "Network Login",         "VOIP Function",            "VOIP Level",           "VOIP Buffer",          "TX Inhibit",
"Enable/Disable Login",

/* Special Commands */
"-Select VFO",          "-Seperator",
"-LCD Waterfall",       "-LCD Spectrum",        "-LCD Nothing",             "-Page Up",             "-Page Down",           "-VFO Frequency",
"-VFO Mode",            "-Rigctl Function",     "-Rigctl Level",            "-Rigctl Param",        "-RX Audio Data",       "-TX Audio Data"
};

struct spanType {
    spanType() {}
    spanType(int num, QString name, unsigned int freq) : num(num), name(name), freq(freq) {}
    int num;
    QString name;
    unsigned int freq;
};

struct funcType {
    funcType() {cmd=funcNone, name="None", data=QByteArray(), minVal=0, maxVal=0, cmd29=false, getCmd=false, setCmd=false, bytes=0, admin=false; }
    funcType(funcs cmd, QString name, QByteArray data, int minVal, int maxVal, bool cmd29, bool getCmd, bool setCmd, uchar bytes, bool admin) :
        cmd(cmd), name(name), data(data), minVal(minVal), maxVal(maxVal), cmd29(cmd29), getCmd(getCmd), setCmd(setCmd), bytes(bytes), admin(admin) {}
    funcs cmd;
    QString name;
    QByteArray data;
    int minVal;
    int maxVal;
    bool cmd29;
    bool getCmd;
    bool setCmd;
    uchar bytes;
    bool admin;
};

//struct commandtype {
//    cmds cmd;
//    std::shared_ptr<void> data;
//};

struct stepType {
    stepType(){};
    stepType(quint8 num, QString name, quint64 hz) : num(num), name(name), hz(hz) {};
    quint8 num;
    QString name;
    quint64 hz;
};

struct spectrumBounds {
    spectrumBounds(){};
    spectrumBounds(double start, double end, uchar edge) : start(start), end(end), edge(edge) {};
    double start;
    double end;
    uchar edge;

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
    quint8 skip=0;
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
    QString tone="67.0";
    QString toneB="67.0";
    QString tsql="67.0";
    QString tsqlB="67.0";
    quint8 dsql=0;
    quint8 dsqlB=0;
    quint16 dtcs=23;
    quint16 dtcsB=23;
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
    uchar tuningStep=0;
    uchar tuningStepB=0;
    quint16 progTs=0;
    quint16 progTsB=0;
    quint8 atten=0;
    quint8 attenB=0;
    quint8 preamp=0;
    quint8 preampB=0;
    quint8 antenna=0;
    quint8 antennaB=0;
    bool ipplus=false;
    bool ipplusB=false;
    bool p25Sql=false;
    quint16 p25Nac=0x293; // The default NAC?
    quint8 dPmrSql=0;
    quint16 dPmrComid=254;
    quint8 dPmrCc=0;
    bool dPmrSCRM=false;
    unsigned int dPmrKey=1;
    bool nxdnSql=false;
    quint8 nxdnRan=0;
    bool nxdnEnc=false;
    unsigned int nxdnKey=0;
    bool dcrSql=false;
    quint16 dcrUc=0;
    bool dcrEnc=0;
    unsigned int dcrKey=0;
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

struct commandErrorType{
    commandErrorType() : func(funcNone), data(QByteArray()), minValue(0), maxValue(0), bytes(0) {};
    commandErrorType(funcs func, QByteArray data, int minValue, int maxValue, char bytes) :
        func(func), data(data), minValue(minValue), maxValue(maxValue), bytes(bytes)  {};

    funcs func;
    QByteArray data;
    int minValue;
    int maxValue;
    uchar bytes;
};

enum audioType {qtAudio,portAudio,rtAudio,tciAudio};
enum codecType { LPCM, PCMU, OPUS, ADPCM };

enum passbandActions {passbandStatic, pbtInnerMove, pbtOuterMove, pbtMoving, passbandResizing};

enum usbDeviceType { usbNone = 0, shuttleXpress, shuttlePro2,
                     RC28, xBoxGamepad, unknownGamepad, eCoderPlus, QuickKeys,
                     StreamDeckMini,StreamDeckMiniV2,StreamDeckOriginal,StreamDeckOriginalV2,
                     StreamDeckOriginalMK2,StreamDeckXL,StreamDeckXLV2,StreamDeckPedal, StreamDeckPlus,
                     XKeysXK3,MiraBox293, MiraBox293S, MiraBoxN3
};

enum usbCommandType{ commandButton, commandKnob, commandAny };
enum usbFeatureType { featureReset,featureResetKeys, featureEventsA, featureEventsB, featureFirmware, featureSerial, featureButton, featureSensitivity, featureBrightness,
                      featureOrientation, featureSpeed, featureColor, featureOverlay, featureTimeout, featureLCD, featureGraph, featureLEDControl,
                      featureWakeScreen, featureClearScreen, featureRefresh, featureGetImageBuffer};


struct periodicType {
    periodicType() {};
    periodicType(funcs func, QString priority, char receiver) : func(func), priority(priority), prioVal(0), receiver(receiver) {};
    periodicType(funcs func, QString priority, int prioVal, char receiver) : func(func), priority(priority), prioVal(prioVal), receiver(receiver) {};
    funcs func;
    QString priority;
    int prioVal;
    char receiver;
};

struct vfoCommandType {
    vfoCommandType (): freqFunc(funcNone), modeFunc(funcNone), vfo(vfoUnknown), receiver(0) {};
    vfoCommandType(funcs freqFunc, funcs modeFunc, vfo_t vfo, uchar receiver):
                freqFunc(freqFunc), modeFunc(modeFunc), vfo(vfo), receiver(receiver) {};
    funcs freqFunc;
    funcs modeFunc;
    vfo_t vfo;
    uchar receiver;
};


struct rigStateType {
    rigStateType(): vfoMode(vfoModeType_t::vfoModeVfo),vfo(vfoUnknown),receiver(0) {};
    vfoModeType_t vfoMode;
    vfo_t vfo;
    uchar receiver;
};

// Some global "helper" functions can go here for now, maybe look at a better location at some point?
inline QColor colorFromString(const QString& color)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    return QColor::fromString(color);
#else
    QColor c;
    c.setNamedColor(color);
    return c;
#endif
}

inline QString getMeterDebug(meter_t m) {
    QString rtn = QString("Meter name: ");
    switch(m) {
    case meterNone:
        rtn.append("meterNone");
        break;
    case meterS:
    case meterSubS:
        rtn.append("meterS");
        break;
    case meterCenter:
        rtn.append("meterCenter");
        break;
    case meterSWR:
        rtn.append("meterSWR");
        break;
    case meterPower:
        rtn.append("meterPower");
        break;
    case meterALC:
        rtn.append("meterALC");
        break;
    case meterComp:
        rtn.append("meterComp");
        break;
    case meterVoltage:
        rtn.append("meterVoltage");
        break;
    case meterCurrent:
        rtn.append("meterCurrent");
        break;
    case meterRxdB:
        rtn.append("meterRxdB");
        break;
    case meterTxMod:
        rtn.append("meterTxMod");
        break;
    case meterRxAudio:
        rtn.append("meterRxAudio");
        break;
    case meterLatency:
        rtn.append("meterLatency");
        break;
    case meterdBu:
        rtn.append("meterdBu");
        break;
    case meterdBuEMF:
        rtn.append("meterdBuEMF");
        break;
    case meterdBm:
        rtn.append("meterdBm");
        break;
    default:
        rtn.append("UNKNOWN");
        break;
    }
    return rtn;
}

Q_DECLARE_METATYPE(freqt)
Q_DECLARE_METATYPE(spectrumMode_t)
Q_DECLARE_METATYPE(rigMode_t)
Q_DECLARE_METATYPE(vfo_t)
Q_DECLARE_METATYPE(duplexMode_t)
Q_DECLARE_METATYPE(rptAccessTxRx_t)
Q_DECLARE_METATYPE(pttType_t)
Q_DECLARE_METATYPE(rptrAccessData)
Q_DECLARE_METATYPE(usbFeatureType)
Q_DECLARE_METATYPE(funcs)
Q_DECLARE_METATYPE(memoryType)
Q_DECLARE_METATYPE(antennaInfo)
Q_DECLARE_METATYPE(scopeData)
Q_DECLARE_METATYPE(timekind)
Q_DECLARE_METATYPE(datekind)
Q_DECLARE_METATYPE(toneInfo)
Q_DECLARE_METATYPE(meter_t)
Q_DECLARE_METATYPE(meterkind)
Q_DECLARE_METATYPE(spectrumBounds)
Q_DECLARE_METATYPE(rigInfo)
Q_DECLARE_METATYPE(lpfhpf)

#endif // WFVIEWTYPES_H
