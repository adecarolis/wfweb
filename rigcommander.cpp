#include "rigcommander.h"
#include <QDebug>

#include "rigidentities.h"
#include "logcategories.h"

// Copytight 2017-2020 Elliott H. Liggett

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

rigCommander::rigCommander()
{
    rigState.filter = 0;
    rigState.mode = 0;
    rigState.ptt = 0;
}

rigCommander::~rigCommander()
{
    closeComm();
}


void rigCommander::commSetup(unsigned char rigCivAddr, QString rigSerialPort, quint32 rigBaudRate)
{
    // construct
    // TODO: Bring this parameter and the comm port from the UI.
    // Keep in hex in the UI as is done with other CIV apps.

    // civAddr = 0x94; // address of the radio. Decimal is 148.
    civAddr = rigCivAddr; // address of the radio. Decimal is 148.
    usingNativeLAN = false;

    // ---
    setup();
    // ---

    this->rigSerialPort = rigSerialPort;
    this->rigBaudRate = rigBaudRate;

    comm = new commHandler(rigSerialPort, rigBaudRate);
    ptty = new pttyHandler();

    // data from the comm port to the program:
    connect(comm, SIGNAL(haveDataFromPort(QByteArray)), this, SLOT(handleNewData(QByteArray)));

    // data from the ptty to the rig:
    connect(ptty, SIGNAL(haveDataFromPort(QByteArray)), comm, SLOT(receiveDataFromUserToRig(QByteArray)));

    // data from the program to the comm port:
    connect(this, SIGNAL(dataForComm(QByteArray)), comm, SLOT(receiveDataFromUserToRig(QByteArray)));

    // data from the rig to the ptty:
    connect(comm, SIGNAL(haveDataFromPort(QByteArray)), ptty, SLOT(receiveDataFromRigToPtty(QByteArray)));

    connect(comm, SIGNAL(haveSerialPortError(QString, QString)), this, SLOT(handleSerialPortError(QString, QString)));
    connect(ptty, SIGNAL(haveSerialPortError(QString, QString)), this, SLOT(handleSerialPortError(QString, QString)));

    connect(this, SIGNAL(getMoreDebug()), comm, SLOT(debugThis()));
    connect(this, SIGNAL(getMoreDebug()), ptty, SLOT(debugThis()));
    emit commReady();

}

void rigCommander::commSetup(unsigned char rigCivAddr, udpPreferences prefs)
{
    // construct
    // TODO: Bring this parameter and the comm port from the UI.
    // Keep in hex in the UI as is done with other CIV apps.

    // civAddr = 0x94; // address of the radio. Decimal is 148.
    civAddr = rigCivAddr; // address of the radio. Decimal is 148.
    usingNativeLAN = true;

    // ---
    setup();
    // ---

    if (udp == Q_NULLPTR) {

        udp = new udpHandler(prefs);

        udpHandlerThread = new QThread(this);

        udp->moveToThread(udpHandlerThread);

        connect(this, SIGNAL(initUdpHandler()), udp, SLOT(init()));
        connect(udpHandlerThread, SIGNAL(finished()), udp, SLOT(deleteLater()));

        udpHandlerThread->start();

        emit initUdpHandler();

        //this->rigSerialPort = rigSerialPort;
        //this->rigBaudRate = rigBaudRate;

        ptty = new pttyHandler();

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

        connect(this, SIGNAL(haveChangeLatency(quint16)), udp, SLOT(changeLatency(quint16)));
        connect(this, SIGNAL(haveSetVolume(unsigned char)), udp, SLOT(setVolume(unsigned char)));

        // Connect for errors/alerts
        connect(udp, SIGNAL(haveNetworkError(QString, QString)), this, SLOT(handleSerialPortError(QString, QString)));
        connect(udp, SIGNAL(haveNetworkStatus(QString)), this, SLOT(handleStatusUpdate(QString)));

        connect(ptty, SIGNAL(haveSerialPortError(QString, QString)), this, SLOT(handleSerialPortError(QString, QString)));
        connect(this, SIGNAL(getMoreDebug()), ptty, SLOT(debugThis()));
        emit haveAfGain(255);
    }

    // data from the comm port to the program:

    emit commReady();
    emit stateInfo(&rigState);

    pttAllowed = true; // This is for developing, set to false for "safe" debugging. Set to true for deployment.

}

void rigCommander::closeComm()
{
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
    payloadPrefix = QByteArray("\xFE\xFE");
    payloadPrefix.append(civAddr);
    payloadPrefix.append((char)compCivAddr);

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
        emit haveSerialPortError(rigSerialPort, QString("Error from commhandler. Check serial port."));
    }
}

void rigCommander::handleSerialPortError(const QString port, const QString errorText)
{
    qDebug(logRig()) << "Error using port " << port << " message: " << errorText;
    emit haveSerialPortError(port, errorText);
}

void rigCommander::handleStatusUpdate(const QString text)
{
    emit haveStatusUpdate(text);
}

bool rigCommander::usingLAN()
{
    return usingNativeLAN;
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
    return;
}

void rigCommander::prepDataAndSend(QByteArray data)
{
    data.prepend(payloadPrefix);
    //printHex(data, false, true);
    data.append(payloadSuffix);
#ifdef QT_DEBUG
    if(data[4] != '\x15')
    {
        // We don't print out requests for meter levels
        qDebug(logRig()) << "Final payload in rig commander to be sent to rig: ";
        printHex(data);
    }
#endif
    emit dataForComm(data);
}

void rigCommander::powerOn()
{
    QByteArray payload;

    for(int i=0; i < 150; i++)
    {
        payload.append("\xFE");
    }

    payload.append(payloadPrefix); // FE FE 94 E1
    payload.append("\x18\x01");
    payload.append(payloadSuffix); // FD

#ifdef QT_DEBUG
    qDebug(logRig()) << "Power ON command in rigcommander to be sent to rig: ";
    printHex(payload);
#endif

    emit dataForComm(payload);

}

void rigCommander::powerOff()
{
    QByteArray payload;
    payload.setRawData("\x18\x00", 2);
    prepDataAndSend(payload);
}

void rigCommander::enableSpectOutput()
{
    QByteArray payload("\x27\x11\x01");
    prepDataAndSend(payload);
}

void rigCommander::disableSpectOutput()
{
    QByteArray payload;
    payload.setRawData("\x27\x11\x00", 3);
    prepDataAndSend(payload);
}

void rigCommander::enableSpectrumDisplay()
{
    // 27 10 01
    QByteArray payload("\x27\x10\x01");
    prepDataAndSend(payload);
}

void rigCommander::disableSpectrumDisplay()
{
    // 27 10 00
    QByteArray payload;
    payload.setRawData("\x27\x10\x00", 3);
    prepDataAndSend(payload);
}

void rigCommander::setSpectrumBounds(double startFreq, double endFreq, unsigned char edgeNumber)
{
    if((edgeNumber > 4) || (!edgeNumber))
    {
        return;
    }

    unsigned char freqRange = 1; // 1 = VHF, 2 = UHF, 3 = L-Band

    switch(rigCaps.model)
    {
        case model9700:
            if(startFreq > 148)
            {
                freqRange++;
                if(startFreq > 450)
                {
                    freqRange++;
                }
            }
            break;
        case model705:
        case model7300:
        case model7610:
        case model7850:
            // Some rigs do not go past 60 MHz, but we will not encounter
            // requests for those ranges since they are derived from the rig's existing scope range.
            // start value of freqRange is 1.
            if(startFreq > 1.6)
                freqRange++;
            if(startFreq > 2.0)
                freqRange++;
            if(startFreq > 6.0)
                freqRange++;
            if(startFreq > 8.0)
                freqRange++;
            if(startFreq > 11.0)
                freqRange++;
            if(startFreq > 15.0)
                freqRange++;
            if(startFreq > 20.0)
                freqRange++;
            if(startFreq > 22.0)
                freqRange++;
            if(startFreq > 26.0)
                freqRange++;
            if(startFreq > 30.0)
                freqRange++;
            if(startFreq > 45.0)
                freqRange++;
            if(startFreq > 60.0)
                freqRange++;
            if(startFreq > 74.8)
                freqRange++;
            if(startFreq > 108.0)
                freqRange++;
            if(startFreq > 137.0)
                freqRange++;
            if(startFreq > 400.0)
                freqRange++;
            break;
        default:
            return;
            break;


    }
    QByteArray lowerEdge = makeFreqPayload(startFreq);
    QByteArray higherEdge = makeFreqPayload(endFreq);


    QByteArray payload;

    payload.setRawData("\x27\x1E", 2);
    payload.append(freqRange);
    payload.append(edgeNumber);

    payload.append(lowerEdge);
    payload.append(higherEdge);

    prepDataAndSend(payload);
}

void rigCommander::getScopeMode()
{
    // center or fixed
    QByteArray payload;
    payload.setRawData("\x27\x14\x00", 3);
    prepDataAndSend(payload);
}

void rigCommander::getScopeEdge()
{
    QByteArray payload;
    payload.setRawData("\x27\x16", 2);
    prepDataAndSend(payload);
}

void rigCommander::setScopeEdge(char edge)
{
    // 1 2 or 3
    // 27 16 00 0X
    if((edge <1) || (edge >4))
        return;
    QByteArray payload;
    payload.setRawData("\x27\x16\x00", 3);
    payload.append(edge);
    prepDataAndSend(payload);
}

void rigCommander::getScopeSpan()
{
    getScopeSpan(false);
}

void rigCommander::getScopeSpan(bool isSub)
{
    QByteArray payload;
    payload.setRawData("\x27\x15", 2);
    payload.append(static_cast<unsigned char>(isSub));
    prepDataAndSend(payload);
}

void rigCommander::setScopeSpan(char span)
{
    // See ICD, page 165, "19-12".
    // 2.5k = 0
    // 5k = 2, etc.
    if((span <0 ) || (span >7))
            return;

    QByteArray payload;
    double freq; // MHz
    payload.setRawData("\x27\x15\x00", 3);
    // next 6 bytes are the frequency
    switch(span)
    {
        case 0:
            // 2.5k
            freq = 2.5E-3;
            break;
        case 1:
            // 5k
            freq = 5.0E-3;
            break;
        case 2:
            freq = 10.0E-3;
            break;
        case 3:
            freq = 25.0E-3;
            break;
        case 4:
            freq = 50.0E-3;
            break;
        case 5:
            freq = 100.0E-3;
            break;
        case 6:
            freq = 250.0E-3;
            break;
        case 7:
            freq = 500.0E-3;
            break;
        default:
            return;
            break;
    }

    payload.append( makeFreqPayload(freq));
    payload.append("\x00");
    // printHex(payload, false, true);
    prepDataAndSend(payload);
}

void rigCommander::setSpectrumMode(spectrumMode spectMode)
{
    QByteArray specModePayload;
    specModePayload.setRawData("\x27\x14\x00", 3);
    specModePayload.append( static_cast<unsigned char>(spectMode) );
    prepDataAndSend(specModePayload);
}

void rigCommander::getSpectrumRefLevel()
{
    QByteArray payload;
    payload.setRawData("\x27\x19\x00", 3);
    prepDataAndSend(payload);
}

void rigCommander::getSpectrumRefLevel(unsigned char mainSub)
{
    QByteArray payload;
    payload.setRawData("\x27\x19", 2);
    payload.append(mainSub);
    prepDataAndSend(payload);
}

void rigCommander::setSpectrumRefLevel(int level)
{
    //qDebug(logRig()) << __func__ << ": Setting scope to level " << level;
    QByteArray setting;
    QByteArray number;
    QByteArray pn;
    setting.setRawData("\x27\x19\x00", 3);

    if(level >= 0)
    {
        pn.setRawData("\x00", 1);
        number = bcdEncodeInt(level*10);
    } else {
        pn.setRawData("\x01", 1);
        number = bcdEncodeInt( (-level)*10 );
    }

    setting.append(number);
    setting.append(pn);

    //qDebug(logRig()) << __func__ << ": scope reference number: " << number << ", PN to: " << pn;
    //printHex(setting, false, true);

    prepDataAndSend(setting);
}


void rigCommander::getSpectrumCenterMode()
{
    QByteArray specModePayload;
    specModePayload.setRawData("\x27\x14", 2);
    prepDataAndSend(specModePayload);
}

void rigCommander::getSpectrumMode()
{
    QByteArray specModePayload;
    specModePayload.setRawData("\x27\x14", 2);
    prepDataAndSend(specModePayload);
}

void rigCommander::setFrequency(freqt freq)
{
    //QByteArray freqPayload = makeFreqPayload(freq);
    QByteArray freqPayload = makeFreqPayload(freq);
    QByteArray cmdPayload;

    cmdPayload.append(freqPayload);
    cmdPayload.prepend('\x00');

    //printHex(cmdPayload, false, true);
    prepDataAndSend(cmdPayload);
    rigState.vfoAFreq = freq;
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
    qDebug(logRig()) << __func__ << ": encoded frequency for Hz: " << freq.Hz  <<\
                     ", double: " << freq.MHzDouble << " as 64-bit uint: " << freqInt;
    printHex(result, false, true);

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
    //qDebug(logRig()) << "encoded frequency for " << freq << " as int " << freqInt;
    //printHex(result, false, true);
    return result;
}

void rigCommander::setRitEnable(bool ritEnabled)
{
    QByteArray payload;

    if(ritEnabled)
    {
        payload.setRawData("\x21\x01\x01", 3);
    } else {
        payload.setRawData("\x21\x01\x00", 3);
    }
    prepDataAndSend(payload);
}

void rigCommander::getRitEnabled()
{
    QByteArray payload;
    payload.setRawData("\x21\x01", 2);
    prepDataAndSend(payload);
}

void rigCommander::getRitValue()
{
    QByteArray payload;
    payload.setRawData("\x21\x00", 2);
    prepDataAndSend(payload);
}

void rigCommander::setRitValue(int ritValue)
{
    QByteArray payload;
    QByteArray freqBytes;
    freqt f;

    bool isNegative = false;
    payload.setRawData("\x21\x00", 2);

    if(ritValue < 0)
    {
        isNegative = true;
        ritValue *= -1;
    }

    if(ritValue > 9999)
        return;

    f.Hz = ritValue;

    freqBytes = makeFreqPayload(f);

    freqBytes.truncate(2);
    payload.append(freqBytes);

    payload.append(QByteArray(1,(char)isNegative));

    prepDataAndSend(payload);
}

void rigCommander::setMode(unsigned char mode, unsigned char modeFilter)
{
    QByteArray payload;
    if(mode < 0x22 + 1)
    {
        // mode command | filter
        // 0x01 | Filter 01 automatically
        // 0x04 | user-specififed 01, 02, 03 | note, is "read the current mode" on older rigs
        // 0x06 | "default" filter is auto

        payload.setRawData("\x06", 1); // cmd 06 needs filter specified
        //payload.setRawData("\x04", 1); // cmd 04 will apply the default filter, but it seems to always pick FIL 02

        payload.append(mode);
        if(rigCaps.model==model706)
        {
            payload.append("\x01"); // "normal" on IC-706
        } else {
            payload.append(modeFilter);
        }
        prepDataAndSend(payload);
        rigState.mode = mode;
        rigState.filter = modeFilter;
    }
}

void rigCommander::setDataMode(bool dataOn)
{
    QByteArray payload;

    payload.setRawData("\x1A\x06", 2);
    if(dataOn)
    {
        payload.append("\x01\x03", 2); // data mode on, wide bandwidth

    } else {
        payload.append("\x00\x00", 2); // data mode off, bandwidth not defined per ICD.
    }
    prepDataAndSend(payload);
    rigState.datamode = dataOn;
}

void rigCommander::getFrequency()
{
    // figure out frequency and then respond with haveFrequency();
    // send request to radio
    // 1. make the data
    QByteArray payload("\x03");
    prepDataAndSend(payload);
}

void rigCommander::getMode()
{
    QByteArray payload("\x04");
    prepDataAndSend(payload);
}

void rigCommander::getDataMode()
{
    QByteArray payload("\x1A\x06");
    prepDataAndSend(payload);
}

void rigCommander::setDuplexMode(duplexMode dm)
{
    QByteArray payload;
    if(dm==dmDupAutoOff)
    {
        payload.setRawData("\x1A\x05\x00\x46\x00", 5);
    } else if (dm==dmDupAutoOn)
    {
        payload.setRawData("\x1A\x05\x00\x46\x01", 5);
    } else {
        payload.setRawData("\x0F", 1);
        payload.append((unsigned char) dm);
    }
    prepDataAndSend(payload);
}

void rigCommander::getDuplexMode()
{
    QByteArray payload;

    // Duplex mode:
    payload.setRawData("\x0F", 1);
    prepDataAndSend(payload);

    // Auto Repeater Mode:
    payload.setRawData("\x1A\x05\x00\x46", 4);
    prepDataAndSend(payload);
}

void rigCommander::getTransmitFrequency()
{
    QByteArray payload;
    payload.setRawData("\x1C\x03", 2);
    prepDataAndSend(payload);
}

void rigCommander::setTone(quint16 tone)
{
    QByteArray fenc = encodeTone(tone);

    QByteArray payload;
    payload.setRawData("\x1B\x00", 2);
    payload.append(fenc);

    //qDebug() << __func__ << "TONE encoded payload: ";
    printHex(payload);

    prepDataAndSend(payload);
}

void rigCommander::setTSQL(quint16 tsql)
{
    QByteArray fenc = encodeTone(tsql);

    QByteArray payload;
    payload.setRawData("\x1B\x01", 2);
    payload.append(fenc);

    //qDebug() << __func__ << "TSQL encoded payload: ";
    printHex(payload);

    prepDataAndSend(payload);
}

void rigCommander::setDTCS(quint16 dcscode, bool tinv, bool rinv)
{

    QByteArray denc = encodeTone(dcscode, tinv, rinv);

    QByteArray payload;
    payload.setRawData("\x1B\x02", 2);
    payload.append(denc);

    //qDebug() << __func__ << "DTCS encoded payload: ";
    printHex(payload);

    prepDataAndSend(payload);
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
    payload.setRawData("\x1B\x00", 2);
    prepDataAndSend(payload);
}

void rigCommander::getTSQL()
{
    QByteArray payload;
    payload.setRawData("\x1B\x01", 2);
    prepDataAndSend(payload);
}

void rigCommander::getDTCS()
{
    QByteArray payload;
    payload.setRawData("\x1B\x02", 2);
    prepDataAndSend(payload);
}

void rigCommander::getRptAccessMode()
{
    QByteArray payload;
    payload.setRawData("\x16\x5D", 2);
    prepDataAndSend(payload);
}

void rigCommander::setRptAccessMode(rptAccessTxRx ratr)
{
    QByteArray payload;
    payload.setRawData("\x16\x5D", 2);
    payload.append((unsigned char)ratr);
    prepDataAndSend(payload);
}

void rigCommander::setIPP(bool enabled)
{
    QByteArray payload;
    payload.setRawData("\x16\x65", 2);
    if(enabled)
    {
        payload.append("\x01");
    } else {
        payload.append("\x00");
    }
    prepDataAndSend(payload);
}

void rigCommander::getIPP()
{
    QByteArray payload;
    payload.setRawData("\x16\x65", 2);
    prepDataAndSend(payload);
}

void rigCommander::setSatelliteMode(bool enabled)
{
    QByteArray payload;
    payload.setRawData("\x16\x5A", 2);
    if(enabled)
    {
        payload.append("\x01");
    } else {
        payload.append("\x00");
    }
    prepDataAndSend(payload);
}

void rigCommander::getSatelliteMode()
{
    QByteArray payload;
    payload.setRawData("\x16\x5A", 2);
    prepDataAndSend(payload);
}

void rigCommander::getPTT()
{
    QByteArray payload;
    payload.setRawData("\x1C\x00", 2);
    prepDataAndSend(payload);
}

void rigCommander::getBandStackReg(char band, char regCode)
{
    QByteArray payload("\x1A\x01");
    payload.append(band); // [01 through 11]
    payload.append(regCode); // [01...03]. 01 = latest, 03 = oldest
    prepDataAndSend(payload);
}

void rigCommander::setPTT(bool pttOn)
{
    //bool pttAllowed = false;

    if(pttAllowed)
    {
        QByteArray payload("\x1C\x00", 2);
        payload.append((char)pttOn);
        prepDataAndSend(payload);
        rigState.ptt = pttOn;
    }
}

void rigCommander::setCIVAddr(unsigned char civAddr)
{
    // Note: This is the radio's CIV address
    // the computer's CIV address is defined in the header file.
    this->civAddr = civAddr;
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
    // qDebug(logRig()) << "data list has this many elements: " << dataList.size();
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

        //qDebug(logRig()) << "Data received: ";
        //printHex(data, false, true);
        if(data.length() < 4)
        {
            if(data.length())
            {
                // Finally this almost never happens
                // qDebug(logRig()) << "Data length too short: " << data.length() << " bytes. Data:";
                //printHex(data, false, true);
            }
            // no
            //return;
            // maybe:
            // continue;
        }

        if(!data.startsWith("\xFE\xFE"))
        {
            // qDebug(logRig()) << "Warning: Invalid data received, did not start with FE FE.";
            // find 94 e0 and shift over,
            // or look inside for a second FE FE
            // Often a local echo will miss a few bytes at the beginning.
            if(data.startsWith('\xFE'))
            {
                data.prepend('\xFE');
                // qDebug(logRig()) << "Warning: Working with prepended data stream.";
                parseData(payloadIn);
                return;
            } else {
                //qDebug(logRig()) << "Error: Could not reconstruct corrupted data: ";
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
            // qDebug(logRig()) << "[FOUND] Trimmed off echo:";
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
            //        //qDebug(logRig()) << "Trimmed off echo:";
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
#ifdef QT_DEBUG
                    qDebug(logRig()) << "Caught it! Found the echo'd broadcast request from us!";
#endif
                } else {
                    payloadIn = data.right(data.length() - 4);
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
        qDebug(logRig()) << "Recovered " << count << " frames from single data with size" << dataList.count();
    }
    */
}

void rigCommander::parseCommand()
{
    // note: data already is trimmed of the beginning FE FE E0 94 stuff.

#ifdef QT_DEBUG
    bool isSpectrumData = payloadIn.startsWith(QByteArray().setRawData("\x27\x00", 2));

    if( (!isSpectrumData) && (payloadIn[00] != '\x15') )
    {
        // We do not log spectrum and meter data,
        // as they tend to clog up any useful logging.
        printHex(payloadIn);
    }
#endif

    switch(payloadIn[00])
    {

        case 00:
            // frequency data
            parseFrequency();
            break;
        case 03:
            parseFrequency();
            break;
        case '\x25':
            if((int)payloadIn[1] == 0)
            {
                emit haveFrequency(parseFrequency(payloadIn, 5));
            }
            break;
        case '\x01':
            //qDebug(logRig()) << "Have mode data";
            this->parseMode();
            break;
        case '\x04':
            //qDebug(logRig()) << "Have mode data";
            this->parseMode();
            break;
        case '\x05':
            //qDebug(logRig()) << "Have frequency data";
            this->parseFrequency();
            break;
        case '\x06':
            //qDebug(logRig()) << "Have mode data";
            this->parseMode();
            break;
        case '\x0F':
            emit haveDuplexMode((duplexMode)(unsigned char)payloadIn[1]);
            break;
        case '\x11':
            emit haveAttenuator((unsigned char)payloadIn.at(1));
            break;
        case '\x14':
            // read levels
            parseLevels();
            break;
        case '\x15':
            // Metering such as s, power, etc
            parseLevels();
            break;
        case '\x16':
            parseRegister16();
            break;
        case '\x19':
            // qDebug(logRig()) << "Have rig ID: " << (unsigned int)payloadIn[2];
            // printHex(payloadIn, false, true);
            model = determineRadioModel(payloadIn[2]); // verify this is the model not the CIV
            determineRigCaps();
            qDebug(logRig()) << "Have rig ID: decimal: " << (unsigned int)model;


            break;
        case '\x21':
            // RIT and Delta TX:
            parseRegister21();
            break;
        case '\x26':
            if((int)payloadIn[1] == 0)
            {
                // This works but LSB comes out as CW?
                // Also, an opportunity to read the data mode
                // payloadIn = payloadIn.right(3);
                // this->parseMode();
            }
            break;
        case '\x27':
            // scope data
            //qDebug(logRig()) << "Have scope data";
            //printHex(payloadIn, false, true);
            parseWFData();
            //parseSpectrum();
            break;
        case '\x1A':
            if(payloadIn[01] == '\x05')
            {
                parseDetailedRegisters1A05();
            } else {
                parseRegisters1A();
            }
            break;
        case '\x1B':
            parseRegister1B();
            break;
        case '\x1C':
            parseRegisters1C();
            break;
        case '\xFB':
            // Fine Business, ACK from rig.
            break;
        case '\xFA':
            // error
#ifdef QT_DEBUG
            qDebug(logRig()) << "Error (FA) received from rig.";
            printHex(payloadIn, false ,true);
#endif
            break;

        default:
            // This gets hit a lot when the pseudo-term is
            // using commands wfview doesn't know yet.
            // qDebug(logRig()) << "Have other data with cmd: " << std::hex << payloadIn[00];
            // printHex(payloadIn, false, true);
            break;
    }
    // is any payload left?

}

void rigCommander::parseLevels()
{
    //qDebug(logRig()) << "Received a level status readout: ";
    // printHex(payloadIn, false, true);

    // wrong: unsigned char level = (payloadIn[2] * 100) + payloadIn[03];
    unsigned char hundreds = payloadIn[2];
    unsigned char tens = (payloadIn[3] & 0xf0) >> 4;
    unsigned char units = (payloadIn[3] & 0x0f);

    unsigned char level = (100*hundreds) + (10*tens) + units;

    //qDebug(logRig()) << "Level is: " << (int)level << " or " << 100.0*level/255.0 << "%";

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
                }
                break;
            case '\x02':
                // RX RF Gain
                emit haveRfGain(level);
                break;
            case '\x03':
                // Squelch level
                emit haveSql(level);
                break;
            case '\x0A':
                // TX RF level
                emit haveTxPower(level);
                break;
            case '\x0B':
                // Mic Gain
                emit haveMicGain(level);
                break;
            case '\x0E':
                // compressor level
                emit haveCompLevel(level);
                break;
            case '\x15':
                // monitor level
                emit haveMonitorLevel(level);
                break;
            case '\x16':
                // VOX gain
                emit haveVoxGain(level);
                break;
            case '\x17':
                // anti-VOX gain
                emit haveAntiVoxGain(level);
                break;

            default:
                qDebug(logRig()) << "Unknown control level (0x14) received at register " << payloadIn[1] << " with level " << level;
                break;

        }
        return;
    }

    if(payloadIn[0] == '\x15')
    {
        switch(payloadIn[1])
        {
            case '\x02':
                // S-Meter
                emit haveMeter(meterS, level);
                break;
            case '\x11':
                // RF-Power meter
                emit haveMeter(meterPower, level);
                break;
            case '\x12':
                // SWR
                emit haveMeter(meterSWR, level);
                break;
            case '\x13':
                // ALC
                emit haveMeter(meterALC, level);
                break;
            case '\x14':
                // COMP dB reduction
                emit haveMeter(meterComp, level);
                break;
            case '\x15':
                // VD (12V)
                emit haveMeter(meterVoltage, level);
                break;
            case '\x16':
                // ID
                emit haveMeter(meterCurrent, level);
                break;

            default:
                qDebug(logRig()) << "Unknown meter level (0x15) received at register " << payloadIn[1] << " with level " << level;
                break;
        }


    return;
    }

}

void rigCommander::setTxPower(unsigned char power)
{
    QByteArray payload("\x14\x0A");
    payload.append(bcdEncodeInt(power));
    prepDataAndSend(payload);
}

void rigCommander::setMicGain(unsigned char gain)
{
    QByteArray payload("\x14\x0B");
    payload.append(bcdEncodeInt(gain));
    prepDataAndSend(payload);
}

void rigCommander::getModInput(bool dataOn)
{
    setModInput(inputMic, dataOn, true);
}

void rigCommander::setModInput(rigInput input, bool dataOn)
{
    setModInput(input, dataOn, false);
}

void rigCommander::setModInput(rigInput input, bool dataOn, bool isQuery)
{
//    The input enum is as follows:

//    inputMic=0,
//    inputACC=1,
//    inputUSB=3,
//    inputLAN=5,
//    inputACCA,
//    inputACCB};

    QByteArray payload;
    QByteArray inAsByte;

    if(isQuery)
        input = inputMic;


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
        default:
            break;
    }
    if(dataOn)
    {
        payload[3] = payload[3] + 1;
    }

    if(isQuery)
    {
        payload.truncate(4);
    }

    prepDataAndSend(payload);

}

void rigCommander::setModInputLevel(rigInput input, unsigned char level)
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

void rigCommander::getModInputLevel(rigInput input)
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

QByteArray rigCommander::getUSBAddr()
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
    QByteArray payload = getUSBAddr();
    prepDataAndSend(payload);
}


void rigCommander::setUSBGain(unsigned char gain)
{
    QByteArray payload = getUSBAddr();
    payload.append(bcdEncodeInt(gain));
    prepDataAndSend(payload);
}

QByteArray rigCommander::getLANAddr()
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
        default:
            break;
    }

    return payload;
}

void rigCommander::getLANGain()
{
    QByteArray payload = getLANAddr();
    prepDataAndSend(payload);
}

void rigCommander::setLANGain(unsigned char gain)
{
    QByteArray payload = getLANAddr();
    payload.append(bcdEncodeInt(gain));
    prepDataAndSend(payload);
}

QByteArray rigCommander::getACCAddr(unsigned char ab)
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
        default:
            break;
    }

    return payload;
}

void rigCommander::getACCGain()
{
    QByteArray payload = getACCAddr(0);
    prepDataAndSend(payload);
}


void rigCommander::getACCGain(unsigned char ab)
{
    QByteArray payload = getACCAddr(ab);
    prepDataAndSend(payload);
}


void rigCommander::setACCGain(unsigned char gain)
{
    QByteArray payload = getACCAddr(0);
    payload.append(bcdEncodeInt(gain));
    prepDataAndSend(payload);
}

void rigCommander::setACCGain(unsigned char gain, unsigned char ab)
{
    QByteArray payload = getACCAddr(ab);
    payload.append(bcdEncodeInt(gain));
    prepDataAndSend(payload);
}

void rigCommander::setCompLevel(unsigned char compLevel)
{
    QByteArray payload("\x14\x0E");
    payload.append(bcdEncodeInt(compLevel));
    prepDataAndSend(payload);
}

void rigCommander::setMonitorLevel(unsigned char monitorLevel)
{
    QByteArray payload("\x14\x0E");
    payload.append(bcdEncodeInt(monitorLevel));
    prepDataAndSend(payload);
}

void rigCommander::setVoxGain(unsigned char gain)
{
    QByteArray payload("\x14\x16");
    payload.append(bcdEncodeInt(gain));
    prepDataAndSend(payload);
}

void rigCommander::setAntiVoxGain(unsigned char gain)
{
    QByteArray payload("\x14\x17");
    payload.append(bcdEncodeInt(gain));
    prepDataAndSend(payload);
}


void rigCommander::getRfGain()
{
    QByteArray payload("\x14\x02");
    prepDataAndSend(payload);
}

void rigCommander::getAfGain()
{
    QByteArray payload("\x14\x01");
    prepDataAndSend(payload);
}

void rigCommander::getSql()
{
    QByteArray payload("\x14\x03");
    prepDataAndSend(payload);
}

void rigCommander::getTxLevel()
{
    QByteArray payload("\x14\x0A");
    prepDataAndSend(payload);
}

void rigCommander::getMicGain()
{
    QByteArray payload("\x14\x0B");
    prepDataAndSend(payload);
}

void rigCommander::getCompLevel()
{
    QByteArray payload("\x14\x0E");
    prepDataAndSend(payload);
}

void rigCommander::getMonitorLevel()
{
    QByteArray payload("\x14\x15");
    prepDataAndSend(payload);
}

void rigCommander::getVoxGain()
{
    QByteArray payload("\x14\x16");
    prepDataAndSend(payload);
}

void rigCommander::getAntiVoxGain()
{
    QByteArray payload("\x14\x17");
    prepDataAndSend(payload);
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
//    getMonitorLevel(); // 0x15
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
    QByteArray payload("\x15\x02");
    prepDataAndSend(payload);
}

void rigCommander::getRFPowerMeter()
{
    QByteArray payload("\x15\x11");
    prepDataAndSend(payload);
}

void rigCommander::getSWRMeter()
{
    QByteArray payload("\x15\x12");
    prepDataAndSend(payload);
}

void rigCommander::getALCMeter()
{
    QByteArray payload("\x15\x13");
    prepDataAndSend(payload);
}

void rigCommander::getCompReductionMeter()
{
    QByteArray payload("\x15\x14");
    prepDataAndSend(payload);
}

void rigCommander::getVdMeter()
{
    QByteArray payload("\x15\x15");
    prepDataAndSend(payload);
}

void rigCommander::getIDMeter()
{
    QByteArray payload("\x15\x16");
    prepDataAndSend(payload);
}


void rigCommander::setSquelch(unsigned char level)
{
    sendLevelCmd(0x03, level);
}

void rigCommander::setRfGain(unsigned char level)
{
    sendLevelCmd(0x02, level);
}

void rigCommander::setAfGain(unsigned char level)
{
    if (udp == Q_NULLPTR) {
        sendLevelCmd(0x01, level);
    }
    else {
        emit haveSetVolume(level);
    }
}

void rigCommander::setRefAdjustCourse(unsigned char level)
{
    // 1A 05 00 72 0000-0255
    QByteArray payload;
    payload.setRawData("\x1A\x05\x00\x72", 4);
    payload.append(bcdEncodeInt((unsigned int)level));
    prepDataAndSend(payload);

}

void rigCommander::setRefAdjustFine(unsigned char level)
{
    qDebug(logRig()) << __FUNCTION__ << " level: " << level;
    // 1A 05 00 73 0000-0255
    QByteArray payload;
    payload.setRawData("\x1A\x05\x00\x73", 4);
    payload.append(bcdEncodeInt((unsigned int)level));
    prepDataAndSend(payload);
}

void rigCommander::sendLevelCmd(unsigned char levAddr, unsigned char level)
{
    QByteArray payload("\x14");
    payload.append(levAddr);
    // careful here. The value in the units and tens can't exceed 99.
    // ie, can't do this: 01 f2
    payload.append((int)level/100); // make sure it works with a zero
    // convert the tens:
    int tens = (level - 100*((int)level/100))/10;
    // convert the units:
    int units = level - 100*((int)level/100);
    units = units - 10*((int)(units/10));
    // combine and send:
    payload.append((tens << 4) | (units) ); // make sure it works with a zero

    prepDataAndSend(payload);
}

void rigCommander::getRefAdjustCourse()
{
    // 1A 05 00 72
    QByteArray payload;
    payload.setRawData("\x1A\x05\x00\x72", 4);
    prepDataAndSend(payload);
}

void rigCommander::getRefAdjustFine()
{
    // 1A 05 00 73
    QByteArray payload;
    payload.setRawData("\x1A\x05\x00\x73", 4);
    prepDataAndSend(payload);
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
            ritHz = f.Hz*((payloadIn.at(4)=='\x01')?-1:1);
            emit haveRitFrequency(ritHz);
            break;
        case '\x01':
            // RIT on/off
            if(payloadIn.at(02) == '\x01')
            {
                emit haveRitEnabled(true);
            } else {
                emit haveRitEnabled(false);
            }
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
    // qDebug(logRig()) << "Have ATU status from radio. Emitting.";
    // Expect:
    // [0]: 0x1c
    // [1]: 0x01
    // [2]: 0 = off, 0x01 = on, 0x02 = tuning in-progress
    emit haveATUStatus((unsigned char) payloadIn[2]);
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
    rigState.ptt = (unsigned char)payloadIn[2];

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
    // qDebug(logRig()) << "Looking at register 1A :";
    // printHex(payloadIn, false, true);

    // "INDEX: 00 01 02 03 04 "
    // "DATA:  1a 06 01 03 fd " (data mode enabled, filter width 3 selected)

    switch(payloadIn[01])
    {
        case '\x00':
            // Memory contents
            break;
        case '\x01':
            // band stacking register
            parseBandStackReg();
            break;
        case '\x06':
            // data mode
            // emit havedataMode( (bool) payloadIn[somebit])
            // index
            // 03 04
            // XX YY
            // XX = 00 (off) or 01 (on)
            // YY: filter selected, 01 through 03.;
            // if YY is 00 then XX was also set to 00
            emit haveDataMode((bool)payloadIn[03]);
            rigState.datamode = (bool)payloadIn[03];
            break;
        case '\x07':
            // IP+ status
            break;
        default:
            break;
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
            break;
        case '\x01':
            // "TSQL tone"
            tone = decodeTone(payloadIn);
            emit haveTSQL(tone);
            break;
        case '\x02':
            // DTCS (DCS)
            tone = decodeTone(payloadIn, tinv, rinv);
            emit haveDTCS(tone, tinv, rinv);
            break;
        case '\x07':
            // "CSQL code (DV mode)"
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
    switch(payloadIn.at(1))
    {
        case '\x5d':
            emit haveRptAccessMode((rptAccessTxRx)payloadIn.at(2));
            break;
        case '\x02':
            // Preamp
            emit havePreamp((unsigned char)payloadIn.at(2));
            break;
        default:
            break;
    }
}

void rigCommander::parseBandStackReg()
{
    // qDebug(logRig()) << "Band stacking register response received: ";
    // printHex(payloadIn, false, true);
    // Reference output, 20 meters, regCode 01 (latest):
    // "INDEX: 00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 "
    // "DATA:  1a 01 05 01 60 03 23 14 00 00 03 10 00 08 85 00 08 85 fd "
    // char band = payloadIn[2];
    // char regCode = payloadIn[3];
    freqt freqs = parseFrequency(payloadIn, 7);
    float freq = (float)freqs.MHzDouble;

    bool dataOn = (payloadIn[11] & 0x10) >> 4; // not sure...
    char mode = payloadIn[9];

    // 09, 10 mode
    // 11 digit RH: data mode on (1) or off (0)
    // 11 digit LH: CTCSS 0 = off, 1 = TONE, 2 = TSQL

    // 12, 13 : tone freq setting
    // 14, 15 tone squelch freq setting
    // if more, memory name (label) ascii

    // qDebug(logRig()) << "band: " << QString("%1").arg(band) << " regCode: " << (QString)regCode << " freq: " << freq;
    // qDebug(logRig()) << "mode: " << (QString)mode << " dataOn: " << dataOn;
    emit haveBandStackReg(freq, mode, dataOn);
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

    rigInput input;
    input = (rigInput)bcdHexToUChar(payloadIn[4]);
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
            return;
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
            qDebug(logRig()) << "Received 0x15 center span data: for frequency " << freqSpan.Hz;
            //printHex(payloadIn, false, true);
            break;
        case 0x16:
            // read edge mode center in edge mode
            emit haveScopeEdge((char)payloadIn[2]);
            qDebug(logRig()) << "Received 0x16 edge in center mode:";
            printHex(payloadIn, false, true);
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
            qDebug(logRig()) << "Unknown waveform data received: ";
            printHex(payloadIn, false, true);
            break;
    }
}

void rigCommander::determineRigCaps()
{
    //TODO: Determine available bands (low priority, rig will reject out of band requests anyway)

    std::vector <bandType> standardHF;
    std::vector <bandType> standardVU;

    // Most commonly supported "HF" bands:
    standardHF = {band6m, band10m, band10m, band12m,
                 band15m, band17m, band20m, band30m,
                 band40m, band60m, band80m, band160m};

    standardVU = {band70cm, band2m};


    rigCaps.model = model;
    rigCaps.modelID = model; // may delete later
    rigCaps.civ = incomingCIVAddr;

    rigCaps.hasDD = false;
    rigCaps.hasDV = false;
    rigCaps.hasATU = false;

    rigCaps.hasCTCSS = false;
    rigCaps.hasDTCS = false;

    rigCaps.spectSeqMax = 0;
    rigCaps.spectAmpMax = 0;
    rigCaps.spectLenMax = 0;

    // Clear inputs list in case we have re-connected.
    rigCaps.inputs.clear(); 
    rigCaps.inputs.append(inputMic);

    rigCaps.hasAttenuator = true; // Verify that all recent rigs have attenuators
    rigCaps.attenuators.push_back('\x00');
    rigCaps.hasPreamp = true;
    rigCaps.preamps.push_back('\x00');

    rigCaps.hasAntennaSel = false;

    rigCaps.hasTransmit = true;

    // Common, reasonable defaults for most supported HF rigs:
    rigCaps.bsr[band160m] = 0x01;
    rigCaps.bsr[band80m] = 0x02;
    rigCaps.bsr[band40m] = 0x03;
    rigCaps.bsr[band30m] = 0x04;
    rigCaps.bsr[band20m] = 0x05;
    rigCaps.bsr[band17m] = 0x06;
    rigCaps.bsr[band15m] = 0x07;
    rigCaps.bsr[band12m] = 0x08;
    rigCaps.bsr[band10m] = 0x09;
    rigCaps.bsr[band6m] = 0x10;
    rigCaps.bsr[bandGen] = 0x11;

    // Bands that seem to change with every model:
    rigCaps.bsr[band2m] = 0x00;
    rigCaps.bsr[band70cm] = 0x00;
    rigCaps.bsr[band23cm] = 0x00;

    // These bands generally aren't defined:
    rigCaps.bsr[band4m] = 0x00;
    rigCaps.bsr[band60m] = 0x00;
    rigCaps.bsr[bandWFM] = 0x00;
    rigCaps.bsr[bandAir] = 0x00;
    rigCaps.bsr[band630m] = 0x00;
    rigCaps.bsr[band2200m] = 0x00;

    switch(model){
        case model7300:
            rigCaps.modelName = QString("IC-7300");
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
            rigCaps.attenuators.push_back('\x20');
            rigCaps.preamps.push_back('\x01');
            rigCaps.preamps.push_back('\x02');
            rigCaps.bands = standardHF;
            rigCaps.bands.push_back(band4m);
            rigCaps.bands.push_back(bandGen);
            rigCaps.bands.push_back(band630m);
            rigCaps.bands.push_back(band2200m);
            break;
        case modelR8600:
            rigCaps.modelName = QString("IC-R8600");
            rigCaps.hasSpectrum = true;
            rigCaps.spectSeqMax = 11;
            rigCaps.spectAmpMax = 160;
            rigCaps.spectLenMax = 475;
            rigCaps.inputs.clear();
            rigCaps.hasLan = true;
            rigCaps.hasEthernet = true;
            rigCaps.hasWiFi = false;
            rigCaps.hasTransmit = false;
            rigCaps.hasCTCSS = true;
            rigCaps.hasDTCS = true;
            rigCaps.hasDV = true;
            rigCaps.attenuators.push_back('\x10');
            rigCaps.attenuators.push_back('\x20');
            rigCaps.attenuators.push_back('\x30');
            rigCaps.preamps.push_back('\x01');
            rigCaps.preamps.push_back('\x02');
            rigCaps.hasAntennaSel = true;
            rigCaps.antennas = {0x01, 0x02, 0x03};
            rigCaps.bands = standardHF;
            rigCaps.bands.insert(rigCaps.bands.end(), standardVU.begin(), standardVU.end());
            rigCaps.bands.insert(rigCaps.bands.end(), {band23cm, band4m, band630m, band2200m, bandGen});
            break;
        case model9700:
            rigCaps.modelName = QString("IC-9700");
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
            rigCaps.attenuators.push_back('\x10');
            rigCaps.preamps.push_back('\x01');
            rigCaps.bands = standardVU;
            rigCaps.bands.push_back(band23cm);
            rigCaps.bsr[band23cm] = 0x03;
            rigCaps.bsr[band70cm] = 0x02;
            rigCaps.bsr[band2m] = 0x01;
            break;
        case model7610:
            rigCaps.modelName = QString("IC-7610");
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
            rigCaps.attenuators.insert(rigCaps.attenuators.end(),
                                      {'\x03', '\x06', '\x09', '\x12',\
                                       '\x15', '\x18', '\x21', '\x24',\
                                       '\x27', '\x30', '\x33', '\x36',
                                       '\x39', '\x42', '\x45'});
            rigCaps.preamps.push_back('\x01');
            rigCaps.preamps.push_back('\x02');
            rigCaps.hasAntennaSel = true;
            rigCaps.antennas = {0x01, 0x02};
            rigCaps.hasATU = true;
            rigCaps.bands = standardHF;
            rigCaps.bands.push_back(bandGen);
            rigCaps.bands.push_back(band630m);
            rigCaps.bands.push_back(band2200m);
            break;
        case model7850:
            rigCaps.modelName = QString("IC-785x");
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
            rigCaps.attenuators.insert(rigCaps.attenuators.end(),
                                      {'\x03', '\x06', '\x09',
                                       '\x12', '\x15', '\x18', '\x21'});
            rigCaps.preamps.push_back('\x01');
            rigCaps.preamps.push_back('\x02');
            rigCaps.hasAntennaSel = true;
            rigCaps.antennas = {0x01, 0x02, 0x03, 0x04};
            rigCaps.bands = standardHF;
            rigCaps.bands.push_back(bandGen);
            rigCaps.bands.push_back(band630m);
            rigCaps.bands.push_back(band2200m);
            break;
        case model705:
            rigCaps.modelName = QString("IC-705");
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
            rigCaps.attenuators.insert(rigCaps.attenuators.end(),{ '\x10' , '\x20'});
            rigCaps.preamps.push_back('\x01');
            rigCaps.preamps.push_back('\x02');
            rigCaps.bands = standardHF;
            rigCaps.bands.insert(rigCaps.bands.end(), standardVU.begin(), standardVU.end());
            rigCaps.bands.push_back(bandGen);
            rigCaps.bands.push_back(bandAir);
            rigCaps.bands.push_back(bandWFM);
            rigCaps.bsr[band70cm] = 0x14;
            rigCaps.bsr[band2m] = 0x13;
            rigCaps.bsr[bandAir] = 0x12;
            rigCaps.bsr[bandWFM] = 0x11;
            rigCaps.bsr[bandGen] = 0x15;
            rigCaps.bands.push_back(band630m);
            rigCaps.bands.push_back(band2200m);
            break;
        case model7100:
            rigCaps.modelName = QString("IC-7100");
            rigCaps.hasSpectrum = false;
            rigCaps.inputs.append(inputUSB);
            rigCaps.inputs.append(inputACC);
            rigCaps.hasLan = false;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = false;
            rigCaps.hasATU = true;
            rigCaps.hasCTCSS = true;
            rigCaps.hasDTCS = true;
            rigCaps.attenuators.push_back('\x12');
            rigCaps.preamps.push_back('\x01');
            rigCaps.preamps.push_back('\x02');
            rigCaps.bands = standardHF;
            rigCaps.bands.insert(rigCaps.bands.end(), standardVU.begin(), standardVU.end());
            rigCaps.bands.push_back(band4m);
            rigCaps.bands.push_back(bandGen);
            rigCaps.bsr[band2m] = 0x11;
            rigCaps.bsr[band70cm] = 0x12;
            rigCaps.bsr[bandGen] = 0x13;
            break;
        case model706:
            rigCaps.modelName = QString("IC-706");
            rigCaps.hasSpectrum = false;
            rigCaps.inputs.clear();
            rigCaps.hasLan = false;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = false;
            rigCaps.hasATU = true;
            rigCaps.attenuators.push_back('\x20');
            rigCaps.bands = standardHF;
            rigCaps.bands.insert(rigCaps.bands.end(), standardVU.begin(), standardVU.end());
            rigCaps.bands.push_back(bandGen);
            break;
        default:
            rigCaps.modelName = QString("IC-RigID: 0x%1").arg(rigCaps.model, 0, 16);
            rigCaps.hasSpectrum = false;
            rigCaps.spectSeqMax = 0;
            rigCaps.spectAmpMax = 0;
            rigCaps.spectLenMax = 0;
            rigCaps.inputs.clear();
            rigCaps.hasLan = false;
            rigCaps.hasEthernet = false;
            rigCaps.hasWiFi = false;
            rigCaps.hasPreamp = false;
            rigCaps.hasAntennaSel = false;
            rigCaps.attenuators.push_back('\x10');
            rigCaps.attenuators.push_back('\x12');
            rigCaps.attenuators.push_back('\x20');
            rigCaps.bands = standardHF;
            rigCaps.bands.insert(rigCaps.bands.end(), standardVU.begin(), standardVU.end());
            rigCaps.bands.insert(rigCaps.bands.end(), {band23cm, band4m, band630m, band2200m, bandGen});
            qDebug(logRig()) << "Found unknown rig: " << rigCaps.modelName;
            break;
    }
    haveRigCaps = true;
    if(lookingForRig)
    {
        lookingForRig = false;
        foundRig = true;
#ifdef QT_DEBUG
        qDebug(logRig()) << "---Rig FOUND from broadcast query:";
#endif
        this->civAddr = incomingCIVAddr; // Override and use immediately.
        payloadPrefix = QByteArray("\xFE\xFE");
        payloadPrefix.append(civAddr);
        payloadPrefix.append((char)compCivAddr);
        // if there is a compile-time error, remove the following line, the "hex" part is the issue:
        qDebug(logRig()) << "Using incomingCIVAddr: (int): " << this->civAddr << " hex: " << hex << this->civAddr;
        emit discoveredRigID(rigCaps);
    } else {
        emit haveRigID(rigCaps);
    }
}

void rigCommander::parseSpectrum()
{
    if(!haveRigCaps)
    {
#ifdef QT_DEBUG
        qDebug(logRig()) << "Spectrum received in rigCommander, but rigID is incomplete.";
#endif
        return;
    }
    if(rigCaps.spectSeqMax == 0)
    {
        // there is a chance this will happen with rigs that support spectrum. Once our RigID query returns, we will parse correctly.
        qDebug(logRig()) << "Warning: Spectrum sequence max was zero, yet spectrum was received.";
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

    unsigned char sequence = bcdHexToUChar(payloadIn[03]);
    //unsigned char sequenceMax = bcdHexToDecimal(payloadIn[04]);


    // unsigned char waveInfo = payloadIn[06]; // really just one byte?
    //qDebug(logRig()) << "Spectrum Data received: " << sequence << "/" << sequenceMax << " mode: " << scopeMode << " waveInfo: " << waveInfo << " length: " << payloadIn.length();

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


        // wave information
        spectrumLine.clear();
        // For Fixed, and both scroll modes, the following produces correct information:
        fStart = parseFrequency(payloadIn, 9);
        spectrumStartFreq = fStart.MHzDouble;
        fEnd = parseFrequency(payloadIn, 14);
        spectrumEndFreq = fEnd.MHzDouble;
        if(scopeMode == spectModeCenter)
        {
            // "center" mode, start is actuall center, end is bandwidth.
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
        //qDebug(logRig()) << "sequence: " << sequence << "spec index: " << (sequence-2)*55 << " payloadPosition: " << payloadIn.length() - 5 << " payload length: " << payloadIn.length();
    } else if (sequence == rigCaps.spectSeqMax)
    {
        // last spectrum, a little bit different (last 25 pixels). Total at end is 475 pixels (7300).
        payloadIn.chop(1);
        spectrumLine.insert(spectrumLine.length(), payloadIn.right(payloadIn.length() - 5));
        //qDebug(logRig()) << "sequence: " << sequence << " spec index: " << (sequence-2)*55 << " payloadPosition: " << payloadIn.length() - 5 << " payload length: " << payloadIn.length();
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

    //unsigned char thousands = ((hundreds & 0xf0)>>4);
    unsigned char rtnVal;
    rtnVal = (hundreds & 0x0f)*100;
    rtnVal += ((tensunits & 0xf0)>>4)*10;
    rtnVal += (tensunits & 0x0f);
    //rtnVal += thousands * 1000;

    return rtnVal;
}

QByteArray rigCommander::bcdEncodeInt(unsigned int num)
{
    if(num > 9999)
    {
        qDebug(logRig()) << __FUNCTION__ << "Error, number is too big for four-digit conversion: " << num;
        return QByteArray();
    }

    char thousands = num / 1000;
    char hundreds = (num - (1000*thousands)) / 100;
    char tens = (num - (1000*thousands) - (100*hundreds)) / 10;
    char units = (num - (1000*thousands) - (100*hundreds) - (10*tens));

    char b0 = hundreds | (thousands << 4);
    char b1 = units | (tens << 4);

    //qDebug(logRig()) << __FUNCTION__ << " encoding value " << num << " as hex:";
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
    if(payloadIn.length() == 7)
    {
        // 7300 has these digits too, as zeros.
        // IC-705 or IC-9700 with higher frequency data available.
        frequencyMhz += 100*(payloadIn[05] & 0x0f);
        frequencyMhz += (1000*((payloadIn[05] & 0xf0) >> 4));

        freq.Hz += (payloadIn[05] & 0x0f) * 1E6 *          100;
        freq.Hz += ((payloadIn[05] & 0xf0) >> 4) * 1E6 *  1000;

    }

    freq.Hz += (payloadIn[04] & 0x0f) * 1E6;
    freq.Hz += ((payloadIn[04] & 0xf0) >> 4) * 1E6 *        10;

    frequencyMhz += payloadIn[04] & 0x0f;
    frequencyMhz += 10*((payloadIn[04] & 0xf0) >> 4);

    // KHz land:
    frequencyMhz += ((payloadIn[03] & 0xf0) >>4)/10.0 ;
    frequencyMhz += (payloadIn[03] & 0x0f) / 100.0;

    frequencyMhz += ((payloadIn[02] & 0xf0) >> 4) / 1000.0;
    frequencyMhz += (payloadIn[02] & 0x0f) / 10000.0;

    frequencyMhz += ((payloadIn[01] & 0xf0) >> 4) /  100000.0;
    frequencyMhz += (payloadIn[01] & 0x0f)        / 1000000.0;

    freq.Hz += payloadIn[01] & 0x0f;
    freq.Hz += ((payloadIn[01] & 0xf0) >> 4)*      10;

    freq.Hz  += (payloadIn[02] & 0x0f) *           100;
    freq.Hz  += ((payloadIn[02] & 0xf0) >> 4) *   1000;

    freq.Hz  += (payloadIn[03] & 0x0f) *         10000;
    freq.Hz  += ((payloadIn[03] & 0xf0) >>4) *  100000;

    freq.MHzDouble = frequencyMhz;

    rigState.vfoAFreq = freq;
    emit haveFrequency(freq);
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

    // TODO: 64-bit value is incorrect, multiplying by wrong numbers.

    float freq = 0.0;

    freqt freqs;
    freqs.MHzDouble = 0;
    freqs.Hz = 0;

    // MHz:
    freq += 100*(data[lastPosition+1] & 0x0f);
    freq += (1000*((data[lastPosition+1] & 0xf0) >> 4));

    freq += data[lastPosition] & 0x0f;
    freq += 10*((data[lastPosition] & 0xf0) >> 4);

    freqs.Hz += (data[lastPosition] & 0x0f) * 1E6;
    freqs.Hz += ((data[lastPosition] & 0xf0) >> 4) * 1E6 *     10; //   10 MHz

    if(data.length() >= lastPosition+1)
    {
        freqs.Hz += (data[lastPosition+1] & 0x0f) * 1E6 *         100; //  100 MHz
        freqs.Hz += ((data[lastPosition+1] & 0xf0) >> 4) * 1E6 * 1000; // 1000 MHz
    }


    // Hz:
    freq += ((data[lastPosition-1] & 0xf0) >>4)/10.0 ;
    freq += (data[lastPosition-1] & 0x0f) / 100.0;

    freq += ((data[lastPosition-2] & 0xf0) >> 4) / 1000.0;
    freq += (data[lastPosition-2] & 0x0f) / 10000.0;

    freq += ((data[lastPosition-3] & 0xf0) >> 4) / 100000.0;
    freq += (data[lastPosition-3] & 0x0f) / 1000000.0;

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
    unsigned char filter;
    if(payloadIn[2] != '\xFD')
    {
        filter = payloadIn[2];
    } else {
        filter = 0;
    }
    rigState.mode = (unsigned char)payloadIn[01];
    rigState.filter = filter;
    emit haveMode((unsigned char)payloadIn[01], filter);
}


void rigCommander::startATU()
{
    QByteArray payload("\x1C\x01\x02");
    prepDataAndSend(payload);
}

void rigCommander::setATU(bool enabled)
{
    QByteArray payload;

    if(enabled)
    {
        payload.setRawData("\x1C\x01\x01", 3);
    } else {
        payload.setRawData("\x1C\x01\x00", 3);
    }
    prepDataAndSend(payload);
}

void rigCommander::getATUStatus()
{
    //qDebug(logRig()) << "Sending out for ATU status in RC.";
    QByteArray payload("\x1C\x01");
    prepDataAndSend(payload);
}

void rigCommander::getAttenuator()
{
    QByteArray payload("\x11");
    prepDataAndSend(payload);
}

void rigCommander::getPreamp()
{
    QByteArray payload("\x16\x02");
    prepDataAndSend(payload);
}

void rigCommander::getAntenna()
{
    // This one might neet some thought
    // as it seems each antenna has to be checked.
    // Maybe 0x12 alone will do it.
    QByteArray payload("\x12");
    prepDataAndSend(payload);
}

void rigCommander::setAttenuator(unsigned char att)
{
    QByteArray payload("\x11");
    payload.append(att);
    prepDataAndSend(payload);
}

void rigCommander::setPreamp(unsigned char pre)
{
    QByteArray payload("\x16\x02");
    payload.append(pre);
    prepDataAndSend(payload);
}

void rigCommander::setAntenna(unsigned char ant)
{
    QByteArray payload("\x12");
    payload.append(ant);
    payload.append("\x01"); // "on", presumably the other ones turn off...
    prepDataAndSend(payload);
}

void rigCommander::getRigID()
{
    QByteArray payload;
    payload.setRawData("\x19\x00", 2);
    prepDataAndSend(payload);
}

void rigCommander::changeLatency(const quint16 value)
{
    emit haveChangeLatency(value);
}

void rigCommander::sayAll()
{
    QByteArray payload;
    payload.setRawData("\x13\x00", 2);
    prepDataAndSend(payload);
}

void rigCommander::sayFrequency()
{
    QByteArray payload;
    payload.setRawData("\x13\x01", 2);
    prepDataAndSend(payload);
}

void rigCommander::sayMode()
{
    QByteArray payload;
    payload.setRawData("\x13\x02", 2);
    prepDataAndSend(payload);
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
    //qDebug(logRig()) << "emit dataForComm()";
    emit dataForComm(data);
}








