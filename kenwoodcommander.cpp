#include "kenwoodcommander.h"
#include <QDebug>

#include "rigidentities.h"
#include "logcategories.h"
#include "printhex.h"

// Copyright 2017-2024 Elliott H. Liggett W6EL and Phil E. Taylor M0VSE

// This file parses data from the radio and also forms commands to the radio.

kenwoodCommander::kenwoodCommander(rigCommander* parent) : rigCommander(parent)
{

    qInfo(logRig()) << "creating instance of kenwoodCommander()";

}

kenwoodCommander::kenwoodCommander(quint8 guid[GUIDLEN], rigCommander* parent) : rigCommander(parent)
{
    qInfo(logRig()) << "creating instance of kenwoodCommander() with GUID";
    memcpy(this->guid, guid, GUIDLEN);
    // Add some commands that is a minimum for rig detection
}

kenwoodCommander::~kenwoodCommander()
{
    qInfo(logRig()) << "closing instance of kenwoodCommander()";

    emit requestRadioSelection(QList<radio_cap_packet>()); // Remove radio list.

    queue->setRigCaps(Q_NULLPTR); // Remove access to rigCaps

    qDebug(logRig()) << "Closing rig comms";
}


void kenwoodCommander::commSetup(QHash<quint8,rigInfo> rigList, quint8 rigCivAddr, QString rigSerialPort, quint32 rigBaudRate, QString vsp,quint16 tcpPort, quint8 wf)
{
    // constructor for serial connected rigs
    // As the serial connection is quite simple, no need to use a dedicated class.
    this->rigList = rigList;

    Q_UNUSED(rigCivAddr)
    Q_UNUSED(wf)

    if (serialPort != Q_NULLPTR) {
        if (serialPort->isOpen())
            serialPort->close();
        delete serialPort;
        serialPort = Q_NULLPTR;
    }

    serialPort = new QSerialPort();
    connect(serialPort, &QSerialPort::readyRead, this, &kenwoodCommander::receiveSerialData);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    connect(serialPort, SIGNAL(error(QSerialPort::SerialPortError)), this, SLOT(serialPortError(QSerialPort::SerialPortError)));
#else
    connect(serialPort, &QSerialPort::errorOccurred, this, &kenwoodCommander::serialPortError);
#endif

    serialPort->setPortName(rigSerialPort);
    serialPort->setBaudRate(rigBaudRate);
    serialPort->setStopBits(QSerialPort::OneStop);
    serialPort->setFlowControl(QSerialPort::HardwareControl);
    serialPort->setParity(QSerialPort::NoParity);

    if(serialPort->open(QIODevice::ReadWrite))
    {
        qInfo(logSerial()) << "Opened port: " << rigSerialPort << "with baud rate" << rigBaudRate;
        portConnected = true;
    } else {
        qInfo(logSerial()) << "Could not open serial port " << rigSerialPort << " , please check Radio Access under Settings.";
        emit havePortError(errorType(true, rigSerialPort, "Could not open Serial or USB port.\nPlease check Radio Access under Settings."));
        portConnected=false;
    }


    if (vsp.toLower() != "none") {
        qInfo(logRig()) << "Attempting to connect to vsp/pty:" << vsp;
        ptty = new pttyHandler(vsp,this);
        // data from the ptty to the rig:
        connect(ptty, SIGNAL(haveDataFromPort(QByteArray)), serialPort, SLOT(sendData(QByteArray)));
        // data from the rig to the ptty:
        connect(this, SIGNAL(haveDataFromRig(QByteArray)), ptty, SLOT(receiveDataFromRigToPtty(QByteArray)));
    }

    if (tcpPort > 0) {
        tcp = new tcpServer(this);
        tcp->startServer(tcpPort);
        // data from the tcp port to the rig:
        connect(tcp, SIGNAL(receiveData(QByteArray)), serialPort, SLOT(sendData(QByteArray)));
        connect(this, SIGNAL(haveDataFromRig(QByteArray)), tcp, SLOT(sendData(QByteArray)));
    }

    // Run setup common to all rig types
    commonSetup();
}

void kenwoodCommander::commSetup(QHash<quint8,rigInfo> rigList, quint8 rigCivAddr, udpPreferences prefs, audioSetup rxSetup, audioSetup txSetup, QString vsp, quint16 tcpPort)
{
    // constructor for network (LAN) connected rigs
    this->rigList = rigList;

    Q_UNUSED(rigCivAddr)
    Q_UNUSED(prefs)
    Q_UNUSED(rxSetup)
    Q_UNUSED(txSetup)
    Q_UNUSED(vsp)
    Q_UNUSED(tcpPort)

    // Run setup common to all rig types
    commonSetup();
}

void kenwoodCommander::closeComm()
{
    qInfo(logRig()) << "Closing rig comms";
    if (serialPort != Q_NULLPTR && portConnected)
    {
        if (serialPort->isOpen())
            serialPort->close();
        delete serialPort;
        serialPort = Q_NULLPTR;
    }
    portConnected=false;
}

void kenwoodCommander::commonSetup()
{
    // common elements between the two constructors go here:

    // Minimum commands we need to find rig model
    rigCaps.commands.clear();
    rigCaps.commandsReverse.clear();
    rigCaps.commands.insert(funcTransceiverId,funcType(funcTransceiverId, QString("Transceiver ID"),"ID",0,0,false,true,false,3));
    rigCaps.commandsReverse.insert(QByteArrayLiteral("ID"),funcTransceiverId);

    connect(queue,SIGNAL(haveCommand(funcs,QVariant,uchar)),this,SLOT(receiveCommand(funcs,QVariant,uchar)));

    emit commReady();
}



void kenwoodCommander::process()
{
    // new thread enters here. Do nothing but do check for errors.
}


void kenwoodCommander::receiveBaudRate(quint32 baudrate)
{
    Q_UNUSED(baudrate)
}

void kenwoodCommander::setPTTType(pttType_t ptt)
{
    Q_UNUSED(ptt)
}

void kenwoodCommander::setRigID(quint8 rigID)
{
    Q_UNUSED(rigID)
}

void kenwoodCommander::setCIVAddr(quint8 civAddr)
{
    Q_UNUSED(civAddr)
}

void kenwoodCommander::setAfGain(quint8 level)
{
    Q_UNUSED(level)
}

void kenwoodCommander::powerOn()
{
    qDebug(logRig()) << "Power ON command in kenwoodCommander to be sent to rig: ";
    QByteArray d;
    quint8 cmd = 1;

    if (getCommand(funcPowerControl,d,cmd,0).cmd == funcNone)
    {
        d.append("PS");
    }

    d.append(QString("%0;").arg(cmd).toLatin1());
    QMutexLocker locker(&serialMutex);
    serialPort->write(d);
    lastSentCommand = d;
}

void kenwoodCommander::powerOff()
{
    qDebug(logRig()) << "Power OFF command in kenwoodCommander to be sent to rig: ";
    QByteArray d;
    quint8 cmd = 0;
    if (getCommand(funcPowerControl,d,cmd,0).cmd == funcNone)
    {
        d.append("PS");
    }

    d.append(QString("%0;").arg(cmd).toLatin1());
    QMutexLocker locker(&serialMutex);
    serialPort->write(d);
    lastSentCommand = d;
}




void kenwoodCommander::handleNewData(const QByteArray& data)
{
    emit haveDataForServer(data);
}


funcType kenwoodCommander::getCommand(funcs func, QByteArray &payload, int value, uchar receiver)
{
    funcType cmd;

    // Value is set to INT_MIN by default as this should be outside any "real" values
    auto it = this->rigCaps.commands.find(func);
    if (it != this->rigCaps.commands.end())
    {
        if (value == INT_MIN || (value>=it.value().minVal && value <= it.value().maxVal))
        {
            /*
            if (value == INT_MIN)
                qDebug(logRig()) << QString("%0 with no value (get)").arg(funcString[func]);
            else
                qDebug(logRig()) << QString("%0 with value %1 (Range: %2-%3)").arg(funcString[func]).arg(value).arg(it.value().minVal).arg(it.value().maxVal);
            */
            cmd = it.value();
            payload.append(cmd.data);
        }
        else if (value != INT_MIN)
        {
            qDebug(logRig()) << QString("Value %0 for %1 is outside of allowed range (%2-%3)").arg(value).arg(funcString[func]).arg(it.value().minVal).arg(it.value().maxVal);
        }
    } else {
        // Don't try this command again as the rig doesn't support it!
        qDebug(logRig()) << "Removing unsupported command from queue" << funcString[func] << "VFO" << receiver;
        this->queue->del(func,receiver);
    }
    return cmd;
}

void kenwoodCommander::receiveSerialData()
{
    const QByteArray in = serialPort->readAll();
    parseData(in);
    emit haveDataFromRig(in);
}


void kenwoodCommander::parseData(QByteArray data)
{
    funcs func = funcNone;
    QList<QByteArray> commands = data.split(';');

    for (auto &d: commands) {
        if (d.isEmpty())
            continue;
        uchar receiver = 0; // Used for Dual/RX

        int count = 0;
        for (int i=d.length();i>0;i--)
        {
            auto it = rigCaps.commandsReverse.find(d.left(i));
            if (it != rigCaps.commandsReverse.end())
            {
                func = it.value();
                count = i;
                break;
            }
        }

        d.remove(0,count);
        if (!rigCaps.commands.contains(func)) {
            // Don't warn if we haven't received rigCaps yet
            if (haveRigCaps)
                qInfo(logRig()) << "Unsupported command received from rig" << d << "Check rig file";
            return;
        }

        QVector<memParserFormat> memParser;
        QVariant value;
        switch (func)
        {
        case funcUnselectedFreq:
        case funcSelectedFreq:
        {
            freqt f;
            bool ok=false;
            f.Hz = d.toULongLong(&ok);
            if (ok)
            {
                f.MHzDouble = f.Hz/1000000.0;
                value.setValue(f);
            }
            break;
        }
        case funcUnselectedMode:
        case funcSelectedMode:
        {
            for (auto& m: rigCaps.modes)
            {
                if (m.reg == uchar(d.at(0) - NUMTOASCII))
                {
                    value.setValue(m);
                    break;
                }
            }
            break;
        }
        case funcDataMode:
        {
            modeInfo m;
            m.data = uchar(d.at(0) - NUMTOASCII);
            value.setValue(m);
            break;
        }
        case funcIFFilter:
        {
            modeInfo m;
            m.filter = uchar(d.at(0) - NUMTOASCII);
            value.setValue(m);
            break;
        }
        case funcAntenna:
            antennaInfo a;
            a.antenna = uchar(d.at(0) - NUMTOASCII);
            a.rx = uchar(d.at(1) - NUMTOASCII);
            value.setValue(a);
            break;
        case funcDATAOffMod:
        case funcDATA1Mod:
        {
            for (auto &r: rigCaps.inputs)
            {
                if (r.reg == (d.at(0) - NUMTOASCII))
                {
                    value.setValue(r);
                    break;
                }
            }
            break;
        }
        case funcTransceiverStatus:
            // This is a response to the IF command which contains a wealth of information
            // We could use this for various things if we want.
            // It doesn't work when in data mode though!
            value.setValue(bool(d.at(26) - NUMTOASCII));
            isTransmitting = value.toBool();
            break;
        case funcSetTransmit:
            func = funcTransceiverStatus;
            value.setValue(bool(true));
            isTransmitting = true;
            break;
        case funcSetReceive:
            func = funcTransceiverStatus;
            value.setValue(bool(false));
            isTransmitting = false;
            break;
        case funcPBTInner:
        case funcPBTOuter:
            value.setValue<uchar>(d.toUShort() * 19);
            break;
        case funcFilterWidth:
            value.setValue<ushort>(d.toUShort());
            break;
        case funcMeterType:
            break;
        case funcAttenuator:
            value.setValue<uchar>(d.at(1) - NUMTOASCII);
            break;
        case funcRITFreq:
            break;
        case funcMonitor:
        case funcRitStatus:
        case funcCompressor:
        case funcVox:
            value.setValue<bool>(d.at(0) - NUMTOASCII);
            break;
        case funcCompressorLevel:
            value.setValue<uchar>(d.mid(3,3).toUShort());
            break;
        case funcPreamp:
        case funcNoiseBlanker:
        case funcNoiseReduction:
        case funcAGC:
        case funcPowerControl:
            value.setValue<uchar>(d.at(0) - NUMTOASCII);
            break;
        case funcAGCTimeConstant:
        case funcMemorySelect:
        case funcAfGain:
        case funcRfGain:
        case funcRFPower:
        case funcSquelch:
        case funcMonitorGain:
        case funcKeySpeed:
            value.setValue<uchar>(d.toUShort());
            break;
        case funcSMeter:
            if (isTransmitting)
                func = funcPowerMeter;

        case funcSWRMeter:
        case funcALCMeter:
        case funcCompMeter:
            // TS-590 uses 0-30 for meters (others may be different?), Icom uses 0-255.
            value.setValue<uchar>(d.toUShort()*8);
            break;
        case funcMemoryContents:
        // Contains the contents of the rig memory
        {
            qDebug() << "Received mem:" << d;
            memoryType mem;
            if (parseMemory(d,&rigCaps.memParser,&mem)) {
                value.setValue(mem);
            }
            break;
        }
        case funcTransceiverId:
        {
            quint8 model = uchar(d.toUShort());
            value.setValue(model);
            if (!this->rigCaps.modelID || model != this->rigCaps.modelID)
            {
                if (this->rigList.contains(model))
                {
                    this->rigCaps.modelID = this->rigList.find(model).key();
                }
                qInfo(logRig()) << QString("Have new rig ID: %0").arg(this->rigCaps.modelID);
                determineRigCaps();
            }
            break;
        }
        // These contain freq/mode/data information, might be useful?
        case funcRXFreqAndMode:
        case funcTXFreqAndMode:
            break;
        case funcVFOASelect:
        case funcVFOBSelect:
        case funcMemoryMode:
            // Do we need to do anything with this?
            break;
        case funcFA:
            qInfo(logRig()) << "Error received from rig. Last command:" << lastSentCommand << "data:" << d;
            break;
        default:
            qWarning(logRig()).noquote() << "Unhandled command received from rig:" << funcString[func] << "value:" << d.mid(0,10);
            break;
        }

        if(func != funcScopeWaveData
            && func != funcSMeter
            && func != funcAbsoluteMeter
            && func != funcCenterMeter
            && func != funcPowerMeter
            && func != funcSWRMeter
            && func != funcALCMeter
            && func != funcCompMeter
            && func != funcVdMeter
            && func != funcIdMeter)
        {
            // We do not log spectrum and meter data,
            // as they tend to clog up any useful logging.
            qDebug(logRigTraffic()) << QString("Received from radio: %0").arg(funcString[func]);
            printHexNow(d, logRigTraffic());
        }

        if (value.isValid() && queue != Q_NULLPTR) {
            queue->receiveValue(func,value,receiver);
        }
    }
}

bool kenwoodCommander::parseMemory(QByteArray d,QVector<memParserFormat>* memParser, memoryType* mem)
{
    // Parse the memory entry into a memoryType, set some defaults so we don't get an unitialized warning.
    mem->frequency.Hz=0;
    mem->frequency.VFO=activeVFO;
    mem->frequency.MHzDouble=0.0;
    mem->frequencyB = mem->frequency;
    mem->duplexOffset = mem->frequency;
    mem->duplexOffsetB = mem->frequency;
    mem->scan = 0x0;
    memset(mem->UR, 0x0, sizeof(mem->UR));
    memset(mem->URB, 0x0, sizeof(mem->UR));
    memset(mem->R1, 0x0, sizeof(mem->R1));
    memset(mem->R1B, 0x0, sizeof(mem->R1B));
    memset(mem->R2, 0x0, sizeof(mem->R2));
    memset(mem->R2B, 0x0, sizeof(mem->R2B));
    memset(mem->name, 0x0, sizeof(mem->name));

    // We need to add 2 characters so that the parser works!
    d.insert(0,"**");
    for (auto &parse: *memParser) {
        // non-existant memory is short so send what we have so far.
        if (d.size() < (parse.pos+1+parse.len) && parse.spec != 'Z') {
            return true;
        }
        QByteArray data = d.mid(parse.pos+1,parse.len);

        switch (parse.spec)
        {
        case 'a':
            mem->group = data.left(parse.len).toInt();
            break;
        case 'b':
            mem->channel = data.left(parse.len).toInt();
            break;
        case 'c':
            mem->scan = data.left(parse.len).toInt();
            break;
        case 'd':
            mem->split = data.left(parse.len).toInt();
            break;
        case 'e':
            mem->vfo=data.left(parse.len).toInt();
            break;
        case 'E':
            mem->vfoB=data.left(parse.len).toInt();
            break;
        case 'f':
            mem->frequency.Hz = data.left(parse.len).toLongLong();
            break;
        case 'F':
            mem->frequencyB.Hz =data.left(parse.len).toLongLong();
            break;
        case 'g':
            mem->mode=data.left(parse.len).toInt();
            break;
        case 'G':
            mem->modeB=data.left(parse.len).toInt();
            break;
        case 'h':
            mem->filter=data.left(parse.len).toInt();
            break;
        case 'H':
            mem->filterB=data.left(parse.len).toInt();
            break;
        case 'i': // single datamode
            mem->datamode=data.left(parse.len).toInt();
            break;
        case 'I': // single datamode
            mem->datamodeB=data.left(parse.len).toInt();
            break;
        case 'l': // tonemode
            mem->tonemode=data.left(parse.len).toInt();
            break;
        case 'L': // tonemode
            mem->tonemodeB=data.left(parse.len).toInt();
            break;
        case 'm':
            mem->dsql = data.left(parse.len).toInt();
            break;
        case 'M':
            mem->dsqlB = data.left(parse.len).toInt();
            break;
        case 'n':
            mem->tone = ctcssTones[data.left(parse.len).toInt()];
            break;
        case 'N':
            mem->toneB = ctcssTones[data.left(parse.len).toInt()];
            break;
        case 'o':
            mem->tsql = ctcssTones[data.left(parse.len).toInt()];
            break;
        case 'O':
            mem->tsqlB = ctcssTones[data.left(parse.len).toInt()];
            break;
        case 'p':
            mem->dtcsp = data.left(parse.len).toInt();
            break;
        case 'P':
            mem->dtcspB = data.left(parse.len).toInt();
            break;
        case 'q':
            mem->dtcs = data.left(parse.len).toInt();
            break;
        case 'Q':
            mem->dtcsB = data.left(parse.len).toInt();
            break;
        case 'r':
            mem->dvsql = data.left(parse.len).toInt();
            break;
        case 'R':
            mem->dvsqlB = data.left(parse.len).toInt();
            break;
        case 's':
            mem->duplexOffset.Hz = data.left(parse.len).toLong();
            break;
        case 'S':
            mem->duplexOffsetB.Hz = data.left(parse.len).toLong();
            break;
        case 't':
            memcpy(mem->UR,data.data(),qMin(int(sizeof mem->UR),data.size()));
            break;
        case 'u':
            memcpy(mem->R1,data.data(),qMin(int(sizeof mem->R1),data.size()));
            break;
        case 'U':
            memcpy(mem->R1B,data.data(),qMin(int(sizeof mem->R1B),data.size()));
            break;
        case 'v':
            memcpy(mem->R2,data.data(),qMin(int(sizeof mem->R2),data.size()));
            break;
        case 'V':
            memcpy(mem->R2B,data.data(),qMin(int(sizeof mem->R2B),data.size()));
            break;
        case 'z':
            memcpy(mem->name,data.data(),qMin(int(sizeof mem->name),data.size()));
            break;
        default:
            qInfo() << "Parser didn't match!" << "spec:" << parse.spec << "pos:" << parse.pos << "len" << parse.len;
            break;
        }
    }

    return true;
}

void kenwoodCommander::determineRigCaps()
{
    // First clear all of the current settings
    rigCaps.preamps.clear();
    rigCaps.attenuators.clear();
    rigCaps.inputs.clear();
    rigCaps.scopeCenterSpans.clear();
    rigCaps.bands.clear();
    rigCaps.modes.clear();
    rigCaps.commands.clear();
    rigCaps.commandsReverse.clear();
    rigCaps.antennas.clear();
    rigCaps.filters.clear();
    rigCaps.steps.clear();
    rigCaps.memParser.clear();
    rigCaps.satParser.clear();
    rigCaps.periodic.clear();
    // modelID should already be set!
    while (!rigList.contains(rigCaps.modelID))
    {
        if (!rigCaps.modelID) {
            qWarning(logRig()) << "No default rig definition found, cannot continue (sorry!)";
            return;
        }
        // Unknown rig, load default
        qInfo(logRig()) << QString("No rig definition found for CI-V address: 0x%0, using defaults (some functions may not be available)").arg(rigCaps.modelID,2,16);
        rigCaps.modelID=0;
    }
    rigCaps.filename = rigList.find(rigCaps.modelID).value().path;
    QSettings* settings = new QSettings(rigCaps.filename, QSettings::Format::IniFormat);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    settings->setIniCodec("UTF-8");
#endif
    if (!settings->childGroups().contains("Rig"))
    {
        qWarning(logRig()) << rigCaps.filename << "Cannot be loaded!";
        return;
    }
    settings->beginGroup("Rig");
    // Populate rigcaps

    rigCaps.manufacturer = manufKenwood;
    rigCaps.modelName = settings->value("Model", "").toString();
    rigCaps.rigctlModel = settings->value("RigCtlDModel", 0).toInt();
    qInfo(logRig()) << QString("Loading Rig: %0 from %1").arg(rigCaps.modelName,rigCaps.filename);

    rigCaps.numReceiver = settings->value("NumberOfReceivers",1).toUInt();
    rigCaps.numVFO = settings->value("NumberOfVFOs",1).toUInt();
    rigCaps.spectSeqMax = settings->value("SpectrumSeqMax",0).toUInt();
    rigCaps.spectAmpMax = settings->value("SpectrumAmpMax",0).toUInt();
    rigCaps.spectLenMax = settings->value("SpectrumLenMax",0).toUInt();

    rigCaps.hasSpectrum = settings->value("HasSpectrum",false).toBool();
    rigCaps.hasLan = settings->value("HasLAN",false).toBool();
    rigCaps.hasEthernet = settings->value("HasEthernet",false).toBool();
    rigCaps.hasWiFi = settings->value("HasWiFi",false).toBool();
    rigCaps.hasQuickSplitCommand = settings->value("HasQuickSplit",false).toBool();
    rigCaps.hasDD = settings->value("HasDD",false).toBool();
    rigCaps.hasDV = settings->value("HasDV",false).toBool();
    rigCaps.hasTransmit = settings->value("HasTransmit",false).toBool();
    rigCaps.hasFDcomms = settings->value("HasFDComms",false).toBool();
    rigCaps.hasCommand29 = settings->value("HasCommand29",false).toBool();

    rigCaps.memGroups = settings->value("MemGroups",0).toUInt();
    rigCaps.memories = settings->value("Memories",0).toUInt();
    rigCaps.memStart = settings->value("MemStart",1).toUInt();
    rigCaps.memFormat = settings->value("MemFormat","").toString();
    rigCaps.satMemories = settings->value("SatMemories",0).toUInt();
    rigCaps.satFormat = settings->value("SatFormat","").toString();

    // If rig doesn't have FD comms, tell the commhandler early.
    emit setHalfDuplex(!rigCaps.hasFDcomms);

    // Temporary QHash to hold the function string lookup // I would still like to find a better way of doing this!
    QHash<QString, funcs> funcsLookup;
    for (int i=0;i<NUMFUNCS;i++)
    {
        if (!funcString[i].startsWith("+")) {
            funcsLookup.insert(funcString[i].toUpper(), funcs(i));
        }
    }

    int numCommands = settings->beginReadArray("Commands");
    if (numCommands == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numCommands; c++)
        {
            settings->setArrayIndex(c);

            if (funcsLookup.contains(settings->value("Type", "****").toString().toUpper()))
            {
                funcs func = funcsLookup.find(settings->value("Type", "").toString().toUpper()).value();
                rigCaps.commands.insert(func, funcType(func, funcString[int(func)],
                                                       settings->value("String", "").toByteArray(),
                                                       settings->value("Min", 0).toInt(NULL), settings->value("Max", 0).toInt(NULL),
                                                       settings->value("Command29",false).toBool(),
                                                       settings->value("GetCommand",true).toBool(),
                                                       settings->value("SetCommand",true).toBool(),
                                                       settings->value("Bytes",true).toInt()));

                rigCaps.commandsReverse.insert(settings->value("String", "").toByteArray(),func);
            } else {
                qWarning(logRig()) << "**** Function" << settings->value("Type", "").toString() << "Not Found, rig file may be out of date?";
            }
        }
        settings->endArray();
    }

    int numPeriodic = settings->beginReadArray("Periodic");
    if (numPeriodic == 0){
        qWarning(logRig())<< "No periodic commands defined, please check rigcaps file";
        settings->endArray();
    } else {
        for (int c=0; c < numPeriodic; c++)
        {
            settings->setArrayIndex(c);

            if(funcsLookup.contains(settings->value("Command", "****").toString().toUpper())) {
                funcs func = funcsLookup.find(settings->value("Command", "").toString().toUpper()).value();
                if (!rigCaps.commands.contains(func)) {
                    qWarning(logRig()) << "Cannot find periodic command" << settings->value("Command", "").toString() << "in rigcaps, ignoring";
                } else {
                    rigCaps.periodic.append(periodicType(func,
                                                         settings->value("Priority","").toString(),priorityMap[settings->value("Priority","").toString()],
                                                         settings->value("VFO",-1).toInt()));
                }
            }
        }
        settings->endArray();
    }

    int numModes = settings->beginReadArray("Modes");
    if (numModes == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numModes; c++)
        {
            settings->setArrayIndex(c);
            rigCaps.modes.push_back(modeInfo(rigMode_t(settings->value("Num", 0).toUInt()),
                                             settings->value("Reg", 0).toString().toUInt(), settings->value("Name", "").toString(), settings->value("Min", 0).toInt(),
                                             settings->value("Max", 0).toInt()));
        }
        settings->endArray();
    }

    int numSpans = settings->beginReadArray("Spans");
    if (numSpans == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numSpans; c++)
        {
            settings->setArrayIndex(c);
            rigCaps.scopeCenterSpans.push_back(centerSpanData(centerSpansType(settings->value("Num", 0).toUInt()),
                                                              settings->value("Name", "").toString(), settings->value("Freq", 0).toUInt()));
        }
        settings->endArray();
    }

    int numInputs = settings->beginReadArray("Inputs");
    if (numInputs == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numInputs; c++)
        {
            settings->setArrayIndex(c);
            rigCaps.inputs.append(rigInput(inputTypes(settings->value("Num", 0).toUInt()),
                                           settings->value("Reg", 0).toString().toInt(),settings->value("Name", "").toString()));
        }
        settings->endArray();
    }

    int numSteps = settings->beginReadArray("Tuning Steps");
    if (numSteps == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numSteps; c++)
        {
            settings->setArrayIndex(c);
            rigCaps.steps.push_back(stepType(settings->value("Num", 0).toString().toUInt(),
                                             settings->value("Name", "").toString(),settings->value("Hz", 0ULL).toULongLong()));
        }
        settings->endArray();
    }

    int numPreamps = settings->beginReadArray("Preamps");
    if (numPreamps == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numPreamps; c++)
        {
            settings->setArrayIndex(c);
            rigCaps.preamps.push_back(genericType(settings->value("Num", 0).toString().toUInt(), settings->value("Name", 0).toString()));
        }
        settings->endArray();
    }

    int numAntennas = settings->beginReadArray("Antennas");
    if (numAntennas == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numAntennas; c++)
        {
            settings->setArrayIndex(c);
            rigCaps.antennas.push_back(genericType(settings->value("Num", 0).toString().toUInt(), settings->value("Name", 0).toString()));
        }
        settings->endArray();
    }

    int numAttenuators = settings->beginReadArray("Attenuators");
    if (numAttenuators == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numAttenuators; c++)
        {
            settings->setArrayIndex(c);
            qDebug(logRig()) << "** GOT ATTENUATOR" << settings->value("dB", 0).toString().toUInt();
            rigCaps.attenuators.push_back((quint8)settings->value("dB", 0).toString().toUInt());
        }
        settings->endArray();
    }

    int numFilters = settings->beginReadArray("Filters");
    if (numFilters == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numFilters; c++)
        {
            settings->setArrayIndex(c);
            rigCaps.filters.push_back(filterType(settings->value("Num", 0).toString().toUInt(), settings->value("Name", "").toString(), settings->value("Modes", 0).toUInt()));
        }
        settings->endArray();
    }

    int numBands = settings->beginReadArray("Bands");
    if (numBands == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numBands; c++)
        {
            settings->setArrayIndex(c);
            QString region = settings->value("Region","").toString();
            availableBands band =  availableBands(settings->value("Num", 0).toInt());
            quint64 start = settings->value("Start", 0ULL).toULongLong();
            quint64 end = settings->value("End", 0ULL).toULongLong();
            uchar bsr = static_cast<uchar>(settings->value("BSR", 0).toInt());
            double range = settings->value("Range", 0.0).toDouble();
            int memGroup = settings->value("MemoryGroup", -1).toInt();
            char bytes = settings->value("Bytes", 5).toInt();
            bool ants = settings->value("Antennas",true).toBool();
            QColor color(settings->value("Color", "#00000000").toString()); // Default color should be none!
            QString name(settings->value("Name", "None").toString());
            float power = settings->value("Power", 0.0f).toFloat();
            rigCaps.bands.push_back(bandType(region,band,bsr,start,end,range,memGroup,bytes,ants,power,color,name));
            rigCaps.bsr[band] = bsr;
            qDebug(logRig()) << "Adding Band " << band << "Start" << start << "End" << end << "BSR" << QString::number(bsr,16);
        }
        settings->endArray();
    }

    settings->endGroup();
    delete settings;


    // Setup memory formats.
    static QRegularExpression memFmtEx("%(?<flags>[-+#0])?(?<pos>\\d+|\\*)?(?:\\.(?<width>\\d+|\\*))?(?<spec>[abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ+])");
    QRegularExpressionMatchIterator i = memFmtEx.globalMatch(rigCaps.memFormat);
    while (i.hasNext()) {
        QRegularExpressionMatch qmatch = i.next();

#if (QT_VERSION >= QT_VERSION_CHECK(6,0,0))
        if (qmatch.hasCaptured("spec") && qmatch.hasCaptured("pos") && qmatch.hasCaptured("width")) {
#endif
            rigCaps.memParser.append(memParserFormat(qmatch.captured("spec").at(0).toLatin1(),qmatch.captured("pos").toInt(),qmatch.captured("width").toInt()));
        }
#if (QT_VERSION >= QT_VERSION_CHECK(6,0,0))
        }
#endif

    QRegularExpressionMatchIterator i2 = memFmtEx.globalMatch(rigCaps.satFormat);

    while (i2.hasNext()) {
        QRegularExpressionMatch qmatch = i2.next();
#if (QT_VERSION >= QT_VERSION_CHECK(6,0,0))
        if (qmatch.hasCaptured("spec") && qmatch.hasCaptured("pos") && qmatch.hasCaptured("width")) {
#endif
            rigCaps.satParser.append(memParserFormat(qmatch.captured("spec").at(0).toLatin1(),qmatch.captured("pos").toInt(),qmatch.captured("width").toInt()));
#if (QT_VERSION >= QT_VERSION_CHECK(6,0,0))
            }
#endif
    }

    // Copy received guid so we can recognise this radio.
    memcpy(rigCaps.guid, this->guid, GUIDLEN);

    haveRigCaps = true;
    queue->setRigCaps(&rigCaps);

    // Also signal that a radio is connected to the radio status window
    if (!usingNativeLAN) {
        QList<radio_cap_packet>radios;
        radio_cap_packet r;
        r.civ = rigCaps.modelID;
        r.baudrate = qToBigEndian(rigCaps.baudRate);
#ifdef Q_OS_WINDOWS
        strncpy_s(r.name,rigCaps.modelName.toLocal8Bit(),sizeof(r.name)-1);
#else
        strncpy(r.name,rigCaps.modelName.toLocal8Bit(),sizeof(r.name)-1);
#endif
        radios.append(r);
        emit requestRadioSelection(radios);
        emit setRadioUsage(0, true, true, QString("<Local>"), QString("127.0.0.1"));
    }

    qDebug(logRig()) << "---Rig FOUND" << rigCaps.modelName;
    emit discoveredRigID(rigCaps);
    emit haveRigID(rigCaps);
}

void kenwoodCommander::receiveCommand(funcs func, QVariant value, uchar receiver)
{
    QByteArray payload;
    int val=INT_MIN;

    if (func == funcSWRMeter || func == funcCompMeter || func == funcALCMeter)
    {
        // Cannot query for these meters, but they will be sent automatically on TX
        return;
    }

    if (func == funcSelectVFO) {
        // Special command
        vfo_t v = value.value<vfo_t>();
        func = (v == vfoA)?funcVFOASelect:(v == vfoB)?funcVFOBSelect:funcNone;
        value.clear();
        val = INT_MIN;
    }

    // The transceiverStatus command cannot be used to set PTT, this is handled by TX/RX commands.
    if (value.isValid() && func==funcTransceiverStatus)
    {
        if (value.value<bool>())
        {
            // Are we in data mode?
            value.setValue<uchar>(queue->getCache(funcDataMode).value.value<modeInfo>().data);
            func = funcSetTransmit;
        }
        else
        {
            value.clear();
            func = funcSetReceive;
        }
    }

    funcType cmd = getCommand(func,payload,val,receiver);

    if (cmd.cmd != funcNone) {
        if (value.isValid())
        {
            // This is a SET command
            if (!cmd.setCmd) {
                qDebug(logRig()) << "Removing unsupported set command from queue" << funcString[func] << "VFO" << receiver;
                queue->del(func,receiver);
                return;
            }
            else if (!strcmp(value.typeName(),"QString"))
            {
                QString text = value.value<QString>();
                if (func == funcSendCW)
                {
                    QByteArray textData = text.toLocal8Bit();
                    quint8 p=0;
                    for(int c=0; c < textData.length(); c++)
                    {
                        p = textData.at(c);
                        if( ( (p >= 0x30) && (p <= 0x39) ) ||
                            ( (p >= 0x41) && (p <= 0x5A) ) ||
                            ( (p >= 0x61) && (p <= 0x7A) ) ||
                            (p==0x2F) || (p==0x3F) || (p==0x2E) ||
                            (p==0x2D) || (p==0x2C) || (p==0x3A) ||
                            (p==0x27) || (p==0x28) || (p==0x29) ||
                            (p==0x3D) || (p==0x2B) || (p==0x22) ||
                            (p==0x40) || (p==0x20))
                        {
                            // Allowed character, continue
                        } else {
                            qWarning(logRig()) << "Invalid character detected in CW message at position " << c << ", the character is " << text.at(c);
                            textData[c] = 0x3F; // "?"
                        }
                    }
                    if (textData.isEmpty())
                    {
                        payload.append(QString("0").leftJustified(cmd.bytes, QChar(' ')).toLatin1());
                    } else {
                        payload.append(" ");
                        payload.append(textData.leftJustified(cmd.bytes, QChar(' ').toLatin1()));
                    }
                    qDebug(logRig()) << "Sending CW: payload:" << payload.toHex(' ');
                }
            }
            else if(!strcmp(value.typeName(),"freqt"))
            {
                payload.append(QString::number(value.value<freqt>().Hz).rightJustified(cmd.bytes, QChar('0')).toLatin1());
            }
            else if(!strcmp(value.typeName(),"bool") || !strcmp(value.typeName(),"uchar"))
            {
                // This is a simple number
                payload.append(QString::number(value.value<uchar>()).rightJustified(cmd.bytes, QChar('0')).toLatin1());
            }
            else if(!strcmp(value.typeName(),"uint"))
            {
                if (func == funcMemoryContents) {
                    payload.append(QString::number(quint16(value.value<uint>() & 0xffff)).rightJustified(cmd.bytes, QChar('0')).toLatin1());
                } else {
                    payload.append(QString::number(value.value<uint>()).rightJustified(cmd.bytes, QChar('0')).toLatin1());
                }
            }
            else if(!strcmp(value.typeName(),"ushort") )
            {
                if (func == funcPBTInner || func == funcPBTOuter) {
                    ushort v=ushort(value.value<ushort>()/19);
                    payload.append(QString::number(v).rightJustified(cmd.bytes, QChar('0')).toLatin1());
                } else {
                    payload.append(QString::number(value.value<ushort>()).rightJustified(cmd.bytes, QChar('0')).toLatin1());
                }
            }
            else if(!strcmp(value.typeName(),"modeInfo"))
            {
                if (cmd.cmd == funcSelectedMode)
                {
                    modeInfo m = value.value<modeInfo>();
                    payload.append(QString("%0").arg(m.reg).toLatin1());
                    if (m.data != 0xff && m.mk != modeCW &&  value.value<modeInfo>().mk != modeCW_R)
                        queue->add(priorityImmediate,queueItem(funcDataMode,value,false,receiver));
                    if (m.filter != 0xff)
                        queue->add(priorityImmediate,queueItem(funcIFFilter,value,false,receiver));
                }
                else if (cmd.cmd == funcDataMode)
                {
                    payload.append(QString::number(value.value<modeInfo>().data).toLatin1());
                }
                else if (cmd.cmd == funcIFFilter)
                {
                    payload.append(QString::number(value.value<modeInfo>().filter).toLatin1());
                }
            }
            else if(!strcmp(value.typeName(),"bandStackType"))
            {
                bandStackType b = value.value<bandStackType>();
                payload.append(QString::number(b.regCode).toLatin1());

            }
            else if(!strcmp(value.typeName(),"antennaInfo"))
            {
                antennaInfo a = value.value<antennaInfo>();
                payload.append(QString("%0%1%2").arg(a.antenna).arg(uchar(a.rx)).arg(9).toLatin1());
            }
            else if(!strcmp(value.typeName(),"rigInput"))
            {
                payload.append(QString::number(value.value<rigInput>().reg).toLatin1());
            }
            else if (!strcmp(value.typeName(),"memoryType")) {

                // We need to iterate through memParser to build the correct format
                bool finished=false;
                QVector<memParserFormat> parser;
                memoryType mem = value.value<memoryType>();

                if (mem.sat)
                {
                    parser = rigCaps.satParser;
                }
                else
                {
                    parser = rigCaps.memParser;
                }

                int end=1;
                for (auto &parse: parser) {
                    if (parse.pos > end)
                    {
                        // Insert padding
                        payload.append(QString().leftJustified(parse.pos-end, '0').toLatin1());
                    }
                    end = parse.pos+parse.len;
                    switch (parse.spec)
                    {
                    case 'a':
                        payload.append(QString::number(mem.group).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 'b':
                        payload.append(QString::number(mem.channel).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 'c':
                        payload.append(QString::number(mem.scan).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 'C':
                        payload.append(QString::number(mem.scan).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 'd':
                        payload.append(QString::number(mem.split).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 'e':
                        payload.append(QString::number(mem.vfo).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 'E':
                        payload.append(QString::number(mem.vfoB).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 'f':
                        payload.append(QString::number(mem.frequency.Hz).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 'F':
                        payload.append(QString::number(mem.frequencyB.Hz).rightJustified(parse.len, QChar('0')).toLatin1());
                    case 'g':
                        payload.append(QString::number(mem.mode).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 'G':
                        payload.append(QString::number(mem.modeB).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 'h':
                        payload.append(QString::number(mem.filter).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 'H':
                        payload.append(QString::number(mem.filterB).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 'i': // single datamode
                        payload.append(QString::number(mem.datamode).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 'I':
                        payload.append(QString::number(mem.datamode).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 'l': // tonemode
                        payload.append(QString::number(mem.tonemode).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 'L':
                        payload.append(QString::number(mem.tonemodeB).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 'm':
                        payload.append(QString::number(mem.dsql).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 'M':
                        payload.append(QString::number(mem.dsqlB).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 'n':
                    {
                        for (int i=0;i<42;i++)
                        {
                            if (ctcssTones[i]==mem.tone) {
                                payload.append(QString::number(i).rightJustified(parse.len, QChar('0')).toLatin1());
                                break;
                            }
                        }
                        break;
                    }
                    case 'N':
                        for (int i=0;i<42;i++)
                        {
                            if (ctcssTones[i]==mem.toneB) {
                                payload.append(QString::number(i).rightJustified(parse.len, QChar('0')).toLatin1());
                                break;
                            }
                        }
                        break;
                    case 'o':
                        for (int i=0;i<42;i++)
                        {
                            if (ctcssTones[i]==mem.tsql) {
                                payload.append(QString::number(i).rightJustified(parse.len, QChar('0')).toLatin1());
                                break;
                            }
                        }
                        break;
                    case 'O':
                        for (int i=0;i<42;i++)
                        {
                            if (ctcssTones[i]==mem.tsqlB) {
                                payload.append(QString::number(i).rightJustified(parse.len, QChar('0')).toLatin1());
                                break;
                            }
                        }
                        break;
                    case 'q':
                        payload.append(QString::number(mem.dtcs).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 'Q':
                        payload.append(QString::number(mem.dtcsB).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 'r':
                        payload.append(QString::number(mem.dvsql).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 'R':
                        payload.append(QString::number(mem.dvsqlB).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 's':
                        payload.append(QString::number(mem.duplexOffset.Hz).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 'S':
                        payload.append(QString::number(mem.duplexOffsetB.Hz).rightJustified(parse.len, QChar('0')).toLatin1());
                        break;
                    case 't':
                        payload.append(QByteArray(mem.UR).leftJustified(parse.len,' ',true));
                        break;
                    case 'T':
                        payload.append(QByteArray(mem.URB).leftJustified(parse.len,' ',true));
                        break;
                    case 'u':
                        payload.append(QByteArray(mem.R1).leftJustified(parse.len,' ',true));
                        break;
                    case 'U':
                        payload.append(QByteArray(mem.R1B).leftJustified(parse.len,' ',true));
                        break;
                    case 'v':
                        payload.append(QByteArray(mem.R2).leftJustified(parse.len,' ',true));
                        break;
                    case 'V':
                        payload.append(QByteArray(mem.R2B).leftJustified(parse.len,' ',true));
                        break;
                    case 'z':
                        payload.append(QByteArray(mem.name).leftJustified(parse.len,' ',true));
                        break;
                    default:
                        break;
                    }
                    if (finished)
                        break;
                }
                qDebug(logRig()) << "Writing memory location:" << payload.toHex(' ');
            }
            //qInfo() << "Sending set:" << funcString[cmd.cmd] << "Payload:" << payload;

        } else
        {
            // This is a GET command
            if (!cmd.getCmd)
            {
                // Get command not supported
                qDebug(logRig()) << "Removing unsupported get command from queue" << funcString[func] << "VFO" << receiver;
                queue->del(func,receiver);
                return;
            }
            if (cmd.cmd == funcSelectedMode)
            {
                // As the mode command doesn't provide filter/data settings, query for those as well
                queue->add(priorityImmediate,funcIFFilter,false,0);
                queue->add(priorityImmediate,funcDataMode,false,0);
            }
        }
        // Send the command
        payload.append(";");
        if (portConnected)
        {
            QMutexLocker locker(&serialMutex);
            if (serialPort->write(payload) != payload.size())
            {
                qInfo(logSerial()) << "Error writing to serial port";
            }
            lastSentCommand = payload;
        }
    }
}

void kenwoodCommander::serialPortError(QSerialPort::SerialPortError err)
{
    switch (err) {
    case QSerialPort::NoError:
        break;
    default:
        qDebug(logSerial()) << "Serial port error";
        break;
    }
}

