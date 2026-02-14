#include "webserver.h"
#include "logcategories.h"

webServer::webServer(QObject *parent) :
    QObject(parent)
{
}

webServer::~webServer()
{
    if (statusTimer) {
        statusTimer->stop();
    }
    if (wsServer) {
        wsServer->close();
    }
    if (httpServer) {
        httpServer->close();
    }
    qDeleteAll(wsClients);
    wsClients.clear();
}

void webServer::init(quint16 httpPort, quint16 wsPort)
{
    this->setObjectName("Web Server");
    queue = cachingQueue::getInstance();
    rigCaps = queue->getRigCaps();

    connect(queue, SIGNAL(rigCapsUpdated(rigCapabilities*)), this, SLOT(receiveRigCaps(rigCapabilities*)));
    connect(queue, SIGNAL(cacheUpdated(cacheItem)), this, SLOT(receiveCache(cacheItem)));

    // HTTP server for static files
    httpServer = new QTcpServer(this);
    if (httpServer->listen(QHostAddress::Any, httpPort)) {
        qInfo() << "Web HTTP server listening on port" << httpPort;
        connect(httpServer, &QTcpServer::newConnection, this, &webServer::onHttpConnection);
    } else {
        qWarning() << "Web HTTP server failed to listen on port" << httpPort;
    }

    // WebSocket server for rig control
    wsServer = new QWebSocketServer(QStringLiteral("wfview Web"), QWebSocketServer::NonSecureMode, this);
    if (wsServer->listen(QHostAddress::Any, wsPort)) {
        qInfo() << "Web WebSocket server listening on port" << wsPort;
        connect(wsServer, &QWebSocketServer::newConnection, this, &webServer::onWsNewConnection);
    } else {
        qWarning() << "Web WebSocket server failed to listen on port" << wsPort;
    }

    // Periodic status updates (meters, etc.) every 200ms
    statusTimer = new QTimer(this);
    connect(statusTimer, &QTimer::timeout, this, &webServer::sendPeriodicStatus);
    statusTimer->start(200);
}

void webServer::receiveRigCaps(rigCapabilities *caps)
{
    rigCaps = caps;
    // Notify connected clients that rig capabilities changed
    if (rigCaps) {
        QJsonObject obj;
        obj["type"] = "rigConnected";
        obj["model"] = rigCaps->modelName;
        obj["hasTransmit"] = rigCaps->hasTransmit;

        QJsonArray modes;
        for (const modeInfo &mi : rigCaps->modes) {
            modes.append(mi.name);
        }
        obj["modes"] = modes;
        sendJsonToAll(obj);
    }
}

// --- HTTP Static File Serving ---

void webServer::onHttpConnection()
{
    QTcpSocket *socket = httpServer->nextPendingConnection();
    connect(socket, &QTcpSocket::readyRead, this, &webServer::onHttpReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &webServer::onHttpDisconnected);
}

void webServer::onHttpReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) return;

    QByteArray request = socket->readAll();
    QString requestStr = QString::fromUtf8(request);

    // Parse the first line: GET /path HTTP/1.1
    QStringList lines = requestStr.split("\r\n");
    if (lines.isEmpty()) return;

    QStringList parts = lines.first().split(' ');
    if (parts.size() < 2) return;

    QString method = parts[0];
    QString path = parts[1];

    if (method != "GET") {
        sendHttpResponse(socket, 405, "Method Not Allowed", "text/plain", "Method Not Allowed");
        return;
    }

    // Default to index.html
    if (path == "/") {
        path = "/index.html";
    }

    serveStaticFile(socket, path);
}

void webServer::onHttpDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (socket) {
        socket->deleteLater();
    }
}

void webServer::serveStaticFile(QTcpSocket *socket, const QString &path)
{
    // Serve from Qt resource system
    QString resourcePath = ":/web" + path;
    QFile file(resourcePath);

    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        sendHttpResponse(socket, 404, "Not Found", "text/plain", "Not Found");
        return;
    }

    QByteArray body = file.readAll();
    file.close();

    // Determine content type
    QByteArray contentType = "application/octet-stream";
    if (path.endsWith(".html")) contentType = "text/html; charset=utf-8";
    else if (path.endsWith(".js")) contentType = "application/javascript; charset=utf-8";
    else if (path.endsWith(".css")) contentType = "text/css; charset=utf-8";
    else if (path.endsWith(".png")) contentType = "image/png";
    else if (path.endsWith(".svg")) contentType = "image/svg+xml";
    else if (path.endsWith(".ico")) contentType = "image/x-icon";

    sendHttpResponse(socket, 200, "OK", contentType, body);
}

void webServer::sendHttpResponse(QTcpSocket *socket, int statusCode, const QString &statusText,
                                  const QByteArray &contentType, const QByteArray &body)
{
    QByteArray response;
    response.append(QString("HTTP/1.1 %1 %2\r\n").arg(statusCode).arg(statusText).toUtf8());
    response.append("Content-Type: " + contentType + "\r\n");
    response.append(QString("Content-Length: %1\r\n").arg(body.size()).toUtf8());
    response.append("Connection: close\r\n");
    response.append("Access-Control-Allow-Origin: *\r\n");
    response.append("\r\n");
    response.append(body);

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

// --- WebSocket ---

void webServer::onWsNewConnection()
{
    QWebSocket *pSocket = wsServer->nextPendingConnection();

    connect(pSocket, &QWebSocket::textMessageReceived, this, &webServer::onWsTextMessage);
    connect(pSocket, &QWebSocket::disconnected, this, &webServer::onWsDisconnected);

    wsClients.append(pSocket);
    qInfo() << "Web client connected:" << pSocket->peerAddress().toString();

    // Send current state to new client
    sendCurrentState(pSocket);
}

void webServer::onWsTextMessage(QString message)
{
    QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());
    if (!pClient) return;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "Web: Invalid JSON from client:" << parseError.errorString();
        return;
    }

    QJsonObject cmd = doc.object();
    handleCommand(pClient, cmd);
}

void webServer::onWsDisconnected()
{
    QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());
    if (pClient) {
        qInfo() << "Web client disconnected:" << pClient->peerAddress().toString();
        wsClients.removeAll(pClient);
        pClient->deleteLater();
    }
}

void webServer::handleCommand(QWebSocket *client, const QJsonObject &cmd)
{
    QString type = cmd["cmd"].toString();

    if (type == "setFrequency") {
        quint64 hz = cmd["value"].toVariant().toULongLong();
        if (hz > 0) {
            freqt f;
            f.Hz = hz;
            f.MHzDouble = hz / 1.0E6;
            f.VFO = activeVFO;
            vfoCommandType t = queue->getVfoCommand(vfoA, 0, true);
            queue->addUnique(priorityImmediate, queueItem(t.freqFunc, QVariant::fromValue<freqt>(f), false, 0));
        }
    }
    else if (type == "setMode") {
        QString modeName = cmd["value"].toString();
        modeInfo m = stringToMode(modeName);
        if (m.mk != modeUnknown) {
            vfoCommandType t = queue->getVfoCommand(vfoA, 0, true);
            qCDebug(logWebServer) << "setMode:" << modeName << "mk=" << m.mk << "reg=" << m.reg
                                  << "name=" << m.name << "filter=" << m.filter << "func=" << t.modeFunc;
            queue->addUnique(priorityImmediate, queueItem(t.modeFunc, QVariant::fromValue<modeInfo>(m), false, 0));
        } else {
            qCWarning(logWebServer) << "setMode: unknown mode name:" << modeName;
        }
    }
    else if (type == "selectVFO") {
        QString vfoName = cmd["value"].toString();
        vfo_t v = (vfoName == "B") ? vfoB : vfoA;
        queue->addUnique(priorityImmediate, queueItem(funcSelectVFO, QVariant::fromValue<vfo_t>(v), false));
    }
    else if (type == "swapVFO") {
        queue->add(priorityImmediate, funcVFOSwapAB, false, false);
    }
    else if (type == "equalizeVFO") {
        queue->add(priorityImmediate, funcVFOEqualAB, false, false);
    }
    else if (type == "setPTT") {
        bool on = cmd["value"].toBool();
        queue->add(priorityImmediate, queueItem(funcTransceiverStatus, QVariant::fromValue<bool>(on), false, uchar(0)));
    }
    else if (type == "setAfGain") {
        ushort val = static_cast<ushort>(qBound(0, cmd["value"].toInt(), 255));
        queue->addUnique(priorityImmediate, queueItem(funcAfGain, QVariant::fromValue<ushort>(val), false, 0));
    }
    else if (type == "setRfGain") {
        ushort val = static_cast<ushort>(qBound(0, cmd["value"].toInt(), 255));
        queue->addUnique(priorityImmediate, queueItem(funcRfGain, QVariant::fromValue<ushort>(val), false, 0));
    }
    else if (type == "setRfPower") {
        ushort val = static_cast<ushort>(qBound(0, cmd["value"].toInt(), 255));
        queue->addUnique(priorityImmediate, queueItem(funcRFPower, QVariant::fromValue<ushort>(val), false, 0));
    }
    else if (type == "setSquelch") {
        ushort val = static_cast<ushort>(qBound(0, cmd["value"].toInt(), 255));
        queue->addUnique(priorityImmediate, queueItem(funcSquelch, QVariant::fromValue<ushort>(val), false, 0));
    }
    else if (type == "setAttenuator") {
        ushort val = static_cast<ushort>(qBound(0, cmd["value"].toInt(), 255));
        queue->addUnique(priorityImmediate, queueItem(funcAttenuator, QVariant::fromValue<ushort>(val), false, 0));
    }
    else if (type == "setPreamp") {
        ushort val = static_cast<ushort>(qBound(0, cmd["value"].toInt(), 255));
        queue->addUnique(priorityImmediate, queueItem(funcPreamp, QVariant::fromValue<ushort>(val), false, 0));
    }
    else if (type == "setNoiseBlanker") {
        bool on = cmd["value"].toBool();
        queue->add(priorityImmediate, queueItem(funcNoiseBlanker, QVariant::fromValue<uchar>(on ? 1 : 0), false, 0));
    }
    else if (type == "setNoiseReduction") {
        bool on = cmd["value"].toBool();
        queue->add(priorityImmediate, queueItem(funcNoiseReduction, QVariant::fromValue<uchar>(on ? 1 : 0), false, 0));
    }
    else if (type == "setAGC") {
        uchar val = static_cast<uchar>(qBound(0, cmd["value"].toInt(), 255));
        queue->add(priorityImmediate, queueItem(funcAGC, QVariant::fromValue<uchar>(val), false, 0));
    }
    else if (type == "setCompressor") {
        bool on = cmd["value"].toBool();
        queue->add(priorityImmediate, queueItem(funcCompressor, QVariant::fromValue<uchar>(on ? 1 : 0), false, 0));
    }
    else if (type == "setMonitor") {
        bool on = cmd["value"].toBool();
        queue->add(priorityImmediate, queueItem(funcMonitor, QVariant::fromValue<uchar>(on ? 1 : 0), false, 0));
    }
    else if (type == "setTuner") {
        bool on = cmd["value"].toBool();
        queue->add(priorityImmediate, queueItem(funcTunerStatus, QVariant::fromValue<uchar>(on ? 1 : 0), false, 0));
    }
    else if (type == "setSplit") {
        bool on = cmd["value"].toBool();
        queue->add(priorityImmediate, queueItem(funcSplitStatus, QVariant::fromValue<uchar>(on ? 1 : 0), false, 0));
    }
    else if (type == "getStatus") {
        sendCurrentState(client);
    }
    else {
        qWarning() << "Web: Unknown command:" << type;
        QJsonObject err;
        err["type"] = "error";
        err["message"] = QString("Unknown command: %1").arg(type);
        sendJsonTo(client, err);
    }
}

void webServer::sendCurrentState(QWebSocket *client)
{
    // Send rig info
    QJsonObject info;
    info["type"] = "rigInfo";

    if (rigCaps) {
        info["connected"] = true;
        info["model"] = rigCaps->modelName;
        info["hasTransmit"] = rigCaps->hasTransmit;

        QJsonArray modes;
        for (const modeInfo &mi : rigCaps->modes) {
            modes.append(mi.name);
        }
        info["modes"] = modes;
    } else {
        info["connected"] = false;
    }
    sendJsonTo(client, info);

    // Send current status
    if (rigCaps) {
        sendJsonTo(client, buildStatusJson());
    }
}

QJsonObject webServer::buildStatusJson()
{
    QJsonObject status;
    status["type"] = "status";

    vfoCommandType t = queue->getVfoCommand(vfoA, 0, false);

    // Frequency
    cacheItem freqCache = queue->getCache(t.freqFunc, 0);
    if (freqCache.value.isValid()) {
        freqt f = freqCache.value.value<freqt>();
        status["frequency"] = (qint64)f.Hz;
    }

    // Mode
    cacheItem modeCache = queue->getCache(t.modeFunc, 0);
    if (modeCache.value.isValid()) {
        modeInfo m = modeCache.value.value<modeInfo>();
        status["mode"] = modeToString(m);
        status["filter"] = m.filter;
    }

    // S-Meter
    cacheItem smeter = queue->getCache(funcSMeter, 0);
    if (smeter.value.isValid()) {
        status["sMeter"] = smeter.value.toDouble();
    }

    // Power meter
    cacheItem power = queue->getCache(funcPowerMeter, 0);
    if (power.value.isValid()) {
        status["powerMeter"] = power.value.toDouble();
    }

    // SWR
    cacheItem swr = queue->getCache(funcSWRMeter, 0);
    if (swr.value.isValid()) {
        status["swrMeter"] = swr.value.toDouble();
    }

    // TX status
    cacheItem txStatus = queue->getCache(funcTransceiverStatus, 0);
    if (txStatus.value.isValid()) {
        status["transmitting"] = txStatus.value.toBool();
    }

    // AF Gain
    cacheItem afGain = queue->getCache(funcAfGain, 0);
    if (afGain.value.isValid()) {
        status["afGain"] = afGain.value.toInt();
    }

    // RF Gain
    cacheItem rfGain = queue->getCache(funcRfGain, 0);
    if (rfGain.value.isValid()) {
        status["rfGain"] = rfGain.value.toInt();
    }

    // RF Power
    cacheItem rfPower = queue->getCache(funcRFPower, 0);
    if (rfPower.value.isValid()) {
        status["rfPower"] = rfPower.value.toInt();
    }

    // Squelch
    cacheItem squelch = queue->getCache(funcSquelch, 0);
    if (squelch.value.isValid()) {
        status["squelch"] = squelch.value.toInt();
    }

    return status;
}

void webServer::receiveCache(cacheItem item)
{
    if (wsClients.isEmpty()) return;

    QJsonObject update;
    update["type"] = "update";

    funcs func = item.command;

    // Map various freq/mode funcs to canonical names
    if (func == funcFreqTR || func == funcSelectedFreq || func == funcFreq) {
        func = funcFreq;
    } else if (func == funcModeTR || func == funcSelectedMode || func == funcMode) {
        func = funcMode;
    }

    switch (func) {
    case funcFreq:
    case funcFreqGet:
    case funcFreqSet:
    {
        freqt f = item.value.value<freqt>();
        update["frequency"] = (qint64)f.Hz;
        break;
    }
    case funcMode:
    case funcModeGet:
    case funcModeSet:
    {
        modeInfo m = item.value.value<modeInfo>();
        update["mode"] = modeToString(m);
        update["filter"] = m.filter;
        break;
    }
    case funcSMeter:
        update["sMeter"] = item.value.toDouble();
        break;
    case funcPowerMeter:
        update["powerMeter"] = item.value.toDouble();
        break;
    case funcSWRMeter:
        update["swrMeter"] = item.value.toDouble();
        break;
    case funcTransceiverStatus:
        update["transmitting"] = item.value.toBool();
        break;
    case funcAfGain:
        update["afGain"] = item.value.toInt();
        break;
    case funcRfGain:
        update["rfGain"] = item.value.toInt();
        break;
    case funcRFPower:
        update["rfPower"] = item.value.toInt();
        break;
    case funcSquelch:
        update["squelch"] = item.value.toInt();
        break;
    default:
        return; // Don't send updates for unhandled funcs
    }

    sendJsonToAll(update);
}

void webServer::sendPeriodicStatus()
{
    if (wsClients.isEmpty() || !rigCaps) return;

    // Request meter updates by querying current cache values
    QJsonObject status;
    status["type"] = "meters";

    cacheItem smeter = queue->getCache(funcSMeter, 0);
    if (smeter.value.isValid()) {
        status["sMeter"] = smeter.value.toDouble();
    }

    cacheItem power = queue->getCache(funcPowerMeter, 0);
    if (power.value.isValid()) {
        status["powerMeter"] = power.value.toDouble();
    }

    cacheItem swr = queue->getCache(funcSWRMeter, 0);
    if (swr.value.isValid()) {
        status["swrMeter"] = swr.value.toDouble();
    }

    sendJsonToAll(status);
}

void webServer::sendJsonToAll(const QJsonObject &obj)
{
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    for (QWebSocket *client : wsClients) {
        client->sendTextMessage(QString::fromUtf8(data));
    }
}

void webServer::sendJsonTo(QWebSocket *client, const QJsonObject &obj)
{
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    client->sendTextMessage(QString::fromUtf8(data));
}

QString webServer::modeToString(modeInfo m)
{
    if (m.name.isEmpty()) {
        // Fallback
        switch (m.mk) {
        case modeLSB: return "LSB";
        case modeUSB: return "USB";
        case modeAM: return "AM";
        case modeCW: return "CW";
        case modeRTTY: return "RTTY";
        case modeFM: return "FM";
        case modeCW_R: return "CW-R";
        case modeRTTY_R: return "RTTY-R";
        case modeLSB_D: return "LSB-D";
        case modeUSB_D: return "USB-D";
        default: return "Unknown";
        }
    }
    return m.name;
}

modeInfo webServer::stringToMode(const QString &mode)
{
    modeInfo m;
    m.mk = modeUnknown;

    if (!rigCaps) return m;

    for (const modeInfo &mi : rigCaps->modes) {
        if (mi.name.compare(mode, Qt::CaseInsensitive) == 0) {
            m = mi;
            m.filter = 1;
            m.data = 0;
            return m;
        }
    }
    return m;
}
