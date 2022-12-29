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
#ifdef USESQL
    database db;
    db.close();
#else
    QMap<QString, spotData*>::iterator spot = allSpots.begin();
    while (spot != allSpots.end())
    {
        delete spot.value(); // Stop memory leak?
        spot = allSpots.erase(spot);
    }
#endif
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
        tcpRegex = QRegularExpression("^DX de ([a-z-|A-Z|0-9|#|/]+):\\s+([0-9|.]+)\\s+([a-z|A-Z|0-9|/]+)+\\s+(.*)\\s+(\\d{4}Z)");

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
            authenticated = false;
        }
    }
    else {
        if (tcpSocket != Q_NULLPTR)
        {
            sendTcpData(QString("bye\n"));
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

#ifdef USESQL
                database db = database();
                db.query(QString("DELETE from spots where dxcall='%1'").arg(data->dxcall));
                QString query = QString("INSERT INTO spots(type,spottercall,frequency,dxcall,mode,comment,timestamp) VALUES('%1','%2',%3,'%4','%5','%6','%7')\n")
                    .arg("UDP").arg(data->spottercall).arg(data->frequency).arg(data->dxcall).arg(data->mode).arg(data->comment).arg(data->timestamp.toString("yyyy-MM-dd hh:mm:ss"));
                db.query(query);
#else
                bool found = false;
                QMap<QString, spotData*>::iterator spot = allSpots.find(data->dxcall);
                while (spot != allSpots.end() && spot.key() == data->dxcall && spot.value()->frequency == data->frequency) {
                    found = true;
                    ++spot;
                }
                if (found == false) {
                    allSpots.insert(data->dxcall, data);
                }
#endif
                emit sendOutput(QString("<spot><action>add</action><dxcall>%1</dxcall><spottercall>%2</spottercall><frequency>%3</frequency><comment>%4</comment></spot>\n")
                    .arg(data->dxcall).arg(data->spottercall).arg(data->frequency).arg(data->comment));

            }
            else if (action == "delete")
            {
                QString dxcall = spot.firstChildElement("dxcall").text();
                double frequency = spot.firstChildElement("frequency").text().toDouble() / 1000.0;

#ifdef USESQL
                database db = database();
                QString query=QString("DELETE from spots where dxcall='%1' AND frequency=%2").arg(dxcall).arg(frequency);
                db.query(query);
                qInfo(logCluster()) << query;
#else
                QMap<QString, spotData*>::iterator spot = allSpots.find(dxcall);
                while (spot != allSpots.end() && spot.key() == dxcall && spot.value()->frequency == frequency) 
                {
                    delete spot.value(); // Stop memory leak?
                    spot = allSpots.erase(spot);
                }
#endif
                emit sendOutput(QString("<spot><action>delete</action><dxcall>%1</dxcall<frequency>%3</frequency></spot>\n")
                    .arg(dxcall).arg(frequency));
            }
            updateSpots();
        }
    }
}


void dxClusterClient::tcpDataReceived()
{
    QString data = QString(tcpSocket->readAll());

    emit sendOutput(data);
    if (!authenticated) {
        if (data.contains("login:") || data.contains("call:") || data.contains("callsign:")) {
            sendTcpData(QString("%1\n").arg(tcpUserName));
            return;
        }
        if (data.contains("password:")) {
            sendTcpData(QString("%1\n").arg(tcpPassword));
            return;
        }
        if (data.contains("Hello")) {
            authenticated = true;
            enableSkimmerSpots(skimmerSpots);
        }
    }
    else {
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

#ifdef USESQL
                database db = database();
                db.query(QString("DELETE from spots where dxcall='%1'").arg(data->dxcall));
                QString query = QString("INSERT INTO spots(type,spottercall,frequency,dxcall,comment,timestamp) VALUES('%1','%2',%3,'%4','%5','%6')\n")
                    .arg("TCP").arg(data->spottercall).arg(data->frequency).arg(data->dxcall).arg(data->comment).arg(data->timestamp.toString("yyyy-MM-dd hh:mm:ss"));
                db.query(query);
#else
                bool found = false;
                QMap<QString, spotData*>::iterator spot = allSpots.find(data->dxcall);
                while (spot != allSpots.end() && spot.key() == data->dxcall && spot.value()->frequency == data->frequency) {
                    found = true;
                    ++spot;
                }
                if (found == false) {
                    allSpots.insert(data->dxcall, data);
                }
#endif
            }
        }
        updateSpots();
    }
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
#ifdef USESQL
    database db = database();
    db.query(QString("DELETE FROM spots where timestamp < datetime('now', '-%1 minutes')").arg(tcpTimeout));
#else
    QMap<QString, spotData*>::iterator spot = allSpots.begin();;
    while (spot != allSpots.end()) {
        if (spot.value()->timestamp.addSecs(tcpTimeout * 60) < QDateTime::currentDateTimeUtc())
        {
            delete spot.value(); // Stop memory leak?
            spot = allSpots.erase(spot);
        } 
        else
        {
            ++spot;
        }
    }
#endif
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
    QList<spotData> spots;
#ifdef USESQL
    // Set the required frequency range.
    QString queryText = QString("SELECT * FROM spots WHERE frequency > %1 AND frequency < %2").arg(lowFreq).arg(highFreq);
    //QString queryText = QString("SELECT * FROM spots");
    database db;
    auto query = db.query(queryText);

    while (query.next()) {
        // Step through all current spots within range
        spotData s = spotData();
        s.dxcall = query.value(query.record().indexOf("dxcall")).toString();
        s.frequency = query.value(query.record().indexOf("frequency")).toDouble();
        spots.append(s);
    }
#else
    QMap<QString, spotData*>::iterator spot = allSpots.begin();;
    while (spot != allSpots.end()) {
        if (spot.value()->frequency > lowFreq && spot.value()->frequency < highFreq)
        {
            spots.append(**spot);
        }
        ++spot;
    }

#endif
    emit sendSpots(spots);
}

void dxClusterClient::enableSkimmerSpots(bool enable)
{
    skimmerSpots = enable;
    if (authenticated) {
        if (skimmerSpots) {
            sendTcpData(QString("Set Dx Filter Skimmer\n"));
        }
        else
        { 
            sendTcpData(QString("Set Dx Filter Not Skimmer\n"));
        }
        
    }
}
