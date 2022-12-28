#ifndef CLUSTER_H
#define CLUSTER_H

#include <QObject>
#include <QDebug>
#include <QUdpSocket>
#include <QTcpSocket>
#include <QDomDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QDateTime>
#include <QRegularExpression>
#include <QTimer>

#ifdef USESQL
#include <QSqlDatabase>
#include <QSqlQuery>
#endif

#include <qcustomplot.h>

#ifdef USESQL
#include "database.h"
#endif

struct spotData {
    QString dxcall;
    double frequency;
    QString spottercall;
    QDateTime timestamp;
    QString mode;
    QString comment;
    QCPItemText* text = Q_NULLPTR;
    bool current = false;
};

struct clusterSettings {
    QString server;
    int port=7300;
    QString userName;
    QString password;
    int timeout=30;
    bool isdefault;
};

class dxClusterClient : public QObject
{
    Q_OBJECT

public:
    explicit dxClusterClient(QObject* parent = nullptr);
    virtual ~dxClusterClient();

signals:
    void addSpot(spotData* spot);
    void deleteSpot(QString dxcall);
    void deleteOldSpots(int minutes);
    void sendOutput(QString text);
    void sendSpots(QList<spotData> spots);

public slots:
    void udpDataReceived();
    void tcpDataReceived();
    void tcpDisconnected();
    void enableUdp(bool enable);
    void enableTcp(bool enable);
    void setUdpPort(int p) { udpPort = p; }
    void setTcpServerName(QString s) { tcpServerName = s; }
    void setTcpPort(int p) { tcpPort = p; }
    void setTcpUserName(QString s) { tcpUserName = s; }
    void setTcpPassword(QString s) { tcpPassword = s; }
    void setTcpTimeout(int p) { tcpTimeout = p; }
    void tcpCleanup();
    void freqRange(double low, double high);
    void enableSkimmerSpots(bool enable);

private:
    void sendTcpData(QString data);
    bool databaseOpen();
    void updateSpots();

    bool udpEnable;
    bool tcpEnable;
    QUdpSocket* udpSocket=Q_NULLPTR;
    QTcpSocket* tcpSocket=Q_NULLPTR;
    int udpPort;
    QString tcpServerName;
    int tcpPort;
    QString tcpUserName;
    QString tcpPassword;
    int tcpTimeout;
    QDomDocument udpSpotReader;
    QRegularExpression tcpRegex;
    QMutex mutex;
    bool authenticated=false;
    QTimer* tcpCleanupTimer=Q_NULLPTR;
#ifdef USESQL
    QSqlDatabase db;
#endif
    double lowFreq;
    double highFreq;
    QMap<QString,spotData*> allSpots;
    bool skimmerSpots = false;
};

#endif
