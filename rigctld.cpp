#include "rigctld.h"
#include "logcategories.h"



rigCtlD::rigCtlD(QObject* parent) :
    QTcpServer(parent)
{
}

rigCtlD::~rigCtlD()
{
}

int rigCtlD::startServer(qint16 port) 
{
    if (!this->listen(QHostAddress::Any, port)) {
        qDebug(logRigCtlD()) << "could not start on port " << port;
        return -1;
    }
    else
    {
        qDebug(logRigCtlD()) << "started on port " << port;
    }
    return 0;
}

void rigCtlD::incomingConnection(qintptr socket) {
    rigCtlClient* client = new rigCtlClient(socket, rigCaps, this);
    connect(this, SIGNAL(onStopped()), client, SLOT(closeSocket()));
}


void rigCtlD::stopServer() 
{
    qDebug(logRigCtlD()) << "stopping server";
    emit onStopped();
}

void rigCtlD::receiveRigCaps(rigCapabilities caps)
{
    qDebug(logRigCtlD()) << "Got rigcaps for:" << caps.modelName;
    this->rigCaps = caps;
}

rigCtlClient::rigCtlClient(int socketId, rigCapabilities caps, rigCtlD *parent) : QObject(parent)
{
    commandBuffer.clear();
    sessionId = socketId;
    rigCaps = caps;
    socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(sessionId))
    {
        qDebug(logRigCtlD()) << " error binding socket: " << sessionId;
        return;
    }
    connect(socket, SIGNAL(readyRead()), this, SLOT(socketReadyRead()), Qt::DirectConnection);
    connect(socket, SIGNAL(disconnected()), this, SLOT(socketDisconnected()), Qt::DirectConnection);
    connect(parent, SIGNAL(sendData(QString)), this, SLOT(sendData(QString)), Qt::DirectConnection);
    qDebug(logRigCtlD()) << " session connected: " << sessionId;
}

void rigCtlClient::socketReadyRead()
{
    QByteArray data = socket->readAll();
    commandBuffer.append(data);
    if (commandBuffer.endsWith('\n'))
    {
        // Process command
        qDebug(logRigCtlD()) << sessionId << "command received" << commandBuffer;
        QString cmd = commandBuffer[0];

        if (cmd.toLower() == "q")
        {
            closeSocket();
        }
        else if (cmd == "1")
        {
            dumpCaps();
        }
        commandBuffer.clear();
    }
}

void rigCtlClient::socketDisconnected()
{
    qDebug(logRigCtlD()) << sessionId << "disconnected";
    socket->deleteLater();
    this->deleteLater();
}

void rigCtlClient::closeSocket()
{
    socket->close();
}

void rigCtlClient::sendData(QString data)
{
    if (socket != Q_NULLPTR && socket->isValid() && socket->isOpen())
    {
        socket->write(data.toLatin1());
    }
}


void rigCtlClient::dumpCaps()
{
    sendData(QString("Caps dump for model: %1\n").arg(rigCaps.modelID));
    sendData(QString("Model Name:\t%1\n").arg(rigCaps.modelName));
    sendData(QString("Mfg Name:\tIcom\n"));
    sendData(QString("Backend version:\t0.1\n"));
    sendData(QString("Backend copyright:\t2021\n"));
    sendData(QString("Rig type:\tTransceiver\n"));
    sendData(QString("PTT type:\tRig capable\n"));
    sendData(QString("DCD type:\tRig capable\n"));
    sendData(QString("Port type:\tNetwork link\n"));

}