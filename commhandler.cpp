#include "commhandler.h"
#include "logcategories.h"

#include <QDebug>

// Copyright 2017-2020 Elliott H. Liggett

commHandler::commHandler(QObject* parent) : QObject(parent)
{
    //constructor
    // TODO: The following should become arguments and/or functions
    // Add signal/slot everywhere for comm port setup.
    // Consider how to "re-setup" and how to save the state for next time.
    baudrate = 115200;
    stopbits = 1;
    portName = "/dev/ttyUSB0";
    this->PTTviaRTS = false;

    init();
}

commHandler::commHandler(QString portName, quint32 baudRate, quint8 wfFormat, QObject* parent) : QObject(parent)
{
    //constructor
    // grab baud rate and other comm port details
    // if they need to be changed later, please
    // destroy this and create a new one.


    if (wfFormat == 1) { // Single waterfall packet
        combineWf = true;
        qDebug(logSerial()) << "*********** Combine Waterfall Mode Enabled!";
    }

    // TODO: The following should become arguments and/or functions
    // Add signal/slot everywhere for comm port setup.
    // Consider how to "re-setup" and how to save the state for next time.
    baudrate = baudRate;
    stopbits = 1;
    this->portName = portName;
    this->PTTviaRTS = false;
    init();

}

void commHandler::init()
{
    if (port != Q_NULLPTR) {
        delete port;
        port = Q_NULLPTR;
        isConnected = false;
    }

    port = new QSerialPort();
    setupComm(); // basic parameters
    openPort();
    // qInfo(logSerial()) << "Serial buffer size: " << port->readBufferSize();
    //port->setReadBufferSize(1024); // manually. 256 never saw any return from the radio. why...
    //qInfo(logSerial()) << "Serial buffer size: " << port->readBufferSize();

    connect(port, SIGNAL(readyRead()), this, SLOT(receiveDataIn()));
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    connect(port, SIGNAL(error(QSerialPort::SerialPortError)), this, SLOT(handleError(QSerialPort::SerialPortError)));
#else
    connect(port, SIGNAL(errorOccurred(QSerialPort::SerialPortError)), this, SLOT(handleError(QSerialPort::SerialPortError)));
#endif
    lastDataReceived = QTime::currentTime();
}

commHandler::~commHandler()
{
    qInfo(logSerial()) << "Closing serial port: " << port->portName();
    if (isConnected) {
        this->closePort();
    }
    delete port;
}

void commHandler::setupComm()
{
    serialError = false;
    port->setPortName(portName);
    port->setBaudRate(baudrate);
    port->setStopBits(QSerialPort::OneStop);// OneStop is other option
}

void commHandler::receiveDataFromUserToRig(const QByteArray &data)
{
    sendDataOut(data);
}

void commHandler::sendDataOut(const QByteArray &writeData)
{
    // Recycle port to attempt reconnection.
    if (lastDataReceived.msecsTo(QTime::currentTime()) > 2000) {
        qDebug(logSerial()) << "Serial port error? Attempting reconnect...";
        lastDataReceived = QTime::currentTime();
        QTimer::singleShot(500, this, SLOT(init()));
        return;
    }

    mutex.lock();

    qint64 bytesWritten;

    previousSent = writeData;

    if(PTTviaRTS)
    {
        // Size:    1  2  3    4    5    6    7    8
        //index:    0  1  2    3    4    5    6    7
        //Query:   FE FE TO FROM 0x1C 0x00 0xFD
        //PTT On:  FE FE TO FROM 0x1C 0x00 0x01 0xFD
        //PTT Off: FE FE TO FROM 0x1C 0x00 0x00 0xFD

        if(writeData.endsWith(QByteArrayLiteral("\x1C\x00\xFD")))
        {
            // Query
            //qDebug(logSerial()) << "Looks like PTT Query";
            bool pttOn = this->rtsStatus();
            QByteArray pttreturncmd = QByteArray("\xFE\xFE");
            pttreturncmd.append(writeData.at(3));
            pttreturncmd.append(writeData.at(2));
            pttreturncmd.append(QByteArray("\x1C\x00", 2));
            pttreturncmd.append((char)pttOn);
            pttreturncmd.append("\xFD");
            //qDebug(logSerial()) << "Sending fake PTT query result: " << (bool)pttOn;
            printHex(pttreturncmd, false, true);
            emit haveDataFromPort(pttreturncmd);

            
            mutex.unlock();
            return;
        } else if(writeData.endsWith(QByteArrayLiteral("\x1C\x00\x01\xFD")))
        {
            // PTT ON
            //qDebug(logSerial()) << "Looks like PTT ON";
            setRTS(true);
            mutex.unlock();
            return;
        } else if(writeData.endsWith(QByteArrayLiteral("\x1C\x00\x00\xFD")))
        {
            // PTT OFF
            //qDebug(logSerial()) << "Looks like PTT OFF";
            setRTS(false);
            mutex.unlock();
            return;
        }
    }

    bytesWritten = port->write(writeData);
    
    if(bytesWritten != (qint64)writeData.size())
    {
    qDebug(logSerial()) << "bytesWritten: " << bytesWritten << " length of byte array: " << writeData.length()\
             << " size of byte array: " << writeData.size()\
             << " Wrote all bytes? " << (bool) (bytesWritten == (qint64)writeData.size());
    }

    mutex.unlock();
}

void commHandler::receiveDataIn()
{
    // connected to comm port data signal

    // Here we get a little specific to CIV radios
    // because we know what constitutes a valid "frame" of data.

    // new code:
    lastDataReceived = QTime::currentTime();
    port->startTransaction();
    inPortData = port->readAll();

    if (inPortData.startsWith("\xFC\xFC\xFC\xFC\xFC"))
    {
        // Colission detected by remote end, re-send previous command.
        qInfo(logSerial()) << "Collision detected by remote, resending previous command";
        port->commitTransaction();
        sendDataOut(previousSent);
        return;
    }

    if(inPortData.size() == 1)
    {
        // Generally for baud <= 9600
        if (inPortData == "\xFE")
        {
            // This will get hit twice.
            // After the FE FE, we transition into
            // the normal .startsWith FE FE block
            // where the normal rollback code can handle things.
            port->rollbackTransaction();
            rolledBack = true;
            return;
        }
    }

    if (inPortData.startsWith("\xFE\xFE"))
    {
        if(inPortData.contains("\xFC"))
        {
            //qInfo(logSerial()) << "Transaction contains collision data. Dumping.";
            //printHex(inPortData, false, true);
            port->commitTransaction();
            return;
        }
        if(inPortData.endsWith("\xFD"))
        {
            // good!
            port->commitTransaction();

            //payloadIn = data.right(data.length() - 4);

            // Do we need to combine waterfall into single packet?
            int combined = 0;
            if (combineWf) {
                int pos = inPortData.indexOf(QByteArrayLiteral("\x27\x00\x00"));
                int fdPos = inPortData.mid(pos).indexOf(QByteArrayLiteral("\xfd"));
                //printHex(inPortData, false, true);
                while (pos > -1 && fdPos > -1) {
                    combined++;
                    spectrumDivisionNumber = inPortData[pos + 3] & 0x0f;
                    spectrumDivisionNumber += ((inPortData[pos + 3] & 0xf0) >> 4) * 10;

                    if (spectrumDivisionNumber == 1) {
                        // This is the first waveform data.
                        spectrumDivisionMax = 0;
                        spectrumDivisionMax = inPortData[pos + 4] & 0x0f;
                        spectrumDivisionMax += ((inPortData[pos + 4] & 0xf0) >> 4) * 10;
                        spectrumData.clear();
                        spectrumData = inPortData.mid(pos - 4, fdPos+4); // Don't include terminating FD
                        spectrumData[8] = spectrumData[7]; // Make max = current;
                        //qDebug() << "New Spectrum seq:" << spectrumDivisionNumber << "pos = " << pos << "len" << fdPos;

                    }
                    else  if (spectrumDivisionNumber > lastSpectrum && spectrumDivisionNumber <= spectrumDivisionMax) {
                        spectrumData.insert(spectrumData.length(), inPortData.mid(pos + 5, fdPos-5));
                        //qInfo() << "Added spectrum seq:" << spectrumDivisionNumber << "len" << fdPos-5<< "Spec" << spectrumData.length();
                        //printHex(inPortData.mid((pos+4),fdPos - (pos+5)), false, true);
                    }
                    else {
                        qDebug() << "Invalid Spectrum Division received" << spectrumDivisionNumber << "last Spectrum" << lastSpectrum;
                    }

                    lastSpectrum = spectrumDivisionNumber;
                        
                    if (spectrumDivisionNumber == spectrumDivisionMax) {
                        //qDebug() << "Got Spectrum! length=" << spectrumData.length();
                        spectrumData.append("\xfd"); // Need to add FD on the end.
                        //printHex(spectrumData, false, true);
                        emit haveDataFromPort(spectrumData);
                        lastSpectrum = 0;
                    }
                    inPortData = inPortData.remove(pos-4, fdPos+5);
                    pos = inPortData.indexOf(QByteArrayLiteral("\x27\x00\x00"));
                    fdPos = inPortData.mid(pos).indexOf(QByteArrayLiteral("\xfd"));
                }
                // If we still have data left, let the main function deal with it, any spectrum data has been removed
                if (inPortData.length() == 0)
                {
                    return;
                }
               // qDebug() << "Got extra data!";
                //printHex(inPortData, false, true);
            }
            // 
            emit haveDataFromPort(inPortData);

            if(rolledBack)
            {
                // qInfo(logSerial()) << "Rolled back and was successful. Length: " << inPortData.length();
                //printHex(inPortData, false, true);
                rolledBack = false;
            }
        } else {
            // did not receive the entire thing so roll back:
            // qInfo(logSerial()) << "Rolling back transaction. End not detected. Length: " << inPortData.length();
            //printHex(inPortData, false, true);
            port->rollbackTransaction();
            rolledBack = true;
        }
    } else {
        port->commitTransaction(); // do not emit data, do not keep data.
        //qInfo(logSerial()) << "Warning: received data with invalid start. Dropping data.";
        //qInfo(logSerial()) << "THIS SHOULD ONLY HAPPEN ONCE!!";
        // THIS SHOULD ONLY HAPPEN ONCE!

        // unrecoverable. We did not receive the start and must
        // have missed it earlier because we did not roll back to
        // preserve the beginning.

        //printHex(inPortData, false, true);

    }
}

void commHandler::setRTS(bool rtsOn)
{
    bool success = port->setRequestToSend(rtsOn);
    if(!success)
    {
        qInfo(logSerial()) << "Error, could not set RTS on port " << portName;
    }
}

bool commHandler::rtsStatus()
{
    return port->isRequestToSend();
}

void commHandler::setUseRTSforPTT(bool PTTviaRTS)
{
    this->PTTviaRTS = PTTviaRTS;
}

void commHandler::openPort()
{
    bool success;
    success = port->open(QIODevice::ReadWrite);
    if(success)
    {
        port->setDataTerminalReady(false);
        port->setRequestToSend(false);
        isConnected = true;
        qInfo(logSerial()) << "Opened port: " << portName;
        return;
    } else {
        qInfo(logSerial()) << "Could not open serial port " << portName << " , please restart.";
        isConnected = false;
        serialError = true;
        emit havePortError(errorType(true, portName, "Could not open port. Please restart."));
        return;
    }
}

void commHandler::closePort()
{
    if(port)
    {
        port->close();
        //delete port;
    }
    isConnected = false;
}

void commHandler::debugThis()
{
    // Do not use, function is for debug only and subject to change.
    qInfo(logSerial()) << "comm debug called.";

    inPortData = port->readAll();
    emit haveDataFromPort(inPortData);
}


void commHandler::printHex(const QByteArray &pdata, bool printVert, bool printHoriz)
{
    qDebug(logSerial()) << "---- Begin hex dump -----:";
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
            qDebug(logSerial()) << strings.at(i);
        }
    }

    if(printHoriz)
    {
        qDebug(logSerial()) << index;
        qDebug(logSerial()) << sdata;
    }
    qDebug(logSerial()) << "----- End hex dump -----";
}


void commHandler::handleError(QSerialPort::SerialPortError err)
{
    switch (err) {
        case QSerialPort::NoError:
            break;
        default:
            if (lastDataReceived.msecsTo(QTime::currentTime()) > 2000) {
                qDebug(logSerial()) << "Serial port" << port->portName() << "Error, attempting disconnect/reconnect";
                lastDataReceived = QTime::currentTime();
                QTimer::singleShot(500, this, SLOT(init()));
            }
            break;
    }
}
