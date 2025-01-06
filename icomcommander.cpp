#include "icomcommander.h"
#include <QDebug>

#include "rigidentities.h"
#include "logcategories.h"
#include "printhex.h"

// Copyright 2017-2024 Elliott H. Liggett W6EL and Phil E. Taylor M0VSE

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

icomCommander::icomCommander(rigCommander* parent) : rigCommander(parent)
{

    qInfo(logRig()) << "creating instance of icomCommander()";

}

icomCommander::icomCommander(quint8 guid[GUIDLEN], rigCommander* parent) : rigCommander(parent)
{
    qInfo(logRig()) << "creating instance of icomCommander() with GUID";
    memcpy(this->guid, guid, GUIDLEN);
    // Add some commands that is a minimum for rig detection
}

icomCommander::~icomCommander()
{
    qInfo(logRig()) << "closing instance of icomCommander()";

    queue->setRigCaps(Q_NULLPTR); // Remove access to rigCaps

    qDebug(logRig()) << "Closing rig comms";
    if (comm != Q_NULLPTR) {
        delete comm;
    }

    if (udpHandlerThread != Q_NULLPTR) {
        udpHandlerThread->quit();
        udpHandlerThread->wait();
    }

    if (ptty != Q_NULLPTR) {
        delete ptty;
    }
}


void icomCommander::commSetup(QHash<quint8,rigInfo> rigList, quint8 rigCivAddr, QString rigSerialPort, quint32 rigBaudRate, QString vsp,quint16 tcpPort, quint8 wf)
{
    // construct

    // TODO: Bring this parameter and the comm port from the UI.
    // Keep in hex in the UI as is done with other CIV apps.
    this->rigList = rigList;
    civAddr = rigCivAddr; // address of the radio.
    usingNativeLAN = false;

    this->rigSerialPort = rigSerialPort;
    this->rigBaudRate = rigBaudRate;
    rigCaps.baudRate = rigBaudRate;


    comm = new commHandler(rigSerialPort, rigBaudRate,wf,this);
    // data from the comm port to the program:
    connect(comm, SIGNAL(haveDataFromPort(QByteArray)), this, SLOT(handleNewData(QByteArray)));
    // data from the program to the comm port:
    connect(this, SIGNAL(dataForComm(QByteArray)), comm, SLOT(receiveDataFromUserToRig(QByteArray)));
    // Whether radio is half duplex only
    connect(this, SIGNAL(setHalfDuplex(bool)), comm, SLOT(setHalfDuplex(bool)));
    connect(comm, SIGNAL(havePortError(errorType)), this, SLOT(handlePortError(errorType)));
    connect(this, SIGNAL(getMoreDebug()), comm, SLOT(debugThis()));


    if (vsp.toLower() != "none") {
        qInfo(logRig()) << "Attempting to connect to vsp/pty:" << vsp;
        ptty = new pttyHandler(vsp,this);
        // data from the ptty to the rig:
        connect(ptty, SIGNAL(haveDataFromPort(QByteArray)), comm, SLOT(receiveDataFromUserToRig(QByteArray)));
        // data from the rig to the ptty:
        connect(comm, SIGNAL(haveDataFromPort(QByteArray)), ptty, SLOT(receiveDataFromRigToPtty(QByteArray)));
        connect(ptty, SIGNAL(havePortError(errorType)), this, SLOT(handlePortError(errorType)));
        connect(this, SIGNAL(getMoreDebug()), ptty, SLOT(debugThis()));
    }

    if (tcpPort > 0) {
        tcp = new tcpServer(this);
        tcp->startServer(tcpPort);
        // data from the tcp port to the rig:
        connect(tcp, SIGNAL(receiveData(QByteArray)), comm, SLOT(receiveDataFromUserToRig(QByteArray)));
        connect(comm, SIGNAL(haveDataFromPort(QByteArray)), tcp, SLOT(sendData(QByteArray)));
    }

    commonSetup();
}

void icomCommander::commSetup(QHash<quint8,rigInfo> rigList, quint8 rigCivAddr, udpPreferences prefs, audioSetup rxSetup, audioSetup txSetup, QString vsp, quint16 tcpPort)
{
    // construct
    // TODO: Bring this parameter and the comm port from the UI.
    // Keep in hex in the UI as is done with other CIV apps.

    this->rigList = rigList;
    civAddr = rigCivAddr; // address of the radio
    usingNativeLAN = true;

    if (udp != Q_NULLPTR) {
        closeComm();
    }

    udp = new udpHandler(prefs,rxSetup,txSetup);

    udpHandlerThread = new QThread(this);
    udpHandlerThread->setObjectName("udpHandler()");

    udp->moveToThread(udpHandlerThread);

    connect(this, SIGNAL(initUdpHandler()), udp, SLOT(init()));
    connect(udpHandlerThread, SIGNAL(finished()), udp, SLOT(deleteLater()));
    udpHandlerThread->start();

    emit initUdpHandler();

    // Data from UDP to the program
    connect(udp, SIGNAL(haveDataFromPort(QByteArray)), this, SLOT(handleNewData(QByteArray)));
    // data from the program to the rig:
    connect(this, SIGNAL(dataForComm(QByteArray)), udp, SLOT(receiveDataFromUserToRig(QByteArray)));
    // Audio from UDP
    connect(udp, SIGNAL(haveAudioData(audioPacket)), this, SLOT(receiveAudioData(audioPacket)));

    connect(this, SIGNAL(haveChangeLatency(quint16)), udp, SLOT(changeLatency(quint16)));
    connect(this, SIGNAL(haveSetVolume(quint8)), udp, SLOT(setVolume(quint8)));
    connect(udp, SIGNAL(haveBaudRate(quint32)), this, SLOT(receiveBaudRate(quint32)));
    // Connect for errors/alerts
    connect(udp, SIGNAL(haveNetworkError(errorType)), this, SLOT(handlePortError(errorType)));
    connect(udp, SIGNAL(haveNetworkStatus(networkStatus)), this, SLOT(handleStatusUpdate(networkStatus)));
    connect(udp, SIGNAL(haveNetworkAudioLevels(networkAudioLevels)), this, SLOT(handleNetworkAudioLevels(networkAudioLevels)));
    // Other assorted UDP connections
    connect(udp, SIGNAL(requestRadioSelection(QList<radio_cap_packet>)), this, SLOT(radioSelection(QList<radio_cap_packet>)));
    connect(udp, SIGNAL(setRadioUsage(quint8, bool, quint8, QString, QString)), this, SLOT(radioUsage(quint8, bool, quint8, QString, QString)));
    connect(this, SIGNAL(selectedRadio(quint8)), udp, SLOT(setCurrentRadio(quint8)));

    if (vsp != "None") {
        ptty = new pttyHandler(vsp,this);
        // data from the ptty to the rig:
        connect(ptty, SIGNAL(haveDataFromPort(QByteArray)), udp, SLOT(receiveDataFromUserToRig(QByteArray)));
        // data from the rig to the ptty:
        connect(udp, SIGNAL(haveDataFromPort(QByteArray)), ptty, SLOT(receiveDataFromRigToPtty(QByteArray)));

        connect(ptty, SIGNAL(havePortError(errorType)), this, SLOT(handlePortError(errorType)));
        connect(this, SIGNAL(getMoreDebug()), ptty, SLOT(debugThis()));
    }

    if (tcpPort > 0) {
        tcp = new tcpServer(this);
        tcp->startServer(tcpPort);
        // data from the tcp port to the rig:
        connect(tcp, SIGNAL(receiveData(QByteArray)), udp, SLOT(receiveDataFromUserToRig(QByteArray)));
        // data from the rig to the tcp port:
        connect(udp, SIGNAL(haveDataFromPort(QByteArray)), tcp, SLOT(sendData(QByteArray)));
    }

    emit haveAfGain(rxSetup.localAFgain);
    localVolume = rxSetup.localAFgain;

    commonSetup();

}

void icomCommander::closeComm()
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

void icomCommander::commonSetup()
{

    // common elements between the two constructors go here:
    setCIVAddr(civAddr);
    spectSeqMax = 0; // this is now set after rig ID determined

    payloadSuffix = QByteArray("\xFD");

    lookingForRig = true;
    foundRig = false;

    // Add the below commands so we can get a response until we have received rigCaps
    rigCaps.commands.clear();
    rigCaps.commandsReverse.clear();
    rigCaps.commands.insert(funcTransceiverId,funcType(funcTransceiverId, QString("Transceiver ID"),QByteArrayLiteral("\x19\x00"),0,0,false,true,false));
    rigCaps.commandsReverse.insert(QByteArrayLiteral("\x19\x00"),funcTransceiverId);

    connect(queue,SIGNAL(haveCommand(funcs,QVariant,uchar)),this,SLOT(receiveCommand(funcs,QVariant,uchar)));
    oldScopeMode = spectModeUnknown;

    pttAllowed = true; // This is for developing, set to false for "safe" debugging. Set to true for deployment.

    emit commReady();
}



void icomCommander::process()
{
    // new thread enters here. Do nothing but do check for errors.
    if(comm!=Q_NULLPTR && comm->serialError)
    {
        emit havePortError(errorType(rigSerialPort, QString("Error from commhandler. Check serial port.")));
    }
}


void icomCommander::receiveBaudRate(quint32 baudrate) {
    rigCaps.baudRate = baudrate;
    emit haveBaudRate(baudrate);
}

void icomCommander::setPTTType(pttType_t ptt)
{
    qDebug(logRig()) << "Received request to set PTT Type to:" << ptt;
    if(!usingNativeLAN)
    {
        if(comm != Q_NULLPTR)
        {
            comm->setPTTType(ptt);
        }
    }
}

void icomCommander::findRigs()
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

void icomCommander::prepDataAndSend(QByteArray data)
{    
    data.prepend(payloadPrefix);
    data.append(payloadSuffix);

    if(data[4] != '\x15')
    {
        // We don't print out requests for meter levels
        qDebug(logRigTraffic()) << "Final payload in rig commander to be sent to rig: ";
        printHexNow(data, logRigTraffic());
    }
    lastCommandToRig = data;
    emit dataForComm(data);
}

funcType icomCommander::getCommand(funcs func, QByteArray &payload, int value, uchar receiver)
{
    funcType cmd;
    // Value is set to INT_MIN by default as this should be outside any "real" values
    auto it = rigCaps.commands.find(func);
    if (it != rigCaps.commands.end())
    {
        if (value == INT_MIN || (value>=it.value().minVal && value <= it.value().maxVal))
        {
            /*
            if (value == INT_MIN)
                qDebug(logRig()) << QString("%0 with no value (get)").arg(funcString[func]);
            else
                qDebug(logRig()) << QString("%0 with value %1 (Range: %2-%3)").arg(funcString[func]).arg(value).arg(it.value().minVal).arg(it.value().maxVal);
            */
            if (rigCaps.hasCommand29 && it.value().cmd29)
            {
                // This can use cmd29 so add sub/main to the command
                payload.append('\x29');
                payload.append(static_cast<uchar>(receiver));
            } else if (!rigCaps.hasCommand29 && receiver)
            {
                // We don't have command29 so can't select sub, but if this is a scope command, let it through.
                switch (func)
                {
                case funcScopeMode: case funcScopeSpan: case funcScopeRef: case funcScopeHold:
                case funcScopeSpeed: case funcScopeRBW: case funcScopeVBW: case funcScopeCenterType: case funcScopeEdge:
                    break;
                default:
                    qDebug(logRig()) << "Rig has no Command29, removing command:" << funcString[func] << "VFO" << receiver;
                    queue->del(func,receiver);
                    break;
                }
            }
            payload.append(it.value().data);
            cmd = it.value();
        }
        else if (value != INT_MIN)
        {
            qDebug(logRig()) << QString("Value %0 for %1 is outside of allowed range (%2-%3)").arg(value).arg(funcString[func]).arg(it.value().minVal).arg(it.value().maxVal);
        }
    } else {
        // Don't try this command again as the rig doesn't support it!
        qDebug(logRig()) << "Removing unsupported command from queue" << funcString[func] << "VFO" << receiver;
        queue->del(func,receiver);
    }
    return cmd;
}

void icomCommander::powerOn()
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

    if (!usingNativeLAN || !rigCaps.hasLan) {
        for(int i=0; i < numFE; i++)
        {
            payload.append("\xFE");
        }
    }

    quint8 cmd = 0x01;
    payload.append(payloadPrefix);
    if (getCommand(funcPowerControl,payload,cmd).cmd != funcNone)
    {
        payload.append(cmd);
        payload.append(payloadSuffix); // FD
    }
    else
    {
        // We may not know the command to turn the radio on so here it is:
        payload.append("\x18\x01");
        payload.append(payloadSuffix); // FD
    }

    qDebug(logRig()) << "Power ON command in icomCommander to be sent to rig: ";
    printHex(payload);

    emit dataForComm(payload);

}

void icomCommander::powerOff()
{
    QByteArray payload;
    quint8 cmd = '\x00';
    if (getCommand(funcPowerControl,payload,cmd).cmd != funcNone)
    {
        payload.append(cmd);
        prepDataAndSend(payload);
    }
}


QByteArray icomCommander::makeFreqPayload(freqt freq,uchar numchars)
{
    QByteArray result;
    quint64 freqInt = freq.Hz;

    quint8 a;

    if (numchars == 5 && freq.Hz >= 1E10)
    {
        // Quick fix for IC905, will need to do something better eventually M0VSE
        numchars = 6;
    }

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

QByteArray icomCommander::makeFreqPayload(double freq)
{
    quint64 freqInt = (quint64) (freq * 1E6);

    QByteArray result;
    quint8 a;
    int numchars = 5;
    if (freqInt >= 1E10)
        numchars = 6;

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


QByteArray icomCommander::encodeTone(quint16 tone)
{
    return encodeTone(tone, false, false);
}

QByteArray icomCommander::encodeTone(quint16 tone, bool tinv, bool rinv)
{
    // This function is fine to use for DTCS and TONE
    QByteArray enct;

    quint8 inv=0;
    inv = inv | (quint8)rinv;
    inv = inv | ((quint8)tinv) << 4;

    enct.append(inv);

    quint8 hundreds = tone / 1000;
    quint8 tens = (tone-(hundreds*1000)) / 100;
    quint8 ones = (tone -(hundreds*1000)-(tens*100)) / 10;
    quint8 dec =  (tone -(hundreds*1000)-(tens*100)-(ones*10));

    enct.append(tens | (hundreds<<4));
    enct.append(dec | (ones <<4));

    return enct;
}


toneInfo icomCommander::decodeTone(QByteArray eTone)
{
    // index:  00 01  02 03 04
    // CTCSS:  1B 01  00 12 73 = PL 127.3, decode as 1273
    // D(T)CS: 1B 01  TR 01 23 = T/R Invert bits + DCS code 123

    toneInfo t;
    if (eTone.length() < 3) {
        return t;
    }

    if((eTone.at(0) & 0x01) == 0x01)
        t.tinv = true;
    if((eTone.at(0) & 0x10) == 0x10)
        t.rinv = true;

    t.tone += (eTone.at(2) & 0x0f);
    t.tone += ((eTone.at(2) & 0xf0) >> 4) *   10;
    t.tone += (eTone.at(1) & 0x0f) *  100;
    t.tone += ((eTone.at(1) & 0xf0) >> 4) * 1000;

    return t;
}

void icomCommander::setCIVAddr(quint8 civAddr)
{
    // Note: This sets the radio's CIV address
    // the computer's CIV address is defined in the header file.

    this->civAddr = civAddr;
    payloadPrefix = QByteArray("\xFE\xFE");
    payloadPrefix.append(civAddr);
    payloadPrefix.append((char)compCivAddr);
}

void icomCommander::handleNewData(const QByteArray& data)
{
    emit haveDataForServer(data);
    parseData(data);
}

void icomCommander::parseData(QByteArray dataInput)
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

        if((quint8)data[02] == civAddr)
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
                // We can use this to indicate power status I think
                if (!rigPoweredOn) {
                    queue->receiveValue(funcPowerControl,QVariant::fromValue<bool>(true),0);
                    rigPoweredOn = true;
                }

                break;
            case '\x00':
                // data send initiated by the rig due to user control
                // extract the payload out and parse.
                if((quint8)data[03]==compCivAddr)
                {
                    // This is an echo of our own broadcast request.
                    // The data are "to 00" and "from E1"
                    // Don't use it!
                    // We can use this to indicate power status I think.
                    if (rigPoweredOn) {
                        qDebug(logRig()) << "Echo caught:" << data.toHex(' ');
                        queue->message("Radio is available but may be powered-off");
                        queue->receiveValue(funcPowerControl,QVariant::fromValue<bool>(false),0);
                        rigPoweredOn = false;
                    }
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

void icomCommander::parseCommand()
{

#ifdef DEBUG_PARSE
    QElapsedTimer performanceTimer;
    performanceTimer.start();
#endif

    funcs func = funcNone;
    uchar receiver = 0;

    if (payloadIn.endsWith((char)0xfd))
    {
        payloadIn.chop(1);
    }

    if (rigCaps.hasCommand29 && payloadIn.at(0) == '\x29')
    {
        receiver = static_cast<uchar>(payloadIn.at(1));
        payloadIn.remove(0,2);
    }

    // As some commands bave both single and multi-byte options, start at 4 characters and work down to 1.
    // This is quite wasteful as many commands are single-byte, but I can't think of an easier way?
    int count = 0;
    for (int i=4;i>0;i--)
    {
        auto it = rigCaps.commandsReverse.find(payloadIn.left(i));
        if (it != rigCaps.commandsReverse.end())
        {
            func = it.value();
            count = i;
            break;
        }
    }

    // Remove the command so all we are left with is the data.
    payloadIn.remove(0,count);

#ifdef DEBUG_PARSE
    int currentParse=performanceTimer.nsecsElapsed();
#endif

    if (!rigCaps.commands.contains(func)) {
        // Don't warn if we haven't received rigCaps yet
        if (haveRigCaps)
            qInfo(logRig()) << "Unsupported command received from rig" <<  payloadIn.toHex().mid(0,10) << "Check rig file";
        return;
    }

    freqt test;
    QVector<memParserFormat> memParser;
    QVariant value;
    uchar vfo=0; // Used for second VFO
    switch (func)
    {
    case funcVFODualWatch:
        value.setValue(static_cast<bool>(bool(payloadIn.at(0))));
        break;
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
    case funcFreq:
        receiver = payloadIn.at(0);
        payloadIn.remove(0,1);
    case funcSelectedFreq:
    case funcUnselectedFreq:
        if (func == funcUnselectedFreq)
            vfo=1;
    case funcFreqGet:
    case funcFreqTR:
    case funcTXFreq:
    {
        value.setValue(parseFreqData(payloadIn,vfo));
        //qInfo(logRig()) << funcString[func] << "len:" << payloadIn.size() << "receiver=" << receiver << "vfo=" << vfo <<
        //    "value:" << value.value<freqt>().Hz << "data:" << payloadIn.toHex(' ');

        break;
    }
    case funcModeGet:
    case funcModeTR:
    {
        if (static_cast<quint8>(payloadIn.at(0)) == 0xff) {
            qWarning(logRig()) << "Invalid mode received, 0xff";
            return;
        }
        modeInfo m;
        if(payloadIn.size() > 1)
        {
            m.filter = payloadIn.at(1);
        } else {
            m.filter = 0;
        }
        // This command doesn't include data setting.
        m = parseMode(bcdHexToUChar(payloadIn.at(0)), 0xff, m.filter,receiver,vfo);
        //qInfo() << "Received basic mode (no data) rx:" << receiver << "vfo:" << vfo << m.name << "data:" << m.data << "filter:" << m.filter;
        value.setValue(m);
        break;
    }
    case funcMode:
        receiver=payloadIn.at(0);
    case funcSelectedMode:
    case funcUnselectedMode:
    {
        if (func == funcUnselectedMode || func == funcSelectedMode) {
            vfo=payloadIn.at(0);
        }

        // If in an invalid mode, the radio may respond with 0xff
        if (uchar(payloadIn.at(1)) != 0xff) {
            // New format payload with mode+datamode+filter
            modeInfo m = parseMode(bcdHexToUChar(payloadIn.at(1)), uchar(payloadIn.at(2)),uchar(payloadIn.at(3)),receiver,vfo);
            m.VFO = selVFO_t(receiver);
            value.setValue(m);
            //qInfo() << "Received full mode rx:" << receiver << "vfo:" << vfo << "name:"<<  m.name << "data:" << m.data << "filter:" << m.filter << payloadIn;
        }
        break;
    }

    case funcVFOBandMS:
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        break;
    case funcMemoryMode:
        qInfo(logRig) << "Memory Mode command!";
        break;
    case funcSatelliteMemory:
        memParser=rigCaps.satParser;
    case funcMemoryContents:
    {
        qDebug() << "Received mem:" << payloadIn.toHex(' ');
        memoryType mem;
        if (memParser.isEmpty()) {
             memParser=rigCaps.memParser;
             mem.sat=false;
        } else {
             mem.sat=true;
        }

        if (parseMemory(&memParser,&mem)) {
            value.setValue(mem);
        }
        break;
    }
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif
    case funcMemoryClear:
    case funcMemoryKeyer:
    case funcMemoryToVFO:
    case funcMemoryWrite:
        break;
    case funcScanning:
        break;
    case funcReadFreqOffset:
        value.setValue(parseFreqData(payloadIn,receiver));
        break;
    // These return a single byte that we convert to a uchar (0-99)
    case funcTuningStep:
    case funcAttenuator:
        value.setValue(bcdHexToUChar(payloadIn.at(0)));
        break;
    // Return a duplexMode_t for split or duplex (same function)
    case funcSplitStatus:
        value.setValue(static_cast<duplexMode_t>(uchar(payloadIn.at(0))));
        break;
    case funcQuickSplit:
        value.setValue(bcdHexToUChar(payloadIn.at(0)));
        break;
    case funcAntenna:
    {
        antennaInfo ant;
        ant.rx=false;
        ant.antenna = bcdHexToUChar(payloadIn.at(0));
        if (payloadIn.size()>1)
            ant.rx = static_cast<bool>(payloadIn.at(1));
        value.setValue(ant);
        //qInfo(logRig()) << "Got antenna" << ant.antenna << "rx" << ant.rx;
        break;

    // Register 13 (speech) has no get values
    // Register 14 (levels) starts here:
    }
    case funcAfGain:        
        if (udp == Q_NULLPTR) {
            value.setValue(bcdHexToUChar(payloadIn.at(0),payloadIn.at(1)));
        } else {
            value.setValue(localVolume);
        }
        break;
    // The following group are 2 bytes converted to uchar (0-255) but require special processing
    case funcKeySpeed: {
        uchar level = bcdHexToUChar(payloadIn.at(0),payloadIn.at(1));
        value.setValue<ushort>(round((level / 6.071) + 6));
        break;
    }
    case funcCwPitch: {
        uchar level = bcdHexToUChar(payloadIn.at(0),payloadIn.at(1));
        value.setValue<ushort>(round((((600.0 / 255.0) * level) + 300) / 5.0) * 5.0);
        break;
    }
    case funcRfGain:
    case funcSquelch:
    case funcAPFLevel:
    case funcNRLevel:
    case funcPBTInner:
    case funcPBTOuter:
    case funcIFShift:
    case funcRFPower:
    case funcMicGain:
    case funcNotchFilter:
    case funcCompressorLevel:
    case funcBreakInDelay:
    case funcNBLevel:
    case funcDigiSelShift:
    case funcDriveGain:
    case funcMonitorGain:
    case funcVoxGain:
    case funcAntiVoxGain:
    case funcBackLightLevel:
    // 0x15 Meters
    case funcSMeter:
    case funcCenterMeter:
    case funcPowerMeter:
    case funcSWRMeter:
    case funcALCMeter:
    case funcCompMeter:
    case funcVdMeter:
    case funcIdMeter:
	
	case funcBeepLevel:
	case funcBeepMain:
	case funcBeepSub:
	
	case funcRFSQLControl:
	case funcTXDelayHF:
	case funcTXDelay50m:
	case funcTimeOutTimer:
	case funcTimeOutCIV:   
	
        value.setValue(bcdHexToUChar(payloadIn.at(0),payloadIn.at(1)));
        break;
    case funcAGC:
    case funcAGCTimeConstant:
    case funcBreakIn:   // This is 0,1 or 2
    case funcPreamp:
    case funcManualNotchWidth:
    case funcSSBTXBandwidth:
    case funcDSPIFFilter:
    // Bass treble (A105)
    case funcSSBRXBass:
    case funcSSBRXTreble:
    case funcAMRXBass:
    case funcAMRXTreble:
    case funcFMRXBass:
    case funcFMRXTreble:
    case funcSSBTXBass:
    case funcSSBTXTreble:
    case funcAMTXBass:
    case funcAMTXTreble:
    case funcFMTXBass:
    case funcFMTXTreble:
    case funcBandEdgeBeep:
        value.setValue(bcdHexToUChar(payloadIn.at(0)));
        break;

    // LPF/HPF
    case funcSSBRXHPFLPF:
    case FuncAMRXHPFLPF:
    case funcFMRXHPFLPF:
    case FuncCWRXHPFLPF:
    case funcRTTYRXHPFLPF:
        value.setValue(lpfhpf(ushort(payloadIn.at(0))*100,ushort(payloadIn.at(1))*100));
        break;
    case funcAbsoluteMeter:
    {
        meterkind m;
        m.value = float(bcdHexToUInt(payloadIn.at(0),payloadIn.at(1)))/10.0;
        if (bool(payloadIn.at(2)) == true) {
            m.value=-m.value;
        }
        if (payloadIn.at(3) == 0)
            m.type=meterdBu;
        else if (payloadIn.at(3) == 1)
            m.type=meterdBuEMF;
        else if (payloadIn.at(3) == 2)
            m.type=meterdBm;
        else {
            qWarning(logRig()) << "Unknown meter type received!";
            m.type = meterNone;
        }
        value.setValue(m);
        break;
    }
    case funcMeterType:
    {
        meter_t m;
        if (payloadIn.at(0) == 0)
            m = meterS;
        else if (payloadIn.at(0) == 1)
            m = meterdBu;
        else if (payloadIn.at(0) == 2)
            m = meterdBuEMF;
        else if (payloadIn.at(0) == 3)
            m = meterdBm;
        else {
            qWarning(logRig()) << "Unknown meterType received!";
            m = meterNone;
        }
        value.setValue(m);
        break;
    }
    // The following group ALL return bool
    case funcMainSubTracking:
    case funcSatelliteMode:
    case funcNoiseBlanker:
    case funcAudioPeakFilter:
    case funcNoiseReduction:
    case funcAutoNotch:
    case funcRepeaterTone:
    case funcRepeaterTSQL:
    case funcRepeaterDTCS:
    case funcRepeaterCSQL:
    case funcCompressor:
    case funcMonitor:
    case funcVox:
    case funcManualNotch:
    case funcDigiSel:
    case funcTwinPeakFilter:
    case funcDialLock:
    case funcOverflowStatus:
    case funcSMeterSqlStatus:
    case funcVariousSql:
    case funcRXAntenna:
    case funcIPPlus:
    case funcBeepLevelLimit:
    case funcBeepConfirmation:
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        break;
    case funcToneSquelchType:
    {
        rptrAccessData r;
        r.accessMode = static_cast<rptAccessTxRx_t>(bcdHexToUChar(payloadIn.at(0)));
        r.useSecondaryVFO = static_cast<bool>(vfo);
        value.setValue(r);
        break;
    }
    // 0x17 is CW send and 0x18 is power control (no reply)
    // 0x19 it automatically added.
    case funcTransceiverId:
        value.setValue(static_cast<uchar>(payloadIn.at(0)));
        if (!rigCaps.modelID || quint8(payloadIn.at(0)) != rigCaps.modelID)
        {
            if (rigList.contains(uchar(payloadIn.at(0))))
            {
                this->model = rigList.find(uchar(payloadIn.at(0))).key();
            }
            rigCaps.modelID = payloadIn.at(0);
            qInfo(logRig()) << QString("Have new rig ID: 0x%0").arg(rigCaps.modelID,1,16);
            determineRigCaps();
        }
        break;
    // 0x1a
    case funcBandStackReg:
    {
        bandStackType bsr;
        bsr.band = bcdHexToUChar(payloadIn.at(0));
        bsr.regCode = bcdHexToUChar(payloadIn.at(1));
        for (const auto &b: rigCaps.bands)
        {
            if (b.bsr == bsr.band)
            {
                bsr.freq = parseFreqData(payloadIn.mid(2,b.bytes),receiver);
                // The Band Stacking command returns the regCode in the position that VFO is expected.
                // As BSR is always on the active VFO, just set that.
                bsr.freq.VFO = selVFO_t::activeVFO;
                bsr.mode = bcdHexToUChar(payloadIn.at(b.bytes+2));
                bsr.filter = bcdHexToUChar(payloadIn.at(b.bytes+3));
                bsr.data = (payloadIn.at(b.bytes+4) & 0xf0) >> 4;
                bsr.sql = (payloadIn.at(b.bytes+4) & 0x0f);
                if (payloadIn.length()>b.bytes+10) {
                    bsr.tone = decodeTone(payloadIn.mid(b.bytes+5,3));
                    bsr.tsql = decodeTone(payloadIn.mid(b.bytes+8,3));
                }
                qDebug(logRig()) << QString("BSR received, band:%0 code:%1 freq:%2 data:%3 mode:%4 filter:%5")
                                        .arg(bsr.band).arg(bsr.regCode).arg(bsr.freq.Hz).arg(bsr.data).arg(bsr.mode).arg(bsr.filter);
                value.setValue(bsr);
                break;
            }

        }
        if (!value.isValid()) {
            qWarning(logRig()) << "Unknown BSR received, check rig file:" << payloadIn.toHex(' ');
        } else {
            qInfo(logRig()) << "BSR received:" << payloadIn.toHex(' ');
        }
        break;
    }
    case funcFilterWidth:
    {
        quint16 calc;
        quint8 pass = bcdHexToUChar((quint8)payloadIn.at(0));
        modeInfo m = queue->getCache(rigCaps.commands.contains(funcSelectedMode)?funcSelectedMode:funcMode,receiver).value.value<modeInfo>();

        if (m.mk == modeAM)
        {
             calc = 200 + (pass * 200);
        }
        else if (pass <= 10)
        {
             calc = 50 + (pass * 50);
        }
        else {
             calc = 600 + ((pass - 10) * 100);
        }
        value.setValue(calc);
        //qInfo() << "Got filter width" << calc << "VFO" << receiver;
        break;
    }
    case funcDataModeWithFilter:
    {
        modeInfo m;
        // New format payload with mode+datamode+filter
        m = parseMode(0xff, bcdHexToUChar(payloadIn.at(0)),bcdHexToUChar(payloadIn.at(1)),receiver,vfo);
        m.VFO = selVFO_t(receiver & 0x01);
        value.setValue(m);
        break;
    }
    case funcAFMute:
        // TODO add AF Mute
        break;
    // 0x1a 0x05 various registers below are 2 byte (0-255) as uchar
    case funcREFAdjust:
    case funcREFAdjustFine:
    case funcACCAModLevel:
    case funcACCBModLevel:
    case funcUSBModLevel:
    case funcLANModLevel:
    case funcSPDIFModLevel:
    case funcNBWidth:
        value.setValue(bcdHexToUChar(payloadIn.at(0),payloadIn.at(1)));
        break;
    // Singla byte returned as uchar (0-99)
    case funcDATAOffMod:
    case funcDATA1Mod:
    case funcDATA2Mod:
    case funcDATA3Mod:
    {
        for (auto &r: rigCaps.inputs)
        {
            if (r.reg == bcdHexToUChar(payloadIn.at(0)))
            {
                value.setValue(r);
                break;
            }
        }
        break;
    }
    case funcDashRatio:
    case funcNBDepth:
    case funcVOXDelay:
        value.setValue(bcdHexToUChar(payloadIn.at(0)));
        break;
    // Fixed Freq Scope Edges
    case funcScopeEdge1a:
    case funcScopeEdge2a:
    case funcScopeEdge3a:
    case funcScopeEdge4a:
    case funcScopeEdge1b:
    case funcScopeEdge2b:
    case funcScopeEdge3b:
    case funcScopeEdge4b:
    case funcScopeEdge1c:
    case funcScopeEdge2c:
    case funcScopeEdge3c:
    case funcScopeEdge4c:
    case funcScopeEdge1d:
    case funcScopeEdge2d:
    case funcScopeEdge3d:
    case funcScopeEdge4d:
    case funcScopeEdge1e:
    case funcScopeEdge2e:
    case funcScopeEdge3e:
    case funcScopeEdge4e:
    case funcScopeEdge1f:
    case funcScopeEdge2f:
    case funcScopeEdge3f:
    case funcScopeEdge4f:
    case funcScopeEdge1g:
    case funcScopeEdge2g:
    case funcScopeEdge3g:
    case funcScopeEdge4g:
    case funcScopeEdge1h:
    case funcScopeEdge2h:
    case funcScopeEdge3h:
    case funcScopeEdge4h:
    case funcScopeEdge1i:
    case funcScopeEdge2i:
    case funcScopeEdge3i:
    case funcScopeEdge4i:
    case funcScopeEdge1j:
    case funcScopeEdge2j:
    case funcScopeEdge3j:
    case funcScopeEdge4j:
    case funcScopeEdge1k:
    case funcScopeEdge2k:
    case funcScopeEdge3k:
    case funcScopeEdge4k:
    case funcScopeEdge1l:
    case funcScopeEdge2l:
    case funcScopeEdge3l:
    case funcScopeEdge4l:
    case funcScopeEdge1m:
    case funcScopeEdge2m:
    case funcScopeEdge3m:
    case funcScopeEdge4m:
    case funcScopeEdge1n:
    case funcScopeEdge2n:
    case funcScopeEdge3n:
    case funcScopeEdge4n:
    case funcScopeEdge1o:
    case funcScopeEdge2o:
    case funcScopeEdge3o:
    case funcScopeEdge4o:
    case funcScopeEdge1p:
    case funcScopeEdge2p:
    case funcScopeEdge3p:
    case funcScopeEdge4p:
    case funcScopeEdge1q:
    case funcScopeEdge2q:
    case funcScopeEdge3q:
    case funcScopeEdge4q:
    case funcScopeEdge1r:
    case funcScopeEdge2r:
    case funcScopeEdge3r:
    case funcScopeEdge4r:
    case funcScopeEdge1s:
    case funcScopeEdge2s:
    case funcScopeEdge3s:
    case funcScopeEdge4s:
        break;
    // 0x1b register (tones)
    case funcToneFreq:
    case funcTSQLFreq:
    case funcDTCSCode:
    case funcCSQLCode:
        value.setValue(decodeTone(payloadIn));
        break;
    // 0x1c register (bool)
    case funcRitStatus:
    case funcTransceiverStatus:
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        break;
    // tuner is 0-2
    case funcTunerStatus:
        value.setValue(bcdHexToUChar(payloadIn.at(0)));
        break;
    // 0x21 RIT:
    case funcRITFreq:
    {
        /* M0VSE DOES THIS NEEED FIXING? */
        short ritHz = 0;
        freqt f;
        QByteArray longfreq;
        longfreq = payloadIn.mid(2,2);
        longfreq.append(QByteArray(3,'\x00'));
        f = parseFrequency(longfreq, 3);
        if(payloadIn.length() < 5)
             break;
        ritHz = f.Hz*((payloadIn.at(4)=='\x01')?-1:1);
        value.setValue(ritHz);
        break;
    }
    case funcRitTXStatus:
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        break;
    case funcTXFreqMon:
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        break;
    // 0x27
    case funcScopeWaveData:
    {
        receiver=payloadIn.at(0);
        payloadIn.remove(0,1);
        scopeData d;
        if (parseSpectrum(d,receiver))
            value.setValue(d);
        break;
    }
    case funcScopeOnOff:
        // confirming scope is on
    case funcScopeDataOutput:
        // confirming output enabled/disabled of wf data.
    case funcScopeMainSub:
        // This tells us whether we are receiving main or sub data
    case funcScopeSingleDual:
        // This tells us whether we are receiving single or dual scopes
        //qInfo(logRig()) << "funcScopeSingleDual (" << receiver <<") " << static_cast<bool>(payloadIn.at(0));
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        break;
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
    case funcScopeMode:
        // fixed or center
        // [1] 0x14
        // [2] 0x00
        // [3] 0x00 (center), 0x01 (fixed), 0x02, 0x03
        receiver = payloadIn.at(0);
        value.setValue(static_cast<spectrumMode_t>(uchar(payloadIn.at(1))));
        break;
    case funcScopeSpan:
    {
        receiver = payloadIn.at(0);
        payloadIn.remove(0,1);
        freqt f = parseFrequency(payloadIn, 3);
        for (auto &s: rigCaps.scopeCenterSpans)
        {
            if (s.freq == f.Hz)
            {
                value.setValue(s);
            }
        }
        break;
    }
    case funcScopeEdge:
        // read edge mode center in edge mode
        // [1] 0x16
        // [2] 0x01, 0x02, 0x03: Edge 1,2,3
        receiver=payloadIn.at(0);
        value.setValue(bcdHexToUChar(payloadIn.at(1)));
        break;
    case funcBandEdgeFreq:
        // M0VSE add this
        break;
    case funcScopeHold:
        receiver=payloadIn.at(0);
        value.setValue(static_cast<bool>(payloadIn.at(1)));
        break;
    case funcScopeRef:
    {
        receiver=payloadIn.at(0);
        // scope reference level
        // [1] 0x19
        // [2] 0x00
        // [3] 10dB digit, 1dB digit
        // [4] 0.1dB digit, 0
        // [5] 0x00 = +, 0x01 = -
        quint8 negative = payloadIn.at(3);
        short ref = bcdHexToUInt(payloadIn.at(1), payloadIn.at(2));
        ref = ref / 10;
        if(negative){
             ref *= (-1*negative);
        }
        value.setValue(ref);
        break;
    }
    case funcScopeSpeed:
        receiver=payloadIn.at(0);
        value.setValue(static_cast<uchar>(payloadIn.at(1)));
        break;
    case funcScopeVBW:
        receiver=payloadIn.at(0);
        break;
    case funcScopeRBW:
        receiver=payloadIn.at(0);
        break;
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif
    case funcScopeFixedEdgeFreq:
    case funcScopeDuringTX:
    case funcScopeCenterType:
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
    {
        if (!lastCommandToRig.isEmpty()) {
            if (!warnedAboutFA) {
                qInfo(logRig()) << "Occasional error response (FA) from rig can safely be ignored";
                 warnedAboutFA=true;
            }
            qWarning(logRig()) << "Error (FA) received from rig, last command sent:";

            QStringList messages = getHexArray(lastCommandToRig);
            for (const auto &msg: messages)
                qWarning(logRig()) << msg;
            }
        break;
    }
    default:
        qWarning(logRig()).noquote() << "Unhandled command received from rig:" << funcString[func] << "value:" << payloadIn.toHex().mid(0,10);
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

    if (value.isValid() && queue != Q_NULLPTR) {
        queue->receiveValue(func,value,receiver);
    }

}

void icomCommander::determineRigCaps()
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
                                QByteArray::fromHex(settings->value("String", "").toString().toUtf8()),
                                                       settings->value("Min", 0).toInt(NULL), settings->value("Max", 0).toInt(NULL),
                                                       settings->value("Command29",false).toBool(),
                                                       settings->value("GetCommand",true).toBool(),
                                                       settings->value("SetCommand",true).toBool()));

                            rigCaps.commandsReverse.insert(QByteArray::fromHex(settings->value("String", "").toString().toUtf8()),func);
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
                settings->value("Reg", 0).toString().toUInt(), settings->value("Name", "").toString(), settings->value("Min", 0).toInt(), settings->value("Max", 0).toInt()));
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
                settings->value("Reg", 0).toString().toUInt(),settings->value("Name", "").toString()));
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

    haveRigCaps = true;
    queue->setRigCaps(&rigCaps);

    // Copy received guid so we can recognise this radio.
    memcpy(rigCaps.guid, this->guid, GUIDLEN);

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

bool icomCommander::parseSpectrum(scopeData& d, uchar receiver)
{
    bool ret = false;

    if(!haveRigCaps)
    {
        qDebug(logRig()) << "Spectrum received in icomCommander, but rigID is incomplete.";
        return ret;
    }
    if(rigCaps.spectSeqMax == 0)
    {
        // there is a chance this will happen with rigs that support spectrum. Once our RigID query returns, we will parse correctly.
        qInfo(logRig()) << "Warning: Spectrum sequence max was zero, yet spectrum was received.";
        return ret;
    }

    if (receiver)
        d = subScopeData;
    else
        d = mainScopeData;

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

    d.receiver = receiver;
    quint8 sequence = bcdHexToUChar(payloadIn.at(0));
    quint8 sequenceMax = bcdHexToUChar(payloadIn.at(1));

    int freqLen = 5;
    // M0VSE THIS SHOULD BE FIXED, BUT NOT SURE HOW AS WE DON'T KNOW WHICH BAND WE ARE ON?
    // On the IC-905 10GHz+ uses 6 bytes for freq!
    if (rigCaps.modelID == 0xAC && (payloadIn.size()==491 || payloadIn.size() == 16)) {
        freqLen = 6;
    }

    // Sequnce 2, index 05 is the start of data
    // Sequence 11. index 05, is the last chunk
    // Sequence 11, index 29, is the actual last pixel (it seems)

    // It looks like the data length may be variable, so we need to detect it each time.
    // start at payloadIn.length()-1 (to override the FD). Never mind, index -1 bad.
    // chop off FD.
    if (sequence == 1)
    {
        // This should work on Qt5, but I need to test, use the switch below instead for now.
        //d.mode = static_cast<spectrumMode_t>(payloadIn.at(2)); // 0=center, 1=fixed

        switch (payloadIn.at(2))
        {
        case 0:
            d.mode = spectrumMode_t::spectModeCenter;
            break;
        case 1:
            d.mode = spectrumMode_t::spectModeFixed;
            break;
        case 2:
            d.mode = spectrumMode_t::spectModeScrollC;
            break;
        case 3:
            d.mode = spectrumMode_t::spectModeScrollF;
            break;
        default:
            d.mode = spectrumMode_t::spectModeUnknown;
            break;
        }

        if(d.mode != oldScopeMode)
        {
            // Modes:
            // 0x00 Center
            // 0x01 Fixed
            // 0x02 Scroll-C
            // 0x03 Scroll-F
            oldScopeMode = d.mode;
        }

        d.oor=(bool)payloadIn[3+(freqLen*2)];
        if (d.oor) {
            d.data = QByteArray(rigCaps.spectLenMax,'\0');
            d.valid=true;
            return true;
        }

        // clear wave information
        d.data.clear();

        // For Fixed, and both scroll modes, the following produces correct information:
        fStart = parseFreqData(payloadIn.mid(3,freqLen),receiver);
        d.startFreq = fStart.MHzDouble;
        fEnd = parseFreqData(payloadIn.mid(3+freqLen,freqLen),receiver);
        d.endFreq = fEnd.MHzDouble;

        if(d.mode == spectModeCenter)
        {
            // "center" mode, start is actual center, end is bandwidth.
            d.startFreq -= d.endFreq;
            d.endFreq = d.startFreq + 2*(d.endFreq);
        }

        if (sequence == sequenceMax) // Must be a LAN packet.
        {
            d.data.append(payloadIn.right(payloadIn.length()-4-(freqLen*2)));
            ret = true;
        }

        //qInfo(logRig()) << "Spectrum Data received start:" << d.startFreq << "end:" << d.endFreq << "seq:" << sequence << "/" << sequenceMax << "mode:" << d.mode << "oor" << d.oor << "scopelen:" << d.data.size() << "length:" << payloadIn.length();
    }
    else if ((sequence > 1) && (sequence < sequenceMax))
    {
        // spectrum from index 05 to index 54, length is 55 per segment. Length is 56 total. Pixel data is 50 pixels.
        // sequence numbers 2 through 10, 50 pixels each. Total after sequence 10 is 450 pixels.
        d.data.insert(d.data.length(), payloadIn.right(payloadIn.length() - 2));
        ret = false;
        //qInfo(logRig()) << "sequence: " << sequence << "spec index: " << (sequence-2)*55 << " payloadPosition: " << payloadIn.length() - 2 << " payload length: " << payloadIn.length();
    } else if (sequence == sequenceMax)
    {
        // last spectrum, a little bit different (last 25 pixels). Total at end is 475 pixels (7300).
        d.data.insert(d.data.length(), payloadIn.right(payloadIn.length() - 2));
        ret = true;
        //qInfo(logRig()) << "sequence: " << sequence << " spec index: " << (sequence-2)*55 << " payloadPosition: " << payloadIn.length() - 2 << " payload length: " << payloadIn.length();
    }
    d.valid=ret;

    if (!ret) {
        // We need to temporarilly store the scope data somewhere.
        if (receiver)
            subScopeData = d;
        else
            mainScopeData = d;
    }
    return ret;
}

quint8 icomCommander::bcdHexToUChar(quint8 in)
{
    quint8 out = 0;
    out = in & 0x0f;
    out += ((in & 0xf0) >> 4)*10;
    return out;
}

unsigned int icomCommander::bcdHexToUInt(quint8 hundreds, quint8 tensunits)
{
    // convert:
    // hex data: 0x41 0x23
    // convert to uint:
    // uchar: 4123
    quint8 thousands = ((hundreds & 0xf0)>>4);
    unsigned int rtnVal;
    rtnVal = (hundreds & 0x0f)*100;
    rtnVal += ((tensunits & 0xf0)>>4)*10;
    rtnVal += (tensunits & 0x0f);
    rtnVal += thousands * 1000;

    return rtnVal;
}

unsigned int icomCommander::bcdHexToUInt(quint8 tenthou, quint8 hundreds, quint8 tensunits)
{
    // convert:
    // hex data: 0x41 0x23
    // convert to uint:
    // uchar: 4123
    quint8 thousands = ((hundreds & 0xf0)>>4);
    unsigned int rtnVal;
    rtnVal = (hundreds & 0x0f)*100;
    rtnVal += ((tensunits & 0xf0)>>4)*10;
    rtnVal += (tensunits & 0x0f);
    rtnVal += thousands * 1000;
    rtnVal += (tenthou & 0x0f) * 10000;
    return rtnVal;
}
quint8 icomCommander::bcdHexToUChar(quint8 hundreds, quint8 tensunits)
{
    // convert:
    // hex data: 0x01 0x23
    // convert to uchar:
    // uchar: 123

    quint8 rtnVal;
    rtnVal = (hundreds & 0x0f)*100;
    rtnVal += ((tensunits & 0xf0)>>4)*10;
    rtnVal += (tensunits & 0x0f);

    return rtnVal;
}

QByteArray icomCommander::bcdEncodeInt(quint16 num)
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

    QByteArray result;
    result.append(b0).append(b1);
    return result;
}



QByteArray icomCommander::bcdEncodeInt(unsigned int num)
{
    if(num > 999999)
    {
        qInfo(logRig()) << __FUNCTION__ << "Error, number is too big for six-digit conversion: " << num;
        return QByteArray();
    }

    char tenthou = num / 10000;
    char thousands = (num - (10000*tenthou)) / 1000;
    char hundreds = (num - (10000*tenthou) - (1000*thousands)) / 100;
    char tens = (num - (10000*tenthou) - (1000*thousands) - (100*hundreds)) / 10;
    char units = (num - (10000*tenthou) - (1000*thousands) - (100*hundreds) - (10*tens));

    char b0 = tenthou;
    char b1 = hundreds | (thousands << 4);
    char b2 = units | (tens << 4);

    QByteArray result;
    result.append(b0).append(b1).append(b2);
    qInfo(logRig()) << __FUNCTION__ << " encoding value " << num << " as hex:" << result.toHex(' ');

    return result;
}
QByteArray icomCommander::bcdEncodeChar(quint8 num)
{
    if(num > 99)
    {
        qInfo(logRig()) << __FUNCTION__ << "Error, number is too big for two-digit conversion: " << num;
        return QByteArray();
    }

    uchar tens = num / 10;
    uchar units = num - (10*tens);

    uchar b0 = units | (tens << 4);

    //qInfo(logRig()) << __FUNCTION__ << " encoding value " << num << " as hex:";
    //printHex(QByteArray(b0), false, true);
    //printHex(QByteArray(b1), false, true);

    QByteArray result;
    result.append(b0);
    return result;
}



freqt icomCommander::parseFrequency()
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

    return freq;
}

freqt icomCommander::parseFrequencyRptOffset(QByteArray data)
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

    f.MHzDouble=f.Hz/1E6;
    f.VFO = activeVFO;
    return f;
}

freqt icomCommander::parseFrequency(QByteArray data, quint8 lastPosition)
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

    if (data.length() <= lastPosition)
    {
        // Something bad has happened!
        qWarning(logRig()) << "parseFrequency() given last position:" << lastPosition << "but data is only" << data.length() << "bytes";
        return freqs;
    }
    // Does Frequency contain 100 MHz/1 GHz data?
    if(data.length() > lastPosition+2)
    {
        freqs.Hz += (data[lastPosition+2] & 0x0f) * 1E9; //  1 GHz
        freqs.Hz += ((data[lastPosition+2] & 0xf0) >> 4) * 1E9 * 10; // 10 GHz
    }
    if(data.length() > lastPosition+1)
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


freqt icomCommander::parseFreqData(QByteArray data, uchar receiver)
{
    freqt freq;
    freq.Hz = parseFreqDataToInt(data);
    freq.MHzDouble = freq.Hz/1000000.0;
    freq.VFO = selVFO_t(receiver);
    //qInfo(logRig()) << "Received Frequency" << freq.Hz << "vfo" << receiver;
    return freq;
}

quint64 icomCommander::parseFreqDataToInt(QByteArray data)
{
    // Allow raw data to be parsed. Use a lookup table (pow10) for speed
    // Should support VERY large or VERY small numbers!
    quint64 val=0;

    for (int i=0;i<data.size()*2;i=i+2)
    {
        val += (data[i/2] & 0x0f) * pow10[i];
        val += ((data[i/2] & 0xf0) >> 4) * (pow10[i+1]);
    }

    return val;
}


modeInfo icomCommander::parseMode(uchar mode, uchar data, uchar filter, uchar receiver,uchar vfo)
{
    modeInfo mi;
    bool found=false;
    if (mode == 0xff)
    {
        mi.reg=0xff;
        mi.mk=modeUnknown;
        mi.filter=filter;
        mi.data=data;
        found = true;
    }
    else
    {
        for (auto& m: rigCaps.modes)
        {
            if (m.reg == mode)
            {
                mi = modeInfo(m);
                mi.filter = filter;
                mi.data = data;
                found = true;
                break;
            }
        }
    }

    if (!found) {
        qInfo(logRig()) << QString("parseMode() No such mode %0 with filter %1").arg(mode).arg(filter) << payloadIn.toHex(' ') ;
    }

    // We cannot query sub VFO width without command29.
    if (!rigCaps.hasCommand29)
        receiver = 0;


    cacheItem item;

    // Does the current mode support filterwidth?
    // Cannot get the filterwidth of the second VFO, so use the default values.
    if (vfo == 0 && mi.bwMin >0  && mi.bwMax > 0) {
        item = queue->getCache(funcFilterWidth,receiver);
    }

    if (item.value.isValid()) {
        mi.pass = item.value.toInt();
    }
    else
    {

        /*  We haven't got a valid passband from the rig yet so we
            need to create a 'fake' one from default values
            This will be replaced with a valid one if we get it

            Also use default widths for modes that have no control*/

        if (mi.mk == modeCW || mi.mk == modeCW_R || mi.mk == modePSK || mi.mk == modePSK_R) {
            switch (filter) {
            case 1:
                mi.pass=1200;
                break;
            case 2:
                mi.pass=500;
                break;
            case 3:
                mi.pass=250;
                break;
            }
        }
        else if (mi.mk == modeRTTY || mi.mk == modeRTTY_R)
        {
            switch (filter) {
            case 1:
                mi.pass=2400;
                break;
            case 2:
                mi.pass=500;
                break;
            case 3:
                mi.pass=250;
                break;
            }
        }
        else if (mi.mk == modeAM)
        {
            switch (filter) {
            case 1:
                mi.pass=9000;
                break;
            case 2:
                mi.pass=6000;
                break;
            case 3:
                mi.pass=3000;
                break;
            }
        }
        else if (mi.mk == modeFM)
        {
            switch (filter) {
            case 1:
                mi.pass=15000;
                break;
            case 2:
                mi.pass=10000;
                break;
            case 3:
                mi.pass=7000;
                break;
            }
        }
        else if (mi.mk == modeWFM)
        {
            mi.pass=200000;
        }
        else { // SSB or unknown mode
            switch (filter) {
            case 1:
                mi.pass=3000;
                break;
            case 2:
                mi.pass=2400;
                break;
            case 3:
                mi.pass=1800;
                break;
            }
        }
    }

    //qInfo() << "Mode" << mi.name << "Passband" << mi.pass;
    return mi;
}

bool icomCommander::parseMemory(QVector<memParserFormat>* memParser, memoryType* mem)
{
    // Parse the memory entry into a memoryType, set some defaults so we don't get an unitialized warning.
    mem->frequency.Hz=0;
    mem->frequency.VFO=activeVFO;
    mem->frequency.MHzDouble=0.0;
    mem->frequencyB = mem->frequency;
    mem->duplexOffset = mem->frequency;
    mem->duplexOffsetB = mem->frequency;
    mem->scan = 0xfe;
    memset(mem->UR, 0x0, sizeof(mem->UR));
    memset(mem->URB, 0x0, sizeof(mem->UR));
    memset(mem->R1, 0x0, sizeof(mem->R1));
    memset(mem->R1B, 0x0, sizeof(mem->R1B));
    memset(mem->R2, 0x0, sizeof(mem->R2));
    memset(mem->R2B, 0x0, sizeof(mem->R2B));
    memset(mem->name, 0x0, sizeof(mem->name));
    // We need to add 2 characters so that the parser works!
    payloadIn.insert(0,"**");
    for (auto &parse: *memParser) {
        // non-existant memory is short so send what we have so far.
        if (payloadIn.size() < (parse.pos+1+parse.len) && parse.spec != 'Z') {
            return true;
        }
        QByteArray data = payloadIn.mid(parse.pos+1,parse.len);
        //qInfo(logRig()) << "Parse:" << data.toHex() << "pos" << parse.pos;
        switch (parse.spec)
        {
        case 'a':
            if (parse.len == 1) {
                mem->group = bcdHexToUChar(data[0]);
            }
            else
            {
                mem->group = bcdHexToUChar(data[0],data[1]);
            }
            break;
        case 'b':
            mem->channel = bcdHexToUChar(data[0],data[1]);
            break;
        case 'c':
            mem->scan = data[0];
            break;
        case 'C':
            mem->skip = data[0] >> 4 & 0xf;
            mem->scan = data[0] & 0xf;
            break;
        case 'd': // combined split and scan
            mem->split = quint8(data[0] >> 4 & 0x0f);
            mem->scan = quint8(data[0] & 0x0f);
            break;
        case 'D': // duplex only (used for IC-R8600)
            mem->duplex = quint8(data[0] & 0x0f);
            break;
        case 'e':
            mem->vfo=data[0];
            break;
        case 'E':
            mem->vfoB=data[0];
            break;
        case 'f':
            mem->frequency.Hz = parseFreqDataToInt(data);
            break;
        case 'F':
            mem->frequencyB.Hz = parseFreqDataToInt(data);
            break;
        case 'g':
            mem->mode=bcdHexToUChar(data[0]);
            break;
        case 'G':
            mem->modeB=bcdHexToUChar(data[0]);
            break;
        case 'h':
            mem->filter=bcdHexToUChar(data[0]);
            break;
        case 'H':
            mem->filterB=bcdHexToUChar(data[0]);
            break;
        case 'i': // single datamode
            mem->datamode=bcdHexToUChar(data[0]);
            break;
        case 'I': // single datamode
            mem->datamodeB=bcdHexToUChar(data[0]);
            break;
        case 'j': // combined duplex and tonemode
            mem->duplex=duplexMode_t(quint8(data[0] >> 4 & 0x0f));
            mem->tonemode=quint8(quint8(data[0] & 0x0f));
            break;
        case 'J': // combined duplex and tonemodeB
            mem->duplexB=duplexMode_t((data[0] >> 4 & 0x0f));
            mem->tonemodeB=data[0] & 0x0f;
            break;
        case 'k': // combined datamode and tonemode
            mem->datamode=(quint8(data[0] >> 4 & 0x0f));
            mem->tonemode=data[0] & 0x0f;
            break;
        case 'K': // combined datamode and tonemode
            mem->datamodeB=(quint8(data[0] >> 4 & 0x0f));
            mem->tonemodeB=data[0] & 0x0f;
            break;
        case 'l': // tonemode
            mem->tonemode=data[0] & 0x0f;
            break;
        case 'L': // tonemode
            mem->tonemodeB=data[0] & 0x0f;
            break;
        case 'm':
            mem->dsql = (quint8(data[0] >> 4 & 0x0f));
            break;
        case 'M':
            mem->dsqlB = (quint8(data[0] >> 4 & 0x0f));
            break;
        case 'n':            
            mem->tone = bcdHexToUInt(data[1],data[2]); // First byte is not used
            break;
        case 'N':
            mem->toneB = bcdHexToUInt(data[1],data[2]); // First byte is not used
            break;
        case 'o':
            mem->tsql = bcdHexToUInt(data[1],data[2]); // First byte is not used
            break;
        case 'O':
            mem->tsqlB = bcdHexToUInt(data[1],data[2]); // First byte is not used
            break;
        case 'p':
            mem->dtcsp = (quint8(data[0] >> 3 & 0x02) | quint8(data[0] & 0x01));
            break;
        case 'P':
            mem->dtcspB = (quint8(data[0] >> 3 & 0x10) | quint8(data[0] & 0x01));
            break;
        case 'q':
            mem->dtcs = bcdHexToUInt(data[0],data[1]);
            break;
        case 'Q':
            mem->dtcsB = bcdHexToUInt(data[0],data[1]);
            break;
        case 'r':
            mem->dvsql = bcdHexToUChar(data[0]);
            break;
        case 'R':
            mem->dvsqlB = bcdHexToUChar(data[0]);
            break;
        case 's':
            mem->duplexOffset.Hz = parseFreqDataToInt(data);
            break;
        case 'S':
            mem->duplexOffsetB.Hz = parseFreqDataToInt(data);
            break;
        case 't':
            memcpy(mem->UR,data.data(),qMin(int(sizeof mem->UR),data.size()));
            break;
        case 'T':
            memcpy(mem->URB,data.data(),qMin(int(sizeof mem->URB),data.size()));
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
        case 'w': // Tuning step
            if (bool(data[0])) { // Only set if enabled.
                mem->tuningStep = bcdHexToUChar(data[1]);
                mem->progTs = bcdHexToUInt(data[2],data[3]);
            } else {
                mem->tuningStep = 0;
                mem->progTs = 5;
            }
            break;
        case 'x': // Attenuator & Preamp
            mem->atten = bcdHexToUChar(data[0]);
            mem->preamp = bcdHexToUChar(data[1]);
            break;
        case 'y': // Antenna
            mem->antenna = bcdHexToUChar(data[0]);
            break;
        case '+': // IP Plus
            mem->ipplus=bool(data[0] & 0x0f);
            break;
        case 'z':
            if (mem->scan == 0xfe)
                mem->scan = 0;
            memcpy(mem->name,data.data(),qMin(int(sizeof mem->name),data.size()));
            break;
        case 'Z': // Special mode dependant features (I have no idea how to make these work!)
            for (const auto &m: rigCaps.modes)
            {
                if (m.reg == mem->mode)
                {
                    // This mode is the one we are interested in!
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
                    switch (m.mk)
                    {
                    case modeFM:
                        mem->tonemode=data[0] & 0x0f;
                        mem->tsql = bcdHexToUInt(data[2],data[3]); // First byte is not used
                        mem->dtcsp = quint8(data[4] & 0x0f);
                        mem->dtcs = bcdHexToUInt(data[5],data[6]);
                        break;
                    case modeDV:
                        mem->dsql = (quint8(data[0] & 0x0f));
                        mem->dvsql = bcdHexToUChar(data[1]);
                        break;
                    case modeP25:
                        mem->p25Sql = bool(data[0]&0x01);
                        mem->p25Nac = quint16((quint16(data[1] & 0x0f) << 8) | (quint16(data[2]&0x0f) << 4) | quint16(data[3]&0x0f));
                        break;
                    case modedPMR:
                        mem->dPmrSql = bcdHexToUChar(data[0]);
                        mem->dPmrComid = bcdHexToUInt(data[1],data[2]);
                        mem->dPmrCc = bcdHexToUChar(data[3]);
                        mem->dPmrSCRM = bool(data[4]&0x01);
                        mem->dPmrKey = bcdHexToUInt(data[5],data[6],data[7]);
                        break;
                    case modeNXDN_N:
                    case modeNXDN_VN:
                        mem->nxdnSql = bool(data[0]&0x01);
                        mem->nxdnRan = bcdHexToUChar(data[1]);
                        mem->nxdnEnc = bool(data[2]&0x01);
                        mem->nxdnKey = bcdHexToUInt(data[3],data[4],data[5]);
                        break;
                    case modeDCR:
                        mem->dcrSql = bool(data[0]&0x01);
                        mem->dcrUc = bcdHexToUInt(data[1],data[2]);
                        mem->dcrEnc = bool(data[3]&0x01);
                        mem->dcrKey = bcdHexToUInt(data[4],data[5],data[6]);
                        break;
                    default:
                        break;
                    }
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif


                    break;
                }
            }

            break;
        default:
            qInfo() << "Parser didn't match!" << "spec:" << parse.spec << "pos:" << parse.pos << "len" << parse.len;
            break;
        }
    }

    return true;
}

void icomCommander::getRigID()
{
    QByteArray payload;
    if (getCommand(funcTransceiverId,payload).cmd != funcNone)
    {
        prepDataAndSend(payload);
    } else {
        // If we haven't got this command yet, need to use the default one!
        QByteArray payload = "\x19\x00";
        prepDataAndSend(payload);
    }
}

void icomCommander::setRigID(quint8 rigID)
{
    // This function overrides radio model detection.
    // It can be used for radios without Rig ID commands,
    // or to force a specific radio model

    qInfo(logRig()).noquote() << QString("Setting rig ID to: 0x%d").arg(rigID,1,16);

    lookingForRig = true;
    foundRig = false;

    // needed because this is a fake message and thus the value is uninitialized
    // this->civAddr comes from how icomCommander is setup and should be accurate.
    this->incomingCIVAddr = this->civAddr;

    if (rigList.contains(rigID)) this->model = rigID;
    rigCaps.modelID = rigID;
    rigCaps.model = this->model;
    determineRigCaps();
}


void icomCommander::setAfGain(quint8 level)
{
    if (udp == Q_NULLPTR)
    {
        QByteArray payload;
        if (getCommand(funcAfGain,payload,level).cmd != funcNone)
        {
            payload.append(bcdEncodeInt(quint16(level)));
            prepDataAndSend(payload);
        }
    }
    else {
        emit haveSetVolume(level);
        localVolume = level;
    }
}

uchar icomCommander::makeFilterWidth(ushort pass,uchar receiver)
{
    quint8 calc;
    modeInfo mi = queue->getCache(rigCaps.commands.contains(funcSelectedMode)?funcSelectedMode:funcMode,receiver).value.value<modeInfo>();
    if (mi.mk == modeAM) { // AM 0-49

        calc = quint16((pass / 200) - 1);
        if (calc > 49)
            calc = 49;
    }
    else if (pass >= 600) // SSB/CW/PSK 10-40 (10-31 for RTTY)
    {
        calc = quint16((pass / 100) + 4);
        if (((calc > 31) && (mi.mk == modeRTTY || mi.mk == modeRTTY_R)))
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

    //qDebug(logRig()) << "Requested filter width" << pass << "sending value" << uchar(b1);
    return b1;
}

unsigned char icomCommander::convertNumberToHex(unsigned char num)
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
    return result;
}

void icomCommander::receiveCommand(funcs func, QVariant value, uchar receiver)
{
    //qInfo() << "Got command:" << funcString[func];
    int val=INT_MIN;
    if (value.isValid() && value.canConvert<int>()) {
        // Used to validate payload, otherwise ignore.
        val = value.value<int>();
        //qInfo(logRig()) << "Got value" << QString(value.typeName());
        if (func == funcMemoryContents || func == funcMemoryClear || func == funcMemoryWrite || func == funcMemoryMode)
        {
            // Strip out group number from memory for validation purposes.
            qDebug(logRig()) << "Memory Command" << funcString[func] << "with valuetype "  << QString(value.typeName());
            val = val & 0xffff;
        }
    }

    if (func == funcSendCW)
    {
        val = value.value<QString>().length();
        qDebug(logRig()) << "Send CW received";
    }

    if (func == funcAfGain && value.isValid() && udp != Q_NULLPTR) {
        // Ignore the AF Gain command, just queue it for processing
        emit haveSetVolume(static_cast<uchar>(value.toInt()));
        queue->receiveValue(func,value,false);
        return;
    }

    // Need to work out what to do with older dual-VFO rigs.
    /*
    if ((func == funcMainFreq || func == funcSubFreq) && !rigCaps.commands.contains(func))
    {
        if (value.isValid())
            func = funcFreqSet;
        else
            func = funcFreqGet;
    } else if ((func == funcMainMode || func == funcSubMode) && !rigCaps.commands.contains(func))
    {
        if (value.isValid())
            func = funcModeSet;
        else
            func = funcModeGet;
    } else
    */
    if (func == funcSelectVFO) {
        // Special command
        vfo_t v = value.value<vfo_t>();
        func = (v == vfoA)?funcVFOASelect:(v == vfoB)?funcVFOBSelect:(v == vfoMain)?funcVFOMainSelect:(v == vfoSub)?funcVFOSubSelect:funcNone;
        value.clear();
        val = INT_MIN;
    }

    QByteArray payload;
    funcType cmd;
    cmd = getCommand(func,payload,val,receiver);
    if (cmd.cmd != funcNone)
    {
        // Certain commands need the receiver number adding first!
        switch (cmd.cmd) {
        case funcFreq: case funcMode: case funcScopeMode: case funcScopeSpan: case funcScopeRef: case funcScopeHold:
        case funcScopeSpeed: case funcScopeRBW: case funcScopeVBW: case funcScopeCenterType: case funcScopeEdge:
            payload.append(receiver);
            break;
        default:
            break;
        }

        if (value.isValid())
        {

            if (!cmd.setCmd) {
                qDebug(logRig()) << "Removing unsupported set command from queue" << funcString[func] << "VFO" << receiver;
                queue->del(func,receiver);
                return;
            } if (func == funcFreqSet) {
                queue->addUnique(priorityImmediate,funcFreqGet,false,receiver);
            } else if (func == funcModeSet) {
                queue->addUnique(priorityImmediate,funcModeGet,false,receiver);
            } else if (cmd.getCmd && func != funcScopeFixedEdgeFreq && func != funcSpeech &&
                       func != funcBandStackReg && func != funcMemoryContents && func != funcSatelliteMemory && func != funcSendCW) {
                // This was a set command, so queue a get to retrieve the updated value
                queue->addUnique(priorityImmediate,func,false,receiver);
            }


            if (!strcmp(value.typeName(),"bool"))
            {
                payload.append(value.value<bool>());
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
                            (p==0x40) || (p==0x20) || p == 0xff)
                        {
                            // Allowed character, continue
                        } else {
                            qWarning(logRig()) << "Invalid character detected in CW message at position " << c << ", the character is " << text.at(c);
                            textData[c] = 0x3F; // "?"
                        }
                    }
                    payload.append(textData);
                    qDebug(logRig()) << "Sending CW: payload:" << payload.toHex(' ');
                 }
            }
            else if (!strcmp(value.typeName(),"uchar"))
            {
                payload.append(bcdEncodeChar(value.value<uchar>()));
                qDebug(logRig()) << "**** setting uchar value" << funcString[func] << "val" << value.value<uchar>();
            }
            else if (!strcmp(value.typeName(),"ushort"))
            {
                 if (func == funcFilterWidth) {
                    payload.append(makeFilterWidth(value.value<ushort>(),receiver));
                    //qInfo() << "Setting filter width" << value.value<ushort>() << "VFO" << receiver << "hex" << payload.toHex();

                }
                else if (func == funcKeySpeed){
                    ushort wpm = round((value.value<ushort>()-6) * (6.071));
                    qDebug(logRig()) << "Sending key speed orig:" << value.value<ushort>() << "sent:" << wpm;
                    payload.append(bcdEncodeInt(wpm));
                }
                else if (func == funcCwPitch) {
                    ushort pitch = 0;
                    pitch = ceil((value.value<ushort>() - 300) * (255.0 / 600.0));
                    payload.append(bcdEncodeInt(pitch));
                }
                else {
                    payload.append(bcdEncodeInt(value.value<ushort>()));
                }
            }
            else if (!strcmp(value.typeName(),"short") && func == funcRITFreq)
            {
                // Currently only used for RIT (I think)
                bool isNegative = false;
                short val = value.value<short>();
                qDebug() << "Setting RIT to " << val;
                if(val < 0)
                {
                    isNegative = true;
                    val *= -1;
                }
                freqt f;
                QByteArray freqBytes;
                f.Hz = val;
                freqBytes = makeFreqPayload(f);
                freqBytes.truncate(2);
                payload.append(freqBytes);
                payload.append(QByteArray(1,(char)isNegative));
            }
            else if (!strcmp(value.typeName(),"uint") && (func == funcMemoryContents || func == funcMemoryMode))
            {
                qDebug(logRig()) << "Get Memory Contents" << (value.value<uint>() & 0xffff);
                qDebug(logRig()) << "Get Memory Group (if exists)" << (value.value<uint>() >> 16 & 0xffff);
                // Format is different for all radios!
                if (func == funcMemoryContents) {
                    for (auto &parse: rigCaps.memParser) {
                        // If "a" exists, break out of the loop as soon as we have the value.
                        if (parse.spec == 'a') {
                            if (parse.len == 1) {
                                payload.append(bcdEncodeChar(quint16(value.value<uint>() >> 16 & 0xff)));
                            }
                            else if (parse.len == 2)
                            {
                                payload.append(bcdEncodeInt(quint16(value.value<uint>() >> 16 & 0xffff)));
                            }
                            break;
                        }
                    }
                }
                payload.append(bcdEncodeInt(quint16(value.value<uint>() & 0xffff)));
            }
            else if (!strcmp(value.typeName(),"memoryType")) {
                // We need to iterate through memParser to build the correct format
                bool finished=false;
                char nul = 0x0;
                uchar ffchar = 0xff;
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

                // Format is different for all radios!
                for (auto &parse: parser) {
                    switch (parse.spec)
                    {
                    case 'a':
                        if (parse.len == 1) {
                            payload.append(mem.group);
                        }
                        else if (parse.len == 2)
                        {
                            payload.append(bcdEncodeInt(mem.group));
                        }
                        break;
                    case 'b':
                        payload.append(bcdEncodeInt(mem.channel));
                        break;
                    case 'c':
                        // Are we deleting the memory?
                        if (mem.del) {
                            payload.append(ffchar);
                            finished=true;
                            break;
                        } else {
                            payload.append(mem.scan);
                        }
                        break;
                    case 'C':
                        // Are we deleting the memory?
                        if (mem.del) {
                            payload.append(ffchar);
                            finished=true;
                            break;
                        } else {
                            payload.append(mem.scan);
                        }
                        break;
                    case 'd': // combined split and scan
                        payload.append(quint8((mem.split << 4 & 0xf0) | (mem.scan & 0x0f)));
                        break;
                    case 'D': // Duplex only
                        payload.append(mem.duplex);
                        break;
                    case 'e':
                        payload.append(mem.vfo);
                        break;
                    case 'E':
                        payload.append(mem.vfoB);
                        break;
                    case 'f':
                        if (mem.del) {
                            payload.append(ffchar);
                            finished=true;
                            break;
                        } else {
                            // IC905 memories use 6 byte freq (despite the docs saying 5)
                            // This workaround will add any missing bytes.
                            QByteArray f = makeFreqPayload(mem.frequency);
                            for (int i=f.size(); i<parse.len;i++) {
                                f.append(nul);
                            }
                            payload.append(f);
                        }
                        break;
                    case 'F':
                    {
                        QByteArray f = makeFreqPayload(mem.frequencyB);
                        for (int i=f.size(); i<parse.len;i++) {
                            f.append(nul);
                        }
                        payload.append(f);
                        break;
                    }
                    case 'g':
                        payload.append(bcdEncodeChar(mem.mode));
                        break;
                    case 'G':
                        payload.append(bcdEncodeChar(mem.modeB));
                        break;
                    case 'h':
                        payload.append(bcdEncodeChar(mem.filter));
                        break;
                    case 'H':
                        payload.append(bcdEncodeChar(mem.filterB));
                        break;
                    case 'i': // single datamode
                        payload.append(bcdEncodeChar(mem.datamode));
                        break;
                    case 'I':
                        payload.append(bcdEncodeChar(mem.datamodeB));
                        break;
                    case 'j': // combined duplex and tonemode
                        payload.append((mem.duplex << 4) | mem.tonemode);
                        break;
                    case 'J': // combined duplex and tonemode
                        payload.append((mem.duplexB << 4) | mem.tonemodeB);
                        break;
                    case 'k': // combined datamode and tonemode
                        payload.append((mem.datamode << 4 & 0xf0) | (mem.tonemode & 0x0f));
                        break;
                    case 'K': // combined datamode and tonemode
                        payload.append((mem.datamodeB << 4 & 0xf0) | (mem.tonemodeB & 0x0f));
                        break;
                    case 'l': // tonemode
                        payload.append(mem.tonemode);
                        break;
                    case 'L':
                        payload.append(mem.tonemodeB);
                        break;
                    case 'm':
                        payload.append(mem.dsql << 4);
                        break;
                    case 'M':
                        payload.append(mem.dsqlB << 4);
                        break;
                    case 'n':
                        payload.append(nul);
                        payload.append(bcdEncodeInt(mem.tone));
                        break;
                    case 'N':
                        payload.append(nul);
                        payload.append(bcdEncodeInt(mem.toneB));
                        break;
                    case 'o':
                        payload.append(nul);
                        payload.append(bcdEncodeInt(mem.tsql));
                        break;
                    case 'O':
                        payload.append(nul);
                        payload.append(bcdEncodeInt(mem.tsqlB));
                        break;
                    case 'p':
                        payload.append((mem.dtcsp << 3 & 0x10) |  (mem.dtcsp & 0x01));
                        break;
                    case 'P':
                        payload.append((mem.dtcspB << 3 & 0x10) |  (mem.dtcspB & 0x01));
                        break;
                    case 'q':
                        payload.append(bcdEncodeInt(mem.dtcs));
                        break;
                    case 'Q':
                        payload.append(bcdEncodeInt(mem.dtcsB));
                        break;
                    case 'r':
                        payload.append(bcdEncodeChar(mem.dvsql));
                        break;
                    case 'R':
                        payload.append(bcdEncodeChar(mem.dvsqlB));
                        break;
                    case 's':
                        payload.append(makeFreqPayload(mem.duplexOffset).mid(1,parse.len));
                        break;
                    case 'S':
                        payload.append(makeFreqPayload(mem.duplexOffsetB).mid(1,parse.len));
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
                    case 'w': // Tuning step
                        payload.append(quint8(mem.tuningStep!=0?1:0));
                        payload.append(bcdEncodeChar(qMax(uchar(1),mem.tuningStep))); // 0 is invalid.
                        payload.append(bcdEncodeInt(mem.progTs));
                        break;
                    case 'x': // Attenuator & Preamp
                        payload.append(bcdEncodeChar(mem.atten));
                        payload.append(bcdEncodeChar(mem.preamp));
                        break;
                    case 'y': // Antenna
                        payload.append(bcdEncodeChar(mem.antenna));
                        break;
                    case '+': // IP Plus
                        payload.append(bcdEncodeChar(mem.ipplus));
                        break;
                    case 'z':
                        payload.append(QByteArray(mem.name).leftJustified(parse.len,' ',true));
                        break;
                    case 'Z': // Special mode dependant features (I have no idea how to make these work!)
                        //qInfo() << "Looking for mode:" << mem.mode;
                        for (const auto &m: rigCaps.modes)
                        {
                            if (m.reg == mem.mode)
                            {
                                // This mode is the one we are interested in!
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
                                switch (m.mk)
                                {
                                case modeFM:
                                    if (mem.tonemode) {
                                        payload.append(bcdEncodeChar(mem.tonemode));
                                        payload.append(nul);
                                        payload.append(bcdEncodeInt(mem.tsql));
                                        payload.append(bcdEncodeChar(mem.dtcsp));
                                        payload.append(bcdEncodeInt(mem.dtcs));
                                    }
                                    break;
                                case modeDV:
                                    if (mem.dsql) {
                                        payload.append(bcdEncodeChar(mem.dsql?2:0)); // Set must be 2 but radio returns 1?
                                        payload.append(bcdEncodeChar(mem.dvsql));
                                    }
                                    break;
                                case modeP25:
                                    if (mem.p25Sql) {
                                        payload.append(bcdEncodeChar(mem.p25Sql));
                                        payload.append(uchar((mem.p25Nac & 0xf00) >> 8));
                                        payload.append(uchar((mem.p25Nac & 0x0f0) >> 4));
                                        payload.append(uchar(mem.p25Nac & 0x00f));
                                    }
                                    break;
                                case modedPMR:
                                    qInfo() << "Sending dPMR sq:" << mem.dPmrSql << "Com ID:" << mem.dPmrComid << "CC:" << mem.dPmrCc << "SCRM:" << mem.dPmrSCRM << "Key:" << mem.dPmrKey;;
                                    if (mem.dPmrSql || mem.dPmrSCRM) {
                                        payload.append(bcdEncodeChar(mem.dPmrSql));
                                        payload.append(bcdEncodeInt(mem.dPmrComid));
                                        payload.append(bcdEncodeChar(mem.dPmrCc));
                                        payload.append(bcdEncodeChar(mem.dPmrSCRM));
                                        payload.append(bcdEncodeInt(mem.dPmrKey));
                                    }
                                    break;
                                case modeNXDN_N:
                                case modeNXDN_VN:
                                    if (mem.nxdnSql || mem.nxdnEnc) {
                                    payload.append(bcdEncodeChar(mem.nxdnSql));
                                    payload.append(bcdEncodeChar(mem.nxdnRan));
                                    payload.append(bcdEncodeChar(mem.nxdnEnc));
                                    payload.append(bcdEncodeInt(mem.nxdnKey));
                                    }
                                    break;
                                case modeDCR:
                                    if (mem.dcrSql || mem.dcrEnc) {
                                        payload.append(bcdEncodeChar(mem.dcrSql));
                                        payload.append(bcdEncodeInt(mem.dcrUc));
                                        payload.append(bcdEncodeChar(mem.dcrEnc));
                                        payload.append(bcdEncodeInt(mem.dcrKey));
                                    }
                                    break;
                                default:
                                    break;
                                }
                                break;
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif

                            }
                        }
                        break;
                    default:
                        break;
                    }
                    if (finished)
                        break;
                }
                qDebug(logRig()) << "Writing memory location:" << payload.toHex(' ');

            }
            else if (!strcmp(value.typeName(),"int") && (func == funcScopeRef))
            {
                bool isNegative = false;
                int level = value.value<int>();
                if(level < 0)
                {
                    isNegative = true;
                    level *= -1;
                }
                payload.append(bcdEncodeInt(quint16(level*10)));
                payload.append(static_cast<quint8>(isNegative));
            }
            else if (!strcmp(value.typeName(),"modeInfo"))
            {
                {
                    modeInfo m = value.value<modeInfo>();
                    if (func == funcDataModeWithFilter)
                    {
                        payload.append(bcdEncodeChar(m.data));
                        if (m.data != 0)
                           payload.append(m.filter);
                    } else {
                        payload.append(bcdEncodeChar(m.reg));
                        if (func == funcMode|| func == funcSelectedMode || func == funcUnselectedMode)
                           payload.append(m.data);
                        if (!rigCaps.filters.empty() && m.mk != modeWFM)
                            payload.append(m.filter);
                        qDebug(logRig()) << "Sending mode command" << funcString[func] << " mode:" << m.name<< "data:" << m.data << "filter" << m.filter;
                    }
                }
            }
            else if(!strcmp(value.typeName(),"freqt"))
            {
                if (func == funcSendFreqOffset) {
                    payload.append(makeFreqPayload(value.value<freqt>()).mid(1,3));
                } else {
                    payload.append(makeFreqPayload(value.value<freqt>()));
                }
            }
            else if(!strcmp(value.typeName(),"antennaInfo"))
            {
                payload.append(bcdEncodeChar(value.value<antennaInfo>().antenna));
                if (rigCaps.commands.contains(funcRXAntenna))
                    payload.append(value.value<antennaInfo>().rx);
                //qInfo(logRig) << "Sending antenna info" << payload.toHex(' ');
            }
            else if(!strcmp(value.typeName(),"rigInput"))
            {
                payload.append(bcdEncodeChar(value.value<rigInput>().reg));
            }
            else if (!strcmp(value.typeName(),"spectrumBounds"))
            {
                spectrumBounds s = value.value<spectrumBounds>();
                uchar range=1;
                uchar oldRange=0;
                for (const bandType& band: rigCaps.bands)
                {

                    if (oldRange != range && band.range != 0.0 && s.start > band.range)
                    {
                        range++;
                        oldRange=band.range;
                    }
                }
                payload.append(bcdEncodeChar(range));
                payload.append(bcdEncodeChar(s.edge));
                payload.append(makeFreqPayload(s.start));
                payload.append(makeFreqPayload(s.end));
                qInfo() << "Bounds" << range << s.edge << s.start << s.end << payload.toHex();

            }
            else if (!strcmp(value.typeName(),"duplexMode_t"))
            {
                payload.append(static_cast<uchar>(value.value<duplexMode_t>()));
            }
            else if (!strcmp(value.typeName(),"spectrumMode_t"))
            {
                payload.append(static_cast<uchar>(value.value<spectrumMode_t>()));
            }
            else if (!strcmp(value.typeName(),"centerSpanData"))
            {
                centerSpanData span = value.value<centerSpanData>();
                double freq = double(span.freq/1000000.0);
                payload.append(makeFreqPayload(freq));
            }
            else if (!strcmp(value.typeName(),"toneInfo"))
            {
                toneInfo t = value.value<toneInfo>();
                payload.append(encodeTone(t.tone, t.tinv, t.rinv));
            }
            else if (!strcmp(value.typeName(),"bandStackType"))
            {

                bandStackType bsr = value.value<bandStackType>();
                payload.append(bcdEncodeChar(bsr.band));        //byte 0
                payload.append(bcdEncodeChar(bsr.regCode));     //byte 1
                if (bsr.freq.Hz != 0) {
                    // We are setting the bsr so send freq/mode data.
                    // First find which band we are working on.
                    for (const auto &b: rigCaps.bands)
                    {
                        if (b.bsr == bsr.band)
                        {
                            payload.append(makeFreqPayload(bsr.freq,b.bytes));
                            payload.append(bcdEncodeChar(bsr.mode));
                            payload.append(bcdEncodeChar(bsr.filter));
                            payload.append((bsr.data << 4 & 0xf0) + (bsr.sql & 0x0f));
                            if (bsr.tone.tone != 0) {
                                payload.append(encodeTone(bsr.tone.tone));
                                payload.append(encodeTone(bsr.tsql.tone));
                            }
                            break;
                        }

                    }
                }
                qInfo(logRig()) << "Sending BSR, Band Code:" << bsr.band << "Register Code:" << bsr.regCode << "(Sent:" << payload.toHex(' ') << ")";
            }
            else if (!strcmp(value.typeName(),"datekind"))
            {
                datekind d = value.value<datekind>();
                qInfo(logRig()) << QString("Sending new date: (MM-DD-YYYY) %0-%1-%2").arg(d.month).arg(d.day).arg(d.year);
                // YYYYMMDD
                payload.append(convertNumberToHex(d.year/100)); // 20
                payload.append(convertNumberToHex(d.year - 100*(d.year/100))); // 21
                payload.append(convertNumberToHex(d.month));
                payload.append(convertNumberToHex(d.day));

            }
            else if (!strcmp(value.typeName(),"timekind"))
            {
                timekind t = value.value<timekind>();
                if (cmd.cmd == funcTime) {
                    qInfo(logRig()) << QString("Sending new time: (HH:MM) %0:%1").arg(t.hours).arg(t.minutes);
                    payload.append(convertNumberToHex(t.hours));
                    payload.append(convertNumberToHex(t.minutes));

                } else if (cmd.cmd == funcUTCOffset) {
                    qInfo(logRig()) << QString("Sending new UTC offset: %0%1:%2").arg(t.isMinus?"-":"+").arg(t.hours).arg(t.minutes);
                    payload.append(convertNumberToHex(t.hours));
                    payload.append(convertNumberToHex(t.minutes));
                    payload.append((uchar)t.isMinus);

                }
            }
            else if (!strcmp(value.typeName(),"rptrAccessData"))
            {
                rptrAccessData r = value.value<rptrAccessData>();
                qDebug(logRig()) << "Sending rptrAccessData Mode" << r.accessMode;
                payload.append(bcdEncodeChar(static_cast<uchar>(r.accessMode)));
            }
            else
            {
                qInfo(logRig()) << "Got unknown value type" << QString(value.typeName());
                return;
            }              
        } else {
            // This is a get command
            if (!cmd.getCmd)
            {
                // Get command not supported
                qDebug(logRig()) << "Removing unsupported get command from queue" << funcString[func] << "VFO" << receiver;
                queue->del(func,receiver);
                return;
            }
            //else if (cmd.cmd == funcVFOModeSelect) {
                //qInfo(logRig()) << "Attempting to select VFO mode:" << payload.toHex(' ');
            //}
        }
        prepDataAndSend(payload);
    }
    else
    {
        qDebug(logRig()) << "cachingQueue(): unimplemented command" << funcString[func];
        queue->del(func,receiver);
    }
}

