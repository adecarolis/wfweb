#include "yaesucommander.h"
#include <QDebug>

#include "rigidentities.h"
#include "logcategories.h"
#include "printhex.h"

// Copyright 2017-2024 Elliott H. Liggett W6EL and Phil E. Taylor M0VSE

// This file parses data from the radio and also forms commands to the radio.

yaesuCommander::yaesuCommander(rigCommander* parent) : rigCommander(parent)
{

    qInfo(logRig()) << "creating instance of yaesuCommander()";

}

yaesuCommander::yaesuCommander(quint8 guid[GUIDLEN], rigCommander* parent) : rigCommander(parent)
{
    qInfo(logRig()) << "creating instance of yaesuCommander() with GUID";
    memcpy(this->guid, guid, GUIDLEN);
    // Add some commands that is a minimum for rig detection
}

yaesuCommander::~yaesuCommander()
{
    qInfo(logRig()) << "closing instance of yaesuCommander()";

    emit requestRadioSelection(QList<radio_cap_packet>()); // Remove radio list.

    queue->setRigCaps(Q_NULLPTR); // Remove access to rigCaps

    if (port != Q_NULLPTR) {
        if (port->isOpen())
        {
            port->close();
        }
        delete port;
        port = Q_NULLPTR;
    }

    qDebug(logRig()) << "Closing rig comms";

    if (ft4222 != Q_NULLPTR)
    {
        ft4222->stop();
        ft4222->exit();
        ft4222->wait();
    }

    if (controlThread != Q_NULLPTR)
    {
        controlThread->quit();
        controlThread->wait();
    }
    if (catThread != Q_NULLPTR)
    {
        catThread->quit();
        catThread->wait();
    }
    if (audioThread != Q_NULLPTR)
    {
        audioThread->quit();
        audioThread->wait();
    }
    if (scopeThread != Q_NULLPTR)
    {
        scopeThread->quit();
        scopeThread->wait();
    }

}

void yaesuCommander::commSetup(QHash<quint16,rigInfo> rigList, quint16 rigCivAddr, QString rigSerialPort, quint32 rigBaudRate, QString vsp,quint16 tcpPort, quint8 wf)
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

    ft4222 = new ft4222Handler();
    connect(ft4222, &ft4222Handler::dataReady, this, &yaesuCommander::haveScopeData);
    connect(ft4222, &ft4222Handler::finished, ft4222, &QObject::deleteLater);

    ft4222->start();

    connect(port, &QIODevice::readyRead, this, &yaesuCommander::receiveDataFromRig);
    connect(this, &yaesuCommander::setScopePoll, ft4222, &ft4222Handler::setPoll);

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

    connect(this, SIGNAL(sendDataToRig(QByteArray)),this,SLOT(dataForRig(QByteArray)));

    // Run setup common to all rig type
    commonSetup();
}

void yaesuCommander::haveScopeData(QByteArray in)
{
    // in contains a valid yaesu data packet.

    if (!haveRigCaps)
    {
        return;
    }

    yaesu_scope_data_t d = (yaesu_scope_data_t)in.data();

    for (unsigned int i=0; i< sizeof(d->wf1);i++) {
        d->wf1[i] = ~d->wf1[i];
    }

    if (rigCaps.numReceiver > 1)
    {
        for (unsigned int i=0; i< sizeof(d->wf2);i++) {
            d->wf2[i] = ~d->wf2[i];
        }
    }

    QByteArray data = QByteArray((char*)d->data,sizeof(d->data));
    freqt fa,fb;
    //quint16 model = data.mid(17,5).toHex().toUShort();
    fa.Hz = data.mid(64,5).toHex().toLongLong();
    fa.MHzDouble=fa.Hz/1000000.0;
    fb.Hz = data.mid(89,5).toHex().toLongLong();
    fb.MHzDouble=fb.Hz/1000000.0;
    quint8 byte17 = quint8(data.mid(17,1).toHex().toUShort());
    quint8 byte32 = quint8(data.mid(32,1).toHex().toUShort());
    quint8 byte33 = quint8(data.mid(33,1).toHex().toUShort());

    quint8 mode = byte17 & 0x0f;// & 0x0f;
    quint8 span = byte32 & 0x0f;
    quint8 speed = (byte33 >> 4);// & 0x0f;

    centerSpanData cs = queue->getCache(funcScopeSpan,0).value.value<centerSpanData>();
    if (cs.cstype != span)
    {
        // Span has changed!
        for (auto &s: rigCaps.scopeCenterSpans)
        {
            if (s.cstype == span)
            {
                cs=s;
                qInfo() << "New center span" << cs.cstype << "freq" << cs.freq << "name" << cs.name;
                queue->receiveValue(funcScopeSpan,QVariant::fromValue<centerSpanData>(cs),0);
                break;
            }
        }
    }

    uchar md = queue->getCache(funcScopeMode,0).value.value<uchar>();
    if (md != mode)
    {
        qInfo() << "received scope mode:" << mode << "current mode" << md;
        //qInfo() << "Data" << data.toHex(' ');
        queue->receiveValue(funcScopeMode,QVariant::fromValue<uchar>(mode),0);
    }
    scopeData scope;
    scope.data = QByteArray((char*)d->wf1,sizeof(d->wf1));
    scope.valid=true;
    scope.receiver=0;
    scope.startFreq=fa.MHzDouble-((cs.freq/2)/1000000.0);
    scope.endFreq=fa.MHzDouble+((cs.freq/2)/1000000.0);
    scope.mode = mode;
    scope.oor = false;
    scope.fixedEdge = 1;

    queue->receiveValue(funcScopeWaveData,QVariant::fromValue<scopeData>(scope),0);
    queue->receiveValue(funcSelectedFreq,QVariant::fromValue<freqt>(fa),0);
    queue->receiveValue(funcUnselectedFreq,QVariant::fromValue<freqt>(fb),0);
    emit setScopePoll(quint64(specTime+wfTime+10));

}

void yaesuCommander::commSetup(QHash<quint16,rigInfo> rigList, quint16 rigCivAddr, udpPreferences prefs, audioSetup rxSetup, audioSetup txSetup, QString vsp, quint16 tcpPort)
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

    QHostAddress remoteIP;
    QHostAddress localIP;

    // First find the correct local and remote IP addresses
    if (!remoteIP.setAddress(prefs.ipAddress))
    {
        QHostInfo remote = QHostInfo::fromName(prefs.ipAddress);
        foreach(const auto &addr, remote.addresses())
        {
            if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
                remoteIP = addr;
                qInfo(logUdp()) << "Got IP Address :" << prefs.ipAddress << ": " << addr.toString();
                break;
            }
        }
        if (remoteIP.isNull())
        {
            qInfo(logUdp()) << "Error obtaining IP Address for :" << prefs.ipAddress << ": " << remote.errorString();
            return;
        }
    }

    // Convoluted way to find the external IP address, there must be a better way????
    QString localhostname = QHostInfo::localHostName();
    QList<QHostAddress> hostList = QHostInfo::fromName(localhostname).addresses();
    foreach (const auto &address, hostList)
    {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && address.isLoopback() == false)
        {
            localIP = QHostAddress(address.toString());
        }
    }


    // Each UDP port is in it's own thread and Control will start Cat, then Cat starts Audio.
    // Scope will be started by commander once we know if it's needed or not.

    control = new yaesuUdpControl(prefs,localIP,remoteIP);
    controlThread = new QThread(this);
    controlThread->setObjectName("udpControl()");
    control->moveToThread(controlThread);
    controlThread->start(QThread::NormalPriority);
    connect(controlThread, &QThread::finished, control, &yaesuUdpControl::deleteLater);
    connect(this, &yaesuCommander::initUdpControl, control, &yaesuUdpControl::init);
    connect(control, SIGNAL(haveNetworkError(errorType)), this, SLOT(handlePortError(errorType)));
    connect(control, SIGNAL(haveNetworkStatus(networkStatus)), this, SLOT(handleStatusUpdate(networkStatus)));

    cat = new yaesuUdpCat(localIP,remoteIP,prefs.serialLANPort);
    catThread = new QThread(this);
    catThread->setObjectName("udpCAT()");
    cat->moveToThread(catThread);
    catThread->start(QThread::NormalPriority);
    connect(catThread, &QThread::finished, cat, &yaesuUdpCat::deleteLater);
    connect(control, &yaesuUdpControl::initCat, cat, &yaesuUdpCat::init);
    connect(cat, SIGNAL(haveCatDataFromRig(QByteArray)), this, SLOT(receiveCatDataFromRig(QByteArray)));
    connect(this, SIGNAL(sendDataToRig(QByteArray)),cat,SLOT(sendCatDataToRig(QByteArray)));

    audio = new yaesuUdpAudio(localIP,remoteIP,prefs.audioLANPort,rxSetup,txSetup);
    audioThread = new QThread(this);
    audioThread->setObjectName("udpAudio()");
    audio->moveToThread(audioThread);
    audioThread->start(QThread::TimeCriticalPriority);
    connect(audioThread, &QThread::finished, cat, &yaesuUdpAudio::deleteLater);
    connect(cat, &yaesuUdpCat::initAudio, audio, &yaesuUdpAudio::init);
    connect(this, SIGNAL(haveSetVolume(quint8)), audio, SLOT(setVolume(quint8)));
    connect(audio, SIGNAL(haveNetworkAudioLevels(networkAudioLevels)), this, SLOT(handleNetworkAudioLevels(networkAudioLevels)));

    scope = new yaesuUdpScope(localIP,remoteIP,prefs.scopeLANPort);
    scopeThread = new QThread(this);
    scopeThread->setObjectName("udpScope()");
    scope->moveToThread(scopeThread);
    scopeThread->start(QThread::NormalPriority);
    connect(scopeThread, &QThread::finished, cat, &yaesuUdpScope::deleteLater);
    connect(this, &yaesuCommander::initScope, scope, &yaesuUdpScope::init);
    connect(scope, &yaesuUdpScope::haveScopeData, this, &yaesuCommander::haveScopeData);
    connect(this, &yaesuCommander::setScopePoll, scope, &yaesuUdpScope::setPoll);

    // This will tell all children what the current session is. Must be set as soon as it's known.
    // I am not currently aware of any other "global" settings between each port.
    connect(control, &yaesuUdpControl::haveSession, cat, &yaesuUdpCat::setSession);
    connect(control, &yaesuUdpControl::haveSession, audio, &yaesuUdpAudio::setSession);
    connect(control, &yaesuUdpControl::haveSession, scope, &yaesuUdpScope::setSession);

    // This will try to connect to the radio and then start Cat (which will start audio) scope is started if rigcaps supports scope.
    emit initUdpControl();


    // None of these make any sense for a single radio server.
    //connect(this, SIGNAL(haveChangeLatency(quint16)), udp, SLOT(changeLatency(quint16)));
    //connect(udp, SIGNAL(haveBaudRate(quint32)), this, SLOT(receiveBaudRate(quint32)));
    // Other assorted UDP connections
    //connect(udp, SIGNAL(requestRadioSelection(QList<radio_cap_packet>)), this, SLOT(radioSelection(QList<radio_cap_packet>)));
    //connect(udp, SIGNAL(setRadioUsage(quint8, bool, quint8, QString, QString)), this, SLOT(radioUsage(quint8, bool, quint8, QString, QString)));
    //connect(this, SIGNAL(selectedRadio(quint8)), udp, SLOT(setCurrentRadio(quint8)));

    // Run setup common to all rig types

    commonSetup();

}

void yaesuCommander::lanConnected()
{
    qInfo() << QString("Connected to: %0:%1").arg(prefs.ipAddress).arg(prefs.controlLANPort);
    qInfo() << "Sending initial connection request";
}

void yaesuCommander::lanDisconnected()
{
    qInfo() << QString("Disconnected from: %0:%1").arg(prefs.ipAddress,prefs.controlLANPort);
    portConnected=false;
}


void yaesuCommander::closeComm()
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

void yaesuCommander::commonSetup()
{
    // common elements between the two constructors go here:


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
    rigCaps.commands.insert(funcTransceiverId,funcType(funcTransceiverId, QString("Transceiver ID"),"ID",0,9999,false,false,true,false,4,false));
    rigCaps.commandsReverse.insert(QByteArray("ID"),funcTransceiverId);

    connect(queue,SIGNAL(haveCommand(funcs,QVariant,uchar)),this,SLOT(receiveCommand(funcs,QVariant,uchar)));

    emit commReady();
}

void yaesuCommander::dataForRig(QByteArray d)
{
    if (portConnected) {
        QMutexLocker locker(&serialMutex);
        if (port->write(d) != d.size())
        {
            qInfo(logSerial()) << "Error writing to port";
        }
    }
}

void yaesuCommander::process()
{
    // new thread enters here. Do nothing but do check for errors.
}


void yaesuCommander::receiveBaudRate(quint32 baudrate)
{
    Q_UNUSED(baudrate)
}

void yaesuCommander::setPTTType(pttType_t ptt)
{
    Q_UNUSED(ptt)
}

void yaesuCommander::setRigID(quint16 rigID)
{
    Q_UNUSED(rigID)
}

void yaesuCommander::setCIVAddr(quint16 civAddr)
{
    Q_UNUSED(civAddr)
}

void yaesuCommander::powerOn()
{
    qDebug(logRig()) << "Power ON command in yaesuCommander to be sent to rig: ";
    QByteArray d;
    quint8 cmd = 1;

    if (getCommand(funcPowerControl,d,cmd,0).cmd == funcNone)
    {
        d.append("PS");
    }

    d.append(QString("%0;").arg(cmd).toLatin1());
    emit sendDataToRig(d);
    lastCommand.data = d;
}

void yaesuCommander::powerOff()
{
    qDebug(logRig()) << "Power OFF command in yaesuCommander to be sent to rig: ";
    QByteArray d;
    quint8 cmd = 0;
    if (getCommand(funcPowerControl,d,cmd,0).cmd == funcNone)
    {
        d.append("PS");
    }

    d.append(QString("%0;").arg(cmd).toLatin1());
    emit sendDataToRig(d);
    lastCommand.data = d;
}




void yaesuCommander::handleNewData(const QByteArray& data)
{
    emit haveDataForServer(data);
}


funcType yaesuCommander::getCommand(funcs func, QByteArray &payload, int value, uchar receiver)
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

void yaesuCommander::receiveDataFromRig()
{
    if (portConnected) {
        const QByteArray in = port->readAll();
        parseData(in);
        emit haveDataFromRig(in);
    }
}

void yaesuCommander::receiveCatDataFromRig(QByteArray in)
{
    parseData(in);
    emit haveDataFromRig(in);
}

void yaesuCommander::receiveScopeDataFromRig(QByteArray in)
{
    //parseData(in);
    //emit haveDataFromRig(in);
}

void yaesuCommander::receiveAudioDataFromRig(QByteArray in)
{
    //parseData(in);
    //emit haveDataFromRig(in);
}


void yaesuCommander::parseData(QByteArray data)
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
            continue;
        } else {
            type = this->rigCaps.commands.find(func).value();
        }

        d.remove(0,count);

        if (type.padr) {
            // Value has right padding, so remove all trailing zeros.
            for (int i=type.bytes;i>0;i++)
            {
                if (d.size()>1 && d.back() == '0')
                {
                    d.remove(d.size()-1,1);
                } else {
                    break;
                }
            }
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
            modeInfo mi = queue->getCache(func,receiver).value.value<modeInfo>();
            for (auto& m: rigCaps.modes)
            {
                if (m.reg == uchar(d.toShort(nullptr,16)))
                {
                    if (mi.reg != m.reg)
                    {
                        // Mode has changed.
                        qInfo(logRig()) << funcString[func] << "New mode" << d.toShort(nullptr,16) << m.name;
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
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
        case funcSSBModSource:
        case funcAMModSource:
        case funcFMModSource:
        case funcDataModSource:
            func = funcDATAOffMod;
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
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif
        case funcPBTInner:
        case funcPBTOuter:
            break;
        case funcFilterWidth: {
            // We need to work out which mode first:
            uchar v = uchar(d.toShort());
            auto m = queue->getCache(funcSelectedMode,receiver).value.value<modeInfo>();
            for (const auto& w: rigCaps.widths)
            {
                if ((w.bands >> m.reg) & 0x01)
                {
                    if (v == w.num)
                    {
                        value.setValue<short>(w.hz);
                        break;
                    }
                }
            }
            break;
        }
        case funcMeterType:
            break;
        case funcRitStatus:
        case funcMonitor:
        case funcCompressor:
        case funcVox:
        case funcRepeaterTone:
        case funcRepeaterTSQL:
        case funcScopeOnOff:
        case funcScopeHold:
        case funcOverflowStatus:
        case funcSMeterSqlStatus:
        case funcSplitStatus:
        case funcTransceiverStatus:
            value.setValue<bool>(d.mid(0,1).toUShort());
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
        case funcBreakIn:
            value.setValue<uchar>(d.toUShort());
            break;
        // These are numbers, as they might contain other info, only extract type.bytes len of data.
        case funcTunerStatus:
        {
            uchar s = d.mid(0, type.bytes).toUShort();
            switch(s)  {
            case 000:
                // 000: not enabled
                value.setValue<uchar>(0);
                break;
            case 010:
                // 010: tuned and enabled
                value.setValue<uchar>(1);
                break;
            case 013:
                // 011: tuning, we think
                value.setValue<uchar>(2);
                break;
            }
            break;
        }
        case funcSSBUSBModGain:
        case funcAMUSBModGain:
        case funcFMUSBModGain:
        case funcDataUSBModGain:
            func = funcUSBModLevel;
            value.setValue<uchar>(d.mid(0,type.bytes).toUShort());
            break;
        case funcSSBRearModGain:
        case funcAMRearModGain:
        case funcFMRearModGain:
        case funcDataRearModGain:
            func = funcACCAModLevel;
            value.setValue<uchar>(d.mid(0,type.bytes).toUShort());
            break;
        case funcAGCTimeConstant:
        case funcMemorySelect:
        case funcRfGain:
        case funcRFPower:
        case funcMicGain:
        case funcSquelch:
        case funcMonitorGain:
        case funcKeySpeed:
        case funcScopeRef:
        case funcScopeEdge:
        case funcAttenuator:
        case funcPreamp:
        case funcNoiseBlanker:
        case funcNoiseReduction:
        case funcAGC:
        case funcPowerControl:
        case funcFilterShape:
        case funcRoofingFilter:
        case funcScopeSpeed:
        case funcCwPitch:
            value.setValue<uchar>(d.mid(0,type.bytes).toUShort());
            break;
        case funcXitFreq:
        case funcRitFreq:
            value.setValue<short>(d.toShort());
            break;
        case funcCompressorLevel:
            // We only want the second value.
            value.setValue<uchar>(d.mid(type.bytes,type.bytes).toUShort());
            break;
        case funcScopeSpan:
            for (auto &s: rigCaps.scopeCenterSpans)
            {
                if (s.cstype == uchar(d.mid(0,type.bytes).toUShort()) )
                {
                    value.setValue(s);
                }
            }
            break;
        case funcSMeter:
            value.setValue(getMeterCal(meterS,d.toUShort()));
            break;
        case funcPowerMeter:
            value.setValue(getMeterCal(meterPower,d.toUShort()));
            break;
        case funcSWRMeter:
            value.setValue(getMeterCal(meterSWR,d.toUShort()));
            break;
        case funcALCMeter:
            value.setValue(getMeterCal(meterALC,d.toUShort()));
            break;
        case funcCompMeter:
            value.setValue(getMeterCal(meterComp,d.toUShort()));
            break;
        case funcVdMeter:
            value.setValue(getMeterCal(meterVoltage,d.toUShort()));
            break;
        case funcIdMeter:
            value.setValue(getMeterCal(meterCurrent,d.toUShort()));
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
        case funcDate:
        case funcTime:
            qInfo(logRig()) << "Received" << funcString[func] << d;
            break;
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
            quint16 model = d.toUShort();
            value.setValue(model);
            qInfo(logRig()) << "Response to rig id query" << d << "decoded as" << model;
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
        case funcVFOASelect:
        case funcVFOBSelect:
        case funcMemoryMode:
            // Do we need to do anything with this?
            break;
        case funcScopeMode:
            // The range is 0-A, so use hex to convert.
            value.setValue(uchar(d.mid(0,type.bytes).toUShort(nullptr,16)));
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

bool yaesuCommander::parseMemory(QByteArray d,QVector<memParserFormat>* memParser, memoryType* mem)
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

void yaesuCommander::determineRigCaps()
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
    rigCaps.roofing.clear();
    rigCaps.scopeModes.clear();
    rigCaps.widths.clear();

    for (int i = meterNone; i < meterUnknown; i++)
    {
        rigCaps.meters[i].clear();
        rigCaps.meterLines[i] = 0.0;
    }

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

    rigCaps.manufacturer = manufYaesu;
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
                                                       settings->value("PadRight",false).toBool(),
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
            rigCaps.scopeCenterSpans.push_back(centerSpanData(uchar(settings->value("Num", 0).toUInt()),
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

    int numMeters = settings->beginReadArray("Meters");
    if (numMeters == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numMeters; c++)
        {
            settings->setArrayIndex(c);
            QString meterName = settings->value("Meter", "None").toString();
            if (meterName != "None" && meterName != "")
                for (int i = meterNone; i < meterUnknown; i++)
                {
                    if (meterName == meterString[i] && i != meterNone)
                    {
                        if (settings->value("RedLine", false).toBool())
                        {
                            rigCaps.meterLines[i] = settings->value("ActualVal", 0.0).toDouble();
                        }
                        rigCaps.meters[i].insert(settings->value("RigVal", 0).toInt(),settings->value("ActualVal", 0.0).toDouble());
                        break;
                    }
                }
        }
        settings->endArray();
    }

    int numRoofing = settings->beginReadArray("Roofing");
    if (numRoofing == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numRoofing; c++)
        {
            settings->setArrayIndex(c);
            rigCaps.roofing.push_back(genericType(settings->value("Num", 0).toString().toUInt(), settings->value("Name", 0).toString()));
        }
        settings->endArray();
    }


    int numScopeModes = settings->beginReadArray("ScopeModes");
    if (numScopeModes == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numScopeModes; c++)
        {
            settings->setArrayIndex(c);
            rigCaps.scopeModes.push_back(genericType(settings->value("Num", 0).toString().toUInt(), settings->value("Name", 0).toString()));
        }
        settings->endArray();
    }


    int numWidths = settings->beginReadArray("Widths");
    if (numWidths == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numWidths; c++)
        {
            settings->setArrayIndex(c);
            rigCaps.widths.push_back(widthsType(settings->value("Bands", 0).toString().toUShort(nullptr,16),
                    uchar(settings->value("Num", 0).toString().toUShort()),
                    settings->value("Hz", 0).toString().toUShort()));
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
    if (rigCaps.hasSpectrum)
    {
        emit initScope();
    }

}

void yaesuCommander::receiveCommand(funcs func, QVariant value, uchar receiver)
{
    QByteArray payload;
    int val=INT_MIN;

    if (func == funcSelectVFO) {
        vfo_t v = value.value<vfo_t>();
        func = (v == vfoA)?funcVFOASelect:(v == vfoB)?funcVFOBSelect:(v == vfoMain)?funcNone:(v == vfoSub)?funcNone:funcNone;
        value.clear();
        val = INT_MIN;
    }

    // Certain commands are mode specific so we should modify the command here:
    if (func == funcDATAOffMod) {
        modeInfo mi = queue->getCache(funcSelectedMode,0).value.value<modeInfo>();
        if (mi.mk == modeLSB || mi.mk == modeUSB)
            func = funcSSBModSource;
        else if (mi.mk == modeAM)
            func = funcAMModSource;
        else if (mi.mk == modeFM)
            func = funcFMModSource;
        else
            func = funcDataModSource;
    }

    if (func == funcUSBModLevel) {
        modeInfo mi = queue->getCache(funcSelectedMode,0).value.value<modeInfo>();
        if (mi.mk == modeLSB || mi.mk == modeUSB)
            func = funcSSBUSBModGain;
        else if (mi.mk == modeAM)
            func = funcAMUSBModGain;
        else if (mi.mk == modeFM)
            func = funcFMUSBModGain;
        else
            func = funcDataUSBModGain;
    }

    if (func == funcACCAModLevel) {
        modeInfo mi = queue->getCache(funcSelectedMode,0).value.value<modeInfo>();
        if (mi.mk == modeLSB || mi.mk == modeUSB)
            func = funcSSBRearModGain;
        else if (mi.mk == modeAM)
            func = funcAMRearModGain;
        else if (mi.mk == modeFM)
            func = funcFMRearModGain;
        else
            func = funcDataRearModGain;
    }


    //qInfo() << "requested command:" << funcString[func];

    funcType cmd = getCommand(func,payload,val,receiver);

    if (func == funcAfGain && value.isValid() && usingNativeLAN) {
        // Ignore the AF Gain command, just queue it for processing
        double val = value.toInt();
        if (cmd.cmd != funcNone) {
            val = (255.0/cmd.maxVal) * value.toInt();
        }
        emit haveSetVolume(static_cast<uchar>(val));
        queue->receiveValue(func,value,false);
        return;
    }

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
                if (cmd.padr)
                    payload.append(QString::number(d.cstype).leftJustified(cmd.bytes, QChar('0')).toLatin1());
                else
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
                    qDebug(logRig()) << "Sending CW: payload:" << payload.toStdString().c_str();
                }
            }
            else if(!strcmp(value.typeName(),"freqt"))
            {
                payload.append(QString::number(value.value<freqt>().Hz).rightJustified(cmd.bytes, QChar('0')).toLatin1());
            }
            else if(!strcmp(value.typeName(),"bool") || !strcmp(value.typeName(),"uchar") || !strcmp(value.typeName(),"int"))
            {
                // This is a simple number
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
                    if (cmd.padr)
                        payload.append(QString::number(value.value<uchar>()).leftJustified(1, QChar('0')).toLatin1());
                    else
                        payload.append(QString::number(value.value<uchar>()).rightJustified(1, QChar('0')).toLatin1());
                } else {
                    if (cmd.padr)
                        payload.append(QString::number(value.value<uchar>(),16).leftJustified(cmd.bytes, QChar('0')).toLatin1());
                    else
                        payload.append(QString::number(value.value<uchar>(),16).rightJustified(cmd.bytes, QChar('0')).toLatin1());
                }
            }
            else if(!strcmp(value.typeName(),"uint"))
            {
                if (func == funcMemoryContents) {
                    if (cmd.padr)
                        payload.append(QString::number(quint16(value.value<uint>() & 0xffff)).leftJustified(cmd.bytes, QChar('0')).toLatin1());
                    else
                        payload.append(QString::number(quint16(value.value<uint>() & 0xffff)).rightJustified(cmd.bytes, QChar('0')).toLatin1());
                } else {
                    if (cmd.padr)
                        payload.append(QString::number(value.value<uint>()).leftJustified(cmd.bytes, QChar('0')).toLatin1());
                    else
                        payload.append(QString::number(value.value<uint>()).rightJustified(cmd.bytes, QChar('0')).toLatin1());
                }
            }
            else if(!strcmp(value.typeName(),"short") )
            {
                short val=value.value<short>();
                if (val < 0)
                    payload.append("-");
                else
                    payload.append("+");
                payload.append(QString::number(abs(val)).rightJustified(cmd.bytes, QChar('0')).toLatin1());
            }
            else if(!strcmp(value.typeName(),"ushort") )
            {
                if (func == funcFilterWidth)
                {
                    // We need to work out which mode first:
                    ushort width=value.value<ushort>();
                    auto m = queue->getCache(funcSelectedMode,receiver).value.value<modeInfo>();
                    for (const auto& w: rigCaps.widths)
                    {
                        if ((w.bands >> m.reg) & 0x01 )
                        {
                            if (w.hz >= width) {
                                payload.append(QString::number(w.num).rightJustified(cmd.bytes, QChar('0')).toLatin1());
                                break;
                            }
                        }
                    }
                }
                else
                {
                    if (cmd.padr)
                        payload.append(QString::number(value.value<ushort>()).leftJustified(cmd.bytes, QChar('0')).toLatin1());
                    else
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
            else if(!strcmp(value.typeName(),"modeInfo"))
            {
                if (cmd.cmd == funcSelectedMode)
                {
                    modeInfo m = value.value<modeInfo>();
                    payload.append(QString("%0").arg(m.reg,1,16).toLatin1());
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
            else if (!strcmp(value.typeName(),"spectrumBounds"))
            {
                spectrumBounds s = value.value<spectrumBounds>();
                payload.append(QString("%0%1%2").arg(s.edge).arg(quint64(s.start*1000000.0),8,10,QChar('0')).arg(quint64(s.end*1000000.0),8,10,QChar('0')).toLatin1());
                qInfo() << "Fixed Edge Bounds edge:" << s.edge << "start:" << s.start << "end:" << s.end << payload;

            }
            else if (!strcmp(value.typeName(),"datekind"))
            {
                datekind d = value.value<datekind>();
                qInfo(logRig()) << QString("Sending new date: (MM-DD-YYYY) %0-%1-%2").arg(d.month).arg(d.day).arg(d.year);
                // YYYYMMDD
                payload.append(QString("%0%1%2").arg(d.year,4,10,QChar('0')).arg(d.month,2,10,QChar('0')).arg(d.day,2,10,QChar('0')).toLatin1());
            }
            else if (!strcmp(value.typeName(),"timekind"))
            {
                timekind t = value.value<timekind>();
                qInfo(logRig()) << QString("Sending new time: (HH:MM) %0:%1").arg(t.hours).arg(t.minutes);
                payload.append(QString("%0%1%2").arg(t.hours,2,10,QChar('0')).arg(t.minutes,2,10,QChar('0')).arg("00").toLatin1());

            }
            else if (!strcmp(value.typeName(),"memoryType"))
            {

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
        payload.append(";");

        qDebug(logRigTraffic()).noquote() << "Send to rig: " << funcString[cmd.cmd] << payload.toStdString().c_str();

        emit sendDataToRig(payload);

        lastCommand.func = func;
        lastCommand.data = payload;
        lastCommand.minValue = cmd.minVal;
        lastCommand.maxValue = cmd.maxVal;
        lastCommand.bytes = cmd.bytes;
    }
}

void yaesuCommander::serialPortError(QSerialPort::SerialPortError err)
{
    switch (err) {
    case QSerialPort::NoError:
        break;
    default:
        qDebug(logSerial()) << "Serial port error";
        break;
    }
}
