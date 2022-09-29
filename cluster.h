#ifndef CLUSTER_H
#define CLUSTER_H

#include <QObject>
#include <QDebug>
#include <QUdpSocket>
#include <QTcpSocket>
#include <QXmlSimpleReader>
#include <QXmlInputSource>
#include <QDomDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QDateTime>
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


class dxClusterClient : public QObject
{
    Q_OBJECT

public:
    explicit dxClusterClient(QObject* parent = nullptr);
    virtual ~dxClusterClient();

signals:
    void addSpot(spotData spot);
    void deleteSpot(QString dxcall);
    void deleteOldSpots(int minutes);

public slots:
    void udpDataReceived();
    void enableUdp(bool enable);
    void enableTcp(bool enable);
    void setUdpPort(int p) { udpPort = p; }
    void setTcpServerName(QString s) { tcpServerName = s; }
    void setTcpUserName(QString s) { tcpUserName = s; }
    void setTcpPassword(QString s) { tcpPassword = s; }

private:
    bool udpEnable;
    bool tcpEnable;
    QUdpSocket* udpSocket=Q_NULLPTR;
    QTcpSocket* tcpSocket=Q_NULLPTR;
    int udpPort;
    QString tcpServerName;
    QString tcpUserName;
    QString tcpPassword;
    QDomDocument udpSpotReader;
    QXmlInputSource udpXmlSource;
    QMutex udpMutex;
};

#endif
