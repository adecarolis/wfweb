#include "pttyhandler.h"
#include "logcategories.h"

#include <QDebug>
#include <QFile>

#include <sstream>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

// Copyright 2017-2021 Elliott H. Liggett & Phil Taylor 

pttyHandler::pttyHandler(QString pty)
{
    //constructor
    portName = pty;

#ifdef Q_OS_WIN
    port = new QSerialPort();
    // TODO: The following should become arguments and/or functions
    // Add signal/slot everywhere for comm port setup.
    // Consider how to "re-setup" and how to save the state for next time.
    baudRate = 115200;
    stopBits = 1;
    portName = pty;
#endif

    openPort();

}


void pttyHandler::openPort()
{
    serialError = false;
    bool success=false;

#ifdef Q_OS_WIN
    port->setPortName(portName);
    port->setBaudRate(baudRate);
    port->setStopBits(QSerialPort::OneStop);// OneStop is other option
    success = port->open(QIODevice::ReadWrite);
    connect(port, SIGNAL(readyRead()), this, SLOT(receiveDataIn(0)));
#else
    // Generic method in Linux/MacOS to find a pty
    ptfd = ::posix_openpt(O_RDWR | O_NOCTTY);

    if (ptfd >=0)
    {
        qDebug(logSerial()) << "Opened pt device: " << ptfd << ", attempting to grant pt status";

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
        qDebug(logSerial()) << "Opened pseudoterminal, slave name :" << ptsname(ptfd);

        ptReader = new QSocketNotifier(ptfd, QSocketNotifier::Read, this);
        connect(ptReader, &QSocketNotifier::activated,
                this, &pttyHandler::receiveDataIn);

        success=true;
    }
#endif

    if (!success)
    {
        ptfd = 0;
        qDebug(logSerial()) << "Could not open pseudo terminal port, please restart.";
        isConnected = false;
        serialError = true;
        emit haveSerialPortError(portName, "Could not open pseudo terminal port. Please restart.");
        return;
    }


#ifndef Q_OS_WIN
    ptDevSlave = QString::fromLocal8Bit(ptsname(ptfd));

    if (portName != "" && portName != "none")
    {
        if (!QFile::link(ptDevSlave, portName))
        {
            qDebug(logSerial()) << "Error creating link to" << ptDevSlave << "from" << portName;
        } else {
            qDebug(logSerial()) << "Created link to" << ptDevSlave << "from" << portName;
        }
    }
#endif

    isConnected = true;
}

pttyHandler::~pttyHandler()
{
    this->closePort();
}

void pttyHandler::receiveDataFromRigToPtty(const QByteArray& data)
{
    if ((unsigned char)data[2] != (unsigned char)0xE1 && (unsigned char)data[3] != (unsigned char)0xE1)
    {
        // send to the pseudo port as well
        // index 2 is dest, 0xE1 is wfview, 0xE0 is assumed to be the other device.
        // Changed to "Not 0xE1"
        // 0xE1 = wfview
        // 0xE0 = pseudo-term host
        // 0x00 = broadcast to all
        //qDebug(logSerial()) << "Sending data from radio to pseudo-terminal";
        sendDataOut(data);
    }
}

void pttyHandler::sendDataOut(const QByteArray& writeData)
{
    qint64 bytesWritten = 0;

    //qDebug(logSerial()) << "Data to pseudo term:";
    //printHex(writeData, false, true);

    mutex.lock();
#ifdef Q_OS_WIN
    bytesWritten = port->write(writeData);
#else
    bytesWritten = ::write(ptfd, writeData.constData(), writeData.size());
#endif
    if (bytesWritten != writeData.length()) {
        qDebug(logSerial()) << "bytesWritten: " << bytesWritten << " length of byte array: " << writeData.length()\
            << " size of byte array: " << writeData.size()\
            << " Wrote all bytes? " << (bool)(bytesWritten == (qint64)writeData.size());
    }
    mutex.unlock();
}

void pttyHandler::receiveDataIn(int fd) {

#ifndef Q_OS_WIN
    ssize_t available = 255; // Read up to 'available' bytes
#else
    Q_UNUSED(fd);
#endif

   // Linux will correctly return the number of available bytes with the FIONREAD ioctl
   // Sadly MacOS always returns zero!
#ifdef Q_OS_LINUX
    int ret = ::ioctl(fd, FIONREAD, (char *) &available);
    if (ret != 0)
        return;
#endif

#ifdef Q_OS_WIN
    port->startTransaction();
    inPortData = port->readAll();
#else
    inPortData.resize(available);
    ssize_t got = ::read(fd, inPortData.data(), available);
    int err = errno;
    if (got < 0) {
        qDebug(logSerial()) << tr("Read failed: %1").arg(QString::fromLatin1(strerror(err)));
        return;
    }
    inPortData.resize(got);
#endif

    if (inPortData.startsWith("\xFE\xFE"))
    {
        if (inPortData.endsWith("\xFD"))
        {
            // good!
#ifdef Q_OS_WIN
            port->commitTransaction();
#endif

            // filter 1A 05 01 12/27 = C-IV transceive command before forwarding on.
            if (inPortData.contains(QByteArrayLiteral("\x1a\x05\x01\x12")) || inPortData.contains(QByteArrayLiteral("\x1a\x05\x01\x27")))
            {
                //qDebug(logSerial()) << "Filtered transceive command";
                //printHex(inPortData, false, true);
                QByteArray reply= QByteArrayLiteral("\xfe\xfe\x00\x00\xfb\xfd");
                reply[2] = inPortData[3];
                reply[3] = inPortData[2];
                sendDataOut(inPortData); // Echo command back
                sendDataOut(reply);
            }
            else
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
            rolledBack = true;
#ifdef Q_OS_WIN
            port->rollbackTransaction();
        }
    }
    else {
        port->commitTransaction(); // do not emit data, do not keep data.
        //qDebug(logSerial()) << "Warning: received data with invalid start. Dropping data.";
        //qDebug(logSerial()) << "THIS SHOULD ONLY HAPPEN ONCE!!";
        // THIS SHOULD ONLY HAPPEN ONCE!
    }
#else
        }
    }
#endif
}



void pttyHandler::closePort()
{
#ifdef Q_OS_WIN
    if (port)
    {
        port->close();
        delete port;
    }
#else
    if (portName != "" && portName != "none")
    {
        QFile::remove(portName);
    }
#endif
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


