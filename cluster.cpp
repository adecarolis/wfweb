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
    database db;
    db.close();
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

            connect(udpSocket, SIGNAL(readyRead()), this, SLOT(udpDataReceived()), Qt::QueuedConnection);
        }
    }
    else {
        if (udpSocket != Q_NULLPTR)
        {
            qInfo(logCluster()) << "Stopping udpSocket() on:" << udpPort;

            udpSocket->disconnect();
            delete udpSocket;
            udpSocket = Q_NULLPTR;
        }
    }
}

void dxClusterClient::enableTcp(bool enable)
{
    tcpEnable = enable;
    if (enable)
    {
        tcpRegex = QRegularExpression("^DX de ([a-z|A-Z|0-9|/]+):\\s+([0-9|.]+)\\s+([a-z|A-Z|0-9|/]+)+\\s+(.*)\\s+(\\d{4}Z)");

        if (tcpSocket == Q_NULLPTR)
        {
            tcpSocket = new QTcpSocket(this);
            tcpSocket->connectToHost(tcpServerName, tcpPort);
            qInfo(logCluster()) << "Starting tcpSocket() on:" << tcpPort;

            connect(tcpSocket, SIGNAL(readyRead()), this, SLOT(tcpDataReceived()), Qt::QueuedConnection);
            connect(tcpSocket, SIGNAL(disconnected()), this, SLOT(tcpDisconnected()));

            tcpCleanupTimer = new QTimer(this);
            tcpCleanupTimer->setInterval(1000 * 10); // Run once a minute
            connect(tcpCleanupTimer, SIGNAL(timeout()), this, SLOT(tcpCleanup()));
            tcpCleanupTimer->start();
        }
    }
    else {
        if (tcpSocket != Q_NULLPTR)
        {
            qInfo(logCluster()) << "Disconnecting tcpSocket() on:" << tcpPort;
            if (tcpCleanupTimer != Q_NULLPTR)
            {
                tcpCleanupTimer->stop();
                delete tcpCleanupTimer;
                tcpCleanupTimer = Q_NULLPTR;
            }
            tcpSocket->disconnect();
            delete tcpSocket;
            tcpSocket = Q_NULLPTR;
            //emit deleteOldSpots(0);
        }
    }
}

void dxClusterClient::udpDataReceived()
{
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
                spotData* data = new spotData();
                data->dxcall = spot.firstChildElement("dxcall").text();
                data->frequency = spot.firstChildElement("frequency").text().toDouble() / 1000.0;
                data->spottercall = spot.firstChildElement("spottercall").text();
                data->timestamp = QDateTime::fromString(spot.firstChildElement("timestamp").text(),"yyyy-MM-dd hh:mm:ss");
                data->mode = spot.firstChildElement("mode").text();
                data->comment = spot.firstChildElement("comment").text();
                //emit addSpot(data);

                database db = database();
                db.query(QString("DELETE from spots where dxcall='%1'").arg(data->dxcall));
                QString query = QString("INSERT INTO spots(type,spottercall,frequency,dxcall,mode,comment,timestamp) VALUES('%1','%2',%3,'%4','%5','%6','%7')\n")
                    .arg("UDP").arg(data->spottercall).arg(data->frequency).arg(data->dxcall).arg(data->mode).arg(data->comment).arg(data->timestamp.toString("yyyy-MM-dd hh:mm:ss"));
                db.query(query);
                emit sendOutput(query);
            }
            else if (action == "delete")
            {
                QString dxcall = spot.firstChildElement("dxcall").text();
                double frequency = spot.firstChildElement("frequency").text().toDouble() / 1000.0;
                //emit deleteSpot(dxcall);
                database db = database();
                QString query=QString("DELETE from spots where dxcall='%1' AND frequency=%2").arg(dxcall).arg(frequency);
                db.query(query);
                qInfo(logCluster()) << query;
            }
            updateSpots();
        }
    }
}


void dxClusterClient::tcpDataReceived()
{
    QString data = QString(tcpSocket->readAll());
    emit(sendOutput(data));

    if (data.contains("login:")) {
        sendTcpData(QString("%1\n").arg(tcpUserName));
        return;
    }
    if (data.contains("password:")) {
        sendTcpData(QString("%1\n").arg(tcpPassword));
        return;
    }

    QRegularExpressionMatchIterator i = tcpRegex.globalMatch(data);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        if (match.hasMatch()) {
            spotData* data = new spotData();
            data->spottercall = match.captured(1);
            data->frequency = match.captured(2).toDouble() / 1000.0;
            data->dxcall = match.captured(3);
            data->comment = match.captured(4).trimmed();
            data->timestamp = QDateTime::currentDateTimeUtc();
            //data.timestamp = QDateTime::fromString(match.captured(5), "hhmmZ");
            //emit addSpot(data);
            database db = database();
            db.query(QString("DELETE from spots where dxcall='%1'").arg(data->dxcall));
            QString query = QString("INSERT INTO spots(type,spottercall,frequency,dxcall,comment,timestamp) VALUES('%1','%2',%3,'%4','%5','%6')\n")
                .arg("TCP").arg(data->spottercall).arg(data->frequency).arg(data->dxcall).arg(data->comment).arg(data->timestamp.toString("yyyy-MM-dd hh:mm:ss"));
            db.query(query);
            emit sendOutput(query);
        }
    }
    updateSpots();
}


void dxClusterClient::sendTcpData(QString data)
{
    qInfo(logCluster()) << "Sending:" << data;
    if (tcpSocket != Q_NULLPTR && tcpSocket->isValid() && tcpSocket->isOpen())
    {
        tcpSocket->write(data.toLatin1());
    }
    else
    {
        qInfo(logCluster()) << "socket not open!";
    }
}

void dxClusterClient::tcpCleanup()
{
    //emit deleteOldSpots(tcpTimeout);
    database db = database();
    db.query(QString("DELETE FROM spots where timestamp < datetime('now', '-%1 minutes')").arg(tcpTimeout));
}

void dxClusterClient::tcpDisconnected() {
    qWarning(logCluster()) << "TCP Cluster server disconnected...";
    // Need to start a timer and attempt reconnect.
}

void dxClusterClient::freqRange(double low, double high)
{
    lowFreq = low;
    highFreq = high;
    //qInfo(logCluster) << "New range" << low << "-" << high;
    updateSpots();
}

void dxClusterClient::updateSpots()
{
    // Set the required freqency range.
    QString queryText = QString("SELECT * FROM spots WHERE frequency > %1 AND frequency < %2").arg(lowFreq).arg(highFreq);
    //QString queryText = QString("SELECT * FROM spots");
    database db;
    auto query = db.query(queryText);

    QList<spotData> spots;
    while (query.next()) {
        // Step through all current spots within range
        spotData s = spotData();
        s.dxcall = query.value(query.record().indexOf("dxcall")).toString();
        s.frequency = query.value(query.record().indexOf("frequency")).toDouble();
        spots.append(s);
    }
    emit sendSpots(spots);
}
