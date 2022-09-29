#include "cluster.h"
#include "logcategories.h"


dxClusterClient::dxClusterClient(QObject* parent):
    QObject(parent)
{
    qInfo(logCluster()) << "starting dxClusterClient()";
}

dxClusterClient::~dxClusterClient()
{
    qInfo(logCluster()) << "closing dxClusterClient()";
    enableUdp(false);
    enableTcp(false);
}

void dxClusterClient::enableUdp(bool enable)
{
    udpEnable = enable;
    if (enable)
    {
        if (udpSocket == Q_NULLPTR)
        {
            udpSocket = new QUdpSocket(this);
            bool result = udpSocket->bind(QHostAddress::AnyIPv4, udpPort);
            qInfo(logCluster()) << "Starting udpSocket() on:" << udpPort << "Result:" << result;

            //udpDataReceived();
            connect(udpSocket, SIGNAL(readyRead()), this, SLOT(udpDataReceived()), Qt::QueuedConnection);

        }
    }
    else {
        if (udpSocket != Q_NULLPTR)
        {
            udpSocket->disconnect();
            delete udpSocket;
        }
    }
}

void dxClusterClient::enableTcp(bool enable)
{
    tcpEnable = enable;
}

void dxClusterClient::udpDataReceived()
{
    QMutexLocker locker(&udpMutex); // Not sure if this is needed?

    QHostAddress sender;
    quint16 port;
    QByteArray datagram;
    datagram.resize(udpSocket->pendingDatagramSize());
    udpSocket->readDatagram(datagram.data(), datagram.size(), &sender, &port);

    if (udpSpotReader.setContent(datagram))
    {
        QDomElement spot = udpSpotReader.firstChildElement("spot");
        if (spot.nodeName() == "spot")
        {
            // This is a spot?
            QString action = spot.firstChildElement("action").text();
            if (action == "add") {
                spotData data;
                data.dxcall = spot.firstChildElement("dxcall").text();
                data.frequency = spot.firstChildElement("frequency").text().toDouble()/1000.0;
                data.spottercall = spot.firstChildElement("spottercall").text();
                data.timestamp = QDateTime::fromString(spot.firstChildElement("timestamp").text());
                data.mode = spot.firstChildElement("mode").text();
                data.comment = spot.firstChildElement("comment").text();
                emit addSpot(data);
                qInfo(logCluster()) << "ADD DX=" << data.dxcall <<
                    "SPOTTER=" << data.spottercall <<
                    "FREQ=" << data.frequency <<
                    "MODE=" << data.mode;
            }
            else if (action == "delete")
            {
                QString dxcall = spot.firstChildElement("dxcall").text();
                emit deleteSpot(dxcall);
                qInfo(logCluster()) << "DELETE DX=" << dxcall;
            }
        }
    }
}

