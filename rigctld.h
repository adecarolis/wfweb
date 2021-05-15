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

typedef void (*voidFunction)(void);
//struct Interface {

//    std::map<std::string, std::pair<voidFunction, std::type_index>> m1;

//    template<typename T>
//    void insert(std::string s1, std::string s2, T f1) {
//        auto tt = std::type_index(typeid(f1));
//        m1.insert(std::make_pair(s1,
//            std::make_pair((voidFunction)f1, tt)));
//        m1.insert(std::make_pair(s2,
//            std::make_pair((voidFunction)f1, tt)));
//    }

//    template<typename T, typename... Args>
//    T searchAndCall(std::string s1, Args&&... args) {
//        auto mapIter = m1.find(s1);
//        /*chk if not end*/
//        auto mapVal = mapIter->second;

//        // auto typeCastedFun = reinterpret_cast< nowT(*)(Args ...)>(mapVal.first);
//        auto typeCastedFun = (T(*)(Args ...))(mapVal.first);

//        //compare the types is equal or not
//        assert(mapVal.second == std::type_index(typeid(typeCastedFun)));
//        return typeCastedFun(std::forward<Args>(args)...);
//    }
//};

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

    //Interface commands;
};


#endif
