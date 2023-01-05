#include "pttyhandler.h"
#include "logcategories.h"

#include <QDebug>
#include <QFile>

#ifndef Q_OS_WIN
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#endif

// Copyright 2017-2021 Elliott H. Liggett & Phil Taylor 

pttyHandler::pttyHandler(QString pty, QObject* parent) : QObject(parent)
{
    //constructor
    if (pty == "" || pty.toLower() == "none")
    {
        // Just return if pty is not configured.
        return;
    }

    portName = pty;

#ifdef Q_OS_WIN
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
    port = new QSerialPort();
    port->setPortName(portName);
    port->setBaudRate(baudRate);
    port->setStopBits(QSerialPort::OneStop);// OneStop is other option
    success = port->open(QIODevice::ReadWrite);

    if (success) {
        connect(port, &QSerialPort::readyRead, this, std::bind(&pttyHandler::receiveDataIn, this, (int)0));
    }
#else
    // Generic method in Linux/MacOS to find a pty
    ptfd = ::posix_openpt(O_RDWR | O_NONBLOCK);

    if (ptfd >=0)
    {
        qInfo(logSerial()) << "Opened pt device: " << ptfd << ", attempting to grant pt status";

        if (grantpt(ptfd))
        {
            qInfo(logSerial()) << "Failed to grantpt";
            return;
        }
        if (unlockpt(ptfd))
        {
            qInfo(logSerial()) << "Failed to unlock pt";
            return;
        }
        // we're good!
        qInfo(logSerial()) << "Opened pseudoterminal, slave name :" << ptsname(ptfd);

        // Open the slave device to keep alive.
        ptKeepAlive = open(ptsname(ptfd), O_RDONLY);

        ptReader = new QSocketNotifier(ptfd, QSocketNotifier::Read, this);
        connect(ptReader, &QSocketNotifier::activated,
                this, &pttyHandler::receiveDataIn);

        success=true;
    }
#endif

    if (!success)
    {
        ptfd = 0;
        qInfo(logSerial()) << "Could not open pseudo terminal port, please restart.";
        isConnected = false;
        serialError = true;
        emit havePortError(errorType(portName, "Could not open pseudo terminal port. Please restart."));
        return;
    }


#ifndef Q_OS_WIN
    ptDevSlave = QString::fromLocal8Bit(ptsname(ptfd));

    if (portName != "" && portName.toLower() != "none")
    {
        if (!QFile::link(ptDevSlave, portName))
        {
            qInfo(logSerial()) << "Error creating link to" << ptDevSlave << "from" << portName;
        } else {
            qInfo(logSerial()) << "Created link to" << ptDevSlave << "from" << portName;
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

    int fePos=data.lastIndexOf((char)0xfe);
    if (fePos > 0 && data.length() > fePos+2)
        fePos=fePos-1;
    else
    {
        qDebug(logSerial()) << "Invalid command";
        printHex(data,false,true);
    }

    if (disableTransceive && ((unsigned char)data[fePos + 2] == 0x00 || (unsigned char)data[fePos + 3] == 0x00))
    {
        // Ignore data that is sent to/from transceive address as client has requested transceive disabled.
        qDebug(logSerial()) << "Transceive command filtered";
        return;
    }

    if (isConnected && (unsigned char)data[fePos + 2] != 0xE1 && (unsigned char)data[fePos + 3] != 0xE1)
    {
        // send to the pseudo port as well
        // index 2 is dest, 0xE1 is wfview, 0xE0 is assumed to be the other device.
        // Changed to "Not 0xE1"
        // 0xE1 = wfview
        // 0xE0 = pseudo-term host
        // 0x00 = broadcast to all
        //qInfo(logSerial()) << "Sending data from radio to pseudo-terminal";        
        sendDataOut(data);
    }
}

void pttyHandler::sendDataOut(const QByteArray& writeData)
{
    qint64 bytesWritten = 0;

    //qInfo(logSerial()) << "Data to pseudo term:";
    //printHex(writeData, false, true);
    if (isConnected) {
        mutex.lock();
#ifdef Q_OS_WIN
        bytesWritten = port->write(writeData);
#else
        bytesWritten = ::write(ptfd, writeData.constData(), writeData.size());
#endif
        if (bytesWritten != writeData.length()) {
            qInfo(logSerial()) << "bytesWritten: " << bytesWritten << " length of byte array: " << writeData.length()\
                << " size of byte array: " << writeData.size()\
                << " Wrote all bytes? " << (bool)(bytesWritten == (qint64)writeData.size());
        }
        mutex.unlock();
    }
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
        qInfo(logSerial()) << tr("Read failed: %1").arg(QString::fromLatin1(strerror(err)));
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

            int lastFE = inPortData.lastIndexOf((char)0xfe);
            if (civId == 0 && inPortData.length() > lastFE + 2 && (quint8)inPortData[lastFE + 2] > (quint8)0xdf && (quint8)inPortData[lastFE + 2] < (quint8)0xef) {
                // This is (should be) the remotes CIV id.
                civId = (quint8)inPortData[lastFE + 2];
                qInfo(logSerial()) << "pty detected remote CI-V:" << QString("0x%1").arg(civId,0,16);
            }
            else if (civId != 0 && inPortData.length() > lastFE + 2 && (quint8)inPortData[lastFE + 2] != civId)
            {
                civId = (quint8)inPortData[lastFE + 2];
                qInfo(logSerial()) << "pty remote CI-V changed:" << QString("0x%1").arg((quint8)civId,0,16);
            }
            // filter C-IV transceive command before forwarding on.
            if (inPortData.contains(rigCaps.transceiveCommand))
            {
                //qInfo(logSerial()) << "Filtered transceive command";
                //printHex(inPortData, false, true);
                QByteArray reply= QByteArrayLiteral("\xfe\xfe\x00\x00\xfb\xfd");
                reply[2] = inPortData[3];
                reply[3] = inPortData[2];
                sendDataOut(inPortData); // Echo command back
                sendDataOut(reply);
                if (!disableTransceive) {
                    qInfo(logSerial()) << "pty requested CI-V Transceive disable";
                    disableTransceive = true;
                }
            }
            else if (inPortData.length() > lastFE + 2 && ((quint8)inPortData[lastFE + 1] == civId || (quint8)inPortData[lastFE + 2] == civId))
            {
                emit haveDataFromPort(inPortData);
                qDebug(logSerial()) << "Data from pseudo term:";
                printHex(inPortData, false, true);
            }

            if (rolledBack)
            {
                // qInfo(logSerial()) << "Rolled back and was successful. Length: " << inPortData.length();
                //printHex(inPortData, false, true);
                rolledBack = false;
            }
        }
        else {
            // did not receive the entire thing so roll back:
            // qInfo(logSerial()) << "Rolling back transaction. End not detected. Length: " << inPortData.length();
            //printHex(inPortData, false, true);
            rolledBack = true;
#ifdef Q_OS_WIN
            port->rollbackTransaction();
        }
    }
    else {
        port->commitTransaction(); // do not emit data, do not keep data.
        //qInfo(logSerial()) << "Warning: received data with invalid start. Dropping data.";
        //qInfo(logSerial()) << "THIS SHOULD ONLY HAPPEN ONCE!!";
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
    if (port != Q_NULLPTR)
    {
        port->close();
        delete port;
    }
#else
    if (isConnected && portName != "" && portName.toLower() != "none")
    {
        QFile::remove(portName);
    }

    if (ptKeepAlive > 0) {
        close(ptKeepAlive);
    }
#endif
    isConnected = false;
}


void pttyHandler::debugThis()
{
    // Do not use, function is for debug only and subject to change.
    qInfo(logSerial()) << "comm debug called.";

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

void pttyHandler::receiveFoundRigID(rigCapabilities rigCaps) {
    this->rigCaps = rigCaps;
    qInfo(logSerial) << "Received rigCapabilities for" << rigCaps.modelName;

}


