#include "tcpserver.h"

#include "logcategories.h"

tcpServer::tcpServer(QObject* parent) : QTcpServer(parent)
{
}

tcpServer::~tcpServer()
{
    qInfo(logTcpServer()) << "closing tcpServer";
}

int tcpServer::startServer(qint16 port) {

    if (!this->listen(QHostAddress::Any, port)) {
        qInfo(logTcpServer()) << "could not start on port " << port;
        return -1;
    }
    else
    {
        qInfo(logTcpServer()) << "started on port " << port;
    }

    return 0;
}

void tcpServer::incomingConnection(qintptr socket) {
    tcpServerClient* client = new tcpServerClient(socket, this);
    connect(this, SIGNAL(onStopped()), client, SLOT(closeSocket()));
    emit newClient(socket); // Signal par
}

void tcpServer::stopServer()
{
    qInfo(logTcpServer()) << "stopping server";
    emit onStopped();
}


void tcpServer::receiveDataFromClient(QByteArray data)
{
    emit receiveData(data);
}

void tcpServer::sendData(QByteArray data) {

    emit sendDataToClient(data);

}

tcpServerClient::tcpServerClient(int socketId, tcpServer* parent) : QObject(parent)
{
    sessionId = socketId;
    socket = new QTcpSocket(this);
    this->parent = parent;
    if (!socket->setSocketDescriptor(sessionId))
    {
        qInfo(logTcpServer()) << " error binding socket: " << sessionId;
        return;
    }
    connect(socket, SIGNAL(readyRead()), this, SLOT(socketReadyRead()), Qt::DirectConnection);
    connect(socket, SIGNAL(disconnected()), this, SLOT(socketDisconnected()), Qt::DirectConnection);
    connect(parent, SIGNAL(sendDataToClient(QByteArray)), this, SLOT(receiveDataToClient(QByteArray)), Qt::DirectConnection);
    connect(this, SIGNAL(sendDataFromClient(QByteArray)), parent, SLOT(receiveDataFromClient(QByteArray)), Qt::DirectConnection);
    qInfo(logTcpServer()) << " session connected: " << sessionId;

}

void tcpServerClient::socketReadyRead() {
    QByteArray data;
    if (socket->bytesAvailable()) {
        data=socket->readAll();
        emit sendDataFromClient(data);
    }
}

void tcpServerClient::socketDisconnected() {
    qInfo(logTcpServer()) << sessionId << "disconnected";
    socket->deleteLater();
    this->deleteLater();
}

void tcpServerClient::closeSocket()
{
    socket->close();
}

void tcpServerClient::receiveDataToClient(QByteArray data) {

    if (socket != Q_NULLPTR && socket->isValid() && socket->isOpen())
    {
        socket->write(data);
    }
    else
    {
        qInfo(logTcpServer()) << "socket not open!";
    }
}