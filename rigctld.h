#ifndef RIGCTLD_H
#define RIGCTLD_H

#include <QObject>
#include <QDebug>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSet>
#include <QDataStream>

#include <map>
#include <vector>
#include <typeindex>

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
    void setFrequency(freqt freq);
    void setPTT(bool state);
    void setMode(unsigned char mode, unsigned char modeFilter);
    void setDataMode(bool dataOn, unsigned char modeFilter);
    void setVFO(unsigned char vfo);
    void setSplit(unsigned char split);

public slots:
    virtual void incomingConnection(qintptr socketDescriptor);
    void receiveRigCaps(rigCapabilities caps);
    void receiveStateInfo(rigStateStruct* state);
    void receiveFrequency(freqt freq);

private: 
    rigStateStruct* rigState = Q_NULLPTR;
};


class rigCtlClient : public QObject
{
        Q_OBJECT

public:

    explicit rigCtlClient(int socket, rigCapabilities caps, rigStateStruct *state, rigCtlD* parent = Q_NULLPTR);
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
    void dumpCaps(QString sep);
    rigCapabilities rigCaps;
    rigStateStruct* rigState = Q_NULLPTR;
    rigCtlD* parent;
    QString getMode(unsigned char mode, bool datamode);
    unsigned char getMode(QString modeString);
    QString getFilter(unsigned char mode, unsigned char filter);
};


#endif
