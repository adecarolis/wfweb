#include "rigcommander.h"
#include <QDebug>

#include "rigidentities.h"
#include "logcategories.h"
#include "printhex.h"

// Copyright 2017-2023 Elliott H. Liggett W6EL and Phil E. Taylor M0VSE

// This file parses data from the radio and also forms commands to the radio.
// The radio physical interface is handled by the commHandler() instance "comm"

//
// See here for a wonderful CI-V overview:
// http://www.plicht.de/ekki/civ/civ-p0a.html
//
// The IC-7300 "full" manual also contains a command reference.

// How to make spectrum display stop using rigctl:
//  echo "w \0xFE\0xFE\0x94\0xE0\0x27\0x11\0x00\0xFD" | rigctl -m 3073 -r /dev/ttyUSB0 -s 115200 -vvvvv

// Note: When sending \x00, must use QByteArray.setRawData()

rigCommander::rigCommander(QObject* parent) : QObject(parent)
{
    qInfo(logRig()) << "creating instance of rigCommander()";
    state.set(SCOPEFUNC, true, false);
}

rigCommander::rigCommander(quint8 guid[GUIDLEN], QObject* parent) : QObject(parent)
{
    qInfo(logRig()) << "creating instance of rigCommander()";
    state.set(SCOPEFUNC, true, false);
    memcpy(this->guid, guid, GUIDLEN);
}

rigCommander::~rigCommander()
{
    qInfo(logRig()) << "closing instance of rigCommander()";
    closeComm();
}


void rigCommander::commSetup(QHash<unsigned char,QString> rigList, unsigned char rigCivAddr, QString rigSerialPort, quint32 rigBaudRate, QString vsp,quint16 tcpPort, quint8 wf)
{
    // construct
    // TODO: Bring this parameter and the comm port from the UI.
    // Keep in hex in the UI as is done with other CIV apps.

    this->rigList = rigList;
    // civAddr = 0x94; // address of the radio. Decimal is 148.
    civAddr = rigCivAddr; // address of the radio. Decimal is 148.
    usingNativeLAN = false;

    //qInfo(logRig()) << "Opening connection to Rig:" << QString("0x%1").arg((unsigned char)rigCivAddr,0,16) << "on serial port" << rigSerialPort << "at baud rate" << rigBaudRate;
    // ---
    setup();
    // ---

    this->rigSerialPort = rigSerialPort;
    this->rigBaudRate = rigBaudRate;
    rigCaps.baudRate = rigBaudRate;

    comm = new commHandler(rigSerialPort, rigBaudRate,wf,this);
    ptty = new pttyHandler(vsp,this);

    if (tcpPort > 0) {
        tcp = new tcpServer(this);
        tcp->startServer(tcpPort);
    }


    // data from the comm port to the program:
    connect(comm, SIGNAL(haveDataFromPort(QByteArray)), this, SLOT(handleNewData(QByteArray)));

    // data from the ptty to the rig:
    connect(ptty, SIGNAL(haveDataFromPort(QByteArray)), comm, SLOT(receiveDataFromUserToRig(QByteArray)));

    // data from the program to the comm port:
    connect(this, SIGNAL(dataForComm(QByteArray)), comm, SLOT(receiveDataFromUserToRig(QByteArray)));
    if (tcpPort > 0) {
        // data from the tcp port to the rig:
        connect(tcp, SIGNAL(receiveData(QByteArray)), comm, SLOT(receiveDataFromUserToRig(QByteArray)));
        connect(comm, SIGNAL(haveDataFromPort(QByteArray)), tcp, SLOT(sendData(QByteArray)));
    }
    connect(this, SIGNAL(toggleRTS(bool)), comm, SLOT(setRTS(bool)));

    // data from the rig to the ptty:
    connect(comm, SIGNAL(haveDataFromPort(QByteArray)), ptty, SLOT(receiveDataFromRigToPtty(QByteArray)));

    connect(comm, SIGNAL(havePortError(errorType)), this, SLOT(handlePortError(errorType)));
    connect(ptty, SIGNAL(havePortError(errorType)), this, SLOT(handlePortError(errorType)));

    connect(this, SIGNAL(getMoreDebug()), comm, SLOT(debugThis()));
    connect(this, SIGNAL(getMoreDebug()), ptty, SLOT(debugThis()));

    connect(this, SIGNAL(discoveredRigID(rigCapabilities)), ptty, SLOT(receiveFoundRigID(rigCapabilities)));

    emit commReady();
    sendState(); // Send current rig state to rigctld

}

void rigCommander::commSetup(QHash<unsigned char,QString> rigList, unsigned char rigCivAddr, udpPreferences prefs, audioSetup rxSetup, audioSetup txSetup, QString vsp, quint16 tcpPort)
{
    // construct
    // TODO: Bring this parameter and the comm port from the UI.
    // Keep in hex in the UI as is done with other CIV apps.

    this->rigList = rigList;
    // civAddr = 0x94; // address of the radio. Decimal is 148.
    civAddr = rigCivAddr; // address of the radio. Decimal is 148.
    usingNativeLAN = true;
    // -- 
    setup();
    // ---


    if (udp == Q_NULLPTR) {

        udp = new udpHandler(prefs,rxSetup,txSetup);

        udpHandlerThread = new QThread(this);
        udpHandlerThread->setObjectName("udpHandler()");

        udp->moveToThread(udpHandlerThread);

        connect(this, SIGNAL(initUdpHandler()), udp, SLOT(init()));
        connect(udpHandlerThread, SIGNAL(finished()), udp, SLOT(deleteLater()));

        udpHandlerThread->start();

        emit initUdpHandler();

        //this->rigSerialPort = rigSerialPort;
        //this->rigBaudRate = rigBaudRate;

        ptty = new pttyHandler(vsp,this);

        if (tcpPort > 0) {
            tcp = new tcpServer(this);
            tcp->startServer(tcpPort);
        }
        // Data from UDP to the program
        connect(udp, SIGNAL(haveDataFromPort(QByteArray)), this, SLOT(handleNewData(QByteArray)));

        // data from the rig to the ptty:
        connect(udp, SIGNAL(haveDataFromPort(QByteArray)), ptty, SLOT(receiveDataFromRigToPtty(QByteArray)));

        // Audio from UDP
        connect(udp, SIGNAL(haveAudioData(audioPacket)), this, SLOT(receiveAudioData(audioPacket)));

        // data from the program to the rig:
        connect(this, SIGNAL(dataForComm(QByteArray)), udp, SLOT(receiveDataFromUserToRig(QByteArray)));

        // data from the ptty to the rig:
        connect(ptty, SIGNAL(haveDataFromPort(QByteArray)), udp, SLOT(receiveDataFromUserToRig(QByteArray)));

        if (tcpPort > 0) {
            // data from the tcp port to the rig:
            connect(tcp, SIGNAL(receiveData(QByteArray)), udp, SLOT(receiveDataFromUserToRig(QByteArray)));
            connect(udp, SIGNAL(haveDataFromPort(QByteArray)), tcp, SLOT(sendData(QByteArray)));
        }

        connect(this, SIGNAL(haveChangeLatency(quint16)), udp, SLOT(changeLatency(quint16)));
        connect(this, SIGNAL(haveSetVolume(unsigned char)), udp, SLOT(setVolume(unsigned char)));
        connect(udp, SIGNAL(haveBaudRate(quint32)), this, SLOT(receiveBaudRate(quint32)));

        // Connect for errors/alerts
        connect(udp, SIGNAL(haveNetworkError(errorType)), this, SLOT(handlePortError(errorType)));
        connect(udp, SIGNAL(haveNetworkStatus(networkStatus)), this, SLOT(handleStatusUpdate(networkStatus)));
        connect(udp, SIGNAL(haveNetworkAudioLevels(networkAudioLevels)), this, SLOT(handleNetworkAudioLevels(networkAudioLevels)));


        connect(ptty, SIGNAL(havePortError(errorType)), this, SLOT(handlePortError(errorType)));
        connect(this, SIGNAL(getMoreDebug()), ptty, SLOT(debugThis()));

        connect(this, SIGNAL(discoveredRigID(rigCapabilities)), ptty, SLOT(receiveFoundRigID(rigCapabilities)));

        connect(udp, SIGNAL(requestRadioSelection(QList<radio_cap_packet>)), this, SLOT(radioSelection(QList<radio_cap_packet>)));
        connect(udp, SIGNAL(setRadioUsage(quint8, quint8, QString, QString)), this, SLOT(radioUsage(quint8, quint8, QString, QString)));
        connect(this, SIGNAL(selectedRadio(quint8)), udp, SLOT(setCurrentRadio(quint8)));
        emit haveAfGain(rxSetup.localAFgain);
        localVolume = rxSetup.localAFgain;
    }

    // data from the comm port to the program:

    emit commReady();
    sendState(); // Send current rig state to rigctld

    pttAllowed = true; // This is for developing, set to false for "safe" debugging. Set to true for deployment.

}

void rigCommander::closeComm()
{
    qDebug(logRig()) << "Closing rig comms";
    if (comm != Q_NULLPTR) {
        delete comm;
    }
    comm = Q_NULLPTR;

    if (udpHandlerThread != Q_NULLPTR) {
        udpHandlerThread->quit();
        udpHandlerThread->wait();
    }
    udp = Q_NULLPTR;

    if (ptty != Q_NULLPTR) {
        delete ptty;
    }
    ptty = Q_NULLPTR;
}

void rigCommander::setup()
{
    // common elements between the two constructors go here:
    setCIVAddr(civAddr);
    spectSeqMax = 0; // this is now set after rig ID determined

    payloadSuffix = QByteArray("\xFD");

    lookingForRig = false;
    foundRig = false;
    oldScopeMode = spectModeUnknown;

    pttAllowed = true; // This is for developing, set to false for "safe" debugging. Set to true for deployment.
}



void rigCommander::process()
{
    // new thread enters here. Do nothing but do check for errors.
    if(comm!=Q_NULLPTR && comm->serialError)
    {
        emit havePortError(errorType(rigSerialPort, QString("Error from commhandler. Check serial port.")));
    }
}

void rigCommander::handlePortError(errorType err)
{
    qInfo(logRig()) << "Error using port " << err.device << " message: " << err.message;
    emit havePortError(err);
}

void rigCommander::handleStatusUpdate(const networkStatus status)
{
    emit haveStatusUpdate(status);
}

void rigCommander::handleNetworkAudioLevels(networkAudioLevels l)
{
    emit haveNetworkAudioLevels(l);
}

bool rigCommander::usingLAN()
{
    return usingNativeLAN;
}

void rigCommander::receiveBaudRate(quint32 baudrate) {
    rigCaps.baudRate = baudrate;
    emit haveBaudRate(baudrate);
}

void rigCommander::setRTSforPTT(bool enabled)
{
    if(!usingNativeLAN)
    {
        useRTSforPTT_isSet = true;
        useRTSforPTT_manual = enabled;
        if(comm != NULL)
        {
            rigCaps.useRTSforPTT=enabled;
            comm->setUseRTSforPTT(enabled);
        }
    }
}

void rigCommander::findRigs()
{
    // This function sends data to 0x00 ("broadcast") to look for any connected rig.
    lookingForRig = true;
    foundRig = false;

    QByteArray data;
    QByteArray data2;
    //data.setRawData("\xFE\xFE\xa2", 3);
    data.setRawData("\xFE\xFE\x00", 3);
    data.append((char)compCivAddr); // wfview's address, 0xE1
    data2.setRawData("\x19\x00", 2); // get rig ID
    data.append(data2);
    data.append(payloadSuffix);

    emit dataForComm(data);
    // HACK for testing radios that do not respond to rig ID queries: 
    //this->model = model736;
    //this->determineRigCaps();
    return;
}

void rigCommander::prepDataAndSend(QByteArray data)
{
    data.prepend(payloadPrefix);
    //printHex(data, false, true);
    data.append(payloadSuffix);

    if(data[4] != '\x15')
    {
        // We don't print out requests for meter levels
        qDebug(logRigTraffic()) << "Final payload in rig commander to be sent to rig: ";
        //printHex(data);
        //printHex(data, logRigTraffic());
        printHexNow(data, logRigTraffic());
    }

    emit dataForComm(data);
}

bool rigCommander::getCommand(funcs func, QByteArray &payload, int value)
{
    // Value is set to INT_MIN by default as this should be outside any "real" values
    auto it = rigCaps.commands.find(func);
    if (it != rigCaps.commands.end())
    {
        if (value == INT_MIN || (value>=it.value().minVal && value <= it.value().maxVal))
        {
            if (value == INT_MIN)
                qDebug(logRig()) << QString("%0 with no value (get)").arg(funcString[func]);
            else
                qDebug(logRig()) << QString("%0 with value %1 (Range: %2-%3)").arg(funcString[func]).arg(value).arg(it.value().minVal).arg(it.value().maxVal);
            payload.append(it.value().data);
            return true;
        }
        else if (value != INT_MIN)
        {
            qInfo(logRig()) << QString("Value %0 for %1 is outside of allowed range (%2-%3)").arg(value).arg(funcString[func]).arg(it.value().minVal).arg(it.value().maxVal);
        }
    }
    return false;
}

void rigCommander::powerOn()
{
    QByteArray payload;

    int numFE=150;
    switch (this->rigBaudRate) {
    case 57600:
        numFE = 75;
        break;
    case 38400:
        numFE = 50;
        break;
    case 19200:
        numFE = 25;
        break;
    case 9600:
        numFE = 13;
        break;
    case 4800:
        numFE = 7;
        break;
    }

    for(int i=0; i < numFE; i++)
    {
        payload.append("\xFE");
    }

    unsigned char cmd = 0x01;
    if (getCommand(funcRFPower,payload,cmd))
    {
        payload.append(cmd);
        prepDataAndSend(payload);
    }
    else
    {
        // We may not know the command to turn the radio on so here it is:
        payload.append(payloadPrefix); // FE FE 94 E1
        payload.append("\x18\x01");
        payload.append(payloadSuffix); // FD
    }

    qDebug(logRig()) << "Power ON command in rigcommander to be sent to rig: ";
    printHex(payload);

    emit dataForComm(payload);

}

void rigCommander::powerOff()
{
    QByteArray payload;
    unsigned char cmd = '\x00';
    if (getCommand(funcRFPower,payload,cmd))
    {
        payload.append(cmd);
        prepDataAndSend(payload);
    }
}

void rigCommander::enableSpectOutput()
{
    QByteArray payload;
    unsigned char cmd = '\x01';
    if (getCommand(funcScopeDataOutput,payload,cmd))
    {
        payload.append(cmd);
        prepDataAndSend(payload);
    }
}

void rigCommander::disableSpectOutput()
{
    QByteArray payload;
    unsigned char cmd = '\x00';
    if (getCommand(funcScopeDataOutput,payload,cmd))
    {
        payload.append(cmd);
        prepDataAndSend(payload);
    }
}

void rigCommander::enableSpectrumDisplay()
{
    QByteArray payload;
    unsigned char cmd = '\x01';
    if (getCommand(funcScopeOnOff,payload,cmd))
    {
        payload.append(cmd);
        prepDataAndSend(payload);
    }
}

void rigCommander::disableSpectrumDisplay()
{
    QByteArray payload;
    unsigned char cmd = '\x00';
    if (getCommand(funcScopeOnOff,payload,cmd))
    {
        payload.append(cmd);
        prepDataAndSend(payload);
    }
}

void rigCommander::setSpectrumBounds(double startFreq, double endFreq, unsigned char edgeNumber)
{
    unsigned char range = 1;

    QByteArray payload;
    if (getCommand(funcScopeFixedFreq,payload,edgeNumber))
    {
        // Each band should be configured with a maximum range, except for the ICR8600 which doesn't really have "bands"
        for (bandType band: rigCaps.bands)
        {
            if (band.range != 0.0 && startFreq > band.range)
                range++;
        }

        payload.append(range);
        payload.append(edgeNumber);
        payload.append(makeFreqPayload(startFreq));
        payload.append(makeFreqPayload(endFreq));

        prepDataAndSend(payload);
        qInfo(logRig()) << QString("Setting Fixed Range from: %0 to %1 edge %2 range %3").arg(startFreq).arg(endFreq).arg(edgeNumber).arg(range);
    }
}

void rigCommander::getScopeMode()
{
    // center or fixed
    QByteArray payload;
    unsigned char cmd = '\x00';
    if (getCommand(funcScopeCenterFixed,payload,cmd))
    {
        payload.append(cmd);
        prepDataAndSend(payload);
    }
}

void rigCommander::getScopeEdge()
{
    QByteArray payload;
    unsigned char cmd = '\x00';
    if (getCommand(funcScopeEdgeNumber,payload,cmd))
    {
        payload.append(cmd);
        prepDataAndSend(payload);
    }
}

void rigCommander::setScopeEdge(char edge)
{
    // 1 2 or 3 (check command definition)
    QByteArray payload;
    unsigned char vfo = '\x00';
    if (getCommand(funcScopeEdgeNumber,payload,edge))
    {
        payload.append(vfo);
        payload.append(edge);
        prepDataAndSend(payload);
    }
}

void rigCommander::getScopeSpan()
{
    getScopeSpan(false);
}

void rigCommander::getScopeSpan(bool isSub)
{
    QByteArray payload;
    if (getCommand(funcScopeCenterSpan,payload,static_cast<int>(isSub)))
    {
        payload.append(static_cast<unsigned char>(isSub));
        prepDataAndSend(payload);
    }
}

void rigCommander::setScopeSpan(char span)
{
    // See ICD, page 165, "19-12".
    // 2.5k = 0
    // 5k = 2, etc.

    QByteArray payload;
    unsigned char vfo = '\x00';
    if (getCommand(funcScopeCenterSpan,payload,span))
    {
        payload.append(vfo);
        payload.append("\x00"); // 10Hz/1Hz
        for (auto&& s: rigCaps.scopeCenterSpans)
        {
            if (s.cstype == static_cast<centerSpansType>(span))
            {
                double freq = double(s.freq/1000000.0);
                payload.append(makeFreqPayload(freq));
                //qDebug(logRig()) << "Set Span for" << freq << "MHz" << "cmd:";
                prepDataAndSend(payload);
                break;
            }

        }
    }
}

void rigCommander::setSpectrumMode(spectrumMode spectMode)
{
    QByteArray payload;
    unsigned char vfo = '\x00';
    if (getCommand(funcScopeCenterFixed,payload,static_cast<int>(spectMode)))
    {
        payload.append(vfo);
        payload.append(static_cast<unsigned char>(spectMode) );
        prepDataAndSend(payload);
    }
}

void rigCommander::getSpectrumRefLevel()
{
    getSpectrumRefLevel((unsigned char)0x0);
}

void rigCommander::getSpectrumRefLevel(unsigned char mainSub)
{
    QByteArray payload;
    if (getCommand(funcScopeRef,payload,static_cast<int>(mainSub)))
    {
        payload.append(mainSub);
        prepDataAndSend(payload);
    }
}

void rigCommander::setSpectrumRefLevel(int level)
{
    // -30 to +10
    unsigned char vfo = 0x0;
    QByteArray payload;
    if (getCommand(funcScopeRef,payload,level))
    {
        bool isNegative = false;
        if(level < 0)
        {
            isNegative = true;
            level *= -1;
        }
        payload.append(vfo);
        payload.append(bcdEncodeInt(level*10));
        payload.append(static_cast<unsigned char>(isNegative));
        prepDataAndSend(payload);
    }
}

// Not used
void rigCommander::getSpectrumCenterMode()
{
    QByteArray payload;
    if (getCommand(funcScopeCenterFixed,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getSpectrumMode()
{
    QByteArray payload;
    if (getCommand(funcScopeCenterFixed,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setFrequency(unsigned char vfo, freqt freq)
{
    QByteArray payload;
    if (getCommand(funcMainSubFreq,payload,vfo))
    {
        payload.append(vfo);
        payload.append(makeFreqPayload(freq));
        prepDataAndSend(payload);
    }
    else if (getCommand(funcFreqSet,payload))
    {
        payload.append(makeFreqPayload(freq));
        prepDataAndSend(payload);
    }
}

void rigCommander::selectVFO(vfo_t vfo)
{
    // Note, some radios use main/sub,
    // some use A/B,
    // and some appear to use both...
    //    vfoA=0, vfoB=1,vfoMain = 0xD0,vfoSub = 0xD1
    funcs f = (vfo == vfoA)?funcVFOASelect:(vfo == vfoB)?funcVFOBSelect:(vfo == vfoMain)?funcVFOMainSelect:funcVFOSubSelect;
    QByteArray payload;
    if (getCommand(f,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::equalizeVFOsAB()
{
    QByteArray payload;
    if (getCommand(funcVFOEqualAB,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::equalizeVFOsMS()
{
    QByteArray payload;
    if (getCommand(funcVFOEqualMS,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::exchangeVFOs()
{
    // NB: This command exchanges A-B or M-S
    // depending upon the radio.
    QByteArray payload;
    if (getCommand(funcVFOSwapAB,payload))
    {
        prepDataAndSend(payload);
    }
    else if (getCommand(funcVFOSwapMS,payload))
    {
        prepDataAndSend(payload);
    }
}

QByteArray rigCommander::makeFreqPayload(freqt freq)
{
    QByteArray result;
    quint64 freqInt = freq.Hz;

    unsigned char a;
    int numchars = 5;
    for (int i = 0; i < numchars; i++) {
        a = 0;
        a |= (freqInt) % 10;
        freqInt /= 10;
        a |= ((freqInt) % 10)<<4;

        freqInt /= 10;

        result.append(a);
        //printHex(result, false, true);
    }

    return result;
}

QByteArray rigCommander::makeFreqPayload(double freq)
{
    quint64 freqInt = (quint64) (freq * 1E6);

    QByteArray result;
    unsigned char a;
    int numchars = 5;
    for (int i = 0; i < numchars; i++) {
        a = 0;
        a |= (freqInt) % 10;
        freqInt /= 10;
        a |= ((freqInt) % 10)<<4;

        freqInt /= 10;

        result.append(a);
        //printHex(result, false, true);
    }
    //qInfo(logRig()) << "encoded frequency for " << freq << " as int " << freqInt;
    //printHex(result, false, true);
    return result;
}

void rigCommander::setRitEnable(bool ritEnabled)
{
    QByteArray payload;
    if (getCommand(funcRitStatus,payload,static_cast<int>(ritEnabled)))
    {
        payload.append(static_cast<unsigned char>(ritEnabled));
        prepDataAndSend(payload);
    }
}

void rigCommander::getRitEnabled()
{
    QByteArray payload;
    if (getCommand(funcRitStatus,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getRitValue()
{
    QByteArray payload;
    if (getCommand(funcRITFreq,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setRitValue(int ritValue)
{
    QByteArray payload;
    if (getCommand(funcRITFreq,payload,ritValue))
    {
        bool isNegative = false;
        if(ritValue < 0)
        {
            isNegative = true;
            ritValue *= -1;
        }
        QByteArray freqBytes;
        freqt f;
        f.Hz = ritValue;
        freqBytes = makeFreqPayload(f);
        freqBytes.truncate(2);
        payload.append(freqBytes);
        payload.append(QByteArray(1,(char)isNegative));
        prepDataAndSend(payload);
    }
}

void rigCommander::setMode(mode_info m)
{
    foreach (auto&& filter, rigCaps.filters)
    {
        if (filter.num == m.filter)
        {
            // Does this mode support this filter?
            if (!(filter.modes & (1 << m.reg)))
            {
                qInfo(logRig()) << "Filter" << m.filter << "not supported in mode" << m.name << "reg:" << m.reg << "modes:" << QString::number(filter.modes,16);
                m.filter = '\x01'; // Set the default filter if not set
            }
            break;
        }
    }

    QByteArray payload;
    if (getCommand(funcMainSubMode,payload,m.reg))
    {
        payload.append(static_cast<unsigned char>(m.VFO));
        payload.append(m.reg);
        payload.append(static_cast<unsigned char>(m.data));
        payload.append(m.filter);
        prepDataAndSend(payload);
    }
    else if (getCommand(funcModeSet,payload))
    {
        payload.append(m.reg);
        payload.append(m.filter);
        prepDataAndSend(payload);
    }
}

void rigCommander::setMode(unsigned char mode, unsigned char modeFilter)
{
    foreach (mode_info m, rigCaps.modes)
    {
        if (m.reg == mode)
        {
            mode_info mi = mode_info(m);
            mi.filter = modeFilter;
            mi.VFO=selVFO_t::activeVFO;
            setMode(mi);
            break;
        }
    }
}

void rigCommander::setDataMode(bool dataOn, unsigned char filter)
{
    QByteArray payload;
    if (getCommand(funcDataModeWithFilter,payload,static_cast<int>(dataOn)))
    {
        payload.append(static_cast<unsigned char>(dataOn));
        payload.append((dataOn) ? filter : 0x0); // if data mode off, bandwidth not defined per ICD.
        prepDataAndSend(payload);
    }
}

void rigCommander::getFrequency()
{
    getFrequency((unsigned char)0x0);
}

void rigCommander::getFrequency(unsigned char vfo)
{
    QByteArray payload;
    if (getCommand(funcMainSubFreq,payload,vfo))
    {
        payload.append(vfo);
        prepDataAndSend(payload);
    }
    else if (getCommand(funcFreqSet,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getMode()
{
    getMode((unsigned char)0x0);
}
void rigCommander::getMode(unsigned char vfo)
{
    QByteArray payload;
    if (getCommand(funcMainSubMode,payload,vfo))
    {
        payload.append(vfo);
        prepDataAndSend(payload);
    }
    else if (getCommand(funcModeSet,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getDataMode()
{
    QByteArray payload;
    if (getCommand(funcDataModeWithFilter,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getTuningStep()
{
    QByteArray payload;
    if (getCommand(funcTuningStep,payload))
    {
        prepDataAndSend(payload);
        qInfo() << "Getting tuning step";
    }
}

void rigCommander::setTuningStep(unsigned char step)
{
    QByteArray payload;
    if (getCommand(funcTuningStep,payload,static_cast<int>(step)))
    {
        payload.append(static_cast<unsigned char>(step));
        prepDataAndSend(payload);
        qInfo() << "Setting tuning step to" << step;
    }
}


void rigCommander::getSplit()
{
    QByteArray payload;
    if (getCommand(funcSplitStatus,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setSplit(bool splitEnabled)
{
    QByteArray payload;
    if (getCommand(funcSplitStatus,payload,static_cast<int>(splitEnabled)))
    {
        payload.append(static_cast<unsigned char>(splitEnabled));
        prepDataAndSend(payload);
    }
}

void rigCommander::setDuplexMode(duplexMode dm)
{
    QByteArray payload;
    if (getCommand(funcDuplexStatus,payload,static_cast<int>(dm)))
    {
        payload.append(static_cast<unsigned char>(dm));
        prepDataAndSend(payload);
    }
    else
    {
        setSplit(static_cast<bool>(dm));
    }
}

void rigCommander::getDuplexMode()
{
    QByteArray payload;
    if (getCommand(funcDuplexStatus,payload))
    {
        prepDataAndSend(payload);
    }
    else
    {
        getSplit();
    }
}

void rigCommander::setQuickSplit(bool qsOn)
{
    QByteArray payload;
    if (getCommand(funcQuickSplit,payload,static_cast<int>(qsOn)))
    {
        payload.append(static_cast<unsigned char>(qsOn));
        prepDataAndSend(payload);
    }
}

void rigCommander::setPassband(quint16 pass)
{
    /*
    * Passband is calculated as follows, if a higher than legal value is provided,
    * It will set passband to the maximum allowed for the particular mode.
    *
    * Mode                Data        Steps
    * SSB/CW/RTTY/PSK     0 to 9      50 ~ 500 Hz (50 Hz)
    * SSB/CW/PSK          10 to 40    600 Hz ~ 3.6 kHz (100 Hz)
    * RTTY                10 to 31    600 ~ 2.7 kHz (100 Hz)
    * AM                  0 to 49     200 Hz ~ 10.0 kHz (200 Hz)
    * WFM                   NOT SUPPORTED
    */

    // Passband may be fixed?
    unsigned char mode = state.getChar(MODE);
    foreach (mode_info m, rigCaps.modes)
    {
        if (m.reg == mode && m.bw) {
            QByteArray payload;
            if (getCommand(funcFilterWidth,payload,pass))
            {
                unsigned char calc;
                if (mode == modeAM) { // AM 0-49

                    calc = quint16((pass / 200) - 1);
                    if (calc > 49)
                        calc = 49;
                }
                else if (pass >= 600) // SSB/CW/PSK 10-40 (10-31 for RTTY)
                {
                    calc = quint16((pass / 100) + 4);
                    if (((calc > 31) && (mode == modeRTTY || mode == modeRTTY_R)))
                    {
                        calc = 31;
                    }
                    else if (calc > 40) {
                        calc = 40;
                    }
                }
                else {  // SSB etc 0-9
                    calc = quint16((pass / 50) - 1);
                }

                char tens = (calc / 10);
                char units = (calc - (10 * tens));

                char b1 = (units) | (tens << 4);

                payload.append(b1);
                prepDataAndSend(payload);
            }
        }
    }

}

void rigCommander::getPassband()
{
    // Passband may be fixed?
    unsigned char mode = state.getChar(MODE);
    foreach (mode_info m, rigCaps.modes)
    {
        if (m.reg == mode && m.bw)
        {
            QByteArray payload;
            if (getCommand(funcFilterWidth,payload))
            {
                prepDataAndSend(payload);
            }
        }
    }
}

void rigCommander::getCwPitch()
{
    QByteArray payload;
    if (getCommand(funcCwPitch,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setCwPitch(unsigned char pitch)
{
    QByteArray payload;
    if (getCommand(funcCwPitch,payload,pitch))
    {
        payload.append(bcdEncodeInt(pitch));
        prepDataAndSend(payload);
    }
}

void rigCommander::getDashRatio()
{
    QByteArray payload;
    if (getCommand(funcDashRatio,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setDashRatio(unsigned char ratio)
{
    QByteArray payload;
    if (getCommand(funcDashRatio,payload,ratio))
    {
        payload.append(bcdEncodeInt(ratio).at(1)); // Discard first byte
        prepDataAndSend(payload);
    }
}

void rigCommander::getPskTone()
{
    QByteArray payload;
    if (getCommand(funcPSKTone,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setPskTone(unsigned char tone)
{
    QByteArray payload;
    if (getCommand(funcPSKTone,payload,tone))
    {
        prepDataAndSend(payload);
        payload.append(bcdEncodeInt(tone));
    }
}

void rigCommander::getRttyMark()
{
    QByteArray payload;
    if (getCommand(funcRTTYMarkTone,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setRttyMark(unsigned char mark)
{
    QByteArray payload;
    if (getCommand(funcRTTYMarkTone,payload,mark))
    {
        prepDataAndSend(payload);
        payload.append(bcdEncodeInt(mark));
    }
}

void rigCommander::getTransmitFrequency()
{
    QByteArray payload;
    if (getCommand(funcReadTXFreq,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setTone(quint16 tone)
{
    rptrTone_t t;
    t.tone = tone;
    setTone(t);
}

void rigCommander::setTone(rptrTone_t t)
{
    QByteArray payload;
    if (getCommand(funcMainSubPrefix,payload))
    {
        payload.append(static_cast<unsigned char>(t.useSecondaryVFO));
    }

    if (getCommand(funcRepeaterTone,payload,static_cast<int>(t.tone)))
    {
        payload.append(encodeTone(t.tone));
        prepDataAndSend(payload);
    }
}

void rigCommander::setTSQL(quint16 t)
{
    rptrTone_t tn;
    tn.tone = t;
    setTSQL(tn);
}

void rigCommander::setTSQL(rptrTone_t t)
{
    QByteArray payload;
    if (getCommand(funcMainSubPrefix,payload))
    {
        payload.append(static_cast<unsigned char>(t.useSecondaryVFO));
    }

    if (getCommand(funcRepeaterTSQL,payload,static_cast<int>(t.tone)))
    {
        payload.append(encodeTone(t.tone));
        prepDataAndSend(payload);
    }
}

void rigCommander::setDTCS(quint16 dcscode, bool tinv, bool rinv)
{
    QByteArray payload;
    if (getCommand(funcRepeaterDTCS,payload,static_cast<int>(dcscode)))
    {
        payload.append(encodeTone(dcscode, tinv, rinv));
        prepDataAndSend(payload);
    }
}

QByteArray rigCommander::encodeTone(quint16 tone)
{
    return encodeTone(tone, false, false);
}

QByteArray rigCommander::encodeTone(quint16 tone, bool tinv, bool rinv)
{
    // This function is fine to use for DTCS and TONE
    QByteArray enct;

    unsigned char inv=0;
    inv = inv | (unsigned char)rinv;
    inv = inv | ((unsigned char)tinv) << 4;

    enct.append(inv);

    unsigned char hundreds = tone / 1000;
    unsigned char tens = (tone-(hundreds*1000)) / 100;
    unsigned char ones = (tone -(hundreds*1000)-(tens*100)) / 10;
    unsigned char dec =  (tone -(hundreds*1000)-(tens*100)-(ones*10));

    enct.append(tens | (hundreds<<4));
    enct.append(dec | (ones <<4));

    return enct;
}

quint16 rigCommander::decodeTone(QByteArray eTone)
{
    bool t;
    bool r;
    return decodeTone(eTone, t, r);
}

quint16 rigCommander::decodeTone(QByteArray eTone, bool &tinv, bool &rinv)
{
    // index:  00 01  02 03 04
    // CTCSS:  1B 01  00 12 73 = PL 127.3, decode as 1273
    // D(T)CS: 1B 01  TR 01 23 = T/R Invert bits + DCS code 123

    if (eTone.length() < 5) {
        return 0;
    }
    tinv = false; rinv = false;
    quint16 result = 0;

    if((eTone.at(2) & 0x01) == 0x01)
        tinv = true;
    if((eTone.at(2) & 0x10) == 0x10)
        rinv = true;

    result += (eTone.at(4) & 0x0f);
    result += ((eTone.at(4) & 0xf0) >> 4) *   10;
    result += (eTone.at(3) & 0x0f) *  100;
    result += ((eTone.at(3) & 0xf0) >> 4) * 1000;

    return result;
}

void rigCommander::getTone()
{
    QByteArray payload;
    if (getCommand(funcRepeaterTone,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getTSQL()
{
    QByteArray payload;
    if (getCommand(funcRepeaterTSQL,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getDTCS()
{
    QByteArray payload;
    if (getCommand(funcRepeaterDTCS,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getRptAccessMode()
{
    QByteArray payload;
    if (getCommand(funcToneSquelchType,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setRptAccessMode(rptAccessTxRx ratr)
{
    rptrAccessData_t rd;
    rd.accessMode = ratr;
    setRptAccessMode(rd);
}

void rigCommander::setRptAccessMode(rptrAccessData_t rd)
{
    // NB: This function is the only recommended
    // function to be used for toggling tone and tone squelch.

    QByteArray payload;
    if (getCommand(funcMainSubPrefix,payload))
    {
        payload.append(static_cast<unsigned char>(rd.useSecondaryVFO));
    }

    if (getCommand(funcToneSquelchType,payload))
    {
        payload.append((unsigned char)rd.accessMode);
        prepDataAndSend(payload);
    }
    else
    {
        // These radios either don't support DCS or
        // we just haven't added DCS yet.

        switch(rd.accessMode)
        {
        case ratrNN:
        {
            // No tone at all
            if(rd.turnOffTone)
            {
                if (getCommand(funcRepeaterTone,payload))
                {
                    payload.append('\x00');
                    prepDataAndSend(payload);
                }
            }
            else if (rd.turnOffTSQL)
            {
                if (getCommand(funcRepeaterTSQL,payload))
                {
                    payload.append('\x00');
                    prepDataAndSend(payload);
                }
            }
            break;
        }
        case ratrTN:
        {
            if (getCommand(funcRepeaterTone,payload))
            {
                payload.append('\x01');
                prepDataAndSend(payload);
            }
            break;
        }
        case ratrTT:
        case ratrNT:
        {
            if (getCommand(funcRepeaterTSQL,payload))
            {
                payload.append('\x01');
                prepDataAndSend(payload);
            }
            break;
        }
        default:
            qWarning(logRig()) << "Cannot set tone mode" << (unsigned char)rd.accessMode << "on rig model" << rigCaps.modelName;
            return;
        }
    }
}

void rigCommander::setRptDuplexOffset(freqt f)
{
    QByteArray payload;
    if (getCommand(funcSendFreqOffset,payload))
    {
        payload.append(makeFreqPayload(f).mid(1,3));
        prepDataAndSend(payload);
    }
}

void rigCommander::getRptDuplexOffset()
{
    QByteArray payload;
    if (getCommand(funcReadFreqOffset,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setIPP(bool enabled)
{
    QByteArray payload;
    if (getCommand(funcIPPlus,payload,static_cast<int>(enabled)))
    {
        payload.append(static_cast<unsigned char>(enabled));
        prepDataAndSend(payload);
    }
}

void rigCommander::getIPP()
{
    QByteArray payload;
    if (getCommand(funcIPPlus,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setSatelliteMode(bool enabled)
{
    QByteArray payload;
    if (getCommand(funcSatelliteMode,payload,static_cast<int>(enabled)))
    {
        payload.append(static_cast<unsigned char>(enabled));
        prepDataAndSend(payload);
    }
}

void rigCommander::getSatelliteMode()
{
    QByteArray payload;
    if (getCommand(funcSatelliteMode,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getPTT()
{
    QByteArray payload;
    if (getCommand(funcTransceiverStatus,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getBandStackReg(char band, char regCode)
{
    QByteArray payload;
    if (getCommand(funcBandStackReg,payload,band))
    {
        payload.append(band); // [01 through 11]
        payload.append(regCode); // [01...03]. 01 = latest, 03 = oldest
        prepDataAndSend(payload);
    }
}

void rigCommander::setPTT(bool pttOn)
{
    QByteArray payload;
    if (pttAllowed && getCommand(funcTransceiverStatus,payload,static_cast<int>(pttOn)))
    {
        payload.append(static_cast<unsigned char>(pttOn));
        prepDataAndSend(payload);
    }
}

void rigCommander::sendCW(QString textToSend)
{
    QByteArray payload;
    if (pttAllowed && getCommand(funcSendCW,payload,textToSend.length()))
    {
        QByteArray textData = textToSend.toLocal8Bit();
        unsigned char p=0;
        bool printout=false;
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
                (p==0x40) || (p==0x20) )
            {
                // Allowed character, continue
            } else {
                qWarning(logRig()) << "Invalid character detected in CW message at position " << c << ", the character is " << textToSend.at(c);
                printout = true;
                textData[c] = 0x3F; // "?"
            }
        }
        if(printout)
            printHex(textData);

        payload.append(textData);
        prepDataAndSend(payload);
    }
}

void rigCommander::sendStopCW()
{
    QByteArray payload;
    if (getCommand(funcSendCW,payload))
    {
        payload.append("\xFF");
        prepDataAndSend(payload);
    }
}

void rigCommander::setCIVAddr(unsigned char civAddr)
{
    // Note: This sets the radio's CIV address
    // the computer's CIV address is defined in the header file.

    this->civAddr = civAddr;
    payloadPrefix = QByteArray("\xFE\xFE");
    payloadPrefix.append(civAddr);
    payloadPrefix.append((char)compCivAddr);
}

void rigCommander::handleNewData(const QByteArray& data)
{
    emit haveDataForServer(data);
    parseData(data);
}

void rigCommander::receiveAudioData(const audioPacket& data)
{
    emit haveAudioData(data);
}

void rigCommander::parseData(QByteArray dataInput)
{
    // TODO: Clean this up.
    // It finally works very nicely, needs to be streamlined.
    //

    int index = 0;
    volatile int count = 0; // debug purposes

    // use this:
    QList <QByteArray> dataList = dataInput.split('\xFD');
    QByteArray data;
    // qInfo(logRig()) << "data list has this many elements: " << dataList.size();
    if (dataList.last().isEmpty())
    {
        dataList.removeLast(); // if the original ended in FD, then there is a blank entry at the end.
    }
    // Only thing is, each frame is missing '\xFD' at the end. So append! Keeps the frames intact.
    for(index = 0; index < dataList.count(); index++)
    {
        data = dataList[index];
        data.append('\xFD'); // because we expect it to be there.
    // foreach(listitem)
    // listitem.append('\xFD');
    // continue parsing...

        count++;
        // Data echo'd back from the rig start with this:
        // fe fe 94 e0 ...... fd

        // Data from the rig that is not an echo start with this:
        // fe fe e0 94 ...... fd (for example, a reply to a query)

        // Data from the rig that was not asked for is sent to controller 0x00:
        // fe fe 00 94 ...... fd (for example, user rotates the tune control or changes the mode)

        //qInfo(logRig()) << "Data received: ";
        //printHex(data, false, true);
        if(data.length() < 4)
        {
            if(data.length())
            {
                // Finally this almost never happens
                // qInfo(logRig()) << "Data length too short: " << data.length() << " bytes. Data:";
                //printHex(data, false, true);
            }
            // no
            //return;
            // maybe:
            // continue;
        }

        if(!data.startsWith("\xFE\xFE"))
        {
            // qInfo(logRig()) << "Warning: Invalid data received, did not start with FE FE.";
            // find 94 e0 and shift over,
            // or look inside for a second FE FE
            // Often a local echo will miss a few bytes at the beginning.
            if(data.startsWith('\xFE'))
            {
                data.prepend('\xFE');
                // qInfo(logRig()) << "Warning: Working with prepended data stream.";
                parseData(payloadIn);
                return;
            } else {
                //qInfo(logRig()) << "Error: Could not reconstruct corrupted data: ";
                //printHex(data, false, true);
                // data.right(data.length() - data.find('\xFE\xFE'));
                // if found do not return and keep going.
                return;
            }
        }

        if((unsigned char)data[02] == civAddr)
        {
            // data is or begins with an echoback from what we sent
            // find the first 'fd' and cut it. Then continue.
            //payloadIn = data.right(data.length() - data.indexOf('\xfd')-1);
            // qInfo(logRig()) << "[FOUND] Trimmed off echo:";
            //printHex(payloadIn, false, true);
            //parseData(payloadIn);
            //return;
        }

        incomingCIVAddr = data[03]; // track the CIV of the sender.

        switch(data[02])
        {
            //    case civAddr: // can't have a variable here :-(
            //        // data is or begins with an echoback from what we sent
            //        // find the first 'fd' and cut it. Then continue.
            //        payloadIn = data.right(data.length() - data.indexOf('\xfd')-1);
            //        //qInfo(logRig()) << "Trimmed off echo:";
            //        //printHex(payloadIn, false, true);
            //        parseData(payloadIn);
            //        break;
            // case '\xE0':

            case (char)0xE0:
            case (char)compCivAddr:
                // data is a reply to some query we sent
                // extract the payload out and parse.
                // payload = getpayload(data); // or something
                // parse (payload); // recursive ok?
                payloadIn = data.right(data.length() - 4);
                if(payloadIn.contains("\xFE"))
                {
                    //qDebug(logRig()) << "Corrupted data contains FE within message body: ";
                    //printHex(payloadIn);
                    break;
                }
                parseCommand();
                break;
            case '\x00':
                // data send initiated by the rig due to user control
                // extract the payload out and parse.
                if((unsigned char)data[03]==compCivAddr)
                {
                    // This is an echo of our own broadcast request.
                    // The data are "to 00" and "from E1"
                    // Don't use it!
                    qDebug(logRig()) << "Caught it! Found the echo'd broadcast request from us! Rig has not responded to broadcast query yet.";
                } else {
                    payloadIn = data.right(data.length() - 4); // Removes FE FE E0 94 part
                    if(payloadIn.contains("\xFE"))
                    {
                        //qDebug(logRig()) << "Corrupted data contains FE within message body: ";
                        //printHex(payloadIn);
                        break;
                    }
                    parseCommand();
                }
                break;
            default:
                // could be for other equipment on the CIV network.
                // just drop for now.
                // relaySendOutData(data);
                break;
        }
    }
    /*
    if(dataList.length() > 1)
    {
        qInfo(logRig()) << "Recovered " << count << " frames from single data with size" << dataList.count();
    }
    */
}

void rigCommander::parseCommand()
{


    // note: data already is trimmed of the beginning FE FE E0 94 stuff.
    if (!haveRigCaps)
    {
        if  (payloadIn[0] == '\x19') {
            model = determineRadioModel(payloadIn[2]); // verify this is the model not the CIV
            rigCaps.modelID = payloadIn[2];
            determineRigCaps();
            qInfo(logRig()) << "Have rig ID: decimal: " << (unsigned int)rigCaps.modelID;
        }
        return; // We can't do anything else until we have a rig model.
    }

#ifdef DEBUG_PARSE
    QElapsedTimer performanceTimer;
    performanceTimer.start();
#endif

    funcs func = funcNone;

    // Many commands are single character so it makes sense to start there I think?
    for (int i=1;i<=4;i++)
    {
        auto it = rigCaps.commandsReverse.find(payloadIn.left(i));
        if (it != rigCaps.commandsReverse.end())
        {
            func = it.value();
            break;
        }
    }

 /*
    QByteArray search = payloadIn.left(4); // Maximum length of command to search.

    while (search.length()>0)
    {
        auto it = rigCaps.commandsReverse.find(search);
        if (it != rigCaps.commandsReverse.end())
        {
            func = it.value();
            break;
        }
        search.removeLast();
    }
*/

#ifdef DEBUG_PARSE
    int currentParse=performanceTimer.nsecsElapsed();
#endif

    if (!rigCaps.commands.contains(func)) {
        qInfo(logRig()) << "Unsupported command received from rig" << funcString[func] << "Check rig file (" << payloadIn.toHex() << ")";
        return;
    }

    switch (func)
    {
    case funcFreqGet:
    case funcfreqTR:
    case funcReadTXFreq:
        parseFrequency();
        break;
    case funcVFODualWatch:
        // Not currently used, but will report the current dual-watch status
        break;
    case funcMainSubFreq:
        emit haveFrequency(parseFrequency(payloadIn, 5));
        break;
    case funcModeGet:
    case funcModeTR:
        parseMode();
        break;
    case funcMainSubMode:
        if((int)payloadIn[1] == 0) // VFOA only currently
        {
             emit haveDataMode((bool)payloadIn[03]);
             state.set(DATAMODE, (quint8)payloadIn[3], false);
             this->parseMode();
        }
        break;
    case funcMemoryContents:
    case funcMemoryClear:
    case funcMemoryKeyer:
    case funcMemoryToVFO:
    case funcMemoryWrite:
        break;
    case funcScanning:
        break;
    case funcReadFreqOffset:
        emit haveRptOffsetFrequency(parseFrequencyRptOffset(payloadIn));
        break;
    case funcSplitStatus:
    case funcDuplexStatus:
        emit haveDuplexMode((duplexMode)(unsigned char)payloadIn[1]);
        state.set(DUPLEX, (duplexMode)(unsigned char)payloadIn[1], false);
        break;
    case funcTuningStep:
        // We could use this to update the current tuning step from the radio
        // although we use many more different steps!
        emit haveTuningStep((unsigned char)payloadIn[1]);
    case funcAttenuator:
        emit haveAttenuator((unsigned char)payloadIn.at(1));
        state.set(ATTENUATOR, (quint8)payloadIn[1], false);
        break;
    case funcAntenna:
        emit haveAntenna((unsigned char)payloadIn.at(1), (bool)payloadIn.at(2));
        state.set(ANTENNA, (quint8)payloadIn[1], false);
        state.set(RXANTENNA, (bool)payloadIn[2], false);
        break;
    // Register 13 (speech) has no get values
    // Register 14 (levels) starts here:
    case funcAfGain:
        if (udp == Q_NULLPTR) {
             emit haveAfGain(bcdHexToUChar(payloadIn[2],payloadIn[3]));
             state.set(AFGAIN, bcdHexToUChar(payloadIn[2],payloadIn[3]), false);
        } else {
             state.set(AFGAIN, localVolume, false);
        }
        break;
    case funcRfGain:
        emit haveRfGain(bcdHexToUChar(payloadIn[2],payloadIn[3]));
        state.set(RFGAIN, bcdHexToUChar(payloadIn[2],payloadIn[3]), false);
        break;
    case funcSquelch:
        emit haveSql(bcdHexToUChar(payloadIn[2],payloadIn[3]));
        state.set(SQUELCH, bcdHexToUChar(payloadIn[2],payloadIn[3]), false);
        break;
    case funcAPFLevel:
        break;
    case funcNRLevel:
        emit haveNRLevel(bcdHexToUChar(payloadIn[2],payloadIn[3]));
        state.set(NR, bcdHexToUChar(payloadIn[2],payloadIn[3]), false);
	break;
    case funcPBTInner:
        emit haveTPBFInner(bcdHexToUChar(payloadIn[2],payloadIn[3]));
        state.set(PBTIN, bcdHexToUChar(payloadIn[2],payloadIn[3]), false);
        break;
    case funcPBTOuter:
        emit haveTPBFOuter(bcdHexToUChar(payloadIn[2],payloadIn[3]));
        state.set(PBTOUT, bcdHexToUChar(payloadIn[2],payloadIn[3]), false);
        break;
    case funcIFShift:
        emit haveIFShift(bcdHexToUChar(payloadIn[2],payloadIn[3]));
        break;
    case funcCwPitch:
        emit haveCwPitch(bcdHexToUChar(payloadIn[2],payloadIn[3]));
        state.set(CWPITCH, bcdHexToUChar(payloadIn[2],payloadIn[3]), false);
        break;
    case funcRFPower:
        emit haveTxPower(bcdHexToUChar(payloadIn[2],payloadIn[3]));
        state.set(RFPOWER, bcdHexToUChar(payloadIn[2],payloadIn[3]), false);
        break;
    case funcMicGain:
        emit haveNRLevel(bcdHexToUChar(payloadIn[2],payloadIn[3]));
        state.set(MICGAIN, bcdHexToUChar(payloadIn[2],payloadIn[3]), false);
        break;
    case funcKeySpeed:
        emit haveKeySpeed(bcdHexToUChar(payloadIn[2],payloadIn[3]));
        state.set(KEYSPD, bcdHexToUChar(payloadIn[2],payloadIn[3]), false);
        break;
    case funcNotchFilter:
        state.set(NOTCHF, bcdHexToUChar(payloadIn[2],payloadIn[3]), false);
        // Not implemented
        // emit haveNotch(bcdHexToUChar(payloadIn[2],payloadIn[3]));
        break;
    case funcCompressorLevel:
        emit haveCompLevel(bcdHexToUChar(payloadIn[2],payloadIn[3]));
        state.set(COMPLEVEL, bcdHexToUChar(payloadIn[2],payloadIn[3]), false);
        break;
    case funcBreakInDelay:
        break;
    case funcNBLevel:
        emit haveNBLevel(bcdHexToUChar(payloadIn[2],payloadIn[3]));
        state.set(NB, bcdHexToUChar(payloadIn[2],payloadIn[3]), false);
        break;
    case funcDigiSelShift:
        break;
    case funcDriveGain:
        break;
    case funcMonitorGain:
        emit haveMonitorGain(bcdHexToUChar(payloadIn[2],payloadIn[3]));
        state.set(MONITORLEVEL, bcdHexToUChar(payloadIn[2],payloadIn[3]), false);
        break;
    case funcVoxGain:
        emit haveVoxGain(bcdHexToUChar(payloadIn[2],payloadIn[3]));
        state.set(VOXGAIN, bcdHexToUChar(payloadIn[2],payloadIn[3]), false);
        break;
    case funcAntiVoxGain:
        emit haveAntiVoxGain(bcdHexToUChar(payloadIn[2],payloadIn[3]));
        state.set(ANTIVOXGAIN, bcdHexToUChar(payloadIn[2],payloadIn[3]), false);
        break;
    // 0x15 Meters
    case funcSMeterSqlStatus:
        break;
    case funcSMeter:
        emit haveMeter(meterS,bcdHexToUChar(payloadIn[2],payloadIn[3]));
        state.set(SMETER, bcdHexToUChar(payloadIn[2],payloadIn[3]), false);
        break;
    case funcVariousSql:
    case funcOverflowStatus:
        break;
    case funcCenterMeter:
        emit haveMeter(meterCenter,bcdHexToUChar(payloadIn[2],payloadIn[3]));
        break;
    case funcPowerMeter:
        emit haveMeter(meterPower,bcdHexToUChar(payloadIn[2],payloadIn[3]));
        state.set(POWERMETER, bcdHexToUChar(payloadIn[2],payloadIn[3]), false);
        break;
    case funcSWRMeter:
        emit haveMeter(meterSWR,bcdHexToUChar(payloadIn[2],payloadIn[3]));
        state.set(SWRMETER, bcdHexToUChar(payloadIn[2],payloadIn[3]), false);
        break;
    case funcALCMeter:
        emit haveMeter(meterALC,bcdHexToUChar(payloadIn[2],payloadIn[3]));
        state.set(ALCMETER, bcdHexToUChar(payloadIn[2],payloadIn[3]), false);
        break;
    case funcCompMeter:
        emit haveMeter(meterComp,bcdHexToUChar(payloadIn[2],payloadIn[3]));
        state.set(COMPMETER, bcdHexToUChar(payloadIn[2],payloadIn[3]), false);
        break;
    case funcVdMeter:
        emit haveMeter(meterVoltage,bcdHexToUChar(payloadIn[2],payloadIn[3]));
        state.set(VOLTAGEMETER, bcdHexToUChar(payloadIn[2],payloadIn[3]), false);
        break;
    case funcIdMeter:
        emit haveMeter(meterCurrent,bcdHexToUChar(payloadIn[2],payloadIn[3]));
        state.set(CURRENTMETER, bcdHexToUChar(payloadIn[2],payloadIn[3]), false);
        break;
    // 0x16 enable/disable functions:
    case funcPreamp:
        emit havePreamp((unsigned char)payloadIn.at(2));
        state.set(PREAMP, (quint8)payloadIn.at(2), false);
        break;
    case funcAGCTime:
        state.set(AGC, (quint8)payloadIn[2], false);
        break;
    case funcNoiseBlanker:
        emit haveNB(payloadIn.at(2) != 0);
        state.set(NBFUNC, payloadIn.at(2) != 0, false);
        break;
    case funcAudioPeakFilter:
        break;
    case funcNoiseReduction:
        emit haveNR(payloadIn.at(2) != 0);
        state.set(NRFUNC, payloadIn.at(2) != 0, false);
        break;
    case funcAutoNotch:
        state.set(ANFFUNC, payloadIn.at(2) != 0, false);
        break;
    case funcRepeaterTone:
    {
        rptAccessTxRx ra;
        if(payloadIn.at(2)==1)
        {
             ra = ratrTONEon;
        } else {
             ra = ratrTONEoff;
        }
        emit haveRptAccessMode(ra);
        state.set(TONEFUNC, payloadIn.at(2) != 0, false);
        break;
    }
    case funcRepeaterTSQL:
    {
        rptAccessTxRx ra;
        if(payloadIn.at(2)==1)
        {
             ra = ratrTSQLon;
        } else {
             ra = ratrTSQLoff;
        }
        emit haveRptAccessMode(ra);
        state.set(TSQLFUNC, payloadIn.at(2) != 0, false);
        break;
    }
    case funcRepeaterDTCS:
    case funcRepeaterCSQL:
        break;
    case funcCompressor:
        emit haveComp(payloadIn.at(2) != 0);
        state.set(COMPFUNC, payloadIn.at(2) != 0, false);
        break;
    case funcMonitor:
        emit haveMonitor(payloadIn.at(2) != 0);
        state.set(MONFUNC, payloadIn.at(2) != 0, false);
        break;
    case funcVox:
        emit haveVox(payloadIn.at(2) != 0);
        state.set(VOXFUNC, payloadIn.at(2) != 0, false);
        break;
    case funcBreakIn:
    {
        if (payloadIn.at(2) == '\00') {
             state.set(FBKINFUNC, false, false);
             state.set(SBKINFUNC, false, false);
        }
        else if (payloadIn.at(2) == '\01') {
             state.set(FBKINFUNC, false, false);
             state.set(SBKINFUNC, true, false);

        }
        else if (payloadIn.at(2) == '\02') {
             state.set(FBKINFUNC, true, false);
             state.set(SBKINFUNC, false, false);
        }
        emit haveCWBreakMode(payloadIn.at(2));
        break;
    }
    case funcManualNotch:
        state.set(MNFUNC, payloadIn.at(2) != 0, false);
        break;
    case funcDigiSel:
        break;
    case funcTwinPeakFilter:
        break;
    case funcDialLock:
        state.set(LOCKFUNC, payloadIn.at(2) != 0, false);
        break;
    case funcRXAntenna:
        break;
    case funcDSPIFFilter:
        break;
    case funcNotchWidth:
        break;
    case funcSSBBandwidth:
        break;
    case funcMainSubTracking:
    case funcIPPlus:
        break;
    // 0x17 is CW send and 0x18 is power control (no reply)
    // 0x19 should never appear as we already have the rigID.
    case funcTransceiverId:
        model = determineRadioModel(payloadIn[2]); // verify this is the model not the CIV
        rigCaps.modelID = payloadIn[2];
        determineRigCaps();
        qInfo(logRig()) << "Have rig ID: decimal: " << (unsigned int)rigCaps.modelID;
        break;
    // 0x1a
    case funcBandStackReg:
        parseBandStackReg();
        break;
    case funcFilterWidth:
    {
        quint16 calc;
        quint8 pass = bcdHexToUChar((quint8)payloadIn[2]);
        if (state.getChar(MODE) == modeAM) {
             calc = 200 + (pass * 200);
        }
        else if (pass <= 10)
        {
             calc = 50 + (pass * 50);
        }
        else {
             calc = 600 + ((pass - 10) * 100);
        }
        emit havePassband(calc);
        state.set(PASSBAND, calc, false);
        break;
    }
    case funcDataModeWithFilter:
        emit haveDataMode((bool)payloadIn[03]);
        state.set(DATAMODE, (quint8)payloadIn[3], false);
        break;
    case funcAFMute:
        state.set(MUTEFUNC, (quint8)payloadIn[2], false);
        break;
    // 0x1a 0x05 various registers!
    case funcREFAdjust:
        emit haveRefAdjustCourse(bcdHexToUChar(payloadIn[4],payloadIn[5]));
        break;
    case funcREFAdjustFine:
        emit haveRefAdjustFine(bcdHexToUChar(payloadIn[4],payloadIn[5]));
        break;
    case funcACC1ModLevel:
        emit haveACCGain(bcdHexToUChar(payloadIn[4],payloadIn[5]),5); // This might need some work?
        break;
    case funcUSBModLevel:
        emit haveUSBGain(bcdHexToUChar(payloadIn[4],payloadIn[5]));
        break;
    case funcLANModLevel:
        emit haveLANGain(bcdHexToUChar(payloadIn[4],payloadIn[5]));
        break;
    case funcDATAOffMod:
        emit haveModInput((inputTypes)bcdHexToUChar(payloadIn[4]),false);
        break;
    case funcDATA1Mod:
        emit haveModInput((inputTypes)bcdHexToUChar(payloadIn[4]),true);
        break;
    case funcDATA2Mod:
        break;
    case funcDATA3Mod:
        break;
    case funcDashRatio:
        emit haveDashRatio(bcdHexToUChar(payloadIn[4]));
	break;
    // 0x1b register
    case funcToneFreq:
        emit haveTone(decodeTone(payloadIn));
        state.set(CTCSS, decodeTone(payloadIn), false);
        break;
    case funcTSQLFreq:
        emit haveTSQL(decodeTone(payloadIn));
        state.set(TSQL, decodeTone(payloadIn), false);
        break;
    case funcDCSFreq:
    {
        quint16 tone=0;
        bool tinv = false;
        bool rinv = false;
        tone = decodeTone(payloadIn, tinv, rinv);
        emit haveDTCS(tone, tinv, rinv);
        state.set(DTCS, tone, false);
        break;
    }
    case funcCSQLSetting:
        emit haveTone(decodeTone(payloadIn));
        state.set(CSQL, decodeTone(payloadIn), false);
        break;
    // 0x1c register
    case funcTransceiverStatus:
        parsePTT();
        break;
    case funcTunerStatus:
        parseATU();
        break;
    // 0x21 RIT:
    case funcRITFreq:
    {
        int ritHz = 0;
        freqt f;
        QByteArray longfreq;
        longfreq = payloadIn.mid(2,2);
        longfreq.append(QByteArray(3,'\x00'));
        f = parseFrequency(longfreq, 3);
        if(payloadIn.length() < 5)
             break;
        ritHz = f.Hz*((payloadIn.at(4)=='\x01')?-1:1);
        emit haveRitFrequency(ritHz);
        state.set(RITVALUE, ritHz, false);
        break;
    }
    case funcRitStatus:
        emit haveRitEnabled(static_cast<bool>(payloadIn.at(02)));
        state.set(RITFUNC, static_cast<bool>(payloadIn.at(02)), false);
        break;
    // 0x27
    case funcScopeWaveData:
        parseSpectrum();
        break;
    case funcScopeOnOff:
        // confirming scope is on
        state.set(SCOPEFUNC, (bool)payloadIn[2], true);
        break;
    case funcScopeDataOutput:
        // confirming output enabled/disabled of wf data.
        break;
    case funcScopeMainSub:
        // This tells us whether we are receiving main or sub data
        break;
    case funcScopeSingleDual:
        // This tells us whether we are receiving single or dual scopes
        break;
    case funcScopeCenterFixed:
        // fixed or center
        // [1] 0x14
        // [2] 0x00
        // [3] 0x00 (center), 0x01 (fixed), 0x02, 0x03
        emit haveSpectrumMode(static_cast<spectrumMode>((unsigned char)payloadIn[3]));
        break;
    case funcScopeCenterSpan:
        // read span in center mode
        // [1] 0x15
        // [2] to [8] is span encoded as a frequency
        emit haveScopeSpan(parseFrequency(payloadIn, 6), static_cast<bool>(payloadIn.at(2)));
        break;
    case funcScopeEdgeNumber:
        // read edge mode center in edge mode
        // [1] 0x16
        // [2] 0x01, 0x02, 0x03: Edge 1,2,3
        emit haveScopeEdge((char)payloadIn[2]);
        break;
    case funcScopeHold:
        // Hold status (only 9700?)
        break;
    case funcScopeRef:
        // scope reference level
        // [1] 0x19
        // [2] 0x00
        // [3] 10dB digit, 1dB digit
        // [4] 0.1dB digit, 0
        // [5] 0x00 = +, 0x01 = -
        parseSpectrumRefLevel();
        break;
    case funcScopeSpeed:
    case funcScopeDuringTX:
    case funcScopeCenterType:
    case funcScopeVBW:
    case funcScopeFixedFreq:
    case funcScopeRBW:
        break;
    // 0x28
    case funcVoiceTX:
        break;
    //0x29 - Prefix certain commands with this to get/set certain values without changing current VFO
    // If we use it for get commands, need to parse the \x29\x<VFO> first.
    case funcMainSubPrefix:
        break;
    case funcFB:
        break;
    case funcFA:
        qInfo(logRig()) << "Error (FA) received from rig.";
        printHex(payloadIn, false ,true);
        break;
    default:
        qWarning(logRig()) << "Unhandled command received from rig" << payloadIn.toHex() << "Contact support!";
        break;
    }

    if( (func != funcScopeWaveData) && (payloadIn[00] != '\x15') )
    {
        // We do not log spectrum and meter data,
        // as they tend to clog up any useful logging.
        qDebug(logRigTraffic()) << "Received from radio:";
        printHexNow(payloadIn, logRigTraffic());
    }


#ifdef DEBUG_PARSE
    averageParseTime += currentParse;
    if (lowParse > currentParse)
        lowParse = currentParse;
    else if (highParse < currentParse)
        highParse = currentParse;

    numParseSamples++;
    if (lastParseReport.msecsTo(QTime::currentTime()) >= 10000)
    {
        qInfo(logRig()) << QString("10 second average command parse time %0 ns (low=%1, high=%2, num=%3:").arg(averageParseTime/numParseSamples).arg(lowParse).arg(highParse).arg(numParseSamples);
        averageParseTime = 0;
        numParseSamples = 0;
        lowParse=9999;
        highParse=0;
        lastParseReport = QTime::currentTime();
    }
#endif
}

void rigCommander::parseLevels()
{
    //qInfo(logRig()) << "Received a level status readout: ";
    // printHex(payloadIn, false, true);

    // wrong: unsigned char level = (payloadIn[2] * 100) + payloadIn[03];
    unsigned char hundreds = payloadIn[2];
    unsigned char tens = (payloadIn[3] & 0xf0) >> 4;
    unsigned char units = (payloadIn[3] & 0x0f);

    unsigned char level = ((unsigned char)100*hundreds) + (10*tens) + units;

    //qInfo(logRig()) << "Level is: " << (int)level << " or " << 100.0*level/255.0 << "%";

    // Typical RF gain response (rather low setting):
    // "INDEX: 00 01 02 03 04 "
    // "DATA:  14 02 00 78 fd "

    if(payloadIn[0] == '\x14')
    {
        switch(payloadIn[1])
        {
            case '\x01':
                // AF level - ignore if LAN connection.
                if (udp == Q_NULLPTR) {
                    emit haveAfGain(level);
                    state.set(AFGAIN, level, false);
                }
                else {
                    state.set(AFGAIN, localVolume, false);
                }
                break;
            case '\x02':
                // RX RF Gain
                emit haveRfGain(level);
                state.set(RFGAIN, level, false);
                break;
            case '\x03':
                // Squelch level
                emit haveSql(level);                
                state.set(SQUELCH, level, false);
                break;
            case '\x07':
                // Twin BPF Inner, or, IF-Shift level
                if(rigCaps.commands.contains(funcPBTInner))
                    emit haveTPBFInner(level);
                else
                    emit haveIFShift(level);
                state.set(PBTIN, level, false);
                break;
            case '\x08':
                // Twin BPF Outer
                emit haveTPBFOuter(level);
                state.set(PBTOUT, level, false);
                break;
            case '\x06':
                // NR Level
                emit haveNRLevel(level);
                state.set(NR, level, false);
                break;
            case '\x09':
                // CW Pitch
                emit haveCwPitch(level);
                state.set(CWPITCH, level, false);
                break;
            case '\x0A':
                // TX RF level
                emit haveTxPower(level);
                state.set(RFPOWER, level, false);
                break;
            case '\x0B':
                // Mic Gain
                emit haveMicGain(level);
                state.set(MICGAIN, level, false);
                break;
            case '\x0C':
                state.set(KEYSPD, level, false);
                //qInfo(logRig()) << "Have received key speed in RC, raw level: " << level << ", WPM: " << (level/6.071)+6 << ", rounded: " << round((level/6.071)+6);
                emit haveKeySpeed(round((level / 6.071) + 6));
                break;
            case '\x0D':
                // Notch filder setting - ignore for now
                state.set(NOTCHF, level, false);
                break;
            case '\x0E':
                // compressor level
                emit haveCompLevel(level);
                state.set(COMPLEVEL, level, false);
                break;
            case '\x12':
                emit haveNB((bool)level);
                state.set(NB, level, false);
                break;
            case '\x15':
                // monitor level
                emit haveMonitorGain(level);
                state.set(MONITORLEVEL, level, false);
                break;
            case '\x16':
                // VOX gain
                emit haveVoxGain(level);
                state.set(VOXGAIN, level, false);
                break;
            case '\x17':
                // anti-VOX gain
                emit haveAntiVoxGain(level);
                state.set(ANTIVOXGAIN, level, false);
                break;

            default:
                qInfo(logRig()) << "Unknown control level (0x14) received at register " << QString("0x%1").arg((int)payloadIn[1],2,16) << " with level " << QString("0x%1").arg((int)level,2,16) << ", int=" << (int)level;
                printHex(payloadIn);
                break;
        }
        return;
    }

    if(payloadIn[0] == '\x15')
    {
        switch(payloadIn[1])
        {
            case '\x01':
                // noise or s-meter sequelch status
                break;
            case '\x02':
                // S-Meter
                emit haveMeter(meterS, level);
                state.set(SMETER, level, false);
                break;
            case '\x04':
                // Center (IC-R8600)
                emit haveMeter(meterCenter, level);
                state.set(SMETER, level, false);
                break;
            case '\x05':
                // Various squelch (tone etc.)
                break;
            case '\x11':
                // RF-Power meter
                emit haveMeter(meterPower, level);
                state.set(POWERMETER, level, false);
                break;
            case '\x12':
                // SWR
                emit haveMeter(meterSWR, level);
                state.set(SWRMETER, level, false);
                break;
            case '\x13':
                // ALC
                emit haveMeter(meterALC, level);
                state.set(ALCMETER, level, false);
                break;
            case '\x14':
                // COMP dB reduction
                emit haveMeter(meterComp, level);
                state.set(COMPMETER, level, false);
                break;
            case '\x15':
                // VD (12V)
                emit haveMeter(meterVoltage, level);
                state.set(VOLTAGEMETER, level, false);
                break;
            case '\x16':
                // ID
                emit haveMeter(meterCurrent, level);
                state.set(CURRENTMETER, level, false);
                break;

            default:
                qInfo(logRig()) << "Unknown meter level (0x15) received at register " << (unsigned int) payloadIn[1] << " with level " << level;
                break;
        }


    return;
    }

}

void rigCommander::setIFShift(unsigned char level)
{
    QByteArray payload;
    if (getCommand(funcIFShift,payload,level))
    {
        payload.append(bcdEncodeInt(level));
        prepDataAndSend(payload);
    }
}

void rigCommander::setTPBFInner(unsigned char level)
{
    QByteArray payload;
    // Passband may be fixed?
    unsigned char mode = state.getChar(MODE);
    foreach (mode_info m, rigCaps.modes)
    {
        if (m.reg == mode)
        {
            if (m.bw)
            {
                if (getCommand(funcPBTInner,payload,level))
                {
                    payload.append(bcdEncodeInt(level));
                    prepDataAndSend(payload);
                }
            }
        }
    }
}

void rigCommander::setTPBFOuter(unsigned char level)
{
    QByteArray payload;
    // Passband may be fixed?
    unsigned char mode = state.getChar(MODE);
    foreach (mode_info m, rigCaps.modes)
    {
        if (m.reg == mode)
        {
            if (m.bw)
            {
                if (getCommand(funcPBTOuter,payload,level))
                {
                    payload.append(bcdEncodeInt(level));
                    prepDataAndSend(payload);
                }
            }
        }
    }
}

void rigCommander::setTxPower(unsigned char power)
{
    QByteArray payload;
    if (getCommand(funcRFPower,payload,power))
    {
        payload.append(bcdEncodeInt(power));
        prepDataAndSend(payload);
    }
}

void rigCommander::setMicGain(unsigned char gain)
{
    QByteArray payload;
    if (getCommand(funcMicGain,payload,gain))
    {
        payload.append(bcdEncodeInt(gain));
        prepDataAndSend(payload);
    }
}

void rigCommander::getModInput(bool dataOn)
{
    QByteArray payload;
    funcs f=(dataOn) ? funcDATA1Mod :funcDATAOffMod;

    if (getCommand(f,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setModInput(inputTypes input, bool dataOn)
{
//    The input enum is as follows:

//    inputMic=0,
//    inputACC=1,
//    inputUSB=3,
//    inputLAN=5,
//    inputACCA,
//    inputACCB};

    QByteArray payload;
    funcs f=(dataOn) ? funcDATA1Mod :funcDATAOffMod;

    if (getCommand(f,payload,input))
    {
        payload.append(input);
        prepDataAndSend(payload);
    }

    /*
    switch(rigCaps.model)
    {
        case model9700:
            payload.setRawData("\x1A\x05\x01\x15", 4);
            payload.append((unsigned char)input);
            break;
        case model7610:
            payload.setRawData("\x1A\x05\x00\x91", 4);
            payload.append((unsigned char)input);
            break;
        case model7300:
            payload.setRawData("\x1A\x05\x00\x66", 4);
            payload.append((unsigned char)input);
            break;
        case model7850:
            payload.setRawData("\x1A\x05\x00\x63", 4);
            switch(input)
            {
                case inputMic:
                    inAsByte.setRawData("\x00", 1);
                    break;
                case inputACCA:
                    inAsByte.setRawData("\x01", 1);
                    break;
                case inputACCB:
                    inAsByte.setRawData("\x02", 1);
                    break;
                case inputUSB:
                    inAsByte.setRawData("\x08", 1);
                    break;
                case inputLAN:
                    inAsByte.setRawData("\x09", 1);
                    break;
                default:
                    return;

            }
            payload.append(inAsByte);
            break;
        case model705:
            payload.setRawData("\x1A\x05\x01\x18", 4);
            switch(input)
            {
                case inputMic:
                    inAsByte.setRawData("\x00", 1);
                    break;
                case inputUSB:
                    inAsByte.setRawData("\x01", 1);
                    break;
                case inputLAN: // WLAN
                    inAsByte.setRawData("\x03", 1);
                    break;
                default:
                    return;
            }
            payload.append(inAsByte);
            break;
        case model7700:
            payload.setRawData("\x1A\x05\x00\x32", 4);
            if(input==inputLAN)
            {
                // NOTE: CIV manual says data may range from 0 to 3
                // But data 0x04 does correspond to LAN.
                payload.append("\x04");
            } else {
                payload.append((unsigned char)input);
            }
            break;
        case model7600:
            payload.setRawData("\x1A\x05\x00\x30", 4);
            payload.append((unsigned char)input);
            break;
        case model7100:
            payload.setRawData("\x1A\x05\x00\x90", 4);
            payload.append((unsigned char)input);
            break;
        case model7200:
            payload.setRawData("\x1A\x03\x23", 3);
            switch(input)
            {
                case inputMic:
                    payload.setRawData("\x00", 1);
                    break;
                case inputUSB:
                    payload.setRawData("\x03", 1);
                    break;
                case inputACC:
                    payload.setRawData("\x01", 1);
                    break;
                default:
                    return;
            }
        default:
            break;
    }
    if(dataOn)
    {
        if(rigCaps.model==model7200)
        {
            payload[2] = payload[2] + 1;
        } else {
            payload[3] = payload[3] + 1;
        }
    }

    if(isQuery)
    {
        payload.truncate(4);
    }

    prepDataAndSend(payload);
    */
}

void rigCommander::setModInputLevel(inputTypes input, unsigned char level)
{
    switch(input)
    {
        case inputMic:
            setMicGain(level);
            break;

        case inputACCA:
            setACCGain(level, 0);
            break;

        case inputACCB:
            setACCGain(level, 1);
            break;

        case inputACC:
            setACCGain(level);
            break;

        case inputUSB:
            setUSBGain(level);
            break;

        case inputLAN:
            setLANGain(level);
            break;

        default:
            break;
    }
}

void rigCommander::setAfMute(bool gainOn)
{
    QByteArray payload;
    if (getCommand(funcAFMute,payload,gainOn))
    {
            payload.append(static_cast<unsigned char>(gainOn));
            prepDataAndSend(payload);
    }
}

void rigCommander::setDialLock(bool lockOn)
{
    QByteArray payload;
    if (getCommand(funcDialLock,payload,lockOn))
    {
            payload.append(static_cast<unsigned char>(lockOn));
            prepDataAndSend(payload);
    }
}

void rigCommander::getModInputLevel(inputTypes input)
{
    switch(input)
    {
        case inputMic:
            getMicGain();
            break;

        case inputACCA:
            getACCGain(0);
            break;

        case inputACCB:
            getACCGain(1);
            break;

        case inputACC:
            getACCGain();
            break;

        case inputUSB:
            getUSBGain();
            break;

        case inputLAN:
            getLANGain();
            break;

        default:
            break;
    }
}

void rigCommander::getAfMute()
{
    QByteArray payload;
    if (getCommand(funcAFMute,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getDialLock()
{
    QByteArray payload;
    if (getCommand(funcDialLock,payload))
    {
        prepDataAndSend(payload);
    }
}

QByteArray rigCommander::getUSBAddr() // Not Used
{
    QByteArray payload;

    switch(rigCaps.model)
    {
        case model705:
            payload.setRawData("\x1A\x05\x01\x16", 4);
            break;
        case model9700:
            payload.setRawData("\x1A\x05\x01\x13", 4);
            break;
        case model7200:
            payload.setRawData("\x1A\x03\x25", 3);
            break;
        case model7100:
        case model7610:
            payload.setRawData("\x1A\x05\x00\x89", 4);
            break;
        case model7300:
            payload.setRawData("\x1A\x05\x00\x65", 4);
            break;
        case model7850:
            payload.setRawData("\x1A\x05\x00\x61", 4);
            break;
        case model7600:
            payload.setRawData("\x1A\x05\x00\x29", 4);
            break;
        default:
            break;
    }
    return payload;
}

void rigCommander::getUSBGain()
{
    QByteArray payload;
    if (getCommand(funcUSBModLevel,payload))
    {
        prepDataAndSend(payload);
    }
}


void rigCommander::setUSBGain(unsigned char gain)
{
    QByteArray payload;
    if (getCommand(funcUSBModLevel,payload,gain))
    {
        payload.append(bcdEncodeInt(gain));
        prepDataAndSend(payload);
    }
}

QByteArray rigCommander::getLANAddr() // Not Used
{
    QByteArray payload;
    switch(rigCaps.model)
    {
        case model705:
            payload.setRawData("\x1A\x05\x01\x17", 4);
            break;
        case model9700:
            payload.setRawData("\x1A\x05\x01\x14", 4);
            break;
        case model7610:
            payload.setRawData("\x1A\x05\x00\x90", 4);
            break;
        case model7850:
            payload.setRawData("\x1A\x05\x00\x62", 4);
            break;
        case model7700:
            payload.setRawData("\x1A\x05\x01\x92", 4);
            break;
        default:
            break;
    }

    return payload;
}

void rigCommander::getLANGain()
{
    QByteArray payload;
    if (getCommand(funcLANModLevel,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setLANGain(unsigned char gain)
{
    QByteArray payload;
    if (getCommand(funcLANModLevel,payload,gain))
    {
        payload.append(bcdEncodeInt(gain));
        prepDataAndSend(payload);
    }
}

QByteArray rigCommander::getACCAddr(unsigned char ab) // Not Used
{
    QByteArray payload;

    // Note: the manual for the IC-7600 does not call out a
    // register to adjust the ACC gain.

    // 7850: ACC-A = 0, ACC-B = 1

    switch(rigCaps.model)
    {
        case model9700:
            payload.setRawData("\x1A\x05\x01\x12", 4);
            break;
        case model7100:
            payload.setRawData("\x1A\x05\x00\x87", 4);
            break;
        case model7610:
            payload.setRawData("\x1A\x05\x00\x88", 4);
            break;
        case model7300:
            payload.setRawData("\x1A\x05\x00\x64", 4);
            break;
        case model7850:
            // Note: 0x58 = ACC-A, 0x59 = ACC-B
            if(ab==0)
            {
                // A
                payload.setRawData("\x1A\x05\x00\x58", 4);
            } else {
                // B
                payload.setRawData("\x1A\x05\x00\x59", 4);
            }
            break;
        case model7700:
            payload.setRawData("\x1A\x05\x00\x30", 4);
            break;
        default:
            break;
    }

    return payload;
}

void rigCommander::getACCGain()
{
    getACCGain((unsigned char)0x0);
}


void rigCommander::getACCGain(unsigned char ab)
{
    funcs f=(ab == 0) ? funcACC1ModLevel :funcACC2ModLevel;
    QByteArray payload;
    if (getCommand(f,payload))
    {
        prepDataAndSend(payload);
    }
}


void rigCommander::setACCGain(unsigned char gain)
{
    setACCGain(gain, (unsigned char)0x0);
}

void rigCommander::setACCGain(unsigned char gain, unsigned char ab)
{
    funcs f=(ab == 0) ? funcACC1ModLevel :funcACC2ModLevel;
    QByteArray payload;
    if (getCommand(f,payload,gain))
    {
        payload.append(bcdEncodeInt(gain));
        prepDataAndSend(payload);
    }
}

void rigCommander::setCompLevel(unsigned char compLevel)
{
    QByteArray payload;
    if (getCommand(funcCompressorLevel,payload,compLevel))
    {
        payload.append(bcdEncodeInt(compLevel));
        prepDataAndSend(payload);
    }
}

void rigCommander::setMonitorGain(unsigned char monitorLevel)
{
    QByteArray payload;
    if (getCommand(funcMonitorGain,payload,monitorLevel))
    {
        payload.append(bcdEncodeInt(monitorLevel));
        prepDataAndSend(payload);
    }
}

void rigCommander::setVoxGain(unsigned char gain)
{
    QByteArray payload;
    if (getCommand(funcVoxGain,payload,gain))
    {
        payload.append(bcdEncodeInt(gain));
        prepDataAndSend(payload);
    }
}

void rigCommander::setAntiVoxGain(unsigned char gain)
{
    QByteArray payload;
    if (getCommand(funcAntiVoxGain,payload,gain))
    {
        payload.append(bcdEncodeInt(gain));
        prepDataAndSend(payload);
    }
}

void rigCommander::setNBLevel(unsigned char level)
{
    QByteArray payload;
    if (getCommand(funcNBLevel,payload,level))
    {
        payload.append(bcdEncodeInt(level));
        prepDataAndSend(payload);
    }
}

void rigCommander::setNRLevel(unsigned char level)
{
    QByteArray payload;
    if (getCommand(funcNRLevel,payload,level))
    {
        payload.append(bcdEncodeInt(level));
        prepDataAndSend(payload);
    }
}


void rigCommander::getRfGain()
{
    QByteArray payload;
    if (getCommand(funcRfGain,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getAfGain()
{
    if (udp == Q_NULLPTR)
    {
        QByteArray payload;
        if (getCommand(funcAfGain,payload))
        {
            prepDataAndSend(payload);
        }
    }
    else {
        emit haveAfGain(localVolume);
    }
}

void rigCommander::getIFShift()
{
    QByteArray payload;
    if (getCommand(funcIFShift,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getTPBFInner()
{
    // Passband may be fixed?
    unsigned char mode = state.getChar(MODE);
    foreach (mode_info m, rigCaps.modes)
    {
        if (m.reg == mode) {
            if (m.bw) {
                QByteArray payload;
                if (getCommand(funcPBTInner,payload))
                {
                    prepDataAndSend(payload);
                }
                break;
            }
        }
    }
}

void rigCommander::getTPBFOuter()
{
    // Passband may be fixed?
    unsigned char mode = state.getChar(MODE);
    foreach (mode_info m, rigCaps.modes)
    {
        if (m.reg == mode) {
            if (m.bw) {
                QByteArray payload;
                if (getCommand(funcPBTOuter,payload))
                {
                    prepDataAndSend(payload);
                }
                break;
            }
        }
    }
}

void rigCommander::getSql()
{
    QByteArray payload;
    if (getCommand(funcSquelch,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getTxLevel()
{
    QByteArray payload;
    if (getCommand(funcRFPower,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getMicGain()
{
    QByteArray payload;
    if (getCommand(funcMicGain,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getCompLevel()
{
    QByteArray payload;
    if (getCommand(funcCompressorLevel,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getMonitorGain()
{
    QByteArray payload;
    if (getCommand(funcMonitorGain,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getVoxGain()
{
    QByteArray payload;
    if (getCommand(funcVoxGain,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getAntiVoxGain()
{
    QByteArray payload;
    if (getCommand(funcAntiVoxGain,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getNBLevel()
{
    QByteArray payload;
    if (getCommand(funcNBLevel,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getNRLevel()
{
    QByteArray payload;
    if (getCommand(funcNRLevel,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getLevels()
{
    // Function to grab all levels
    getRfGain(); //0x02
    getAfGain(); // 0x01
    getSql(); // 0x03
    getTxLevel(); // 0x0A
    getMicGain(); // 0x0B
    getCompLevel(); // 0x0E
//    getMonitorGain(); // 0x15
//    getVoxGain(); // 0x16
//    getAntiVoxGain(); // 0x17
}

void rigCommander::getMeters(meterKind meter)
{
    switch(meter)
    {
        case meterS:
            getSMeter();
            break;
        case meterCenter:
            getCenterMeter();
            break;
        case meterSWR:
            getSWRMeter();
            break;
        case meterPower:
            getRFPowerMeter();
            break;
        case meterALC:
            getALCMeter();
            break;
        case meterComp:
            getCompReductionMeter();
            break;
        case meterVoltage:
            getVdMeter();
            break;
        case meterCurrent:
            getIDMeter();
            break;
        default:
            break;
    }
}

void rigCommander::getSMeter()
{
    QByteArray payload;
    if (getCommand(funcSMeter,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getCenterMeter()
{
    QByteArray payload;
    if (getCommand(funcCenterMeter,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getRFPowerMeter()
{
    QByteArray payload;
    if (getCommand(funcCenterMeter,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getSWRMeter()
{
    QByteArray payload;
    if (getCommand(funcSWRMeter,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getALCMeter()
{
    QByteArray payload;
    if (getCommand(funcALCMeter,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getCompReductionMeter()
{
    QByteArray payload;
    if (getCommand(funcCompMeter,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getVdMeter()
{
    QByteArray payload;
    if (getCommand(funcVdMeter,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getIDMeter()
{
    QByteArray payload;
    if (getCommand(funcIdMeter,payload))
    {
        prepDataAndSend(payload);
    }
}


void rigCommander::setSquelch(unsigned char level)
{
    QByteArray payload;
    if (getCommand(funcSquelch,payload,level))
    {
        payload.append(bcdEncodeInt(level));
        prepDataAndSend(payload);
    }
}

void rigCommander::setRfGain(unsigned char level)
{
    QByteArray payload;
    if (getCommand(funcRfGain,payload,level))
    {
        payload.append(bcdEncodeInt(level));
        prepDataAndSend(payload);
    }
}

void rigCommander::setAfGain(unsigned char level)
{
    if (udp == Q_NULLPTR)
    {
        QByteArray payload;
        if (getCommand(funcAfGain,payload,level))
        {
            payload.append(bcdEncodeInt(level));
            prepDataAndSend(payload);
        }
    }
    else {
        emit haveSetVolume(level);
        localVolume = level;
    }
}

void rigCommander::setRefAdjustCourse(unsigned char level)
{
    // 1A 05 00 72 0000-0255
    QByteArray payload;
    if (getCommand(funcREFAdjust,payload,level))
    {
        payload.append(bcdEncodeInt(level));
        prepDataAndSend(payload);
    }
}

void rigCommander::setRefAdjustFine(unsigned char level)
{
    QByteArray payload;
    if (getCommand(funcREFAdjustFine,payload,level))
    {
        payload.append(bcdEncodeInt(level));
        prepDataAndSend(payload);
    }
}

void rigCommander::setTime(timekind t)
{
    QByteArray payload;
    if (getCommand(funcTime,payload))
    {
        payload.append(convertNumberToHex(t.hours));
        payload.append(convertNumberToHex(t.minutes));
        prepDataAndSend(payload);
        qInfo(logRig()) << QString("Setting Time: %0:%1").arg(t.hours).arg(t.minutes);
    }

/*
    QByteArray payload;

    switch(rigCaps.model)
    {
        case model705:
            payload.setRawData("\x1A\x05\x01\x66", 4);
            break;
        case model7300:
            payload.setRawData("\x1A\x05\x00\x95", 4);
            break;
        case model7610:
            payload.setRawData("\x1A\x05\x01\x59", 4);
            break;
        case model7700:
            payload.setRawData("\x1A\x05\x00\x59", 4);
            break;
        case model7850:
            payload.setRawData("\x1A\x05\x00\x96", 4);
            break;
        case model9700:
            payload.setRawData("\x1A\x05\x01\x80", 4);
            break;
        case modelR8600:
            payload.setRawData("\x1A\x05\x01\x32", 4);
            break;
        default:
            return;
            break;

    }
*/
}

void rigCommander::setDate(datekind d)
{

    QByteArray payload;
    if (getCommand(funcDate,payload))
    {
        // YYYYMMDD
        payload.append(convertNumberToHex(d.year/100)); // 20
        payload.append(convertNumberToHex(d.year - 100*(d.year/100))); // 21
        payload.append(convertNumberToHex(d.month));
        payload.append(convertNumberToHex(d.day));
        prepDataAndSend(payload);
        qInfo(logRig()) << QString("Setting Date: %0-%1-%2").arg(d.year).arg(d.month).arg(d.day);
    }

    /*
    switch(rigCaps.model)
    {
        case model705:
            payload.setRawData("\x1A\x05\x01\x65", 4);
            break;
        case model7300:
            payload.setRawData("\x1A\x05\x00\x94", 4);
            break;
        case model7610:
            payload.setRawData("\x1A\x05\x01\x58", 4);
            break;
        case model7700:
            payload.setRawData("\x1A\x05\x00\x58", 4);
            break;
        case model7850:
            payload.setRawData("\x1A\x05\x00\x95", 4);
            break;
        case model9700:
            payload.setRawData("\x1A\x05\x01\x79", 4);
            break;
        case modelR8600:
            payload.setRawData("\x1A\x05\x01\x31", 4);
            break;
        default:
            return;
            break;

    }
    */
}

void rigCommander::setUTCOffset(timekind t)
{
    QByteArray payload;
    if (getCommand(funcUTCOffset,payload))
    {
        // YYYYMMDD
        payload.append(convertNumberToHex(t.minutes));
        payload.append(static_cast<unsigned char>(t.isMinus));
        prepDataAndSend(payload);
        qInfo(logRig()) << QString("Setting UTC Offset: %0%1:%2").arg((t.isMinus)?"-":"+").arg(t.hours).arg(t.minutes);
    }

/*
    switch(rigCaps.model)
    {
        case model705:
            payload.setRawData("\x1A\x05\x01\x70", 4);
            break;
        case model7300:
            payload.setRawData("\x1A\x05\x00\x96", 4);
            break;
        case model7610:
            payload.setRawData("\x1A\x05\x01\x62", 4);
            break;
        case model7700:
            payload.setRawData("\x1A\x05\x00\x61", 4);
            break;
        case model7850:
            // Clock 1:
            payload.setRawData("\x1A\x05\x00\x99", 4);
            break;
        case model9700:
            payload.setRawData("\x1A\x05\x01\x84", 4);
            break;
        case modelR8600:
            payload.setRawData("\x1A\x05\x01\x35", 4);
            break;
        default:
            return;
            break;

    }
*/
}

unsigned char rigCommander::convertNumberToHex(unsigned char num)
{
    // Two digit only
    if(num > 99)
    {
        qInfo(logRig()) << "Invalid numeric conversion from num " << num << " to hex.";
        return 0xFA;
    }
    unsigned char result = 0;
    result =  (num/10) << 4;
    result |= (num - 10*(num/10));
    //qDebug(logRig()) << "Converting number: " << num << " to hex: " + QString("0x%1").arg(result, 2, 16, QChar('0');
    return result;
}

void rigCommander::getRefAdjustCourse()
{
    QByteArray payload;
    if (getCommand(funcREFAdjust,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getRefAdjustFine()
{
    QByteArray payload;
    if (getCommand(funcREFAdjustFine,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::parseRegisters1C()
{
    // PTT lives here
    // Not sure if 02 is the right place to switch.
    // TODO: test this function
    switch(payloadIn[01])
    {
        case '\x00':
            parsePTT();
            break;
        case '\x01':
            // ATU status (on/off/tuning)
            parseATU();
            break;
        default:
            break;
    }
}

void rigCommander::parseRegister21()
{
    // Register 21 is RIT and Delta TX
    int ritHz = 0;
    freqt f;
    QByteArray longfreq;

    // Example RIT value reply:
    // Index: 00 01 02 03 04 05
    // DATA:  21 00 32 03 00 fd

    switch(payloadIn[01])
    {
        case '\x00':
            // RIT frequency
            //
            longfreq = payloadIn.mid(2,2);
            longfreq.append(QByteArray(3,'\x00'));
            f = parseFrequency(longfreq, 3);
            if(payloadIn.length() < 5)
                break;
            ritHz = f.Hz*((payloadIn.at(4)=='\x01')?-1:1);
            emit haveRitFrequency(ritHz);
            state.set(RITVALUE, ritHz, false);
            break;
        case '\x01':
            // RIT on/off
            if(payloadIn.at(02) == '\x01')
            {
                emit haveRitEnabled(true);
            } else {
                emit haveRitEnabled(false);
            }
            state.set(RITFUNC, (bool)payloadIn.at(02), false);
            break;
        case '\x02':
            // Delta TX setting on/off
            break;
        default:
            break;
    }
}

void rigCommander::parseATU()
{
    // qInfo(logRig()) << "Have ATU status from radio. Emitting.";
    // Expect:
    // [0]: 0x1c
    // [1]: 0x01
    // [2]: 0 = off, 0x01 = on, 0x02 = tuning in-progress
    emit haveATUStatus((unsigned char) payloadIn[2]);
    // This is a bool so any non-zero will mean enabled.
    state.set(TUNERFUNC, (bool)payloadIn[2], false);
}

void rigCommander::parsePTT()
{
    // read after payloadIn[02]

    if(payloadIn[2] == (char)0)
    {
        // PTT off
        emit havePTTStatus(false);
    } else {
        // PTT on
        emit havePTTStatus(true);
    }
    state.set(PTT,(bool)payloadIn[2],false);
}

void rigCommander::parseRegisters1A()
{
    // The simpler of the 1A stuff:

    // 1A 06: data mode on/off
    //    07: IP+ enable/disable
    //    00:   memory contents
    //    01:   band stacking memory contents (last freq used is stored here per-band)
    //    03: filter width
    //    04: AGC rate
    // qInfo(logRig()) << "Looking at register 1A :";
    // printHex(payloadIn, false, true);

    // "INDEX: 00 01 02 03 04 "
    // "DATA:  1a 06 01 03 fd " (data mode enabled, filter width 3 selected)

    switch(payloadIn[01])
    {
        case '\x00':
        {
            // Memory contents
            break;
        }
        case '\x01':
        {
            // band stacking register
            parseBandStackReg();
            break;
        }
        case '\x03':
        {
            quint16 calc;
            quint8 pass = bcdHexToUChar((quint8)payloadIn[2]);
            if (state.getChar(MODE) == modeAM) {
                calc = 200 + (pass * 200);
            }
            else if (pass <= 10)
            {
                calc = 50 + (pass * 50);
            }
            else {
                calc = 600 + ((pass - 10) * 100);
            }
            emit havePassband(calc);
            state.set(PASSBAND, calc, false);
            break;
        }
        case '\x04':
        {
            state.set(AGC, (quint8)payloadIn[2], false);
            break;
        }
        case '\x06':
        {
            // data mode
            // emit havedataMode( (bool) payloadIn[somebit])
            // index
            // 03 04
            // XX YY
            // XX = 00 (off) or 01 (on)
            // YY: filter selected, 01 through 03.;
            // if YY is 00 then XX was also set to 00
            emit haveDataMode((bool)payloadIn[03]);
            state.set(DATAMODE, (quint8)payloadIn[3], false);
            break;
        }
        case '\x07':
        {
            // IP+ status
            break;
        }
        case '\x09':
        {
            state.set(MUTEFUNC, (quint8)payloadIn[2], false);
        }
        default:
        {
            break;
        }
    }
}

void rigCommander::parseRegister1B()
{
    quint16 tone=0;
    bool tinv = false;
    bool rinv = false;

    switch(payloadIn[01])
    {
        case '\x00':
            // "Repeater tone"
            tone = decodeTone(payloadIn);
            emit haveTone(tone);
            state.set(CTCSS, tone, false);
            break;
        case '\x01':
            // "TSQL tone"
            tone = decodeTone(payloadIn);
            emit haveTSQL(tone);
            state.set(TSQL, tone, false);
            break;
        case '\x02':
            // DTCS (DCS)
            tone = decodeTone(payloadIn, tinv, rinv);
            emit haveDTCS(tone, tinv, rinv);
            state.set(DTCS, tone, false);
            break;
        case '\x07':
            // "CSQL code (DV mode)"
            tone = decodeTone(payloadIn);
            state.set(CSQL, tone, false);
            break;
        default:
            break;
    }
}

void rigCommander::parseRegister16()
{
    //"INDEX: 00 01 02 03 "
    //"DATA:  16 5d 00 fd "
    //               ^-- mode info here
    rptAccessTxRx ra;

    switch(payloadIn.at(1))
    {
        case '\x5d':
            emit haveRptAccessMode((rptAccessTxRx)payloadIn.at(2));
            break;
        case '\x02':
            // Preamp
            emit havePreamp((unsigned char)payloadIn.at(2));
            state.set(PREAMP, (quint8)payloadIn.at(2), false);
            break;
        case '\x22':
            emit haveNB(payloadIn.at(2) != 0);
            state.set(NBFUNC, payloadIn.at(2) != 0, false);
            break;
        case '\x40':
            emit haveNR(payloadIn.at(2) != 0);
            state.set(NRFUNC, payloadIn.at(2) != 0, false);
            break;
        case '\x41': // Auto notch
            state.set(ANFFUNC, payloadIn.at(2) != 0, false);
            break;
        case '\x42':
            state.set(TONEFUNC, payloadIn.at(2) != 0, false);
            if(payloadIn.at(2)==1)
            {
                ra = ratrTONEon;
            } else {
                ra = ratrTONEoff;
            }
            emit haveRptAccessMode(ra);
            break;
        case '\x43':
            state.set(TSQLFUNC, payloadIn.at(2) != 0, false);
            if(payloadIn.at(2)==1)
            {
                ra = ratrTSQLon;
            } else {
                ra = ratrTSQLoff;
            }
            emit haveRptAccessMode(ra);
            break;
        case '\x44':
            emit haveComp(payloadIn.at(2) != 0);
            state.set(COMPFUNC, payloadIn.at(2) != 0, false);
            break;
        case '\x45':
            emit haveMonitor(payloadIn.at(2) != 0);
            state.set(MONFUNC, payloadIn.at(2) != 0, false);
            break;
        case '\x46':
            emit haveVox(payloadIn.at(2) != 0);
            state.set(VOXFUNC, payloadIn.at(2) != 0, false);
            break;
        case '\x47':
            if (payloadIn.at(2) == '\00') {
                state.set(FBKINFUNC, false, false);
                state.set(SBKINFUNC, false, false);
            }
            else if (payloadIn.at(2) == '\01') {
                state.set(FBKINFUNC, false, false);
                state.set(SBKINFUNC, true, false);

            }
            else if (payloadIn.at(2) == '\02') {
                state.set(FBKINFUNC, true, false);
                state.set(SBKINFUNC, false, false);
            }
            emit haveCWBreakMode(payloadIn.at(2));
            break;
        case '\x48': // Manual Notch
            state.set(MNFUNC, payloadIn.at(2) != 0, false);
            break;
        case '\x50': // Dial lock
            state.set(LOCKFUNC, payloadIn.at(2) != 0, false);
            break;
        default:
            break;
    }
}

void rigCommander::parseBandStackReg()
{
    //qInfo(logRig()) << "Band stacking register response received: ";
    //printHex(payloadIn, false, true);

    // Reference output, 20 meters, regCode 01 (latest):
    // "INDEX: 00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 "
    // "DATA:  1a 01 05 01 60 03 23 14 00 00 03 10 00 08 85 00 08 85 fd "

    char band = payloadIn[2];
    char regCode = payloadIn[3];
    freqt freqs = parseFrequency(payloadIn, 7);
    //float freq = (float)freqs.MHzDouble;

    // The Band Stacking command returns the regCode in the position that VFO is expected.
    // As BSR is always on the active VFO, just set that.
    freqs.VFO = selVFO_t::activeVFO;

    bool dataOn = (payloadIn[11] & 0x10) >> 4; // not sure...
    char mode = payloadIn[9];
    char filter = payloadIn[10];
    // 09, 10 mode
    // 11 digit RH: data mode on (1) or off (0)
    // 11 digit LH: CTCSS 0 = off, 1 = TONE, 2 = TSQL

    // 12, 13 : tone freq setting
    // 14, 15 tone squelch freq setting
    // if more, memory name (label) ascii

    qInfo(logRig()) << "BSR in rigCommander: band: " << QString("%1").arg(band) << " regCode: " << (QString)regCode << " freq Hz: " << freqs.Hz << ", mode: " << (unsigned int)mode << ", filter: " << (unsigned int)filter << " data: " << dataOn;
    //qInfo(logRig()) << "mode: " << (QString)mode << " dataOn: " << dataOn;
    //qInfo(logRig()) << "Freq Hz: " << freqs.Hz;

    emit haveBandStackReg(freqs, mode, filter, dataOn);
}

void rigCommander::parseDetailedRegisters1A05()
{
    // It seems a lot of misc stuff is under this command and subcommand.
    // 1A 05 ...
    // 00 01 02 03 04 ...

    // 02 and 03 make up a BCD'd number:
    // 0001, 0002, 0003, ... 0101, 0102, 0103...

    // 04 is a typical single byte response
    // 04 05 is a typical 0-255 response

    // This file processes the registers which are radically different in each model.
    // It is a work in progress.
    // TODO: inputMod source and gain for models: 7700, and 7600

    int level = (100*bcdHexToUChar(payloadIn[4])) + bcdHexToUChar(payloadIn[5]);

    int subcmd = bcdHexToUChar(payloadIn[3]) + (100*bcdHexToUChar(payloadIn[2]));

    inputTypes input = (inputTypes)bcdHexToUChar(payloadIn[4]);
    int inputRaw = bcdHexToUChar(payloadIn[4]);

    switch(rigCaps.model)
    {
        case model9700:
            switch(subcmd)
            {

                case 72:
                    // course reference
                    emit haveRefAdjustCourse(  bcdHexToUChar(payloadIn[5]) + (100*bcdHexToUChar(payloadIn[4])) );
                    break;
                case 73:
                    // fine reference
                    emit haveRefAdjustFine( bcdHexToUChar(payloadIn[5]) + (100*bcdHexToUChar(payloadIn[4])) );
                    break;
                case 112:
                    emit haveACCGain(level, 5);
                    break;
                case 113:
                    emit haveUSBGain(level);
                    break;
                case 114:
                    emit haveLANGain(level);
                    break;
                case 115:
                    emit haveModInput(input, false);
                    break;
                case 116:
                    emit haveModInput(input, true);
                    break;
                default:
                    break;
            }
            break;
        case model7850:
            switch(subcmd)
            {
                case 63:
                    switch(inputRaw)
                    {
                        case 0:
                            input = inputMic;
                            break;
                        case 1:
                            input = inputACCA;
                            break;
                        case 2:
                            input = inputACCB;
                            break;
                        case 8:
                            input = inputUSB;
                            break;
                        case 9:
                            input = inputLAN;
                            break;
                        default:
                            input = inputUnknown;
                            break;
                    }
                    emit haveModInput(input, false);
                    break;
                case 64:
                    switch(inputRaw)
                    {
                        case 0:
                            input = inputMic;
                            break;
                        case 1:
                            input = inputACCA;
                            break;
                        case 2:
                            input = inputACCB;
                            break;
                        case 8:
                            input = inputUSB;
                            break;
                        case 9:
                            input = inputLAN;
                            break;
                        default:
                            input = inputUnknown;
                            break;
                    }
                    emit haveModInput(input, true);
                    break;
                case 58:
                    emit haveACCGain(level, 0);
                    break;
                case 59:
                    emit haveACCGain(level, 1);
                    break;
                case 61:
                    emit haveUSBGain(level);
                    break;
                case 62:
                    emit haveLANGain(level);
                    break;
                default:
                    break;
            }
            break;
        case model7610:
            switch(subcmd)
            {
                case 91:
                    emit haveModInput(input, false);
                    break;
                case 92:
                    emit haveModInput(input, true);
                    break;
                case 88:
                    emit haveACCGain(level, 5);
                    break;
                case 89:
                    emit haveUSBGain(level);
                    break;
                case 90:
                    emit haveLANGain(level);
                    break;
                case 228:
                    emit haveDashRatio(inputRaw);
                default:
                    break;
            }
            return;
        case model7600:
            switch(subcmd)
            {
                case 30:
                    emit haveModInput(input, false);
                    break;
                case 31:
                    emit haveModInput(input, true);
                    break;
                case 29:
                    emit haveUSBGain(level);
                    break;
                default:
                    break;
            }
            return;
        case model7300:
            switch(subcmd)
            {
                case 64:
                    emit haveACCGain(level, 5);
                    break;
                case 65:
                    emit haveUSBGain(level);
                    break;
                case 66:
                    emit haveModInput(input, false);
                    break;
                case 67:
                    emit haveModInput(input, true);
                    break;
                default:
                    break;
            }
            return;
        case model7100:
            switch(subcmd)
            {
                case 87:
                    emit haveACCGain(level, 5);
                    break;
                case 89:
                    emit haveUSBGain(level);
                    break;
                case 90:
                    emit haveModInput(input, false);
                    break;
                case 91:
                    emit haveModInput(input, true);
                    break;
                default:
                    break;
            }
            break;
        case model705:
            switch(subcmd)
            {
                case 116:
                    emit haveUSBGain(level);
                    break;
                case 117:
                    emit haveLANGain(level);
                    break;
                case 118:
                    switch(inputRaw)
                    {
                        case 0:
                            input = inputMic;
                            break;
                        case 1:
                            input = inputUSB;
                            break;
                        case 3:
                            input = inputLAN;
                            break;
                        default:
                            input = inputUnknown;
                            break;
                    }
                    emit haveModInput(input, false);
                    break;
                case 119:
                    switch(inputRaw)
                    {
                        case 0:
                            input = inputMic;
                            break;
                        case 1:
                            input = inputUSB;
                            break;
                        case 3:
                            input = inputLAN;
                            break;
                        default:
                            input = inputUnknown;
                            break;
                    }
                    emit haveModInput(input, true);
                    break;
                default:
                    break;
            }
            break;

        default:
            break;
    }
}

void rigCommander::parseWFData()
{
    freqt freqSpan;
    bool isSub;
    switch(payloadIn[1])
    {
        case 0:
            // Chunk of spectrum
            parseSpectrum();
            break;
        case 0x10:
            // confirming scope is on
            state.set(SCOPEFUNC, (bool)payloadIn[2], true);
            break;
        case 0x11:
            // confirming output enabled/disabled of wf data.
            break;
        case 0x14:
            // fixed or center
            emit haveSpectrumMode(static_cast<spectrumMode>((unsigned char)payloadIn[3]));
            // [1] 0x14
            // [2] 0x00
            // [3] 0x00 (center), 0x01 (fixed), 0x02, 0x03
            break;
        case 0x15:
            // read span in center mode
            // [1] 0x15
            // [2] to [8] is span encoded as a frequency
            isSub = payloadIn.at(2)==0x01;
            freqSpan = parseFrequency(payloadIn, 6);
            emit haveScopeSpan(freqSpan, isSub);
            //qInfo(logRig()) << "Received 0x15 center span data: for frequency " << freqSpan.Hz;
            //printHex(payloadIn, false, true);
            break;
        case 0x16:
            // read edge mode center in edge mode
            emit haveScopeEdge((char)payloadIn[2]);
            //qInfo(logRig()) << "Received 0x16 edge in center mode:";
            //printHex(payloadIn, false, true);
            // [1] 0x16
            // [2] 0x01, 0x02, 0x03: Edge 1,2,3
            break;
        case 0x17:
            // Hold status (only 9700?)
            qDebug(logRig()) << "Received 0x17 hold status - need to deal with this!";
            printHex(payloadIn, false, true);
            break;
        case 0x19:
            // scope reference level
            // [1] 0x19
            // [2] 0x00
            // [3] 10dB digit, 1dB digit
            // [4] 0.1dB digit, 0
            // [5] 0x00 = +, 0x01 = -
            parseSpectrumRefLevel();
            break;
        default:
            qInfo(logRig()) << "Unknown waveform data received: ";
            printHex(payloadIn, false, true);
            break;
    }
}


mode_info rigCommander::createMode(mode_kind m, unsigned char reg, QString name, bool bw)
{
    mode_info mode;
    mode.mk = m;
    mode.reg = reg;
    mode.name = name;
    mode.bw = bw;
    return mode;
}

centerSpanData rigCommander::createScopeCenter(centerSpansType s, QString name)
{
    centerSpanData csd;
    csd.cstype = s;
    csd.name = name;
    return csd;
}

void rigCommander::determineRigCaps()
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
    rigCaps.filename = rigList.find(rigCaps.modelID).value();
    QSettings* settings = new QSettings(rigCaps.filename, QSettings::Format::IniFormat);
    if (!settings->childGroups().contains("Rig"))
    {
        qWarning(logRig()) << rigCaps.filename << "Cannot be loaded!";
        return;
    }
    settings->beginGroup("Rig");
    // Populate rigcaps

    rigCaps.modelName = settings->value("Model", "").toString();
    qInfo(logRig()) << QString("Loading Rig: %0 from %1").arg(rigCaps.modelName,rigCaps.filename);

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
    rigCaps.useRTSforPTT = settings->value("UseRTSforPTT",false).toBool();

    // Temporary QList to hold the function string lookup // I would still like to find a better way of doing this!
    QHash<QString, funcs> funcsLookup;
    for (int i=0;i<NUMFUNCS;i++)
    {
        funcsLookup.insert(funcString[i].toUpper(), funcs(i));
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
                                            QByteArray::fromHex(settings->value("String", "").toString().toUtf8()),
                                            settings->value("Min", 0).toInt(), settings->value("Max", 0).toInt()));

                rigCaps.commandsReverse.insert(QByteArray::fromHex(settings->value("String", "").toString().toUtf8()),func);
            } else {
                qInfo(logRig()) << "Function" << settings->value("Type", "").toString() << "Not Found!";
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
            rigCaps.modes.push_back(createMode(mode_kind(settings->value("Num", 0).toInt()),  settings->value("Num", 0).toInt(), settings->value("Name", "").toString(), settings->value("BW", 0).toInt()!=0));
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
            rigCaps.scopeCenterSpans.push_back(
                centerSpanData(centerSpansType(settings->value("Num", 0).toInt()),  settings->value("Name", "").toString(), settings->value("Freq", 0).toUInt()));
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
            rigCaps.inputs.append(rigInput((inputTypes)settings->value("Num", 0).toInt(),settings->value("Name", "").toString()));
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
            rigCaps.steps.push_back(stepType(settings->value("Num", 0).toInt(),settings->value("Name", "").toString(),settings->value("Hz", 0ULL).toULongLong()));
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
            rigCaps.preamps.push_back((unsigned char)settings->value("Num", 0).toInt());
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
            rigCaps.antennas.push_back((unsigned char)settings->value("Num", 0).toInt());
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
            rigCaps.attenuators.push_back((unsigned char)settings->value("dB", 0).toString().toUInt(nullptr,16)); // Don't really care if it fails!
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
            rigCaps.filters.push_back(filterType((unsigned char)settings->value("Num", 0).toInt(), settings->value("Name", "").toString(), settings->value("Modes", 0).toUInt()));
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
            availableBands band =  availableBands(settings->value("Num", 0).toInt());
            quint64 start = settings->value("Start", 0ULL).toULongLong();
            quint64 end = settings->value("End", 0ULL).toULongLong();
            int bsr = settings->value("BSR", 0).toInt();
            double range = settings->value("Range", 0.0).toDouble();

            rigCaps.bands.push_back(bandType(band,start,end,range));
            rigCaps.bsr[band] = bsr;
            qInfo(logRig()) << "Adding Band " << band << "Start" << start << "End" << end << "BSR" << bsr;
        }
        settings->endArray();
    }

    settings->endGroup();
    delete settings;

    /*
    switch(model){
        case model7300:
            rigCaps.modelName = QString("IC-7300");
            rigCaps.rigctlModel = 3073;
            rigCaps.hasSpectrum = true;
            rigCaps.spectSeqMax = 11;
            rigCaps.spectAmpMax = 160;
            rigCaps.spectLenMax = 475;
            rigCaps.inputs.append(inputUSB);
            rigCaps.inputs.append(inputACC);
            rigCaps.hasLan = false;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = false;
            rigCaps.hasATU = true;
            rigCaps.hasCTCSS = true;
            rigCaps.hasTBPF = true;
            rigCaps.attenuators.push_back('\x20');
            rigCaps.preamps.push_back('\x01');
            rigCaps.preamps.push_back('\x02');
           // rigCaps.bands = standardHF;
           // rigCaps.bands.insert(rigCaps.bands.end(), { bandDef4m, bandDef630m, bandDef2200m, bandDefGen });
            rigCaps.transceiveCommand = QByteArrayLiteral("\x1a\x05\x00\x71");
            rigCaps.hasVFOMS = false;
            rigCaps.hasVFOAB = true;
            rigCaps.hasQuickSplitCommand = true;
            rigCaps.quickSplitCommand = QByteArrayLiteral("\x1a\x05\x00\x30");
            // new functions
            //rigCaps.commands.insert(funcTransceive,QByteArrayLiteral("\x1a\x05\x00\x71"));
            //rigCaps.commands.insert(funcQuickSplit,QByteArrayLiteral("\x1a\x05\x00\x30"));
            //rigCaps.commands.insert(funcDashRatio,QByteArrayLiteral("\x1a\x05\x01\x61"));
            break;
        case modelR8600:
            rigCaps.modelName = QString("IC-R8600");
            rigCaps.rigctlModel = 3079;
            rigCaps.hasSpectrum = true;
            rigCaps.spectSeqMax = 11;
            rigCaps.spectAmpMax = 160;
            rigCaps.spectLenMax = 475;
            rigCaps.inputs.clear();
            rigCaps.hasLan = true;
            rigCaps.hasEthernet = true;
            rigCaps.hasWiFi = false;
            rigCaps.hasTransmit = false;
            rigCaps.hasPTTCommand = false;
            rigCaps.hasCTCSS = true;
            rigCaps.hasDTCS = true;
            rigCaps.hasDV = true;
            rigCaps.hasTBPF = true;
            rigCaps.attenuators.push_back('\x10');
            rigCaps.attenuators.push_back('\x20');
            rigCaps.attenuators.push_back('\x30');
            rigCaps.preamps.push_back('\x01');
            rigCaps.preamps.push_back('\x02');
            rigCaps.hasAntennaSel = true;
            rigCaps.antennas = {0x00, 0x01, 0x02};
           // rigCaps.bands = standardHF;
           // rigCaps.bands.insert(rigCaps.bands.end(), standardVU.begin(), standardVU.end());
           // rigCaps.bands.insert(rigCaps.bands.end(), { bandDef23cm, bandDef4m, bandDef630m, bandDef2200m, bandDefGen });
           // rigCaps.modes = commonModes;
            rigCaps.modes.insert(rigCaps.modes.end(), {
                                     createMode(modeWFM, 0x06, "WFM"), createMode(modeS_AMD, 0x11, "S-AM (D)"),
                                     createMode(modeS_AML, 0x14, "S-AM(L)"), createMode(modeS_AMU, 0x15, "S-AM(U)"),
                                     createMode(modeP25, 0x16, "P25"), createMode(modedPMR, 0x18, "dPMR"),
                                     createMode(modeNXDN_VN, 0x19, "NXDN-VN"), createMode(modeNXDN_N, 0x20, "NXDN-N"),
                                     createMode(modeDCR, 0x21, "DCR")});
            rigCaps.scopeCenterSpans.insert(rigCaps.scopeCenterSpans.end(), {createScopeCenter(cs1M, "±1M"), createScopeCenter(cs2p5M, "±2.5M")});
            rigCaps.transceiveCommand = QByteArrayLiteral("\x1a\x05\x00\x92");
            rigCaps.hasVFOMS = true; // not documented very well
            rigCaps.hasVFOAB = true; // so we just do both...
            // new functions
            //rigCaps.commands.insert(funcTransceive,QByteArrayLiteral("\x1a\x05\x00\x92"));
            break;
        case model9700:
            rigCaps.modelName = QString("IC-9700");
            rigCaps.rigctlModel = 3081;
            rigCaps.hasSpectrum = true;
            rigCaps.spectSeqMax = 11;
            rigCaps.spectAmpMax = 160;
            rigCaps.spectLenMax = 475;
            rigCaps.inputs.append(inputLAN);
            rigCaps.inputs.append(inputUSB);
            rigCaps.inputs.append(inputACC);
            rigCaps.hasLan = true;
            rigCaps.hasEthernet = true;
            rigCaps.hasWiFi = false;
            rigCaps.hasDD = true;
            rigCaps.hasDV = true;
            rigCaps.hasCTCSS = true;
            rigCaps.hasDTCS = true;
            rigCaps.hasRepeaterModes = true;
            rigCaps.hasTBPF = true;
            rigCaps.attenuators.push_back('\x10');
            rigCaps.preamps.push_back('\x01');
            //rigCaps.bands = standardVU;
            //rigCaps.bands.push_back(bandDef23cm);
            rigCaps.bsr[band23cm] = 0x03;
            rigCaps.bsr[band70cm] = 0x02;
            rigCaps.bsr[band2m] = 0x01;
            //rigCaps.modes = commonModes;
            rigCaps.modes.insert(rigCaps.modes.end(), {createMode(modeDV, 0x17, "DV"),
                                                       createMode(modeDD, 0x22, "DD")});
            rigCaps.transceiveCommand = QByteArrayLiteral("\x1a\x05\x01\x27");
            rigCaps.hasVFOMS = true;
            rigCaps.hasVFOAB = true;
            rigCaps.hasAdvancedRptrToneCmds = true;
            rigCaps.hasQuickSplitCommand = true;
            rigCaps.quickSplitCommand = QByteArrayLiteral("\x1a\x05\x00\x43");
            // new functions
            //rigCaps.commands.insert(funcTransceive,QByteArrayLiteral("\x1a\x05\x01\x27"));
            //rigCaps.commands.insert(funcQuickSplit,QByteArrayLiteral("\x1a\x05\x00\x43"));
            //rigCaps.commands.insert(funcDashRatio,QByteArrayLiteral("\x1a\x05\x02\x24"));
            break;
        case model905:
            rigCaps.modelName = QString("IC-905");
            rigCaps.rigctlModel = 0;
            rigCaps.hasSpectrum = true;
            rigCaps.spectSeqMax = 11;
            rigCaps.spectAmpMax = 160;
            rigCaps.spectLenMax = 475;
            rigCaps.inputs.append(inputLAN);
            rigCaps.inputs.append(inputUSB);
            rigCaps.hasLan = true;
            rigCaps.hasEthernet = true;
            rigCaps.hasWiFi = false;
            rigCaps.hasDD = true;
            rigCaps.hasDV = true;
            rigCaps.hasCTCSS = true;
            rigCaps.hasDTCS = true;
            rigCaps.hasRepeaterModes = true;
            rigCaps.hasTBPF = true;
            rigCaps.attenuators.push_back('\x10');
            rigCaps.preamps.push_back('\x01');
            //rigCaps.bands = standardVU;
            //rigCaps.bands.push_back(bandDef23cm);
            //rigCaps.bands.push_back(bandDef13cm);
            //rigCaps.bands.push_back(bandDef6cm);
           // rigCaps.bands.push_back(bandDef3cm);
            rigCaps.bsr[band2m] = 0x01;
            rigCaps.bsr[band70cm] = 0x02;
            rigCaps.bsr[band23cm] = 0x03;
            rigCaps.bsr[band13cm] = 0x04;
            rigCaps.bsr[band6cm] = 0x05;
            rigCaps.bsr[band3cm] = 0x06;
            //rigCaps.modes = commonModes;
            rigCaps.modes.insert(rigCaps.modes.end(), {createMode(modeDV, 0x17, "DV"),
                                                       createMode(modeDD, 0x22, "DD"),
                                                       createMode(modeATV, 0x23, "ATV")
                                                       });

            rigCaps.scopeCenterSpans.insert(rigCaps.scopeCenterSpans.end(), {createScopeCenter(cs1M, "±1M"),
                                                                             createScopeCenter(cs2p5M, "±2.5M"),
                                                                             createScopeCenter(cs5M, "±5M"),
                                                                             createScopeCenter(cs10M, "±10M"),
                                                                             createScopeCenter(cs25M, "±25M")});
            rigCaps.transceiveCommand = QByteArrayLiteral("\x1a\x05\x01\x42");
            rigCaps.hasVFOMS = false;
            rigCaps.hasVFOAB = true;
            rigCaps.hasAdvancedRptrToneCmds = true;
            rigCaps.hasQuickSplitCommand = true;
            rigCaps.quickSplitCommand = QByteArrayLiteral("\x1a\x05\x00\x46");
            // new functions
            //rigCaps.commands.insert(funcTransceive,QByteArrayLiteral("\x1a\x05\x01\x42"));
            //rigCaps.commands.insert(funcQuickSplit,QByteArrayLiteral("\x1a\x05\x00\x46"));
            //rigCaps.commands.insert(funcDashRatio,QByteArrayLiteral("\x1a\x05\x02\x36"));
            break;
        case model910h:
            rigCaps.modelName = QString("IC-910H");
            rigCaps.rigctlModel = 3044;
            rigCaps.hasSpectrum = false;
            rigCaps.hasLan = false;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = false;
            rigCaps.hasFDcomms = false;
            rigCaps.hasDD = false;
            rigCaps.hasDV = false;
            rigCaps.hasCTCSS = true;
            rigCaps.hasDTCS = true;
            rigCaps.hasRepeaterModes = true;
            rigCaps.hasATU = false;
            rigCaps.attenuators.insert(rigCaps.attenuators.end(),{ '\x10' , '\x20', '\x30'});
            rigCaps.preamps.push_back('\x01');
            //rigCaps.bands = standardVU;
            //rigCaps.bands.push_back(bandDef23cm);
            rigCaps.bsr[band23cm] = 0x03;
            rigCaps.bsr[band70cm] = 0x02;
            rigCaps.bsr[band2m] = 0x01;
            //rigCaps.modes = commonModes;
            rigCaps.transceiveCommand = QByteArrayLiteral("\x1a\x05\x00\x58");
            rigCaps.hasVFOMS = false;
            rigCaps.hasVFOAB = true;
            // new functions
            //rigCaps.commands.insert(funcTransceive,QByteArrayLiteral("\x1a\x05\x00\x58"));
            break;
        case model7600:
            rigCaps.modelName = QString("IC-7600");
            rigCaps.rigctlModel = 3063;
            rigCaps.hasSpectrum = false;
            rigCaps.inputs.append(inputACC);
            rigCaps.inputs.append(inputUSB);
            rigCaps.hasLan = false;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = false;
            rigCaps.hasFDcomms = false;
            rigCaps.hasATU = true;
            rigCaps.hasCTCSS = false;
            rigCaps.hasDTCS = false;
            rigCaps.hasTBPF = true;
            rigCaps.attenuators.insert(rigCaps.attenuators.end(), {0x00, 0x06, 0x12, 0x18});
            rigCaps.preamps.push_back('\x01');
            rigCaps.preamps.push_back('\x02');
            rigCaps.antennas = {0x00, 0x01};
            //rigCaps.bands = standardHF;
            //rigCaps.bands.push_back(bandDefGen);
            rigCaps.bsr[bandGen] = 0x11;
            //rigCaps.modes = commonModes;
            rigCaps.modes.insert(rigCaps.modes.end(), { createMode(modePSK, 0x12, "PSK"),
                                                       createMode(modePSK_R, 0x13, "PSK-R") });
            rigCaps.transceiveCommand = QByteArrayLiteral("\x1a\x05\x00\x97");
            rigCaps.hasVFOMS = true;
            rigCaps.hasVFOAB = false;
            rigCaps.hasQuickSplitCommand = true;
            rigCaps.quickSplitCommand = QByteArrayLiteral("\x1a\x05\x00\x64");
            // new functions
            //rigCaps.commands.insert(funcTransceive,QByteArrayLiteral("\x1a\x05\x00\x97"));
            //rigCaps.commands.insert(funcQuickSplit,QByteArrayLiteral("\x1a\x05\x00\x64"));
            break;
        case model7610:
            rigCaps.modelName = QString("IC-7610");
            rigCaps.rigctlModel = 3078;
            rigCaps.hasSpectrum = true;
            rigCaps.spectSeqMax = 15;
            rigCaps.spectAmpMax = 200;
            rigCaps.spectLenMax = 689;
            rigCaps.inputs.append(inputLAN);
            rigCaps.inputs.append(inputUSB);
            rigCaps.inputs.append(inputACC);
            rigCaps.hasLan = true;
            rigCaps.hasEthernet = true;
            rigCaps.hasWiFi = false;
            rigCaps.hasCTCSS = true;
            rigCaps.hasTBPF = true;
            rigCaps.attenuators.insert(rigCaps.attenuators.end(),
                                      {'\x03', '\x06', '\x09', '\x12',\
                                       '\x15', '\x18', '\x21', '\x24',\
                                       '\x27', '\x30', '\x33', '\x36',
                                       '\x39', '\x42', '\x45'});
            rigCaps.preamps.push_back('\x01');
            rigCaps.preamps.push_back('\x02');
            rigCaps.hasAntennaSel = true;
            rigCaps.antennas = {0x00, 0x01};
            rigCaps.hasATU = true;
            rigCaps.bands = standardHF;
            rigCaps.bands.insert(rigCaps.bands.end(), { bandDef630m, bandDef2200m, bandDefGen });
            //rigCaps.modes = commonModes;
            rigCaps.modes.insert(rigCaps.modes.end(), { createMode(modePSK, 0x12, "PSK"),
                                                       createMode(modePSK_R, 0x13, "PSK-R") });
            rigCaps.hasRXAntenna = true;
            rigCaps.transceiveCommand = QByteArrayLiteral("\x1a\x05\x01\x12");
            rigCaps.hasSpecifyMainSubCmd = true;
            rigCaps.hasQuickSplitCommand = true;
            rigCaps.quickSplitCommand = QByteArrayLiteral("\x1a\x05\x00\x33");
            rigCaps.hasVFOMS = true;
            rigCaps.hasVFOAB = false;
            break;            
        case model7850:
            rigCaps.modelName = QString("IC-785x");
            rigCaps.rigctlModel = 3075;
            rigCaps.hasSpectrum = true;
            rigCaps.spectSeqMax = 15;
            rigCaps.spectAmpMax = 136;
            rigCaps.spectLenMax = 689;
            rigCaps.inputs.append(inputLAN);
            rigCaps.inputs.append(inputUSB);
            rigCaps.inputs.append(inputACCA);
            rigCaps.inputs.append(inputACCB);
            rigCaps.hasLan = true;
            rigCaps.hasEthernet = true;
            rigCaps.hasWiFi = false;
            rigCaps.hasATU = true;
            rigCaps.hasCTCSS = true;
            rigCaps.hasTBPF = true;
            rigCaps.attenuators.insert(rigCaps.attenuators.end(),
                                      {'\x03', '\x06', '\x09',
                                       '\x12', '\x15', '\x18', '\x21'});
            rigCaps.preamps.push_back('\x01');
            rigCaps.preamps.push_back('\x02');
            rigCaps.hasAntennaSel = true;
            rigCaps.antennas = {0x00, 0x01, 0x02, 0x03};
            //rigCaps.bands = standardHF;
            //rigCaps.bands.insert(rigCaps.bands.end(), { bandDef630m, bandDef2200m, bandDefGen });
            //rigCaps.modes = commonModes;
            rigCaps.modes.insert(rigCaps.modes.end(), {createMode(modePSK, 0x12, "PSK"),
                                                       createMode(modePSK_R, 0x13, "PSK-R")});
            rigCaps.hasRXAntenna = true;
            rigCaps.transceiveCommand = QByteArrayLiteral("\x1a\x05\x01\x55");
            rigCaps.hasSpecifyMainSubCmd = true;
            rigCaps.hasQuickSplitCommand = true;
            rigCaps.quickSplitCommand = QByteArrayLiteral("\x1a\x05\x01\x13");
            rigCaps.hasVFOMS = true;
            rigCaps.hasVFOAB = false;
            break;
        case model705:
            rigCaps.modelName = QString("IC-705");
            rigCaps.rigctlModel = 3085;
            rigCaps.hasSpectrum = true;
            rigCaps.spectSeqMax = 11;
            rigCaps.spectAmpMax = 160;
            rigCaps.spectLenMax = 475;
            rigCaps.inputs.append(inputLAN);
            rigCaps.inputs.append(inputUSB);
            rigCaps.hasLan = true;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = true;
            rigCaps.hasDD = true;
            rigCaps.hasDV = true;
            rigCaps.hasATU = true;
            rigCaps.hasCTCSS = true;
            rigCaps.hasDTCS = true;
            rigCaps.hasRepeaterModes = true;
            rigCaps.hasTBPF = true;
            rigCaps.attenuators.insert(rigCaps.attenuators.end(),{ '\x10' , '\x20'});
            rigCaps.preamps.push_back('\x01');
            rigCaps.preamps.push_back('\x02');
            //rigCaps.bands = standardHF;
            //rigCaps.bands.insert(rigCaps.bands.end(), standardVU.begin(), standardVU.end());
            //rigCaps.bands.insert(rigCaps.bands.end(), { bandDefAir, bandDefGen, bandDefWFM, bandDef630m, bandDef2200m });
            rigCaps.bsr[band70cm] = 0x14;
            rigCaps.bsr[band2m] = 0x13;
            rigCaps.bsr[bandAir] = 0x12;
            rigCaps.bsr[bandWFM] = 0x11;
            rigCaps.bsr[bandGen] = 0x15;
            //rigCaps.modes = commonModes;
            rigCaps.modes.insert(rigCaps.modes.end(), {createMode(modeWFM, 0x06, "WFM"),
                                                       createMode(modeDV, 0x17, "DV")});
            rigCaps.transceiveCommand = QByteArrayLiteral("\x1a\x05\x01\x31");
            rigCaps.hasVFOMS = false;
            rigCaps.hasVFOAB = true;
            rigCaps.hasQuickSplitCommand = true;
            rigCaps.quickSplitCommand = QByteArrayLiteral("\x1a\x05\x00\x45");
            // new functions
            //rigCaps.commands.insert(funcTransceive,QByteArrayLiteral("\x1a\x05\x01\x31"));
            //rigCaps.commands.insert(funcQuickSplit,QByteArrayLiteral("\x1a\x05\x00\x45"));
            //rigCaps.commands.insert(funcDashRatio,QByteArrayLiteral("\x1A\x05\x02\x52"));
            break;
        case model7000:
            rigCaps.modelName = QString("IC-7000");
            rigCaps.rigctlModel = 3060;
            rigCaps.hasSpectrum = false;
            rigCaps.inputs.append(inputACC);
            rigCaps.hasLan = false;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = false;
            rigCaps.hasFDcomms = false;
            rigCaps.hasATU = true;
            rigCaps.hasCTCSS = true;
            rigCaps.hasDTCS = true;
            rigCaps.hasTBPF = true;
            rigCaps.attenuators.push_back('\x12');
            rigCaps.preamps.push_back('\x01');
            //rigCaps.bands = standardHF;
            //rigCaps.bands.insert(rigCaps.bands.end(), standardVU.begin(), standardVU.end());
            //rigCaps.bands.push_back(bandDefGen);
            rigCaps.bsr[band2m] = 0x11;
            rigCaps.bsr[band70cm] = 0x12;
            rigCaps.bsr[bandGen] = 0x13;
            //rigCaps.modes = commonModes;
            rigCaps.transceiveCommand = QByteArrayLiteral("\x1a\x05\x00\x92");
            rigCaps.hasVFOMS = false;
            rigCaps.hasVFOAB = true;
            rigCaps.hasQuickSplitCommand = true;
            rigCaps.quickSplitCommand = QByteArrayLiteral("\x1a\x05\x00\x52");
            // new functions
            // rigCaps.commands.insert(funcTransceive,QByteArrayLiteral("\x1a\x05\x00\x92"));
            //rigCaps.commands.insert(funcQuickSplit,QByteArrayLiteral("\x1a\x05\x00\x52"));
            break;
        case model7410:
            rigCaps.modelName = QString("IC-7410");
            rigCaps.rigctlModel = 3067;
            rigCaps.hasSpectrum = false;
            rigCaps.inputs.append(inputACC);
            rigCaps.hasLan = false;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = false;
            rigCaps.hasFDcomms = true;
            rigCaps.hasATU = true;
            rigCaps.hasCTCSS = true;
            rigCaps.hasDTCS = true;
            rigCaps.hasTBPF = true;
            rigCaps.attenuators.push_back('\x20');
            rigCaps.preamps.push_back('\x01');
            rigCaps.preamps.push_back('\x02');
            rigCaps.antennas = {0x00, 0x01};
            //rigCaps.bands = standardHF;
            //rigCaps.bands.push_back(bandDefGen);
            rigCaps.bsr[bandGen] = 0x11;
            //rigCaps.modes = commonModes;
            rigCaps.transceiveCommand = QByteArrayLiteral("\x1a\x05\x00\x40");
            rigCaps.hasVFOMS = false;
            rigCaps.hasVFOAB = true;
            rigCaps.hasQuickSplitCommand = true;
            rigCaps.quickSplitCommand = QByteArrayLiteral("\x1a\x05\x00\x11");
            // new functions
            //rigCaps.commands.insert(funcTransceive,QByteArrayLiteral("\x1a\x05\x00\x40"));
            //rigCaps.commands.insert(funcQuickSplit,QByteArrayLiteral("\x1a\x05\x00\x11"));
            break;
        case model7100:
            rigCaps.modelName = QString("IC-7100");
            rigCaps.rigctlModel = 3070;
            rigCaps.hasSpectrum = false;
            rigCaps.inputs.append(inputUSB);
            rigCaps.inputs.append(inputACC);
            rigCaps.hasLan = false;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = false;
            rigCaps.hasFDcomms = false;
            rigCaps.hasATU = true;
            rigCaps.hasCTCSS = true;
            rigCaps.hasDTCS = true;
            rigCaps.hasRepeaterModes = true;
            rigCaps.hasTBPF = true;
            rigCaps.attenuators.push_back('\x12');
            rigCaps.preamps.push_back('\x01');
            rigCaps.preamps.push_back('\x02');
            //rigCaps.bands = standardHF;
            //rigCaps.bands.insert(rigCaps.bands.end(), standardVU.begin(), standardVU.end());
            //rigCaps.bands.insert(rigCaps.bands.end(), { bandDef4m, bandDefGen});
            rigCaps.bsr[band2m] = 0x11;
            rigCaps.bsr[band70cm] = 0x12;
            rigCaps.bsr[bandGen] = 0x13;
            //rigCaps.modes = commonModes;
            rigCaps.modes.insert(rigCaps.modes.end(), {createMode(modeWFM, 0x06, "WFM"),
                                                       createMode(modeDV, 0x17, "DV")});
            rigCaps.transceiveCommand = QByteArrayLiteral("\x1a\x05\x00\x95");
            rigCaps.hasVFOMS = false;
            rigCaps.hasVFOAB = true;
            rigCaps.hasQuickSplitCommand = true;
            rigCaps.quickSplitCommand = QByteArrayLiteral("\x1a\x05\x00\x15");
            // new functions
           // rigCaps.commands.insert(funcTransceive,QByteArrayLiteral("\x1a\x05\x00\x95"));
            //rigCaps.commands.insert(funcQuickSplit,QByteArrayLiteral("\x1a\x05\x00\x15"));
            //rigCaps.commands.insert(funcDashRatio,QByteArrayLiteral("\x1A\x05\x01\x35"));

            break;
        case model7200:
            rigCaps.modelName = QString("IC-7200");
            rigCaps.rigctlModel = 3061;
            rigCaps.hasSpectrum = false;
            rigCaps.inputs.append(inputUSB);
            rigCaps.inputs.append(inputACC);
            rigCaps.hasLan = false;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = false;
            rigCaps.hasFDcomms = false;
            rigCaps.hasATU = true;
            rigCaps.hasCTCSS = true;
            rigCaps.hasDTCS = true;
            rigCaps.hasTBPF = true;
            rigCaps.attenuators.push_back('\x20');
            rigCaps.preamps.push_back('\x01');
            //rigCaps.bands = standardHF;
            //rigCaps.bands.push_back(bandDefGen);
            rigCaps.bsr[bandGen] = 0x11;
            //rigCaps.modes = commonModes;
            rigCaps.transceiveCommand = QByteArrayLiteral("\x1a\x03\x48");
            rigCaps.hasVFOMS = false;
            rigCaps.hasVFOAB = true;
            rigCaps.hasQuickSplitCommand = true;
            rigCaps.quickSplitCommand = QByteArrayLiteral("\x1a\x03\x18");
            // new functions
            //rigCaps.commands.insert(funcTransceive,QByteArrayLiteral("\x1a\x03\x48"));
            //rigCaps.commands.insert(funcQuickSplit,QByteArrayLiteral("\x1a\x03\x18"));
            break;
        case model7700:
            rigCaps.modelName = QString("IC-7700");
            rigCaps.rigctlModel = 3062;
            rigCaps.hasSpectrum = false;
            rigCaps.inputs.append(inputLAN);
            //rigCaps.inputs.append(inputSPDIF);
            rigCaps.inputs.append(inputACC);
            rigCaps.hasLan = true;
            rigCaps.hasEthernet = true;
            rigCaps.hasWiFi = false;
            rigCaps.hasCTCSS = true;
            rigCaps.hasTBPF = true;
            rigCaps.attenuators.insert(rigCaps.attenuators.end(),
                                      {'\x06', '\x12', '\x18'});
            rigCaps.preamps.push_back('\x01');
            rigCaps.preamps.push_back('\x02');
            rigCaps.hasAntennaSel = true;
            rigCaps.antennas = {0x00, 0x01, 0x02, 0x03}; // not sure if 0x03 works
            rigCaps.hasATU = true;
            //rigCaps.bands = standardHF;
            //rigCaps.bands.insert(rigCaps.bands.end(), { bandDefGen, bandDef630m, bandDef2200m });
            //rigCaps.modes = commonModes;
            rigCaps.modes.insert(rigCaps.modes.end(), {createMode(modePSK, 0x12, "PSK"),
                                                       createMode(modePSK_R, 0x13, "PSK-R")});
            rigCaps.transceiveCommand = QByteArrayLiteral("\x1a\x05\x00\x95");
            rigCaps.hasVFOMS = false;
            rigCaps.hasVFOAB = true;
            rigCaps.hasQuickSplitCommand = true;
            rigCaps.quickSplitCommand = QByteArrayLiteral("\x1a\x05\x00\x67");
            // new functions
            //rigCaps.commands.insert(funcTransceive,QByteArrayLiteral("\x1a\x05\x00\x95"));
            //rigCaps.commands.insert(funcQuickSplit,QByteArrayLiteral("\x1a\x05\x00\x67"));
            //rigCaps.commands.insert(funcDashRatio,QByteArrayLiteral("\x1A\x05\x01\x34"));
            break;
        case model703:
            rigCaps.modelName = QString("IC-703");
            rigCaps.rigctlModel = 3055;
            rigCaps.hasSpectrum = false;
            rigCaps.inputs.clear();
            rigCaps.hasLan = false;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = false;
            rigCaps.hasFDcomms = false;
            rigCaps.hasATU = true;
            rigCaps.hasPTTCommand = false;
            rigCaps.useRTSforPTT = true;
            rigCaps.hasDataModes = false;
            rigCaps.attenuators.push_back('\x20');
            //rigCaps.bands = standardHF;
            //rigCaps.bands.insert(rigCaps.bands.end(), standardVU.begin(), standardVU.end());
            //rigCaps.bands.push_back(bandDefGen);
            //rigCaps.modes = commonModes;
            rigCaps.modes.insert(rigCaps.modes.end(), createMode(modeWFM, 0x06, "WFM"));
            rigCaps.transceiveCommand = QByteArrayLiteral("\x1a\x05\x00\x00");
            // new functions
            //rigCaps.commands.insert(funcTransceive,QByteArrayLiteral("\x1a\x05\x00\x00"));
            //rigCaps.commands.insert(funcFreqGet,QByteArrayLiteral("\x00"));
            //rigCaps.commands.insert(funcFreqSet,QByteArrayLiteral("\x03"));
            //rigCaps.commands.insert(funcModeGet,QByteArrayLiteral("\x01"));
            //rigCaps.commands.insert(funcModeSet,QByteArrayLiteral("\x06"));

            break;
        case model706:
            rigCaps.modelName = QString("IC-706");
            rigCaps.rigctlModel = 3009;
            rigCaps.hasSpectrum = false;
            rigCaps.inputs.clear();
            rigCaps.hasLan = false;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = false;
            rigCaps.hasFDcomms = false;
            rigCaps.hasATU = true;
            rigCaps.hasPTTCommand = false;
            rigCaps.useRTSforPTT = true;
            rigCaps.hasDataModes = false;
            rigCaps.attenuators.push_back('\x20');
            //rigCaps.bands = standardHF;
            //rigCaps.bands.insert(rigCaps.bands.end(), standardVU.begin(), standardVU.end());
            //rigCaps.bands.push_back(bandDefGen);
            //rigCaps.modes = commonModes;
            rigCaps.modes.insert(rigCaps.modes.end(), createMode(modeWFM, 0x06, "WFM"));
            rigCaps.transceiveCommand = QByteArrayLiteral("\x1a\x05\x00\x00");
            rigCaps.hasVFOMS = false;
            rigCaps.hasVFOAB = true;
            // new functions
            //rigCaps.commands.insert(funcTransceive,QByteArrayLiteral("\x1a\x05\x00\x00"));
            //rigCaps.commands.insert(funcFreqGet,QByteArrayLiteral("\x00"));
            //rigCaps.commands.insert(funcFreqSet,QByteArrayLiteral("\x03"));
            //rigCaps.commands.insert(funcModeGet,QByteArrayLiteral("\x01"));
            //rigCaps.commands.insert(funcModeSet,QByteArrayLiteral("\x06"));
            break;
        case model718:
            rigCaps.modelName = QString("IC-718");
            rigCaps.rigctlModel = 3013;
            rigCaps.hasSpectrum = false;
            rigCaps.inputs.clear();
            rigCaps.hasLan = false;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = false;
            rigCaps.hasFDcomms = false;
            rigCaps.hasATU = false;
            rigCaps.hasPTTCommand = false;
            rigCaps.useRTSforPTT = true;
            rigCaps.hasIFShift = true;
            rigCaps.hasDataModes = false;
            rigCaps.attenuators.push_back('\x20');
            rigCaps.preamps.push_back('\x01');
            rigCaps.bands =   {bandDef10m, bandDef10m, bandDef12m,
                               bandDef15m, bandDef17m, bandDef20m, bandDef30m,
                               bandDef40m, bandDef60m, bandDef80m, bandDef160m, bandDefGen};
            rigCaps.modes = { createMode(modeLSB, 0x00, "LSB"), createMode(modeUSB, 0x01, "USB"),
                              createMode(modeAM, 0x02, "AM"),
                              createMode(modeCW, 0x03, "CW"), createMode(modeCW_R, 0x07, "CW-R"),
                              createMode(modeRTTY, 0x04, "RTTY"), createMode(modeRTTY_R, 0x08, "RTTY-R")
                            };
            rigCaps.transceiveCommand = QByteArrayLiteral("\x1a\x05\x00\x00");
            rigCaps.hasVFOMS = false;
            rigCaps.hasVFOAB = true;
            // new functions
            //rigCaps.commands.insert(funcTransceive,QByteArrayLiteral("\x1a\x05\x00\x00"));
            //rigCaps.commands.insert(funcFreqGet,QByteArrayLiteral("\x00"));
            //rigCaps.commands.insert(funcFreqSet,QByteArrayLiteral("\x03"));
            //rigCaps.commands.insert(funcModeGet,QByteArrayLiteral("\x01"));
            //rigCaps.commands.insert(funcModeSet,QByteArrayLiteral("\x06"));
            break;
        case model736:
            rigCaps.modelName = QString("IC-736");
            rigCaps.rigctlModel = 3020;
            rigCaps.hasSpectrum = false;
            rigCaps.inputs.clear();
            rigCaps.hasLan = false;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = false;
            rigCaps.hasFDcomms = false;
            rigCaps.hasATU = false;
            rigCaps.hasPTTCommand = false;
            rigCaps.useRTSforPTT = true;
            rigCaps.hasDataModes = false;
            rigCaps.attenuators.push_back('\x20');
            rigCaps.preamps.push_back('\x01');
            rigCaps.bands = standardHF;
            rigCaps.modes = { createMode(modeLSB, 0x00, "LSB"), createMode(modeUSB, 0x01, "USB"),
                              createMode(modeAM, 0x02, "AM"), createMode(modeFM, 0x05, "FM"),
                              createMode(modeCW, 0x03, "CW"), createMode(modeCW_R, 0x07, "CW-R"),
                            };
            rigCaps.hasVFOMS = false;
            rigCaps.hasVFOAB = true;
            //rigCaps.commands.insert(funcFreqGet,QByteArrayLiteral("\x00"));
            //rigCaps.commands.insert(funcFreqSet,QByteArrayLiteral("\x03"));
           // rigCaps.commands.insert(funcModeGet,QByteArrayLiteral("\x01"));
            //rigCaps.commands.insert(funcModeSet,QByteArrayLiteral("\x06"));
            break;
        case model737:
            rigCaps.modelName = QString("IC-737");
            rigCaps.rigctlModel = 3021;
            rigCaps.hasSpectrum = false;
            rigCaps.inputs.clear();
            rigCaps.hasLan = false;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = false;
            rigCaps.hasFDcomms = false;
            rigCaps.hasATU = false;
            rigCaps.hasPTTCommand = false;
            rigCaps.useRTSforPTT = true;
            rigCaps.hasDataModes = false;
            rigCaps.attenuators.push_back('\x20');
            rigCaps.preamps.push_back('\x01');
            rigCaps.bands = standardHF;
            rigCaps.modes = { createMode(modeLSB, 0x00, "LSB"), createMode(modeUSB, 0x01, "USB"),
                              createMode(modeAM, 0x02, "AM"), createMode(modeFM, 0x05, "FM"),
                              createMode(modeCW, 0x03, "CW"), createMode(modeCW_R, 0x07, "CW-R"),
                            };
            rigCaps.hasVFOMS = false;
            rigCaps.hasVFOAB = true;
            //rigCaps.commands.insert(funcFreqGet,QByteArrayLiteral("\x00"));
           // rigCaps.commands.insert(funcFreqSet,QByteArrayLiteral("\x03"));
           // rigCaps.commands.insert(funcModeGet,QByteArrayLiteral("\x01"));
           // rigCaps.commands.insert(funcModeSet,QByteArrayLiteral("\x06"));
            break;
        case model738:
            rigCaps.modelName = QString("IC-738");
            rigCaps.rigctlModel = 3022;
            rigCaps.hasSpectrum = false;
            rigCaps.inputs.clear();
            rigCaps.hasLan = false;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = false;
            rigCaps.hasFDcomms = false;
            rigCaps.hasATU = false;
            rigCaps.hasPTTCommand = false;
            rigCaps.useRTSforPTT = true;
            rigCaps.hasDataModes = false;
            rigCaps.attenuators.push_back('\x20');
            rigCaps.preamps.push_back('\x01');
            rigCaps.bands = standardHF;
            rigCaps.modes = { createMode(modeLSB, 0x00, "LSB"), createMode(modeUSB, 0x01, "USB"),
                              createMode(modeAM, 0x02, "AM"), createMode(modeFM, 0x05, "FM"),
                              createMode(modeCW, 0x03, "CW"), createMode(modeCW_R, 0x07, "CW-R"),
                            };
            rigCaps.hasVFOMS = false;
            rigCaps.hasVFOAB = true;
            //rigCaps.commands.insert(funcFreqGet,QByteArrayLiteral("\x00"));
            //rigCaps.commands.insert(funcFreqSet,QByteArrayLiteral("\x03"));
           // rigCaps.commands.insert(funcModeGet,QByteArrayLiteral("\x01"));
           // rigCaps.commands.insert(funcModeSet,QByteArrayLiteral("\x06"));
            break;
        case model746:
            rigCaps.modelName = QString("IC-746");
            rigCaps.rigctlModel = 3023;
            rigCaps.hasSpectrum = false;
            rigCaps.inputs.clear();
            rigCaps.hasLan = false;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = false;
            rigCaps.hasFDcomms = false;
            rigCaps.hasATU = true;
            rigCaps.hasTBPF = true;
            rigCaps.hasIFShift = true;
            rigCaps.hasCTCSS = true;
            rigCaps.hasDTCS = true;
            rigCaps.hasRepeaterModes = true;
            rigCaps.hasAntennaSel = true;
            rigCaps.preamps.push_back('\x01');
            rigCaps.preamps.push_back('\x02');
            rigCaps.attenuators.insert(rigCaps.attenuators.end(),{ '\x20'});
            // There are two HF and VHF ant, 12-01 adn 12-02 select the HF, the VHF is auto selected
            // this incorrectly shows up as 2 and 3 in the drop down.
            rigCaps.antennas = {0x01, 0x02};
            rigCaps.bands = standardHF;
            rigCaps.bands.push_back(bandDef2m);
            rigCaps.bands.push_back(bandDefGen);
            //rigCaps.modes = commonModes;
            rigCaps.transceiveCommand = QByteArrayLiteral("\x1a\x05\x00\x00");
            rigCaps.hasVFOMS = true;
            rigCaps.hasVFOAB = true;
            // new functions
            //rigCaps.commands.insert(funcTransceive,QByteArrayLiteral("\x1a\x05\x00\x00"));
            break;
        case model756:
            rigCaps.modelName = QString("IC-756");
            rigCaps.rigctlModel = 3026;
            rigCaps.hasSpectrum = false;
            rigCaps.inputs.clear();
            rigCaps.hasLan = false;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = false;
            rigCaps.hasFDcomms = false;
            rigCaps.hasATU = true;
            rigCaps.hasTBPF = true;
            rigCaps.preamps.push_back('\x01');
            rigCaps.preamps.push_back('\x02');
            rigCaps.attenuators.insert(rigCaps.attenuators.end(),{ '\x06' , '\x12', '\x18'});
            rigCaps.antennas = {0x00, 0x01};
            rigCaps.bands = standardHF;
            rigCaps.bands.push_back(bandDefGen);
            rigCaps.bsr[bandGen] = 0x11;
            //rigCaps.modes = commonModes;
            rigCaps.transceiveCommand = QByteArrayLiteral("\x1a\x05\x00\x00");
            rigCaps.hasVFOMS = true;
            rigCaps.hasVFOAB = false;
            // new functions
           // rigCaps.commands.insert(funcTransceive,QByteArrayLiteral("\x1a\x05\x00\x00"));
            break;
        case model756pro:
            rigCaps.modelName = QString("IC-756 Pro");
            rigCaps.rigctlModel = 3027;
            rigCaps.hasSpectrum = false;
            rigCaps.inputs.clear();
            rigCaps.hasLan = false;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = false;
            rigCaps.hasFDcomms = false;
            rigCaps.hasATU = true;
            rigCaps.hasTBPF = true;
            rigCaps.preamps.push_back('\x01');
            rigCaps.preamps.push_back('\x02');
            rigCaps.attenuators.insert(rigCaps.attenuators.end(),{ '\x06' , '\x12', '\x18'});
            rigCaps.antennas = {0x00, 0x01};
            rigCaps.bands = standardHF;
            rigCaps.bands.push_back(bandDefGen);
            rigCaps.bsr[bandGen] = 0x11;
            //rigCaps.modes = commonModes;
            rigCaps.transceiveCommand = QByteArrayLiteral("\x1a\x05\x00\x00");
            rigCaps.hasVFOMS = true;
            rigCaps.hasVFOAB = false;
            // new functions
            //rigCaps.commands.insert(funcTransceive,QByteArrayLiteral("\x1a\x05\x00\x00"));
            break;
        case model756proii:
            rigCaps.modelName = QString("IC-756 Pro II");
            rigCaps.rigctlModel = 3027;
            rigCaps.hasSpectrum = false;
            rigCaps.inputs.clear();
            rigCaps.hasLan = false;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = false;
            rigCaps.hasFDcomms = false;
            rigCaps.hasATU = true;
            rigCaps.hasTBPF = true;
            rigCaps.preamps.push_back('\x01');
            rigCaps.preamps.push_back('\x02');
            rigCaps.attenuators.insert(rigCaps.attenuators.end(),{ '\x06' , '\x12', '\x18'});
            rigCaps.antennas = {0x00, 0x01};
            rigCaps.bands = standardHF;
            rigCaps.bands.push_back(bandDefGen);
            rigCaps.bsr[bandGen] = 0x11;
            //rigCaps.modes = commonModes;
            rigCaps.transceiveCommand = QByteArrayLiteral("\x1a\x05\x00\x00");
            rigCaps.hasVFOMS = true;
            rigCaps.hasVFOAB = false;
            rigCaps.hasQuickSplitCommand = true;
            rigCaps.quickSplitCommand = QByteArrayLiteral("\x1a\x05\x24");
            // new functions
            //rigCaps.commands.insert(funcTransceive,QByteArrayLiteral("\x1a\x05\x00\x00"));
           // rigCaps.commands.insert(funcQuickSplit,QByteArrayLiteral("\x1a\x05\x24"));
            break;
        case model756proiii:
            rigCaps.modelName = QString("IC-756 Pro III");
            rigCaps.rigctlModel = 3027;
            rigCaps.hasSpectrum = false;
            rigCaps.inputs.clear();
            rigCaps.hasLan = false;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = false;
            rigCaps.hasFDcomms = false;
            rigCaps.hasATU = true;
            rigCaps.hasTBPF = true;
            rigCaps.preamps.push_back('\x01');
            rigCaps.preamps.push_back('\x02');
            rigCaps.attenuators.insert(rigCaps.attenuators.end(),{ '\x06' , '\x12', '\x18'});
            rigCaps.antennas = {0x00, 0x01};
            rigCaps.bands = standardHF;
            rigCaps.bands.push_back(bandDefGen);
            rigCaps.bsr[bandGen] = 0x11;
            //rigCaps.modes = commonModes;
            rigCaps.transceiveCommand = QByteArrayLiteral("\x1a\x05\x00\x00");
            rigCaps.hasVFOMS = true;
            rigCaps.hasVFOAB = false;
            rigCaps.hasQuickSplitCommand = true;
            rigCaps.quickSplitCommand = QByteArrayLiteral("\x1a\x05\x24");
            // new functions
            //rigCaps.commands.insert(funcTransceive,QByteArrayLiteral("\x1a\x05\x00\x00"));
            //rigCaps.commands.insert(funcQuickSplit,QByteArrayLiteral("\x1a\x05\x24"));
            break;
        case model9100:
            rigCaps.modelName = QString("IC-9100");
            rigCaps.rigctlModel = 3068;
            rigCaps.hasSpectrum = false;
            rigCaps.inputs.append(inputUSB); // TODO, add commands for this radio's inputs
            rigCaps.inputs.append(inputACC);
            rigCaps.hasLan = false;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = false;
            rigCaps.hasFDcomms = false;
            rigCaps.hasATU = true;
            rigCaps.hasDV = true;
            rigCaps.hasTBPF = true;
            rigCaps.hasRepeaterModes = true;
            rigCaps.preamps.push_back('\x01');
            rigCaps.preamps.push_back('\x02');
            rigCaps.attenuators.insert(rigCaps.attenuators.end(),{ '\x20' });
            rigCaps.antennas = {0x00, 0x01};
            rigCaps.bands = standardHF;
            rigCaps.bands.insert(rigCaps.bands.end(), standardVU.begin(), standardVU.end());
            rigCaps.bands.push_back(bandDef23cm);
            rigCaps.bands.push_back(bandDefGen);
            rigCaps.bsr[band2m] = 0x11;
            rigCaps.bsr[band70cm] = 0x12;
            rigCaps.bsr[band23cm] = 0x13;
            rigCaps.bsr[bandGen] = 0x14;
            //rigCaps.modes = commonModes;
            rigCaps.modes.insert(rigCaps.modes.end(), {createMode(modeDV, 0x17, "DV")});
            rigCaps.hasVFOMS = true;
            rigCaps.hasVFOAB = true;
            rigCaps.hasQuickSplitCommand = true;
            rigCaps.quickSplitCommand = QByteArrayLiteral("\x1a\x05\x00\x14");
            // new functions
            //rigCaps.commands.insert(funcQuickSplit,QByteArrayLiteral("\x1a\x05\x00\x14"));
            break;
        default:
            rigCaps.modelName = QString("IC-0x%1").arg(rigCaps.modelID, 2, 16);
            rigCaps.hasSpectrum = false;
            rigCaps.spectSeqMax = 0;
            rigCaps.spectAmpMax = 0;
            rigCaps.spectLenMax = 0;
            rigCaps.inputs.clear();
            rigCaps.hasLan = false;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = false;
            rigCaps.hasFDcomms = false;
            rigCaps.hasPreamp = false;
            rigCaps.hasAntennaSel = false;
            rigCaps.attenuators.push_back('\x10');
            rigCaps.attenuators.push_back('\x12');
            rigCaps.attenuators.push_back('\x20');
            rigCaps.bands = standardHF;
            rigCaps.bands.insert(rigCaps.bands.end(), standardVU.begin(), standardVU.end());
            rigCaps.bands.insert(rigCaps.bands.end(), {bandDef23cm, bandDef4m, bandDef630m, bandDef2200m, bandDefGen});
            //rigCaps.modes = commonModes;
            rigCaps.transceiveCommand = QByteArrayLiteral("\x1a\x05\x00\x00");
            rigCaps.hasVFOMS = true;
            rigCaps.hasVFOAB = true;
            qInfo(logRig()) << "Found unknown rig: 0x" << QString("%1").arg(rigCaps.modelID, 2, 16);
            break;
    }
    */
    haveRigCaps = true;


    // Copy received guid so we can recognise this radio.
    memcpy(rigCaps.guid, this->guid, GUIDLEN);

    if(!usingNativeLAN)
    {
        if(useRTSforPTT_isSet)
        {
            rigCaps.useRTSforPTT = useRTSforPTT_manual;
        }
        comm->setUseRTSforPTT(rigCaps.useRTSforPTT);
    }

    if(lookingForRig)
    {
        lookingForRig = false;
        foundRig = true;

        qDebug(logRig()) << "---Rig FOUND from broadcast query:";
        this->civAddr = incomingCIVAddr; // Override and use immediately.
        payloadPrefix = QByteArray("\xFE\xFE");
        payloadPrefix.append(civAddr);
        payloadPrefix.append((char)compCivAddr);
        // if there is a compile-time error, remove the following line, the "hex" part is the issue:
        qInfo(logRig()) << "Using incomingCIVAddr: (int): " << this->civAddr << " hex: " << QString("0x%1").arg(this->civAddr,0,16);
        emit discoveredRigID(rigCaps);
    } else {
        if(!foundRig)
        {
            emit discoveredRigID(rigCaps);
            foundRig = true;
        }
        emit haveRigID(rigCaps);
    }
}

void rigCommander::parseSpectrum()
{
    if(!haveRigCaps)
    {
        qDebug(logRig()) << "Spectrum received in rigCommander, but rigID is incomplete.";
        return;
    }
    if(rigCaps.spectSeqMax == 0)
    {
        // there is a chance this will happen with rigs that support spectrum. Once our RigID query returns, we will parse correctly.
        qInfo(logRig()) << "Warning: Spectrum sequence max was zero, yet spectrum was received.";
        return;
    }
    // Here is what to expect:
    // payloadIn[00] = '\x27';
    // payloadIn[01] = '\x00';
    // payloadIn[02] = '\x00';
    //
    // Example long: (sequences 2-10, 50 pixels)
    // "INDEX: 00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 "
    // "DATA:  27 00 00 07 11 27 13 15 01 00 22 21 09 08 06 19 0e 20 23 25 2c 2d 17 27 29 16 14 1b 1b 21 27 1a 18 17 1e 21 1b 24 21 22 23 13 19 23 2f 2d 25 25 0a 0e 1e 20 1f 1a 0c fd "
    //                  ^--^--(seq 7/11)
    //                        ^-- start waveform data 0x00 to 0xA0, index 05 to 54
    //

    // Example medium: (sequence #11)
    // "INDEX: 00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 "
    // "DATA:  27 00 00 11 11 0b 13 21 23 1a 1b 22 1e 1a 1d 13 21 1d 26 28 1f 19 1a 18 09 2c 2c 2c 1a 1b fd "

    // Example short: (sequence #1) includes center/fixed mode at [05]. No pixels.
    // "INDEX: 00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 "
    // "DATA:  27 00 00 01 11 01 00 00 00 14 00 00 00 35 14 00 00 fd "
    //                        ^-- mode 00 (center) or 01 (fixed)
    //                                     ^--14.00 MHz lower edge
    //                                                    ^-- 14.350 MHz upper edge
    //                                                          ^-- possibly 00=in range 01 = out of range

    // Note, the index used here, -1, matches the ICD in the owner's manual.
    // Owner's manual + 1 = our index.

    // divs: Mode: Waveinfo: Len:   Comment:
    // 2-10  var   var       56     Minimum wave information w/waveform data
    // 11    10    26        31     Minimum wave information w/waveform data
    // 1     1     0         18     Only Wave Information without waveform data

    freqt fStart;
    freqt fEnd;

    unsigned char vfo = bcdHexToUChar(payloadIn[02]);
    unsigned char sequence = bcdHexToUChar(payloadIn[03]);

    //unsigned char sequenceMax = bcdHexToDecimal(payloadIn[04]);

    if (vfo == 1)
    {
        // This is for the second VFO!
        return;
    }

    // unsigned char waveInfo = payloadIn[06]; // really just one byte?
    //qInfo(logRig()) << "Spectrum Data received: " << sequence << "/" << sequenceMax << " mode: " << scopeMode << " waveInfo: " << waveInfo << " length: " << payloadIn.length();

    // Sequnce 2, index 05 is the start of data
    // Sequence 11. index 05, is the last chunk
    // Sequence 11, index 29, is the actual last pixel (it seems)

    // It looks like the data length may be variable, so we need to detect it each time.
    // start at payloadIn.length()-1 (to override the FD). Never mind, index -1 bad.
    // chop off FD.
    if ((sequence == 1) && (sequence < rigCaps.spectSeqMax))
    {

        spectrumMode scopeMode = (spectrumMode)bcdHexToUChar(payloadIn[05]); // 0=center, 1=fixed

        if(scopeMode != oldScopeMode)
        {
            //TODO: support the other two modes (firmware 1.40)
            // Modes:
            // 0x00 Center
            // 0x01 Fixed
            // 0x02 Scroll-C
            // 0x03 Scroll-F
            emit haveSpectrumMode(scopeMode);
            oldScopeMode = scopeMode;
        }

        if(payloadIn.length() >= 15)
        {
            bool outOfRange = (bool)payloadIn[16];
            if(outOfRange != wasOutOfRange)
            {
                emit haveScopeOutOfRange(outOfRange);
                wasOutOfRange = outOfRange;
                return;
            }
        }

        // wave information
        spectrumLine.clear();
        // For Fixed, and both scroll modes, the following produces correct information:
        fStart = parseFrequency(payloadIn, 9);
        spectrumStartFreq = fStart.MHzDouble;
        fEnd = parseFrequency(payloadIn, 14);
        spectrumEndFreq = fEnd.MHzDouble;
        if(scopeMode == spectModeCenter)
        {
            // "center" mode, start is actual center, end is bandwidth.
            spectrumStartFreq -= spectrumEndFreq;
            spectrumEndFreq = spectrumStartFreq + 2*(spectrumEndFreq);
            // emit haveSpectrumCenterSpan(span);
        }

        if (payloadIn.length() > 400) // Must be a LAN packet.
        {
            payloadIn.chop(1);
            //spectrumLine.append(payloadIn.mid(17,475)); // write over the FD, last one doesn't, oh well.
            spectrumLine.append(payloadIn.right(payloadIn.length()-17)); // write over the FD, last one doesn't, oh well.
            emit haveSpectrumData(spectrumLine, spectrumStartFreq, spectrumEndFreq);
        }
    } else if ((sequence > 1) && (sequence < rigCaps.spectSeqMax))
    {
        // spectrum from index 05 to index 54, length is 55 per segment. Length is 56 total. Pixel data is 50 pixels.
        // sequence numbers 2 through 10, 50 pixels each. Total after sequence 10 is 450 pixels.
        payloadIn.chop(1);
        spectrumLine.insert(spectrumLine.length(), payloadIn.right(payloadIn.length() - 5)); // write over the FD, last one doesn't, oh well.
        //qInfo(logRig()) << "sequence: " << sequence << "spec index: " << (sequence-2)*55 << " payloadPosition: " << payloadIn.length() - 5 << " payload length: " << payloadIn.length();
    } else if (sequence == rigCaps.spectSeqMax)
    {
        // last spectrum, a little bit different (last 25 pixels). Total at end is 475 pixels (7300).
        payloadIn.chop(1);
        spectrumLine.insert(spectrumLine.length(), payloadIn.right(payloadIn.length() - 5));
        //qInfo(logRig()) << "sequence: " << sequence << " spec index: " << (sequence-2)*55 << " payloadPosition: " << payloadIn.length() - 5 << " payload length: " << payloadIn.length();
        emit haveSpectrumData(spectrumLine, spectrumStartFreq, spectrumEndFreq);
    }
}

void rigCommander::parseSpectrumRefLevel()
{
    // 00: 27
    // 01: 19
    // 02: 00 (fixed)
    // 03: XX
    // 04: x0
    // 05: 00 (+) or 01 (-)

    unsigned char negative = payloadIn[5];
    int value = bcdHexToUInt(payloadIn[3], payloadIn[4]);
    value = value / 10;
    if(negative){
        value *= (-1*negative);
    }
    emit haveSpectrumRefLevel(value);
}

unsigned char rigCommander::bcdHexToUChar(unsigned char in)
{
    unsigned char out = 0;
    out = in & 0x0f;
    out += ((in & 0xf0) >> 4)*10;
    return out;
}

unsigned int rigCommander::bcdHexToUInt(unsigned char hundreds, unsigned char tensunits)
{
    // convert:
    // hex data: 0x41 0x23
    // convert to uint:
    // uchar: 4123
    unsigned char thousands = ((hundreds & 0xf0)>>4);
    unsigned int rtnVal;
    rtnVal = (hundreds & 0x0f)*100;
    rtnVal += ((tensunits & 0xf0)>>4)*10;
    rtnVal += (tensunits & 0x0f);
    rtnVal += thousands * 1000;

    return rtnVal;
}

unsigned char rigCommander::bcdHexToUChar(unsigned char hundreds, unsigned char tensunits)
{
    // convert:
    // hex data: 0x01 0x23
    // convert to uchar:
    // uchar: 123

    unsigned char rtnVal;
    rtnVal = (hundreds & 0x0f)*100;
    rtnVal += ((tensunits & 0xf0)>>4)*10;
    rtnVal += (tensunits & 0x0f);

    return rtnVal;
}

QByteArray rigCommander::bcdEncodeInt(unsigned int num)
{
    if(num > 9999)
    {
        qInfo(logRig()) << __FUNCTION__ << "Error, number is too big for four-digit conversion: " << num;
        return QByteArray();
    }

    char thousands = num / 1000;
    char hundreds = (num - (1000*thousands)) / 100;
    char tens = (num - (1000*thousands) - (100*hundreds)) / 10;
    char units = (num - (1000*thousands) - (100*hundreds) - (10*tens));

    char b0 = hundreds | (thousands << 4);
    char b1 = units | (tens << 4);

    //qInfo(logRig()) << __FUNCTION__ << " encoding value " << num << " as hex:";
    //printHex(QByteArray(b0), false, true);
    //printHex(QByteArray(b1), false, true);


    QByteArray result;
    result.append(b0).append(b1);
    return result;
}

void rigCommander::parseFrequency()
{
    freqt freq;
    freq.Hz = 0;
    freq.MHzDouble = 0;

    // process payloadIn, which is stripped.
    // float frequencyMhz
    //    payloadIn[04] = ; // XX MHz
    //    payloadIn[03] = ; //   XX0     KHz
    //    payloadIn[02] = ; //     X.X   KHz
    //    payloadIn[01] = ; //      . XX KHz

    // printHex(payloadIn, false, true);

    frequencyMhz = 0.0;
    if (payloadIn.length() == 7)
    {
        // 7300 has these digits too, as zeros.
        // IC-705 or IC-9700 with higher frequency data available.
        frequencyMhz += 100 * (payloadIn[05] & 0x0f);
        frequencyMhz += (1000 * ((payloadIn[05] & 0xf0) >> 4));

        freq.Hz += (payloadIn[05] & 0x0f) * 1E6 * 100;
        freq.Hz += ((payloadIn[05] & 0xf0) >> 4) * 1E6 * 1000;

    }

    freq.Hz += (payloadIn[04] & 0x0f) * 1E6;
    freq.Hz += ((payloadIn[04] & 0xf0) >> 4) * 1E6 * 10;

    frequencyMhz += payloadIn[04] & 0x0f;
    frequencyMhz += 10 * ((payloadIn[04] & 0xf0) >> 4);

    // KHz land:
    frequencyMhz += ((payloadIn[03] & 0xf0) >> 4) / 10.0;
    frequencyMhz += (payloadIn[03] & 0x0f) / 100.0;

    frequencyMhz += ((payloadIn[02] & 0xf0) >> 4) / 1000.0;
    frequencyMhz += (payloadIn[02] & 0x0f) / 10000.0;

    frequencyMhz += ((payloadIn[01] & 0xf0) >> 4) / 100000.0;
    frequencyMhz += (payloadIn[01] & 0x0f) / 1000000.0;

    freq.Hz += payloadIn[01] & 0x0f;
    freq.Hz += ((payloadIn[01] & 0xf0) >> 4) * 10;

    freq.Hz += (payloadIn[02] & 0x0f) * 100;
    freq.Hz += ((payloadIn[02] & 0xf0) >> 4) * 1000;

    freq.Hz += (payloadIn[03] & 0x0f) * 10000;
    freq.Hz += ((payloadIn[03] & 0xf0) >> 4) * 100000;

    freq.MHzDouble = frequencyMhz;
    
    if (state.getChar(CURRENTVFO) == 0) {
        state.set(VFOAFREQ, freq.Hz, false);
    }
    else {
        state.set(VFOBFREQ, freq.Hz, false);
    }
    
    emit haveFrequency(freq);
}

freqt rigCommander::parseFrequencyRptOffset(QByteArray data)
{
    // VHF 600 KHz:
    // DATA:  0c 00 60 00 fd
    // INDEX: 00 01 02 03 04

    // UHF 5 MHz:
    // DATA:  0c 00 00 05 fd
    // INDEX: 00 01 02 03 04

    freqt f;
    f.Hz = 0;

    f.Hz += (data[3] & 0x0f)        *    1E6; // 1 MHz
    f.Hz += ((data[3] & 0xf0) >> 4) *    1E6 * 10; //   10 MHz
    f.Hz += (data[2] & 0x0f) *          10E3; // 10 KHz
    f.Hz += ((data[2] & 0xf0) >> 4) *  100E3; // 100 KHz
    f.Hz += (data[1] & 0x0f) *           100; // 100 Hz
    f.Hz += ((data[1] & 0xf0) >> 4) *   1000; // 1 KHz
    f.MHzDouble = f.Hz/1000000.0;
    f.VFO=activeVFO;
    return f;
}

freqt rigCommander::parseFrequency(QByteArray data, unsigned char lastPosition)
{
    // process payloadIn, which is stripped.
    // float frequencyMhz
    //    payloadIn[04] = ; // XX MHz
    //    payloadIn[03] = ; //   XX0     KHz
    //    payloadIn[02] = ; //     X.X   KHz
    //    payloadIn[01] = ; //      . XX KHz

    //printHex(data, false, true);

    // TODO: Check length of data array prior to reading +/- position

    // NOTE: This function was written on the IC-7300, which has no need for 100 MHz and 1 GHz.
    //       Therefore, this function has to go to position +1 to retrieve those numbers for the IC-9700.

    freqt freqs;
    freqs.MHzDouble = 0;
    freqs.Hz = 0;

    // Does Frequency contain 100 MHz/1 GHz data?
    if(data.length() >= lastPosition+1)
    {
        freqs.Hz += (data[lastPosition+1] & 0x0f) * 1E6 *         100; //  100 MHz
        freqs.Hz += ((data[lastPosition+1] & 0xf0) >> 4) * 1E6 * 1000; // 1000 MHz
    }

    // Does Frequency contain VFO data? (\x25 command)
    if (lastPosition-4 >= 0 && (quint8)data[lastPosition-4] < 0x02)
    {
        freqs.VFO=(selVFO_t)(quint8)data[lastPosition-4];
    }

    freqs.Hz += (data[lastPosition] & 0x0f) * 1E6;
    freqs.Hz += ((data[lastPosition] & 0xf0) >> 4) * 1E6 *     10; //   10 MHz

    freqs.Hz += (data[lastPosition-1] & 0x0f) *          10E3; // 10 KHz
    freqs.Hz += ((data[lastPosition-1] & 0xf0) >> 4) *  100E3; // 100 KHz

    freqs.Hz += (data[lastPosition-2] & 0x0f) *           100; // 100 Hz
    freqs.Hz += ((data[lastPosition-2] & 0xf0) >> 4) *   1000; // 1 KHz

    freqs.Hz += (data[lastPosition-3] & 0x0f) *             1; // 1 Hz
    freqs.Hz += ((data[lastPosition-3] & 0xf0) >> 4) *     10; // 10 Hz

    freqs.MHzDouble = (double)(freqs.Hz / 1000000.0);
    return freqs;
}


void rigCommander::parseMode()
{
    unsigned char filter=0;
    unsigned char mode=0;

    if(payloadIn[0] == '\x26')
    {
        // New format payload with mode+datamode+filter
        mode = (unsigned char)payloadIn[2];
        filter = (unsigned char)payloadIn[4];
    } else {
        if(payloadIn[2] != '\xFD')
        {
            filter = payloadIn[2];
        } else {
            filter = 0;
        }
        mode = (unsigned char)payloadIn[01];
    }
    emit haveMode(mode, filter);
    state.set(MODE,mode,false);
    state.set(FILTER, filter, false);
    quint16 pass = 0;

    if (!state.isValid(PASSBAND)) {
 
        /*  We haven't got a valid passband from the rig so we 
            need to create a 'fake' one from default values
            This will be replaced with a valid one if we get it */

        if (mode == 3 || mode == 7 || mode == 12 || mode == 17) {
            switch (filter) {
            case 1:
                pass=1200;
                break;
            case 2:
                pass=500;
                break;
            case 3:
                pass=250;
                break;
            }
        }
        else if (mode == 4 || mode == 8)
        {
            switch (filter) {
            case 1:
                pass=2400;
                break;
            case 2:
                pass=500;
                break;
            case 3:
                pass=250;
                break;
            }
        }
        else if (mode == 2)
        {
            switch (filter) {
            case 1:
                pass=9000;
                break;
            case 2:
                pass=6000;
                break;
            case 3:
                pass=3000;
                break;
            }
        }
        else if (mode == 5)
        {
            switch (filter) {
            case 1:
                pass=15000;
                break;
            case 2:
                pass=10000;
                break;
            case 3:
                pass=7000;
                break;
            }
        }
        else { // SSB or unknown mode
            switch (filter) {
            case 1:
                pass=3000;
                break;
            case 2:
                pass=2400;
                break;
            case 3:
                pass=1800;
                break;
            }
        }
    }
    state.set(PASSBAND, pass, false);
}


void rigCommander::startATU()
{
    QByteArray payload;
    if (getCommand(funcTunerStatus,payload))
    {
        payload.append(static_cast<unsigned char>(0x02));
        prepDataAndSend(payload);
    }
}

void rigCommander::setATU(bool enabled)
{
    QByteArray payload;
    if (getCommand(funcTunerStatus,payload,static_cast<int>(enabled)))
    {
        payload.append(static_cast<unsigned char>(enabled));
        prepDataAndSend(payload);
    }
}

void rigCommander::getATUStatus()
{
    QByteArray payload;
    if (getCommand(funcTunerStatus,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getAttenuator()
{
    QByteArray payload;
    if (getCommand(funcAttenuator,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getPreamp()
{
    QByteArray payload;
    if (getCommand(funcPreamp,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getAntenna()
{
    QByteArray payload;
    if (getCommand(funcAntenna,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setAttenuator(unsigned char att)
{
    QByteArray payload;
    if (getCommand(funcAttenuator,payload,att))
    {
        payload.append(att);
        prepDataAndSend(payload);
    }
}

void rigCommander::setPreamp(unsigned char pre)
{
    QByteArray payload;
    if (getCommand(funcPreamp,payload,pre))
    {
        payload.append(pre);
        prepDataAndSend(payload);
    }
}

void rigCommander::setAntenna(unsigned char ant, bool rx)
{
    QByteArray payload;
    if (getCommand(funcAntenna,payload,ant))
    {
        payload.append(ant);
        if (rigCaps.commands.contains(funcRXAntenna)) {
            payload.append(static_cast<unsigned char>(rx)); // 0x00 = use for TX and RX
        }
        prepDataAndSend(payload);
    }
}

void rigCommander::setNB(bool enabled) {
    QByteArray payload;
    if (getCommand(funcNoiseBlanker,payload,static_cast<int>(enabled)))
    {
        payload.append(static_cast<unsigned char>(enabled));
        prepDataAndSend(payload);
    }
}

void rigCommander::getNB()
{
    QByteArray payload;
    if (getCommand(funcNoiseBlanker,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setNR(bool enabled) {
    QByteArray payload;
    if (getCommand(funcNoiseReduction,payload,static_cast<int>(enabled)))
    {
        payload.append(static_cast<unsigned char>(enabled));
        prepDataAndSend(payload);
    }
}

void rigCommander::getNR()
{
    QByteArray payload;
    if (getCommand(funcNoiseReduction,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setAutoNotch(bool enabled)
{
    QByteArray payload;
    if (getCommand(funcAutoNotch,payload,static_cast<int>(enabled)))
    {
        payload.append(static_cast<unsigned char>(enabled));
        prepDataAndSend(payload);
    }
}

void rigCommander::getAutoNotch()
{
    QByteArray payload;
    if (getCommand(funcAutoNotch,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setToneEnabled(bool enabled)
{
    QByteArray payload;
    if (getCommand(funcRepeaterTone,payload,static_cast<int>(enabled)))
    {
        payload.append(static_cast<unsigned char>(enabled));
        prepDataAndSend(payload);
    }
}

void rigCommander::getToneEnabled()
{
    QByteArray payload;
    if (getCommand(funcRepeaterTone,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setToneSql(bool enabled)
{
    QByteArray payload;
    if (getCommand(funcRepeaterTSQL,payload,static_cast<int>(enabled)))
    {
        payload.append(static_cast<unsigned char>(enabled));
        prepDataAndSend(payload);
    }
}

void rigCommander::getToneSqlEnabled()
{
    QByteArray payload;
    if (getCommand(funcRepeaterTSQL,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setCompressor(bool enabled)
{
    QByteArray payload;
    if (getCommand(funcCompressor,payload,static_cast<int>(enabled)))
    {
        payload.append(static_cast<unsigned char>(enabled));
        prepDataAndSend(payload);
    }
}

void rigCommander::getCompressor()
{
    QByteArray payload;
    if (getCommand(funcCompressor,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setMonitor(bool enabled)
{
    QByteArray payload;
    if (getCommand(funcMonitor,payload,static_cast<int>(enabled)))
    {
        payload.append(static_cast<unsigned char>(enabled));
        prepDataAndSend(payload);
    }
}

void rigCommander::getMonitor()
{
    QByteArray payload;
    if (getCommand(funcMonitor,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setVox(bool enabled)
{
    QByteArray payload;
    if (getCommand(funcVox,payload,static_cast<int>(enabled)))
    {
        payload.append(static_cast<unsigned char>(enabled));
        prepDataAndSend(payload);
    }
}

void rigCommander::getVox()
{
    QByteArray payload;
    if (getCommand(funcVox,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setBreakIn(unsigned char type)
{
    QByteArray payload;
    if (getCommand(funcBreakIn,payload,type))
    {
        payload.append(type);
        prepDataAndSend(payload);
    }
}

void rigCommander::getBreakIn()
{
    QByteArray payload;
    if (getCommand(funcBreakIn,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setKeySpeed(unsigned char wpm)
{
    // 0 = 6 WPM
    // 255 = 48 WPM
    QByteArray payload;
    if (getCommand(funcKeySpeed,payload,wpm))
    {
        unsigned char wpmRadioSend = round((wpm-6) * (6.071));
        payload.append(bcdEncodeInt(wpmRadioSend));
        prepDataAndSend(payload);
    }
}

void rigCommander::getKeySpeed()
{
    QByteArray payload;
    if (getCommand(funcKeySpeed,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::setManualNotch(bool enabled)
{
    QByteArray payload;
    if (getCommand(funcManualNotch,payload,static_cast<int>(enabled)))
    {
        payload.append(static_cast<unsigned char>(enabled));
        prepDataAndSend(payload);
    }
}

void rigCommander::getManualNotch()
{
    QByteArray payload;
    if (getCommand(funcManualNotch,payload))
    {
        prepDataAndSend(payload);
    }
}

void rigCommander::getRigID()
{
    QByteArray payload;
    if (getCommand(funcTransceiverId,payload))
    {
        prepDataAndSend(payload);
    } else {
        // If we haven't got this command yet, need to use the default one!
        QByteArray payload = "\x19\x00";
        prepDataAndSend(payload);
    }
}

void rigCommander::setRigID(unsigned char rigID)
{
    // This function overrides radio model detection.
    // It can be used for radios without Rig ID commands,
    // or to force a specific radio model

    qInfo(logRig()) << "Setting rig ID to: (int)" << (int)rigID;


    lookingForRig = true;
    foundRig = false;

    // needed because this is a fake message and thus the value is uninitialized
    // this->civAddr comes from how rigCommander is setup and should be accurate.
    this->incomingCIVAddr = this->civAddr;

    this->model = determineRadioModel(rigID);
    rigCaps.modelID = rigID;
    rigCaps.model = determineRadioModel(rigID);

    determineRigCaps();
}

void rigCommander::changeLatency(const quint16 value)
{
    emit haveChangeLatency(value);
}

void rigCommander::sayAll()
{
    QByteArray payload;
    unsigned char cmd = 0x0;
    if (getCommand(funcSpeech,payload,cmd))
    {
        payload.append(cmd);
        prepDataAndSend(payload);
    }
}

void rigCommander::sayFrequency()
{
    QByteArray payload;
    unsigned char cmd = 0x1;
    if (getCommand(funcSpeech,payload,cmd))
    {
        payload.append(cmd);
        prepDataAndSend(payload);
    }
}

void rigCommander::sayMode()
{
    QByteArray payload;
    unsigned char cmd = 0x2;
    if (getCommand(funcSpeech,payload,cmd))
    {
        payload.append(cmd);
        prepDataAndSend(payload);
    }
}

// Other:

QByteArray rigCommander::stripData(const QByteArray &data, unsigned char cutPosition)
{
    QByteArray rtndata;
    if(data.length() < cutPosition)
    {
        return rtndata;
    }

    rtndata = data.right(cutPosition);
    return rtndata;
}

void rigCommander::sendState()
{
    emit stateInfo(&state);
}

void rigCommander::radioSelection(QList<radio_cap_packet> radios)
{
    emit requestRadioSelection(radios);
}

void rigCommander::radioUsage(quint8 radio, quint8 busy, QString user, QString ip) {
    emit setRadioUsage(radio, busy, user, ip);
}

void rigCommander::setCurrentRadio(quint8 radio) {
    emit selectedRadio(radio);
}

void rigCommander::stateUpdated()
{
    // A remote process has updated the rigState
    // First we need to find which item(s) have been updated and send the command(s) to the rig.

    QMap<stateTypes, value>::iterator i = state.map.begin();
    while (i != state.map.end()) {
        if (!i.value()._valid || i.value()._updated)
        {
            i.value()._updated = false;
            i.value()._valid = true; // Set value to valid as we have requested it (even if we haven't had a response)
            qDebug(logRigCtlD()) << "Got new value:" << i.key() << "=" << i.value()._value;
            switch (i.key()) {
            case VFOAFREQ:
                if (i.value()._valid) {
                    freqt freq;
                    freq.Hz = state.getInt64(VFOAFREQ);
                    setFrequency(0, freq);
                    setFrequency(0, freq);
                    setFrequency(0, freq);
                }
                getFrequency();
                break;
            case VFOBFREQ:
                if (i.value()._valid) {
                    freqt freq;
                    freq.Hz = state.getInt64(VFOBFREQ);
                    setFrequency(1, freq);
                    setFrequency(1, freq);
                    setFrequency(1, freq);
                }
                getFrequency();
                break;
            case CURRENTVFO:
                // Work on VFOB - how do we do this?
                break;
            case PTT:
                if (i.value()._valid) {
                    setPTT(state.getBool(PTT));
                    setPTT(state.getBool(PTT));
                    setPTT(state.getBool(PTT));
                }
                getPTT();
                break;
            case MODE:
            case FILTER:
                if (state.isValid(MODE) && state.isValid(FILTER)) {
                    setMode(state.getChar(MODE), state.getChar(FILTER));
                }
                getMode();
                break;
            case PASSBAND:
                if (i.value()._valid && state.isValid(MODE)) {
                    setPassband(state.getUInt16(PASSBAND));
                }
                getPassband();
                break;
            case DUPLEX:
                if (i.value()._valid) {
                    setDuplexMode(state.getDuplex(DUPLEX));
                }
                getDuplexMode();
                break;
            case DATAMODE:
                if (i.value()._valid) {
                    setDataMode(state.getBool(DATAMODE), state.getChar(FILTER));
                }
                getDataMode();
                break;
            case ANTENNA:
            case RXANTENNA:
                if (i.value()._valid) {
                    setAntenna(state.getChar(ANTENNA), state.getBool(RXANTENNA));
                }
                getAntenna();
                break;
            case CTCSS:
                if (i.value()._valid) {
                    setTone(state.getChar(CTCSS));
                }
                getTone();
                break;
            case TSQL:
                if (i.value()._valid) {
                    setTSQL(state.getChar(TSQL));
                }
                getTSQL();
                break;
            case DTCS:
                if (i.value()._valid) {
                    setDTCS(state.getChar(DTCS), false, false); // Not sure about this?
                }
                getDTCS();
                break;
            case CSQL:
                if (i.value()._valid) {
                    setTone(state.getChar(CSQL));
                }
                getTone();
                break;
            case PREAMP:
                if (i.value()._valid) {
                    setPreamp(state.getChar(PREAMP));
                }
                getPreamp();
                break;
            case ATTENUATOR:
                if (i.value()._valid) {
                    setAttenuator(state.getChar(ATTENUATOR));
                }
                getAttenuator();
                break;
            case AFGAIN:
                if (i.value()._valid) {
                    setAfGain(state.getChar(AFGAIN));
                }
                getAfGain();
                break;
            case RFGAIN:
                if (i.value()._valid) {
                    setRfGain(state.getChar(RFGAIN));
                }
                getRfGain();
                break;
            case SQUELCH:
                if (i.value()._valid) {
                    setSquelch(state.getChar(SQUELCH));
                }
                getSql();
                break;
            case RFPOWER:
                if (i.value()._valid) {
                    setTxPower(state.getChar(RFPOWER));
                }
                getTxLevel();
                break;
            case MICGAIN:
                if (i.value()._valid) {
                    setMicGain(state.getChar(MICGAIN));
                }
                getMicGain();
                break;
            case COMPLEVEL:
                if (i.value()._valid) {
                    setCompLevel(state.getChar(COMPLEVEL));
                }
                getCompLevel();
                break;
            case MONITORLEVEL:
                if (i.value()._valid) {
                    setMonitorGain(state.getChar(MONITORLEVEL));
                }
                getMonitorGain();
                break;
            case VOXGAIN:
                if (i.value()._valid) {
                    setVoxGain(state.getChar(VOXGAIN));
                }
                getVoxGain();
                break;
            case ANTIVOXGAIN:
                if (i.value()._valid) {
                    setAntiVoxGain(state.getChar(ANTIVOXGAIN));
                }
                getAntiVoxGain();
                break;
            case NBFUNC:
                if (i.value()._valid) {
                    setNB(state.getBool(NBFUNC));
                }
                getNB();
                break;
            case NRFUNC:
                if (i.value()._valid) {
                    setNR(state.getBool(NRFUNC));
                }
                getNR();
                break;
            case ANFFUNC:
                if (i.value()._valid) {
                    setAutoNotch(state.getBool(ANFFUNC));
                }
                getAutoNotch();
                break;
            case TONEFUNC:
                if (i.value()._valid) {
                    setToneEnabled(state.getBool(TONEFUNC));
                }
                getToneEnabled();
                break;
            case TSQLFUNC:
                if (i.value()._valid) {
                    setToneSql(state.getBool(TSQLFUNC));
                }
                getToneSqlEnabled();
                break;
            case COMPFUNC:
                if (i.value()._valid) {
                    setCompressor(state.getBool(COMPFUNC));
                }
                getCompressor();
                break;
            case MONFUNC:
                if (i.value()._valid) {
                    setMonitor(state.getBool(MONFUNC));
                }
                getMonitor();
                break;
            case VOXFUNC:
                if (i.value()._valid) {
                    setVox(state.getBool(VOXFUNC));
                }
                getVox();
                break;
            case SBKINFUNC:
                if (i.value()._valid) {
                    setBreakIn(state.getBool(VOXFUNC));
                }
                getVox();
                break;
            case FBKINFUNC:
                if (i.value()._valid) {
                    setBreakIn(state.getBool(VOXFUNC) << 1);
                }
                getBreakIn();
                break;
            case MNFUNC:
                if (i.value()._valid) {
                    setManualNotch(state.getBool(MNFUNC));
                }
                getManualNotch();
                break;
            case SCOPEFUNC:
                if (i.value()._valid) {
                    if (state.getBool(SCOPEFUNC)) {
                        enableSpectOutput();
                    }
                    else {
                        disableSpectOutput();
                    }
                }
                break;
            case RIGINPUT:
                if (i.value()._valid) {
                    setModInput(state.getInput(RIGINPUT), state.getBool(DATAMODE));
                }
                getModInput(state.getBool(DATAMODE));
                break;
            case POWERONOFF:
                if (i.value()._valid) {
                    if (state.getBool(POWERONOFF)) {
                        powerOn();
                    }
                    else {
                        powerOff();
                    }
                }
                break;
            case RITVALUE:
                if (i.value()._valid) {
                    setRitValue(state.getInt32(RITVALUE));
                }
                getRitValue();
                break;
             case RITFUNC:
                 if (i.value()._valid) {
                     setRitEnable(state.getBool(RITFUNC));
                 }
                 getRitEnabled();
                 break;
                 // All meters can only be updated from the rig end.
             case SMETER:
             case SWRMETER:
             case POWERMETER:
             case ALCMETER:
             case COMPMETER:
             case VOLTAGEMETER:
             case CURRENTMETER:
                 break;
             case AGC:
                 break;
             case MODINPUT:
                 break;
             case FAGCFUNC:
                 break;
             case AIPFUNC:
                 break;
             case APFFUNC:
                 break;
             case RFFUNC: // Should this set RF output power to 0?
                 break;
             case AROFUNC:
                 break;
             case MUTEFUNC:
                 if (i.value()._valid) {
                     setAfMute(state.getBool(MUTEFUNC));
                 }
                 getAfMute();
                 break;
             case VSCFUNC:
                 break;
             case REVFUNC:
                 break;
             case SQLFUNC:
                 break;
             case ABMFUNC:
                 break;
             case BCFUNC:
                 break;
             case MBCFUNC:
                 break;
             case AFCFUNC:
                 break;
             case SATMODEFUNC:
                 break;
             case NBDEPTH:
                 break;
             case NBWIDTH:
                 break;
             case NB:
                 break;
             case NR: {
                 if (i.value()._valid) {
                     QByteArray payload("\x14\x06");
                     payload.append(bcdEncodeInt(state.getChar(NR)));
                     prepDataAndSend(payload);
                 }
                 break;
             }
             case PBTIN: {
                 if (i.value()._valid) {
                     QByteArray payload("\x14\x07");
                     payload.append(bcdEncodeInt(state.getChar(PBTIN)));
                     prepDataAndSend(payload);
                 }
                 break;
             }
             case PBTOUT: {
                 if (i.value()._valid) {
                     QByteArray payload("\x14\x08");
                     payload.append(bcdEncodeInt(state.getChar(PBTOUT)));
                     prepDataAndSend(payload);
                 }
                 break;
             }
             case CWPITCH: {
                 if (i.value()._valid) {
                     QByteArray payload("\x14\x09");
                     payload.append(bcdEncodeInt(state.getChar(CWPITCH)));
                     prepDataAndSend(payload);
                 }
                 break;
             }
             case KEYSPD: {
                 if (i.value()._valid) {
                     QByteArray payload("\x14\x0c");
                     payload.append(bcdEncodeInt(state.getChar(KEYSPD)));
                     prepDataAndSend(payload);
                 }
                 break;
             }
             case NOTCHF: {
                 if (i.value()._valid) {
                     QByteArray payload("\x14\x0d");
                     payload.append(bcdEncodeInt(state.getChar(NOTCHF)));
                     prepDataAndSend(payload);
                 }
                 break;
             }
             case IF: {
                 if (i.value()._valid) {
                     setIFShift(state.getChar(IF));
                 }
                 getIFShift();
                 break;
             }
             case APF:
                 break;
             case BAL:
                 break;
             case RESUMEFUNC:
                 break;
             case TBURSTFUNC:
                 break;
             case TUNERFUNC:
                 if (i.value()._valid) {  
                     setATU(state.getBool(TUNERFUNC));
                 }
                 getATUStatus();
                 break;
             case LOCKFUNC:
                 if (i.value()._valid) {
                     setDialLock(state.getBool(LOCKFUNC));
                 }
                 getDialLock();
                 break;

            case ANN:
            case APO:
            case BACKLIGHT:
            case BEEP:
            case TIME:
            case BAT:
            case KEYLIGHT:
                break;

            }
        }
        ++i;
    }
}

void rigCommander::getDebug()
{
    // generic debug function for development.
    emit getMoreDebug();
}

void rigCommander::printHex(const QByteArray &pdata)
{
    printHex(pdata, false, true);
}

void rigCommander::printHex(const QByteArray &pdata, bool printVert, bool printHoriz)
{
    qDebug(logRig()) << "---- Begin hex dump -----:";
    QString sdata("DATA:  ");
    QString index("INDEX: ");
    QStringList strings;

    for(int i=0; i < pdata.length(); i++)
    {
        strings << QString("[%1]: %2").arg(i,8,10,QChar('0')).arg((unsigned char)pdata[i], 2, 16, QChar('0'));
        sdata.append(QString("%1 ").arg((unsigned char)pdata[i], 2, 16, QChar('0')) );
        index.append(QString("%1 ").arg(i, 2, 10, QChar('0')));
    }

    if(printVert)
    {
        for(int i=0; i < strings.length(); i++)
        {
            //sdata = QString(strings.at(i));
            qDebug(logRig()) << strings.at(i);
        }
    }

    if(printHoriz)
    {
        qDebug(logRig()) << index;
        qDebug(logRig()) << sdata;
    }
    qDebug(logRig()) << "----- End hex dump -----";
}

void rigCommander::dataFromServer(QByteArray data)
{
    //qInfo(logRig()) << "***************** emit dataForComm()" << data;
    emit dataForComm(data);
}

quint8* rigCommander::getGUID() {
    return guid;
}







