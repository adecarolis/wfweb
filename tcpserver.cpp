#include "tcpserver.h"

#include "logcategories.h"

tcpServer::tcpServer(QObject* parent) : QObject(parent)
{
    server = new QTcpServer(this);
    connect(server, SIGNAL(newConnection()), this, SLOT(newConnection()));
    if (!server->listen(QHostAddress::Any, 5010))
    {
        qDebug() << "TCP Server could not start";
    }
    else
    {
        qDebug() << "TCP Server started!";
    }
}

tcpServer::~tcpServer()
{
    if (socket != Q_NULLPTR)
    {
        socket->close();
        delete socket;
    }
    server->close();
    delete server;

}

void tcpServer::newConnection()
{
    qDebug(logSystem()) << QString("Incoming Connection");
    socket = server->nextPendingConnection();
    connect(socket, SIGNAL(readyRead()), this, SLOT(readyRead()));
}

void tcpServer::readyRead() {
    QByteArray data;
    if (socket->bytesAvailable()) {
        data=socket->readAll();
        emit haveDataFromPort(data);
    }
    //qDebug(logSystem()) << QString("Data IN!");

}


void tcpServer::dataToPort(QByteArray data) {
    //qDebug(logSystem()) << QString("TCP Send");

    if (socket != Q_NULLPTR) {
        socket->write(data);
        socket->flush();
    }
}