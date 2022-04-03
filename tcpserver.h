#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <QObject>
#include <QDebug>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSet>
#include <QDataStream>

#include <map>
#include <vector>
#include <typeindex>

class tcpServer : public QTcpServer
{
    Q_OBJECT

public:
    explicit tcpServer(QObject* parent = Q_NULLPTR);
    ~tcpServer();
    int startServer(qint16 port);
    void stopServer();

public slots:
    virtual void incomingConnection(qintptr socketDescriptor);
    void receiveDataFromClient(QByteArray data);
    void sendData(QByteArray data);
signals:
    void onStarted();
    void onStopped();
    void receiveData(QByteArray data); // emit this when we have data from tcp client, connect to rigcommander
    void sendDataToClient(QByteArray data);
    void newClient(int socketId);

private:
    QTcpServer* server;
    QTcpSocket* socket = Q_NULLPTR;
};

class tcpServerClient : public QObject 
{
    Q_OBJECT

public: 
    explicit tcpServerClient(int socket, tcpServer* parent = Q_NULLPTR);
public slots:
    void socketReadyRead();
    void socketDisconnected();
    void closeSocket();
    void receiveDataToClient(QByteArray);

signals:
    void sendDataFromClient(QByteArray data);
protected:
    int sessionId;
    QTcpSocket* socket = Q_NULLPTR;

private:
    tcpServer* parent;
};

#endif