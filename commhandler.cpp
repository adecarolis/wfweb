#include "commhandler.h"

#include <QDebug>

// Copytight 2017-2020 Elliott H. Liggett

commHandler::commHandler()
{
    //constructor
    // grab baud rate and other comm port details
    // if they need to be changed later, please
    // destroy this and create a new one.

    port = new QSerialPort();


    // TODO: The following should become arguments and/or functions
    // Add signal/slot everywhere for comm port setup.
    // Consider how to "re-setup" and how to save the state for next time.
    baudrate = 115200;
    stopbits = 1;
    portName = "/dev/ttyUSB0";

    setupComm(); // basic parameters
    openPort();
    //qDebug() << "Serial buffer size: " << port->readBufferSize();
    //port->setReadBufferSize(1024); // manually. 256 never saw any return from the radio. why...
    //qDebug() << "Serial buffer size: " << port->readBufferSize();

    initializePt();

    connect(port, SIGNAL(readyRead()), this, SLOT(receiveDataIn()));
    connect(pseudoterm, SIGNAL(readyRead()), this, SLOT(receiveDataInPt()));
}

commHandler::commHandler(QString portName)
{
    //constructor
    // grab baud rate and other comm port details
    // if they need to be changed later, please
    // destroy this and create a new one.

    port = new QSerialPort();

    // TODO: The following should become arguments and/or functions
    // Add signal/slot everywhere for comm port setup.
    // Consider how to "re-setup" and how to save the state for next time.
    baudrate = 115200;
    stopbits = 1;
    this->portName = portName;

    setupComm(); // basic parameters
    openPort();
    // qDebug() << "Serial buffer size: " << port->readBufferSize();
    //port->setReadBufferSize(1024); // manually. 256 never saw any return from the radio. why...
    //qDebug() << "Serial buffer size: " << port->readBufferSize();


    initializePt();

    connect(port, SIGNAL(readyRead()), this, SLOT(receiveDataIn()));
    connect(pseudoterm, SIGNAL(readyRead()), this, SLOT(receiveDataInPt())); // sometimes it seems the connection fails.

}

void commHandler::initializePt()
{
    // qDebug() << "init pt";
    pseudoterm = new QSerialPort();
    setupPtComm();
    openPtPort();
}

void commHandler::setupPtComm()
{
    qDebug() << "Setting up Pseudo Term";
    pseudoterm->setPortName("/dev/ptmx");
    // pseudoterm->setBaudRate(baudrate);
    // pseudoterm->setStopBits(QSerialPort::OneStop);
}

void commHandler::openPtPort()
{
    // qDebug() << "opening pt port";
    bool success;
    char ptname[128];
    int sysResult=0;
    QString ptLinkCmd = "ln -s ";
    success = pseudoterm->open(QIODevice::ReadWrite);
    if(success)
    {
        qDebug() << "Opened pt device, attempting to grant pt status";
        ptfd = pseudoterm->handle();
        qDebug() << "ptfd: " << ptfd;
        if(grantpt(ptfd))
        {
            qDebug() << "Failed to grantpt";
            return;
        }
        if(unlockpt(ptfd))
        {
            qDebug() << "Failed to unlock pt";
            return;
        }
        // we're good!
        qDebug() << "Opened pseudoterminal.";
        qDebug() << "Slave name: " << ptsname(ptfd);

        ptsname_r(ptfd, ptname, 128);
        ptDevSlave = QString::fromLocal8Bit(ptname);
        ptLinkCmd.append(ptDevSlave);
        ptLinkCmd.append(" /tmp/rig");
        sysResult = system("rm /tmp/rig");
        sysResult = system(ptLinkCmd.toStdString().c_str());
        if(sysResult)
        {
            qDebug() << "Received error from pseudo-terminal symlink command: code: [" << sysResult << "]" << " command: [" << ptLinkCmd << "]";
        }

    } else {
        ptfd = 0;
        qDebug() << "Could not open pseudo-terminal.";
    }
}

commHandler::~commHandler()
{
    this->closePort();
}


void commHandler::setupComm()
{
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

    mutex.lock();

#ifdef QT_DEBUG

    qint64 bytesWritten;

    bytesWritten = port->write(writeData);
    qDebug() << "bytesWritten: " << bytesWritten << " length of byte array: " << writeData.length()\
             << " size of byte array: " << writeData.size()\
             << " Wrote all bytes? " << (bool) (bytesWritten == (qint64)writeData.size());

#else
    port->write(writeData);

#endif
    mutex.unlock();
}

void commHandler::sendDataOutPt(const QByteArray &writeData)
{
    ptMutex.lock();
    //printHex(writeData, false, true);

#ifdef QT_DEBUG
    qint64 bytesWritten;
    bytesWritten = port->write(writeData);
    qDebug() << "pseudo-term bytesWritten: " << bytesWritten << " length of byte array: " << \
                writeData.length() << " size of byte array: " << writeData.size()\
             << ", wrote all: " << (bool)(bytesWritten == (qint64)writeData.size());
#else
    pseudoterm->write(writeData);
#endif
    ptMutex.unlock();
}


void commHandler::receiveDataInPt()
{
    // We received data from the pseudo-term.
    //qDebug() << "Sending data from pseudo-terminal to radio";
    // Send this data to the radio:
    //QByteArray ptdata = pseudoterm->readAll();
    // should check the data and rollback
    // for now though...
    //sendDataOut(ptdata);
    sendDataOut(pseudoterm->readAll());
    //qDebug() << "Returned from sendDataOut with pseudo-terminal send data.";
}

void commHandler::receiveDataIn()
{
    // connected to comm port data signal

    // Here we get a little specific to CIV radios
    // because we know what constitutes a valid "frame" of data.

    // new code:
    port->startTransaction();
    inPortData = port->readAll();
    if(inPortData.startsWith("\xFE\xFE"))
    {
        if(inPortData.endsWith("\xFD"))
        {
            // good!
            port->commitTransaction();
            emit haveDataFromPort(inPortData);
            if( (inPortData[2] == (char)0x00) || (inPortData[2] == (char)0xE0) || (inPortData[3] == (char)0xE0) )
            {
                // send to the pseudo port as well
                // index 2 is dest, 0xE1 is wfview, 0xE0 is assumed to be the other device.
                // Maybe change to "Not 0xE1"
                // 0xE1 = wfview
                // 0xE0 = pseudo-term host
                // 0x00 = broadcast to all
                //qDebug() << "Sending data from radio to pseudo-terminal";
                sendDataOutPt(inPortData);
            }

            if(rolledBack)
            {
                // qDebug() << "Rolled back and was successfull. Length: " << inPortData.length();
                //printHex(inPortData, false, true);
                rolledBack = false;
            }
        } else {
            // did not receive the entire thing so roll back:
            // qDebug() << "Rolling back transaction. End not detected. Lenth: " << inPortData.length();
            //printHex(inPortData, false, true);
            port->rollbackTransaction();
            rolledBack = true;
        }
    } else {
        port->commitTransaction(); // do not emit data, do not keep data.
        //qDebug() << "Warning: received data with invalid start. Dropping data.";
        //qDebug() << "THIS SHOULD ONLY HAPPEN ONCE!!";
        // THIS SHOULD ONLY HAPPEN ONCE!

        // unrecoverable. We did not receive the start and must
        // have missed it earlier because we did not roll back to
        // preserve the beginning.

        //printHex(inPortData, false, true);

    }
}

void commHandler::openPort()
{
    bool success;
    // port->open();
    success = port->open(QIODevice::ReadWrite);
    if(success)
    {
        isConnected = true;
        //qDebug() << "Opened port!";
        return;
    } else {
        // debug?
        qDebug() << "Could not open serial port " << portName << " , please restart.";
        isConnected = false;
        return;
    }


}

void commHandler::closePort()
{
    port->close();
    isConnected = false;
}

void commHandler::debugThis()
{
    // Do not use, function is for debug only and subject to change.
    qDebug() << "comm debug called.";

    inPortData = port->readAll();
    emit haveDataFromPort(inPortData);
}



void commHandler::printHex(const QByteArray &pdata, bool printVert, bool printHoriz)
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


