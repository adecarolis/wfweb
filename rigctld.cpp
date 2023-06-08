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
    {"PREAMP",funcPreamp,'g'},
    {"ATT",funcAttenuator,'i'},
    {"VOXDELAY",funcVOXDelay,'f'},
    {"AF",funcAfGain,'f'},
    {"RF",funcRfGain,'f'},
    {"SQL",funcSquelch,'f'},
    {"IF",funcIFFilterWidth,'f'},
    {"APF",funcAPFLevel,'f'},
    {"NR",funcNRLevel,'f'},
    {"PBT_IN",funcPBTInner,'f'},
    {"PBT_OUT",funcPBTOuter,'f'},
    {"CWPITCH",funcCwPitch,'f'},
    {"RFPOWER",funcRFPower,'f'},
    {"MICGAIN",funcMicGain,'f'},
    {"KEYSPD",funcKeySpeed,'h'},
    {"NOTCHF",funcNotchFilter,'i'},
    {"COMP",funcCompressorLevel,'f'},
    {"AGC",funcAGCTime,'f'},
    {"BKINDL",funcBreakInDelay,'f'},
    {"BAL",funcNone,'f'},
    {"METER",funcNone,'f'},
    {"VOXGAIN",funcVoxGain,'f'},
    {"ANTIVOX",funcAntiVoxGain,'f'},
    {"SLOPE_LOW",funcNone,'f'},
    {"SLOPE_HIGH",funcNone,'f'},
    {"BKIN_DLYMS",funcBreakInDelay,'f'},
    {"RAWSTR",funcNone,'f'},
    {"SWR",funcSMeter,'f'},
    {"ALC",funcALCMeter,'f'},
    {"STRENGTH",funcSMeter,'f'},
    {"RFPOWER_METER",funcPowerMeter,'f'},
    {"COMPMETER",funcCompMeter,'f'},
    {"VD_METER",funcVdMeter,'f'},
    {"ID_METER",funcIdMeter,'f'},
    {"NOTCHF_RAW",funcNone,'f'},
    {"MONITOR_GAIN",funcMonitorGain,'f'},
    {"NQ",funcNone,'f'},
    {"RFPOWER_METER_WATT",funcNone,'f'},
    {"SPECTRUM_MDOE",funcNone,'f'},
    {"SPECTRUM_SPAN",funcNone,'f'},
    {"SPECTRUM_EDGE_LOW",funcNone,'f'},
    {"SPECTRUM_EDGE_HIGH",funcNone,'f'},
    {"SPECTRUM_SPEED",funcNone,'f'},
    {"SPECTRUM_REF",funcNone,'f'},
    {"SPECTRUM_AVG",funcNone,'f'},
    {"SPECTRUM_ATT",funcNone,'f'},
    {"TEMP_METER",funcNone,'f'},
    {"BAND_SELECT",funcNone,'f'},
    {"USB_AF",funcNone,'f'},
    {"",funcNone,'\x0'}
};

static const subCommandStruct functions_str[] =
{
    {"FAGC",funcAGCTime,'i'},
    {"NB",funcNoiseBlanker,'b'},
    {"COMP",funcCompressor,'b'},
    {"VOX",funcVox,'b'},
    {"TONE",funcRepeaterTone,'b'},
    {"TQSL",funcRepeaterTSQL,'b'},
    {"SBKIN",funcBreakIn,'i'},
    {"FBKIN",funcBreakIn,'i'},
    {"ANF",funcAutoNotch,'b'},
    {"NR",funcNoiseReduction,'b'},
    {"AIP",funcNone,'b'},
    {"APF",funcAPFType,'b'},
    {"MON",funcMonitor,'b'},
    {"MN",funcManualNotch,'b'},
    {"RF",funcNone,'b'},
    {"ARO",funcNone,'b'},
    {"LOCK",funcLockFunction,'b'},
    {"MUTE",funcAFMute,'b'},
    {"VSC",funcNone,'b'},
    {"REV",funcNone,'b'},
    {"SQL",funcSquelch,'b'}, // Actually an integer as ICOM doesn't provide on/off for squelch
    {"ABM",funcNone,'b'},
    {"VSC",funcNone,'b'},
    {"REV",funcNone,'b'},
    {"BC",funcNone,'b'},
    {"MBC",funcNone,'b'},
    {"RIT",funcRitStatus,'b'},
    {"AFC",funcNone,'b'},
    {"SATMODE",funcSatelliteMode,'b'},
    {"SCOPE",funcScopeOnOff,'b'},
    {"RESUME",funcNone,'b'},
    {"TBURST",funcNone,'b'},
    {"TUNER",funcTunerStatus,'b'},
    {"XIT",funcNone,'b'},
    {"",funcNone,'b'}
};

static const subCommandStruct params_str[] =
{
    {"ANN",funcNone,'i'},
    {"APO",funcNone,'i'},
    {"BACKLIGHT",funcBackLight,'i'},
    {"BEEP",funcNone,'i'},
    {"TIME",funcTime,'i'},
    {"BAT",funcNone,'i'},
    {"KEYLIGHT",funcNone,'i'},
    {"",funcNone}
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
    { 'F',  "set_freq",         funcFreqSet,       'x',    ARG_IN, "Frequency" },
    { 'f',  "get_freq",         funcFreqGet,       'x',    ARG_OUT, "Frequency" },
    { 'M',  "set_mode",         funcModeSet,            'm',    ARG_IN, "Mode", "Passband" },
    { 'm',  "get_mode",         funcModeGet,            'm',    ARG_OUT, "Mode", "Passband" },
    { 'I',  "set_split_freq",   funcSendFreqOffset,     'x',    ARG_IN, "TX Frequency" },
    { 'i',  "get_split_freq",   funcReadFreqOffset,     'x',    ARG_OUT, "TX Frequency" },
    { 'X',  "set_split_mode",   funcSplitStatus,        'm',    ARG_IN, "TX Mode", "TX Passband" },
    { 'x',  "get_split_mode",   funcSplitStatus,        'm',    ARG_OUT, "TX Mode", "TX Passband" },
    { 'K',  "set_split_freq_mode",  funcNone,           'x',    ARG_IN,    "TX Frequency", "TX Mode", "TX Passband" },
    { 'k',  "get_split_freq_mode",  funcNone,           'x',    ARG_OUT,   "TX Frequency", "TX Mode", "TX Passband" },
    { 'S',  "set_split_vfo",    funcNone,               'x',    ARG_IN, "Split", "TX VFO" },
    { 's',  "get_split_vfo",    funcNone,               'x',    ARG_OUT, "Split", "TX VFO" },
    { 'N',  "set_ts",           funcTuningStep,         'i',    ARG_IN, "Tuning Step" },
    { 'n',  "get_ts",           funcTuningStep,         'i',    ARG_OUT, "Tuning Step" },
    { 'L',  "set_level",        funcRigctlLevel,        'z',    ARG_IN, "Level", "Level Value" },
    { 'l',  "get_level",        funcRigctlLevel,        'z',    ARG_IN1 | ARG_OUT2, "Level", "Level Value" },
    { 'U',  "set_func",         funcRigctlFunction,     'z',    ARG_IN, "Func", "Func Status" },
    { 'u',  "get_func",         funcRigctlFunction,     'z',    ARG_IN1 | ARG_OUT2, "Func", "Func Status" },
    { 'P',  "set_parm",         funcRigctlParam,        'z',    ARG_IN1 | ARG_NOVFO, "Parm", "Parm Value" },
    { 'p',  "get_parm",         funcRigctlParam,        'z',    ARG_IN1 | ARG_OUT2, "Parm", "Parm Value" },
    { 'G',  "vfo_op",           funcSelectVFO,          'i',    ARG_IN, "Mem/VFO Op" },
    { 'g',  "scan",             funcScanning,           'i',    ARG_IN, "Scan Fct", "Scan Channel" },
    { 'A',  "set_trn",          funcCIVTransceive,      'i',    ARG_IN  | ARG_NOVFO, "Transceive" },
    { 'a',  "get_trn",          funcCIVTransceive,      'i',    ARG_OUT | ARG_NOVFO, "Transceive" },
    { 'R',  "set_rptr_shift",   funcSendFreqOffset,     'i',    ARG_IN, "Rptr Shift" },
    { 'r',  "get_rptr_shift",   funcReadFreqOffset,     'i',    ARG_OUT, "Rptr Shift" },
    { 'O',  "set_rptr_offs",    funcSendFreqOffset,     'i',    ARG_IN, "Rptr Offset" },
    { 'o',  "get_rptr_offs",    funcReadFreqOffset,     'i',    ARG_OUT, "Rptr Offset" },
    { 'C',  "set_ctcss_tone",   funcToneFreq,           'i',    ARG_IN, "CTCSS Tone" },
    { 'c',  "get_ctcss_tone",   funcToneFreq,           'i',    ARG_OUT, "CTCSS Tone" },
    { 'D',  "set_dcs_code",     funcDTCSCode,           'i',    ARG_IN, "DCS Code" },
    { 'd',  "get_dcs_code",     funcDTCSCode,           'i',    ARG_OUT, "DCS Code" },
    { 0x90, "set_ctcss_sql",    funcTSQLFreq,           'i',    ARG_IN, "CTCSS Sql" },
    { 0x91, "get_ctcss_sql",    funcTSQLFreq,           'i',    ARG_OUT, "CTCSS Sql" },
    { 0x92, "set_dcs_sql",      funcCSQLCode,           'i',    ARG_IN, "DCS Sql" },
    { 0x93, "get_dcs_sql",      funcCSQLCode,           'i',    ARG_OUT, "DCS Sql" },
    { 'V',  "set_vfo",          funcSelectVFO,          'v',    ARG_IN  | ARG_NOVFO, "VFO" },
    { 'v',  "get_vfo",          funcSelectVFO,          'v',    ARG_NOVFO | ARG_OUT, "VFO" },
    { 'T',  "set_ptt",          funcTransceiverStatus,  'i',    ARG_IN, "PTT" },
    { 't',  "get_ptt",          funcTransceiverStatus,  'i',    ARG_OUT, "PTT" },
    { 'E',  "set_mem",          funcMemoryContents,     'm',    ARG_IN, "Memory#" },
    { 'e',  "get_mem",          funcMemoryContents,     'm',    ARG_OUT, "Memory#" },
    { 'H',  "set_channel",      funcMemoryMode,         'i',    ARG_IN  | ARG_NOVFO, "Channel"},
    { 'h',  "get_channel",      funcMemoryMode,         'i',    ARG_IN  | ARG_NOVFO, "Channel", "Read Only" },
    { 'B',  "set_bank",         funcMemoryGroup,        'i',    ARG_IN, "Bank" },
    { '_',  "get_info",         funcNone,               'i',    ARG_OUT | ARG_NOVFO, "Info" },
    { 'J',  "set_rit",          funcRITFreq,            's',    ARG_IN, "RIT" },
    { 'j',  "get_rit",          funcRITFreq,            's',    ARG_OUT, "RIT" },
    { 'Z',  "set_xit",          funcNone,               's',    ARG_IN, "XIT" },
    { 'z',  "get_xit",          funcNone,               's',    ARG_OUT, "XIT" },
    { 'Y',  "set_ant",          funcAntenna,            'i',    ARG_IN, "Antenna", "Option" },
    { 'y',  "get_ant",          funcAntenna,            'i',    ARG_IN1 | ARG_OUT2 | ARG_NOVFO, "AntCurr", "Option", "AntTx", "AntRx" },
    { 0x87, "set_powerstat",    funcPowerControl,       'b',    ARG_IN  | ARG_NOVFO, "Power Status" },
    { 0x88, "get_powerstat",    funcPowerControl,       'b',    ARG_OUT | ARG_NOVFO, "Power Status" },
    { 0x89, "send_dtmf",        funcNone,               'i',    ARG_IN, "Digits" },
    { 0x8a, "recv_dtmf",        funcNone,               'i',    ARG_OUT, "Digits" },
    { '*',  "reset",            funcNone,               'i',    ARG_IN, "Reset" },
    { 'w',  "send_cmd",         funcNone,               'i',    ARG_IN1 | ARG_IN_LINE | ARG_OUT2 | ARG_NOVFO, "Cmd", "Reply" },
    { 'W',  "send_cmd_rx",      funcNone,               'i',    ARG_IN | ARG_OUT2 | ARG_NOVFO, "Cmd", "Reply"},
    { 'b',  "send_morse",       funcSendCW,             'i',    ARG_IN | ARG_NOVFO  | ARG_IN_LINE, "Morse" },
    { 0xbb, "stop_morse",       funcSendCW,             'i',    },
    { 0xbc, "wait_morse",       funcSendCW,             'i',    },
    { 0x94, "send_voice_mem",   funcNone,               'i',    ARG_IN, "Voice Mem#" },
    { 0x8b, "get_dcd",          funcNone,               'i',    ARG_OUT, "DCD" },
    { 0x8d, "set_twiddle",      funcNone,               'i',    ARG_IN  | ARG_NOVFO, "Timeout (secs)" },
    { 0x8e, "get_twiddle",      funcNone,               'i',    ARG_OUT | ARG_NOVFO, "Timeout (secs)" },
    { 0x97, "uplink",           funcNone,               'i',    ARG_IN | ARG_NOVFO, "1=Sub, 2=Main" },
    { 0x95, "set_cache",        funcNone,               'i',    ARG_IN | ARG_NOVFO, "Timeout (msecs)" },
    { 0x96, "get_cache",        funcNone,               'i',    ARG_OUT | ARG_NOVFO, "Timeout (msecs)" },
    { '2',  "power2mW",         funcNone,               'i',    ARG_IN1 | ARG_IN2 | ARG_IN3 | ARG_OUT1 | ARG_NOVFO, "Power [0.0..1.0]", "Frequency", "Mode", "Power mW" },
    { '4',  "mW2power",         funcNone,               'i',    ARG_IN1 | ARG_IN2 | ARG_IN3 | ARG_OUT1 | ARG_NOVFO, "Pwr mW", "Freq", "Mode", "Power [0.0..1.0]" },
    { '1',  "dump_caps",        funcNone,               'i',    ARG_NOVFO },
    { '3',  "dump_conf",        funcNone,               'i',    ARG_NOVFO },
    { 0x8f, "dump_state",       funcNone,               'i',    ARG_OUT | ARG_NOVFO },
    { 0xf0, "chk_vfo",          funcSelectVFO,          'v',    ARG_NOVFO, "ChkVFO" },   /* rigctld only--check for VFO mode */
    { 0xf2, "set_vfo_opt",      funcNone,               'i',    ARG_NOVFO | ARG_IN, "Status" }, /* turn vfo option on/off */
    { 0xf3, "get_vfo_info",     funcNone,               'i',    ARG_IN1 | ARG_NOVFO | ARG_OUT5, "VFO", "Freq", "Mode", "Width", "Split", "SatMode" }, /* get several vfo parameters at once */
    { 0xf5, "get_rig_info",     funcNone,               'i',    ARG_NOVFO | ARG_OUT, "RigInfo" }, /* get several vfo parameters at once */
    { 0xf4, "get_vfo_list",     funcNone,               'i',    ARG_OUT | ARG_NOVFO, "VFOs" },
    { 0xf6, "get_modes",        funcNone,               'i',    ARG_OUT | ARG_NOVFO, "Modes" },
    { 0xf9, "get_clock",        funcTime,               'i',    ARG_NOVFO },
    { 0xf8, "set_clock",        funcTime,               'i',    ARG_IN | ARG_NOVFO, "YYYYMMDDHHMMSS.sss+ZZ" },
    { 0xf1, "halt",             funcNone,               'i',    ARG_NOVFO },   /* rigctld only--halt the daemon */
    { 0x8c, "pause",            funcNone,               'i',    ARG_IN, "Seconds" },
    { 0x98, "password",         funcNone,               'i',    ARG_IN | ARG_NOVFO, "Password" },
    { 0xf7, "get_mode_bandwidths", funcNone,            'i',    ARG_IN | ARG_NOVFO, "Mode" },
    { 0xa0, "set_separator",     funcSeparator,         'i',    ARG_IN | ARG_NOVFO, "Separator" },
    { 0xa1, "get_separator",     funcSeparator,         'i',    ARG_NOVFO, "Separator" },
    { 0xa2, "set_lock_mode",     funcLockFunction,      'i',    ARG_IN | ARG_NOVFO, "Locked" },
    { 0xa3, "get_lock_mode",     funcLockFunction,      'i',    ARG_NOVFO, "Locked" },
    { 0xa4, "send_raw",          funcNone,              'i',    ARG_NOVFO | ARG_IN1 | ARG_IN2 | ARG_OUT3, "Terminator", "Command", "Send raw answer" },
    { 0xa5, "client_version",    funcNone,              'i',    ARG_NOVFO | ARG_IN1, "Version", "Client version" },
    { 0x00, "", funcNone, 0x0 },
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
    rigCtlClient* client = new rigCtlClient(socket, rigCaps, this);
    connect(this, SIGNAL(onStopped()), client, SLOT(closeSocket()));
}


void rigCtlD::stopServer()
{
    qInfo(logRigCtlD()) << "stopping server";
    emit onStopped();
}

void rigCtlD::receiveRigCaps(rigCapabilities caps)
{
    qInfo(logRigCtlD()) << "Got rigcaps for:" << caps.modelName;
    this->rigCaps = caps;
}

rigCtlClient::rigCtlClient(int socketId, rigCapabilities caps, rigCtlD* parent) : QObject(parent)
{

    this->setObjectName("RigCtlD");
    queue = cachingQueue::getInstance(this);
    commandBuffer.clear();
    sessionId = socketId;
    rigCaps = caps;
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

}

void rigCtlClient::socketReadyRead()
{
    QByteArray data = socket->readAll();
    commandBuffer.append(data);
    QStringList commandList(commandBuffer.split('\n'));
    QString sep = "\n";

    int ret = -RIG_EINVAL;

    bool setCommand = false;
    QStringList response;

    for (QString &commands : commandList)
    {
        bool extended = false;
        bool longCommand = false;
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

        if (commands[0] == ';' || commands[0] == '|' || commands[0] == ',')
        {
            sep = commands[0].toLatin1();
            commands.remove(0,1);
        }
        else if (commands[0] == '+')
        {
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
            longCommand = true;
        }



        QStringList command = commands.split(" ");
        for (int i=0; commands_list[i].sstr != 0x00; i++)
        {
            if ((longCommand && !strncmp(command[0].toLocal8Bit(), commands_list[i].str,MAXNAMESIZE)) ||
                   (!longCommand && !commands.isNull() && commands[0].toLatin1() == commands_list[i].sstr))
            {
                command.removeFirst(); // Remove the actual command so it only contains params

                if (((commands_list[i].flags & ARG_IN) == ARG_IN))
                {
                    // For debugging only REMOVE next line
                    qInfo(logRigCtlD()) << "Received set command" << commands;
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
                } else if (commands_list[i].sstr == '3')
                {
                    response.append("Model: WFVIEW");
                    ret = RIG_OK;
                    break;
                } else if (commands_list[i].sstr == 0xf0)
                {
                    chkVfoEecuted = true;
                    //ret = RIG_OK;
                    //break;
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
    foreach (QString str, response)
    {
        if (!str.isEmpty())
            sendData(QString("%1%2").arg(str,sep));
    }

    if (sep != "\n") {
        sendData(QString("\n"));
    }
    if (ret < 0 || setCommand )
        sendData(QString("RPRT %1\n").arg(ret));
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
    //qDebug(logRigCtlD()) << sessionId << "TX:" << data;
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
    for (int i = 0; mode_str[i].str[0] != '\0'; i++)
    {
        if (QString(mode_str[i].str) == modeString)
        {
            foreach (auto m, rigCaps.modes)
            {
                if (m.mk == mode_str[i].mk)
                {
                    mode = modeInfo(m);
                    mode.data = mode_str[i].data;
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
    foreach (auto i, rigCaps.antennas)
    {
        ant |= 1<<i.num;
    }
    return ant;
}

quint64 rigCtlClient::getRadioModes(QString md) 
{
    quint64 modes = 0;
    for (auto&& mode : rigCaps.modes)
    {
        for (int i = 0; mode_str[i].str[0] != '\0'; i++)
        {
            QString mstr = QString(mode_str[i].str);
            if (rigCaps.hasDataModes) {
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

quint8 rigCtlClient::vfoFromName(QString vfo) {

    if (vfo.toUpper() == "VFOA" || vfo.toUpper() == "MAIN") return 0;
    if (vfo.toUpper() == "VFOB" || vfo.toUpper() == "SUB") return 1;
    if (vfo.toUpper() == "MEM") return 2;
    return 0;
}

QString rigCtlClient::getVfoName(quint8 vfo)
{
    QString ret;
    switch (vfo) {
    case 0: ret = "VFOA"; break;
    case 1: ret = "VFOB"; break;
    default: ret = "MEM"; break;
    }

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

unsigned long rigCtlClient::doCrc(unsigned char* p, size_t n)
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
    unsigned char b = 0;

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
    funcs func;
    // Use selected/unselected mode/freq if available
    if ((cmd.func == funcFreqGet || cmd.func == funcFreqSet) && rigCaps.commands.contains(funcSelectedFreq)) {
        func = funcSelectedFreq;
    } else if ((cmd.func == funcModeGet || cmd.func == funcModeSet) && rigCaps.commands.contains(funcSelectedMode)) {
        func = funcSelectedMode;
    } else {
        func = cmd.func;
    }
    if (((cmd.flags & ARG_IN) == ARG_IN) && params.size())
    {
        // We are expecting a second argument to the command as it is a set
        QVariant val;
        ret = RIG_OK;
        if (cmd.type == 'i') {
            val.setValue(static_cast<uchar>(params[0].toInt()));
        }
        if (cmd.type == 's') {
            val.setValue(static_cast<short>(params[0].toInt()));
            qInfo(logRigCtlD()) << "Setting short value" << val << "original" << params[0];
        }
        else if (cmd.type == 'f') {
            val.setValue(static_cast<ushort>(float(params[0].toFloat() * 255.0)));  // rigctl sends 0.0-1.0, we expect 0-255
            qInfo(logRigCtlD()) << "Setting float value" << val << "original" << params[0];
        }
        else if (cmd.type == 'g') {
            val.setValue(static_cast<uchar>(float(params[0].toFloat() / 10.0)));
        }
        else if (cmd.type == 'h') {
            val.setValue(static_cast<ushort>(float(params[0].toFloat() * 5.1)));
        }
        else if (cmd.type == 'b') {
            val.setValue(static_cast<bool>(params[0].toInt()));
        }
        else if (cmd.type == 'x') {
            freqt f;
            f.Hz = static_cast<quint64>(params[0].toFloat());
            f.VFO = activeVFO;
            f.MHzDouble = f.Hz/1000000.0;
            val.setValue(f);
            qInfo(logRigCtlD()) << "Setting" << funcString[func] << "to" << f.Hz << "on VFO" << f.VFO << "with" << params[0];
        }
        else if (cmd.type == 'm') {
            modeInfo mi;
            if (getMode(params[0],mi))
            {
                val.setValue(mi);
            }
        }
        else
        {
            qInfo(logRigCtlD()) << "Unable to parse value of type" << cmd.type;
            return -RIG_EINVAL;
        }
        queue->addUnique(priorityImmediate, queueItem(func, val));

    } else {
        // Simple get command
        cacheItem item = queue->getCache(func);
        ret = RIG_OK;
        if (cmd.type == 'b') {
            bool b = item.value.toBool();
            if (extended)
            {
                if (cmd.arg1 != NULL)
                    response.append(QString("%0: %1").arg(cmd.arg1, b));
            }
            else
            {
                response.append(QString::number(b));
            }
        }
        else if (cmd.type == 'i' || cmd.type == 's') {
            int i = item.value.toInt();
            if (extended)
            {
                if (cmd.arg1 != NULL)
                    response.append(QString("%0: %1").arg(cmd.arg1, i));
            }
            else
            {
                response.append(QString::number(i));
            }
        }
        else if (cmd.type == 'f') {
            float f = item.value.toFloat() / 255.0;
            if (extended)
            {
                if (cmd.arg1 != NULL)
                    response.append(QString("%0: %1").arg(cmd.arg1, f));
            }
            else
            {
                response.append(QString::number(f,'F',6));
            }
        }
        else if (cmd.type == 'g') {
            float f = item.value.toFloat() * 10;
            if (extended)
            {
                if (cmd.arg1 != NULL)
                    response.append(QString("%0: %1").arg(cmd.arg1, f));
            }
            else
            {
                response.append(QString::number(f,'F',6));
            }
        }
        else if (cmd.type == 'h') {
            float f = item.value.toFloat() / 5.1;
            if (extended)
            {
                if (cmd.arg1 != NULL)
                    response.append(QString("%0: %1").arg(cmd.arg1, f));
            }
            else
            {
                response.append(QString::number(f,'F',6));
            }
        }
        else if (cmd.type == 'x') { // Frequency
            freqt f = item.value.value<freqt>();
            //qInfo(logRigCtlD()) << "Got Freq" << funcString[func] << f.Hz << "on VFO" << f.VFO;
            if (extended)
            {
                if (cmd.arg1 != NULL)
                    response.append(QString("%0: %1").arg(cmd.arg1, f.Hz));
            }
            else
            {
                response.append(QString::number(f.Hz,'F',6));
            }
        }
        else if (cmd.type == 'v') { // VFO
            uchar v = item.value.value<uchar>();
            if (extended)
            {
                if (cmd.arg1 != NULL)
                    response.append(QString("%0: %1").arg(cmd.arg1, getVfoName(v)));
            }
            else
            {
                response.append(getVfoName(v));
            }
        }
        else if (cmd.type == 'm') { // Mode
            modeInfo m = item.value.value<modeInfo>();
            if (extended)
            {

                if (cmd.arg1 != NULL)
                    response.append(QString("%0: %1").arg(cmd.arg1, getMode(m)));
                if (cmd.arg2 != NULL)
                    response.append(QString("%0: %1").arg(cmd.arg2, m.pass));
            }
            else
            {
                response.append(QString("%0").arg(getMode(m)));
                response.append(QString("%0").arg(m.pass));
            }
        } else {
            qInfo(logRigCtlD()) << "Unsupported type (FIXME):" << item.value.typeName();
            ret = -RIG_EINVAL;
            return ret;
        }

    }

    return ret;
}


int rigCtlClient::getSubCommand(QStringList& response, bool extended, const commandStruct cmd, const subCommandStruct sub[], QStringList params)
{
    int ret = -RIG_EINVAL;

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
            if (sub[i].func != funcNone && rigCaps.commands.contains(sub[i].func)) {
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
                    if (sub[i].type == 'i') {
                        uchar v = static_cast<uchar>(params[1].toInt());
                        if (params[0] == "FBKIN")
                            v = (v << 1) & 0x02; // BREAKIN is not bool!
                        val.setValue(v);
                    }
                    else if (sub[i].type == 'f') {
                        val.setValue(static_cast<ushort>(float(params[1].toFloat() * 255.0)));  // rigctl sends 0.0-1.0, we expect 0-255
                        qInfo(logRigCtlD()) << "Setting float value" << params[0] << "to" << val << "original" << params[1];
                    }
                    else if (sub[i].type == 'g') {
                        val.setValue(static_cast<uchar>(float(params[1].toFloat() / 10.0)));
                    }
                    else if (sub[i].type == 'h') {
                        val.setValue(static_cast<ushort>(float(params[1].toFloat() * 5.1)));
                    }
                    else if (sub[i].type == 'b') {
                        val.setValue(static_cast<bool>(params[1].toInt()));
                    }
                    else
                    {
                        qInfo(logRigCtlD()) << "Unable to parse value of type" << sub[i].type;
                        return -RIG_EINVAL;
                    }
                    queue->addUnique(priorityImmediate, queueItem(sub[i].func, val));
                } else if (params.size() == 1){
                    // Not expecting a second argument as it is a get so dump the cache
                    cacheItem item = queue->getCache(sub[i].func);
                    int val = 0;
                    // Special situation for S-Meter
                    if (params[0] == "STRENGTH") {
                        if (rigCaps.model == model7610)
                            val = getCalibratedValue(item.value.toUInt(), IC7610_STR_CAL);
                        else if (rigCaps.model == model7850)
                            val = getCalibratedValue(item.value.toUInt(), IC7850_STR_CAL);
                        else
                            val = getCalibratedValue(item.value.toUInt(), IC7300_STR_CAL);
                        resp.append(QString("%1").arg(val));
                    }
                    else
                    {
                        if (sub[i].type == 'b') {
                            resp.append(QString("%1").arg(item.value.toBool()));
                        }
                        else if (sub[i].type == 'i') {
                            int val = item.value.toInt();
                            if (params[0] == "FBKIN")
                                val = (val >> 1) & 0x01;
                            resp.append(QString::number(val,16)); // Base 16
                        }
                        else if (sub[i].type == 'f') {
                            resp.append(QString::number(item.value.toFloat() / 255.0,'F',6));
                        }
                        else if (sub[i].type == 'g') {
                            resp.append(QString::number(item.value.toFloat() * 10.0,'F',6));
                        }
                        else if (sub[i].type == 'h') {
                            resp.append(QString::number(item.value.toFloat()/5.1,'F',6));
                        } else {
                            qInfo(logRigCtlD()) << "Unhandled:" << item.value.toUInt() << "OUT" << val;
                            ret = -RIG_EINVAL;
                        }
                    }
                    if (extended && cmd.arg2 != NULL)
                    {
                        response.append(QString("%0: %1").arg(cmd.str, params[0]));
                        response.append(QString("%0: %1").arg(cmd.arg2, resp));
                    }
                    else
                    {
                        response.append(resp);
                    }
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
    response.append(QString::number(rigCaps.rigctlModel));
    // Print something (used to be ITU region)
    response.append("0");
    // Supported RX bands (startf,endf,modes,low_power,high_power,vfo,ant)
    quint32 lowFreq = 0;
    quint32 highFreq = 0;
    for (bandType band : rigCaps.bands)
    {
        if (lowFreq == 0 || band.lowFreq < lowFreq)
            lowFreq = band.lowFreq;
        if (band.highFreq > highFreq)
            highFreq = band.highFreq;
    }
    response.append(QString("%1.000000 %2.000000 0x%3 %4 %5 0x%6 0x%7").arg(lowFreq).arg(highFreq)
                        .arg(modes, 0, 16).arg(-1).arg(-1).arg(0x16000000, 0, 16).arg(getAntennas(), 0, 16));
    response.append("0 0 0 0 0 0 0");

    if (rigCaps.hasTransmit) {
        // Supported TX bands (startf,endf,modes,low_power,high_power,vfo,ant)
        for (bandType band : rigCaps.bands)
        {
            response.append(QString("%1.000000 %2.000000 0x%3 %4 %5 0x%6 0x%7").arg(band.lowFreq).arg(band.highFreq)
                                .arg(modes, 0, 16).arg(2000).arg(100000).arg(0x16000000, 0, 16).arg(getAntennas(), 0, 16));
        }
    }
    response.append("0 0 0 0 0 0 0");
    foreach (auto step, rigCaps.steps)
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
    if(rigCaps.commands.contains(funcPreamp)) {
        foreach (auto pre, rigCaps.preamps)
        {
            if (pre.num == 0)
                continue;
            preamps.append(QString("%1 ").arg(pre.num*10));
        }
        if (preamps.endsWith(" "))
            preamps.chop(1);
    }
    else {
        preamps = "0";
    }
    response.append(preamps);

    QString attens = "";
    if(rigCaps.commands.contains(funcAttenuator)) {
        for (auto att : rigCaps.attenuators)
        {
            if (att == 0)
                continue;
            attens.append(QString("%1 ").arg(att,0,16));
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
        if (functions_str[i].func != funcNone && rigCaps.commands.contains(functions_str[i].func))
        {
            hasFuncs |= 1ULL << i;
        }
    }
    response.append(QString("0x%0").arg(hasFuncs,16,16,QChar('0')));
    response.append(QString("0x%0").arg(hasFuncs,16,16,QChar('0')));

    for (int i = 0 ; levels_str[i].str[0] != '\0'; i++)
    {
        if (levels_str[i].func != funcNone && rigCaps.commands.contains(levels_str[i].func))
        {
            hasLevels |= 1ULL << i;
        }
    }
    response.append(QString("0x%0").arg(hasLevels,16,16,QChar('0')));
    response.append(QString("0x%0").arg(hasLevels,16,16,QChar('0')));

    for (int i = 0 ; params_str[i].str[0] != '\0'; i++)
    {
        if (params_str[i].func != funcNone && rigCaps.commands.contains(params_str[i].func))
        {
            hasParams |= 1ULL << i;
        }
    }
    response.append(QString("0x%0").arg(hasParams,16,16,QChar('0')));
    response.append(QString("0x%0").arg(hasParams,16,16,QChar('0')));

    if (chkVfoEecuted) {
        response.append(QString("vfo_ops=0x%1").arg(255, 0, 16));
        response.append(QString("ptt_type=0x%1").arg(rigCaps.hasTransmit, 0, 16));
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
    response.append(QString("Caps dump for model: %1").arg(rigCaps.modelID));
    response.append(QString("Model Name:\t%1").arg(rigCaps.modelName));
    response.append(QString("Mfg Name:\tIcom"));
    response.append(QString("Backend version:\t0.1"));
    response.append(QString("Backend copyright:\t2021"));
    if (rigCaps.hasTransmit) {
        response.append(QString("Rig type:\tTransceiver"));
    }
    else
    {
        response.append(QString("Rig type:\tReceiver"));
    }
    if(rigCaps.commands.contains(funcTransceiverStatus)) {
        response.append(QString("PTT type:\tRig capable"));
    }
    response.append(QString("DCD type:\tRig capable"));
    response.append(QString("Port type:\tNetwork link"));
    return RIG_OK;
}
