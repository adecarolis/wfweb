#include "pttyhandler.h"
#include "logcategories.h"

#include <QDebug>

// Copyright 2017-2021 Elliott H. Liggett & Phil Taylor 

pttyHandler::pttyHandler()
{
    //constructor
    // grab baud rate and other comm port details
    // if they need to be changed later, please
    // destroy this and create a new one.
    port = new QSerialPort();

    // TODO: The following should become arguments and/or functions
    // Add signal/slot everywhere for comm port setup.
    // Consider how to "re-setup" and how to save the state for next time.
    baudRate = 115200;
    stopBits = 1;

#ifdef Q_OS_WIN
    portName = "COM20";
#else
    portName = "/dev/ptmx";
#endif
    setupPtty(); // basic parameters
    openPort();
    //qDebug(logSerial()) << "Serial buffer size: " << port->readBufferSize();
    //port->setReadBufferSize(1024); // manually. 256 never saw any return from the radio. why...
    //qDebug(logSerial()) << "Serial buffer size: " << port->readBufferSize();

    connect(port, SIGNAL(readyRead()), this, SLOT(receiveDataIn()));
}

pttyHandler::pttyHandler(QString portName, quint32 baudRate)
{
    //constructor
    // grab baud rate and other comm port details
    // if they need to be changed later, please
    // destroy this and create a new one.

    port = new QSerialPort();

    this->portName = portName;
    this->baudRate = baudRate;

    setupPtty(); // basic parameters
    openPort();

    // TODO: The following should become arguments and/or functions
    // Add signal/slot everywhere for comm port setup.
    // Consider how to "re-setup" and how to save the state for next time.
    // qDebug(logSerial()) << "Serial buffer size: " << port->readBufferSize();
    //port->setReadBufferSize(1024); // manually. 256 never saw any return from the radio. why...
    //qDebug(logSerial()) << "Serial buffer size: " << port->readBufferSize();

    connect(port, SIGNAL(readyRead()), this, SLOT(receiveDataIn()));
}

void pttyHandler::setupPtty()
{
    qDebug(logSerial()) << "Setting up Pseudo Term";
    serialError = false;
    port->setPortName(portName);
    port->setBaudRate(baudRate);
    port->setStopBits(QSerialPort::OneStop);// OneStop is other option
}


void pttyHandler::openPort()
{
    // qDebug(logSerial()) << "opening pt port";
    bool success;
#ifndef Q_OS_WIN
    char ptname[128];
    int sysResult = 0;
    QString ptLinkCmd = "ln -s ";
#endif
    success = port->open(QIODevice::ReadWrite);
    if (success)
    {
#ifndef Q_OS_WIN

        qDebug(logSerial()) << "Opened pt device, attempting to grant pt status";
        ptfd = port->handle();
        qDebug(logSerial()) << "ptfd: " << ptfd;
        if (grantpt(ptfd))
        {
            qDebug(logSerial()) << "Failed to grantpt";
            return;
        }
        if (unlockpt(ptfd))
        {
            qDebug(logSerial()) << "Failed to unlock pt";
            return;
        }
        // we're good!
        qDebug(logSerial()) << "Opened pseudoterminal.";
        qDebug(logSerial()) << "Slave name: " << ptsname(ptfd);

        ptsname_r(ptfd, ptname, 128);
        ptDevSlave = QString::fromLocal8Bit(ptname);
        ptLinkCmd.append(ptDevSlave);
        ptLinkCmd.append(" /tmp/rig");
        sysResult = system("rm /tmp/rig");
        sysResult = system(ptLinkCmd.toStdString().c_str());
        if (sysResult)
        {
            qDebug(logSerial()) << "Received error from pseudo-terminal symlink command: code: [" << sysResult << "]" << " command: [" << ptLinkCmd << "]";
        }

        isConnected = true;
#endif

    }
    else {
        ptfd = 0;
        qDebug(logSerial()) << "Could not open pseudo terminal port " << portName << " , please restart.";
        isConnected = false;
        serialError = true;
        emit haveSerialPortError(portName, "Could not open pseudo terminal port. Please restart.");
        return;
    }
}

pttyHandler::~pttyHandler()
{
    this->closePort();
}

void pttyHandler::receiveDataFromRigToPtty(const QByteArray& data)
{
    if (data[2] != (char)0xE1)
    {
        // send to the pseudo port as well
        // index 2 is dest, 0xE1 is wfview, 0xE0 is assumed to be the other device.
        // Maybe change to "Not 0xE1"
        // 0xE1 = wfview
        // 0xE0 = pseudo-term host
        // 0x00 = broadcast to all
        //qDebug(logSerial()) << "Sending data from radio to pseudo-terminal";
        sendDataOut(data);
    }
}

void pttyHandler::sendDataOut(const QByteArray& writeData)
{

    mutex.lock();

#ifdef QT_DEBUG

    qDebug(logSerial()) << "Data to term:";
    printHex(writeData, false, true);

    qint64 bytesWritten;

    if (inPortData[2] != (char)0xE1) {
        bytesWritten = port->write(writeData);
    }
    if (bytesWritten != writeData.length()) {
        qDebug(logSerial()) << "bytesWritten: " << bytesWritten << " length of byte array: " << writeData.length()\
            << " size of byte array: " << writeData.size()\
            << " Wrote all bytes? " << (bool)(bytesWritten == (qint64)writeData.size());
    }
#else
    if (inPortData[2] != (char)0xE1) {
        port->write(writeData);
    }
#endif
    mutex.unlock();
}


void pttyHandler::receiveDataIn()
{
    // connected to comm port data signal

    // Here we get a little specific to CIV radios
    // because we know what constitutes a valid "frame" of data.

    // new code:
    port->startTransaction();
    inPortData = port->readAll();
    if (inPortData.startsWith("\xFE\xFE"))
    {
        if (inPortData.endsWith("\xFD"))
        {
            // good!
            port->commitTransaction();

            // filter 1A 05 01 12 = C-IV transceive command before forwarding on.
            if (inPortData.length()>6 && inPortData[4] != (char)0x1A && inPortData[5] != (char)0x05 && inPortData[6] != (char)0x01 && inPortData[7] != (char)0x12)
            {
                emit haveDataFromPort(inPortData);
                qDebug(logSerial()) << "Data from pseudo term:";
                printHex(inPortData, false, true);
            }

            if (rolledBack)
            {
                // qDebug(logSerial()) << "Rolled back and was successfull. Length: " << inPortData.length();
                //printHex(inPortData, false, true);
                rolledBack = false;
            }
        }
        else {
            // did not receive the entire thing so roll back:
            // qDebug(logSerial()) << "Rolling back transaction. End not detected. Lenth: " << inPortData.length();
            //printHex(inPortData, false, true);
            port->rollbackTransaction();
            rolledBack = true;
        }
    }
    else {
        port->commitTransaction(); // do not emit data, do not keep data.
        //qDebug(logSerial()) << "Warning: received data with invalid start. Dropping data.";
        //qDebug(logSerial()) << "THIS SHOULD ONLY HAPPEN ONCE!!";
        // THIS SHOULD ONLY HAPPEN ONCE!

        // unrecoverable. We did not receive the start and must
        // have missed it earlier because we did not roll back to
        // preserve the beginning.

        //printHex(inPortData, false, true);

    }
}

void pttyHandler::closePort()
{
    if (port)
    {
        port->close();
        delete port;
    }
    isConnected = false;
}


void pttyHandler::debugThis()
{
    // Do not use, function is for debug only and subject to change.
    qDebug(logSerial()) << "comm debug called.";

    inPortData = port->readAll();
    emit haveDataFromPort(inPortData);
}



void pttyHandler::printHex(const QByteArray& pdata, bool printVert, bool printHoriz)
{
    qDebug(logSerial()) << "---- Begin hex dump -----:";
    QString sdata("DATA:  ");
    QString index("INDEX: ");
    QStringList strings;

    for (int i = 0; i < pdata.length(); i++)
    {
        strings << QString("[%1]: %2").arg(i, 8, 10, QChar('0')).arg((unsigned char)pdata[i], 2, 16, QChar('0'));
        sdata.append(QString("%1 ").arg((unsigned char)pdata[i], 2, 16, QChar('0')));
        index.append(QString("%1 ").arg(i, 2, 10, QChar('0')));
    }

    if (printVert)
    {
        for (int i = 0; i < strings.length(); i++)
        {
            //sdata = QString(strings.at(i));
            qDebug(logSerial()) << strings.at(i);
        }
    }

    if (printHoriz)
    {
        qDebug(logSerial()) << index;
        qDebug(logSerial()) << sdata;
    }
    qDebug(logSerial()) << "----- End hex dump -----";
}


