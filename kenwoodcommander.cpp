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

    if (rtpThread != Q_NULLPTR) {
        receiveCommand(funcVOIP,QVariant::fromValue<uchar>(0),0);
        qInfo(logUdp()) << "Stopping RTP thread";
        rtpThread->quit();
        rtpThread->wait();
    }

    emit requestRadioSelection(QList<radio_cap_packet>()); // Remove radio list.

    queue->setRigCaps(Q_NULLPTR); // Remove access to rigCaps

    if (port != Q_NULLPTR) {
        port->close();
        delete port;
        port = Q_NULLPTR;
    }
    qDebug(logRig()) << "Closing rig comms";
}


void kenwoodCommander::commSetup(QHash<quint8,rigInfo> rigList, quint8 rigCivAddr, QString rigSerialPort, quint32 rigBaudRate, QString vsp,quint16 tcpPort, quint8 wf)
{
    // constructor for serial connected rigs
    // As the serial connection is quite simple, no need to use a dedicated class.
    this->rigList = rigList;
    this->rigCivAddr = rigCivAddr;
    this->rigSerialPort = rigSerialPort;
    this->rigBaudRate = rigBaudRate;
    this->vsp = vsp;
    this->tcpPort = tcpPort;
    this->wf = wf;

    usingNativeLAN = false;

    if (port != Q_NULLPTR) {
        if (port->isOpen())
            port->close();
        delete port;
        port = Q_NULLPTR;
    }

    port = new QSerialPort();

    qobject_cast<QSerialPort*>(port)->setPortName(rigSerialPort);
    qobject_cast<QSerialPort*>(port)->setBaudRate(rigBaudRate);
    qobject_cast<QSerialPort*>(port)->setStopBits(QSerialPort::OneStop);
    qobject_cast<QSerialPort*>(port)->setFlowControl(QSerialPort::HardwareControl);
    qobject_cast<QSerialPort*>(port)->setParity(QSerialPort::NoParity);

    network = false;
    // Run setup common to all rig type
    commonSetup();
}

void kenwoodCommander::commSetup(QHash<quint8,rigInfo> rigList, quint8 rigCivAddr, udpPreferences prefs, audioSetup rxSetup, audioSetup txSetup, QString vsp, quint16 tcpPort)
{
    // constructor for network (LAN) connected rigs
    this->rigList = rigList;
    this->rigCivAddr = rigCivAddr;
    this->prefs = prefs;
    this->rxSetup = rxSetup;
    this->txSetup = txSetup;
    this->vsp = vsp;
    this->tcpPort = tcpPort;
    this->connType = prefs.connectionType;

    usingNativeLAN = true;

    port = new QTcpSocket(this);

    connect(qobject_cast<QTcpSocket*>(port), &QTcpSocket::connected, this, &kenwoodCommander::lanConnected);
    connect(qobject_cast<QTcpSocket*>(port), &QTcpSocket::disconnected, this, &kenwoodCommander::lanDisconnected);
    qobject_cast<QTcpSocket*>(port)->connectToHost(prefs.ipAddress,prefs.controlLANPort);

    // Run setup common to all rig types    
    commonSetup();
}

void kenwoodCommander::lanConnected()
{
    qInfo() << QString("Connected to: %0:%1").arg(prefs.ipAddress).arg(prefs.controlLANPort);
    qInfo() << "Sending initial connection request";
    port->write("##CN;\n");
}

void kenwoodCommander::lanDisconnected()
{
    qInfo() << QString("Disconnected from: %0:%1").arg(prefs.ipAddress,prefs.controlLANPort);
    portConnected=false;
}


void kenwoodCommander::closeComm()
{
    qInfo(logRig()) << "Closing rig comms";
    if (port != Q_NULLPTR && portConnected)
    {
        if (port->isOpen())
            port->close();
        delete port;
        port = Q_NULLPTR;
    }
    portConnected=false;
}

void kenwoodCommander::commonSetup()
{
    // common elements between the two constructors go here:
    connect(port, &QIODevice::readyRead, this, &kenwoodCommander::receiveDataFromRig);

    if(port->open(QIODevice::ReadWrite))
    {
        portConnected = true;
    } else
    {
        if (network)
            emit havePortError(errorType(true, prefs.ipAddress, "Could not open port.\nPlease check Radio Access under Settings."));
        else
            emit havePortError(errorType(true, rigSerialPort, "Could not open port.\nPlease check Radio Access under Settings."));
        portConnected=false;
    }


    if (vsp.toLower() != "none") {
        qInfo(logRig()) << "Attempting to connect to vsp/pty:" << vsp;
        ptty = new pttyHandler(vsp,this);
        // data from the ptty to the rig:
        connect(ptty, SIGNAL(haveDataFromPort(QByteArray)), port, SLOT(sendData(QByteArray)));
        // data from the rig to the ptty:
        connect(this, SIGNAL(haveDataFromRig(QByteArray)), ptty, SLOT(receiveDataFromRigToPtty(QByteArray)));
    }

    if (tcpPort > 0) {
        tcp = new tcpServer(this);
        tcp->startServer(tcpPort);
        // data from the tcp port to the rig:
        connect(tcp, SIGNAL(receiveData(QByteArray)), port, SLOT(sendData(QByteArray)));
        connect(this, SIGNAL(haveDataFromRig(QByteArray)), tcp, SLOT(sendData(QByteArray)));
    }

    // Is this a network connection?



    // Minimum commands we need to find rig model and login
    rigCaps.commands.clear();
    rigCaps.commandsReverse.clear();
    rigCaps.commands.insert(funcTransceiverId,funcType(funcTransceiverId, QString("Transceiver ID"),"ID",0,999,false,true,false,3,false));
    rigCaps.commandsReverse.insert(QByteArray("ID"),funcTransceiverId);

    rigCaps.commands.insert(funcConnectionRequest,funcType(funcConnectionRequest, QString("Connection Request"),"##CN",0,1,false,true,true,1,false));
    rigCaps.commandsReverse.insert(QByteArray("##CN"),funcConnectionRequest);

    rigCaps.commands.insert(funcLogin,funcType(funcLogin, QString("Network Login"),"##ID",0,0,false,false,true,0,false));
    rigCaps.commandsReverse.insert(QByteArray("##ID"),funcLogin);

    rigCaps.commands.insert(funcLoginEnableDisable,funcType(funcLoginEnableDisable, QString("Enable/Disable Login"),"##UE",0,1,false,false,true,1,false));
    rigCaps.commandsReverse.insert(QByteArray("##UE"),funcLoginEnableDisable);

    rigCaps.commands.insert(funcTXInhibit,funcType(funcTXInhibit, QString("Transmit Inhibit"),"##TI",0,1,false,false,true,1,false));
    rigCaps.commandsReverse.insert(QByteArray("##TI"),funcTXInhibit);

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
    port->write(d);
    lastCommand.data = d;
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
    port->write(d);
    lastCommand.data = d;
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

void kenwoodCommander::receiveDataFromRig()
{
    const QByteArray in = port->readAll();
    parseData(in);
    emit haveDataFromRig(in);
}


void kenwoodCommander::parseData(QByteArray data)
{
    funcs func = funcNone;
    funcType type;

    // Handle partially received command (missing terminating ;), queue it for next run.
    if (!data.endsWith(";"))
    {
        partial.append(data);
        return;
    }

    if (!partial.isEmpty()) {
        data.prepend(partial);
        partial.clear();
    }

    QList<QByteArray> commands = data.split(';');
    QByteArray cmdFull;
    for (auto &d: commands) {        
        if (d.isEmpty())
            continue;
        uchar receiver = 0; // Used for Dual/RX
        cmdFull = d; // save the complete cmd for debug later.

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

        if (!rigCaps.commands.contains(func)) {
            // Don't warn if we haven't received rigCaps yet
            if (haveRigCaps)
                qInfo(logRig()) << "Unsupported command received from rig" << d << "Check rig file";
            return;
        } else {
            type = this->rigCaps.commands.find(func).value();
        }

        d.remove(0,count);

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
            modeInfo mi = queue->getCache(func,receiver).value.value<modeInfo>();
            for (auto& m: rigCaps.modes)
            {
                if (m.reg == uchar(d.at(0) - NUMTOASCII))
                {
                    if (mi.reg != m.reg)
                    {
                        // Mode has changed.
                        mi.reg=m.reg;
                        mi.VFO=m.VFO;
                        mi.bwMax=m.bwMax;
                        mi.mk=m.mk;
                        mi.name=m.name;
                        mi.pass=m.pass;
                    }
                    value.setValue(mi);
                    break;
                }
            }
            break;
        }
        case funcDataMode:
        {
            func = funcSelectedMode;
            modeInfo mi = queue->getCache(func,receiver).value.value<modeInfo>();
            mi.data = uchar(d.at(0) - NUMTOASCII);
            value.setValue(mi);
            break;
        }
        case funcIFFilter:
        {
            func = funcSelectedMode;
            modeInfo mi = queue->getCache(func,receiver).value.value<modeInfo>();
            mi.filter = uchar(d.at(0) - NUMTOASCII);
            value.setValue(mi);
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
                if (r.reg == d.toInt())
                {
                    value.setValue(r);
                    break;
                }
            }
            break;
        }
        case funcTransceiverStatus:
        {
            // This is a response to the IF command which contains a wealth of information
            // We could use this for various things if we want.
            // It doesn't work when in data mode though!
            bool ok=false;
            short rit = d.mid(16,5).toShort(&ok);
            if (ok) {
                value.setValue(rit);
                queue->receiveValue(funcRitFreq,QVariant::fromValue(rit),receiver);
            }

            queue->receiveValue(funcRitStatus,QVariant::fromValue(bool(d.at(21) - NUMTOASCII)),receiver);

            // This is PTT status
            value.setValue(bool(d.at(26) - NUMTOASCII));
            isTransmitting = value.toBool();
            break;
        }
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
            break;
        case funcFilterWidth: {
            // We need to work out which mode first:
            short width=0;
            uchar v = d.toShort();
            auto m = queue->getCache(funcSelectedMode,receiver).value.value<modeInfo>();
            if (m.mk == modeLSB || m.mk == modeUSB)
            {
                if (v == 0)
                    width = 50;
                else if (v == 1)
                    width = 80;
                else if (v >1 && v <10)
                    width = v * 50;
                else
                    width = (v-5)*100;
            }
            else if (m.mk == modeCW || m.mk == modeCW_R)
            {
                if (v == 0)
                    width = 50;
                else if (v == 1)
                    width = 80;
                else if (v > 1 && v < 10)
                    width = v * 50;
                else if (v > 9 && v < 15)
                    width = (v-5)*100;
                else
                    width = (v-13)*500;
            }
            else if (m.mk == modeRTTY || m.mk == modeRTTY_R)
            {
                if (v < 6)
                    width = (v+5)*50;
                else
                    width = (v-4)*500;
            }
            else if (m.mk == modePSK || m.mk == modePSK_R)
            {
                if (v == 0)
                    width = 50;
                else if (v == 1)
                    width = 80;
                else if (v >1 && v <11)
                    width = v * 50;
                else
                    width = (v-11)*200;
            }

            value.setValue<ushort>(width);
            //qInfo() << "Got filter width" << width << "original" << d.toUShort();
            break;
        }
        case funcMeterType:
            break;
        case funcXitFreq:
        case funcRitFreq:
            break;
        case funcMonitor:
        case funcRitStatus:
        case funcCompressor:
        case funcVox:
        case funcRepeaterTone:
        case funcRepeaterTSQL:
        case funcScopeOnOff:
        case funcScopeHold:
        case funcOverflowStatus:
        case funcSMeterSqlStatus:
        case funcSplitStatus:
            value.setValue<bool>(d.at(0) - NUMTOASCII);
            break;
        case funcAfGain:
            if (!usingNativeLAN)
            {
                value.setValue<uchar>(d.toUShort());
            }
            else
            {
                continue;
            }
            break;
        // These are numbers, as they might contain other info, only extract type.bytes len of data.
        case funcTunerStatus:
        {
            uchar s = d.mid(0, type.bytes).toUShort();
            switch(s)  {
            case 0:
                // 000: not enabled
                value.setValue<uchar>(0);
                break;
            case 10:
                // 010: tuned and enabled
                value.setValue<uchar>(1);
                break;
            case 11:
                // 011: tuning, we think
                value.setValue<uchar>(2);
                break;
            }
            break;
        }
        case funcAGCTimeConstant:
        case funcMemorySelect:
        case funcRfGain:
        case funcRFPower:
        case funcSquelch:
        case funcMonitorGain:
        case funcKeySpeed:
        case funcScopeRef:
        case funcAttenuator:
        case funcPreamp:
        case funcNoiseBlanker:
        case funcNoiseReduction:
        case funcAGC:
        case funcPowerControl:
        case funcLANModLevel:
            value.setValue<uchar>(d.mid(0,type.bytes).toUShort());
            break;
        case funcCompressorLevel:
            // We only want the second value.
            value.setValue<uchar>(d.mid(type.bytes,type.bytes).toUShort());
            break;
        case funcScopeSpan:
            for (auto &s: rigCaps.scopeCenterSpans)
            {
                if (s.cstype == (d.at(0) - NUMTOASCII) )
                {
                    value.setValue(s);
                }
            }
            break;
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
        case funcSMeter:
            if (isTransmitting)
                func = funcPowerMeter;
        case funcSWRMeter:
        case funcALCMeter:
        case funcCompMeter:
        case funcVdMeter:
        case funcIdMeter:
            // TS-590 uses 0-30 for meters (TS-890 uses 70), Icom uses 0-255.
            value.setValue<uchar>(d.toUShort() * (255/(type.maxVal-type.minVal)));
            //qDebug(logRig()) << "Meter value received: value: " << value << "raw: " << d.toUShort() << "Meter type: " << func;
            break;
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif
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
        case funcToneFreq:
        case funcTSQLFreq:
        {
            for (const auto &t: rigCaps.ctcss)
            if (d.toUShort() == t.tone) {
                value.setValue(t);
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
        case funcConnectionRequest:
            qInfo() << "Received connection request command";
            // Send login
            queue->add(priorityImmediate,queueItem(funcLogin,QVariant::fromValue(prefs),false,0));
            break;
        case funcLogin:
        {
            if (d.at(0) - NUMTOASCII == 0) {
                emit havePortError(errorType(true, prefs.ipAddress, "Invalid Username/Password"));
            }
            loginRequired = false;
            qInfo(logRig()) << "Received login reply with command:" <<funcString[func] << "data" << d << (d.toInt()?"Successful":"Failure");
            break;
        }
        case funcTXInhibit:
            qInfo(logRig()) << "Received" << funcString[func] << "with value" << d << (d.toInt()?"TX Authorized":"TX Inhibited");
            if (d.toInt()==0)
            {
                this->txSetup.sampleRate=0; // Disable TX audio.
            }
            break;
        case funcLoginEnableDisable:
            qInfo(logRig()) << "Received" << funcString[func] << "with value" << d << (d.toInt()?"Enabled":"Disabled");
            break;
        case funcScopeRange:
        {
            currentScope.valid=false;
            currentScope.oor = 0;   // No easy way to get OOR unless we calculate it.
            currentScope.receiver = 0;
            if (queue->getCache(funcScopeMode,receiver).value.value<spectrumMode_t>() == spectrumMode_t::spectModeCenter)
            {

                // We are in center mode so the scope range doesn't tell us anything!

                double span = 0.0;
                for (const auto &s: rigCaps.scopeCenterSpans)
                {
                    if (s.cstype == queue->getCache(funcScopeSpan,receiver).value.value<centerSpanData>().cstype)
                    {
                        span = double(s.freq) / 1000000.0 ;
                    }
                }
                vfo_t vfo = queue->getCache(funcSelectVFO,receiver).value.value<vfo_t>();
                double freq = queue->getCache(vfo==vfoA?funcSelectedFreq:funcUnselectedFreq,receiver).value.value<freqt>().MHzDouble;
                currentScope.startFreq=double(freq - (span/2));
                currentScope.endFreq=double(freq + (span/2));
            } else {
                currentScope.startFreq = double(d.mid(0,8).toULongLong())/1000000.0;
                currentScope.endFreq = double(d.mid(8,8).toULongLong())/1000000.0;
            }
            //qInfo() << "Range:" << d << "is" << currentScope.startFreq << "/" << currentScope.endFreq;
            break;
        }
        case funcScopeMode:
            value.setValue<spectrumMode_t>(spectrumMode_t(d.at(0) - NUMTOASCII));
            currentScope.mode = spectrumMode_t(d.at(0) - NUMTOASCII);
            break;
        case funcScopeWaveData:
        {
            currentScope.data.clear();
            for (int i=0;i<d.length();i=i+2)
            {
                bool ok;
                currentScope.data.append(uchar(rigCaps.spectAmpMax-(d.mid(i,2).toShort(&ok,16))));
            }
            currentScope.valid=true;
            value.setValue(currentScope);
            break;
        }
        case funcUSBScope:
        case funcScopeInfo:
        {
            short s = d.mid(0,2).toShort();
            if (s == 0)
            {
                scopeSplit=0;
                currentScope.data.clear();
                if (func == funcScopeInfo)
                {
                    currentScope.valid=false;
                    currentScope.receiver = 0;
                    currentScope.mode=spectrumMode_t(d.at(2) - NUMTOASCII);
                    currentScope.oor = bool(d.at(25) - NUMTOASCII);
                    if (currentScope.mode == spectrumMode_t::spectModeCenter) {
                        quint32 span = double(d.mid(3,11).toULongLong());
                        quint32 center = double(d.mid(14,11).toULongLong());
                        currentScope.startFreq = double(center - (span/2))/1000000.0;
                        currentScope.endFreq = double(center + (span/2))/1000000.0;
                    } else {
                        currentScope.startFreq = double(d.mid(3,11).toULongLong())/1000000.0;
                        currentScope.endFreq = double(d.mid(14,11).toULongLong())/1000000.0;
                    }
                    return;
                }
            }

            for (int i=2;i<d.length();i=i+2)
            {
                bool ok;
                currentScope.data.append(uchar(rigCaps.spectAmpMax-(d.mid(i,2).toShort(&ok,16))));
            }

            scopeSplit++;

            if (scopeSplit == rigCaps.spectSeqMax) {
                currentScope.valid=true;
                value.setValue(currentScope);
                func = funcScopeWaveData;
            }
            break;
        }
        case funcScopeClear:
            currentScope.data.clear();
            currentScope.valid=false;
            break;
        case funcCWDecode:
            value.setValue<QString>(d);
            break;
        case funcRXEqualizer:
        case funcTXEqualizer:
            qInfo(logRig()) << "Received" << funcString[func] << "values" << d;
            // M0VSE deal with these in some way when we add a rig EQ panel?
            break;
        case funcVOIP:
            qInfo(logRig()) << "Recieved VOIP response:" << d.toInt();
            if (d.toInt() && rtpThread == Q_NULLPTR) {
                rtp = new rtpAudio(prefs.ipAddress,quint16(prefs.audioLANPort),this->rxSetup, this->txSetup);
                rtpThread = new QThread(this);
                rtpThread->setObjectName("RTP()");
                rtp->moveToThread(rtpThread);
                connect(this, SIGNAL(initRtpAudio()), rtp, SLOT(init()));
                connect(rtpThread, SIGNAL(finished()), rtp, SLOT(deleteLater()));
                connect(this, SIGNAL(haveChangeLatency(quint16)), rtp, SLOT(changeLatency(quint16)));
                connect(this, SIGNAL(haveSetVolume(quint8)), rtp, SLOT(setVolume(quint8)));
                // Audio from UDP
                connect(rtp, SIGNAL(haveAudioData(audioPacket)), this, SLOT(receiveAudioData(audioPacket)));
                QObject::connect(rtp, SIGNAL(haveRxLevels(quint16, quint16, quint16, quint16, bool, bool)), this, SLOT(getRxLevels(quint16, quint16, quint16, quint16, bool, bool)));
                QObject::connect(rtp, SIGNAL(haveTxLevels(quint16, quint16, quint16, quint16, bool, bool)), this, SLOT(getTxLevels(quint16, quint16, quint16, quint16, bool, bool)));
                rtpThread->start(QThread::TimeCriticalPriority);
                emit initRtpAudio();
            }
            break;
        case funcFilterControlSSB:
        case funcFilterControlData:
            qInfo(logRig()) << "Received" << funcString[func] << "value" << d;
            break;
        case funcFA:
            qInfo(logRig()) << "Rig error, last command sent:" << funcString[lastCommand.func] << "(min:" << lastCommand.minValue << "max:" <<
                lastCommand.maxValue << "bytes:" << lastCommand.bytes <<  ") data:" << lastCommand.data;
            break;
        default:
            qWarning(logRig()).noquote() << "Unhandled command received from rig:" << funcString[func] << "value:" << d.mid(0,10);
            break;
        }

        if(func != funcScopeWaveData
            && func != funcScopeInfo
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
            qDebug(logRigTraffic()) << QString("Received from radio: cmd: %0, data: %1").arg(funcString[func]).arg(d.toStdString().c_str());
            //printHexNow(d, logRigTraffic());
#ifdef QT_DEBUG
            qDebug(logRigTraffic()) << QString("Full command string: [%0]").arg(cmdFull.toStdString().c_str());
#endif
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
            for (const auto &tn: rigCaps.ctcss)
                if (tn.tone == data.left(parse.len).toInt())
                    mem->tone = tn.name;
            break;
        case 'N':
            for (const auto &tn: rigCaps.ctcss)
                if (tn.tone == data.left(parse.len).toInt())
                    mem->toneB = tn.name;
            break;
        case 'o':
            for (const auto &tn: rigCaps.ctcss)
                if (tn.tone == data.left(parse.len).toInt())
                    mem->tsql = tn.name;
            break;
        case 'O':
            for (const auto &tn: rigCaps.ctcss)
                if (tn.tone == data.left(parse.len).toInt())
                    mem->tsqlB = tn.name;
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
    for (int i=0;i<funcLastFunc;i++)
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
                                                       settings->value("Bytes",0).toInt(),
                                                       settings->value("Admin",false).toBool()
                                                       ));

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
            if (settings->value("Num", -1).toString().toInt() == -1) {
                rigCaps.attenuators.push_back(genericType(settings->value("dB", 0).toString().toUInt(),QString("%0 dB").arg(settings->value("dB", 0).toString().toUInt())));
            } else {
                rigCaps.attenuators.push_back(genericType(settings->value("Num", 0).toString().toUInt(), settings->value("Name", 0).toString()));
            }
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

    int numCTCSS = settings->beginReadArray("CTCSS");
    if (numCTCSS == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numCTCSS; c++)
        {
            settings->setArrayIndex(c);
            rigCaps.ctcss.push_back(toneInfo(settings->value("Reg", 0).toUInt(), QString::number(settings->value("Tone", 0.0).toFloat(),'f',1)));
        }
        settings->endArray();
    }

    // Not even sure if there are any Kenwood radios with DTCS?
    int numDTCS = settings->beginReadArray("DTCS");
    if (numDTCS == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numDTCS; c++)
        {
            settings->setArrayIndex(c);
            rigCaps.dtcs.push_back(toneInfo(settings->value("Reg", 0).toInt(), QString::number(settings->value("Reg", 0).toInt()).rightJustified(4,'0')));
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
            qint64 offset = settings->value("Offset", 0).toLongLong();
            rigCaps.bands.push_back(bandType(region,band,bsr,start,end,range,memGroup,bytes,ants,power,color,name,offset));
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

    if (!usingNativeLAN) {
        emit setRadioUsage(0, true, true, QString("<Local>"), QString("127.0.0.1"));
    } else {
        emit setRadioUsage(0,prefs.adminLogin,true,prefs.username,prefs.ipAddress);
    }

    qDebug(logRig()) << "---Rig FOUND" << rigCaps.modelName;
    emit discoveredRigID(rigCaps);
    emit haveRigID(rigCaps);
}

void kenwoodCommander::receiveCommand(funcs func, QVariant value, uchar receiver)
{
    QByteArray payload;
    int val=INT_MIN;

    if (loginRequired && func != funcLogin && func != funcConnectionRequest)
    {
        qInfo() << "Command received before login, requeing";
        queue->add(priorityHigh,queueItem(func,value,false,receiver));
        return;
    }

    if (func == funcSelectVFO) {
        vfo_t v = value.value<vfo_t>();
        func = (v == vfoA)?funcVFOASelect:(v == vfoB)?funcVFOBSelect:(v == vfoMain)?funcNone:(v == vfoSub)?funcNone:funcNone;
        value.clear();
        val = INT_MIN;
    }

    if (func == funcAfGain && value.isValid() && usingNativeLAN) {
        // Ignore the AF Gain command, just queue it for processing
        emit haveSetVolume(static_cast<uchar>(value.toInt()));
        queue->receiveValue(func,value,false);
        return;
    }
    if (value.isValid() && (func == funcRitFreq || func == funcXitFreq))
    {
        // There is no command to directly set the RIT, only up or down commands.
        short rit = value.value<short>();
        static short old = rit;
        short diff = 0;
        diff = rit - old;

        if (rit < old)
        {
            func = funcRITDown;
        }
        else if (rit > old)
        {
            func = funcRITUp;
        }

        //qInfo() << "Updating RIT with" << diff << "old" << old << "new" << rit;
        value.setValue<short>(abs(diff));
        if (diff == 0)
        {
            return;
        }
        old = rit;
    }

    // The transceiverStatus command cannot be used to set PTT, this is handled by TX/RX commands.
    if (value.isValid() && func==funcTransceiverStatus)
    {
        if (value.value<bool>())
        {
            func = funcSetTransmit;
        }
        else
        {
            func = funcSetReceive;
        }

        value.clear();
    }

    //qInfo() << "requested command:" << funcString[func];

    funcType cmd = getCommand(func,payload,val,receiver);


    if (cmd.cmd != funcNone) {
        if (value.isValid())
        {

            // This is a SET command
            if (!prefs.adminLogin && cmd.admin) {
                qWarning(logRig()) << "Admin permission required for set command" << funcString[func] << "access denied";
                return;
            }

            if (!cmd.setCmd) {
                qDebug(logRig()) << "Removing unsupported set command from queue" << funcString[func] << "VFO" << receiver;
                queue->del(func,receiver);
                return;
            }

            if (cmd.cmd == funcScopeOnOff && value.toBool() == true)
            {
                if (connType == connectionUSB)
                    if (aiModeEnabled) // Scope is VERY slow, 1fps.
                        value.setValue(uchar(4));
                    else
                        value.setValue(uchar(5));
                else if (connType == connectionWAN)
                    value.setValue(uchar(3));
                else if (connType == connectionWiFi)
                    value.setValue(uchar(2));
                qInfo() << "Setting scope type to:" << value.toInt() << "for connection type:" << connType;
            } else if (cmd.cmd == funcAutoInformation)
            {
                aiModeEnabled = value.toBool();
            }

            if (!strcmp(value.typeName(),"centerSpanData"))
            {
                centerSpanData d = value.value<centerSpanData>();
                payload.append(QString::number(d.cstype).rightJustified(cmd.bytes, QChar('0')).toLatin1());
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
                if (func == funcVOIP) {
                    qInfo(logRig()) << "Sending VOIP command:" << value.value<uchar>();
                    // We have logged-in so start audio
                }
                if(func==funcTunerStatus) {
                    switch(value.value<uchar>()) {
                    case 0:
                        // Turn it off
                        payload.append(QString::number(100).rightJustified(3, QChar('0')).toLatin1());
                        break;
                    case 1:
                        // Turn it on
                        payload.append(QString::number(110).rightJustified(3, QChar('0')).toLatin1());
                        break;
                    case 2:
                        // Run the tuning cycle
                        payload.append(QString::number(111).rightJustified(3, QChar('0')).toLatin1());
                        break;
                    default:
                        break;
                    }
                } else if ( (func==funcALCMeter) || (func==funcSWRMeter) ||
                            (func==funcCompMeter) || (func==funcIdMeter) || (func==funcVdMeter) ) {
                    // The cmd.bytes is set to four for receiving these values, but when sending values, exactly one byte is required.
                    payload.append(QString::number(value.value<uchar>()).rightJustified(1, QChar('0')).toLatin1());
                } else {
                    payload.append(QString::number(value.value<uchar>()).rightJustified(cmd.bytes, QChar('0')).toLatin1());
                }
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
                if (func == funcFilterWidth)
                {
                    // We need to work out which mode first:
                    ushort width=value.value<ushort>();
                    char val = 0;
                    auto m = queue->getCache(funcSelectedMode,receiver).value.value<modeInfo>();
                    if (m.mk == modeLSB || m.mk == modeUSB)
                    {
                        if (width < 60)
                            val = 0;
                        else if (width < 100)
                            val = 1;
                        else if (width >= 100  && width < 600)
                            val = width / 50;
                        else
                            val = (width / 100) + 5;
                    }
                    else if (m.mk == modeCW || m.mk == modeCW_R)
                    {
                        if (width < 60)
                            val = 0;
                        else if (width < 100)
                            val = 1;
                        else if (width >= 100 && width < 600)
                            val = width / 50;
                        else if (width >= 600 && width < 1500)
                            val = (width / 100) + 5;
                        else
                            val = (width / 500) + 13;

                    }
                    else if (m.mk == modeRTTY || m.mk == modeRTTY_R)
                    {
                        if (width < 1000)
                            val = (width / 50) - 5;
                        else
                            val = (width / 500) + 4;
                    }
                    else if (m.mk == modePSK || m.mk == modePSK_R)
                    {
                        if (width < 60)
                            val = 0;
                        else if (width < 100)
                            val = 1;
                        else if (width >= 100 && width < 600)
                            val = width / 50;
                        else
                            val = (width / 200) + 11;
                    }
                    //qInfo() << "Got filter width" << width << "original" << d.toUShort();
                    payload.append(QString::number(val).rightJustified(cmd.bytes, QChar('0')).toLatin1());
                }
                else
                {
                    payload.append(QString::number(value.value<ushort>()).rightJustified(cmd.bytes, QChar('0')).toLatin1());
                }
            }
            else if(!strcmp(value.typeName(),"udpPreferences"))
            {
                udpPreferences p = value.value<udpPreferences>();
                qInfo(logRig()) << "Sending login for user:" << p.username;
                payload.append(QString("%0%1%2%3%4").arg(p.adminLogin?0:1)
                    .arg(QString::number(p.username.length()).rightJustified(2, QChar('0')))
                    .arg(QString::number(p.password.length()).rightJustified(2, QChar('0')))
                    .arg(p.username).arg(p.password).toLatin1());
            }
            else if(!strcmp(value.typeName(),"toneInfo"))
            {
                payload.append(QString::number(value.value<toneInfo>().tone).rightJustified(cmd.bytes, QChar('0')).toLatin1());
            }
            else if(!strcmp(value.typeName(),"spectrumMode_t"))
            {
                if (value.value<spectrumMode_t>() != spectrumMode_t::spectModeScrollF)
                    payload.append(QString::number(value.value<spectrumMode_t>()).rightJustified(cmd.bytes, QChar('0')).toLatin1());
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
                payload.append(QString::number(b.band).rightJustified(cmd.bytes,'0').toLatin1());
            }
            else if(!strcmp(value.typeName(),"antennaInfo"))
            {
                antennaInfo a = value.value<antennaInfo>();
                payload.append(QString("%0%1").arg(uchar(a.antenna)).arg(uchar(a.rx)).leftJustified(cmd.bytes,'9').toLatin1());
            }
            else if(!strcmp(value.typeName(),"rigInput"))
            {
                payload.append(QString::number(value.value<rigInput>().reg).rightJustified(cmd.bytes,'0').toLatin1());
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
                        payload.append(QString::number(mem.group).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'b':
                        payload.append(QString::number(mem.channel).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'c':
                        payload.append(QString::number(mem.scan).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'C':
                        payload.append(QString::number(mem.scan).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'd':
                        payload.append(QString::number(mem.split).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'e':
                        payload.append(QString::number(mem.vfo).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'E':
                        payload.append(QString::number(mem.vfoB).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'f':
                        payload.append(QString::number(mem.frequency.Hz).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'F':
                        payload.append(QString::number(mem.frequencyB.Hz).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'g':
                        payload.append(QString::number(mem.mode).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'G':
                        payload.append(QString::number(mem.modeB).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'h':
                        payload.append(QString::number(mem.filter).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'H':
                        payload.append(QString::number(mem.filterB).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'i': // single datamode
                        payload.append(QString::number(mem.datamode).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'I':
                        payload.append(QString::number(mem.datamode).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'l': // tonemode
                        payload.append(QString::number(mem.tonemode).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'L':
                        payload.append(QString::number(mem.tonemodeB).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'm':
                        payload.append(QString::number(mem.dsql).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'M':
                        payload.append(QString::number(mem.dsqlB).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'n':
                    {
                        for (const auto &tn: rigCaps.ctcss)
                            if (tn.name == mem.tone)
                                payload.append(QString::number(tn.tone).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    }
                    case 'N':
                        for (const auto &tn: rigCaps.ctcss)
                            if (tn.name == mem.toneB)
                                payload.append(QString::number(tn.tone).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'o':
                        for (const auto &tn: rigCaps.ctcss)
                            if (tn.name == mem.tsql)
                                payload.append(QString::number(tn.tone).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'O':
                        for (const auto &tn: rigCaps.ctcss)
                            if (tn.name == mem.tsqlB)
                                payload.append(QString::number(tn.tone).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'q':
                        payload.append(QString::number(mem.dtcs).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'Q':
                        payload.append(QString::number(mem.dtcsB).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'r':
                        payload.append(QString::number(mem.dvsql).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'R':
                        payload.append(QString::number(mem.dvsqlB).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 's':
                        payload.append(QString::number(mem.duplexOffset.Hz).rightJustified(parse.len, QChar('0'),true).toLatin1());
                        break;
                    case 'S':
                        payload.append(QString::number(mem.duplexOffsetB.Hz).rightJustified(parse.len, QChar('0'),true).toLatin1());
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
        if (network)
        {
            payload.prepend(";");
            payload.append(";\n");
        } else {
            payload.append(";");
        }

        if (portConnected)
        {
            QMutexLocker locker(&serialMutex);
#ifdef QT_DEBUG
            qDebug(logRigTraffic()).noquote() << "Send to rig: " << payload.toStdString().c_str();
#endif
            if (port->write(payload) != payload.size())
            {
                qInfo(logSerial()) << "Error writing to port";
            }
            lastCommand.func = func;
            lastCommand.data = payload;
            lastCommand.minValue = cmd.minVal;
            lastCommand.maxValue = cmd.maxVal;
            lastCommand.bytes = cmd.bytes;
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


void kenwoodCommander::getRxLevels(quint16 amplitudePeak, quint16 amplitudeRMS,quint16 latency,quint16 current, bool under, bool over) {
    status.rxAudioLevel = amplitudePeak;
    status.rxLatency = latency;
    status.rxCurrentLatency =   qint32(current);
    status.rxUnderrun = under;
    status.rxOverrun = over;
    audioLevelsRxPeak[(audioLevelsRxPosition)%audioLevelBufferSize] = amplitudePeak;
    audioLevelsRxRMS[(audioLevelsRxPosition)%audioLevelBufferSize] = amplitudeRMS;

    if((audioLevelsRxPosition)%4 == 0)
    {
        // calculate mean and emit signal
        quint8 meanPeak = findMax(audioLevelsRxPeak);
        quint8 meanRMS = findMean(audioLevelsRxRMS);
        networkAudioLevels l;
        l.haveRxLevels = true;
        l.rxAudioPeak = meanPeak;
        l.rxAudioRMS = meanRMS;
        emit haveNetworkAudioLevels(l);
    }
    audioLevelsRxPosition++;
}

void kenwoodCommander::getTxLevels(quint16 amplitudePeak, quint16 amplitudeRMS ,quint16 latency, quint16 current, bool under, bool over) {
    status.txAudioLevel = amplitudePeak;
    status.txLatency = latency;
    status.txCurrentLatency = qint32(current);
    status.txUnderrun = under;
    status.txOverrun = over;
    audioLevelsTxPeak[(audioLevelsTxPosition)%audioLevelBufferSize] = amplitudePeak;
    audioLevelsTxRMS[(audioLevelsTxPosition)%audioLevelBufferSize] = amplitudeRMS;

    if((audioLevelsTxPosition)%4 == 0)
    {
        // calculate mean and emit signal
        quint8 meanPeak = findMax(audioLevelsTxPeak);
        quint8 meanRMS = findMean(audioLevelsTxRMS);
        networkAudioLevels l;
        l.haveTxLevels = true;
        l.txAudioPeak = meanPeak;
        l.txAudioRMS = meanRMS;
        emit haveNetworkAudioLevels(l);
    }
    audioLevelsTxPosition++;
}

quint8 kenwoodCommander::findMean(quint8 *data)
{
    unsigned int sum=0;
    for(int p=0; p < audioLevelBufferSize; p++)
    {
        sum += data[p];
    }
    return sum / audioLevelBufferSize;
}

quint8 kenwoodCommander::findMax(quint8 *data)
{
    unsigned int max=0;
    for(int p=0; p < audioLevelBufferSize; p++)
    {
        if(data[p] > max)
            max = data[p];
    }
    return max;
}
