#include "webserver.h"
#include "logcategories.h"

#include <QStandardPaths>
#include <QDir>
#include <QProcess>
#include <QSslConfiguration>
#include <QTimer>

webServer::webServer(QObject *parent) :
    QObject(parent)
{
}

webServer::~webServer()
{
    // Restore DATA MOD OFF setting if mic was active
    if (dataOffModSaved && queue) {
        queue->addUnique(priorityImmediate, queueItem(funcDATAOffMod, QVariant::fromValue<rigInput>(savedDataOffMod), false, 0));
        dataOffModSaved = false;
        micActiveClient = nullptr;
    }
    if (statusTimer) {
        statusTimer->stop();
    }
    if (usbAudioOutput) {
        usbAudioOutput->stop();
        delete usbAudioOutput;
        usbAudioOutput = nullptr;
        usbAudioOutputDevice = nullptr;
    }
    if (usbAudioInput) {
        usbAudioInput->stop();
        delete usbAudioInput;
        usbAudioInput = nullptr;
    }
    if (txConverterThread) {
        txConverterThread->quit();
        txConverterThread->wait();
    }
    if (rxConverterThread) {
        rxConverterThread->quit();
        rxConverterThread->wait();
    }
    if (wsServer) {
        wsServer->close();
    }
    if (httpServer) {
        httpServer->close();
    }
    audioClients.clear();
    qDeleteAll(wsClients);
    wsClients.clear();
}

bool webServer::setupSsl()
{
    if (!QSslSocket::supportsSsl()) {
        qInfo() << "Web: SSL not supported on this system, using plain HTTP";
        return false;
    }

    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    QString certPath = dataDir + "/wfview-web.crt";
    QString keyPath = dataDir + "/wfview-web.key";

    // Generate self-signed cert if not present
    if (!QFile::exists(certPath) || !QFile::exists(keyPath)) {
        qInfo() << "Web: Generating self-signed SSL certificate...";
        QProcess proc;
        proc.start("openssl", QStringList()
            << "req" << "-x509" << "-newkey" << "rsa:2048"
            << "-keyout" << keyPath << "-out" << certPath
            << "-days" << "3650" << "-nodes"
            << "-subj" << "/CN=wfview");
        if (!proc.waitForFinished(10000) || proc.exitCode() != 0) {
            qWarning() << "Web: Failed to generate SSL certificate:" << proc.readAllStandardError();
            return false;
        }
        qInfo() << "Web: SSL certificate generated at" << certPath;
    }

    // Load certificate
    QFile certFile(certPath);
    if (!certFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Web: Cannot read SSL certificate";
        return false;
    }
    sslCert = QSslCertificate(&certFile, QSsl::Pem);
    certFile.close();
    if (sslCert.isNull()) {
        qWarning() << "Web: Invalid SSL certificate";
        return false;
    }

    // Load private key
    QFile keyFile(keyPath);
    if (!keyFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Web: Cannot read SSL private key";
        return false;
    }
    sslKey = QSslKey(&keyFile, QSsl::Rsa, QSsl::Pem);
    keyFile.close();
    if (sslKey.isNull()) {
        qWarning() << "Web: Invalid SSL private key";
        return false;
    }

    qInfo() << "Web: SSL certificate loaded successfully";
    return true;
}

void webServer::init(quint16 httpPort, quint16 wsPort)
{
    this->setObjectName("Web Server");
    queue = cachingQueue::getInstance();
    rigCaps = queue->getRigCaps();

    connect(queue, SIGNAL(rigCapsUpdated(rigCapabilities*)), this, SLOT(receiveRigCaps(rigCapabilities*)));
    connect(queue, SIGNAL(cacheUpdated(cacheItem)), this, SLOT(receiveCache(cacheItem)));

    sslEnabled = setupSsl();

    if (sslEnabled) {
        // HTTPS + WSS on a single port
        SslTcpServer *sslServer = new SslTcpServer(this);
        sslServer->cert = sslCert;
        sslServer->key = sslKey;
        httpServer = sslServer;

        if (httpServer->listen(QHostAddress::Any, httpPort)) {
            qInfo() << "Web HTTPS server listening on port" << httpPort;
            connect(httpServer, &QTcpServer::newConnection, this, &webServer::onHttpConnection);
        } else {
            qWarning() << "Web HTTPS server failed to listen on port" << httpPort;
        }

        // WebSocket server in NonSecureMode (SSL handled by SslTcpServer)
        // Does NOT listen on its own port — connections handed off via handleConnection
        wsServer = new QWebSocketServer(QStringLiteral("wfview Web"), QWebSocketServer::NonSecureMode, this);
        connect(wsServer, &QWebSocketServer::newConnection, this, &webServer::onWsNewConnection);
    } else {
        // Plain HTTP + WS on separate ports (fallback)
        httpServer = new QTcpServer(this);
        if (httpServer->listen(QHostAddress::Any, httpPort)) {
            qInfo() << "Web HTTP server listening on port" << httpPort;
            connect(httpServer, &QTcpServer::newConnection, this, &webServer::onHttpConnection);
        } else {
            qWarning() << "Web HTTP server failed to listen on port" << httpPort;
        }

        wsServer = new QWebSocketServer(QStringLiteral("wfview Web"), QWebSocketServer::NonSecureMode, this);
        if (wsServer->listen(QHostAddress::Any, wsPort)) {
            qInfo() << "Web WebSocket server listening on port" << wsPort;
            connect(wsServer, &QWebSocketServer::newConnection, this, &webServer::onWsNewConnection);
        } else {
            qWarning() << "Web WebSocket server failed to listen on port" << wsPort;
        }
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
        obj["hasSpectrum"] = rigCaps->hasSpectrum;
        obj["spectLenMax"] = rigCaps->spectLenMax;
        obj["spectAmpMax"] = rigCaps->spectAmpMax;

        QJsonArray modes;
        for (const modeInfo &mi : rigCaps->modes) {
            modes.append(mi.name);
        }
        obj["modes"] = modes;
        obj["audioAvailable"] = audioConfigured;
        if (audioConfigured) {
            obj["audioSampleRate"] = (int)rigSampleRate;
        }
        obj["txAudioAvailable"] = txAudioConfigured;
        if (!rigCaps->scopeCenterSpans.empty()) {
            QJsonArray spans;
            for (const centerSpanData &s : rigCaps->scopeCenterSpans) {
                QJsonObject span;
                span["reg"] = s.reg;
                span["name"] = s.name;
                span["freq"] = (int)s.freq;
                spans.append(span);
            }
            obj["spans"] = spans;
        }
        if (!rigCaps->preamps.empty()) {
            QJsonArray preamps;
            for (const genericType &p : rigCaps->preamps) {
                QJsonObject po;
                po["num"] = p.num;
                po["name"] = p.name;
                preamps.append(po);
            }
            obj["preamps"] = preamps;
        }
        if (!rigCaps->filters.empty()) {
            QJsonArray filters;
            for (const filterType &f : rigCaps->filters) {
                QJsonObject fo;
                fo["num"] = f.num;
                fo["name"] = f.name;
                filters.append(fo);
            }
            obj["filters"] = filters;
        }
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

    // In SSL single-port mode, detect WebSocket upgrade requests
    if (sslEnabled) {
        QByteArray peek = socket->peek(4096);
        if (peek.contains("Upgrade: websocket") || peek.contains("Upgrade: WebSocket")) {
            disconnect(socket, &QTcpSocket::readyRead, this, &webServer::onHttpReadyRead);
            disconnect(socket, &QTcpSocket::disconnected, this, &webServer::onHttpDisconnected);
            wsServer->handleConnection(socket);
            return;
        }
    }

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
    connect(pSocket, &QWebSocket::binaryMessageReceived, this, &webServer::onWsBinaryMessage);
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
        // Restore DATA MOD OFF if this client had mic active
        if (pClient == micActiveClient && dataOffModSaved && queue) {
            queue->addUnique(priorityImmediate, queueItem(funcDATAOffMod, QVariant::fromValue<rigInput>(savedDataOffMod), false, 0));
            dataOffModSaved = false;
            micActiveClient = nullptr;
            qInfo() << "Web: Restored DATA MOD OFF setting (client disconnected)";
        }
        audioClients.remove(pClient);
        wsClients.removeAll(pClient);
        pClient->deleteLater();
    }
}

void webServer::onWsBinaryMessage(QByteArray message)
{
    if (message.size() < 6) return;

    quint8 msgType = static_cast<quint8>(message[0]);

    if (msgType != 0x03) return; // Only handle TX audio (0x03)

    if (!txAudioConfigured) return;

    // Extract PCM data after 6-byte header: [0x03][0x00][seq_u16LE][reserved_u16LE][PCM...]
    QByteArray pcmData = message.mid(6);
    if (pcmData.isEmpty()) return;

    if (usbAudioOutputDevice) {
        // USB path: write directly to USB audio output device
        if (usbOutputChannels == 2) {
            int numSamples = pcmData.size() / 2; // 16-bit mono samples
            QByteArray stereo;
            stereo.resize(numSamples * 4); // 16-bit stereo
            const qint16 *src = reinterpret_cast<const qint16 *>(pcmData.constData());
            qint16 *dst = reinterpret_cast<qint16 *>(stereo.data());
            for (int i = 0; i < numSamples; i++) {
                dst[i * 2] = src[i];
                dst[i * 2 + 1] = src[i];
            }
            usbAudioOutputDevice->write(stereo);
        } else {
            usbAudioOutputDevice->write(pcmData);
        }
    } else if (txConverter) {
        // LAN path: encode PCM → rig codec, then emit for transmission
        audioPacket pkt;
        pkt.data = pcmData;
        pkt.time = QTime::currentTime();
        pkt.sent = 0;
        pkt.volume = 1.0;
        emit sendToTxConverter(pkt);
    }
}

void webServer::onTxConverted(audioPacket audio)
{
    if (audio.data.isEmpty()) return;
    emit haveTxAudioData(audio);
}

void webServer::requestVfoUpdate()
{
    // After a VFO change, request fresh freq/mode from the radio.
    // Use a short delay to let the queue process the VFO command first,
    // then the normal receiveCache flow pushes the update to web clients.
    QTimer::singleShot(200, this, [this]() {
        if (!queue) return;
        vfoCommandType t = queue->getVfoCommand(vfoA, 0, false);
        if (t.freqFunc != funcNone)
            queue->add(priorityImmediate, t.freqFunc, false, 0);
        if (t.modeFunc != funcNone)
            queue->add(priorityImmediate, t.modeFunc, false, 0);
    });
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
        requestVfoUpdate();
    }
    else if (type == "swapVFO") {
        queue->add(priorityImmediate, funcVFOSwapAB, false, false);
        requestVfoUpdate();
    }
    else if (type == "equalizeVFO") {
        queue->add(priorityImmediate, funcVFOEqualAB, false, false);
        requestVfoUpdate();
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
        uchar val = static_cast<uchar>(qBound(0, cmd["value"].toInt(), 255));
        queue->addUnique(priorityImmediate, queueItem(funcPreamp, QVariant::fromValue<uchar>(val), false, 0));
    }
    else if (type == "setNoiseBlanker") {
        bool on = cmd["value"].toBool();
        queue->addUnique(priorityImmediate, queueItem(funcNoiseBlanker, QVariant::fromValue<uchar>(on ? 1 : 0), false, 0));
        QTimer::singleShot(200, this, [this]() {
            if (queue) queue->add(priorityImmediate, funcNoiseBlanker, false, 0);
        });
    }
    else if (type == "setNoiseReduction") {
        bool on = cmd["value"].toBool();
        queue->addUnique(priorityImmediate, queueItem(funcNoiseReduction, QVariant::fromValue<uchar>(on ? 1 : 0), false, 0));
        QTimer::singleShot(200, this, [this]() {
            if (queue) queue->add(priorityImmediate, funcNoiseReduction, false, 0);
        });
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
        // value: 0=off, 1=on, 2=start tuning
        uchar val = static_cast<uchar>(qBound(0, cmd["value"].toInt(), 2));
        queue->addUnique(priorityImmediate, queueItem(funcTunerStatus, QVariant::fromValue<uchar>(val), false, 0));
        // Query status to trigger cache update
        QTimer::singleShot(200, this, [this]() {
            if (queue) queue->add(priorityImmediate, funcTunerStatus, false, 0);
        });
    }
    else if (type == "setAutoNotch") {
        bool on = cmd["value"].toBool();
        queue->addUnique(priorityImmediate, queueItem(funcAutoNotch, QVariant::fromValue<uchar>(on ? 1 : 0), false, 0));
        QTimer::singleShot(200, this, [this]() {
            if (queue) queue->add(priorityImmediate, funcAutoNotch, false, 0);
        });
    }
    else if (type == "setFilterWidth") {
        ushort val = static_cast<ushort>(qBound(0, cmd["value"].toInt(), 10000));
        queue->addUnique(priorityImmediate, queueItem(funcFilterWidth, QVariant::fromValue<ushort>(val), false, 0));
    }
    else if (type == "setFilter") {
        int filterNum = cmd["value"].toInt();
        if (filterNum >= 1 && filterNum <= 3) {
            vfoCommandType t = queue->getVfoCommand(vfoA, 0, false);
            cacheItem modeCache = queue->getCache(t.modeFunc, 0);
            if (modeCache.value.isValid()) {
                modeInfo m = modeCache.value.value<modeInfo>();
                m.filter = filterNum;
                queue->add(priorityImmediate, queueItem(t.modeFunc, QVariant::fromValue<modeInfo>(m), false, 0));
            }
        }
    }
    else if (type == "setSplit") {
        bool on = cmd["value"].toBool();
        queue->add(priorityImmediate, queueItem(funcSplitStatus, QVariant::fromValue<uchar>(on ? 1 : 0), false, 0));
    }
    else if (type == "setSpan") {
        int idx = cmd["value"].toInt();
        if (rigCaps && idx >= 0 && idx < (int)rigCaps->scopeCenterSpans.size()) {
            centerSpanData span = rigCaps->scopeCenterSpans.at(idx);
            queue->addUnique(priorityImmediate, queueItem(funcScopeSpan, QVariant::fromValue<centerSpanData>(span), false, 0));
        }
    }
    else if (type == "enableAudio") {
        bool enable = cmd["value"].toBool();
        if (enable) {
            if (audioConfigured) {
                audioClients.insert(client);
                qInfo() << "Web: Audio enabled for client" << client->peerAddress().toString();
                QJsonObject resp;
                resp["type"] = "audioStatus";
                resp["enabled"] = true;
                resp["sampleRate"] = (int)rigSampleRate;
                sendJsonTo(client, resp);
            } else {
                QJsonObject resp;
                resp["type"] = "audioStatus";
                resp["enabled"] = false;
                resp["reason"] = "Audio not available (USB rig or not configured)";
                sendJsonTo(client, resp);
            }
        } else {
            audioClients.remove(client);
            qInfo() << "Web: Audio disabled for client" << client->peerAddress().toString();
            QJsonObject resp;
            resp["type"] = "audioStatus";
            resp["enabled"] = false;
            sendJsonTo(client, resp);
        }
    }
    else if (type == "enableMic") {
        bool enable = cmd["value"].toBool();
        if (enable) {
            // Save current DATA MOD OFF setting and switch to USB
            cacheItem cache = queue->getCache(funcDATAOffMod, 0);
            if (cache.value.isValid()) {
                savedDataOffMod = cache.value.value<rigInput>();
                dataOffModSaved = true;
            }
            // Find USB input from rig capabilities
            if (rigCaps) {
                rigInput usbInput;
                bool found = false;
                for (const rigInput &inp : rigCaps->inputs) {
                    if (inp.type == inputUSB) {
                        usbInput = inp;
                        found = true;
                        break;
                    }
                }
                if (found) {
                    queue->addUnique(priorityImmediate, queueItem(funcDATAOffMod, QVariant::fromValue<rigInput>(usbInput), false, 0));
                    qInfo() << "Web: Set DATA MOD OFF to USB for web mic";
                } else {
                    qInfo() << "Web: No USB input found in rig capabilities";
                }
            }
            micActiveClient = client;
        } else {
            // Restore previous DATA MOD OFF setting
            if (dataOffModSaved && queue) {
                queue->addUnique(priorityImmediate, queueItem(funcDATAOffMod, QVariant::fromValue<rigInput>(savedDataOffMod), false, 0));
                qInfo() << "Web: Restored DATA MOD OFF setting";
            }
            dataOffModSaved = false;
            micActiveClient = nullptr;
        }
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
        info["hasSpectrum"] = rigCaps->hasSpectrum;
        info["spectLenMax"] = rigCaps->spectLenMax;
        info["spectAmpMax"] = rigCaps->spectAmpMax;

        QJsonArray modes;
        for (const modeInfo &mi : rigCaps->modes) {
            modes.append(mi.name);
        }
        info["modes"] = modes;
        info["audioAvailable"] = audioConfigured;
        if (audioConfigured) {
            info["audioSampleRate"] = (int)rigSampleRate;
        }
        info["txAudioAvailable"] = txAudioConfigured;
        if (!rigCaps->scopeCenterSpans.empty()) {
            QJsonArray spans;
            for (const centerSpanData &s : rigCaps->scopeCenterSpans) {
                QJsonObject span;
                span["reg"] = s.reg;
                span["name"] = s.name;
                span["freq"] = (int)s.freq;
                spans.append(span);
            }
            info["spans"] = spans;
        }
        if (!rigCaps->preamps.empty()) {
            QJsonArray preamps;
            for (const genericType &p : rigCaps->preamps) {
                QJsonObject obj;
                obj["num"] = p.num;
                obj["name"] = p.name;
                preamps.append(obj);
            }
            info["preamps"] = preamps;
        }
        if (!rigCaps->filters.empty()) {
            QJsonArray filters;
            for (const filterType &f : rigCaps->filters) {
                QJsonObject obj;
                obj["num"] = f.num;
                obj["name"] = f.name;
                filters.append(obj);
            }
            info["filters"] = filters;
        }
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

    // Frequency - keep current VFO frequency for backwards compat
    cacheItem freqCache = queue->getCache(t.freqFunc, 0);
    if (freqCache.value.isValid()) {
        freqt f = freqCache.value.value<freqt>();
        status["frequency"] = (qint64)f.Hz;
    }

    // VFO A and VFO B frequencies (send both)
    vfoCommandType tA = queue->getVfoCommand(vfoA, 0, false);
    cacheItem freqCacheA = queue->getCache(tA.freqFunc, 0);
    if (freqCacheA.value.isValid()) {
        freqt fA = freqCacheA.value.value<freqt>();
        status["vfoAFrequency"] = (qint64)fA.Hz;
    }

    vfoCommandType tB = queue->getVfoCommand(vfoB, 0, false);
    cacheItem freqCacheB = queue->getCache(tB.freqFunc, 0);
    if (freqCacheB.value.isValid()) {
        freqt fB = freqCacheB.value.value<freqt>();
        status["vfoBFrequency"] = (qint64)fB.Hz;
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

    // Split
    cacheItem split = queue->getCache(funcSplitStatus, 0);
    if (split.value.isValid()) {
        status["split"] = split.value.toBool();
    }

    // Tuner (0=off, 1=on, 2=tuning)
    cacheItem tuner = queue->getCache(funcTunerStatus, 0);
    if (tuner.value.isValid()) status["tuner"] = tuner.value.toInt();

    // Preamp
    cacheItem preamp = queue->getCache(funcPreamp, 0);
    if (preamp.value.isValid()) status["preamp"] = preamp.value.toInt();

    // Auto Notch
    cacheItem anf = queue->getCache(funcAutoNotch, 0);
    if (anf.value.isValid()) status["autoNotch"] = anf.value.toBool();

    // Noise Blanker
    cacheItem nb = queue->getCache(funcNoiseBlanker, 0);
    if (nb.value.isValid()) status["nb"] = nb.value.toBool();

    // Noise Reduction
    cacheItem nr = queue->getCache(funcNoiseReduction, 0);
    if (nr.value.isValid()) status["nr"] = nr.value.toBool();

    // IF Filter Width
    cacheItem fw = queue->getCache(funcFilterWidth, 0);
    if (fw.value.isValid()) status["filterWidth"] = fw.value.toInt();

    // Spectrum span
    cacheItem spanCache = queue->getCache(funcScopeSpan, 0);
    if (spanCache.value.isValid() && rigCaps) {
        centerSpanData span = spanCache.value.value<centerSpanData>();
        for (int i = 0; i < (int)rigCaps->scopeCenterSpans.size(); i++) {
            if (rigCaps->scopeCenterSpans.at(i).reg == span.reg) {
                status["spanIndex"] = i;
                break;
            }
        }
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
    case funcScopeWaveData:
    {
        // Send spectrum data as binary message for efficiency
        scopeData sd = item.value.value<scopeData>();
        if (!sd.valid || sd.data.isEmpty()) return;

        // Binary format: [msgType(1)] [reserved(1)] [padding(2)] [startFreq float32(4)] [endFreq float32(4)] [data(N)]
        QByteArray msg;
        msg.resize(12 + sd.data.size());
        msg[0] = 0x01;  // msgType: spectrum data
        msg[1] = 0;     // reserved
        msg[2] = 0;     // padding
        msg[3] = 0;     // padding

        float startF = static_cast<float>(sd.startFreq);
        float endF = static_cast<float>(sd.endFreq);
        memcpy(msg.data() + 4, &startF, 4);
        memcpy(msg.data() + 8, &endF, 4);
        memcpy(msg.data() + 12, sd.data.constData(), sd.data.size());

        sendBinaryToAll(msg);
        return; // Don't send as JSON
    }
    case funcSplitStatus:
        update["split"] = item.value.toBool();
        break;
    case funcTunerStatus:
        update["tuner"] = item.value.toInt();  // 0=off, 1=on, 2=tuning
        break;
    case funcPreamp:
        update["preamp"] = item.value.toInt();
        break;
    case funcAutoNotch:
        update["autoNotch"] = item.value.toBool();
        break;
    case funcNoiseBlanker:
        update["nb"] = item.value.toBool();
        break;
    case funcNoiseReduction:
        update["nr"] = item.value.toBool();
        break;
    case funcFilterWidth:
        update["filterWidth"] = item.value.toInt();
        break;
    case funcScopeSpan:
    {
        centerSpanData span = item.value.value<centerSpanData>();
        if (rigCaps) {
            for (int i = 0; i < (int)rigCaps->scopeCenterSpans.size(); i++) {
                if (rigCaps->scopeCenterSpans.at(i).reg == span.reg) {
                    update["spanIndex"] = i;
                    update["spanName"] = span.name;
                    break;
                }
            }
        }
        break;
    }
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

void webServer::sendBinaryToAll(const QByteArray &data)
{
    for (QWebSocket *client : wsClients) {
        client->sendBinaryMessage(data);
    }
}

void webServer::sendBinaryToAudioClients(const QByteArray &data)
{
    for (QWebSocket *client : audioClients) {
        client->sendBinaryMessage(data);
    }
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

// --- Audio Streaming ---

codecType webServer::codecByteToType(quint8 codec)
{
    switch (codec) {
    case 0x01:
    case 0x20:
        return PCMU;
    case 0x40:
    case 0x41:
        return OPUS;
    case 0x80:
        return ADPCM;
    case 0x02:
    case 0x04:
    case 0x08:
    case 0x10:
    default:
        return LPCM;
    }
}

void webServer::setupAudio(quint8 codec, quint32 sampleRate)
{
    if (audioConfigured) {
        qInfo() << "Web: Audio already configured, skipping";
        return;
    }
    if (codec == 0 || sampleRate == 0) {
        qInfo() << "Web: Audio not available (codec=0 or sampleRate=0)";
        return;
    }

    rigCodec = codec;
    rigSampleRate = sampleRate;

    // Create audio converter in its own thread
    rxConverter = new audioConverter();
    rxConverterThread = new QThread(this);
    rxConverterThread->setObjectName("webAudioConv()");
    rxConverter->moveToThread(rxConverterThread);

    connect(this, &webServer::setupConverter, rxConverter, &audioConverter::init);
    connect(this, &webServer::sendToConverter, rxConverter, &audioConverter::convert);
    connect(rxConverter, &audioConverter::converted, this, &webServer::onRxConverted);
    connect(rxConverterThread, &QThread::finished, rxConverter, &QObject::deleteLater);

    rxConverterThread->start();

    // Input format: what the rig sends
    QAudioFormat inFormat = toQAudioFormat(codec, sampleRate);
    codecType inCodec = codecByteToType(codec);

    // Output format: 16-bit signed LE mono at the rig's sample rate
    // (browser handles any resampling needed)
    QAudioFormat outFormat;
    outFormat.setSampleRate(sampleRate);
    outFormat.setChannelCount(1);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    outFormat.setSampleSize(16);
    outFormat.setSampleType(QAudioFormat::SignedInt);
    outFormat.setByteOrder(QAudioFormat::LittleEndian);
    outFormat.setCodec("audio/pcm");
#else
    outFormat.setSampleFormat(QAudioFormat::Int16);
#endif

    emit setupConverter(inFormat, inCodec, outFormat, LPCM, 5, 4);

    // TX converter: PCM Int16 (from browser) → rig codec (for transmission)
    txConverter = new audioConverter();
    txConverterThread = new QThread(this);
    txConverterThread->setObjectName("webTxConv()");
    txConverter->moveToThread(txConverterThread);

    connect(this, &webServer::setupTxConverter, txConverter, &audioConverter::init);
    connect(this, &webServer::sendToTxConverter, txConverter, &audioConverter::convert);
    connect(txConverter, &audioConverter::converted, this, &webServer::onTxConverted);
    connect(txConverterThread, &QThread::finished, txConverter, &QObject::deleteLater);

    txConverterThread->start();

    // TX: input is PCM Int16 mono (from browser), output is rig codec format
    emit setupTxConverter(outFormat, LPCM, inFormat, inCodec, 5, 4);

    txAudioConfigured = true;
    audioConfigured = true;
    qInfo() << "Web: Audio configured, codec=" << Qt::hex << codec
            << "sampleRate=" << Qt::dec << sampleRate;

    // Notify already-connected web clients that audio is now available
    if (!wsClients.isEmpty()) {
        QJsonObject notify;
        notify["type"] = "audioAvailable";
        notify["available"] = true;
        notify["sampleRate"] = (int)sampleRate;
        notify["txAudioAvailable"] = true;
        sendJsonToAll(notify);
    }
}

void webServer::receiveRxAudio(audioPacket audio)
{
    if (audioClients.isEmpty() || !audioConfigured) return;
    emit sendToConverter(audio);
}

void webServer::onRxConverted(audioPacket audio)
{
    if (audioClients.isEmpty()) return;

    // Binary format: [0x02][0x00][seq_u16LE][rateDiv_u16LE][PCM_Int16LE...]
    // rateDiv = sampleRate / 1000 (e.g. 48 for 48kHz)
    quint16 rateDiv = static_cast<quint16>(rigSampleRate / 1000);
    QByteArray msg;
    int headerSize = 6;
    msg.resize(headerSize + audio.data.size());
    msg[0] = 0x02;  // msgType: RX audio
    msg[1] = 0x00;  // reserved

    quint16 seq = audioSeq++;
    memcpy(msg.data() + 2, &seq, 2);
    memcpy(msg.data() + 4, &rateDiv, 2);
    memcpy(msg.data() + headerSize, audio.data.constData(), audio.data.size());

    sendBinaryToAudioClients(msg);
}

void webServer::setupUsbAudio(quint32 sampleRate)
{
    if (audioConfigured) {
        qInfo() << "Web: Audio already configured, skipping";
        return;
    }

    // Find the rig's USB audio capture device
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QAudioDeviceInfo usbDevice;
    bool found = false;
    for (const QAudioDeviceInfo &dev : QAudioDeviceInfo::availableDevices(QAudio::AudioInput)) {
        if (dev.deviceName().contains("USB", Qt::CaseInsensitive)) {
            usbDevice = dev;
            found = true;
            qInfo() << "Web: Found USB audio device:" << dev.deviceName();
            break;
        }
    }
    if (!found) {
        qInfo() << "Web: No USB audio device found for direct capture";
        return;
    }

    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(1);
    format.setSampleSize(16);
    format.setSampleType(QAudioFormat::SignedInt);
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setCodec("audio/pcm");

    if (!usbDevice.isFormatSupported(format)) {
        // Try stereo (some USB codecs only support stereo)
        format.setChannelCount(2);
        if (!usbDevice.isFormatSupported(format)) {
            format = usbDevice.nearestFormat(format);
            qInfo() << "Web: Using nearest format:" << format.sampleRate()
                     << "ch=" << format.channelCount()
                     << "size=" << format.sampleSize();
        }
    }

    usbAudioInput = new QAudioInput(usbDevice, format, this);
#else
    QAudioDevice usbDevice;
    bool found = false;
    for (const QAudioDevice &dev : QMediaDevices::audioInputs()) {
        if (dev.description().contains("USB", Qt::CaseInsensitive)) {
            usbDevice = dev;
            found = true;
            qInfo() << "Web: Found USB audio device:" << dev.description();
            break;
        }
    }
    if (!found) {
        qInfo() << "Web: No USB audio device found for direct capture";
        return;
    }

    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    usbAudioInput = new QAudioSource(usbDevice, format, this);
#endif

    rigSampleRate = format.sampleRate();

    usbAudioDevice = usbAudioInput->start();
    if (!usbAudioDevice) {
        qWarning() << "Web: Failed to start USB audio capture";
        delete usbAudioInput;
        usbAudioInput = nullptr;
        return;
    }

    connect(usbAudioDevice, &QIODevice::readyRead, this, &webServer::readUsbAudio);

    audioConfigured = true;
    qInfo() << "Web: USB audio capture configured, sampleRate=" << rigSampleRate
            << "channels=" << format.channelCount();

    // Also configure USB audio output (playback) for TX audio
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QAudioDeviceInfo usbOutDevice;
    bool outFound = false;
    for (const QAudioDeviceInfo &dev : QAudioDeviceInfo::availableDevices(QAudio::AudioOutput)) {
        if (dev.deviceName().contains("USB", Qt::CaseInsensitive)) {
            usbOutDevice = dev;
            outFound = true;
            qInfo() << "Web: Found USB audio output device:" << dev.deviceName();
            break;
        }
    }
    if (outFound) {
        QAudioFormat outFormat;
        outFormat.setSampleRate(sampleRate);
        outFormat.setChannelCount(1);
        outFormat.setSampleSize(16);
        outFormat.setSampleType(QAudioFormat::SignedInt);
        outFormat.setByteOrder(QAudioFormat::LittleEndian);
        outFormat.setCodec("audio/pcm");

        usbOutputChannels = 1;
        if (!usbOutDevice.isFormatSupported(outFormat)) {
            outFormat.setChannelCount(2);
            usbOutputChannels = 2;
            if (!usbOutDevice.isFormatSupported(outFormat)) {
                outFormat = usbOutDevice.nearestFormat(outFormat);
                usbOutputChannels = outFormat.channelCount();
                qInfo() << "Web: TX using nearest format:" << outFormat.sampleRate()
                         << "ch=" << outFormat.channelCount()
                         << "size=" << outFormat.sampleSize();
            }
        }

        usbAudioOutput = new QAudioOutput(usbOutDevice, outFormat, this);
        usbAudioOutputDevice = usbAudioOutput->start();
        if (usbAudioOutputDevice) {
            txAudioConfigured = true;
            qInfo() << "Web: USB audio output configured for TX, channels=" << usbOutputChannels;
        } else {
            qWarning() << "Web: Failed to start USB audio output";
            delete usbAudioOutput;
            usbAudioOutput = nullptr;
        }
    }
#else
    QAudioDevice usbOutDevice;
    bool outFound = false;
    for (const QAudioDevice &dev : QMediaDevices::audioOutputs()) {
        if (dev.description().contains("USB", Qt::CaseInsensitive)) {
            usbOutDevice = dev;
            outFound = true;
            qInfo() << "Web: Found USB audio output device:" << dev.description();
            break;
        }
    }
    if (outFound) {
        QAudioFormat outFormat;
        outFormat.setSampleRate(sampleRate);
        outFormat.setChannelCount(1);
        outFormat.setSampleFormat(QAudioFormat::Int16);

        usbOutputChannels = 1;
        // Qt6 QAudioSink handles format negotiation internally

        usbAudioOutput = new QAudioSink(usbOutDevice, outFormat, this);
        usbAudioOutputDevice = usbAudioOutput->start();
        if (usbAudioOutputDevice) {
            txAudioConfigured = true;
            qInfo() << "Web: USB audio output configured for TX";
        } else {
            qWarning() << "Web: Failed to start USB audio output";
            delete usbAudioOutput;
            usbAudioOutput = nullptr;
        }
    }
#endif

    // Notify connected clients
    if (!wsClients.isEmpty()) {
        QJsonObject notify;
        notify["type"] = "audioAvailable";
        notify["available"] = true;
        notify["sampleRate"] = (int)rigSampleRate;
        notify["txAudioAvailable"] = txAudioConfigured;
        sendJsonToAll(notify);
    }
}

void webServer::readUsbAudio()
{
    if (!usbAudioDevice || audioClients.isEmpty()) return;

    QByteArray data = usbAudioDevice->readAll();
    if (data.isEmpty()) return;

    // If stereo, mix down to mono (average L+R)
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    int channels = usbAudioInput ? usbAudioInput->format().channelCount() : 1;
#else
    int channels = usbAudioInput ? usbAudioInput->format().channelCount() : 1;
#endif
    if (channels == 2) {
        int numSamples = data.size() / (2 * channels); // 16-bit stereo
        QByteArray mono;
        mono.resize(numSamples * 2);
        const qint16 *src = reinterpret_cast<const qint16 *>(data.constData());
        qint16 *dst = reinterpret_cast<qint16 *>(mono.data());
        for (int i = 0; i < numSamples; i++) {
            dst[i] = static_cast<qint16>((static_cast<qint32>(src[i*2]) + src[i*2+1]) / 2);
        }
        data = mono;
    }

    // Build binary message: [0x02][0x00][seq_u16LE][rateDiv_u16LE][PCM_Int16LE...]
    quint16 rateDiv = static_cast<quint16>(rigSampleRate / 1000);
    QByteArray msg;
    int headerSize = 6;
    msg.resize(headerSize + data.size());
    msg[0] = 0x02;
    msg[1] = 0x00;

    quint16 seq = audioSeq++;
    memcpy(msg.data() + 2, &seq, 2);
    memcpy(msg.data() + 4, &rateDiv, 2);
    memcpy(msg.data() + headerSize, data.constData(), data.size());

    sendBinaryToAudioClients(msg);
}
