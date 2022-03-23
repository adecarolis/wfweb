#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <qtcpserver.h>
class tcpServer :
    public QObject
{
    Q_OBJECT
public:
    explicit tcpServer(QObject* parent = 0);
    ~tcpServer();
public slots:
    void dataToPort(QByteArray data);
    void readyRead();
    void newConnection();
signals:
    void haveDataFromPort(QByteArray data); // emit this when we have data, connect to rigcommander
private:
    QTcpServer* server;
    QTcpSocket* socket = Q_NULLPTR;
};

#endif