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
#include <qcustomplot.h>

struct spotData {
    QString dxcall;
    double frequency;
    QString spottercall;
    QDateTime timestamp;
    QString mode;
    QString comment;
    QCPItemText* text = Q_NULLPTR;
};

struct clusterSettings {
    QString server;
    int port;
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

public slots:
    void udpDataReceived();
    void tcpDataReceived();
    void enableUdp(bool enable);
    void enableTcp(bool enable);
    void setUdpPort(int p) { udpPort = p; }
    void setTcpServerName(QString s) { tcpServerName = s; }
    void setTcpPort(int p) { tcpPort = p; }
    void setTcpUserName(QString s) { tcpUserName = s; }
    void setTcpPassword(QString s) { tcpPassword = s; }
    void setTcpTimeout(int p) { tcpTimeout = p; }
    void tcpCleanup();

private:
    void sendTcpData(QString data);

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
};

#endif
