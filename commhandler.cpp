#include "commhandler.h"

#include <QDebug>

commHandler::commHandler()
{
    //constructor
    // grab baud rate and other comm port details
    // if they need to be changed later, please
    // destroy this and create a new one.

    port = new QSerialPort();

    // The following should become arguments and/or functions
    baudrate = 115200;
    stopbits = 1;
    portName = "/dev/ttyUSB0";

    setupComm(); // basic parameters
    openPort();
    qDebug() << "Serial buffer size: " << port->readBufferSize();
    //port->setReadBufferSize(1024); // manually. 256 never saw any return from the radio. why...
    //qDebug() << "Serial buffer size: " << port->readBufferSize();


    connect(port, SIGNAL(readyRead()), this, SLOT(receiveDataIn()));
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
    quint64 bytesWritten;

    // sned out data
    //port.send() or whatever
    bytesWritten = port->write(writeData);

    //qDebug() << "bytesWritten: " << bytesWritten << " length of byte array: " << writeData.length() << " size of byte array: " << writeData.size();

    mutex.unlock();
}

void commHandler::receiveDataIn()
{
    // connected to comm port data signal
   // inPortData.append(port->readAll());

   // OLD: inPortData = port->readAll();

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
            // PRoblem: We often get several chunks together this way
            // if we can split and individually send them that would be better.
            // could emit several at each FE....FD segment.
            // should probbly make the buffer smaller to reduce this
            if(rolledBack)
            {
                qDebug() << "Rolled back and was successfull. Length: " << inPortData.length();
                //printHex(inPortData, false, true);
                rolledBack = false;
            }
        } else {
            // did not receive the entire thing so roll back:
            qDebug() << "Rolling back transaction. End not detected. Lenth: " << inPortData.length();
            //printHex(inPortData, false, true);
            port->rollbackTransaction();
            rolledBack = true;
        }
    } else {
        port->commitTransaction();
        qDebug() << "Warning: received data with invalid start. Dropping data.";
        qDebug() << "THIS SHOULD ONLY HAPPEN ONCE!!";
        // THIS SHOULD ONLY HAPPEN ONCE!

        //qDebug() << "Data start: 0x" << (char)inPortData[00] << (char)inPortData[01]; // danger
        // unrecoverable. We did not receive the start and must
        // have missed it earlier because we did not roll back to
        // preserve the beginning.
        //printHex(inPortData, false, true);

    }



    // Here is where we can be smart about this:
    // port->startTransaction(); port->rollbackTransaction();
    // port->commitTransaction();

    // qDebug() << "Data: " << inPortData;
    // OLD: emit haveDataFromPort(inPortData);
}

void commHandler::openPort()
{
    bool success;
    // port->open();
    success = port->open(QIODevice::ReadWrite);
    if(success)
    {
        isConnected = true;
        qDebug() << "Opened port!";
        return;
    } else {
        // debug?
        qDebug() << "Could not open serial port.";
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


