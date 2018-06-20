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

    inPortData = port->readAll();

    // qDebug() << "Data: " << inPortData;
    emit haveDataFromPort(inPortData);
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

