#include "rigctld.h"
#include "logcategories.h"



static struct
{
    quint64 mode;
    rigMode_t mk;
    const char* str;
    uchar data=0;
} mode_str[] =
{
    { RIG_MODE_AM, modeAM, "AM" },
    { RIG_MODE_CW, modeCW,  "CW" },
    { RIG_MODE_USB, modeUSB, "USB" },
    { RIG_MODE_LSB, modeLSB, "LSB" },
    { RIG_MODE_RTTY, modeRTTY, "RTTY" },
    { RIG_MODE_FM, modeFM, "FM" },
    { RIG_MODE_WFM, modeWFM, "WFM" },
    { RIG_MODE_CWR, modeCW_R, "CWR" },
    { RIG_MODE_RTTYR, modeRTTY_R, "RTTYR" },
    { RIG_MODE_AMS, modeUnknown, "AMS" },
    { RIG_MODE_PKTLSB, modeLSB, "PKTLSB", 1U},
    { RIG_MODE_PKTUSB, modeUSB, "PKTUSB" , 1U},
    { RIG_MODE_PKTFM,  modeFM, "PKTFM", true},
    { RIG_MODE_PKTFMN, modeUnknown, "PKTFMN", 1U},
    { RIG_MODE_ECSSUSB, modeUnknown, "ECSSUSB" },
    { RIG_MODE_ECSSLSB, modeUnknown, "ECSSLSB" },
    { RIG_MODE_FAX, modeUnknown, "FAX" },
    { RIG_MODE_SAM, modeUnknown, "SAM" },
    { RIG_MODE_SAL, modeUnknown, "SAL" },
    { RIG_MODE_SAH, modeUnknown, "SAH" },
    { RIG_MODE_DSB, modeUnknown, "DSB"},
    { RIG_MODE_FMN, modeUnknown, "FMN" },
    { RIG_MODE_PKTAM, modeUnknown, "PKTAM"},
    { RIG_MODE_P25, modeP25, "P25"},
    { RIG_MODE_DSTAR, modeDV, "D-STAR"},
    { RIG_MODE_DPMR, modedPMR, "DPMR"},
    { RIG_MODE_NXDNVN, modeNXDN_VN, "NXDN-VN"},
    { RIG_MODE_NXDN_N, modeNXDN_N, "NXDN-N"},
    { RIG_MODE_DCR, modeUnknown, "DCR"},
    { RIG_MODE_AMN, modeUnknown, "AMN"},
    { RIG_MODE_PSK, modePSK, "PSK"},
    { RIG_MODE_PSKR, modePSK_R, "PSKR"},
    { RIG_MODE_C4FM, modeUnknown, "C4FM"},
    { RIG_MODE_SPEC, modeUnknown, "SPEC"},
    { RIG_MODE_NONE, modeUnknown, "" },
};


static const subCommandStruct levels_str[] =
{
    {"PREAMP",funcPreamp,typeUChar},
    {"ATT",funcAttenuator,typeUChar},
    {"VOXDELAY",funcVOXDelay,typeUChar},
    {"AF",funcAfGain,typeFloat},
    {"RF",funcRfGain,typeFloat},
    {"SQL",funcSquelch,typeFloat},
    {"IF",funcFilterWidth,typeFloat},
    {"APF",funcAPFLevel,typeFloat},
    {"NR",funcNRLevel,typeFloat},
    {"PBT_IN",funcPBTInner,typeFloat},
    {"PBT_OUT",funcPBTOuter,typeFloat},
    {"CWPITCH",funcCwPitch,typeFloat},
    {"RFPOWER",funcRFPower,typeFloat},
    {"MICGAIN",funcMicGain,typeFloat},
    {"KEYSPD",funcKeySpeed,typeUShort},
    {"NOTCHF",funcNotchFilter,typeUChar},
    {"COMP",funcCompressorLevel,typeFloat},
    {"AGC",funcAGCTimeConstant,typeUChar},
    {"BKINDL",funcBreakInDelay,typeFloat},
    {"BAL",funcNone,typeFloat},
    {"METER",funcNone,typeFloat},
    {"VOXGAIN",funcVoxGain,typeFloat},
    {"ANTIVOX",funcAntiVoxGain,typeFloat},
    {"SLOPE_LOW",funcNone,typeFloat},
    {"SLOPE_HIGH",funcNone,typeFloat},
    {"BKIN_DLYMS",funcBreakInDelay,typeFloat},
    {"RAWSTR",funcNone,typeFloat},
    {"SWR",funcSMeter,typeSWR},
    {"ALC",funcALCMeter,typeFloat},
    {"STRENGTH",funcSMeter,typeFloat},
    {"RFPOWER_METER",funcPowerMeter,typeFloat},
    {"COMPMETER",funcCompMeter,typeFloat},
    {"VD_METER",funcVdMeter,typeFloat},
    {"ID_METER",funcIdMeter,typeFloat},
    {"NOTCHF_RAW",funcNone,typeFloat},
    {"MONITOR_GAIN",funcMonitorGain,typeFloat},
    {"NQ",funcNone,typeFloat},
    {"RFPOWER_METER_WATT",funcNone,typeFloat},
    {"SPECTRUM_MDOE",funcNone,typeFloat},
    {"SPECTRUM_SPAN",funcNone,typeFloat},
    {"SPECTRUM_EDGE_LOW",funcNone,typeFloat},
    {"SPECTRUM_EDGE_HIGH",funcNone,typeFloat},
    {"SPECTRUM_SPEED",funcNone,typeFloat},
    {"SPECTRUM_REF",funcNone,typeFloat},
    {"SPECTRUM_AVG",funcNone,typeFloat},
    {"SPECTRUM_ATT",funcNone,typeFloat},
    {"TEMP_METER",funcNone,typeFloat},
    {"BAND_SELECT",funcNone,typeFloat},
    {"USB_AF",funcNone,typeFloat},
    {"",funcNone,'\x0'}
};

static const subCommandStruct functions_str[] =
{
    {"FAGC",funcNone,typeBinary},
    {"NB",funcNoiseBlanker,typeBinary},
    {"COMP",funcCompressor,typeBinary},
    {"VOX",funcVox,typeBinary},
    {"TONE",funcRepeaterTone,typeBinary},
    {"TQSL",funcRepeaterTSQL,typeBinary},
    {"SBKIN",funcBreakIn,typeUChar},
    {"FBKIN",funcBreakIn,typeUChar},
    {"ANF",funcAutoNotch,typeBinary},
    {"NR",funcNoiseReduction,typeBinary},
    {"AIP",funcNone,typeBinary},
    {"APF",funcAPFType,typeBinary},
    {"MON",funcMonitor,typeBinary},
    {"MN",funcManualNotch,typeBinary},
    {"RF",funcNone,typeBinary},
    {"ARO",funcNone,typeBinary},
    {"LOCK",funcLockFunction,typeBinary},
    {"MUTE",funcAFMute,typeBinary},
    {"VSC",funcNone,typeBinary},
    {"REV",funcNone,typeBinary},
    {"SQL",funcSquelch,typeBinary}, // Actually an integer as ICOM doesn't provide on/off for squelch
    {"ABM",funcNone,typeBinary},
    {"VSC",funcNone,typeBinary},
    {"REV",funcNone,typeBinary},
    {"BC",funcNone,typeBinary},
    {"MBC",funcNone,typeBinary},
    {"RIT",funcRitStatus,typeBinary},
    {"AFC",funcNone,typeBinary},
    {"SATMODE",funcSatelliteMode,typeBinary},
    {"SCOPE",funcScopeOnOff,typeBinary},
    {"RESUME",funcNone,typeBinary},
    {"TBURST",funcNone,typeBinary},
    {"TUNER",funcTunerStatus,typeBinary},
    {"XIT",funcNone,typeBinary},
    {"",funcNone,typeBinary}
};

static const subCommandStruct params_str[] =
{
    {"ANN",funcNone,typeUChar},
    {"APO",funcNone,typeUChar},
    {"BACKLIGHT",funcBackLightLevel,typeUChar},
    {"BEEP",funcNone,typeUChar},
    {"TIME",funcTime,typeUChar},
    {"BAT",funcNone,typeUChar},
    {"KEYLIGHT",funcNone,typeUChar},
    {"",funcNone,typeNone}
};

#define ARG_IN1  0x01
#define ARG_OUT1 0x02
#define ARG_IN2  0x04
#define ARG_OUT2 0x08
#define ARG_IN3  0x10
#define ARG_OUT3 0x20
#define ARG_IN4  0x40
#define ARG_OUT4 0x80
#define ARG_OUT5 0x100
#define ARG_NONE    0
#define ARG_IN  (ARG_IN1|ARG_IN2|ARG_IN3|ARG_IN4)
#define ARG_OUT  (ARG_OUT1|ARG_OUT2|ARG_OUT3|ARG_OUT4)
#define ARG_IN_LINE 0x4000
#define ARG_NOVFO 0x8000
#define MAXNAMESIZE 32


static const commandStruct commands_list[] =
{
    { 'F',  "set_freq",         funcFreqSet,       typeFreq,    ARG_IN, "Frequency" },
    { 'f',  "get_freq",         funcFreqGet,       typeFreq,    ARG_OUT, "Frequency" },
    { 'M',  "set_mode",         funcModeSet,            typeMode,     ARG_IN, "Mode", "Passband" },
    { 'm',  "get_mode",         funcModeGet,            typeMode,     ARG_OUT, "Mode", "Passband" },
    { 'I',  "set_split_freq",   funcFreqSet,     typeFreq,     ARG_IN, "TX Frequency" },
    { 'i',  "get_split_freq",   funcFreqGet,     typeFreq,     ARG_OUT, "TX Frequency" },
    { 'X',  "set_split_mode",   funcSplitStatus,        typeMode,     ARG_IN, "TX Mode", "TX Passband" },
    { 'x',  "get_split_mode",   funcSplitStatus,        typeMode,     ARG_OUT, "TX Mode", "TX Passband" },
    { 'K',  "set_split_freq_mode",  funcModeSet,           typeFreq,     ARG_IN,    "TX Frequency", "TX Mode", "TX Passband" },
    { 'k',  "get_split_freq_mode",  funcModeGet,           typeFreq,     ARG_OUT,   "TX Frequency", "TX Mode", "TX Passband" },
    { 'S',  "set_split_vfo",    funcSplitStatus,        typeSplitVFO, ARG_IN, "Split", "TX VFO" },
    { 's',  "get_split_vfo",    funcSplitStatus,        typeSplitVFO, ARG_OUT, "Split", "TX VFO" },
    { 'N',  "set_ts",           funcTuningStep,         typeUChar,    ARG_IN, "Tuning Step" },
    { 'n',  "get_ts",           funcTuningStep,         typeUChar,    ARG_OUT, "Tuning Step" },
    { 'L',  "set_level",        funcRigctlLevel,        typeLevel,    ARG_IN, "Level", "Level Value" },
    { 'l',  "get_level",        funcRigctlLevel,        typeLevel,    ARG_IN1 | ARG_OUT2, "Level", "Level Value" },
    { 'U',  "set_func",         funcRigctlFunction,     typeLevel,    ARG_IN, "Func", "Func Status" },
    { 'u',  "get_func",         funcRigctlFunction,     typeLevel,    ARG_IN1 | ARG_OUT2, "Func", "Func Status" },
    { 'P',  "set_parm",         funcRigctlParam,        typeLevel,    ARG_IN1 | ARG_NOVFO, "Parm", "Parm Value" },
    { 'p',  "get_parm",         funcRigctlParam,        typeLevel,    ARG_IN1 | ARG_OUT2, "Parm", "Parm Value" },
    { 'G',  "vfo_op",           funcSelectVFO,          typeUChar,    ARG_IN, "Mem/VFO Op" },
    { 'g',  "scan",             funcScanning,           typeUChar,    ARG_IN, "Scan Fct", "Scan Channel" },
    { 'A',  "set_trn",          funcCIVTransceive,      typeUChar,    ARG_IN  | ARG_NOVFO, "Transceive" },
    { 'a',  "get_trn",          funcCIVTransceive,      typeUChar,    ARG_OUT | ARG_NOVFO, "Transceive" },
    { 'R',  "set_rptr_shift",   funcSendFreqOffset,     typeUChar,    ARG_IN, "Rptr Shift" },
    { 'r',  "get_rptr_shift",   funcReadFreqOffset,     typeUChar,    ARG_OUT, "Rptr Shift" },
    { 'O',  "set_rptr_offs",    funcSendFreqOffset,     typeUChar,    ARG_IN, "Rptr Offset" },
    { 'o',  "get_rptr_offs",    funcReadFreqOffset,     typeUChar,    ARG_OUT, "Rptr Offset" },
    { 'C',  "set_ctcss_tone",   funcToneFreq,           typeUChar,    ARG_IN, "CTCSS Tone" },
    { 'c',  "get_ctcss_tone",   funcToneFreq,           typeUChar,    ARG_OUT, "CTCSS Tone" },
    { 'D',  "set_dcs_code",     funcDTCSCode,           typeUChar,    ARG_IN, "DCS Code" },
    { 'd',  "get_dcs_code",     funcDTCSCode,           typeUChar,    ARG_OUT, "DCS Code" },
    { 0x90, "set_ctcss_sql",    funcTSQLFreq,           typeUChar,    ARG_IN, "CTCSS Sql" },
    { 0x91, "get_ctcss_sql",    funcTSQLFreq,           typeUChar,    ARG_OUT, "CTCSS Sql" },
    { 0x92, "set_dcs_sql",      funcCSQLCode,           typeUChar,    ARG_IN, "DCS Sql" },
    { 0x93, "get_dcs_sql",      funcCSQLCode,           typeUChar,    ARG_OUT, "DCS Sql" },
    { 'V',  "set_vfo",          funcSelectVFO,          typeVFO,    ARG_IN  | ARG_NOVFO, "VFO" },
    { 'v',  "get_vfo",          funcSelectVFO,          typeVFO,    ARG_NOVFO | ARG_OUT, "VFO" },
    { 'T',  "set_ptt",          funcTransceiverStatus,  typePTT,    ARG_IN, "VFO", "PTT" },
    { 't',  "get_ptt",          funcTransceiverStatus,  typePTT,    ARG_OUT, "VFO", "PTT" },
    { 'E',  "set_mem",          funcMemoryContents,     typeMode,    ARG_IN, "Memory#" },
    { 'e',  "get_mem",          funcMemoryContents,     typeMode,    ARG_OUT, "Memory#" },
    { 'H',  "set_channel",      funcMemoryMode,         typeUChar,    ARG_IN  | ARG_NOVFO, "Channel"},
    { 'h',  "get_channel",      funcMemoryMode,         typeUChar,    ARG_IN  | ARG_NOVFO, "Channel", "Read Only" },
    { 'B',  "set_bank",         funcMemoryGroup,        typeUChar,    ARG_IN, "Bank" },
    { '_',  "get_info",         funcNone,               typeUChar,    ARG_OUT | ARG_NOVFO, "Info" },
    { 'J',  "set_rit",          funcRITFreq,            typeShort,    ARG_IN, "RIT" },
    { 'j',  "get_rit",          funcRITFreq,            typeShort,    ARG_OUT, "RIT" },
    { 'Z',  "set_xit",          funcNone,               typeShort,    ARG_IN, "XIT" },
    { 'z',  "get_xit",          funcNone,               typeShort,    ARG_OUT, "XIT" },
    { 'Y',  "set_ant",          funcAntenna,            typeUChar,    ARG_IN, "Antenna", "Option" },
    { 'y',  "get_ant",          funcAntenna,            typeUChar,    ARG_IN1 | ARG_OUT2 | ARG_NOVFO, "AntCurr", "Option", "AntTx", "AntRx" },
    { 0x87, "set_powerstat",    funcPowerControl,       typeBinary,    ARG_IN  | ARG_NOVFO, "Power Status" },
    { 0x88, "get_powerstat",    funcPowerControl,       typeBinary,    ARG_OUT | ARG_NOVFO, "Power Status" },
    { 0x89, "send_dtmf",        funcNone,               typeUChar,    ARG_IN, "Digits" },
    { 0x8a, "recv_dtmf",        funcNone,               typeUChar,    ARG_OUT, "Digits" },
    { '*',  "reset",            funcNone,               typeUChar,    ARG_IN, "Reset" },
    { 'w',  "send_cmd",         funcNone,               typeUChar,    ARG_IN1 | ARG_IN_LINE | ARG_OUT2 | ARG_NOVFO, "Cmd", "Reply" },
    { 'W',  "send_cmd_rx",      funcNone,               typeUChar,    ARG_IN | ARG_OUT2 | ARG_NOVFO, "Cmd", "Reply"},
    { 'b',  "send_morse",       funcSendCW,             typeString,   ARG_IN | ARG_NOVFO  | ARG_IN_LINE, "Morse" },
    { 0xbb, "stop_morse",       funcSendCW,             typeString,   },
    { 0xbc, "wait_morse",       funcSendCW,             typeUChar,    },
    { 0x94, "send_voice_mem",   funcNone,               typeUChar,    ARG_IN, "Voice Mem#" },
    { 0x8b, "get_dcd",          funcNone,               typeUChar,    ARG_OUT, "DCD" },
    { 0x8d, "set_twiddle",      funcNone,               typeUChar,    ARG_IN  | ARG_NOVFO, "Timeout (secs)" },
    { 0x8e, "get_twiddle",      funcNone,               typeUChar,    ARG_OUT | ARG_NOVFO, "Timeout (secs)" },
    { 0x97, "uplink",           funcNone,               typeUChar,    ARG_IN | ARG_NOVFO, "1=Sub, 2=Main" },
    { 0x95, "set_cache",        funcNone,               typeUChar,    ARG_IN | ARG_NOVFO, "Timeout (msecs)" },
    { 0x96, "get_cache",        funcNone,               typeUChar,    ARG_OUT | ARG_NOVFO, "Timeout (msecs)" },
    { '2',  "power2mW",         funcNone,               typeUChar,    ARG_IN1 | ARG_IN2 | ARG_IN3 | ARG_OUT1 | ARG_NOVFO, "Power [0.0..1.0]", "Frequency", "Mode", "Power mW" },
    { '4',  "mW2power",         funcNone,               typeUChar,    ARG_IN1 | ARG_IN2 | ARG_IN3 | ARG_OUT1 | ARG_NOVFO, "Pwr mW", "Freq", "Mode", "Power [0.0..1.0]" },
    { '1',  "dump_caps",        funcNone,               typeUChar,    ARG_NOVFO },
    { '3',  "dump_conf",        funcNone,               typeUChar,    ARG_NOVFO },
    { 0x8f, "dump_state",       funcNone,               typeUChar,    ARG_OUT | ARG_NOVFO },
    { 0xf0, "chk_vfo",          funcSelectVFO,          typeUChar,    ARG_NOVFO, "ChkVFO" },   /* rigctld only--check for VFO mode */
    { 0xf2, "set_vfo_opt",      funcNone,               typeUChar,    ARG_NOVFO | ARG_IN, "Status" }, /* turn vfo option on/off */
    { 0xf3, "get_vfo_info",     funcSelectVFO,          typeVFOInfo,    ARG_IN1 | ARG_NOVFO | ARG_OUT5, "VFO", "Freq", "Mode", "Width", "Split", "SatMode" }, /* get several vfo parameters at once */
    { 0xf5, "get_rig_info",     funcNone,               typeUChar,    ARG_NOVFO | ARG_OUT, "RigInfo" }, /* get several vfo parameters at once */
    { 0xf4, "get_vfo_list",     funcNone,               typeUChar,    ARG_OUT | ARG_NOVFO, "VFOs" },
    { 0xf6, "get_modes",        funcNone,               typeUChar,    ARG_OUT | ARG_NOVFO, "Modes" },
    { 0xf9, "get_clock",        funcTime,               typeUChar,    ARG_NOVFO },
    { 0xf8, "set_clock",        funcTime,               typeUChar,    ARG_IN | ARG_NOVFO, "YYYYMMDDHHMMSS.sss+ZZ" },
    { 0xf1, "halt",             funcNone,               typeUChar,    ARG_NOVFO },   /* rigctld only--halt the daemon */
    { 0x8c, "pause",            funcNone,               typeUChar,    ARG_IN, "Seconds" },
    { 0x98, "password",         funcNone,               typeUChar,    ARG_IN | ARG_NOVFO, "Password" },
    { 0xf7, "get_mode_bandwidths", funcNone,            typeUChar,    ARG_IN | ARG_NOVFO, "Mode" },
    { 0xa0, "set_separator",     funcSeparator,         typeUChar,    ARG_IN | ARG_NOVFO, "Separator" },
    { 0xa1, "get_separator",     funcSeparator,         typeUChar,    ARG_NOVFO, "Separator" },
    { 0xa2, "set_lock_mode",     funcLockFunction,      typeUChar,    ARG_IN | ARG_NOVFO, "Locked" },
    { 0xa3, "get_lock_mode",     funcLockFunction,      typeUChar,    ARG_NOVFO, "Locked" },
    { 0xa4, "send_raw",          funcNone,              typeUChar,    ARG_NOVFO | ARG_IN1 | ARG_IN2 | ARG_OUT3, "Terminator", "Command", "Send raw answer" },
    { 0xa5, "client_version",    funcNone,              typeUChar,    ARG_NOVFO | ARG_IN1, "Version", "Client version" },
    { 0x00, "", funcNone, typeNone},
};




rigCtlD::rigCtlD(QObject* parent) :
    QTcpServer(parent)
{
}

rigCtlD::~rigCtlD()
{
    qInfo(logRigCtlD()) << "closing rigctld";
}


int rigCtlD::startServer(qint16 port)
{
    if (!this->listen(QHostAddress::Any, port)) {
        qInfo(logRigCtlD()) << "could not start on port " << port;
        return -1;
    }
    else
    {
        qInfo(logRigCtlD()) << "started on port " << port;
    }

    return 0;
}

void rigCtlD::incomingConnection(qintptr socket) {
    rigCtlClient* client = new rigCtlClient(socket, this);
    connect(this, SIGNAL(onStopped()), client, SLOT(closeSocket()));
}


void rigCtlD::stopServer()
{
    qInfo(logRigCtlD()) << "stopping server";
    emit onStopped();
}

void rigCtlClient::receiveRigCaps(rigCapabilities* caps)
{
    if (caps != Q_NULLPTR) {
        qInfo(logRigCtlD()) << "Got rigcaps for:" << caps->modelName;
    } else
    {
        qInfo(logRigCtlD()) << "Rig has gone away, close connection now!";
        closeSocket();
    }
    this->rigCaps = caps;
}

rigCtlClient::rigCtlClient(int socketId, rigCtlD* parent) : QObject(parent)
{

    this->setObjectName("RigCtlD");
    queue = cachingQueue::getInstance();
    connect(queue, SIGNAL(rigCapsUpdated(rigCapabilities*)), this, SLOT(receiveRigCaps(rigCapabilities*)));
    rigCaps = queue->getRigCaps();
    commandBuffer.clear();
    sessionId = socketId;
    socket = new QTcpSocket(this);
    this->parent = parent;
    if (!socket->setSocketDescriptor(sessionId))
    {
        qInfo(logRigCtlD()) << " error binding socket: " << sessionId;
        return;
    }
    connect(socket, SIGNAL(readyRead()), this, SLOT(socketReadyRead()), Qt::DirectConnection);
    connect(socket, SIGNAL(disconnected()), this, SLOT(socketDisconnected()), Qt::DirectConnection);
    connect(parent, SIGNAL(sendData(QString)), this, SLOT(sendData(QString)), Qt::DirectConnection);
    qInfo(logRigCtlD()) << " session connected: " << sessionId;

    currentVfo = vfoUnknown;

    // Find what VFOs we have:

    if (rigCaps->numVFO > 0 && rigCaps->commands.contains(funcVFOASelect))
        vfoList |= 1<<0;
    if (rigCaps->numVFO > 1 && rigCaps->commands.contains(funcVFOBSelect))
        vfoList |= 1<<1;
    if (rigCaps->numReceiver > 0 && rigCaps->commands.contains(funcVFOMainSelect))
        vfoList |= 1<<26;
    if (rigCaps->numReceiver > 1 && rigCaps->commands.contains(funcVFOSubSelect))
        vfoList |= 1<<25;
    if (rigCaps->commands.contains(funcMemoryMode))
        vfoList |= 1<<28;

    swrCal = ICOM_SWR_CAL;
}

void rigCtlClient::socketReadyRead()
{

    if (this->rigCaps == Q_NULLPTR) {
        qWarning(logRigCtlD) << "Rig has gone away, closing connection";
        closeSocket();
        return;
    }

    QByteArray data = socket->readAll();
    commandBuffer.append(data);
    QStringList commandList(commandBuffer.split('\n'));
    QString sep = "\n";

    int ret = -RIG_EINVAL;

    bool setCommand = false;
    bool longCommand = false;
    bool extended = false;
    QStringList response;

    for (QString &commands : commandList)
    {
        restart:

        if (commands.endsWith('\r'))
        {
            commands.chop(1); // Remove \n character
        }

        if (commands.isEmpty())
        {
            continue;
        }

        // We have a full line so process command.

        qDebug(logRigCtlD) << sessionId  << "RX:" << commands;

        if (commands[0] == ';' || commands[0] == '|' || commands[0] == ',')
        {
            sep = commands[0].toLatin1();
            commands.remove(0,1);
            extended=true;
        }
        else if (commands[0] == '+')
        {
            //qDebug(logRigCtlD) << "Extended response requested for this command";
            extended = true;
            sep = "\n";
            commands.remove(0,1);
        }
        else if (commands[0] == '#')
        {
            continue;
        }
        else if (commands[0].toLower() == 'q')
        {
            closeSocket();
            return;
        }


        if (commands[0] == '\\')
        {
            commands.remove(0,1);
            //qDebug(logRigCtlD) << "This is a long form command";
            longCommand = true;
        }

        //qDebug(logRigCtlD) << "Got command to parse:" << commands;

        QStringList command = commands.split(" ");
        command.removeAll({}); // Remove any empty strings (double-whitespace issue)
        for (int i=0; commands_list[i].sstr != 0x00; i++)
        {
            if ((longCommand && !strncmp(command[0].toLocal8Bit(), commands_list[i].str,MAXNAMESIZE)) ||
                   (!longCommand && !commands.isNull() && commands[0].toLatin1() == commands_list[i].sstr))
            {
                command.removeFirst(); // Remove the actual command so it only contains params

                if (extended){
                    // First we need to repeat the original command back:
                    QString repeat=QString("%1:").arg(commands_list[i].str);
                    for( const auto &c: command)
                    {
                        repeat+=QString(" %0").arg(c);
                    }
                    response.append(repeat);
                }

                if (((commands_list[i].flags & ARG_IN) == ARG_IN))
                {
                    // For debugging only comment next line M0VSE
                    //qInfo(logRigCtlD()) << "Received set command" << commands;
                    setCommand=true;
                }

                if (commands_list[i].func == funcRigctlFunction)
                {
                    ret = getSubCommand(response, extended, commands_list[i], functions_str, command);
                    commands.clear();
                    break;
                } else if (commands_list[i].func == funcRigctlLevel)
                {
                    ret = getSubCommand(response, extended, commands_list[i], levels_str, command);
                    commands.clear();
                    break;
                } else if (commands_list[i].func == funcRigctlParam)
                {
                    ret = getSubCommand(response, extended, commands_list[i], params_str, command);
                    commands.clear();
                    break;
                } else if (commands_list[i].sstr == 0x8f)
                {
                    ret = dumpState(response, extended);
                    break;
                } else if (commands_list[i].sstr == '1')
                {
                    ret = dumpCaps(response, extended);
                    setCommand=true;
                    break;
                }
                else if (commands_list[i].sstr == '2')
                {
                    // power2mW
                    ret = power2mW(response, extended, commands_list[i], command);
                    setCommand=true;
                    break;
                }
                else if (commands_list[i].sstr == '3')
                {
                    response.append(QString("Model: WFVIEW(%0)").arg(rigCaps->modelName));
                    ret = RIG_OK;
                    break;

                }
                else if (commands_list[i].sstr == '4')
                {
                    // mW2power
                    ret = mW2power(response, extended, commands_list[i], command);
                    setCommand=true;
                    break;
                }
                else if (commands_list[i].sstr == 0xf0)
                {
                    chkVfoEecuted = true;
                    response.append(QString("CHKVFO %0").arg(uchar(1)));
                    ret = RIG_OK;
                    break;
                }
                // Special commands are funcNone so will not get called here
                if (commands_list[i].func != funcNone) {
                    ret = getCommand(response, extended, commands_list[i],command);
                }
                break;
            }
        }

        if (setCommand)
        {
            break;  // Cannot be a compound command so just output result.
        }

        if (!longCommand && !commands.isEmpty()){
            commands.remove(0,1);
            goto restart; // Need to restart loop without going to next command incase of compound command
        }

        sep = "\n";

    }

    // We have finished parsing all commands and have a response to send (hopefully)
    commandBuffer.clear();
    for (int i = 0;i<response.size();i++)
    {
        if (!response[i].isEmpty()) {
            sendData(QString("%0%1").arg(response[i],(i<response.size()-1)?sep:""));
        }
    }

    if (ret < 0 || setCommand || extended)
        sendData(QString("%0RPRT %1\n").arg(response.isEmpty()?"":sep).arg(QString::number(ret)));
    else if (sep == '\n')
        sendData("\n");

}

void rigCtlClient::socketDisconnected()
{
    qInfo(logRigCtlD()) << sessionId << "disconnected";
    socket->deleteLater();
    this->deleteLater();
}

void rigCtlClient::closeSocket()
{
    socket->close();
}

void rigCtlClient::sendData(QString data)
{
    qDebug(logRigCtlD()) << sessionId << "TX:" << data;
    if (socket != Q_NULLPTR && socket->isValid() && socket->isOpen())
    {
        socket->write(data.toLatin1());
    }
    else
    {
        qInfo(logRigCtlD()) << "socket not open!";
    }
}

QString rigCtlClient::getFilter(quint8 mode, quint8 filter) {
    
    if (mode == 3 || mode == 7 || mode == 12 || mode == 17) {
        switch (filter) {
        case 1:
            return QString("1200");
        case 2:
            return QString("500");
        case 3:
            return QString("250");
        }
    }
    else if (mode == 4 || mode == 8)
    {
        switch (filter) {
        case 1:
            return QString("2400");
        case 2:
            return QString("500");
        case 3:
            return QString("250");
        }
    }
    else if (mode == 2)
    {
        switch (filter) {
        case 1:
            return QString("9000");
        case 2:
            return QString("6000");
        case 3:
            return QString("3000");
        }
    }
    else if (mode == 5)
    {
        switch (filter) {
        case 1:
            return QString("15000");
        case 2:
            return QString("10000");
        case 3:
            return QString("7000");
        }
    }
    else { // SSB or unknown mode
        switch (filter) {
        case 1:
            return QString("3000");
        case 2:
            return QString("2400");
        case 3:
            return QString("1800");
        }
    }
    return QString("");
}

QString rigCtlClient::getMode(modeInfo mode)
{
    for (int i = 0; mode_str[i].str[0] != '\0'; i++)
    {
        if (mode_str[i].mk == mode.mk && mode_str[i].data == mode.data)
        {
            return(QString(mode_str[i].str));
        }
    }
    return("UNKNOWN");
}

bool rigCtlClient::getMode(QString modeString, modeInfo& mode)
{
    //qInfo() << "Looking for mode (set)" << modeString;
    for (int i = 0; mode_str[i].str[0] != '\0'; i++)
    {
        if (QString(mode_str[i].str) == modeString)
        {
            for (auto &m: rigCaps->modes)
            {
                if (m.mk == mode_str[i].mk)
                {
                    mode = modeInfo(m);
                    mode.data = mode_str[i].data;
                    mode.filter = 1;
                    return true;
                }
            }
        }
    }
    return false;
}


quint8 rigCtlClient::getAntennas()
{
    quint8 ant=0;
    for (auto &i: rigCaps->antennas)
    {
        ant |= 1<<i.num;
    }
    return ant;
}

quint64 rigCtlClient::getRadioModes(QString md) 
{
    quint64 modes = 0;
    for (auto&& mode : rigCaps->modes)
    {
        for (int i = 0; mode_str[i].str[0] != '\0'; i++)
        {
            QString mstr = QString(mode_str[i].str);
            if (rigCaps->hasDataModes) {
                if (mstr.contains(mode.name))
                {
                    // qDebug(logRigCtlD()) << "Found data mode:" << mode.name << mode_str[i].mode;
                    if (md.isEmpty() || mstr.contains(md)) {
                        modes |= mode_str[i].mode;
                    }
                }
            }
            else if (mode.name==mstr)
            {
                //qDebug(logRigCtlD()) << "Found mode:" << mode.name << mode_str[i].mode;
                if (md.isEmpty() || mstr==md) 
                {
                    modes |= mode_str[i].mode;
                } 
            }
        }
    }
    return modes;
}

QString rigCtlClient::getAntName(quint8 ant)
{
    QString ret;
    switch (ant)
    {
        case 0: ret = "ANT1"; break;
        case 1: ret = "ANT2"; break;
        case 2: ret = "ANT3"; break;
        case 3: ret = "ANT4"; break;
        case 4: ret = "ANT5"; break;
        case 30: ret = "ANT_UNKNOWN"; break;
        case 31: ret = "ANT_CURR"; break;
        default: ret = "ANT_UNK"; break;
    }
    return ret;
}

quint8 rigCtlClient::antFromName(QString name) {
    quint8 ret;

    if (name.toUpper() == "ANT1")
        ret = 0;
    else if (name.toUpper() == "ANT2")
        ret = 1;
    else if (name.toUpper() == "ANT3")
        ret = 2;
    else if (name.toUpper() == "ANT4")
        ret = 3;
    else if (name.toUpper() == "ANT5")
        ret = 4;
    else if (name.toUpper() == "ANT_UNKNOWN")
        ret = 30;
    else if (name.toUpper() == "ANT_CURR")
        ret = 31;
    else  
        ret = 99;
    return ret;
}

vfo_t rigCtlClient::vfoFromName(QString vfo) {

    vfo_t v = vfoUnknown;
    if (vfo.toUpper() == "VFOA")
    {
        if (rigCaps->commands.contains(funcVFOASelect))
            v = vfoA;
        else if (rigCaps->commands.contains(funcVFOMainSelect))
            v = vfoMain;
        else
            v = vfoCurrent;
    }
    else if (vfo.toUpper() == "VFOB")
    {
        if (rigCaps->commands.contains(funcVFOBSelect))
            v = vfoB;
        else if (rigCaps->commands.contains(funcVFOSubSelect))
            v = vfoSub;
        else
            v = vfoCurrent;
    }
    else if (vfo.toUpper() == "MAIN")
    {
        if (rigCaps->commands.contains(funcVFOMainSelect))
            v = vfoMain;
        else if (rigCaps->commands.contains(funcVFOASelect))
            v = vfoA;
        else
            v = vfoCurrent;
    }
    else if (vfo.toUpper() == "SUB")
    {
        if (rigCaps->commands.contains(funcVFOSubSelect))
            v = vfoSub;
        else if (rigCaps->commands.contains(funcVFOBSelect))
            v = vfoB;
        else
            v = vfoCurrent;
    }
    else if (vfo.toUpper() == "MEM")
    {
        v = vfoMem;
    }

    //qInfo() << "vfoFromName" << vfo << "returns" << v;
    return v;
}

QString rigCtlClient::getVfoName(vfo_t vfo)
{
    QString ret;
    switch (vfo) {
    case vfoMain:
        ret = "Main";
        break;
    case vfoSub:
        ret = "Sub";
        break;
    case vfoA:
        ret = "VFOA";
        break;
    case vfoB:
        ret = "VFOB";
        break;
    case vfoCurrent:
        ret = "currVFO";
        break;
    case vfoMem:
        ret = "MEM";
        break;
    default:
        ret = "None";
        break;
    }

    //qInfo() << "vfoName" << vfo << "returns" << ret;

    return ret;
}

int rigCtlClient::getCalibratedValue(quint8 meter,cal_table_t cal) {
    
    int interp;

    int i = 0;
    for (i = 0; i < cal.size; i++) {
        if (meter < cal.table[i].raw)
        {
            break;
        }
    }

    if (i == 0)
    {
        return cal.table[0].val;
    } 
    else if (i >= cal.size)
    {
        return cal.table[i - 1].val;
    } 
    else if (cal.table[i].raw == cal.table[i - 1].raw)
    {
        return cal.table[i].val;
    }

    interp = ((cal.table[i].raw - meter)
        * (cal.table[i].val - cal.table[i - 1].val))
        / (cal.table[i].raw - cal.table[i - 1].raw);

    return cal.table[i].val - interp;
}

unsigned long rigCtlClient::doCrc(quint8* p, size_t n)
{
    unsigned long crc = 0xfffffffful;
    size_t i;

    if (crcTable[0] == 0) { genCrc(crcTable); }

    for (i = 0; i < n; i++)
    {
        crc = crcTable[*p++ ^ (crc & 0xff)] ^ (crc >> 8);
    }

    return ((~crc) & 0xffffffff);
}

void rigCtlClient::genCrc(unsigned long crcTable[])
{
    unsigned long POLYNOMIAL = 0xEDB88320;
    quint8 b = 0;

    while (0 != ++b)
    {
        unsigned long remainder = b;
        unsigned long bit;
        for (bit = 8; bit > 0; --bit)
        {
            if (remainder & 1)
            {
                remainder = (remainder >> 1) ^ POLYNOMIAL;
            }
            else
            {
                remainder = (remainder >> 1);
            }
        }
        crcTable[(size_t)b] = remainder;
    }
}

int rigCtlClient::getCommand(QStringList& response, bool extended, const commandStruct cmd, QStringList params )
{
    // This is a main command
    int ret = -RIG_EINVAL;
    funcs func = cmd.func;

    if (rigCaps == Q_NULLPTR)
        return ret;

    if (((cmd.flags & ARG_IN) == ARG_IN) && params.size())
    {
        // We are expecting a second argument to the command as it is a set
        QVariant val;
        ret = RIG_OK;
        switch (cmd.type)
        {
        case typeUChar:
            if (cmd.func == funcLockFunction)
                val.setValue(modeLock);
            else
                val.setValue(static_cast<uchar>(params[0].toInt()));
            break;
        case typeShort:
            val.setValue(static_cast<short>(params[0].toInt()));
            break;
        case typeUShort:
            val.setValue(static_cast<ushort>(params[0].toInt()));
            break;
        case typeFloat:
            val.setValue(static_cast<ushort>(float(params[0].toFloat() * 255.0)));  // rigctl sends 0.0-1.0, we expect 0-255
            break;
        case typeFloatDiv:
            val.setValue(static_cast<uchar>(float(params[0].toFloat() / 10.0)));
            break;
        case typeFloatDiv5:
            val.setValue(static_cast<ushort>(float(params[0].toFloat() * 5.1)));
            break;
        case typePTT:
            if (params.length()>1){
                queue->add(priorityImmediate, queueItem(funcSelectVFO, QVariant::fromValue<vfo_t>(vfoFromName(params[0])),false,currentRx));
                val.setValue(static_cast<bool>(params[1].toInt()));
            } else {
                val.setValue(static_cast<bool>(params[0].toInt()));
            }

        case typeBinary:
            val.setValue(static_cast<bool>(params[0].toInt()));
            break;
        case typeFreq:
        {
            freqt f;
            currentRx=0;
            if (cmd.sstr == 'I') {
                func = getFreqFunc(splitVfo,true);
                if (splitVfo == vfoSub && rigCaps->numReceiver > 1)
                    currentRx=1;
            } else {
                func = getFreqFunc(currentVfo,true);
            }

            //queue->add(priorityImmediate, queueItem(funcSelectVFO, QVariant::fromValue<vfo_t>(currentVfo),false,currentRx));

            f.Hz = static_cast<quint64>(params[0].toFloat());
            f.VFO = activeVFO;
            f.MHzDouble = f.Hz/1000000.0;
            val.setValue(f);
            break;
        }
        case typeSplitVFO:

            val.setValue(static_cast<bool>(params[0].toInt()));
            if (params.size() > 1) {// Just in case we have invalid info
                //qInfo() << "Split VFO" << params[0] << "VFO" << params[1];
                splitVfo = vfoFromName(params[1]);
            }
            break;

        case typeMode:
        {
            currentRx=0;
            if (cmd.sstr == 'X') {
                func = getModeFunc(splitVfo,true);
                if (splitVfo == vfoSub && rigCaps->numReceiver > 1)
                    currentRx=1;
            } else {
                func = getModeFunc(currentVfo,true);
            }
            //queue->add(priorityImmediate, queueItem(funcSelectVFO, QVariant::fromValue<vfo_t>(currentVfo),false,currentRx));

            modeInfo mi;
            if (getMode(params[0],mi))
            {
                val.setValue(mi);
            } else {
                qInfo(logRigCtlD()) << "Mode not found:" << params[0];
                return -RIG_EINVAL;
            }
            if (params.size() > 1) {
                // We have a passband as well
                bool ok;
                int pb = params[1].toInt(&ok);
                if (ok && pb>0) {
                    queue->add(priorityImmediate, queueItem(funcFilterWidth, QVariant::fromValue<ushort>(pb),false,currentRx));
                }
            }
            break;
        }
        case typeVFO:
            // Setting VFO:
            if (params[0] == "?") {
                // This is a query for a list of values
                QString vfo;
                if (rigCaps->numVFO > 0 && rigCaps->commands.contains(funcVFOASelect))
                    vfo.append("VFOA ");
                if (rigCaps->numVFO > 1 && rigCaps->commands.contains(funcVFOASelect))
                    vfo.append("VFOB ");
                if (rigCaps->numReceiver > 0 && rigCaps->commands.contains(funcVFOMainSelect))
                    vfo.append("Main ");
                if (rigCaps->numReceiver > 1 && rigCaps->commands.contains(funcVFOSubSelect))
                    vfo.append("Sub ");
                if (rigCaps->commands.contains(funcMemoryMode))
                    vfo.append("MEM ");
                vfo.chop(1);
                response.append(vfo);
            }
            else
            {
                currentVfo = vfoFromName(params[0]);
                val.setValue(currentVfo);
                if (currentVfo == vfoSub && rigCaps->numReceiver > 1)
                    currentRx = 1;
                else
                    currentRx = 0;
            }
            break;
        case typeString:
        {
            // Only used for CW?
            QString sendCmd;
            for (QString &cmd : params) {
                sendCmd =sendCmd+cmd+" ";
            }
            sendCmd.chop(1);
            val.setValue(sendCmd);
            break;
        }
        default:
            qInfo(logRigCtlD()) << "Unable to parse value of type" << cmd.type << "Command" << cmd.str;
            return -RIG_EINVAL;
        }

        queue->add(priorityImmediate, queueItem(func, val,false,currentRx));

    } else {
        // Simple get command
        cacheItem item;

        // Build QStringList of Prefixes. This is a bit messy, but not sure how else do to it?
        QStringList prefixes=buildPrefixes(cmd,extended);

        if (cmd.sstr=='i') {
            func = getFreqFunc(splitVfo);
            if (splitVfo == vfoSub && rigCaps->numReceiver > 1)
                currentRx=1;

        }
        else if (cmd.sstr=='x') {
            func = getModeFunc(splitVfo);
            if (splitVfo == vfoSub && rigCaps->numReceiver > 1)
                currentRx=1;
        } else {

            if (func == funcFreqGet) {
                func = getFreqFunc(currentVfo);
            } else if (func == funcModeGet) {
                func = getModeFunc(currentVfo);
            }
        }

        //qInfo() << "getting Cache Value for func" << funcString[func] << "on rx" << currentRx;
        if (rigCaps->commands.contains(func))
            item = queue->getCache(func,currentRx);

        ret = RIG_OK;
        switch (cmd.type){
        case typePTT: {
            bool b = item.value.toBool();
            if (prefixes.length() && params.length())
            {
                response.append(QString("%0%1").arg(prefixes[0], params[0]));
            }
            if (prefixes.length()> 1)
            {
                response.append(QString("%0%1").arg(prefixes[0], QString::number(b)));
            }
            break;
        }
        case typeBinary:
        {
            bool b = item.value.toBool();
            if (prefixes.length())
            {
                response.append(QString("%0%1").arg(prefixes[0], QString::number(b)));
            }
            break;
        }
        case typeUChar:
        case typeUShort:
        case typeShort:
        {
            int i = item.value.toInt();

            if (cmd.func == funcLockFunction)
                i = modeLock;

            if (prefixes.length())
            {
                response.append(QString("%0%1").arg(prefixes[0], QString::number(i)));
            }
            break;
        }
        case typeFloat:
        {
            float f = item.value.toFloat() / 255.0;
            if (prefixes.length())
            {
                response.append(QString("%0%1").arg(prefixes[0],QString::number(f,typeFloat,6)));
            }
            break;
        }
        case typeFloatDiv:
        {
            float f = item.value.toFloat() * 10;
            if (prefixes.length())
            {
                response.append(QString("%0%1").arg(prefixes[0],QString::number(f,typeFloat,6)));
            }
            break;
        }
        case typeFloatDiv5:
        {
            float f = item.value.toFloat() / 5.1;
            if (prefixes.length())
            {
                response.append(QString("%0%1").arg(prefixes[0],QString::number(f,typeFloat,6)));
            }
            break;
        }
        case typeFreq:
        { // Frequency
            freqt f = item.value.value<freqt>();

            //qInfo(logRigCtlD()) << "Got Freq" << funcString[func] << f.Hz << "on VFO" << f.VFO;
            if (prefixes.length())
            {
                response.append(QString("%0%1").arg(prefixes[0],QString::number(f.Hz)));
            }
            break;
        }
        case typeVFO:
        { // VFO
            //uchar v = item.value.value<uchar>();
            if (prefixes.length())
            {
                response.append(QString("%0%1").arg(prefixes[0],getVfoName(currentVfo)));
            }
            break;
        }
        case typeSplitVFO:
        { // VFO
            bool v = item.value.value<uchar>();
            if (prefixes.length()>0)
                response.append(QString("%0%1").arg(prefixes[0],QString::number(v)));
            if (prefixes.length()>1)
                response.append(QString("%0%1").arg(prefixes[1],getVfoName(splitVfo)));
            break;
        }

        case typeVFOInfo:
        {
            uchar rx = 0;
            funcs freqFunc = getFreqFunc(vfoFromName(params[0]));
            funcs modeFunc = getModeFunc(vfoFromName(params[0]));

            if (rigCaps->numReceiver > 1 && params.size() && params[0] == "Sub") {
                rx=1;
            }

            if (prefixes.length()>1)
                response.append(QString("%0%1").arg(prefixes[1],QString::number(queue->getCache(freqFunc,rx).value.value<freqt>().Hz)));
            if (prefixes.length()>2)
                response.append(QString("%0%1").arg(prefixes[2],getMode(queue->getCache(modeFunc,rx).value.value<modeInfo>())));
            if (prefixes.length()>3)
                response.append(QString("%0%1").arg(prefixes[3],QString::number(queue->getCache(funcFilterWidth,rx).value.value<ushort>())));
            if (prefixes.length()>4)
                response.append(QString("%0%1").arg(prefixes[4],QString::number(queue->getCache(funcSplitStatus,rx).value.value<duplexMode_t>())));
            if (prefixes.length()>5)
                response.append(QString("%0%1").arg(prefixes[5],QString::number(queue->getCache(funcSatelliteMode,rx).value.value<bool>())));
            break;
        }

        case typeMode:
        { // Mode
            modeInfo m = item.value.value<modeInfo>();
            cacheItem f = queue->getCache(funcFilterWidth,currentRx);
            if (prefixes.length() > 0)
                response.append(QString("%0%1").arg(prefixes[0],getMode(m)));
            if (prefixes.length() > 1)
                response.append(QString("%0%1").arg(prefixes[1],QString::number(f.value.value<ushort>())));
            break;
        }
        case typeString:
        {
            // Stop sending CW if a blank command is received.
            queue->add(priorityImmediate, queueItem(func, QString(QChar(0xff)),false,0));
            ret = RIG_OK;
            break;
        }
        default:
            qInfo(logRigCtlD()) << "Unsupported type (FIXME):" << item.value.typeName();
            ret = -RIG_EINVAL;
            return ret;
        }
    }

    return ret;
}


int rigCtlClient::getSubCommand(QStringList& response, bool extended, const commandStruct cmd, const subCommandStruct sub[], QStringList params)
{
    Q_UNUSED(extended)

    int ret = -RIG_EINVAL;

    if (rigCaps == Q_NULLPTR)
        return ret;


    QString resp;
    // Get the required subCommand and process it
    if (params.isEmpty())
    {
        return ret;
    }

    if (params[0][0].toLatin1() == '?')
    {
        for (int i=0; sub[i].str[0] != '\0' ;i++)
        {
            if (sub[i].func != funcNone && rigCaps->commands.contains(sub[i].func)) {
                resp.append(sub[i].str);
                resp.append(" ");
            }
        }
    }
    else
    {
        for (int i = 0 ; sub[i].str[0] != '\0'; i++)
        {
            if (!strncmp(params[0].toLocal8Bit(),sub[i].str,MAXNAMESIZE))
            {
                ret = RIG_OK;

                if (((cmd.flags & ARG_IN) == ARG_IN) && params.size() > 1)
                {
                    // We are expecting a second argument to the command
                    QVariant val;
                    switch (sub[i].type)
                    {
                    case typeUShort:
                        val.setValue(static_cast<ushort>(params[1].toInt()));  // rigctl sends 0.0-1.0, we expect 0-255
                        break;
                   case typeShort:
                        val.setValue(static_cast<short>(params[1].toInt()));  // rigctl sends 0.0-1.0, we expect 0-255
                        break;
                    case typeUChar:
                    {
                        uchar v = static_cast<uchar>(params[1].toInt());
                        if (params[0] == "FBKIN")
                            v = (v << 1) & 0x02; // BREAKIN is not bool!
                        if (params[0] == "AGC")
                            v = (v << 1);
                        val.setValue(v);
                        break;
                    }
                    case typeFloat:
                        val.setValue(static_cast<ushort>(float(params[1].toFloat() * 255.0)));  // rigctl sends 0.0-1.0, we expect 0-255
                        //qInfo(logRigCtlD()) << "Setting float value" << params[0] << "to" << val << "original" << params[1];
                        break;
                    case typeFloatDiv:
                        val.setValue(static_cast<uchar>(float(params[1].toFloat() / 10.0)));
                        break;
                    case typeFloatDiv5:
                        val.setValue(static_cast<ushort>(float(params[1].toFloat() * 5.1)));
                        break;
                    case typeBinary:
                        val.setValue(static_cast<bool>(params[1].toInt()));
                        break;
                    default:
                        qInfo(logRigCtlD()) << "Unable to parse value of type" << sub[i].type;
                        return -RIG_EINVAL;
                    }
                    if (rigCaps->commands.contains(sub[i].func))
                        queue->add(priorityImmediate, queueItem(sub[i].func, val,false,currentRx));
                } else if (params.size() == 1){
                    // Not expecting a second argument as it is a get so dump the cache
                    cacheItem item;
                    if (rigCaps->commands.contains(sub[i].func))
                        item = queue->getCache(sub[i].func,currentRx);
                    int val = 0;
                    // Special situation for S-Meter
                    if (params[0] == "STRENGTH") {
                        if (rigCaps->model == model7610)
                            val = getCalibratedValue(item.value.toUInt(), IC7610_STR_CAL);
                        else if (rigCaps->model == model7850)
                            val = getCalibratedValue(item.value.toUInt(), IC7850_STR_CAL);
                        else
                            val = getCalibratedValue(item.value.toUInt(), IC7300_STR_CAL);
                        resp.append(QString("%1").arg(val));
                    }
                    else
                    {
                        switch (sub[i].type)
                        {
                        case typeBinary:
                            resp.append(QString("%1").arg(item.value.toBool()));
                            break;
                        case typeUChar:
                        {
                            int val = item.value.toInt();
                            if (params[0] == "FBKIN")
                                val = (val >> 1) & 0x01;
                            if (params[0] == "AGC")
                                val = (val >> 1);
                            resp.append(QString::number(val));
                            break;
                        }
                        case typeSWR:{
                            float f=rawToFloat(item.value.toInt(),&swrCal);
                            resp.append(QString::number(f,'f',6));
                            break;
                        }
                        case typeUShort:
                        case typeShort:
                            resp.append(QString::number(item.value.toInt()));
                            break;
                        case typeFloat:
                            resp.append(QString::number(item.value.toFloat() / 255.0,'f',6));
                            break;
                        case typeFloatDiv:
                            resp.append(QString::number(item.value.toFloat() * 10.0,'f',6));
                            break;
                        case typeFloatDiv5:
                            resp.append(QString::number(item.value.toFloat()/5.1,'f',6));
                            break;
                        default:
                            qInfo(logRigCtlD()) << "Unhandled:" << item.value.toUInt() << "OUT" << val;
                            ret = -RIG_EINVAL;
                        }
                    }
                    //if (extended && cmd.arg2 != NULL)
                    //{
                    //    response.append(QString("%0: %1").arg(cmd.str, params[0]));
                    //    response.append(QString("%0: %1").arg(cmd.arg2, resp));
                    //}
                    //else
                    //{
                        response.append(resp);
                    //}
                } else {
                    qInfo(logRigCtlD()) << "Invalid number of params" <<  params.size();
                    ret = -RIG_EINVAL;
                }
                break;
            }
        }
    }
    return ret;
}


int rigCtlClient::dumpState(QStringList &response, bool extended)
{
    Q_UNUSED(extended)

    quint64 modes = getRadioModes();

    // rigctld protocol version
    response.append("1");
    // Radio model
    response.append(QString::number(rigCaps->rigctlModel));
    // Print something (used to be ITU region)
    response.append("0");
    // Supported RX bands (startf,endf,modes,low_power,high_power,vfo,ant)
    quint32 lowFreq = 0;
    quint32 highFreq = 0;
    for (const bandType &band : std::as_const(rigCaps->bands))
    {
        if (lowFreq == 0 || band.lowFreq < lowFreq)
            lowFreq = band.lowFreq;
        if (band.highFreq > highFreq)
            highFreq = band.highFreq;
    }
    response.append(QString("%1.000000 %2.000000 0x%3 %4 %5 0x%6 0x%7").arg(lowFreq).arg(highFreq)
                        .arg(modes, 0, 16).arg(-1).arg(-1).arg(vfoList, 0, 16).arg(getAntennas(), 0, 16));
    response.append("0 0 0 0 0 0 0");

    if (rigCaps->hasTransmit) {
        // Supported TX bands (startf,endf,modes,low_power,high_power,vfo,ant)
        for (const bandType &band : std::as_const(rigCaps->bands))
        {
            response.append(QString("%1.000000 %2.000000 0x%3 %4 %5 0x%6 0x%7").arg(band.lowFreq).arg(band.highFreq)
                                .arg(modes, 0, 16).arg(2000).arg(100000).arg(0x16000000, 0, 16).arg(getAntennas(), 0, 16));
        }
    }
    response.append("0 0 0 0 0 0 0");
    for (auto& step: rigCaps->steps)
    {
        if (step.num > 0)
            response.append(QString("0x%1 %2").arg(modes, 0, 16).arg(step.hz));
    }
    response.append("0 0");

    modes = getRadioModes("SB");
    if (modes) {
        response.append(QString("0x%1 3000").arg(modes, 0, 16));
        response.append(QString("0x%1 2400").arg(modes, 0, 16));
        response.append(QString("0x%1 1800").arg(modes, 0, 16));
    }
    modes = getRadioModes("AM");
    if (modes) {
        response.append(QString("0x%1 9000").arg(modes, 0, 16));
        response.append(QString("0x%1 6000").arg(modes, 0, 16));
        response.append(QString("0x%1 3000").arg(modes, 0, 16));
    }
    modes = getRadioModes("CW");
    if (modes) {
        response.append(QString("0x%1 1200").arg(modes, 0, 16));
        response.append(QString("0x%1 500").arg(modes, 0, 16));
        response.append(QString("0x%1 200").arg(modes, 0, 16));
    }
    modes = getRadioModes("FM");
    if (modes) {
        response.append(QString("0x%1 15000").arg(modes, 0, 16));
        response.append(QString("0x%1 10000").arg(modes, 0, 16));
        response.append(QString("0x%1 7000").arg(modes, 0, 16));
    }
    modes = getRadioModes("RTTY");
    if (modes) {
        response.append(QString("0x%1 2400").arg(modes, 0, 16));
        response.append(QString("0x%1 500").arg(modes, 0, 16));
        response.append(QString("0x%1 250").arg(modes, 0, 16));
    }
    modes = getRadioModes("PSK");
    if (modes) {
        response.append(QString("0x%1 1200").arg(modes, 0, 16));
        response.append(QString("0x%1 500").arg(modes, 0, 16));
        response.append(QString("0x%1 250").arg(modes, 0, 16));
    }
    response.append("0 0");
    response.append("9900");
    response.append("9900");
    response.append("10000");
    response.append("0");
    QString preamps="";
    if(rigCaps->commands.contains(funcPreamp)) {
        for (auto &pre: rigCaps->preamps)
        {
            if (pre.num == 0)
                continue;
            preamps.append(QString(" %1").arg(pre.num*10));
        }
        if (preamps.endsWith(" "))
            preamps.chop(1);
    }
    else {
        preamps = "0";
    }
    response.append(preamps);

    QString attens = "";
    if(rigCaps->commands.contains(funcAttenuator)) {
        for (auto att : rigCaps->attenuators)
        {
            if (att == 0)
                continue;
            attens.append(QString(" %1").arg(att));
        }
        if (attens.endsWith(" "))
            attens.chop(1);
    }
    else {
        attens = "0";
    }
    response.append(attens);

    qulonglong hasFuncs=0;
    qulonglong hasLevels=0;
    qulonglong hasParams=0;
    for (int i = 0 ; functions_str[i].str[0] != '\0'; i++)
    {
        if (functions_str[i].func != funcNone && rigCaps->commands.contains(functions_str[i].func))
        {
            hasFuncs |= 1ULL << i;
        }
    }
    response.append(QString("0x%0").arg(hasFuncs,16,16,QChar('0')));
    response.append(QString("0x%0").arg(hasFuncs,16,16,QChar('0')));

    for (int i = 0 ; levels_str[i].str[0] != '\0'; i++)
    {
        if (levels_str[i].func != funcNone && rigCaps->commands.contains(levels_str[i].func))
        {
            hasLevels |= 1ULL << i;
        }
    }
    response.append(QString("0x%0").arg(hasLevels,16,16,QChar('0')));
    response.append(QString("0x%0").arg(hasLevels,16,16,QChar('0')));

    for (int i = 0 ; params_str[i].str[0] != '\0'; i++)
    {
        if (params_str[i].func != funcNone && rigCaps->commands.contains(params_str[i].func))
        {
            hasParams |= 1ULL << i;
        }
    }
    response.append(QString("0x%0").arg(hasParams,16,16,QChar('0')));
    response.append(QString("0x%0").arg(hasParams,16,16,QChar('0')));

    if (chkVfoEecuted) {
        response.append(QString("vfo_ops=0x%1").arg(255, 0, 16));
        response.append(QString("ptt_type=0x%1").arg(rigCaps->hasTransmit, 0, 16));
        response.append(QString("has_set_vfo=0x%1").arg(1, 0, 16));
        response.append(QString("has_get_vfo=0x%1").arg(1, 0, 16));
        response.append(QString("has_set_freq=0x%1").arg(1, 0, 16));
        response.append(QString("has_get_freq=0x%1").arg(1, 0, 16));
        response.append(QString("has_set_conf=0x%1").arg(1, 0, 16));
        response.append(QString("has_get_conf=0x%1").arg(1, 0, 16));
        response.append(QString("has_power2mW=0x%1").arg(1, 0, 16));
        response.append(QString("has_mW2power=0x%1").arg(1, 0, 16));
        response.append(QString("timeout=0x%1").arg(1000, 0, 16));
        response.append("done");
    }

    return RIG_OK;
}

int rigCtlClient::dumpCaps(QStringList &response, bool extended)
{
    Q_UNUSED(extended)
    response.append(QString("Caps dump for model: %1").arg(rigCaps->modelID));
    response.append(QString("Model Name:\t%1").arg(rigCaps->modelName));
    response.append(QString("Mfg Name:\tIcom"));
    response.append(QString("Backend version:\t0.1"));
    response.append(QString("Backend copyright:\t2021"));
    if (rigCaps->hasTransmit) {
        response.append(QString("Rig type:\tTransceiver"));
    }
    else
    {
        response.append(QString("Rig type:\tReceiver"));
    }
    if(rigCaps->commands.contains(funcTransceiverStatus)) {
        response.append(QString("PTT type:\tRig capable"));
    }
    response.append(QString("DCD type:\tRig capable"));
    response.append(QString("Port type:\tNetwork link"));
    return RIG_OK;
}

funcs rigCtlClient::getFreqFunc(vfo_t vfo, bool set)
{
    funcs func = ((rigCaps->commands.contains(funcFreq)) ? funcFreq: funcFreqGet) ;

    currentRx = 0;
    if (set)
        func = ((rigCaps->commands.contains(funcFreq)) ? funcFreq: funcFreqSet) ;

    if ((vfo == vfoA && currentVfo == vfoA) || (vfo == vfoB && currentVfo == vfoB))
    {
        func = ((rigCaps->commands.contains(funcSelectedFreq)) ? funcSelectedFreq: func);
    }
    else if ((vfo == vfoA && currentVfo == vfoB) || (vfo == vfoB && currentVfo == vfoA))
    {
        func = ((rigCaps->commands.contains(funcUnselectedFreq)) ? funcUnselectedFreq: func);
        if (currentVfo == vfoSub && rigCaps->numReceiver>1)
        {
            currentRx=1;
        }
    }
    else if (vfo == vfoMain || vfo == vfoSub)
    {
        func = ((rigCaps->commands.contains(funcFreq)) ? funcFreq: func);
    }
     //qDebug(logRigCtlD) << "Frequency function is:" << funcString[func];
    return func;
}

funcs rigCtlClient::getModeFunc(vfo_t vfo, bool set)
{
    funcs func = ((rigCaps->commands.contains(funcMode)) ? funcMode: funcModeGet) ;

    currentRx=0;

    if (set)
        func = ((rigCaps->commands.contains(funcMode)) ? funcMode: funcModeSet) ;

    if ((vfo == vfoA && currentVfo == vfoA) || (vfo == vfoB && currentVfo == vfoB))
    {
        func = ((rigCaps->commands.contains(funcSelectedMode)) ? funcSelectedMode: func);
    }
    else if ((vfo == vfoA && currentVfo == vfoB) || (vfo == vfoB && currentVfo == vfoA))
    {
        func = ((rigCaps->commands.contains(funcUnselectedMode)) ? funcUnselectedMode: func);
    }
    else if (vfo == vfoMain || vfo == vfoSub)
    {
        func = ((rigCaps->commands.contains(funcMode)) ? funcMode: func);
        if (currentVfo == vfoSub && rigCaps->numReceiver>1)
        {
            currentRx=1;
        }
    }
    //qDebug(logRigCtlD) << "Mode function is:" << funcString[func];
    return func;
}

QStringList rigCtlClient::buildPrefixes(commandStruct cmd,bool extended)
{
    QStringList prefixes;
    if (extended) {
        if (cmd.arg1 != NULL)
            prefixes.append(QString("%0: ").arg(cmd.arg1));
        if (cmd.arg2 != NULL)
            prefixes.append(QString("%0: ").arg(cmd.arg2));
        if (cmd.arg3 != NULL)
            prefixes.append(QString("%0: ").arg(cmd.arg3));
        if (cmd.arg4 != NULL)
            prefixes.append(QString("%0: ").arg(cmd.arg4));
        if (cmd.arg5 != NULL)
            prefixes.append(QString("%0: ").arg(cmd.arg5));
        if (cmd.arg6 != NULL)
            prefixes.append(QString("%0: ").arg(cmd.arg6));
    } else {
        if (cmd.arg1 != NULL)
            prefixes.append("");
        if (cmd.arg2 != NULL)
            prefixes.append("");
        if (cmd.arg3 != NULL)
            prefixes.append("");
        if (cmd.arg4 != NULL)
            prefixes.append("");
        if (cmd.arg5 != NULL)
            prefixes.append("");
        if (cmd.arg6 != NULL)
            prefixes.append("");
    }
    return prefixes;
}

int rigCtlClient::power2mW(QStringList& response, bool extended, const commandStruct cmd, QStringList params )
{
    int ret=RIG_OK;
    QStringList prefixes = buildPrefixes(cmd,extended);
    if (params.size() >= 2)
    {
        bool ok=false;
        float power = params[0].toFloat(&ok);
        quint64 freq = params[1].toULongLong(&ok);
        if (ok)
        {
            if (freq == 0)
            {
                // Frequency was zero, so get cached freq
                freq = queue->getCache(getFreqFunc(currentVfo,false),currentRx).value.value<freqt>().Hz;
            }
            for (auto &b: rigCaps->bands)
            {
                // Highest frequency band is always first!
                if (freq >= b.lowFreq && freq <= b.highFreq)
                {
                    // This frequency is contained within this band!
                    int p = int((b.power * power) * 1000);
                    if (prefixes.length()>3)
                        response.append(QString("%0%1").arg(prefixes[3],QString::number(p)));
                    break;
                }
            }
        }
        else
        {
            ret = RIG_EINVAL;
        }
    } else {
        ret = RIG_EINVAL;
    }
    return ret;
}

int rigCtlClient::mW2power(QStringList& response, bool extended, const commandStruct cmd, QStringList params )
{
    int ret=RIG_OK;
    QStringList prefixes = buildPrefixes(cmd,extended);
    if (params.size() > 2)
    {
        bool ok=false;
        int power = params[0].toInt(&ok);
        quint64 freq = params[1].toULongLong(&ok);
        if (ok)
        {
            if (freq == 0)
            {
                // Frequency was zero, so get cached freq
                freq = queue->getCache(getFreqFunc(currentVfo,false),currentRx).value.value<freqt>().Hz;
            }
            for (auto &b: rigCaps->bands)
            {
                // Highest frequency band is always first!
                if (freq >= b.lowFreq && freq <= b.highFreq)
                {
                    // This frequency is contained within this band!
                    float p = float(power / (b.power * 1000.0));
                    if (prefixes.length()>3)
                        response.append(QString("%0%1").arg(prefixes[3],QString::number(p,'f',5)));
                    break;
                }
            }
        }
        else
        {
            ret = RIG_EINVAL;
        }

    } else {
        ret = RIG_EINVAL;
    }

    return ret;
}

float rigCtlClient::rawToFloat(int raw, const cal_table_float_t *cal)
{

    int i;
    float val;

    if (cal->size == 0)
    {
        return raw;
    }

    for (i = 0; i<cal->size; i++)
    {
        if (raw < cal->table[i].raw)
        {
            break;
        }
    }

    if (raw == cal->table[i - 1].raw)
    {
        return cal->table[i - 1].val;
    }

    if (i == 0)
    {
        return cal->table[0].val;
    }

    if (i >= cal->size)
    {
        return cal->table[i - 1].val;
    }

    if (cal->table[i].raw == cal->table[i - 1].raw)
    {
        return cal->table[i].val;
    }

    val = ((cal->table[i].raw - raw)
                     * (float)(cal->table[i].val - cal->table[i - 1].val))
                    / (float)(cal->table[i].raw - cal->table[i - 1].raw);

    return float(cal->table[i].val - val);
}

