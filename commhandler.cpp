#include "commhandler.h"
#include "logcategories.h"

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
    //qDebug(logSerial()) << "Serial buffer size: " << port->readBufferSize();
    //port->setReadBufferSize(1024); // manually. 256 never saw any return from the radio. why...
    //qDebug(logSerial()) << "Serial buffer size: " << port->readBufferSize();

    connect(port, SIGNAL(readyRead()), this, SLOT(receiveDataIn()));
}

commHandler::commHandler(QString portName, quint32 baudRate)
{
    //constructor
    // grab baud rate and other comm port details
    // if they need to be changed later, please
    // destroy this and create a new one.

    port = new QSerialPort();

    // TODO: The following should become arguments and/or functions
    // Add signal/slot everywhere for comm port setup.
    // Consider how to "re-setup" and how to save the state for next time.
    baudrate = baudRate;
    stopbits = 1;
    this->portName = portName;

    setupComm(); // basic parameters
    openPort();
    // qDebug(logSerial()) << "Serial buffer size: " << port->readBufferSize();
    //port->setReadBufferSize(1024); // manually. 256 never saw any return from the radio. why...
    //qDebug(logSerial()) << "Serial buffer size: " << port->readBufferSize();

    connect(port, SIGNAL(readyRead()), this, SLOT(receiveDataIn()));
}


commHandler::~commHandler()
{
    this->closePort();
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

    mutex.lock();

#ifdef QT_DEBUG

    qint64 bytesWritten;

    bytesWritten = port->write(writeData);
    // TODO: if(log.level == logLevelCrazy){...
    //qDebug(logSerial()) << "bytesWritten: " << bytesWritten << " length of byte array: " << writeData.length()\
    //         << " size of byte array: " << writeData.size()\
    //         << " Wrote all bytes? " << (bool) (bytesWritten == (qint64)writeData.size());

#else
    port->write(writeData);

#endif
    mutex.unlock();
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

            if(rolledBack)
            {
                // qDebug(logSerial()) << "Rolled back and was successfull. Length: " << inPortData.length();
                //printHex(inPortData, false, true);
                rolledBack = false;
            }
        } else {
            // did not receive the entire thing so roll back:
            // qDebug(logSerial()) << "Rolling back transaction. End not detected. Lenth: " << inPortData.length();
            //printHex(inPortData, false, true);
            port->rollbackTransaction();
            rolledBack = true;
        }
    } else {
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

void commHandler::openPort()
{
    bool success;
    // port->open();
    success = port->open(QIODevice::ReadWrite);
    if(success)
    {
        isConnected = true;
        qDebug(logSerial()) << "Opened port: " << portName;
        return;
    } else {
        qDebug(logSerial()) << "Could not open serial port " << portName << " , please restart.";
        isConnected = false;
        serialError = true;
        emit haveSerialPortError(portName, "Could not open port. Please restart.");
        return;
    }
}

void commHandler::closePort()
{
    if(port)
    {
        port->close();
        delete port;
    }
    isConnected = false;
}

void commHandler::debugThis()
{
    // Do not use, function is for debug only and subject to change.
    qDebug(logSerial()) << "comm debug called.";

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


