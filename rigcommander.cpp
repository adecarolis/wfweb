#include "rigcommander.h"
#include <QDebug>

#include "rigidentities.h"

// Copytight 2017,2018 Elliott H. Liggett

// This file parses data from the radio and also forms commands to the radio.
// The radio physical interface is handled by the commHandler() instance "comm"

// TODO:
//      + Allow parameters to pass to the commHandler indicating which serial port to use
//      + Impliment additional commands (of course)
//      + Impliment external serial port "pass through"
//      + Impliment XML RPC server?
//      + Grab initial state of band scope and adjust UI accordingly.

//
// See here for a wonderful CI-V overview:
// http://www.plicht.de/ekki/civ/civ-p0a.html
//
// The IC-7300 "full" manual also contains a command reference.

// How to make spectrum display stop using rigctl:
//  echo "w \0xFE\0xFE\0x94\0xE0\0x27\0x11\0x00\0xFD" | rigctl -m 373 -r /dev/ttyUSB0 -s 115200 -vvvvv

// Note: When sending \x00, must use QByteArray.setRawData()


rigCommander::rigCommander(unsigned char rigCivAddr, QString rigSerialPort)
{
    // construct
    // TODO: Bring this parameter and the comm port from the UI.
    // Keep in hex in the UI as is done with other CIV apps.

    // civAddr = 0x94; // address of the radio. Decimal is 148.
    civAddr = rigCivAddr; // address of the radio. Decimal is 148.

    setCIVAddr(civAddr);
    //compCivAddr = 0xE1;
    //payloadPrefix = QByteArray("\xFE\xFE\x94\xE0");
    payloadPrefix = QByteArray("\xFE\xFE");
    payloadPrefix.append(civAddr);
    payloadPrefix.append(compCivAddr);

    // payloadPrefix.append("\xE0");

    payloadSuffix = QByteArray("\xFD");
    // TODO: list full contents of /dev/serial, grep for IC-7300
    // /dev/serial/by-path$ ls
    //     total 0
    //    lrwxrwxrwx 1 root root 13 Nov 24 21:43 pci-0000:00:12.0-usb-0:2.1:1.0-port0 -> ../../ttyUSB0

    // comm = new commHandler("/dev/ttyUSB0");
    comm = new commHandler(rigSerialPort);

    // data from the comm port to the program:
    connect(comm, SIGNAL(haveDataFromPort(QByteArray)), this, SLOT(handleNewData(QByteArray)));

    // data from the program to the comm port:
    connect(this, SIGNAL(dataForComm(QByteArray)), comm, SLOT(receiveDataFromUserToRig(QByteArray)));

    connect(this, SIGNAL(getMoreDebug()), comm, SLOT(debugThis()));
    pttAllowed = true; // This is for developing, set to false for "safe" debugging. Set to true for deployment.
}

rigCommander::~rigCommander()
{
    delete comm;
}

void rigCommander::process()
{
    // new thread enters here. Do nothing.
}

void rigCommander::prepDataAndSend(QByteArray data)
{
    data.prepend(payloadPrefix);
    //printHex(data, false, true);
    data.append(payloadSuffix);
#ifdef QT_DEBUG
    qDebug() << "Final payload in rig commander to be sent to rig: ";
    printHex(data, false, true);
#endif
    emit dataForComm(data);
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

void rigCommander::setSpectrumBounds()
{

}

void rigCommander::setScopeEdge(char edge)
{
    // 1 2 or 3
    // 27 16 00 0X
    if((edge <1) || (edge >3))
        return;
    QByteArray payload;
    payload.setRawData("\x27\x16\x00", 3);
    payload.append(edge);
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
    printHex(payload, false, true);
    prepDataAndSend(payload);
}

void rigCommander::setSpectrumCenteredMode(bool centerEnable)
{
    QByteArray specModePayload;
    if(centerEnable)
    {
        specModePayload.setRawData("\x27\x14\x00\x00", 4);
    } else {
        specModePayload.setRawData("\x27\x14\x00\x01", 4);
    }
    prepDataAndSend(specModePayload);
}

void rigCommander::setFrequency(double freq)
{
    QByteArray freqPayload = makeFreqPayload(freq);
    QByteArray cmdPayload;

    cmdPayload.append(freqPayload);
    cmdPayload.prepend('\x00');

    //printHex(cmdPayload, false, true);
    prepDataAndSend(cmdPayload);
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
    //qDebug() << "encoded frequency for " << freq << " as int " << freqInt;
    //printHex(result, false, true);
    return result;

}

void rigCommander::setMode(char mode)
{
    QByteArray payload;
    if((mode >=0) && (mode < 10))
    {
        // annoying hack as mode 6 is undefined.
        if(mode > 5)
        {
            mode++;
        }

        // valid
        payload.setRawData("\x06", 1); // cmd 06 needs filter specified
        //payload.setRawData("\x04", 1); // cmd 04 will apply the default filter, but it seems to always pick FIL 02

        payload.append(mode);
        payload.append("\x03"); // wide band
        prepDataAndSend(payload);
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

void rigCommander::getPTT()
{
    QByteArray payload("\x1C\x00", 2);
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
    }
}


void rigCommander::setCIVAddr(unsigned char civAddr)
{
    // Note: This is the radio's CIV address
    // the computer's CIV address is defined in the header file.
    this->civAddr = civAddr;
}

void rigCommander::handleNewData(const QByteArray &data)
{
    parseData(data);
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
    // qDebug() << "data list has this many elements: " << dataList.size();
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

        //qDebug() << "Data received: ";
        //printHex(data, false, true);
        if(data.length() < 4)
        {
            if(data.length())
            {
                // Finally this almost never happens
                // qDebug() << "Data length too short: " << data.length() << " bytes. Data:";
                //printHex(data, false, true);
            }
            // no
            //return;
            // maybe:
            // continue;
        }

        if(!data.startsWith("\xFE\xFE"))
        {
            // qDebug() << "Warning: Invalid data received, did not start with FE FE.";
            // find 94 e0 and shift over,
            // or look inside for a second FE FE
            // Often a local echo will miss a few bytes at the beginning.
            if(data.startsWith('\xFE'))
            {
                data.prepend('\xFE');
                // qDebug() << "Warning: Working with prepended data stream.";
                parseData(payloadIn);
                return;
            } else {
                //qDebug() << "Error: Could not reconstruct corrupted data: ";
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
            // qDebug() << "[FOUND] Trimmed off echo:";
            //printHex(payloadIn, false, true);
            //parseData(payloadIn);
            //return;
        }

        switch(data[02])
        {
            //    case civAddr: // can't have a variable here :-(
            //        // data is or begins with an echoback from what we sent
            //        // find the first 'fd' and cut it. Then continue.
            //        payloadIn = data.right(data.length() - data.indexOf('\xfd')-1);
            //        //qDebug() << "Trimmed off echo:";
            //        //printHex(payloadIn, false, true);
            //        parseData(payloadIn);
            //        break;
            // case '\xE0':

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
                payloadIn = data.right(data.length() - 4);
                parseCommand();
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
        qDebug() << "Recovered " << count << " frames from single data with size" << dataList.count();
    }
    */
}

void rigCommander::parseCommand()
{
    // note: data already is trimmed of the beginning FE FE E0 94 stuff.

#ifdef QT_DEBUG
    if(payloadIn[00] != '\x27')
    {
        // debug only
        printHex(payloadIn, false, true);
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
        case '\x01':
            //qDebug() << "Have mode data";
            this->parseMode();
            break;
        case '\x04':
            //qDebug() << "Have mode data";
            this->parseMode();
            break;
        case '\x05':
            //qDebug() << "Have mode data";
            this->parseMode();
            break;
        case '\x06':
            //qDebug() << "Have mode data";
            this->parseMode();
            break;
        case '\x14':
            // read levels
            parseLevels();
            break;
        case '\x19':
            // qDebug() << "Have rig ID: " << (unsigned int)payloadIn[2];
            // printHex(payloadIn, false, true);
            model = determineRadioModel(payloadIn[2]);
            break;
        case '\x27':
            // scope data
            //qDebug() << "Have scope data";
            //printHex(payloadIn, false, true);
            parseSpectrum();
            break;
        case '\x1A':
            if(payloadIn[01] == '\x05')
            {
                parseDetailedRegisters1A05();
            } else {
                parseRegisters1A();
            }
            break;
        case '\x1C':
            parseRegisters1C();
            break;
        case '\xFB':
            // Fine Business, ACK from rig.
            break;
        case '\xFA':
            // error
            qDebug() << "Error (FA) received from rig.";
            printHex(payloadIn, false ,true);
            break;

        default:
            qDebug() << "Have other data with cmd: " << std::hex << payloadIn[00];
            printHex(payloadIn, false, true);
            break;
    }
    // is any payload left?

}

void rigCommander::parseLevels()
{
    //qDebug() << "Received a level status readout: ";
    // printHex(payloadIn, false, true);

    // wrong: unsigned char level = (payloadIn[2] * 100) + payloadIn[03];
    unsigned char hundreds = payloadIn[2];
    unsigned char tens = (payloadIn[3] & 0xf0) >> 4;
    unsigned char units = (payloadIn[3] & 0x0f);

    unsigned char level = (100*hundreds) + (10*tens) + units;

    //qDebug() << "Level is: " << (int)level << " or " << 100.0*level/255.0 << "%";

    // Typical RF gain response (rather low setting):
    // "INDEX: 00 01 02 03 04 "
    // "DATA:  14 02 00 78 fd "

    switch(payloadIn[1])
    {
        case '\x01':
            // AF level
            emit haveAfGain(level);
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
    }
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
    // Squelch
    QByteArray payload("\x14\x03");
    prepDataAndSend(payload);
}

void rigCommander::setRfGain(unsigned char level)
{
    sendLevelCmd(0x02, level);
}

void rigCommander::setAfGain(unsigned char level)
{
    sendLevelCmd(0x01, level);
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

void rigCommander::parseRegisters1C()
{
    // PTT lives here
    // Not sure if 02 is the right place to switch.
    // TODO: test this function
    switch(payloadIn[02])
    {
        case '\x00':
            parsePTT();
            break;
        case '\x01':
            parseATU();
            // ATU status
            break;
        default:
            break;
    }
}

void rigCommander::parseATU()
{
    // TODO
    // emit haveATUStatus(payload [??] );
}

void rigCommander::parsePTT()
{
    // read after payloadIn[02]

    // Because I'm not sure about this:
    qDebug() << "PTT status received, here is the hex dump:";
    printHex(payloadIn, false, true);
    if(payloadIn[03] == (char)0)
    {
        // PTT off
        emit havePTTStatus(false);
    } else {
        // PTT on
        emit havePTTStatus(true);
    }

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
    // qDebug() << "Looking at register 1A :";
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
            break;
        case '\x07':
            // IP+ status
            break;
        default:
            break;
    }
}

void rigCommander::parseBandStackReg()
{
    // qDebug() << "Band stacking register response received: ";
    // printHex(payloadIn, false, true);
    // Reference output, 20 meters, regCode 01 (latest):
    // "INDEX: 00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 "
    // "DATA:  1a 01 05 01 60 03 23 14 00 00 03 10 00 08 85 00 08 85 fd "
    // char band = payloadIn[2];
    // char regCode = payloadIn[3];
    float freq = parseFrequency(payloadIn, 7);
    bool dataOn = (payloadIn[11] & 0x10) >> 4; // not sure...
    char mode = payloadIn[9];

    // 09, 10 mode
    // 11 digit RH: data mode on (1) or off (0)
    // 11 digit LH: CTCSS 0 = off, 1 = TONE, 2 = TSQL

    // 12, 13 : tone freq setting
    // 14, 15 tone squelch freq setting
    // if more, memory name (label) ascii

    // qDebug() << "band: " << QString("%1").arg(band) << " regCode: " << (QString)regCode << " freq: " << freq;
    // qDebug() << "mode: " << (QString)mode << " dataOn: " << dataOn;
    emit haveBandStackReg(freq, mode, dataOn);
}

void rigCommander::parseDetailedRegisters1A05()
{
    // It seems a lot of misc stuff is under this command and subcommand.
}

void rigCommander::parseSpectrum()
{
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

    unsigned char sequence = bcdHexToDecimal(payloadIn[03]);
    //unsigned char sequenceMax = bcdHexToDecimal(payloadIn[04]);
    unsigned char scopeMode = bcdHexToDecimal(payloadIn[05]);
    // unsigned char waveInfo = payloadIn[06]; // really just one byte?
    //qDebug() << "Spectrum Data received: " << sequence << "/" << sequenceMax << " mode: " << scopeMode << " waveInfo: " << waveInfo << " length: " << payloadIn.length();

    // Sequnce 2, index 05 is the start of data
    // Sequence 11. index 05, is the last chunk
    // Sequence 11, index 29, is the actual last pixel (it seems)

    // It looks like the data length may be variable, so we need to detect it each time.
    // start at payloadIn.length()-1 (to override the FD). Never mind, index -1 bad.
    // chop off FD.
    if(sequence == 1)
    {
        // wave information
        spectrumLine.clear();
        // parseFrequency(endPosition); // overload does not emit! Return? Where? how...
        spectrumStartFreq = parseFrequency(payloadIn, 9);
        spectrumEndFreq = parseFrequency(payloadIn, 14);
        if(scopeMode == 0)
        {
            // "center" mode, start is actuall center, end is bandwidth.
            spectrumStartFreq -= spectrumEndFreq;
            spectrumEndFreq = spectrumStartFreq + 2*(spectrumEndFreq);
        }
    } else if ((sequence > 1) && (sequence < 11))
    {
        // spectrum from index 05 to index 54, length is 55 per segment. Length is 56 total. Pixel data is 50 pixels.
        // sequence numbers 2 through 10, 50 pixels each. Total after sequence 10 is 450 pixels.
        payloadIn.chop(1);
        spectrumLine.insert(spectrumLine.length(), payloadIn.right(payloadIn.length() - 5)); // write over the FD, last one doesn't, oh well.
        //qDebug() << "sequence: " << sequence << "spec index: " << (sequence-2)*55 << " payloadPosition: " << payloadIn.length() - 5 << " payload length: " << payloadIn.length();
    } else if (sequence == 11)
    {
        // last spectrum, a little bit different (last 25 pixels). Total at end is 475 pixels.
        payloadIn.chop(1);
        spectrumLine.insert(spectrumLine.length(), payloadIn.right(payloadIn.length() - 5));
        //qDebug() << "sequence: " << sequence << " spec index: " << (sequence-2)*55 << " payloadPosition: " << payloadIn.length() - 5 << " payload length: " << payloadIn.length();
        emit haveSpectrumData(spectrumLine, spectrumStartFreq, spectrumEndFreq);
    }

    /*
    if(spectrumLine.length() != 475)
    {
        qDebug() << "Unusual length spectrum: " << spectrumLine.length();
        printHex(spectrumLine, false, true);
    }
    */
}

unsigned char rigCommander::bcdHexToDecimal(unsigned char in)
{
    unsigned char out = 0;
    out = in & 0x0f;
    out += ((in & 0xf0) >> 4)*10;
    return out;
}

void rigCommander::parseFrequency()
{
    // process payloadIn, which is stripped.
    // float frequencyMhz
    //    payloadIn[04] = ; // XX MHz
    //    payloadIn[03] = ; //   XX0     KHz
    //    payloadIn[02] = ; //     X.X   KHz
    //    payloadIn[01] = ; //      . XX KHz

    // printHex(payloadIn, false, true);
    frequencyMhz = payloadIn[04] & 0x0f;
    frequencyMhz += 10*((payloadIn[04] & 0xf0) >> 4);

    frequencyMhz += ((payloadIn[03] & 0xf0) >>4)/10.0 ;
    frequencyMhz += (payloadIn[03] & 0x0f) / 100.0;

    frequencyMhz += ((payloadIn[02] & 0xf0) >> 4) / 1000.0;
    frequencyMhz += (payloadIn[02] & 0x0f) / 10000.0;

    frequencyMhz += ((payloadIn[01] & 0xf0) >> 4) / 100000.0;
    frequencyMhz += (payloadIn[01] & 0x0f) / 1000000.0;

    emit haveFrequency(frequencyMhz);
}

float rigCommander::parseFrequency(QByteArray data, unsigned char lastPosition)
{
    // process payloadIn, which is stripped.
    // float frequencyMhz
    //    payloadIn[04] = ; // XX MHz
    //    payloadIn[03] = ; //   XX0     KHz
    //    payloadIn[02] = ; //     X.X   KHz
    //    payloadIn[01] = ; //      . XX KHz

    //printHex(data, false, true);

    float freq = 0.0;

    freq = data[lastPosition] & 0x0f;
    freq += 10*((data[lastPosition] & 0xf0) >> 4);

    freq += ((data[lastPosition-1] & 0xf0) >>4)/10.0 ;
    freq += (data[lastPosition-1] & 0x0f) / 100.0;

    freq += ((data[lastPosition-2] & 0xf0) >> 4) / 1000.0;
    freq += (data[lastPosition-2] & 0x0f) / 10000.0;

    freq += ((data[lastPosition-3] & 0xf0) >> 4) / 100000.0;
    freq += (data[lastPosition-3] & 0x0f) / 1000000.0;

    return freq;
}


void rigCommander::parseMode()
{
    QString mode;
    // LSB:
    //"INDEX: 00 01 02 03 "
    //"DATA:  01 00 02 fd "

    // USB:
    //"INDEX: 00 01 02 03 "
    //"DATA:  01 01 02 fd "
    switch(payloadIn[01])
    {
        case '\x00':
            mode = "LSB";
            break;
        case '\x01':
            mode = "USB";
            break;
        case '\x02':
            mode = "AM";
            break;
        case '\x03':
            mode = "CW";
            break;
        case '\x04':
            mode = "RTTY";
            break;
        case '\x05':
            mode = "FM";
            break;
        case '\x07':
            mode = "CW-R";
            break;
        case '\x08':
            mode = "RTTY-R";
            break;
        default:
            qDebug() << "Mode: Unknown: " << payloadIn[01];
            printHex(payloadIn, false, true);
            mode = QString("");
    }

    emit haveMode(mode);
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
    QByteArray payload("\x1C\x01");
    prepDataAndSend(payload);
}

void rigCommander::getRigID()
{
    QByteArray payload;
    payload.setRawData("\x19\x00", 2);
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

void rigCommander::printHex(const QByteArray &pdata, bool printVert, bool printHoriz)
{
    qDebug() << "---- Begin hex dump -----:";
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
            qDebug() << strings.at(i);
        }
    }

    if(printHoriz)
    {
        qDebug() << index;
        qDebug() << sdata;
    }
    qDebug() << "----- End hex dump -----";
}








