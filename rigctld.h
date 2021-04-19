#ifndef RIGCTLD_H
#define RIGCTLD_H

#include <QObject>
#include <QDebug>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSet>
#include <QDataStream>

#include "rigcommander.h"


class rigCtlD : public QTcpServer
{
    Q_OBJECT

public:
    explicit rigCtlD(QObject *parent=Q_NULLPTR);
    virtual ~rigCtlD();

    int startServer(qint16 port);
    void stopServer();
    rigCapabilities rigCaps;

signals:
    void onStarted();
    void onStopped();
    void sendData(QString data);

public slots:
    virtual void incomingConnection(qintptr socketDescriptor);
    void receiveRigCaps(rigCapabilities caps);


};


class rigCtlClient : public QObject
{
        Q_OBJECT

public:

    explicit rigCtlClient(int socket, rigCapabilities caps, rigCtlD* parent = Q_NULLPTR);
    int getSocketId();


public slots:
    void socketReadyRead(); 
    void socketDisconnected();
    void closeSocket();
    void sendData(QString data);

protected:
    int sessionId;
    QTcpSocket* socket = Q_NULLPTR;
    QString commandBuffer;

private:
    void dumpCaps();
    rigCapabilities rigCaps;

};

#endif
