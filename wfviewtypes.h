
#ifndef WFVIEWTYPES_H
#define WFVIEWTYPES_H

#include <QObject>
#include <QString>
#include <QtGlobal>
#include <QColor>
#include <stdint.h>
//#include <memory>


enum valueType { typeNone=0, typeFloat, typeFloatDiv, typeFloatDiv5, typeUChar, typeUShort,
                 typeChar, typeShort, typeBinary , typeFreq, typeMode, typeLevel, typeVFO, typeString, typedB, typeSplitVFO,typeVFOInfo};

enum connectionStatus_t { connDisconnected, connConnecting, connConnected };

enum underlay_t { underlayNone, underlayPeakHold, underlayPeakBuffer, underlayAverageBuffer };

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
    meterdBm
};


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
    vfoSub = 0xD1,
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

enum pttType_t {
    pttCIV=0x00,
    pttRTS=0x01,
    pttDTR=0x01
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
    modeInfo ():mk(modeUnknown), reg(99), filter(1),VFO(activeVFO), data(0), name(""), bwMin(0), bwMax(0), pass(0) {};
    modeInfo(rigMode_t mk, quint8 reg, QString name, int bwMin, int bwMax): mk(mk), reg(reg), filter(1), VFO(activeVFO), data(false), name(name),bwMin(bwMin), bwMax(bwMax), pass(0) {};
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
    bool oor;
    double startFreq;
    double endFreq;
};

struct toneInfo {
    toneInfo ():tone(0), tinv(false),rinv(false),useSecondaryVFO(false) {};
    toneInfo (toneInfo const &t): tone(t.tone), tinv(t.tinv), rinv(t.rinv), useSecondaryVFO(t.useSecondaryVFO) {};
    toneInfo (quint16 tone):tone(tone), tinv(false),rinv(false),useSecondaryVFO(false) {};
    toneInfo (quint16 tone, bool tinv, bool rinv, bool useSecondaryVFO):tone(tone), tinv(tinv),rinv(rinv),useSecondaryVFO(useSecondaryVFO) {};
    quint16 tone;
    bool tinv;
    bool rinv;
    bool useSecondaryVFO;
    toneInfo &operator=(const toneInfo &i) {
        this->tone=i.tone;
        this->tinv=i.tinv;
        this->rinv=i.rinv;
        this->useSecondaryVFO=i.useSecondaryVFO;
        return *this;
    }
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
    float value;
    meter_t type;
};

// funcs and funcString MUST match exactly (and NUMFUNCS must be updated)
#define NUMFUNCS 240

enum funcs { funcNone,
funcFreqTR,             funcModeTR,             funcBandEdgeFreq,           funcFreqGet,        	funcModeGet,        	funcFreqSet,			// \x00
funcModeSet,            funcVFOSwapAB,          funcVFOSwapMS,              funcVFOEqualAB,     	funcVFOEqualMS,     	funcVFODualWatchOff,	// \x06
funcVFODualWatchOn,     funcVFODualWatch,       funcVFOMainSelect,          funcVFOSubSelect,   	funcVFOASelect,     	funcVFOBSelect,			// \x07
funcVFOBandMS,          funcMemoryMode,         funcMemoryWrite,            funcMemoryToVFO,    	funcMemoryClear,    	funcReadFreqOffset,
funcSendFreqOffset,		funcScanning,		    funcVFOModeSelect,          funcSplitStatus,        funcTuningStep,         funcAttenuator,
funcAntenna,        	funcSpeech,        		funcAfGain,                 funcRfGain,             funcSquelch,            funcAPFLevel,
funcNRLevel,       		funcIFShift,            funcPBTInner,               funcPBTOuter,			funcCwPitch,            funcRFPower,
funcMicGain,            funcKeySpeed,			funcNotchFilter,            funcCompressorLevel,	funcBreakInDelay,		funcNBLevel,
funcDigiSelShift,		funcDriveGain,			funcMonitorGain,            funcVoxGain,			funcAntiVoxGain,		funcBackLight,
funcSMeterSqlStatus,    funcSMeter,             funcAbsoluteMeter,          funcMeterType,
funcCenterMeter,        funcVariousSql,         funcOverflowStatus,         funcPowerMeter,
funcSWRMeter,			funcALCMeter,          	funcCompMeter,              funcVdMeter,            funcIdMeter,			funcPreamp,
funcAGCTime,			funcNoiseBlanker,       funcAudioPeakFilter,        funcNoiseReduction,		funcAutoNotch,          funcRepeaterTone,
funcRepeaterTSQL,		funcRepeaterDTCS,       funcRepeaterCSQL,           funcCompressor,			funcMonitor,            funcVox,
funcBreakIn,			funcManualNotch,        funcDigiSel,                funcTwinPeakFilter,		funcDialLock,			funcRXAntenna,
funcDSPIFFilter,		funcManualNotchWidth,   funcSSBTXBandwidth,         funcMainSubTracking,	funcSatelliteMode,      funcDSQLSetting,
funcToneSquelchType,    funcIPPlus,				funcSendCW,                 funcPowerControl,		funcTransceiverId,		funcFilterWidth,
funcMemoryContents,		funcBandStackReg,		funcMemoryKeyer,            funcIFFilterWidth,      funcQuickDualWatch,		funcQuickSplit,
funcAutoRepeater,		funcTunerStatus,		funcTransverter,            funcTransverterOffset,  funcLockFunction,		funcREFAdjust,
funcREFAdjustFine,		funcACCAModLevel,		funcACCBModLevel,           funcUSBModLevel,		funcLANModLevel,		funcSPDIFModLevel,
funcDATAOffMod,         funcDATA1Mod,			funcDATA2Mod,               funcDATA3Mod,           funcCIVTransceive,		funcTime,
funcDate,               funcUTCOffset,			funcCLOCK2,                 funcCLOCK2UTCOffset,    funcCLOCK2Name,			funcDashRatio,
funcScanSpeed,          funcScanResume,			funcRecorderMode,           funcRecorderTX,         funcRecorderRX,			funcRecorderSplit,
funcRecorderPTTAuto,    funcRecorderPreRec,		funcRXAntConnector,         funcAntennaSelectMode,  funcNBDepth,			funcNBWidth,
funcVOXDelay,           funcVOXVoiceDelay,		funcAPFType,                funcAPFTypeLevel,       funcPSKTone,            funcRTTYMarkTone,
funcDataModeWithFilter, funcAFMute,				funcToneFreq,               funcTSQLFreq,           funcDTCSCode,           funcCSQLCode,
funcTransceiverStatus,  funcXFCStatus,			funcReadTXFreq,             funcCIVOutput,          funcReadTXFreqs,        funcReadUserTXFreqs,
funcUserTXBandEdgeFreq, funcRITFreq,			funcRitStatus,              funcRitTXStatus,        funcSelectedFreq,       funcUnselectedFreq,
funcSelectedMode,       funcUnselectedMode,     funcFreq,                   funcMode,               funcScopeWaveData,      funcScopeOnOff,
funcScopeDataOutput,    funcScopeMainSub,       funcScopeSingleDual,        funcScopeMode,          funcScopeSpan,          funcScopeEdge,
funcScopeHold,          funcScopeRef,           funcScopeSpeed,             funcScopeVBW,           funcScopeRBW,
funcScopeDuringTX,      funcScopeCenterType,    funcScopeFixedEdgeFreq,     funcVoiceTX,			funcMainSubPrefix,		funcAFCSetting,
funcSSBRXHPFLPF,        funcSSBRXBass,          funcSSBRXTreble,            FuncAMRXHPFLPF,         funcAMRXBass,           funcAMRXTreble,
funcFMRXHPFLPF,         funcFMRXBass,           funcFMRXTreble,             FuncCWRXHPFLPF,         funcCWRXTreble,         funcCWRXBass,
funcSSBTXHLPLPF,        funcSSBTXBass,          funcSSBTXTreble,            FuncAMTXHPFLPF,         funcAMTXBass,           funcAMTXTreble,
funcFMTXHPFLPF,         funcFMTXBass,           funcFMTXTreble,             funcBeepLevel,          funcBeepLevelLimit,     funcBeepConfirmation,
funcBandEdgeBeep,       funcBeepMain,           funcBeepSub,                funcRFSQLControl,       funcTXDelayHF,          funcTXDelay50m,
funcTimeOutTimer,       funcTimeOutCIV,
funcGPSTXMode,          funcSatelliteMemory,    funcGPSPosition,            funcMemoryGroup,        funcSelectVFO,          funcSeparator,
funcLCDWaterfall,       funcLCDSpectrum,        funcLCDNothing,             funcPageUp,             funcPageDown,           funcVFOFrequency,
funcVFOMode,            funcRigctlFunction,     funcRigctlLevel,            funcRigctlParam,        funcRXAudio,            funcTXAudio,
funcFA,                 funcFB
};


// Any changes to these strings WILL break rig definitions, add new ones to end. **Missing commas concatenate strings!**
static QString funcString[] { "None",
"Freq (TRX)",           "Mode (TRX)",           "Band Edge Freq",           "Freq Get",             "Mode Get",             "Freq Set",
"Mode Set",             "VFO Swap A/B",         "VFO Swap M/S",             "VFO Equal AB",         "VFO Equal MS",         "VFO Dual Watch Off",
"VFO Dual Watch On",	"VFO Dual Watch",       "VFO Main Select",          "VFO Sub Select",       "VFO A Select",         "VFO B Select",
"VFO Main/Sub Band",    "Memory Mode",          "Memory Write",             "Memory to VFO",        "Memory Clear",         "Read Freq Offset",
"Send Freq Offset",		"Scanning",				"VFO Mode Select",          "Split/Duplex",         "Tuning Step",          "Attenuator Status",
"Antenna",          	"Speech",           	"AF Gain",                  "RF Gain",              "Squelch",              "APF Level",
"NR Level",  			"IF Shift",             "PBT Inner",                "PBT Outer",            "CW Pitch",             "RF Power",
"Mic Gain",             "Key Speed",			"Notch Filter",             "Compressor Level",     "Break-In Delay",       "NB Level",
"DIGI-SEL Shift",		"Drive Gain",			"Monitor Gain",             "Vox Gain",             "Anti-Vox Gain",        "Backlight Level",
"S Meter Sql Status",   "S Meter",				"Absolute Meter",           "Meter Type",
"Center Meter",         "Various Squelch",      "Overflow Status",          "Power Meter",
"SWR Meter",            "ALC Meter",            "Comp Meter",               "Vd Meter",             "Id Meter",             "Preamp Status",
"AGC Time Constant",    "Noise Blanker",        "Audio Peak Filter",        "Noise Reduction",      "Auto Notch",           "Repeater Tone",
"Repeater TSQL",        "Repeater DTCS",        "Repeater CSQL",            "Compressor Status",    "Monitor Status",       "Vox Status",
"Break-In Status",      "Manual Notch",         "DIGI-Sel Status",          "Twin Peak Filter",     "Dial Lock Status",     "RX Antenna",
"DSP IF Filter",        "Manual Notch Width",   "SSB TX Bandwidth",         "Main/Sub Tracking",    "Satellite Mode",       "DSQL Setting",
"Tone Squelch Type",    "IP Plus Status",       "Send CW",                  "Power Control",        "Transceiver ID",       "Filter Width",
"Memory Contents",      "Band Stacking Reg",    "Memory Keyer",             "IF Filter Width",      "Quick Dual Watch",     "Quick Split",
"Auto Repeater Mode",   "Tuner/ATU Status",     "Transverter Function",     "Transverter Offset",   "Lock Function",        "REF Adjust",
"REF Adjust Fine",      "ACC1 Mod Level",       "ACC2 Mod Level",           "USB Mod Level",        "LAN Mod Level",        "SPDIF Mod Level",
"Data Off Mod Input",   "DATA1 Mod Input",      "DATA2 Mod Input",          "DATA3 Mod Input",      "CIV Transceive",       "System Time",
"System Date",          "UTC Offset",           "CLOCK2 Setting",           "CLOCK2 UTC Offset",    "CLOCK 2 Name",         "Dash Ratio",
"Scanning Speed",       "Scanning Resume",      "Recorder Mode",            "Recorder TX",          "Recorder RX",          "Recorder Split",
"Recorder PTT Auto",    "Recorder Pre Rec",     "RX Ant Connector",         "Antenna Select Mode",  "NB Depth",             "NB Width",
"VOX Delay",            "VOX Voice Delay",      "APF Type",                 "APF Type Level",       "PSK Tone",             "RTTY Mark Tone",
"Data Mode Filter",     "AF Mute Status",       "Tone Frequency",           "TSQL Frequency",       "DTCS Code/Polarity",   "CSQL Code",
"Transceiver Status",   "XFC Status",           "Read TX Freq",             "CI-V Output",          "Read TX Freqs",        "Read User TX Freqs",
"User TX Band Edge Freq","RIT Frequency",       "RIT Status",               "RIT TX Status",        "Selected Freq",        "Unselected Freq",
"Selected Mode",        "Unselected Mode",      "RX Frequency",             "RX Mode",              "Scope Wave Data",      "Scope On/Off",
"Scope Data Output",    "Scope Main/Sub",       "Scope Single/Dual",        "Scope Mode",           "Scope Span",           "Scope Edge",
"Scope Hold",           "Scope Ref",            "Scope Speed",              "Scope VBW",            "Scope RBW",
"Scope During TX",      "Scope Center Type",    "Scope Fixed Edge Freq",    "Voice TX",             "Main/Sub Prefix",      "AFC Function",
"SSB RX HPFLPF",        "SSB RX Bass",          "SSB RX Treble",            "AM RX HPFLPF",         "AM RX Bass",           "AM RX Treble",
"FM RX HPFLPF",         "FM RX Bass",           "FM RX Treble",             "CW RX HPFLPF",         "CW RX Bass",           "CW RX Treble",
"SSB TX HPFLPF",        "SSB TX Bass",          "SSB TX Treble",            "AM TX HPFLPF",         "AM TX Bass",           "AM TX Treble",
"FM TX HPFLPF",         "FM TX Bass",           "FM TX Treble",             "Beep Level",           "Beep Level Limit",     "Beep Confirmation",
"Band Edge Beep",       "Beep Main Band",       "Beep Sub Band",            "RF SQL Control",       "TX Delay HF",          "TX Delay 50m",
"Timeout Timer",        "Timeout C-IV",
"GPS TX Mode",          "Satellite Memory",     "GPS Position",             "Memory Group",         "-Select VFO",          "-Seperator",
"-LCD Waterfall",       "-LCD Spectrum",        "-LCD Nothing",             "-Page Up",             "-Page Down",           "-VFO Frequency",
"-VFO Mode",            "-Rigctl Function",     "-Rigctl Level",            "-Rigctl Param",        "-RX Audio Data",       "-TX Audio Data",
"Command Error FA",     "Command OK FB"
};

struct spanType {
    spanType() {}
    spanType(int num, QString name, unsigned int freq) : num(num), name(name), freq(freq) {}
    int num;
    QString name;
    unsigned int freq;
};

struct funcType {
    funcType() {cmd=funcNone;}
    funcType(funcs cmd, QString name, QByteArray data, int minVal, int maxVal, bool cmd29, bool getCmd, bool setCmd) : cmd(cmd), name(name), data(data), minVal(minVal), maxVal(maxVal), cmd29(cmd29), getCmd(getCmd), setCmd(setCmd) {}
    funcs cmd;
    QString name;
    QByteArray data;
    int minVal;
    int maxVal;
    bool cmd29;
    bool getCmd;
    bool setCmd;
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
    quint16 tone=670;
    quint16 toneB=670;
    quint16 tsql=670;
    quint16 tsqlB=670;
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

enum audioType {qtAudio,portAudio,rtAudio,tciAudio};
enum codecType { LPCM, PCMU, OPUS };

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

#endif // WFVIEWTYPES_H
