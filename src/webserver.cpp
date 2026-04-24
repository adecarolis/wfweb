#include "webserver.h"
#ifdef FREEDV_SUPPORT
#include "freedvprocessor.h"
#include <codec2/freedv_api.h>
#endif
#include "logcategories.h"

#include <QStandardPaths>
#include <QDir>
#include <QProcess>
#include <QSettings>
#include <QSslConfiguration>
#include <QTimer>
#include <QThread>
#include <QDateTime>

#ifdef Q_OS_WIN
#include <windows.h>
#include <objbase.h>
#endif

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/bn.h>
#endif

#ifdef Q_OS_MACOS
#include "tlsproxy.h"
#endif

// Fixed TX gain applied to RADE modem output (and the EOO frame) in the
// webserver before writing to the USB CODEC. Combined with RADE_TX_SCALE
// in radeprocessor.cpp, this gives a stable drive level that hits ~100%
// power on a full-duty signal. Kept separate from freedvTxGain so the
// classic-FreeDV ALC loop can't affect RADE.
static constexpr float RADE_TX_GAIN = 0.4f;

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
    if (usbAudioPollTimer) {
        usbAudioPollTimer->stop();
        delete usbAudioPollTimer;
        usbAudioPollTimer = nullptr;
    }
    if (usbAudioInput) {
        usbAudioInput->stop();
        delete usbAudioInput;
        usbAudioInput = nullptr;
    }
    if (freedvTxDrainTimer) {
        freedvTxDrainTimer->stop();
    }
#ifdef FREEDV_SUPPORT
    if (freedvThread) {
        freedvThread->quit();
        freedvThread->wait();
    }
#endif
#ifdef RADE_SUPPORT
    if (radeThread) {
        if (radeProcessor) radeProcessor->stopRequested.store(true);
        radeThread->quit();
        radeThread->wait();
    }
#endif
    if (txConverterThread) {
        txConverterThread->quit();
        txConverterThread->wait();
    }
    if (rxConverterThread) {
        rxConverterThread->quit();
        rxConverterThread->wait();
    }
#ifdef Q_OS_MACOS
    if (tlsProxyWorker) {
        tlsProxyWorker->stop();
    }
    if (tlsProxyThread) {
        tlsProxyThread->quit();
        tlsProxyThread->wait();
    }
#endif
    if (wsServer) {
        wsServer->close();
    }
    if (restServer) {
        restServer->close();
    }
    if (httpServer) {
        httpServer->close();
    }
    audioClients.clear();
    qDeleteAll(wsClients);
    wsClients.clear();
}


#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
// Generate a self-signed PEM certificate and key using the OpenSSL C API.
// Returns true on success, writing PEM files to certPath and keyPath.
static bool generateSelfSignedCert(const QString &certPath, const QString &keyPath)
{
    bool ok = false;
    EVP_PKEY *pkey = nullptr;
    X509 *x509 = nullptr;
    BIO *certBio = nullptr;
    BIO *keyBio = nullptr;
    RSA *rsa = nullptr;
    BIGNUM *bn = nullptr;

    // Generate 2048-bit RSA key
    pkey = EVP_PKEY_new();
    if (!pkey) goto cleanup;

    rsa = RSA_new();
    bn = BN_new();
    if (!rsa || !bn) goto cleanup;
    if (!BN_set_word(bn, RSA_F4)) goto cleanup;
    if (!RSA_generate_key_ex(rsa, 2048, bn, nullptr)) goto cleanup;
    if (!EVP_PKEY_assign_RSA(pkey, rsa)) goto cleanup;
    rsa = nullptr; // now owned by pkey

    // Create X509 certificate
    x509 = X509_new();
    if (!x509) goto cleanup;

    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), 365L * 10 * 24 * 3600); // 10 years
    X509_set_pubkey(x509, pkey);

    {
        X509_NAME *name = X509_get_subject_name(x509);
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                   reinterpret_cast<const unsigned char *>("wfweb"), -1, -1, 0);
        X509_set_issuer_name(x509, name); // self-signed
    }

    if (!X509_sign(x509, pkey, EVP_sha256())) goto cleanup;

    // Write certificate PEM
    certBio = BIO_new_file(certPath.toLocal8Bit().constData(), "wb");
    if (!certBio) goto cleanup;
    if (!PEM_write_bio_X509(certBio, x509)) goto cleanup;

    // Write private key PEM (unencrypted)
    keyBio = BIO_new_file(keyPath.toLocal8Bit().constData(), "wb");
    if (!keyBio) goto cleanup;
    if (!PEM_write_bio_PrivateKey(keyBio, pkey, nullptr, nullptr, 0, nullptr, nullptr)) goto cleanup;

    ok = true;

cleanup:
    if (certBio) BIO_free(certBio);
    if (keyBio) BIO_free(keyBio);
    if (x509) X509_free(x509);
    if (pkey) EVP_PKEY_free(pkey);
    if (rsa) RSA_free(rsa);
    if (bn) BN_free(bn);
    return ok;
}
#endif // Q_OS_WIN || Q_OS_MACOS

bool webServer::setupSsl()
{
    if (!QSslSocket::supportsSsl()) {
        qInfo() << "Web: SSL not supported on this system, using plain HTTP";
        return false;
    }

    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    QString certPath = dataDir + "/wfweb-web.crt";
    QString keyPath = dataDir + "/wfweb-web.key";

    // Generate self-signed cert if not present
    if (!QFile::exists(certPath) || !QFile::exists(keyPath)) {
        qInfo() << "Web: Generating self-signed SSL certificate...";
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        // Use OpenSSL C API directly (openssl CLI not available on Windows,
        // avoids Secure Transport issues on macOS)
        if (!generateSelfSignedCert(certPath, keyPath)) {
            qWarning() << "Web: Failed to generate SSL certificate via OpenSSL API";
            return false;
        }
#else
        QProcess proc;
        proc.start("openssl", QStringList()
            << "req" << "-x509" << "-newkey" << "rsa:2048"
            << "-keyout" << keyPath << "-out" << certPath
            << "-days" << "3650" << "-nodes"
            << "-subj" << "/CN=wfweb");
        if (!proc.waitForFinished(10000) || proc.exitCode() != 0) {
            qWarning() << "Web: Failed to generate SSL certificate:" << proc.readAllStandardError();
            return false;
        }
#endif
        qInfo() << "Web: SSL certificate generated at" << certPath;
    }

#ifdef Q_OS_MACOS
    sslCertPath = certPath;
    sslKeyPath = keyPath;
    // On macOS, Qt5 uses Secure Transport which always sends CertificateRequest
    // to clients, causing Safari to prompt for a client certificate.
    // Use an OpenSSL-based TLS proxy instead.
    if (!QSslSocket::sslLibraryVersionString().contains("OpenSSL")) {
        useOpenSslProxy = true;
        qInfo() << "Web: Will use OpenSSL TLS proxy (Secure Transport workaround)";
        return true;
    }
#endif

    // Load certificate into Qt types (non-proxy path)
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
#ifdef Q_OS_MACOS
        if (useOpenSslProxy) {
            // macOS: plain HTTP server on localhost, TLS proxy on the real port
            httpServer = new QTcpServer(this);
            if (httpServer->listen(QHostAddress::LocalHost, 0)) {
                internalHttpPort = httpServer->serverPort();
                qInfo() << "Web: Internal HTTP server on localhost:" << internalHttpPort;
                connect(httpServer, &QTcpServer::newConnection, this, &webServer::onHttpConnection);
            } else {
                qWarning() << "Web: Internal HTTP server failed to listen";
            }

            // Start OpenSSL TLS proxy in its own thread
            tlsProxyWorker = new TlsProxyWorker(httpPort, internalHttpPort, sslCertPath, sslKeyPath);
            tlsProxyThread = new QThread(this);
            tlsProxyThread->setObjectName("TlsProxy");
            tlsProxyWorker->moveToThread(tlsProxyThread);
            connect(tlsProxyThread, &QThread::started, tlsProxyWorker, &TlsProxyWorker::start);
            connect(tlsProxyThread, &QThread::finished, tlsProxyWorker, &QObject::deleteLater);
            connect(tlsProxyWorker, &TlsProxyWorker::error, this, [](const QString &msg) {
                qWarning() << "Web: TLS proxy error:" << msg;
            });
            tlsProxyThread->start();
        } else
#endif
        {
            // HTTPS + WSS on a single port (Linux/Windows: QSslSocket with OpenSSL backend)
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
        }

        // WebSocket server in NonSecureMode (SSL handled by SslTcpServer or TLS proxy)
        wsServer = new QWebSocketServer(QStringLiteral("wfweb Web"), QWebSocketServer::NonSecureMode, this);
        connect(wsServer, &QWebSocketServer::newConnection, this, &webServer::onWsNewConnection);

        // Plain HTTP server on wsPort for microcontrollers/scripts that don't support TLS
        restServer = new QTcpServer(this);
        if (restServer->listen(QHostAddress::Any, wsPort)) {
            qInfo() << "Web plain HTTP REST server listening on port" << wsPort
                    << "(use http:// on this port for scripts/microcontrollers)";
            connect(restServer, &QTcpServer::newConnection, this, &webServer::onHttpConnection);
        } else {
            qWarning() << "Web plain HTTP REST server failed to listen on port" << wsPort;
        }
    } else {
        // Plain HTTP + WS on separate ports (fallback)
        httpServer = new QTcpServer(this);
        if (httpServer->listen(QHostAddress::Any, httpPort)) {
            qInfo() << "Web HTTP server listening on port" << httpPort;
            connect(httpServer, &QTcpServer::newConnection, this, &webServer::onHttpConnection);
        } else {
            qWarning() << "Web HTTP server failed to listen on port" << httpPort;
        }

        wsServer = new QWebSocketServer(QStringLiteral("wfweb Web"), QWebSocketServer::NonSecureMode, this);
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
        obj["hasMainSub"] = rigCaps->hasCommand29;

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
        QJsonArray fdvModes;
#ifdef RADE_SUPPORT
        fdvModes.append("RADE");
#endif
#ifdef FREEDV_SUPPORT
        fdvModes.append("700D");
        fdvModes.append("700E");
        fdvModes.append("1600");
#endif
        obj["freedvModes"] = fdvModes;
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
    QTcpServer *srv = qobject_cast<QTcpServer *>(sender());
    if (!srv) return;
    QTcpSocket *socket = srv->nextPendingConnection();
    connect(socket, &QTcpSocket::readyRead, this, &webServer::onHttpReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &webServer::onHttpDisconnected);
}

void webServer::onHttpReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) return;

    // WebSocket upgrade applies to SSL sockets or connections through the TLS proxy
    bool isSslConnection = qobject_cast<QSslSocket *>(socket);
#ifdef Q_OS_MACOS
    if (!isSslConnection && useOpenSslProxy)
        isSslConnection = (socket->localPort() == internalHttpPort);
#endif
    if (isSslConnection) {
        QByteArray peek = socket->peek(4096);
        if (peek.contains("Upgrade: websocket") || peek.contains("Upgrade: WebSocket")) {
            disconnect(socket, &QTcpSocket::readyRead, this, &webServer::onHttpReadyRead);
            disconnect(socket, &QTcpSocket::disconnected, this, &webServer::onHttpDisconnected);
            wsServer->handleConnection(socket);
            return;
        }
    }

    // Accumulate — body may arrive in a later TCP segment than the headers
    socketBuffers[socket] += socket->readAll();
    const QByteArray &request = socketBuffers[socket];

    // Wait until we have the complete headers
    int sepIdx = request.indexOf("\r\n\r\n");
    if (sepIdx < 0) return;

    qDebug() << "REST headers:" << request.left(sepIdx);

    QByteArray headerSection = request.left(sepIdx);
    QByteArray lowerHeaders = headerSection.toLower();

    // If there's a Content-Length, wait until the full body has arrived
    int clIdx = lowerHeaders.indexOf("content-length:");
    if (clIdx >= 0) {
        int clEnd = lowerHeaders.indexOf("\r\n", clIdx);
        int contentLength = headerSection.mid(clIdx + 15, clEnd - clIdx - 15).trimmed().toInt();
        if (contentLength > 0 && request.length() < sepIdx + 4 + contentLength)
            return; // body not yet complete — wait for next readyRead
    }

    // Full request received — extract body then release the buffer
    QByteArray body;
    if (clIdx >= 0) {
        int clEnd = lowerHeaders.indexOf("\r\n", clIdx);
        int contentLength = headerSection.mid(clIdx + 15, clEnd - clIdx - 15).trimmed().toInt();
        if (contentLength > 0)
            body = request.mid(sepIdx + 4, contentLength);
    }
    socketBuffers.remove(socket);

    // Parse first line: METHOD /path HTTP/1.1
    int firstLineEnd = headerSection.indexOf("\r\n");
    QByteArray firstLine = (firstLineEnd >= 0) ? headerSection.left(firstLineEnd) : headerSection;
    QList<QByteArray> parts = firstLine.split(' ');
    if (parts.size() < 2) return;

    QString method = QString::fromUtf8(parts[0]);
    QString path = QString::fromUtf8(parts[1]);

    // Strip query string
    int qIdx = path.indexOf('?');
    if (qIdx >= 0) path = path.left(qIdx);

    // OPTIONS preflight for CORS
    if (method == "OPTIONS") {
        sendRestResponse(socket, 200, QJsonObject());
        return;
    }

    // REST API routing
    if (path.startsWith("/api/v1/")) {
        handleRestRequest(socket, method, path, body);
        return;
    }

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
        socketBuffers.remove(socket);
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
    else if (path.endsWith(".mjs")) contentType = "application/javascript; charset=utf-8";
    else if (path.endsWith(".map")) contentType = "application/json; charset=utf-8";
    else if (path.endsWith(".css")) contentType = "text/css; charset=utf-8";
    else if (path.endsWith(".png")) contentType = "image/png";
    else if (path.endsWith(".svg")) contentType = "image/svg+xml";
    else if (path.endsWith(".ico")) contentType = "image/x-icon";
    else if (path.endsWith(".onnx")) contentType = "application/octet-stream";

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
    // The resources are embedded in the binary and change every build;
    // there's no CDN in front.  Tell browsers not to cache so a wfweb
    // rebuild never serves stale JS/CSS to a reloaded page.
    response.append("Cache-Control: no-store, no-cache, must-revalidate\r\n");
    response.append("Pragma: no-cache\r\n");
    response.append("\r\n");
    response.append(body);

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

void webServer::sendRestResponse(QTcpSocket *socket, int statusCode, const QJsonObject &json)
{
    QByteArray body;
    if (!json.isEmpty())
        body = QJsonDocument(json).toJson(QJsonDocument::Compact);

    QString statusText;
    switch (statusCode) {
    case 200: statusText = "OK"; break;
    case 202: statusText = "Accepted"; break;
    case 400: statusText = "Bad Request"; break;
    case 404: statusText = "Not Found"; break;
    case 405: statusText = "Method Not Allowed"; break;
    case 503: statusText = "Service Unavailable"; break;
    default:  statusText = "OK"; break;
    }

    QByteArray response;
    response.append(QString("HTTP/1.1 %1 %2\r\n").arg(statusCode).arg(statusText).toUtf8());
    response.append("Content-Type: application/json\r\n");
    response.append(QString("Content-Length: %1\r\n").arg(body.size()).toUtf8());
    response.append("Connection: close\r\n");
    response.append("Access-Control-Allow-Origin: *\r\n");
    response.append("Access-Control-Allow-Methods: GET, PUT, POST, DELETE, OPTIONS\r\n");
    response.append("Access-Control-Allow-Headers: Content-Type\r\n");
    response.append("\r\n");
    response.append(body);

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

void webServer::handleRestRequest(QTcpSocket *socket, const QString &method,
                                   const QString &path, const QByteArray &body)
{
    // Normalize path: strip trailing slash
    QString p = path;
    if (p.length() > 1 && p.endsWith('/')) p.chop(1);

    // Helper: parse JSON body; returns empty object on empty/invalid input
    auto parseBody = [&]() -> QJsonObject {
        if (body.isEmpty()) return QJsonObject();
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(body, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) return QJsonObject();
        return doc.object();
    };

    // --- GET /api/v1/radio ---
    if (p == "/api/v1/radio") {
        if (method != "GET") {
            QJsonObject e; e["error"] = "Method not allowed";
            sendRestResponse(socket, 405, e); return;
        }
        QJsonObject resp;
        resp["info"] = buildInfoJson();
        if (queue && rigCaps) {
            QJsonObject status = buildStatusJson();
            status.remove("type");
            resp["status"] = status;
        }
        sendRestResponse(socket, 200, resp);
        return;
    }

    // --- GET /api/v1/radio/info ---
    if (p == "/api/v1/radio/info") {
        if (method != "GET") {
            QJsonObject e; e["error"] = "Method not allowed";
            sendRestResponse(socket, 405, e); return;
        }
        sendRestResponse(socket, 200, buildInfoJson());
        return;
    }

    // --- GET /api/v1/radio/status ---
    if (p == "/api/v1/radio/status") {
        if (method != "GET") {
            QJsonObject e; e["error"] = "Method not allowed";
            sendRestResponse(socket, 405, e); return;
        }
        if (!queue || !rigCaps) {
            QJsonObject e; e["error"] = "Rig not connected";
            sendRestResponse(socket, 503, e); return;
        }
        QJsonObject status = buildStatusJson();
        status.remove("type");
        sendRestResponse(socket, 200, status);
        return;
    }

    // --- GET/PUT /api/v1/radio/frequency ---
    if (p == "/api/v1/radio/frequency") {
        if (method == "GET") {
            if (!queue || !rigCaps) {
                QJsonObject e; e["error"] = "Rig not connected";
                sendRestResponse(socket, 503, e); return;
            }
            bool cmd29 = rigCaps && rigCaps->hasCommand29;
            uchar rx = cmd29 ? (queue->getState().vfo == vfoSub ? 1 : 0) : 0;
            vfoCommandType t = queue->getVfoCommand(vfoA, rx, false);
            cacheItem freqCache = queue->getCache(t.freqFunc, t.receiver);
            QJsonObject resp;
            if (freqCache.value.isValid()) {
                freqt f = freqCache.value.value<freqt>();
                resp["hz"] = (qint64)f.Hz;
                resp["mhz"] = f.MHzDouble;
            }
            sendRestResponse(socket, 200, resp);
        } else if (method == "PUT") {
            if (!queue || !rigCaps) {
                QJsonObject e; e["error"] = "Rig not connected";
                sendRestResponse(socket, 503, e); return;
            }
            QJsonObject obj = parseBody();
            if (!obj.contains("hz")) {
                QJsonObject e; e["error"] = "Missing field: hz";
                sendRestResponse(socket, 400, e); return;
            }
            quint64 hz = obj["hz"].toVariant().toULongLong();
            if (hz == 0) {
                QJsonObject e; e["error"] = "Invalid frequency";
                sendRestResponse(socket, 400, e); return;
            }
            freqt f;
            f.Hz = hz;
            f.MHzDouble = hz / 1.0E6;
            f.VFO = activeVFO;
            bool cmd29 = rigCaps && rigCaps->hasCommand29;
            uchar rx = cmd29 ? (queue->getState().vfo == vfoSub ? 1 : 0) : 0;
            vfoCommandType t = queue->getVfoCommand(vfoA, rx, true);
            queue->addUnique(priorityImmediate, queueItem(t.freqFunc, QVariant::fromValue<freqt>(f), false, t.receiver));
            QJsonObject resp; resp["status"] = "accepted";
            sendRestResponse(socket, 202, resp);
        } else {
            QJsonObject e; e["error"] = "Method not allowed";
            sendRestResponse(socket, 405, e);
        }
        return;
    }

    // --- GET/PUT /api/v1/radio/mode ---
    if (p == "/api/v1/radio/mode") {
        if (method == "GET") {
            if (!queue || !rigCaps) {
                QJsonObject e; e["error"] = "Rig not connected";
                sendRestResponse(socket, 503, e); return;
            }
            bool cmd29 = rigCaps && rigCaps->hasCommand29;
            uchar rx = cmd29 ? (queue->getState().vfo == vfoSub ? 1 : 0) : 0;
            vfoCommandType t = queue->getVfoCommand(vfoA, rx, false);
            cacheItem modeCache = queue->getCache(t.modeFunc, t.receiver);
            QJsonObject resp;
            if (modeCache.value.isValid()) {
                modeInfo m = modeCache.value.value<modeInfo>();
                resp["mode"] = modeToString(m);
                resp["filter"] = m.filter;
            }
            sendRestResponse(socket, 200, resp);
        } else if (method == "PUT") {
            if (!queue || !rigCaps) {
                QJsonObject e; e["error"] = "Rig not connected";
                sendRestResponse(socket, 503, e); return;
            }
            QJsonObject obj = parseBody();
            if (!obj.contains("mode")) {
                QJsonObject e; e["error"] = "Missing field: mode";
                sendRestResponse(socket, 400, e); return;
            }
            modeInfo m = stringToMode(obj["mode"].toString());
            if (m.mk == modeUnknown) {
                QJsonObject e; e["error"] = "Unknown mode";
                sendRestResponse(socket, 400, e); return;
            }
            if (obj.contains("filter")) {
                int filt = obj["filter"].toInt();
                if (filt >= 1 && filt <= 3) m.filter = filt;
            }
            bool cmd29 = rigCaps && rigCaps->hasCommand29;
            uchar rx = cmd29 ? (queue->getState().vfo == vfoSub ? 1 : 0) : 0;
            vfoCommandType t = queue->getVfoCommand(vfoA, rx, true);
            queue->addUnique(priorityImmediate, queueItem(t.modeFunc, QVariant::fromValue<modeInfo>(m), false, t.receiver));
            QJsonObject resp; resp["status"] = "accepted";
            sendRestResponse(socket, 202, resp);
        } else {
            QJsonObject e; e["error"] = "Method not allowed";
            sendRestResponse(socket, 405, e);
        }
        return;
    }

    // --- GET/PUT /api/v1/radio/vfo ---
    if (p == "/api/v1/radio/vfo") {
        if (method == "GET") {
            if (!queue || !rigCaps) {
                QJsonObject e; e["error"] = "Rig not connected";
                sendRestResponse(socket, 503, e); return;
            }
            QJsonObject resp;
            vfoCommandType tA = queue->getVfoCommand(vfoA, 0, false);
            cacheItem freqA = queue->getCache(tA.freqFunc, 0);
            if (freqA.value.isValid()) resp["vfoA"] = (qint64)freqA.value.value<freqt>().Hz;
            bool cmd29 = rigCaps && rigCaps->hasCommand29;
            vfoCommandType tB = queue->getVfoCommand(vfoB, cmd29 ? 1 : 0, false);
            cacheItem freqB = queue->getCache(tB.freqFunc, cmd29 ? 1 : 0);
            if (freqB.value.isValid()) resp["vfoB"] = (qint64)freqB.value.value<freqt>().Hz;
            sendRestResponse(socket, 200, resp);
        } else if (method == "PUT") {
            if (!queue || !rigCaps) {
                QJsonObject e; e["error"] = "Rig not connected";
                sendRestResponse(socket, 503, e); return;
            }
            QJsonObject obj = parseBody();
            if (obj.contains("action")) {
                QString action = obj["action"].toString();
                if (action == "swap") {
                    funcs swapFunc = funcNone;
                    if (rigCaps) {
                        if (rigCaps->commands.contains(funcVFOSwapAB))
                            swapFunc = funcVFOSwapAB;
                        else if (rigCaps->commands.contains(funcVFOSwapMS))
                            swapFunc = funcVFOSwapMS;
                    }
                    if (swapFunc != funcNone)
                        queue->add(priorityImmediate, swapFunc, false, 0);
                    requestVfoUpdate();
                    QJsonObject resp; resp["status"] = "accepted";
                    sendRestResponse(socket, 202, resp);
                } else if (action == "equalize") {
                    funcs eqFunc = funcNone;
                    if (rigCaps) {
                        if (rigCaps->commands.contains(funcVFOEqualAB))
                            eqFunc = funcVFOEqualAB;
                        else if (rigCaps->commands.contains(funcVFOEqualMS))
                            eqFunc = funcVFOEqualMS;
                    }
                    if (eqFunc != funcNone)
                        queue->add(priorityImmediate, eqFunc, false, 0);
                    requestVfoUpdate();
                    QJsonObject resp; resp["status"] = "accepted";
                    sendRestResponse(socket, 202, resp);
                } else {
                    QJsonObject e; e["error"] = "Unknown action (use swap or equalize)";
                    sendRestResponse(socket, 400, e);
                }
            } else if (obj.contains("active")) {
                QString vfoName = obj["active"].toString();
                bool wantB = (vfoName == "B");
                vfo_t v;
                if (rigCaps && rigCaps->hasCommand29)
                    v = wantB ? vfoSub : vfoMain;
                else
                    v = wantB ? vfoB : vfoA;
                queue->addUnique(priorityImmediate, queueItem(funcSelectVFO, QVariant::fromValue<vfo_t>(v), false));
                requestVfoUpdate();
                QJsonObject resp; resp["status"] = "accepted";
                sendRestResponse(socket, 202, resp);
            } else {
                QJsonObject e; e["error"] = "Missing field: active or action";
                sendRestResponse(socket, 400, e);
            }
        } else {
            QJsonObject e; e["error"] = "Method not allowed";
            sendRestResponse(socket, 405, e);
        }
        return;
    }

    // --- GET/PUT /api/v1/radio/ptt ---
    if (p == "/api/v1/radio/ptt") {
        if (method == "GET") {
            if (!queue || !rigCaps) {
                QJsonObject e; e["error"] = "Rig not connected";
                sendRestResponse(socket, 503, e); return;
            }
            cacheItem txStatus = queue->getCache(funcTransceiverStatus, 0);
            QJsonObject resp;
            if (txStatus.value.isValid()) resp["transmitting"] = txStatus.value.toBool();
            sendRestResponse(socket, 200, resp);
        } else if (method == "PUT") {
            if (!queue || !rigCaps) {
                QJsonObject e; e["error"] = "Rig not connected";
                sendRestResponse(socket, 503, e); return;
            }
            QJsonObject obj = parseBody();
            if (!obj.contains("transmitting")) {
                QJsonObject e; e["error"] = "Missing field: transmitting";
                sendRestResponse(socket, 400, e); return;
            }
            bool on = obj["transmitting"].toBool();
            queue->add(priorityImmediate, queueItem(funcTransceiverStatus, QVariant::fromValue<bool>(on), false, uchar(0)));
            QJsonObject resp; resp["status"] = "accepted";
            sendRestResponse(socket, 202, resp);
        } else {
            QJsonObject e; e["error"] = "Method not allowed";
            sendRestResponse(socket, 405, e);
        }
        return;
    }

    // --- GET /api/v1/radio/meters ---
    if (p == "/api/v1/radio/meters") {
        if (method != "GET") {
            QJsonObject e; e["error"] = "Method not allowed";
            sendRestResponse(socket, 405, e); return;
        }
        if (!queue || !rigCaps) {
            QJsonObject e; e["error"] = "Rig not connected";
            sendRestResponse(socket, 503, e); return;
        }
        QJsonObject resp;
        cacheItem smeter = queue->getCache(funcSMeter, 0);
        if (smeter.value.isValid()) resp["sMeter"] = smeter.value.toDouble();
        cacheItem power = queue->getCache(funcPowerMeter, 0);
        if (power.value.isValid()) resp["powerMeter"] = power.value.toDouble();
        cacheItem swr = queue->getCache(funcSWRMeter, 0);
        if (swr.value.isValid()) resp["swrMeter"] = swr.value.toDouble();
        cacheItem alc = queue->getCache(funcALCMeter, 0);
        if (alc.value.isValid()) resp["alcMeter"] = alc.value.toDouble();
        sendRestResponse(socket, 200, resp);
        return;
    }

    // --- GET/PUT /api/v1/radio/gains ---
    if (p == "/api/v1/radio/gains") {
        if (method == "GET") {
            if (!queue || !rigCaps) {
                QJsonObject e; e["error"] = "Rig not connected";
                sendRestResponse(socket, 503, e); return;
            }
            QJsonObject resp;
            cacheItem afGain = queue->getCache(funcAfGain, 0);
            if (afGain.value.isValid()) resp["afGain"] = afGain.value.toInt();
            cacheItem rfGain = queue->getCache(funcRfGain, 0);
            if (rfGain.value.isValid()) resp["rfGain"] = rfGain.value.toInt();
            cacheItem rfPower = queue->getCache(funcRFPower, 0);
            if (rfPower.value.isValid()) resp["rfPower"] = rfPower.value.toInt();
            cacheItem squelch = queue->getCache(funcSquelch, 0);
            if (squelch.value.isValid()) resp["squelch"] = squelch.value.toInt();
            cacheItem micGain = queue->getCache(funcUSBModLevel, 0);
            if (micGain.value.isValid()) resp["micGain"] = micGain.value.toInt();
            sendRestResponse(socket, 200, resp);
        } else if (method == "PUT") {
            if (!queue || !rigCaps) {
                QJsonObject e; e["error"] = "Rig not connected";
                sendRestResponse(socket, 503, e); return;
            }
            QJsonObject obj = parseBody();
            if (obj.isEmpty()) {
                QJsonObject e; e["error"] = "Empty or invalid JSON body";
                sendRestResponse(socket, 400, e); return;
            }
            if (obj.contains("afGain")) {
                ushort val = static_cast<ushort>(qBound(0, obj["afGain"].toInt(), 255));
                queue->addUnique(priorityImmediate, queueItem(funcAfGain, QVariant::fromValue<ushort>(val), false, 0));
            }
            if (obj.contains("rfGain")) {
                ushort val = static_cast<ushort>(qBound(0, obj["rfGain"].toInt(), 255));
                queue->addUnique(priorityImmediate, queueItem(funcRfGain, QVariant::fromValue<ushort>(val), false, 0));
            }
            if (obj.contains("rfPower")) {
                ushort val = static_cast<ushort>(qBound(0, obj["rfPower"].toInt(), 255));
                queue->addUnique(priorityImmediate, queueItem(funcRFPower, QVariant::fromValue<ushort>(val), false, 0));
            }
            if (obj.contains("squelch")) {
                ushort val = static_cast<ushort>(qBound(0, obj["squelch"].toInt(), 255));
                queue->addUnique(priorityImmediate, queueItem(funcSquelch, QVariant::fromValue<ushort>(val), false, 0));
            }
            if (obj.contains("micGain")) {
                ushort val = static_cast<ushort>(qBound(0, obj["micGain"].toInt(), 255));
                queue->addUnique(priorityImmediate, queueItem(funcUSBModLevel, QVariant::fromValue<ushort>(val), false, 0));
            }
            QJsonObject resp; resp["status"] = "accepted";
            sendRestResponse(socket, 202, resp);
        } else {
            QJsonObject e; e["error"] = "Method not allowed";
            sendRestResponse(socket, 405, e);
        }
        return;
    }

    // --- GET/PUT /api/v1/radio/rx ---
    if (p == "/api/v1/radio/rx") {
        if (method == "GET") {
            if (!queue || !rigCaps) {
                QJsonObject e; e["error"] = "Rig not connected";
                sendRestResponse(socket, 503, e); return;
            }
            QJsonObject resp;
            cacheItem preamp = queue->getCache(funcPreamp, 0);
            if (preamp.value.isValid()) resp["preamp"] = preamp.value.toInt();
            cacheItem att = queue->getCache(funcAttenuator, 0);
            if (att.value.isValid()) resp["attenuator"] = att.value.toInt();
            cacheItem nb = queue->getCache(funcNoiseBlanker, 0);
            if (nb.value.isValid()) resp["nb"] = nb.value.toBool();
            cacheItem nr = queue->getCache(funcNoiseReduction, 0);
            if (nr.value.isValid()) resp["nr"] = nr.value.toBool();
            cacheItem agc = queue->getCache(funcAGC, 0);
            if (agc.value.isValid()) resp["agc"] = agc.value.toInt();
            cacheItem anf = queue->getCache(funcAutoNotch, 0);
            if (anf.value.isValid()) resp["autoNotch"] = anf.value.toBool();
            cacheItem fw = queue->getCache(funcFilterWidth, 0);
            if (fw.value.isValid()) resp["filterWidth"] = fw.value.toInt();
            sendRestResponse(socket, 200, resp);
        } else if (method == "PUT") {
            if (!queue || !rigCaps) {
                QJsonObject e; e["error"] = "Rig not connected";
                sendRestResponse(socket, 503, e); return;
            }
            QJsonObject obj = parseBody();
            if (obj.isEmpty()) {
                QJsonObject e; e["error"] = "Empty or invalid JSON body";
                sendRestResponse(socket, 400, e); return;
            }
            if (obj.contains("preamp")) {
                uchar val = static_cast<uchar>(qBound(0, obj["preamp"].toInt(), 255));
                queue->addUnique(priorityImmediate, queueItem(funcPreamp, QVariant::fromValue<uchar>(val), false, 0));
            }
            if (obj.contains("attenuator")) {
                uchar val = static_cast<uchar>(qBound(0, obj["attenuator"].toInt(), 255));
                queue->addUnique(priorityImmediate, queueItem(funcAttenuator, QVariant::fromValue<uchar>(val), false, 0));
            }
            if (obj.contains("nb")) {
                uchar val = obj["nb"].toBool() ? 1 : 0;
                queue->addUnique(priorityImmediate, queueItem(funcNoiseBlanker, QVariant::fromValue<uchar>(val), false, 0));
            }
            if (obj.contains("nr")) {
                uchar val = obj["nr"].toBool() ? 1 : 0;
                queue->addUnique(priorityImmediate, queueItem(funcNoiseReduction, QVariant::fromValue<uchar>(val), false, 0));
            }
            if (obj.contains("agc")) {
                uchar val = static_cast<uchar>(qBound(0, obj["agc"].toInt(), 255));
                queue->add(priorityImmediate, queueItem(funcAGC, QVariant::fromValue<uchar>(val), false, 0));
            }
            if (obj.contains("autoNotch")) {
                uchar val = obj["autoNotch"].toBool() ? 1 : 0;
                queue->addUnique(priorityImmediate, queueItem(funcAutoNotch, QVariant::fromValue<uchar>(val), false, 0));
            }
            if (obj.contains("filterWidth")) {
                ushort val = static_cast<ushort>(qBound(0, obj["filterWidth"].toInt(), 10000));
                queue->addUnique(priorityImmediate, queueItem(funcFilterWidth, QVariant::fromValue<ushort>(val), false, 0));
            }
            QJsonObject resp; resp["status"] = "accepted";
            sendRestResponse(socket, 202, resp);
        } else {
            QJsonObject e; e["error"] = "Method not allowed";
            sendRestResponse(socket, 405, e);
        }
        return;
    }

    // --- GET/PUT /api/v1/radio/tx ---
    if (p == "/api/v1/radio/tx") {
        if (method == "GET") {
            if (!queue || !rigCaps) {
                QJsonObject e; e["error"] = "Rig not connected";
                sendRestResponse(socket, 503, e); return;
            }
            QJsonObject resp;
            cacheItem split = queue->getCache(funcSplitStatus, 0);
            if (split.value.isValid()) resp["split"] = split.value.toBool();
            cacheItem tuner = queue->getCache(funcTunerStatus, 0);
            if (tuner.value.isValid()) resp["tuner"] = tuner.value.toInt();
            cacheItem comp = queue->getCache(funcCompressor, 0);
            if (comp.value.isValid()) resp["compressor"] = comp.value.toBool();
            cacheItem mon = queue->getCache(funcMonitor, 0);
            if (mon.value.isValid()) resp["monitor"] = mon.value.toBool();
            sendRestResponse(socket, 200, resp);
        } else if (method == "PUT") {
            if (!queue || !rigCaps) {
                QJsonObject e; e["error"] = "Rig not connected";
                sendRestResponse(socket, 503, e); return;
            }
            QJsonObject obj = parseBody();
            if (obj.isEmpty()) {
                QJsonObject e; e["error"] = "Empty or invalid JSON body";
                sendRestResponse(socket, 400, e); return;
            }
            if (obj.contains("split")) {
                uchar val = obj["split"].toBool() ? 1 : 0;
                queue->addUnique(priorityImmediate, queueItem(funcSplitStatus, QVariant::fromValue<uchar>(val), false, 0));
            }
            if (obj.contains("tuner")) {
                uchar val = static_cast<uchar>(qBound(0, obj["tuner"].toInt(), 2));
                queue->addUnique(priorityImmediate, queueItem(funcTunerStatus, QVariant::fromValue<uchar>(val), false, 0));
            }
            if (obj.contains("compressor")) {
                uchar val = obj["compressor"].toBool() ? 1 : 0;
                queue->add(priorityImmediate, queueItem(funcCompressor, QVariant::fromValue<uchar>(val), false, 0));
            }
            if (obj.contains("monitor")) {
                uchar val = obj["monitor"].toBool() ? 1 : 0;
                queue->add(priorityImmediate, queueItem(funcMonitor, QVariant::fromValue<uchar>(val), false, 0));
            }
            QJsonObject resp; resp["status"] = "accepted";
            sendRestResponse(socket, 202, resp);
        } else {
            QJsonObject e; e["error"] = "Method not allowed";
            sendRestResponse(socket, 405, e);
        }
        return;
    }

    // --- POST/DELETE /api/v1/radio/cw ---
    if (p == "/api/v1/radio/cw") {
        if (method == "POST") {
            if (!queue || !rigCaps) {
                QJsonObject e; e["error"] = "Rig not connected";
                sendRestResponse(socket, 503, e); return;
            }
            QJsonObject obj = parseBody();
            if (!obj.contains("text")) {
                QJsonObject e; e["error"] = "Missing field: text";
                sendRestResponse(socket, 400, e); return;
            }
            QString text = obj["text"].toString();
            if (text.isEmpty()) {
                QJsonObject e; e["error"] = "Empty CW text";
                sendRestResponse(socket, 400, e); return;
            }
            if (obj.contains("wpm")) {
                ushort wpm = static_cast<ushort>(qBound(6, obj["wpm"].toInt(), 48));
                queue->add(priorityImmediate, queueItem(funcKeySpeed, QVariant::fromValue<ushort>(wpm), false, 0));
            }
            queue->add(priorityImmediate, queueItem(funcSendCW, QVariant::fromValue<QString>(text)));
            QJsonObject resp; resp["status"] = "accepted";
            sendRestResponse(socket, 202, resp);
        } else if (method == "DELETE") {
            if (!queue || !rigCaps) {
                QJsonObject e; e["error"] = "Rig not connected";
                sendRestResponse(socket, 503, e); return;
            }
            queue->add(priorityImmediate, queueItem(funcSendCW, QVariant::fromValue<QString>(QString())));
            QJsonObject resp; resp["status"] = "accepted";
            sendRestResponse(socket, 202, resp);
        } else {
            QJsonObject e; e["error"] = "Method not allowed";
            sendRestResponse(socket, 405, e);
        }
        return;
    }

    // --- GET /api/v1/radio/memories ---
    if (p == "/api/v1/radio/memories") {
        if (method == "GET") {
            QJsonArray arr;
            for (auto it = memories.constBegin(); it != memories.constEnd(); ++it) {
                if (!it.value().del) {
                    arr.append(memoryToJson(it.value()));
                }
            }
            QJsonObject resp;
            resp["memories"] = arr;
            resp["count"] = arr.size();
            sendRestResponse(socket, 200, resp);
        } else {
            QJsonObject e; e["error"] = "Method not allowed";
            sendRestResponse(socket, 405, e);
        }
        return;
    }

    // --- POST /api/v1/radio/memories/{channel}/recall ---
    if (p.startsWith("/api/v1/radio/memories/") && p.endsWith("/recall")) {
        if (method != "POST") {
            QJsonObject e; e["error"] = "Method not allowed";
            sendRestResponse(socket, 405, e); return;
        }
        if (!queue || !rigCaps) {
            QJsonObject e; e["error"] = "Rig not connected";
            sendRestResponse(socket, 503, e); return;
        }
        // Extract channel number from path
        QString mid = p.mid(22); // after "/api/v1/radio/memories/"
        mid.chop(7); // remove "/recall"
        int ch = mid.toInt();
        if (ch <= 0) {
            QJsonObject e; e["error"] = "Invalid channel number";
            sendRestResponse(socket, 400, e); return;
        }
        quint32 key = (quint32(0) << 16) | ch;
        auto it = memories.find(key);
        if (it == memories.end()) {
            QJsonObject e; e["error"] = "Memory channel not found";
            sendRestResponse(socket, 404, e); return;
        }
        const memoryType &mem = it.value();
        // Set frequency
        bool cmd29 = rigCaps && rigCaps->hasCommand29;
        uchar rx = cmd29 ? (queue->getState().vfo == vfoSub ? 1 : 0) : 0;
        vfoCommandType t = queue->getVfoCommand(vfoA, rx, true);
        freqt f;
        f.Hz = mem.frequency.Hz;
        f.MHzDouble = mem.frequency.Hz / 1.0E6;
        f.VFO = activeVFO;
        queue->addUnique(priorityImmediate, queueItem(t.freqFunc, QVariant::fromValue<freqt>(f), false, t.receiver));
        // Set mode
        for (const modeInfo &mi : rigCaps->modes) {
            if (mi.reg == mem.mode) {
                modeInfo m = mi;
                m.filter = mem.filter > 0 ? mem.filter : 1;
                m.data = 0;
                queue->addUnique(priorityImmediate, queueItem(t.modeFunc, QVariant::fromValue<modeInfo>(m), false, t.receiver));
                break;
            }
        }
        QJsonObject resp; resp["status"] = "accepted";
        sendRestResponse(socket, 202, resp);
        return;
    }

    // 404 for unknown paths
    QJsonObject e; e["error"] = "Unknown API endpoint";
    sendRestResponse(socket, 404, e);
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
        // Stop FreeDV/RADE TX drain to prevent ALSA underruns
        if (freedvTxActive) {
            freedvTxBuffer.clear();
            freedvTxActive = false;
            if (freedvTxDrainTimer) freedvTxDrainTimer->stop();
            txAudioActive = false;
            if (usbAudioOutput) {
                usbAudioOutput->stop();
                usbAudioOutputDevice = nullptr;
            }
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

    if (freedvEnabled) {
        audioPacket pkt;
        pkt.data = pcmData;
        pkt.time = QTime::currentTime();
        pkt.sent = 0;
        pkt.volume = 1.0;
#ifdef RADE_SUPPORT
        if (freedvModeName == "RADE")
            emit sendToRadeTx(pkt);
        else
#endif
#ifdef FREEDV_SUPPORT
            emit sendToFreeDVTx(pkt);
#else
        {}  // no codec available
#endif
        return;
    }

    if (usbAudioOutput && txAudioConfigured) {
        // USB path
        // Expand mono → stereo if the device requires it
        QByteArray writeData;
        if (usbOutputChannels == 2) {
            int numSamples = pcmData.size() / 2;
            writeData.resize(numSamples * 4);
            const qint16 *src = reinterpret_cast<const qint16 *>(pcmData.constData());
            qint16 *dst = reinterpret_cast<qint16 *>(writeData.data());
            for (int i = 0; i < numSamples; i++) {
                dst[i * 2] = src[i];
                dst[i * 2 + 1] = src[i];
            }
        } else {
            writeData = pcmData;
        }

        if (preTxBuffering) {
            // Accumulate chunks until the buffer reaches the device's full capacity,
            // then start ALSA and dump them all at once.  This ensures ALSA begins
            // with a full buffer rather than an empty one, providing a real jitter
            // absorber instead of racing the drain rate from the first byte.
            preTxBuffer.append(writeData);
            int bufSize = 9600; // ~100ms at 48kHz mono 16-bit
            if (preTxBuffer.size() >= bufSize) {
                preTxBuffering = false;
                usbAudioOutputDevice = usbAudioOutput->start();
                if (usbAudioOutputDevice) {
                    usbAudioOutputDevice->write(preTxBuffer);
                    txAudioActive = true;
                } else {
                    qWarning() << "Web: TX audio ALSA start() failed after pre-buffer";
                }
                preTxBuffer.clear();
            }
        } else if (usbAudioOutputDevice) {
            usbAudioOutputDevice->write(writeData);
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
        bool cmd29 = rigCaps && rigCaps->hasCommand29;
        vfoCommandType tA = queue->getVfoCommand(vfoA, 0, false);
        if (tA.freqFunc != funcNone)
            queue->add(priorityImmediate, tA.freqFunc, false, 0);
        if (tA.modeFunc != funcNone)
            queue->add(priorityImmediate, tA.modeFunc, false, 0);
        // On cmd29 rigs also refresh the sub receiver (VFO B).
        if (cmd29) {
            vfoCommandType tB = queue->getVfoCommand(vfoB, 1, false);
            if (tB.freqFunc != funcNone)
                queue->add(priorityImmediate, tB.freqFunc, false, 1);
            if (tB.modeFunc != funcNone)
                queue->add(priorityImmediate, tB.modeFunc, false, 1);
        }
    });
}

bool webServer::isFreeDVCompatibleMode(rigMode_t mk) const
{
    // FreeDV/RADE modulates audio that the rig transmits as SSB/AM/FM.
    // Modes that carry their own digital signalling (CW, RTTY, PSK, FT8-in-data)
    // are mutually exclusive with FreeDV.
    switch (mk) {
    case modeLSB:
    case modeUSB:
    case modeAM:
    case modeFM:
    case modeLSB_D:
    case modeUSB_D:
    case modeAMN:
    case modeWFM:
    case modeDATAFM:
    case modeDATAFMN:
        return true;
    default:
        return false;
    }
}

void webServer::disableFreeDV()
{
    if (!freedvEnabled) return;
    freedvEnabled = false;
    freedvSync = false;
    freedvSNR = 0.0f;
    freedvTxBuffer.clear();
    freedvTxActive = false;
    if (freedvTxDrainTimer) freedvTxDrainTimer->stop();
#ifdef FREEDV_SUPPORT
    if (freedvProcessor)
        QMetaObject::invokeMethod(freedvProcessor, "cleanup", Qt::QueuedConnection);
#endif
#ifdef RADE_SUPPORT
    freedvFreqOffset = 0.0f;
    radeRxCallsign.clear();
    if (radeProcessor) {
        radeProcessor->stopRequested.store(true);  // immediate cross-thread halt
        QMetaObject::invokeMethod(radeProcessor, "cleanup", Qt::QueuedConnection);
    }
#endif
#ifdef FREEDV_SUPPORT
    if (freedvReporter && reporterEnabled) {
        freedvReporter->disconnectFromService();
        qInfo() << "Web: Reporter disconnected (FreeDV disabled)";
    }
#endif
    qInfo() << "Web: FreeDV disabled";
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
            bool cmd29 = rigCaps && rigCaps->hasCommand29;
            uchar rx = cmd29 ? (queue->getState().vfo == vfoSub ? 1 : 0) : 0;
            vfoCommandType t = queue->getVfoCommand(vfoA, rx, true);
            queue->addUnique(priorityImmediate, queueItem(t.freqFunc, QVariant::fromValue<freqt>(f), false, t.receiver));
        }
    }
    else if (type == "setMode") {
        QString modeName = cmd["value"].toString();
        modeInfo m = stringToMode(modeName);
        if (m.mk != modeUnknown) {
            bool cmd29 = rigCaps && rigCaps->hasCommand29;
            uchar rx = cmd29 ? (queue->getState().vfo == vfoSub ? 1 : 0) : 0;
            vfoCommandType t = queue->getVfoCommand(vfoA, rx, true);
            // Preserve the current filter from cache instead of resetting to FIL1
            cacheItem modeCache = queue->getCache(t.modeFunc, t.receiver);
            if (modeCache.value.isValid()) {
                modeInfo cached = modeCache.value.value<modeInfo>();
                m.filter = cached.filter;
            }
            qCDebug(logWebServer) << "setMode:" << modeName << "mk=" << m.mk << "reg=" << m.reg
                                  << "name=" << m.name << "filter=" << m.filter << "func=" << t.modeFunc;
            queue->addUnique(priorityImmediate, queueItem(t.modeFunc, QVariant::fromValue<modeInfo>(m), false, t.receiver));
        } else {
            qCWarning(logWebServer) << "setMode: unknown mode name:" << modeName;
        }
    }
    else if (type == "selectVFO") {
        QString vfoName = cmd["value"].toString();
        // On cmd29 rigs (IC-7610/785x/9700/905) selecting a VFO means Main/Sub,
        // not A/B. funcSelectVFO in icomcommander maps vfo_t to the right command.
        bool wantB = (vfoName == "B");
        vfo_t v;
        if (rigCaps && rigCaps->hasCommand29)
            v = wantB ? vfoSub : vfoMain;
        else
            v = wantB ? vfoB : vfoA;
        queue->addUnique(priorityImmediate, queueItem(funcSelectVFO, QVariant::fromValue<vfo_t>(v), false));
        requestVfoUpdate();
    }
    else if (type == "swapVFO") {
        // IC-7300/7100/705/etc use VFO Swap A/B; IC-7610/785x/9700/905 use Swap M/S.
        funcs swapFunc = funcNone;
        if (rigCaps) {
            if (rigCaps->commands.contains(funcVFOSwapAB))
                swapFunc = funcVFOSwapAB;
            else if (rigCaps->commands.contains(funcVFOSwapMS))
                swapFunc = funcVFOSwapMS;
        }
        if (swapFunc != funcNone)
            queue->add(priorityImmediate, swapFunc, false, 0);
        requestVfoUpdate();
    }
    else if (type == "equalizeVFO") {
        funcs eqFunc = funcNone;
        if (rigCaps) {
            if (rigCaps->commands.contains(funcVFOEqualAB))
                eqFunc = funcVFOEqualAB;
            else if (rigCaps->commands.contains(funcVFOEqualMS))
                eqFunc = funcVFOEqualMS;
        }
        if (eqFunc != funcNone)
            queue->add(priorityImmediate, eqFunc, false, 0);
        requestVfoUpdate();
    }
    else if (type == "setPTT") {
        bool on = cmd["value"].toBool();
#ifdef RADE_SUPPORT
        // On PTT-off with RADE: generate EOO frame and delay the radio unkey
        // so the callsign-bearing EOO OFDM frame plays out through ALSA/USB
        // before the radio stops transmitting.
        if (!on && freedvEnabled && freedvModeName == "RADE" && radeProcessor && usbAudioOutputDevice) {
            radeProcessor->eooAudioResult.clear();
            bool ok = QMetaObject::invokeMethod(radeProcessor, "generateEooAudio",
                                                 Qt::BlockingQueuedConnection);
            QByteArray eooData = radeProcessor->eooAudioResult;
            qInfo() << "Web: RADE EOO invoke ok=" << ok << "dataSize=" << eooData.size();
            if (!eooData.isEmpty()) {
                // Stereo expansion (same as onRadeTxReady)
                QByteArray writeData;
                if (usbOutputChannels == 2) {
                    int numSamples = eooData.size() / 2;
                    writeData.resize(numSamples * 4);
                    const qint16 *src = reinterpret_cast<const qint16 *>(eooData.constData());
                    qint16 *dst = reinterpret_cast<qint16 *>(writeData.data());
                    for (int i = 0; i < numSamples; i++) {
                        dst[i * 2] = src[i];
                        dst[i * 2 + 1] = src[i];
                    }
                } else {
                    writeData = eooData;
                }
                // RADE uses a fixed TX level (see onRadeTxReady); EOO must match.
                {
                    qint16 *samples = reinterpret_cast<qint16 *>(writeData.data());
                    int count = writeData.size() / (int)sizeof(qint16);
                    for (int i = 0; i < count; i++)
                        samples[i] = qBound((int)-32768, (int)(samples[i] * RADE_TX_GAIN), (int)32767);
                }
                freedvTxBuffer.append(writeData);
                qInfo() << "Web: queued RADE EOO frame (" << writeData.size() << "bytes), delaying unkey";
                // Delay the radio unkey so ALSA has time to play the EOO frame
                QTimer::singleShot(300, this, [this]() {
                    queue->add(priorityImmediate, queueItem(funcTransceiverStatus, QVariant::fromValue<bool>(false), false, uchar(0)));
                    queue->del(funcALCMeter, 0);
                    qInfo() << "Web: RADE delayed unkey sent";
                });
                return;
            }
        }
#endif
        queue->add(priorityImmediate, queueItem(funcTransceiverStatus, QVariant::fromValue<bool>(on), false, uchar(0)));
        // Start/stop ALC meter polling for web clients
        if (on) {
            queue->addUnique(priorityHighest, queueItem(funcALCMeter, true, 0));
        } else {
            queue->del(funcALCMeter, 0);
        }
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
    else if (type == "setMicGain") {
        ushort val = static_cast<ushort>(qBound(0, cmd["value"].toInt(), 255));
        queue->addUnique(priorityImmediate, queueItem(funcUSBModLevel, QVariant::fromValue<ushort>(val), false, 0));
    }
    else if (type == "setAttenuator") {
        uchar val = static_cast<uchar>(qBound(0, cmd["value"].toInt(), 255));
        queue->addUnique(priorityImmediate, queueItem(funcAttenuator, QVariant::fromValue<uchar>(val), false, 0));
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
    else if (type == "setMonitorGain") {
        ushort val = static_cast<ushort>(qBound(0, cmd["value"].toInt(), 255));
        queue->addUnique(priorityImmediate, queueItem(funcMonitorGain, QVariant::fromValue<ushort>(val), false, 0));
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
    else if (type == "setPBTInner") {
        ushort val = static_cast<ushort>(qBound(0, cmd["value"].toInt(), 255));
        queue->addUnique(priorityImmediate, queueItem(funcPBTInner, QVariant::fromValue<ushort>(val), false, 0));
    }
    else if (type == "setPBTOuter") {
        ushort val = static_cast<ushort>(qBound(0, cmd["value"].toInt(), 255));
        queue->addUnique(priorityImmediate, queueItem(funcPBTOuter, QVariant::fromValue<ushort>(val), false, 0));
    }
    else if (type == "setFilterShape") {
        int shape = qBound(0, cmd["value"].toInt(), 1);  // 0=sharp, 1=soft
        // Get current filter from mode cache
        int filter = 1;
        vfoCommandType t = queue->getVfoCommand(vfoA, 0, false);
        cacheItem modeCache = queue->getCache(t.modeFunc, 0);
        if (modeCache.value.isValid()) {
            modeInfo m = modeCache.value.value<modeInfo>();
            filter = qBound(1, (int)m.filter, 3);
        }
        uchar val = uchar(shape + (filter * 10));
        queue->addUnique(priorityImmediate, queueItem(funcFilterShape, QVariant::fromValue<uchar>(val), false, 0));
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
                QTimer::singleShot(200, this, [this]() {
                    if (queue) queue->add(priorityImmediate, funcFilterWidth, false, 0);
                });
            }
        }
    }
    else if (type == "setSplit") {
        bool on = cmd["value"].toBool();
        bool supported = rigCaps && rigCaps->commands.contains(funcSplitStatus);
        qCInfo(logWebServer) << "setSplit:" << on << "supported=" << supported
                             << "hasCmd29=" << (rigCaps && rigCaps->hasCommand29);
        if (supported) {
            queue->addUnique(priorityImmediate, queueItem(funcSplitStatus, QVariant::fromValue<uchar>(on ? 1 : 0), false, 0));
        }
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
            if (usbAudioOutput) {
                // Stop any previous session cleanly, then arm the pre-buffer.
                // ALSA will be started only once the pre-buffer is full, so it
                // never begins with an empty buffer and races the drain rate.
                if (usbAudioOutput->state() != QAudio::StoppedState)
                    usbAudioOutput->stop();
                usbAudioOutputDevice = nullptr;
                preTxBuffer.clear();
                preTxBuffering = true;
                // Reset FreeDV TX state so onFreeDVTxReady() will restart ALSA
                freedvTxBuffer.clear();
                freedvTxActive = false;
                if (freedvTxDrainTimer) freedvTxDrainTimer->stop();
            }
        } else {
            // Restore previous DATA MOD OFF setting
            if (dataOffModSaved && queue) {
                queue->addUnique(priorityImmediate, queueItem(funcDATAOffMod, QVariant::fromValue<rigInput>(savedDataOffMod), false, 0));
                qInfo() << "Web: Restored DATA MOD OFF setting";
            }
            dataOffModSaved = false;
            micActiveClient = nullptr;
            preTxBuffering = false;
            preTxBuffer.clear();
            freedvTxBuffer.clear();
            freedvTxActive = false;
            if (freedvTxDrainTimer) freedvTxDrainTimer->stop();
            txAudioActive = false;
            if (usbAudioOutput) {
                usbAudioOutput->stop();
                usbAudioOutputDevice = nullptr;
            }
        }
    }
    else if (type == "getStatus") {
        sendCurrentState(client);
    }
    else if (type == "sendCW") {
        QString text = cmd["text"].toString();
        if (!text.isEmpty()) {
            queue->add(priorityImmediate, queueItem(funcSendCW, QVariant::fromValue<QString>(text)));
            qCDebug(logWebServer) << "sendCW:" << text;
        }
    }
    else if (type == "setCWSpeed") {
        ushort wpm = static_cast<ushort>(qBound(6, cmd["wpm"].toInt(), 48));
        queue->add(priorityImmediate, queueItem(funcKeySpeed, QVariant::fromValue<ushort>(wpm), false, 0));
        qCDebug(logWebServer) << "setCWSpeed:" << wpm << "WPM";
    }
    else if (type == "stopCW") {
        // Send empty string to stop CW transmission
        queue->add(priorityImmediate, queueItem(funcSendCW, QVariant::fromValue<QString>(QString())));
        qCDebug(logWebServer) << "stopCW";
    }
    else if (type == "setPower") {
        bool on = cmd["value"].toBool();
        if (on) {
            emit requestPowerOn();
        } else {
            emit requestPowerOff();
        }
    }
    else if (type == "disconnectLan") {
        emit requestLanDisconnect();
    }
    else if (type == "reconnectLan") {
        emit requestLanReconnect();
    }
    else if (type == "getMemories") {
        if (!queue || !rigCaps) {
            QJsonObject err;
            err["type"] = "memoryScanComplete";
            err["count"] = 0;
            err["error"] = "Rig not connected";
            sendJsonTo(client, err);
            return;
        }
        if (!rigCaps->commands.contains(funcMemoryContents) || rigCaps->memParser.isEmpty()) {
            QJsonObject err;
            err["type"] = "memoryScanComplete";
            err["count"] = 0;
            err["error"] = "Memories not supported by this radio";
            sendJsonTo(client, err);
            return;
        }
        int start = cmd.contains("start") ? cmd["start"].toInt() : 1;
        int end = cmd.contains("end") ? cmd["end"].toInt() : 99;
        int group = cmd.contains("group") ? cmd["group"].toInt() : 0;
        memories.clear();
        memoryScanActive = true;
        memoryScanCurrent = start;
        memoryScanEnd = end;
        memoryScanGroup = group;
        // Initialize scan timer on first use
        if (!memoryScanTimer) {
            memoryScanTimer = new QTimer(this);
            memoryScanTimer->setSingleShot(true);
            memoryScanTimer->setInterval(500);
            connect(memoryScanTimer, &QTimer::timeout, this, &webServer::scanNextMemory);
        }
        // Request first channel
        uint val = (uint(group) << 16) | uint(start);
        queue->addUnique(priorityImmediate, queueItem(funcMemoryContents, QVariant::fromValue<uint>(val), false, 0));
        memoryScanTimer->start();
    }
    else if (type == "writeMemory") {
        if (!queue || !rigCaps) return;
        int ch = cmd["channel"].toInt();
        int group = cmd.contains("group") ? cmd["group"].toInt() : 0;
        if (ch <= 0) return;
        // Build memoryType from current VFO state
        memoryType mem;
        mem.channel = ch;
        mem.group = group;
        mem.del = false;
        mem.skip = 0;
        mem.scan = 0;
        memset(mem.name, ' ', sizeof(mem.name) - 1);
        mem.name[sizeof(mem.name) - 1] = '\0';
        mem.split = 0;
        // Get current VFO A frequency and mode
        vfoCommandType tA = queue->getVfoCommand(vfoA, 0, false);
        cacheItem freqCache = queue->getCache(tA.freqFunc, 0);
        if (freqCache.value.isValid()) {
            mem.frequency = freqCache.value.value<freqt>();
        }
        cacheItem modeCache = queue->getCache(tA.modeFunc, 0);
        if (modeCache.value.isValid()) {
            modeInfo m = modeCache.value.value<modeInfo>();
            mem.mode = m.reg;
            mem.filter = m.filter > 0 ? m.filter : 1;
            mem.datamode = m.data == 0xff ? 0 : m.data;
        } else {
            mem.mode = 1; // USB
            mem.filter = 1;
            mem.datamode = 0;
        }
        // Copy VFO A to B fields (no split support)
        mem.frequencyB = mem.frequency;
        mem.modeB = mem.mode;
        mem.filterB = mem.filter;
        mem.datamodeB = mem.datamode;
        mem.tonemodeB = mem.tonemode;
        mem.toneB = mem.tone;
        mem.tsqlB = mem.tsql;
        mem.dtcsB = mem.dtcs;
        mem.dtcspB = mem.dtcsp;
        queue->addUnique(priorityImmediate, queueItem(funcMemoryContents, QVariant::fromValue<memoryType>(mem), false, 0));
    }
    else if (type == "clearMemory") {
        if (!queue || !rigCaps) return;
        int ch = cmd["channel"].toInt();
        int group = cmd.contains("group") ? cmd["group"].toInt() : 0;
        if (ch <= 0) return;
        // Write a deleted/empty memory via funcMemoryContents (same path as writeMemory)
        memoryType mem;
        mem.channel = ch;
        mem.group = group;
        mem.del = true;
        mem.split = 0;
        mem.skip = 0;
        mem.scan = 0;
        memset(mem.name, 0, sizeof(mem.name));
        memset(mem.UR, 0, sizeof(mem.UR));
        memset(mem.URB, 0, sizeof(mem.URB));
        memset(mem.R1, 0, sizeof(mem.R1));
        memset(mem.R2, 0, sizeof(mem.R2));
        memset(mem.R1B, 0, sizeof(mem.R1B));
        memset(mem.R2B, 0, sizeof(mem.R2B));
        qCInfo(logWebServer) << "clearMemory: channel=" << ch << "group=" << group;
        memoryScanActive = false; // Prevent scan from interfering
        queue->addUnique(priorityImmediate, queueItem(funcMemoryContents, QVariant::fromValue<memoryType>(mem), false, 0));
        quint32 key = (quint32(group) << 16) | ch;
        memories.remove(key);
    }
    else if (type == "setFreeDV") {
        bool enable = cmd["enabled"].toBool();
        QString modeName = cmd["mode"].toString();
        if (enable && audioConfigured) {
            // FreeDV/RADE rides on SSB. If the user is parked on a mode that
            // isn't USB/LSB (e.g. CW/RTTY/FM left over from a previous session),
            // force to the right sideband for the current frequency — USB on
            // bands >= 10 MHz, LSB below. This matches HF ham convention.
            if (queue) {
                vfoCommandType t = queue->getVfoCommand(vfoA, 0, true);
                cacheItem modeCache = queue->getCache(t.modeFunc, 0);
                cacheItem freqCache = queue->getCache(t.freqFunc, 0);
                if (modeCache.value.isValid()) {
                    modeInfo cur = modeCache.value.value<modeInfo>();
                    if (cur.mk != modeUSB && cur.mk != modeLSB) {
                        quint64 hz = 0;
                        if (freqCache.value.isValid())
                            hz = freqCache.value.value<freqt>().Hz;
                        QString forced = (hz >= 10000000ULL) ? QStringLiteral("USB")
                                                             : QStringLiteral("LSB");
                        modeInfo m = stringToMode(forced);
                        m.filter = cur.filter;
                        qCInfo(logWebServer) << "FreeDV enable: forcing mode from"
                                             << modeToString(cur) << "to" << forced
                                             << "at" << (double)hz/1e6 << "MHz";
                        queue->addUnique(priorityImmediate,
                                         queueItem(t.modeFunc,
                                                   QVariant::fromValue<modeInfo>(m),
                                                   false, 0));
                    }
                }
            }
#ifdef RADE_SUPPORT
            if (modeName == "RADE" && radeProcessor) {
                freedvModeName = modeName;
                freedvEnabled = true;
#ifdef FREEDV_SUPPORT
                if (freedvProcessor)
                    QMetaObject::invokeMethod(freedvProcessor, "cleanup", Qt::QueuedConnection);
#endif
                emit setupRade(rigSampleRate);
                // Send cached callsign to RADE processor for EOO encoding
                if (!reporterCallsign.isEmpty()) {
                    QMetaObject::invokeMethod(radeProcessor, "setTxCallsign",
                                              Qt::QueuedConnection,
                                              Q_ARG(QString, reporterCallsign));
                }
                qInfo() << "Web: RADE enabled";
            } else
#endif
#ifdef FREEDV_SUPPORT
            if (freedvProcessor) {
                int mode = -1;
                if (modeName == "700D") mode = FREEDV_MODE_700D;
                else if (modeName == "700E") mode = FREEDV_MODE_700E;
                else if (modeName == "1600") mode = FREEDV_MODE_1600;
                if (mode >= 0) {
                    freedvModeName = modeName;
                    freedvEnabled = true;
#ifdef RADE_SUPPORT
                    if (radeProcessor) {
                        radeProcessor->stopRequested.store(true);
                        QMetaObject::invokeMethod(radeProcessor, "cleanup", Qt::QueuedConnection);
                    }
#endif
                    emit setupFreeDV(mode, rigSampleRate);
                    qInfo() << "Web: FreeDV enabled, mode=" << modeName;
                }
            }
#elif defined(RADE_SUPPORT)
            { }
#endif
#ifdef FREEDV_SUPPORT
            // Reconnect reporter when entering FreeDV/RADE mode
            if (freedvEnabled && freedvReporter && reporterEnabled)
                freedvReporter->connectToService();
#endif
        } else {
            disableFreeDV();
        }
        QJsonObject notify;
        notify["type"] = "freedvStatus";
        notify["enabled"] = freedvEnabled;
        notify["freedvMode"] = freedvModeName;
        notify["freedvSync"] = freedvSync;
        notify["freedvSNR"] = (double)freedvSNR;
#ifdef RADE_SUPPORT
        if (freedvModeName == "RADE")
            notify["freedvFreqOffset"] = (double)freedvFreqOffset;
#endif
        sendJsonToAll(notify);
    }
    else if (type == "setFreeDVTxGain") {
        float gain = (float)cmd["value"].toDouble();
        freedvTxGain = qBound(0.01f, gain, 1.0f);
    }
    else if (type == "setFreeDVMonitor") {
        freedvMonitor = cmd["value"].toBool();
    }
#ifdef PACKET_SUPPORT
    else if (type == "packetEnable") {
        packetEnabled = cmd["value"].toBool();
        if (dwProc) {
            QMetaObject::invokeMethod(dwProc, "setEnabled", Qt::QueuedConnection,
                                      Q_ARG(bool, packetEnabled));
        }
        packetSaveSettings();
        QJsonObject notify;
        notify["type"] = "packetStatus";
        notify["enabled"] = packetEnabled;
        notify["mode"] = packetMode;
        sendJsonToAll(notify);
    }
    else if (type == "packetCaptureAudio") {
        int seconds = cmd.value("seconds").toInt(10);
        // Save to /tmp with a timestamped name so concurrent captures don't
        // stomp each other.  The path is sent back to the client so the
        // operator can grep for it or scp it off the host.
        QString path = QString("/tmp/wfweb-packet-%1.wav")
            .arg(QDateTime::currentDateTimeUtc().toString("yyyyMMdd-hhmmss"));
        if (!dwProc) {
            qWarning() << "Web: packetCaptureAudio — dwProc is null"
                       << "audioConfigured=" << audioConfigured
                       << "packetEnabled=" << packetEnabled
                       << "rigSampleRate=" << rigSampleRate;
            QJsonObject notify;
            notify["type"] = "packetCaptureFailed";
            notify["reason"] = QString("dwProc null (audioConfigured=%1 packetEnabled=%2) — "
                                       "check wfweb logs; likely audio setup never ran")
                .arg(audioConfigured).arg(packetEnabled);
            sendJsonToAll(notify);
        } else {
            QMetaObject::invokeMethod(dwProc, "startCapture", Qt::QueuedConnection,
                                      Q_ARG(int, seconds),
                                      Q_ARG(QString, path));
            QJsonObject notify;
            notify["type"] = "packetCaptureStarted";
            notify["path"] = path;
            notify["seconds"] = seconds;
            sendJsonToAll(notify);
        }
    }
    else if (type == "packetSetMode") {
        int baud = cmd["value"].toInt(-1);
        if (baud == 300 || baud == 1200 || baud == 9600) {
            // Changing the baud rate invalidates the physical-layer timing
            // any live AX.25 session is calibrated for.  t1v evolves from
            // the observed srt once a session is open; switching 1200→300
            // leaves T1 far too short, so the sender retries long before
            // the peer can even finish TXing an ACK at the new speed.
            // Tear down active sessions so a fresh Connect picks up the
            // new frack/retry values cleanly.
            if (axProc) {
                for (TerminalSession *s : termSessions) {
                    if (s->state == TerminalSession::Connected
                        || s->state == TerminalSession::Connecting) {
                        QByteArray info = QStringLiteral("*** Mode change — link closed").toUtf8();
                        QJsonObject e = termScrollbackEntry(QStringLiteral("info"), info);
                        termAppendScrollback(s, e);
                        termBroadcastData(s->sid, e);
                        s->state = TerminalSession::Disconnecting;
                        termBroadcastSession(s);
                        QMetaObject::invokeMethod(axProc, "disconnectRequest", Qt::QueuedConnection,
                                                  Q_ARG(int, TERM_FIXED_CLIENT),
                                                  Q_ARG(int, s->chan),
                                                  Q_ARG(QString, s->ownCall),
                                                  Q_ARG(QString, s->peerCall),
                                                  Q_ARG(QStringList, s->digis));
                    }
                }
            }

            packetMode = baud;
            if (dwProc) {
                QMetaObject::invokeMethod(dwProc, "setMode", Qt::QueuedConnection,
                                          Q_ARG(int, baud));
            }
            if (axProc) {
                QMetaObject::invokeMethod(axProc, "setLinkParamsForBaud", Qt::QueuedConnection,
                                          Q_ARG(int, baud));
            }
            packetSaveSettings();
            QJsonObject notify;
            notify["type"] = "packetStatus";
            notify["enabled"] = packetEnabled;
            notify["mode"] = packetMode;
            sendJsonToAll(notify);
        }
    }
    else if (type == "packetTx") {
        // {src, dst, info}  required;  path: [ "WIDE1-1", ... ]  optional
        QString src  = cmd["src"].toString().trimmed().toUpper();
        QString dst  = cmd["dst"].toString().trimmed().toUpper();
        QString info = cmd["info"].toString();
        QStringList pathList;
        if (cmd.value("path").isArray()) {
            QJsonArray arr = cmd["path"].toArray();
            for (int i = 0; i < arr.size(); i++) {
                QString p = arr[i].toString().trimmed().toUpper();
                if (!p.isEmpty()) pathList.append(p);
            }
        }

        QString failReason;
        if (!packetEnabled || !dwProc) {
            failReason = "packet modem not enabled";
        } else if (src.isEmpty() || dst.isEmpty() || info.isEmpty()) {
            failReason = "src, dst, and info are required";
        } else if (!(txAudioConfigured && usbAudioOutput) && !txConverter) {
            // Either USB mode (usbAudioOutput + txAudioConfigured both set
            // by setupUsbAudio) or LAN mode (txConverter set by setupAudio).
            failReason = "TX audio not configured";
        } else if (packetTxDraining || freedvTxActive) {
            failReason = "TX already in progress";
        }

        if (!failReason.isEmpty()) {
            QJsonObject notify;
            notify["type"] = "packetTxFailed";
            notify["reason"] = failReason;
            sendJsonToAll(notify);
        } else {
            QString monitor = src + ">" + dst;
            if (!pathList.isEmpty()) monitor += "," + pathList.join(",");
            monitor += ":" + info;

            // Claim the TX slot now — without this flag, a second packetTx
            // that arrives before the queued encode completes would slip
            // past the "already in progress" guard.  Cleared by the drain
            // timer on completion, or by onPacketTxFailed on error.
            packetTxDraining = true;

            // Key the radio immediately so it's settled by the time the
            // encoded burst arrives via onPacketTxReady.
            queue->add(priorityImmediate,
                       queueItem(funcTransceiverStatus,
                                 QVariant::fromValue<bool>(true), false, uchar(0)));
            queue->addUnique(priorityHighest,
                             queueItem(funcALCMeter, true, 0));

            QMetaObject::invokeMethod(dwProc, "transmitFrame", Qt::QueuedConnection,
                                      Q_ARG(QString, monitor));

            QJsonObject notify;
            notify["type"] = "packetTxStarted";
            notify["monitor"] = monitor;
            sendJsonToAll(notify);
            qInfo().noquote() << "Web: Packet TX queued:" << monitor;
        }
    }
    // ---- AX.25 connected-mode terminal sessions ----
    else if (type == "termList") {
        QJsonArray sessions;
        for (const TerminalSession *s : termSessions) {
            sessions.append(termSessionToJson(s));
        }
        QJsonObject reply;
        reply["type"] = "termList";
        reply["sessions"] = sessions;
        // termList is per-client; reply only to the requesting socket if we
        // can identify it.  For now broadcast (cheap, idempotent).
        sendJsonToAll(reply);
    }
    else if (type == "termHistory") {
        QString sid = cmd["sid"].toString();
        TerminalSession *s = termSessions.value(sid, nullptr);
        QJsonObject reply;
        reply["type"] = "termHistory";
        reply["sid"] = sid;
        QJsonArray entries;
        if (s) {
            for (const QJsonObject &e : s->scrollback) entries.append(e);
        }
        reply["entries"] = entries;
        sendJsonToAll(reply);
    }
    else if (type == "termConnect") {
        QString own  = cmd["ownCall"].toString().trimmed().toUpper();
        QString peer = cmd["peerCall"].toString().trimmed().toUpper();
        int chan = cmd.value("chan").toInt(0);
        QStringList digis;
        if (cmd.value("digis").isArray()) {
            QJsonArray arr = cmd["digis"].toArray();
            for (int i = 0; i < arr.size(); i++) {
                QString d = arr[i].toString().trimmed().toUpper();
                if (!d.isEmpty()) digis.append(d);
            }
        }
        QString failReason;
        TerminalSession *existing = termFindByEndpoints(chan, own, peer);
        if (!packetEnabled || !dwProc || !axProc) failReason = "packet modem not enabled";
        else if (own.isEmpty() || peer.isEmpty()) failReason = "ownCall and peerCall are required";
        else if (existing && existing->state != TerminalSession::Disconnected)
            failReason = "session already in progress";

        if (!failReason.isEmpty()) {
            QJsonObject notify;
            notify["type"] = "termError";
            notify["reason"] = failReason;
            sendJsonToAll(notify);
        } else {
            TerminalSession *s = existing;
            if (!s) {
                s = new TerminalSession();
                s->sid = termMakeSid();
                s->chan = chan;
                s->ownCall = own;
                s->peerCall = peer;
                s->createdMs = QDateTime::currentMSecsSinceEpoch();
                termSessions.insert(s->sid, s);
            }
            s->digis = digis;
            s->state = TerminalSession::Connecting;
            s->lastActivityMs = QDateTime::currentMSecsSinceEpoch();
            // Make sure we'll accept inbound to this own_call too.
            termEnsureRegistered(chan, own);
            termAppendScrollback(s, termScrollbackEntry(QStringLiteral("info"),
                QString("Connecting to %1...").arg(peer).toUtf8()));
            termBroadcastSession(s);
            QMetaObject::invokeMethod(axProc, "connectRequest", Qt::QueuedConnection,
                                      Q_ARG(int, TERM_FIXED_CLIENT),
                                      Q_ARG(int, chan),
                                      Q_ARG(QString, own),
                                      Q_ARG(QString, peer),
                                      Q_ARG(QStringList, digis));
            qInfo().noquote() << "Web: termConnect" << s->sid << own << "->" << peer;
        }
    }
    else if (type == "termDisconnect") {
        QString sid = cmd["sid"].toString();
        TerminalSession *s = termSessions.value(sid, nullptr);
        if (s && axProc) {
            bool force = (s->state == TerminalSession::Disconnecting);
            // ax25_link's state_2_awaiting_release handler (dl_disconnect_request
            // with S->state == state_2_awaiting_release) treats a second
            // disconnect as the "impatient application" case: send DM
            // (expedited), stop T1, go straight to state_0_disconnected, and
            // fire server_link_terminated.  So re-issuing the same dlq event
            // here gets us instant tear-down for free.
            s->state = TerminalSession::Disconnecting;
            termAppendScrollback(s, termScrollbackEntry(QStringLiteral("info"),
                force ? QStringLiteral("Forcing disconnect...").toUtf8()
                      : QStringLiteral("Disconnecting...").toUtf8()));
            termBroadcastSession(s);
            QMetaObject::invokeMethod(axProc, "disconnectRequest", Qt::QueuedConnection,
                                      Q_ARG(int, TERM_FIXED_CLIENT),
                                      Q_ARG(int, s->chan),
                                      Q_ARG(QString, s->ownCall),
                                      Q_ARG(QString, s->peerCall),
                                      Q_ARG(QStringList, s->digis));
        }
    }
    else if (type == "termSend") {
        QString sid = cmd["sid"].toString();
        QString text = cmd["data"].toString();
        TerminalSession *s = termSessions.value(sid, nullptr);
        if (s && axProc && s->state == TerminalSession::Connected) {
            QByteArray data = text.toUtf8();
            QMetaObject::invokeMethod(axProc, "sendData", Qt::QueuedConnection,
                                      Q_ARG(int, TERM_FIXED_CLIENT),
                                      Q_ARG(int, s->chan),
                                      Q_ARG(QString, s->ownCall),
                                      Q_ARG(QString, s->peerCall),
                                      Q_ARG(QStringList, s->digis),
                                      Q_ARG(int, 0xF0),
                                      Q_ARG(QByteArray, data));
            QJsonObject entry = termScrollbackEntry(QStringLiteral("tx"), data);
            termAppendScrollback(s, entry);
            termBroadcastData(s->sid, entry);
        }
    }
    else if (type == "termClose") {
        // Force-remove a session record from the server.  Sends a disconnect
        // first if the link is still up; the resulting linkTerminated will
        // also clean up but this lets a stuck session be wiped immediately.
        QString sid = cmd["sid"].toString();
        TerminalSession *s = termSessions.take(sid);
        if (s) {
            if (axProc && s->state != TerminalSession::Disconnected) {
                QMetaObject::invokeMethod(axProc, "disconnectRequest", Qt::QueuedConnection,
                                          Q_ARG(int, TERM_FIXED_CLIENT),
                                          Q_ARG(int, s->chan),
                                          Q_ARG(QString, s->ownCall),
                                          Q_ARG(QString, s->peerCall),
                                          Q_ARG(QStringList, s->digis));
            }
            QJsonObject notify;
            notify["type"] = "termClose";
            notify["sid"] = sid;
            sendJsonToAll(notify);
            delete s;
        }
    }
    else if (type == "termRegister") {
        // Standalone callsign registration so the server accepts inbound
        // connections without the operator first making an outbound call.
        QString own = cmd["ownCall"].toString().trimmed().toUpper();
        int chan = cmd.value("chan").toInt(0);
        if (!own.isEmpty()) termEnsureRegistered(chan, own);
    }
    else if (type == "termUnregister") {
        QString own = cmd["ownCall"].toString().trimmed().toUpper();
        int chan = cmd.value("chan").toInt(0);
        if (!own.isEmpty() && axProc && termRegisteredOwnCalls.remove(own)) {
            QMetaObject::invokeMethod(axProc, "unregisterCallsign", Qt::QueuedConnection,
                                      Q_ARG(int, TERM_FIXED_CLIENT),
                                      Q_ARG(int, chan),
                                      Q_ARG(QString, own));
        }
    }
    else if (type == "termFileSend") {
        QString sid  = cmd["sid"].toString();
        QString name = cmd["name"].toString();
        QByteArray data = QByteArray::fromBase64(cmd["dataB64"].toString().toLatin1());
        TerminalSession *s = termSessions.value(sid, nullptr);
        if (s && !name.isEmpty() && !data.isEmpty()
            && s->state == TerminalSession::Connected
            && !s->xfer.active) {
            yappSendFile(s, name, data);
        } else {
            QJsonObject notify;
            notify["type"] = "termError";
            notify["reason"] = !s ? "unknown session"
                : (s->xfer.active ? "transfer already in progress"
                                  : "session not connected");
            sendJsonToAll(notify);
        }
    }
    else if (type == "termFileAbort") {
        QString sid = cmd["sid"].toString();
        TerminalSession *s = termSessions.value(sid, nullptr);
        if (s && s->xfer.active) yappAbortSend(s);
    }
    else if (type == "termXferAccept") {
        QString sid = cmd["sid"].toString();
        TerminalSession *s = termSessions.value(sid, nullptr);
        if (s && s->xfer.active && s->xfer.phase == "awaiting_accept") {
            yappSendRR(s);
            s->xfer.phase = QStringLiteral("active");
            qInfo().noquote() << "Web: YAPP accept → RR sent" << s->sid;
        }
    }
    else if (type == "termXferReject") {
        QString sid = cmd["sid"].toString();
        TerminalSession *s = termSessions.value(sid, nullptr);
        if (s && s->xfer.active && s->xfer.phase == "awaiting_accept") {
            yappSendAB(s);
            xferBroadcastEnd(s, /*aborted=*/true);
            QByteArray info = QStringLiteral("*** Rejected").toUtf8();
            QJsonObject e = termScrollbackEntry(QStringLiteral("info"), info);
            termAppendScrollback(s, e);
            termBroadcastData(s->sid, e);
            s->xfer = XferState{};
            qInfo().noquote() << "Web: YAPP reject → AB sent" << s->sid;
        }
    }
#endif
#ifdef FREEDV_SUPPORT
    else if (type == "setReporter") {
        reporterCallsign = cmd["callsign"].toString().toUpper().trimmed();
        reporterGrid = cmd["grid"].toString().toUpper().trimmed();
        reporterEnabled = cmd["enabled"].toBool();

        if (reporterEnabled) {
            if (!freedvReporter) {
                freedvReporter = new FreeDVReporter(this);
                connect(freedvReporter, &FreeDVReporter::stateChanged,
                        this, &webServer::onReporterStateChanged);
            }
            freedvReporter->setStation(reporterCallsign, reporterGrid);

            // Seed reporter with current frequency from cache
            if (queue) {
                vfoCommandType t = queue->getVfoCommand(vfoA, 0, false);
                cacheItem freqCache = queue->getCache(t.freqFunc, 0);
                if (freqCache.value.isValid()) {
                    freqt f = freqCache.value.value<freqt>();
                    if (f.Hz > 0) freedvReporter->updateFrequency(f.Hz);
                }
            }

            // Only connect if FreeDV/RADE mode is currently active
            if (freedvEnabled) {
                freedvReporter->updateTx(freedvModeName, false);
                freedvReporter->connectToService();
            }
        } else {
            if (freedvReporter)
                freedvReporter->disconnectFromService();
        }
#ifdef RADE_SUPPORT
        // Pass callsign to RADE processor for EOO encoding
        if (radeProcessor && !reporterCallsign.isEmpty()) {
            QMetaObject::invokeMethod(radeProcessor, "setTxCallsign",
                                      Qt::QueuedConnection,
                                      Q_ARG(QString, reporterCallsign));
        }
#endif

        QJsonObject notify;
        notify["type"] = "reporterStatus";
        notify["enabled"] = reporterEnabled;
        notify["state"] = freedvReporter ? (reporterEnabled ? "connecting" : "disconnected") : "disconnected";
        sendJsonToAll(notify);
    }
#endif
    else {
        qWarning() << "Web: Unknown command:" << type;
        QJsonObject err;
        err["type"] = "error";
        err["message"] = QString("Unknown command: %1").arg(type);
        sendJsonTo(client, err);
    }
}

QJsonObject webServer::buildInfoJson() const
{
    QJsonObject info;
    info["version"] = QString(WFWEB_VERSION);

    // freedvModes depends only on compile-time flags, not on the rig,
    // so it must be sent regardless of whether rigCaps is populated yet.
    QJsonArray fdvModes;
#ifdef RADE_SUPPORT
    fdvModes.append("RADE");
#endif
#ifdef FREEDV_SUPPORT
    fdvModes.append("700D");
    fdvModes.append("700E");
    fdvModes.append("1600");
#endif
    info["freedvModes"] = fdvModes;

    info["isLan"] = lanMode;
    info["lanConnected"] = lanConnected;

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
        info["hasFilterSettings"] = rigCaps->commands.contains(funcPBTInner);
        info["hasPowerControl"] = rigCaps->commands.contains(funcPowerControl);
        info["hasMainSub"] = rigCaps->hasCommand29;
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
                QJsonObject po;
                po["num"] = p.num;
                po["name"] = p.name;
                preamps.append(po);
            }
            info["preamps"] = preamps;
        }
        if (!rigCaps->filters.empty()) {
            QJsonArray filters;
            for (const filterType &f : rigCaps->filters) {
                QJsonObject fo;
                fo["num"] = f.num;
                fo["name"] = f.name;
                filters.append(fo);
            }
            info["filters"] = filters;
        }
    } else {
        info["connected"] = false;
    }
    return info;
}

void webServer::sendCurrentState(QWebSocket *client)
{
    QJsonObject info = buildInfoJson();
    info["type"] = "rigInfo";
    if (!audioConfigured && !audioErrorReason.isEmpty()) {
        info["audioError"] = audioErrorReason;
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

    // VFO A and VFO B frequencies (send both).
    // On cmd29 rigs (IC-7610 etc) the two VFOs are Main (rx=0) and Sub (rx=1),
    // both accessed via funcFreq with different receiver indices. On A/B rigs
    // (IC-7300 etc) funcSelectedFreq/funcUnselectedFreq are keyed by which
    // VFO is currently selected on the radio, NOT by the literal A/B labels —
    // so we have to consult rigState.vfo to map them back to A/B.
    bool cmd29 = rigCaps && rigCaps->hasCommand29;
    if (cmd29) {
        vfoCommandType tA = queue->getVfoCommand(vfoA, 0, false);
        cacheItem freqCacheA = queue->getCache(tA.freqFunc, 0);
        if (freqCacheA.value.isValid()) {
            status["vfoAFrequency"] = (qint64)freqCacheA.value.value<freqt>().Hz;
        }
        vfoCommandType tB = queue->getVfoCommand(vfoB, 1, false);
        cacheItem freqCacheB = queue->getCache(tB.freqFunc, 1);
        if (freqCacheB.value.isValid()) {
            status["vfoBFrequency"] = (qint64)freqCacheB.value.value<freqt>().Hz;
        }
        status["selectedVfo"] = (queue->getState().vfo == vfoSub) ? "B" : "A";
    } else {
        bool bSelected = (queue->getState().vfo == vfoB);
        cacheItem selCache = queue->getCache(funcSelectedFreq, 0);
        cacheItem unselCache = queue->getCache(funcUnselectedFreq, 0);
        if (!selCache.value.isValid()) selCache = freqCache; // fall back to plain funcFreq
        if (selCache.value.isValid()) {
            qint64 hz = (qint64)selCache.value.value<freqt>().Hz;
            status[bSelected ? "vfoBFrequency" : "vfoAFrequency"] = hz;
        }
        if (unselCache.value.isValid()) {
            qint64 hz = (qint64)unselCache.value.value<freqt>().Hz;
            status[bSelected ? "vfoAFrequency" : "vfoBFrequency"] = hz;
        }
        status["selectedVfo"] = bSelected ? "B" : "A";
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

    // ALC
    cacheItem alc = queue->getCache(funcALCMeter, 0);
    if (alc.value.isValid()) {
        status["alcMeter"] = alc.value.toDouble();
    }

    // TX status
    cacheItem txStatus = queue->getCache(funcTransceiverStatus, 0);
    if (txStatus.value.isValid()) {
        status["transmitting"] = txStatus.value.toBool();
    }

    // Power state (on/off)
    cacheItem powerState = queue->getCache(funcPowerControl, 0);
    if (powerState.value.isValid()) {
        rigPoweredOn = powerState.value.toBool();
    }
    status["powerState"] = rigPoweredOn;

    status["isLan"] = lanMode;
    status["lanConnected"] = lanConnected;

    // FreeDV
    status["freedv"] = freedvEnabled;
    if (freedvEnabled) {
        status["freedvMode"] = freedvModeName;
        status["freedvSync"] = freedvSync;
        status["freedvSNR"] = (double)freedvSNR;
#ifdef RADE_SUPPORT
        if (freedvModeName == "RADE")
            status["freedvFreqOffset"] = (double)freedvFreqOffset;
#endif
    }

    // Reporter
#ifdef FREEDV_SUPPORT
    status["reporterEnabled"] = reporterEnabled;
    if (freedvReporter) {
        static const char *stateNames[] = {"disconnected", "connecting", "connected", "error"};
        int s = static_cast<int>(freedvReporter->state());
        status["reporterState"] = QString(stateNames[qBound(0, s, 3)]);
    }
#endif

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

    // Mic Gain
    cacheItem micGain = queue->getCache(funcUSBModLevel, 0);
    if (micGain.value.isValid()) {
        status["micGain"] = micGain.value.toInt();
    }

    // Split
    cacheItem split = queue->getCache(funcSplitStatus, 0);
    if (split.value.isValid()) {
        status["split"] = split.value.toBool();
    }

    // Monitor
    cacheItem mon = queue->getCache(funcMonitor, 0);
    if (mon.value.isValid()) status["monitor"] = mon.value.toBool();
    cacheItem monGain = queue->getCache(funcMonitorGain, 0);
    if (monGain.value.isValid()) status["monitorGain"] = monGain.value.toInt();

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

    // PBT Inner/Outer
    cacheItem pbtIn = queue->getCache(funcPBTInner, 0);
    if (pbtIn.value.isValid()) status["pbtInner"] = pbtIn.value.toInt();
    cacheItem pbtOut = queue->getCache(funcPBTOuter, 0);
    if (pbtOut.value.isValid()) status["pbtOuter"] = pbtOut.value.toInt();

    // Filter Shape
    cacheItem fs = queue->getCache(funcFilterShape, 0);
    if (fs.value.isValid()) status["filterShape"] = fs.value.toInt();

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
#ifdef FREEDV_SUPPORT
        if (freedvReporter) freedvReporter->updateFrequency(f.Hz);
#endif
        break;
    }
    case funcMode:
    case funcModeGet:
    case funcModeSet:
    {
        modeInfo m = item.value.value<modeInfo>();
        update["mode"] = modeToString(m);
        update["filter"] = m.filter;
        // FreeDV/RADE only makes sense on voice-carrying modes. If the rig
        // (or the user) switches to CW/RTTY/PSK/etc, tear it down so it
        // doesn't keep trying to decode/encode through a non-audio modem.
        if (freedvEnabled && !isFreeDVCompatibleMode(m.mk)) {
            qCInfo(logWebServer) << "Disabling FreeDV: mode changed to"
                                 << modeToString(m) << "which is not voice-compatible";
            disableFreeDV();
            QJsonObject notify;
            notify["type"] = "freedvStatus";
            notify["enabled"] = false;
            notify["freedvMode"] = freedvModeName;
            notify["freedvSync"] = false;
            notify["freedvSNR"] = 0.0;
#ifdef RADE_SUPPORT
            if (freedvModeName == "RADE")
                notify["freedvFreqOffset"] = 0.0;
#endif
            sendJsonToAll(notify);
        }
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
    case funcALCMeter:
        update["alcMeter"] = item.value.toDouble();
        break;
    case funcTransceiverStatus:
        update["transmitting"] = item.value.toBool();
#ifdef FREEDV_SUPPORT
        if (freedvReporter) freedvReporter->updateTx(freedvModeName, item.value.toBool());
#endif
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
    case funcUSBModLevel:
        update["micGain"] = item.value.toInt();
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
    case funcMonitor:
        update["monitor"] = item.value.toBool();
        break;
    case funcMonitorGain:
        update["monitorGain"] = item.value.toInt();
        break;
    case funcSplitStatus:
        update["split"] = item.value.toBool();
        break;
    case funcTunerStatus:
        update["tuner"] = item.value.toInt();  // 0=off, 1=on, 2=tuning
        break;
    case funcPreamp:
        update["preamp"] = item.value.toInt();
        break;
    case funcAttenuator:
        update["attenuator"] = item.value.toInt();
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
    case funcPBTInner:
        update["pbtInner"] = item.value.toInt();
        break;
    case funcPBTOuter:
        update["pbtOuter"] = item.value.toInt();
        break;
    case funcFilterShape:
        update["filterShape"] = item.value.toInt();
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
    case funcPowerControl:
        rigPoweredOn = item.value.toBool();
        update["powerState"] = rigPoweredOn;
        break;
    case funcMemoryContents:
    {
        // Only process actual memory data from the radio (memoryType),
        // not our own queued request values (uint) echoed back via cache.
        if (strcmp(item.value.typeName(), "memoryType") != 0) {
            return;
        }
        memoryType mem = item.value.value<memoryType>();
        quint32 key = (quint32(mem.group) << 16) | mem.channel;
        if (mem.del || (mem.frequency.Hz == 0 && mem.mode == 0)) {
            memories.remove(key);
        } else {
            memories[key] = mem;
        }
        // Stop scan timer since we got a response
        if (memoryScanTimer) memoryScanTimer->stop();
        // Broadcast to clients (only non-empty channels)
        if (!mem.del && (mem.frequency.Hz != 0 || mem.mode != 0)) {
            QJsonObject memUpdate;
            memUpdate["type"] = "memoryChannel";
            memUpdate["memory"] = memoryToJson(mem);
            sendJsonToAll(memUpdate);
        }
        // Continue scan if active (but not for our own write/delete echoed back)
        if (memoryScanActive && !mem.del) {
            scanNextMemory();
        }
        return; // Don't send as generic update
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

    cacheItem alc = queue->getCache(funcALCMeter, 0);
    if (alc.value.isValid()) {
        status["alcMeter"] = alc.value.toDouble();
    }

    cacheItem txStatus = queue->getCache(funcTransceiverStatus, 0);
    if (txStatus.value.isValid()) {
        status["transmitting"] = txStatus.value.toBool();
    }

    // Include FreeDV/RADE stats when active (5Hz update rate)
    if (freedvEnabled) {
        status["freedvSync"] = freedvSync;
        status["freedvSNR"] = (double)freedvSNR;
#ifdef RADE_SUPPORT
        if (freedvModeName == "RADE")
            status["freedvFreqOffset"] = (double)freedvFreqOffset;
#endif
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

// --- Memory Channels ---

QString webServer::modeRegToString(quint8 reg)
{
    if (rigCaps) {
        for (const modeInfo &mi : rigCaps->modes) {
            if (mi.reg == reg) return mi.name;
        }
    }
    return QString("?%1").arg(reg);
}

QJsonObject webServer::memoryToJson(const memoryType &mem)
{
    QJsonObject o;
    o["group"] = mem.group;
    o["channel"] = mem.channel;
    o["frequency"] = (qint64)mem.frequency.Hz;
    o["mode"] = modeRegToString(mem.mode);
    o["modeReg"] = mem.mode;
    o["filter"] = mem.filter;
    o["name"] = QString::fromLatin1(mem.name).trimmed();
    o["duplex"] = mem.duplex;
    o["split"] = (int)mem.split;
    o["tonemode"] = mem.tonemode;
    o["tone"] = mem.tone;
    o["tsql"] = mem.tsql;
    o["skip"] = mem.skip;
    o["del"] = mem.del;
    if (mem.frequency.Hz == 0 && mem.mode == 0) {
        o["empty"] = true;
    }
    // Include VFO B data if split
    if (mem.split) {
        o["frequencyB"] = (qint64)mem.frequencyB.Hz;
        o["modeB"] = modeRegToString(mem.modeB);
    }
    return o;
}

void webServer::scanNextMemory()
{
    memoryScanCurrent++;
    if (memoryScanCurrent > memoryScanEnd || !queue) {
        memoryScanActive = false;
        if (memoryScanTimer) memoryScanTimer->stop();
        QJsonObject done;
        done["type"] = "memoryScanComplete";
        int count = 0;
        for (auto &m : memories) {
            if (!m.del) count++;
        }
        done["count"] = count;
        sendJsonToAll(done);
        return;
    }
    uint val = (uint(memoryScanGroup) << 16) | uint(memoryScanCurrent);
    queue->addUnique(priorityImmediate, queueItem(funcMemoryContents, QVariant::fromValue<uint>(val), false, 0));
    if (memoryScanTimer) memoryScanTimer->start();
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

void webServer::setLanInfo(bool isLan, bool connected)
{
    bool changed = (lanMode != isLan) || (lanConnected != connected);
    lanMode = isLan;
    lanConnected = connected;
    if (!changed) return;
    QJsonObject obj;
    obj["type"] = "lanStatus";
    obj["isLan"] = lanMode;
    obj["lanConnected"] = lanConnected;
    sendJsonToAll(obj);
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

#ifdef FREEDV_SUPPORT
    // FreeDV processor thread (created once, activated on demand)
    freedvProcessor = new FreeDVProcessor();
    freedvThread = new QThread(this);
    freedvThread->setObjectName("webFreeDV()");
    freedvProcessor->moveToThread(freedvThread);

    connect(this, &webServer::setupFreeDV, freedvProcessor, &FreeDVProcessor::init);
    connect(this, &webServer::sendToFreeDVRx, freedvProcessor, &FreeDVProcessor::processRx);
    connect(this, &webServer::sendToFreeDVTx, freedvProcessor, &FreeDVProcessor::processTx);
    connect(freedvProcessor, &FreeDVProcessor::rxReady, this, &webServer::onFreeDVRxReady);
    connect(freedvProcessor, &FreeDVProcessor::txReady, this, &webServer::onFreeDVTxReady);
    connect(freedvProcessor, &FreeDVProcessor::statsUpdate, this, &webServer::onFreeDVStats);
    connect(freedvProcessor, &FreeDVProcessor::rxCallsign, this, &webServer::onFreeDVRxCallsign);
    connect(freedvThread, &QThread::finished, freedvProcessor, &QObject::deleteLater);

    freedvThread->start();
#endif

#ifdef RADE_SUPPORT
    // RADE V1 processor thread (created once, activated on demand)
    radeProcessor = new RadeProcessor();
    radeThread = new QThread(this);
    radeThread->setObjectName("webRADE()");
    radeProcessor->moveToThread(radeThread);

    connect(this, &webServer::setupRade, radeProcessor, &RadeProcessor::init);
    connect(this, &webServer::sendToRadeRx, radeProcessor, &RadeProcessor::processRx);
    connect(this, &webServer::sendToRadeTx, radeProcessor, &RadeProcessor::processTx);
    connect(radeProcessor, &RadeProcessor::rxReady, this, &webServer::onRadeRxReady);
    connect(radeProcessor, &RadeProcessor::txReady, this, &webServer::onRadeTxReady);
    connect(radeProcessor, &RadeProcessor::statsUpdate, this, &webServer::onRadeStats);
    connect(radeProcessor, &RadeProcessor::rxCallsign, this, &webServer::onRadeRxCallsign);
    connect(radeThread, &QThread::finished, radeProcessor, &QObject::deleteLater);

    radeThread->start();
#endif

#ifdef PACKET_SUPPORT
    // Dire Wolf packet processor thread (created once, activated on demand)
    dwProc = new DireWolfProcessor();
    dwThread = new QThread(this);
    dwThread->setObjectName("webPacket()");
    dwProc->moveToThread(dwThread);

    connect(this, &webServer::setupPacket, dwProc, &DireWolfProcessor::init);
    connect(this, &webServer::sendToPacketRx, dwProc, &DireWolfProcessor::processRx);
    connect(dwProc, &DireWolfProcessor::rxFrameDecoded, this, &webServer::onPacketRxDecoded);
    connect(dwProc, &DireWolfProcessor::txReady, this, &webServer::onPacketTxReady);
    connect(dwProc, &DireWolfProcessor::txFailed, this, &webServer::onPacketTxFailed);
    connect(dwProc, &DireWolfProcessor::captureComplete, this,
            [this](QString path, int rate, int samples) {
                QJsonObject notify;
                notify["type"] = "packetCaptureComplete";
                notify["path"] = path;
                notify["sampleRate"] = rate;
                notify["samples"] = samples;
                sendJsonToAll(notify);
            });
    connect(dwProc, &DireWolfProcessor::captureFailed, this,
            [this](QString reason) {
                QJsonObject notify;
                notify["type"] = "packetCaptureFailed";
                notify["reason"] = reason;
                sendJsonToAll(notify);
            });
    connect(dwThread, &QThread::finished, dwProc, &QObject::deleteLater);

    dwThread->start();

    // Initialize the modem at the current rig sample rate so it's ready when enabled.
    emit setupPacket(sampleRate);

    // AX.25 connected-mode: spin up the data-link state machine.  Owns
    // its own dispatcher thread internally; we just connect signals.
    if (!axProc) {
        axProc = new AX25LinkProcessor(this);
        // Connected-mode TX: ax25_link → AX25LinkProcessor → DireWolfProcessor.
        // QueuedConnection lets the dispatcher thread cross into the dwProc thread.
        connect(axProc, &AX25LinkProcessor::transmitFrameBytes,
                dwProc, &DireWolfProcessor::transmitFrameBytes,
                Qt::QueuedConnection);
        connect(axProc, &AX25LinkProcessor::linkEstablished,
                this,   &webServer::onAxLinkEstablished,
                Qt::QueuedConnection);
        connect(axProc, &AX25LinkProcessor::linkTerminated,
                this,   &webServer::onAxLinkTerminated,
                Qt::QueuedConnection);
        connect(axProc, &AX25LinkProcessor::rxData,
                this,   &webServer::onAxRxData,
                Qt::QueuedConnection);
        axProc->start();
    }
#endif

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
    if (!audioConfigured) return;
    // Forward audio whenever *any* consumer cares: browser audio clients,
    // or an active packet modem that needs samples even when no one is
    // listening to RX audio in the browser.
    bool haveConsumer = !audioClients.isEmpty();
#ifdef PACKET_SUPPORT
    haveConsumer = haveConsumer || packetEnabled;
#endif
    if (!haveConsumer) return;
    if (freedvEnabled && !freedvMonitor) {
#ifdef RADE_SUPPORT
        if (freedvModeName == "RADE")
            emit sendToRadeRx(audio);
        else
#endif
#ifdef FREEDV_SUPPORT
            emit sendToFreeDVRx(audio);
#else
        {}  // no codec available
#endif
    } else {
        emit sendToConverter(audio);
    }
}

#ifdef FREEDV_SUPPORT
void webServer::onFreeDVRxReady(audioPacket audio)
{
    if (rxConverter) {
        // LAN path: feed into converter
        emit sendToConverter(audio);
    } else {
        // USB path: send directly to browser clients
        quint16 rateDiv = static_cast<quint16>(rigSampleRate / 1000);
        QByteArray msg;
        int headerSize = 6;
        msg.resize(headerSize + audio.data.size());
        msg[0] = 0x02;
        msg[1] = 0x00;
        quint16 seq = audioSeq++;
        memcpy(msg.data() + 2, &seq, 2);
        memcpy(msg.data() + 4, &rateDiv, 2);
        memcpy(msg.data() + headerSize, audio.data.constData(), audio.data.size());
        sendBinaryToAudioClients(msg);
    }
}

void webServer::onFreeDVTxReady(audioPacket audio)
{
    if (usbAudioOutput && txAudioConfigured) {
        // Expand mono → stereo if needed
        QByteArray writeData;
        if (usbOutputChannels == 2) {
            int numSamples = audio.data.size() / 2;
            writeData.resize(numSamples * 4);
            const qint16 *src = reinterpret_cast<const qint16 *>(audio.data.constData());
            qint16 *dst = reinterpret_cast<qint16 *>(writeData.data());
            for (int i = 0; i < numSamples; i++) {
                dst[i * 2] = src[i];
                dst[i * 2 + 1] = src[i];
            }
        } else {
            writeData = audio.data;
        }

        // Apply ALC-controlled gain to modem output
        if (freedvTxGain < 0.99f) {
            qint16 *samples = reinterpret_cast<qint16 *>(writeData.data());
            int count = writeData.size() / (int)sizeof(qint16);
            for (int i = 0; i < count; i++)
                samples[i] = qBound((int)-32768, (int)(samples[i] * freedvTxGain), (int)32767);
        }

        // Accumulate modem output; drain timer feeds ALSA at a steady rate
        freedvTxBuffer.append(writeData);

        // Start ALSA and drain timer on first data
        if (!freedvTxActive) {
            freedvTxActive = true;
            // Stop any existing ALSA session (started by normal mic path)
            if (usbAudioOutput->state() != QAudio::StoppedState)
                usbAudioOutput->stop();
            usbAudioOutputDevice = nullptr;

            usbAudioOutputDevice = usbAudioOutput->start();
            if (usbAudioOutputDevice) {
                txAudioActive = true;
                preTxBuffering = false;
                // Pre-fill ALSA with silence for headroom
                QByteArray prefill(4800 * usbOutputChannels, 0);  // 50ms
                usbAudioOutputDevice->write(prefill);

                if (!freedvTxDrainTimer) {
                    freedvTxDrainTimer = new QTimer(this);
                    freedvTxDrainTimer->setTimerType(Qt::PreciseTimer);
                    connect(freedvTxDrainTimer, &QTimer::timeout, this, &webServer::drainFreeDVTxBuffer);
                }
                freedvTxDrainTimer->start(10);  // 10ms ticks
                qInfo() << "Web: FreeDV TX started, ALSA bufSize=" << usbAudioOutput->bufferSize();
            } else {
                qWarning() << "Web: FreeDV TX ALSA start() failed";
                freedvTxActive = false;
            }
        }
    } else if (txConverter) {
        emit sendToTxConverter(audio);
    }
}
#endif // FREEDV_SUPPORT

void webServer::drainFreeDVTxBuffer()
{
    if (!usbAudioOutputDevice) return;

    // Write 10ms per tick (48kHz × 2 bytes × channels × 0.010s)
    int chunkSize = 960 * usbOutputChannels;  // 960 bytes mono, 1920 stereo

    if (freedvTxBuffer.size() >= chunkSize) {
        usbAudioOutputDevice->write(freedvTxBuffer.left(chunkSize));
        freedvTxBuffer.remove(0, chunkSize);
    } else if (!freedvTxBuffer.isEmpty()) {
        // Partial data: pad with silence to maintain timing
        QByteArray chunk = freedvTxBuffer;
        chunk.append(QByteArray(chunkSize - chunk.size(), 0));
        usbAudioOutputDevice->write(chunk);
        freedvTxBuffer.clear();
    } else if (radeEooDraining
#ifdef PACKET_SUPPORT
               || packetTxDraining
#endif
              ) {
        // One-shot burst (RADE EOO or packet frame) fully drained — stop
        // ALSA and clean up.  For packet we also schedule a delayed PTT-off
        // so the radio has time to push the tail samples through.
#ifdef PACKET_SUPPORT
        bool wasPacket = packetTxDraining;
        packetTxDraining = false;
#endif
        radeEooDraining = false;
        freedvTxActive = false;
        freedvTxDrainTimer->stop();
        txAudioActive = false;
        if (usbAudioOutput) {
            usbAudioOutput->stop();
            usbAudioOutputDevice = nullptr;
        }
#ifdef PACKET_SUPPORT
        if (wasPacket) {
            qInfo() << "Web: Packet TX drain complete, ALSA stopped — delayed unkey";
            QTimer::singleShot(300, this, [this]() {
                if (queue) {
                    queue->add(priorityImmediate,
                               queueItem(funcTransceiverStatus,
                                         QVariant::fromValue<bool>(false), false, uchar(0)));
                    queue->del(funcALCMeter, 0);
                }
                QJsonObject notify;
                notify["type"] = "packetTxComplete";
                sendJsonToAll(notify);
            });
            return;
        }
#endif
        qInfo() << "Web: RADE EOO drain complete, ALSA stopped";
        return;
    } else {
        // No data: write silence to keep ALSA fed
        QByteArray silence(chunkSize, 0);
        usbAudioOutputDevice->write(silence);
    }
}

#ifdef FREEDV_SUPPORT
void webServer::onFreeDVStats(float snr, bool sync)
{
    freedvSNR = snr;
    bool wasSync = freedvSync;
    freedvSync = sync;
    if (sync != wasSync) {
        QJsonObject notify;
        notify["type"] = "freedvStatus";
        notify["freedvSync"] = sync;
        notify["freedvSNR"] = (double)snr;
        sendJsonToAll(notify);

        if (freedvReporter) {
            if (sync) {
                // Sync acquired: send immediate report, start periodic updates
                lastReportedCallsign.clear();
                freedvReporter->updateRxSpot(QString(), freedvModeName, (int)snr);
                startReporterSnrTimer();
            } else {
                // Sync lost: stop periodic updates
                stopReporterSnrTimer();
            }
        }
    }
}
void webServer::onFreeDVRxCallsign(const QString &callsign)
{
    qInfo() << "FreeDV: decoded callsign:" << callsign;
    lastReportedCallsign = callsign;
    if (freedvReporter && freedvSync)
        freedvReporter->updateRxSpot(callsign, freedvModeName, (int)freedvSNR);
}

void webServer::onReporterStateChanged(int state)
{
    QString stateStr;
    switch (static_cast<SpotReporter::State>(state)) {
    case SpotReporter::Disconnected: stateStr = "disconnected"; break;
    case SpotReporter::Connecting:   stateStr = "connecting";   break;
    case SpotReporter::Connected:    stateStr = "connected";    break;
    case SpotReporter::Error:        stateStr = "error";        break;
    }

    QJsonObject notify;
    notify["type"] = "reporterStatus";
    notify["enabled"] = reporterEnabled;
    notify["state"] = stateStr;
    sendJsonToAll(notify);
}

void webServer::startReporterSnrTimer()
{
    if (!reporterSnrTimer) {
        reporterSnrTimer = new QTimer(this);
        reporterSnrTimer->setInterval(4000);
        connect(reporterSnrTimer, &QTimer::timeout, this, [this]() {
            if (freedvReporter && freedvSync)
                freedvReporter->updateRxSpot(lastReportedCallsign, freedvModeName, (int)freedvSNR);
        });
    }
    reporterSnrTimer->start();
}

void webServer::stopReporterSnrTimer()
{
    if (reporterSnrTimer)
        reporterSnrTimer->stop();
    lastReportedCallsign.clear();
}
#endif // FREEDV_SUPPORT

#ifdef RADE_SUPPORT
void webServer::onRadeRxReady(audioPacket audio)
{
    // Same output path as FreeDV: decoded speech -> converter or direct to browser
    if (rxConverter) {
        emit sendToConverter(audio);
    } else {
        quint16 rateDiv = static_cast<quint16>(rigSampleRate / 1000);
        QByteArray msg;
        int headerSize = 6;
        msg.resize(headerSize + audio.data.size());
        msg[0] = 0x02;
        msg[1] = 0x00;
        quint16 seq = audioSeq++;
        memcpy(msg.data() + 2, &seq, 2);
        memcpy(msg.data() + 4, &rateDiv, 2);
        memcpy(msg.data() + headerSize, audio.data.constData(), audio.data.size());
        sendBinaryToAudioClients(msg);
    }
}

void webServer::onRadeTxReady(audioPacket audio)
{
    // Same output path as FreeDV TX: modem tones -> USB ALSA or LAN converter
    if (usbAudioOutput && txAudioConfigured) {
        QByteArray writeData;
        if (usbOutputChannels == 2) {
            int numSamples = audio.data.size() / 2;
            writeData.resize(numSamples * 4);
            const qint16 *src = reinterpret_cast<const qint16 *>(audio.data.constData());
            qint16 *dst = reinterpret_cast<qint16 *>(writeData.data());
            for (int i = 0; i < numSamples; i++) {
                dst[i * 2] = src[i];
                dst[i * 2 + 1] = src[i];
            }
        } else {
            writeData = audio.data;
        }

        // RADE uses a fixed TX level. The modem is a full-duty constant-envelope
        // signal, and radeprocessor.cpp already applies RADE_TX_SCALE. Multiplying
        // by a drifting freedvTxGain on top caused progressive power loss — use a
        // known-good constant instead and keep the ALC loop out of RADE entirely.
        {
            qint16 *samples = reinterpret_cast<qint16 *>(writeData.data());
            int count = writeData.size() / (int)sizeof(qint16);
            for (int i = 0; i < count; i++)
                samples[i] = qBound((int)-32768, (int)(samples[i] * RADE_TX_GAIN), (int)32767);
        }

        freedvTxBuffer.append(writeData);

        if (!freedvTxActive) {
            freedvTxActive = true;
            if (usbAudioOutput->state() != QAudio::StoppedState)
                usbAudioOutput->stop();
            usbAudioOutputDevice = nullptr;

            usbAudioOutputDevice = usbAudioOutput->start();
            if (usbAudioOutputDevice) {
                txAudioActive = true;
                preTxBuffering = false;
                QByteArray prefill(4800 * usbOutputChannels, 0);
                usbAudioOutputDevice->write(prefill);

                if (!freedvTxDrainTimer) {
                    freedvTxDrainTimer = new QTimer(this);
                    freedvTxDrainTimer->setTimerType(Qt::PreciseTimer);
                    connect(freedvTxDrainTimer, &QTimer::timeout, this, &webServer::drainFreeDVTxBuffer);
                }
                freedvTxDrainTimer->start(10);
                qInfo() << "Web: RADE TX started, ALSA bufSize=" << usbAudioOutput->bufferSize();
            } else {
                qWarning() << "Web: RADE TX ALSA start() failed";
                freedvTxActive = false;
            }
        }
    } else if (txConverter) {
        emit sendToTxConverter(audio);
    }
}

void webServer::onRadeStats(float snr, bool sync, float freqOffset)
{
    freedvSNR = snr;
    freedvFreqOffset = freqOffset;
    bool wasSync = freedvSync;
    freedvSync = sync;
    if (sync != wasSync) {
        QJsonObject notify;
        notify["type"] = "freedvStatus";
        notify["freedvSync"] = sync;
        notify["freedvSNR"] = (double)snr;
        notify["freedvFreqOffset"] = (double)freqOffset;
        sendJsonToAll(notify);
#ifdef FREEDV_SUPPORT
        if (freedvReporter) {
            if (sync) {
                // Sync acquired: send immediate report, start periodic updates
                lastReportedCallsign.clear();
                freedvReporter->updateRxSpot(QString(), freedvModeName, (int)snr);
                startReporterSnrTimer();
            } else {
                // Sync lost: stop periodic updates
                stopReporterSnrTimer();
            }
        }
#endif
        // Callsign clearing is handled by a timer started in onRadeRxCallsign.
    }
}

void webServer::onRadeRxCallsign(const QString &callsign)
{
    radeRxCallsign = callsign;

    // Send to browser
    QJsonObject notify;
    notify["type"] = "radeCallsign";
    notify["callsign"] = callsign;
    sendJsonToAll(notify);

    // Clear the callsign from the UI after 5 seconds
    if (!radeCallsignClearTimer) {
        radeCallsignClearTimer = new QTimer(this);
        radeCallsignClearTimer->setSingleShot(true);
        connect(radeCallsignClearTimer, &QTimer::timeout, this, [this]() {
            radeRxCallsign.clear();
            QJsonObject csNotify;
            csNotify["type"] = "radeCallsign";
            csNotify["callsign"] = QString();
            sendJsonToAll(csNotify);
        });
    }
    radeCallsignClearTimer->start(5000);

#ifdef FREEDV_SUPPORT
    // Report to FreeDV Reporter with the decoded callsign.
    // No sync check: the callsign arrives from the EOO frame which is
    // the last frame — sync is already lost by the time the callback fires.
    lastReportedCallsign = callsign;
    if (freedvReporter) {
        qInfo() << "Web: reporting RX spot to FreeDV Reporter:" << callsign
                << "mode=" << freedvModeName << "snr=" << (int)freedvSNR;
        freedvReporter->updateRxSpot(callsign, freedvModeName, (int)freedvSNR);
    } else {
        qInfo() << "Web: no FreeDV Reporter instance, skipping RX spot";
    }
#endif

    qInfo() << "Web: RADE decoded callsign:" << callsign;
}
#endif

#ifdef PACKET_SUPPORT
void webServer::onPacketRxDecoded(int chan, QJsonObject frame)
{
    QJsonObject msg = frame;
    msg["type"] = "packetRxFrame";
    msg["chan"] = chan;
    msg["ts"] = QDateTime::currentMSecsSinceEpoch();
    sendJsonToAll(msg);

    qInfo() << "Web: packet RX chan=" << chan
            << "src=" << frame.value("src").toString()
            << "dst=" << frame.value("dst").toString()
            << "info=" << frame.value("info").toString();
}

// Receives the complete encoded burst for a single AX.25 frame (entire
// preamble+frame+postamble in one audioPacket).  Pushes it into the shared
// TX buffer and drives ALSA through the same drain timer FreeDV/RADE use.
// packetTxDraining tells drainFreeDVTxBuffer to stop ALSA and unkey the
// radio once the buffer empties.
void webServer::onPacketTxReady(audioPacket audio)
{
    // If a connected-mode file transfer is in progress, this audio burst
    // is the encoding of the YAPP frame we queued last — advance
    // progress and pump the next one.  Doing it here (rather than on a
    // Qt timer) keeps at most one frame in ax25_link's queue, so Abort
    // doesn't leave a long tail of pre-queued frames on the air.
    if (TerminalSession *x = yappFindActiveTxXfer()) {
        x->xfer.framesSent++;
        if (x->xfer.framesPlanned > 0) {
            x->xfer.done = qMin<qint64>(x->xfer.total,
                (qint64)x->xfer.total * x->xfer.framesSent / x->xfer.framesPlanned);
        }
        xferBroadcastProgress(x);

        if (x->xfer.framesSent >= x->xfer.framesPlanned) {
            QByteArray info = QString("*** Sent: %1 (%2 bytes)")
                .arg(x->xfer.name).arg(x->xfer.total).toUtf8();
            QJsonObject e = termScrollbackEntry(QStringLiteral("info"), info);
            termAppendScrollback(x, e);
            termBroadcastData(x->sid, e);
            xferBroadcastEnd(x, /*aborted=*/false);
            x->xfer = XferState{};
        } else {
            yappPumpNextFrame(x);
        }
    }

    auto unkey = [this]() {
        if (queue) {
            queue->add(priorityImmediate,
                       queueItem(funcTransceiverStatus,
                                 QVariant::fromValue<bool>(false), false, uchar(0)));
            queue->del(funcALCMeter, 0);
        }
    };

    // Key PTT if no burst is currently in flight.  The packetTx command
    // does this itself before invoking transmitFrame, so packetTxDraining
    // is already true on that path; for connected-mode TX (AX25LinkProcessor
    // → transmitFrameBytes), nothing has keyed the radio yet, so do it
    // here.  Existing drain logic in drainFreeDVTxBuffer / drainPacketLanTxBuffer
    // already handles the delayed unkey when the buffer empties.
    if (!packetTxDraining) {
        if (queue) {
            queue->add(priorityImmediate,
                       queueItem(funcTransceiverStatus,
                                 QVariant::fromValue<bool>(true), false, uchar(0)));
            queue->addUnique(priorityHighest,
                             queueItem(funcALCMeter, true, 0));
        }
        packetTxDraining = true;
    }

    // LAN path: route through the TX converter instead of USB output.
    // The Opus encoder inside audioConverter only accepts specific frame
    // sizes (2.5 / 5 / 10 / 20 / 40 / 60 ms at sample rate), so a single
    // multi-hundred-ms blob would be dropped.  Chunk into 20 ms slices
    // and emit them at wall-clock rate — matches the mic path and keeps
    // the rig's UDP audio jitter buffer happy.
    if (!usbAudioOutput || !txAudioConfigured) {
        if (txConverter) {
            quint32 rate = rigSampleRate ? rigSampleRate : 48000;
            packetLanTxChunkBytes = (int)(rate / 50) * (int)sizeof(qint16);
            // APPEND — multiple YAPP frames arrive back-to-back during a
            // file transfer; replacing the buffer here would truncate any
            // audio still being drained from the previous frame and garble
            // the on-air stream, triggering AX.25 retries.  Timer only
            // starts on the first frame; subsequent calls just extend the
            // playout.
            bool wasIdle = packetLanTxBuffer.isEmpty()
                           && (!packetLanTxTimer || !packetLanTxTimer->isActive());
            packetLanTxBuffer.append(audio.data);
            if (!packetLanTxTimer) {
                packetLanTxTimer = new QTimer(this);
                packetLanTxTimer->setTimerType(Qt::PreciseTimer);
                connect(packetLanTxTimer, &QTimer::timeout,
                        this, &webServer::drainPacketLanTxBuffer);
            }
            qInfo() << "Web: Packet LAN TX"
                    << (wasIdle ? "started" : "appended")
                    << "bytes=" << audio.data.size()
                    << "total=" << packetLanTxBuffer.size()
                    << "chunkBytes=" << packetLanTxChunkBytes;
            if (wasIdle) packetLanTxTimer->start(20);
            return;
        }
        qWarning() << "Web: Packet TX: no USB output and no TX converter";
        packetTxDraining = false;
        unkey();
        QJsonObject notify;
        notify["type"] = "packetTxFailed";
        notify["reason"] = "no TX audio path available";
        sendJsonToAll(notify);
        return;
    }

    // Expand mono -> stereo if the USB output is a stereo device.
    QByteArray writeData;
    if (usbOutputChannels == 2) {
        int numSamples = audio.data.size() / (int)sizeof(qint16);
        writeData.resize(numSamples * 4);
        const qint16 *src = reinterpret_cast<const qint16 *>(audio.data.constData());
        qint16 *dst = reinterpret_cast<qint16 *>(writeData.data());
        for (int i = 0; i < numSamples; i++) {
            dst[i * 2] = src[i];
            dst[i * 2 + 1] = src[i];
        }
    } else {
        writeData = audio.data;
    }

    // Apply the existing ALC-controlled gain so the same slider governs
    // packet TX level alongside FreeDV/RADE.  Packet modem output is full
    // amplitude by default which would clip the rig's USB input.
    if (freedvTxGain < 0.99f) {
        qint16 *samples = reinterpret_cast<qint16 *>(writeData.data());
        int count = writeData.size() / (int)sizeof(qint16);
        for (int i = 0; i < count; i++)
            samples[i] = qBound((int)-32768, (int)(samples[i] * freedvTxGain), (int)32767);
    }

    freedvTxBuffer.append(writeData);
    packetTxDraining = true;

    if (!freedvTxActive) {
        freedvTxActive = true;
        if (usbAudioOutput->state() != QAudio::StoppedState)
            usbAudioOutput->stop();
        usbAudioOutputDevice = nullptr;

        usbAudioOutputDevice = usbAudioOutput->start();
        if (usbAudioOutputDevice) {
            txAudioActive = true;
            preTxBuffering = false;
            // 50 ms silence prefill (identical to FreeDV/RADE TX start).
            QByteArray prefill(4800 * usbOutputChannels, 0);
            usbAudioOutputDevice->write(prefill);

            if (!freedvTxDrainTimer) {
                freedvTxDrainTimer = new QTimer(this);
                freedvTxDrainTimer->setTimerType(Qt::PreciseTimer);
                connect(freedvTxDrainTimer, &QTimer::timeout, this, &webServer::drainFreeDVTxBuffer);
            }
            freedvTxDrainTimer->start(10);
            qInfo() << "Web: Packet TX started, audioBytes=" << audio.data.size()
                    << "bufSize=" << usbAudioOutput->bufferSize();
        } else {
            qWarning() << "Web: Packet TX ALSA start() failed";
            freedvTxActive = false;
            packetTxDraining = false;
            freedvTxBuffer.clear();
            unkey();
            QJsonObject notify;
            notify["type"] = "packetTxFailed";
            notify["reason"] = "audio output start failed";
            sendJsonToAll(notify);
        }
    }
}

void webServer::onPacketTxFailed(QString reason)
{
    qWarning() << "Web: Packet TX failed:" << reason;
    // Release the TX slot and unkey if we already keyed.
    packetTxDraining = false;
    if (queue) {
        queue->add(priorityImmediate,
                   queueItem(funcTransceiverStatus,
                             QVariant::fromValue<bool>(false), false, uchar(0)));
        queue->del(funcALCMeter, 0);
    }
    QJsonObject notify;
    notify["type"] = "packetTxFailed";
    notify["reason"] = reason;
    sendJsonToAll(notify);
}

// LAN path drain: feeds one 20 ms chunk per tick into the TX converter.
// When the buffer empties, delays unkey by 300 ms so the tail has time
// to clear the rig's UDP audio buffer before PTT drops.
void webServer::drainPacketLanTxBuffer()
{
    if (packetLanTxBuffer.isEmpty()) {
        if (packetLanTxTimer) packetLanTxTimer->stop();
        qInfo() << "Web: Packet LAN TX drained, scheduling unkey";
        QTimer::singleShot(300, this, [this]() {
            packetTxDraining = false;
            if (queue) {
                queue->add(priorityImmediate,
                           queueItem(funcTransceiverStatus,
                                     QVariant::fromValue<bool>(false), false, uchar(0)));
                queue->del(funcALCMeter, 0);
            }
            QJsonObject notify;
            notify["type"] = "packetTxComplete";
            sendJsonToAll(notify);
        });
        return;
    }

    int take = qMin(packetLanTxChunkBytes, packetLanTxBuffer.size());
    audioPacket chunk;
    chunk.data = packetLanTxBuffer.left(take);
    chunk.time = QTime::currentTime();
    chunk.volume = 1.0;
    packetLanTxBuffer.remove(0, take);
    emit sendToTxConverter(chunk);
}

// ---- Packet-modem pref persistence ----
//
// packetEnabled and packetMode survive a wfweb restart via the same
// QSettings file the rest of servermain uses.  Load at setupAudio() /
// setupUsbAudio() time (after dwProc exists so we can apply them);
// save on every user change.

void webServer::setSettingsFile(const QString &path)
{
    packetSettingsFile_ = path;
}

static QSettings *packetSettingsFor(const QString &file)
{
    return file.isEmpty()
        ? new QSettings()
        : new QSettings(file, QSettings::IniFormat);
}

void webServer::packetLoadSettings()
{
    QSettings *s = packetSettingsFor(packetSettingsFile_);
    packetEnabled = s->value("Packet/Enabled", false).toBool();
    int m = s->value("Packet/Mode", 300).toInt();
    if (m == 300 || m == 1200 || m == 9600) packetMode = m;
    delete s;
    qInfo() << "Web: Packet prefs loaded — enabled=" << packetEnabled
            << "mode=" << packetMode;
}

void webServer::packetSaveSettings()
{
    QSettings *s = packetSettingsFor(packetSettingsFile_);
    s->setValue("Packet/Enabled", packetEnabled);
    s->setValue("Packet/Mode", packetMode);
    s->sync();
    delete s;
}

// ---- AX.25 connected-mode terminal session helpers ----

QString webServer::termMakeSid()
{
    return QStringLiteral("t%1").arg(termNextSidNum++);
}

QJsonObject webServer::termSessionToJson(const TerminalSession *s) const
{
    QJsonObject j;
    j["sid"] = s->sid;
    j["chan"] = s->chan;
    j["ownCall"] = s->ownCall;
    j["peerCall"] = s->peerCall;
    QJsonArray digisJ;
    for (const QString &d : s->digis) digisJ.append(d);
    j["digis"] = digisJ;
    const char *st = "disconnected";
    switch (s->state) {
    case TerminalSession::Disconnected:  st = "disconnected"; break;
    case TerminalSession::Connecting:    st = "connecting";   break;
    case TerminalSession::Connected:     st = "connected";    break;
    case TerminalSession::Disconnecting: st = "disconnecting";break;
    }
    j["state"] = QString::fromLatin1(st);
    j["incoming"] = s->incoming;
    j["createdMs"] = (qint64)s->createdMs;
    return j;
}

QJsonObject webServer::termScrollbackEntry(const QString &dir, const QByteArray &data)
{
    QJsonObject e;
    e["ts"] = QDateTime::currentMSecsSinceEpoch();
    e["dir"] = dir;          // "rx" | "tx" | "info"
    // Send as UTF-8 string for printable text; binary payloads still survive
    // round-tripping because Qt's QString::fromUtf8 preserves byte sequences
    // it can decode and uses replacement characters otherwise.  YAPP-C
    // (M6) will switch to base64.
    e["data"] = QString::fromUtf8(data);
    return e;
}

void webServer::termAppendScrollback(TerminalSession *s, const QJsonObject &entry)
{
    s->scrollback.append(entry);
    while (s->scrollback.size() > TERM_SCROLLBACK_MAX) s->scrollback.removeFirst();
    s->lastActivityMs = QDateTime::currentMSecsSinceEpoch();
}

void webServer::termBroadcastSession(const TerminalSession *s)
{
    QJsonObject notify = termSessionToJson(s);
    notify["type"] = "termSession";
    sendJsonToAll(notify);
}

void webServer::termBroadcastData(const QString &sid, const QJsonObject &entry)
{
    QJsonObject notify = entry;
    notify["type"] = "termData";
    notify["sid"] = sid;
    sendJsonToAll(notify);
}

webServer::TerminalSession *webServer::termFindByEndpoints(int chan,
                                                            const QString &own,
                                                            const QString &peer)
{
    for (TerminalSession *s : termSessions) {
        if (s->chan == chan && s->ownCall == own && s->peerCall == peer)
            return s;
    }
    return nullptr;
}

void webServer::termEnsureRegistered(int chan, const QString &ownCall)
{
    if (termRegisteredOwnCalls.contains(ownCall)) return;
    if (!axProc) return;
    termRegisteredOwnCalls.insert(ownCall);
    QMetaObject::invokeMethod(axProc, "registerCallsign", Qt::QueuedConnection,
                              Q_ARG(int, TERM_FIXED_CLIENT),
                              Q_ARG(int, chan),
                              Q_ARG(QString, ownCall));
}

void webServer::onAxLinkEstablished(int client, int chan,
                                    const QString &peerCall, const QString &ownCall,
                                    bool incoming)
{
    (void)client;
    TerminalSession *s = termFindByEndpoints(chan, ownCall, peerCall);
    if (!s) {
        // Inbound to a registered own_call: create a session record so the
        // browser can attach.  The user might not have a UI tab open yet —
        // termList will surface it on next refresh.
        s = new TerminalSession();
        s->sid = termMakeSid();
        s->chan = chan;
        s->ownCall = ownCall;
        s->peerCall = peerCall;
        s->incoming = true;
        s->createdMs = QDateTime::currentMSecsSinceEpoch();
        s->lastActivityMs = s->createdMs;
        termSessions.insert(s->sid, s);
    }
    s->state = TerminalSession::Connected;
    termAppendScrollback(s, termScrollbackEntry(QStringLiteral("info"),
        QString("*** CONNECTED to %1").arg(peerCall).toUtf8()));
    termBroadcastSession(s);
    qInfo().noquote() << "Web: AX.25 link up" << s->sid
                      << ownCall << "<->" << peerCall
                      << (incoming ? "(inbound)" : "(outbound)");
}

void webServer::onAxLinkTerminated(int client, int chan,
                                   const QString &peerCall, const QString &ownCall,
                                   int timeout)
{
    (void)client;
    TerminalSession *s = termFindByEndpoints(chan, ownCall, peerCall);
    if (!s) return;
    s->state = TerminalSession::Disconnected;
    QString reason = timeout ? QStringLiteral("*** DISCONNECTED (link timeout)")
                             : QStringLiteral("*** DISCONNECTED");
    termAppendScrollback(s, termScrollbackEntry(QStringLiteral("info"), reason.toUtf8()));
    termBroadcastSession(s);
    qInfo().noquote() << "Web: AX.25 link down" << s->sid
                      << "timeout=" << timeout;
}

void webServer::onAxRxData(int client, int chan,
                           const QString &peerCall, const QString &ownCall,
                           int pid, const QByteArray &data)
{
    (void)client; (void)pid;
    TerminalSession *s = termFindByEndpoints(chan, ownCall, peerCall);
    if (!s) {
        qWarning() << "Web: AX.25 rxData for unknown session"
                   << ownCall << "<->" << peerCall << "chan=" << chan;
        return;
    }
    // Split the incoming stream into ordinary text + YAPP frames.  YAPP
    // frames (SOH-led) are dispatched by yappHandleFrame; what comes back
    // in textOut is just the non-YAPP bytes, which become normal scrollback.
    QByteArray textOut;
    yappFeedIncoming(s, data, textOut);
    if (!textOut.isEmpty()) {
        QJsonObject entry = termScrollbackEntry(QStringLiteral("rx"), textOut);
        termAppendScrollback(s, entry);
        termBroadcastData(s->sid, entry);
    }
}

// ---- Minimal YAPP framing ----
//
// Subset of classic YAPP sufficient for a single file send on top of an
// established connected-mode AX.25 session.  No SI/RR/AT/AB/hash — just
// HD (header) / DT (data) / EF (end of file).  Framing:
//
//     SOH (0x01) | len (u8) | type (u8) | data (<= 255 bytes)
//
// HD payload: "<filename>\0<size-decimal>\0"
// DT payload: raw file bytes
// EF payload: empty

QByteArray webServer::yappFrame(char type, const QByteArray &data)
{
    Q_ASSERT(data.size() <= 255);
    QByteArray out;
    out.reserve(data.size() + 3);
    out.append((char)0x01);
    out.append((char)(uchar)data.size());
    out.append(type);
    out.append(data);
    return out;
}

void webServer::yappFeedIncoming(TerminalSession *s, const QByteArray &chunk,
                                 QByteArray &textOut)
{
    YappRxState &y = s->yapp;
    for (int i = 0; i < chunk.size(); i++) {
        quint8 b = (quint8)chunk[i];
        switch (y.phase) {
        case YappRxState::Idle:
            if (b == 0x01) { y.phase = YappRxState::AwaitLen; }
            else           { textOut.append((char)b); }
            break;
        case YappRxState::AwaitLen:
            y.remaining = (int)b;
            y.buffer.clear();
            y.phase = YappRxState::AwaitType;
            break;
        case YappRxState::AwaitType:
            y.type = (char)b;
            if (y.remaining == 0) {
                yappHandleFrame(s, y.type, QByteArray());
                y.phase = YappRxState::Idle;
            } else {
                y.phase = YappRxState::AwaitData;
            }
            break;
        case YappRxState::AwaitData:
            y.buffer.append((char)b);
            if (--y.remaining == 0) {
                yappHandleFrame(s, y.type, y.buffer);
                y.phase = YappRxState::Idle;
            }
            break;
        }
    }
}

void webServer::xferBroadcastStart(const TerminalSession *s)
{
    QJsonObject notify;
    notify["type"]  = "termXferStart";
    notify["sid"]   = s->sid;
    notify["dir"]   = s->xfer.dir;
    notify["name"]  = s->xfer.name;
    notify["total"] = (qint64)s->xfer.total;
    notify["phase"] = s->xfer.phase;
    sendJsonToAll(notify);
    qInfo().noquote() << "Web: xferStart" << s->sid << s->xfer.dir
                      << s->xfer.phase
                      << s->xfer.name << s->xfer.total << "B"
                      << "wsClients=" << wsClients.size();
}

void webServer::xferBroadcastProgress(TerminalSession *s, bool force)
{
    // Throttle to at most 10 updates/sec so WS doesn't get flooded on
    // large files.
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!force && now - s->xfer.lastProgressMs < 100) return;
    s->xfer.lastProgressMs = now;

    QJsonObject notify;
    notify["type"]  = "termXferProgress";
    notify["sid"]   = s->sid;
    notify["done"]  = (qint64)s->xfer.done;
    notify["total"] = (qint64)s->xfer.total;
    sendJsonToAll(notify);
    qInfo().noquote() << "Web: xferProgress" << s->sid
                      << s->xfer.done << "/" << s->xfer.total;
}

void webServer::xferBroadcastEnd(const TerminalSession *s, bool aborted)
{
    QJsonObject notify;
    notify["type"]    = "termXferEnd";
    notify["sid"]     = s->sid;
    notify["aborted"] = aborted;
    sendJsonToAll(notify);
}

void webServer::yappHandleFrame(TerminalSession *s, char type, const QByteArray &data)
{
    YappRxState &y = s->yapp;
    switch (type) {
    case 'S': {                     // SI — peer wants to send a file
        if (s->xfer.active) {
            // Already busy with another xfer; reject.
            yappSendAB(s);
            return;
        }
        int nul = data.indexOf('\0');
        QString senderInfo = (nul < 0)
            ? QString::fromUtf8(data)
            : QString::fromUtf8(data.constData(), nul);
        s->xfer.active = true;
        s->xfer.dir    = QStringLiteral("rx");
        s->xfer.phase  = QStringLiteral("awaiting_accept");
        s->xfer.name.clear();
        s->xfer.total  = 0;
        s->xfer.done   = 0;

        QJsonObject notify;
        notify["type"]       = "termXferRequest";
        notify["sid"]        = s->sid;
        notify["senderInfo"] = senderInfo;
        sendJsonToAll(notify);

        QByteArray info = QString("*** %1 wants to send you a file — accept or reject")
            .arg(senderInfo).toUtf8();
        QJsonObject e = termScrollbackEntry(QStringLiteral("info"), info);
        termAppendScrollback(s, e);
        termBroadcastData(s->sid, e);
        qInfo().noquote() << "Web: YAPP SI received" << s->sid << "from" << senderInfo;
        break;
    }
    case 'R':                       // RR — peer accepted our request
        if (s->xfer.active && s->xfer.dir == "tx" && s->xfer.phase == "awaiting_rr") {
            yappStartDataPhase(s);
        } else {
            qWarning().noquote() << "Web: unexpected YAPP RR" << s->sid
                                 << "active=" << s->xfer.active
                                 << "dir="    << s->xfer.dir
                                 << "phase="  << s->xfer.phase;
        }
        break;
    case 'H': {                     // HD — new file starting
        int nul1 = data.indexOf('\0');
        if (nul1 < 0) return;
        QString name = QString::fromUtf8(data.constData(), nul1);
        int nul2 = data.indexOf('\0', nul1 + 1);
        QString szStr = (nul2 < 0)
            ? QString::fromLatin1(data.constData() + nul1 + 1, data.size() - nul1 - 1)
            : QString::fromLatin1(data.constData() + nul1 + 1, nul2 - nul1 - 1);
        qint64 sz = szStr.toLongLong();
        y.filename = name;
        y.filesize = sz;
        y.fileBuf.clear();
        y.active = true;

        // Kick off the shared transfer widget on the UI.
        s->xfer.active = true;
        s->xfer.dir    = QStringLiteral("rx");
        s->xfer.name   = name;
        s->xfer.total  = sz;
        s->xfer.done   = 0;
        xferBroadcastStart(s);

        QByteArray info = QString("*** Incoming file: %1 (%2 bytes)")
            .arg(name).arg(sz).toUtf8();
        QJsonObject e = termScrollbackEntry(QStringLiteral("info"), info);
        termAppendScrollback(s, e);
        termBroadcastData(s->sid, e);
        qInfo().noquote() << "Web: YAPP RX start" << s->sid << name << sz << "B";
        break;
    }
    case 'D':                       // DT — data block
        if (y.active) {
            y.fileBuf.append(data);
            s->xfer.done = y.fileBuf.size();
            xferBroadcastProgress(s);
        }
        break;
    case 'A': {                     // AB — abort from peer
        if (!s->xfer.active) return;
        QString reason;
        if (s->xfer.dir == "tx") {
            reason = (s->xfer.phase == "awaiting_rr")
                ? QStringLiteral("*** Peer rejected the transfer")
                : QStringLiteral("*** Transfer aborted by peer");
        } else {
            reason = QString("*** Transfer aborted by sender: %1").arg(y.filename);
        }
        QJsonObject e = termScrollbackEntry(QStringLiteral("info"), reason.toUtf8());
        termAppendScrollback(s, e);
        termBroadcastData(s->sid, e);

        xferBroadcastEnd(s, /*aborted=*/true);
        s->xfer = XferState{};

        y.filename.clear();
        y.fileBuf.clear();
        y.filesize = 0;
        y.active = false;
        qInfo().noquote() << "Web: YAPP aborted (AB from peer)" << s->sid;
        break;
    }
    case 'E': {                     // EF — end of file
        if (!y.active) return;
        s->xfer.done = y.fileBuf.size();
        xferBroadcastProgress(s, /*force=*/true);

        QByteArray info = QString("*** File complete: %1 (%2 bytes)")
            .arg(y.filename).arg(y.fileBuf.size()).toUtf8();
        QJsonObject e = termScrollbackEntry(QStringLiteral("info"), info);
        termAppendScrollback(s, e);
        termBroadcastData(s->sid, e);

        QJsonObject notify;
        notify["type"]   = "termFile";
        notify["sid"]    = s->sid;
        notify["name"]   = y.filename;
        notify["bytes"]  = (qint64)y.fileBuf.size();
        notify["dataB64"] = QString::fromLatin1(y.fileBuf.toBase64());
        sendJsonToAll(notify);

        xferBroadcastEnd(s, /*aborted=*/false);
        s->xfer = XferState{};

        qInfo().noquote() << "Web: YAPP RX complete" << s->sid
                          << y.filename << y.fileBuf.size() << "B";

        y.filename.clear();
        y.fileBuf.clear();
        y.filesize = 0;
        y.active = false;
        break;
    }
    default:
        qWarning().noquote() << "Web: YAPP unknown type" << QString(QChar((int)type))
                              << "len=" << data.size();
        break;
    }
}

void webServer::yappSendFile(TerminalSession *s, const QString &name,
                             const QByteArray &data)
{
    if (!axProc || s->state != TerminalSession::Connected) return;
    if (s->xfer.active) return;     // already transferring

    // Classic YAPP handshake: announce intent with SI, wait for RR before
    // any file bytes hit the air.  The payload is a nul-terminated sender
    // identifier — we use the operator's callsign.
    QByteArray si;
    si.append(s->ownCall.isEmpty() ? QByteArray("wfweb") : s->ownCall.toUtf8());
    si.append('\0');
    QByteArray siFrame = yappFrame('S', si);
    QMetaObject::invokeMethod(axProc, "sendData", Qt::QueuedConnection,
                              Q_ARG(int, TERM_FIXED_CLIENT), Q_ARG(int, s->chan),
                              Q_ARG(QString, s->ownCall), Q_ARG(QString, s->peerCall),
                              Q_ARG(QStringList, s->digis), Q_ARG(int, 0xF0),
                              Q_ARG(QByteArray, siFrame));

    s->xfer.active       = true;
    s->xfer.dir          = QStringLiteral("tx");
    s->xfer.name         = name;
    s->xfer.total        = data.size();
    s->xfer.done         = 0;
    s->xfer.abortPending = false;
    s->xfer.phase        = QStringLiteral("awaiting_rr");
    s->xfer.pendingData  = data;
    xferBroadcastStart(s);

    QByteArray info = QString("*** Requesting to send %1 (%2 bytes) — awaiting peer acceptance...")
        .arg(name).arg(data.size()).toUtf8();
    QJsonObject e = termScrollbackEntry(QStringLiteral("info"), info);
    termAppendScrollback(s, e);
    termBroadcastData(s->sid, e);
    qInfo().noquote() << "Web: YAPP SI sent" << s->sid
                      << name << data.size() << "B — awaiting RR";

    // Safety net: if the peer never replies within 60 s, tear down.
    QString sid = s->sid;
    QTimer::singleShot(60000, this, [this, sid]() {
        TerminalSession *cur = termSessions.value(sid, nullptr);
        if (!cur || !cur->xfer.active) return;
        if (cur->xfer.phase != "awaiting_rr") return;
        QByteArray info = QStringLiteral("*** Transfer timed out — peer did not respond").toUtf8();
        QJsonObject e = termScrollbackEntry(QStringLiteral("info"), info);
        termAppendScrollback(cur, e);
        termBroadcastData(cur->sid, e);
        xferBroadcastEnd(cur, /*aborted=*/true);
        cur->xfer = XferState{};
    });
}

void webServer::yappSendRR(TerminalSession *s)
{
    if (!axProc) return;
    QByteArray rrFrame = yappFrame('R', QByteArray());
    QMetaObject::invokeMethod(axProc, "sendData", Qt::QueuedConnection,
                              Q_ARG(int, TERM_FIXED_CLIENT), Q_ARG(int, s->chan),
                              Q_ARG(QString, s->ownCall), Q_ARG(QString, s->peerCall),
                              Q_ARG(QStringList, s->digis), Q_ARG(int, 0xF0),
                              Q_ARG(QByteArray, rrFrame));
}

void webServer::yappSendAB(TerminalSession *s)
{
    if (!axProc) return;
    QByteArray abFrame = yappFrame('A', QByteArray());
    QMetaObject::invokeMethod(axProc, "sendData", Qt::QueuedConnection,
                              Q_ARG(int, TERM_FIXED_CLIENT), Q_ARG(int, s->chan),
                              Q_ARG(QString, s->ownCall), Q_ARG(QString, s->peerCall),
                              Q_ARG(QStringList, s->digis), Q_ARG(int, 0xF0),
                              Q_ARG(QByteArray, abFrame));
}

void webServer::yappStartDataPhase(TerminalSession *s)
{
    s->xfer.phase = QStringLiteral("active");

    // Build the complete YAPP frame list up front: HD + DTs + EF.
    // Frames are then pumped out one at a time via onPacketTxReady so
    // at most one frame ever sits in ax25_link's I-frame queue.
    QByteArray payload = s->xfer.pendingData;
    s->xfer.pendingData.clear();

    s->xfer.pendingYappFrames.clear();
    QByteArray hd;
    hd.append(s->xfer.name.toUtf8());
    hd.append('\0');
    hd.append(QString::number(s->xfer.total).toLatin1());
    hd.append('\0');
    s->xfer.pendingYappFrames.append(yappFrame('H', hd));

    const int kDataChunk = 100;     // <= paclen=128 after 3-byte YAPP header
    for (int off = 0; off < payload.size(); off += kDataChunk) {
        s->xfer.pendingYappFrames.append(yappFrame('D', payload.mid(off, kDataChunk)));
    }
    s->xfer.pendingYappFrames.append(yappFrame('E', QByteArray()));
    s->xfer.framesPlanned = s->xfer.pendingYappFrames.size();
    s->xfer.framesSent = 0;

    QByteArray info = QString("*** Peer accepted — sending %1 (%2 bytes, %3 frames)")
        .arg(s->xfer.name).arg(s->xfer.total).arg(s->xfer.framesPlanned).toUtf8();
    QJsonObject e = termScrollbackEntry(QStringLiteral("info"), info);
    termAppendScrollback(s, e);
    termBroadcastData(s->sid, e);

    // Re-broadcast xferStart so the UI transitions from "awaiting" to "active".
    xferBroadcastStart(s);
    qInfo().noquote() << "Web: YAPP TX start (post-RR)" << s->sid
                      << s->xfer.name << s->xfer.total << "B"
                      << s->xfer.framesPlanned << "frames";

    // Queue the first frame; subsequent frames are pumped from
    // onPacketTxReady once the previous one's audio is ready.
    yappPumpNextFrame(s);
}

void webServer::yappPumpNextFrame(TerminalSession *s)
{
    if (!axProc || !s->xfer.active || s->xfer.dir != "tx") return;

    // Abort interrupts the pump.  AB jumps the queue: we clear any
    // remaining YAPP frames that haven't been handed to ax25_link yet,
    // then send AB as the very next frame.
    if (s->xfer.abortPending) {
        s->xfer.pendingYappFrames.clear();
        QByteArray abFrame = yappFrame('A', QByteArray());
        QMetaObject::invokeMethod(axProc, "sendData", Qt::QueuedConnection,
            Q_ARG(int, TERM_FIXED_CLIENT), Q_ARG(int, s->chan),
            Q_ARG(QString, s->ownCall), Q_ARG(QString, s->peerCall),
            Q_ARG(QStringList, s->digis), Q_ARG(int, 0xF0),
            Q_ARG(QByteArray, abFrame));
        QByteArray info = QStringLiteral("*** Transfer aborted").toUtf8();
        QJsonObject e = termScrollbackEntry(QStringLiteral("info"), info);
        termAppendScrollback(s, e);
        termBroadcastData(s->sid, e);
        xferBroadcastEnd(s, /*aborted=*/true);
        s->xfer = XferState{};
        return;
    }

    if (s->xfer.pendingYappFrames.isEmpty()) return;

    QByteArray frame = s->xfer.pendingYappFrames.takeFirst();
    QMetaObject::invokeMethod(axProc, "sendData", Qt::QueuedConnection,
        Q_ARG(int, TERM_FIXED_CLIENT), Q_ARG(int, s->chan),
        Q_ARG(QString, s->ownCall), Q_ARG(QString, s->peerCall),
        Q_ARG(QStringList, s->digis), Q_ARG(int, 0xF0),
        Q_ARG(QByteArray, frame));
}

webServer::TerminalSession *webServer::yappFindActiveTxXfer()
{
    for (TerminalSession *s : termSessions) {
        if (s->xfer.active && s->xfer.dir == "tx"
            && s->xfer.phase == "active") return s;
    }
    return nullptr;
}

void webServer::yappAbortSend(TerminalSession *s)
{
    if (!s || !s->xfer.active) return;
    if (s->xfer.dir == "tx") {
        // Flagged: the timer will send AB and tear down next tick.
        s->xfer.abortPending = true;
    } else {
        // RX-side abort: send AB and clear our state.
        QByteArray abFrame = yappFrame('A', QByteArray());
        if (axProc) {
            QMetaObject::invokeMethod(axProc, "sendData", Qt::QueuedConnection,
                Q_ARG(int, TERM_FIXED_CLIENT), Q_ARG(int, s->chan),
                Q_ARG(QString, s->ownCall), Q_ARG(QString, s->peerCall),
                Q_ARG(QStringList, s->digis), Q_ARG(int, 0xF0),
                Q_ARG(QByteArray, abFrame));
        }
        QByteArray info = QStringLiteral("*** Transfer aborted").toUtf8();
        QJsonObject e = termScrollbackEntry(QStringLiteral("info"), info);
        termAppendScrollback(s, e);
        termBroadcastData(s->sid, e);
        xferBroadcastEnd(s, /*aborted=*/true);
        s->xfer = XferState{};
        s->yapp.active = false;
        s->yapp.fileBuf.clear();
        s->yapp.filename.clear();
        s->yapp.filesize = 0;
    }
}
#endif

void webServer::onRxConverted(audioPacket audio)
{
#ifdef PACKET_SUPPORT
    // Fan decoded PCM out to the packet processor. Independent of audio clients:
    // decoding can happen even when no browser is listening to audio.
    if (packetEnabled) {
        emit sendToPacketRx(audio);
    }
#endif

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
    if (sampleRate > 0) {
        rigSampleRate = sampleRate;
    }

    if (audioConfigured) {
        return;
    }

#ifdef Q_OS_WIN
    // Ensure COM is initialized on this thread (required for Windows audio APIs).
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
#endif

    // Find the rig's USB audio capture device.
    // With PulseAudio the IC-7300 shows as "USB Audio CODEC" (matches "USB").
    // With the ALSA backend (used in offscreen/headless mode) it shows as
    // "hw:CARD=CODEC,DEV=0" — no "USB" in the name, so we also match "CODEC".
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QAudioDeviceInfo usbDevice;
    bool found = false;
    qInfo() << "Web: Available audio input devices:";
    for (const QAudioDeviceInfo &dev : QAudioDeviceInfo::availableDevices(QAudio::AudioInput)) {
        qInfo() << "Web:  " << dev.deviceName();
        if (!found && (dev.deviceName().contains("USB", Qt::CaseInsensitive) ||
                       dev.deviceName().contains("CODEC", Qt::CaseInsensitive))) {
            usbDevice = dev;
            found = true;
            qInfo() << "Web: Selected rig audio device:" << dev.deviceName();
        }
    }
    if (!found) {
        qWarning() << "Web: No rig audio device found for direct capture";
        audioErrorReason = "No compatible audio device found.";
        if (!wsClients.isEmpty()) {
            QJsonObject err;
            err["type"] = "audioError";
            err["reason"] = audioErrorReason;
            sendJsonToAll(err);
        }
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

#ifdef Q_OS_WIN
    // Monitor state changes (Windows audio diagnostics)
    connect(usbAudioInput, &QAudioInput::stateChanged, this, &webServer::onAudioStateChanged);
#endif

#else
    QAudioDevice usbDevice;
    bool found = false;
    QList<QAudioDevice> inputDevices = QMediaDevices::audioInputs();
    for (const QAudioDevice &dev : inputDevices) {
        qInfo() << "Web:  " << dev.description();
        if (!found && (dev.description().contains("USB", Qt::CaseInsensitive) ||
                       dev.description().contains("CODEC", Qt::CaseInsensitive))) {
            usbDevice = dev;
            found = true;
            qInfo() << "Web: Selected rig audio device:" << dev.description();
        }
    }
    if (!found) {
        qWarning() << "Web: No rig audio device found for direct capture";
        audioErrorReason = "No compatible audio device found.";
        if (!wsClients.isEmpty()) {
            QJsonObject err;
            err["type"] = "audioError";
            err["reason"] = audioErrorReason;
            sendJsonToAll(err);
        }
        return;
    }

    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    usbAudioInput = new QAudioSource(usbDevice, format, this);
#ifdef Q_OS_WIN
    connect(usbAudioInput, &QAudioSource::stateChanged, this, &webServer::onAudioStateChanged);
#endif
#endif

    rigSampleRate = format.sampleRate();

    usbAudioDevice = usbAudioInput->start();
    if (!usbAudioDevice) {
        qWarning() << "Web: Failed to start USB audio capture";
        delete usbAudioInput;
        usbAudioInput = nullptr;
        audioErrorReason = "Found audio device but failed to open it. It may be in use by another application.";
        if (!wsClients.isEmpty()) {
            QJsonObject err;
            err["type"] = "audioError";
            err["reason"] = audioErrorReason;
            sendJsonToAll(err);
        }
        return;
    }

    connect(usbAudioDevice, &QIODevice::readyRead, this, &webServer::readUsbAudio);

    // readyRead is not reliably emitted for QAudioInput in pull mode on
    // Windows and ALSA backends.  Use a poll timer as a fallback.
    usbAudioPollTimer = new QTimer(this);
    connect(usbAudioPollTimer, &QTimer::timeout, this, &webServer::readUsbAudio);
    usbAudioPollTimer->start(20);  // 20ms = 960 samples at 48kHz

    audioConfigured = true;

#ifdef FREEDV_SUPPORT
    // FreeDV processor thread (created once, activated on demand)
    if (!freedvProcessor) {
        freedvProcessor = new FreeDVProcessor();
        freedvThread = new QThread(this);
        freedvThread->setObjectName("webFreeDV()");
        freedvProcessor->moveToThread(freedvThread);

        connect(this, &webServer::setupFreeDV, freedvProcessor, &FreeDVProcessor::init);
        connect(this, &webServer::sendToFreeDVRx, freedvProcessor, &FreeDVProcessor::processRx);
        connect(this, &webServer::sendToFreeDVTx, freedvProcessor, &FreeDVProcessor::processTx);
        connect(freedvProcessor, &FreeDVProcessor::rxReady, this, &webServer::onFreeDVRxReady);
        connect(freedvProcessor, &FreeDVProcessor::txReady, this, &webServer::onFreeDVTxReady);
        connect(freedvProcessor, &FreeDVProcessor::statsUpdate, this, &webServer::onFreeDVStats);
        connect(freedvProcessor, &FreeDVProcessor::rxCallsign, this, &webServer::onFreeDVRxCallsign);
        connect(freedvThread, &QThread::finished, freedvProcessor, &QObject::deleteLater);

        freedvThread->start();
    }
#endif

#ifdef RADE_SUPPORT
    // RADE V1 processor thread (created once, activated on demand)
    if (!radeProcessor) {
        radeProcessor = new RadeProcessor();
        radeThread = new QThread(this);
        radeThread->setObjectName("webRADE()");
        radeProcessor->moveToThread(radeThread);

        connect(this, &webServer::setupRade, radeProcessor, &RadeProcessor::init);
        connect(this, &webServer::sendToRadeRx, radeProcessor, &RadeProcessor::processRx);
        connect(this, &webServer::sendToRadeTx, radeProcessor, &RadeProcessor::processTx);
        connect(radeProcessor, &RadeProcessor::rxReady, this, &webServer::onRadeRxReady);
        connect(radeProcessor, &RadeProcessor::txReady, this, &webServer::onRadeTxReady);
        connect(radeProcessor, &RadeProcessor::statsUpdate, this, &webServer::onRadeStats);
        connect(radeProcessor, &RadeProcessor::rxCallsign, this, &webServer::onRadeRxCallsign);
        connect(radeThread, &QThread::finished, radeProcessor, &QObject::deleteLater);

        radeThread->start();
    }
#endif

#ifdef PACKET_SUPPORT
    // Dire Wolf packet processor (mirrors the LAN path in setupAudio()).
    if (!dwProc) {
        dwProc = new DireWolfProcessor();
        dwThread = new QThread(this);
        dwThread->setObjectName("webPacket()");
        dwProc->moveToThread(dwThread);

        connect(this, &webServer::setupPacket, dwProc, &DireWolfProcessor::init);
        connect(this, &webServer::sendToPacketRx, dwProc, &DireWolfProcessor::processRx);
        connect(dwProc, &DireWolfProcessor::rxFrameDecoded, this, &webServer::onPacketRxDecoded);
        connect(dwProc, &DireWolfProcessor::txReady, this, &webServer::onPacketTxReady);
        connect(dwProc, &DireWolfProcessor::txFailed, this, &webServer::onPacketTxFailed);
        connect(dwProc, &DireWolfProcessor::captureComplete, this,
                [this](QString path, int rate, int samples) {
                    QJsonObject notify;
                    notify["type"] = "packetCaptureComplete";
                    notify["path"] = path;
                    notify["sampleRate"] = rate;
                    notify["samples"] = samples;
                    sendJsonToAll(notify);
                });
        connect(dwProc, &DireWolfProcessor::captureFailed, this,
                [this](QString reason) {
                    QJsonObject notify;
                    notify["type"] = "packetCaptureFailed";
                    notify["reason"] = reason;
                    sendJsonToAll(notify);
                });
        connect(dwThread, &QThread::finished, dwProc, &QObject::deleteLater);

        dwThread->start();
        emit setupPacket(rigSampleRate);
    }

    if (!axProc) {
        axProc = new AX25LinkProcessor(this);
        connect(axProc, &AX25LinkProcessor::transmitFrameBytes,
                dwProc, &DireWolfProcessor::transmitFrameBytes,
                Qt::QueuedConnection);
        connect(axProc, &AX25LinkProcessor::linkEstablished,
                this,   &webServer::onAxLinkEstablished,
                Qt::QueuedConnection);
        connect(axProc, &AX25LinkProcessor::linkTerminated,
                this,   &webServer::onAxLinkTerminated,
                Qt::QueuedConnection);
        connect(axProc, &AX25LinkProcessor::rxData,
                this,   &webServer::onAxRxData,
                Qt::QueuedConnection);
        axProc->start();

        // Load persisted enable/mode state, then apply it to the processors
        // and broadcast a packetStatus so the UI picks up the restored value.
        packetLoadSettings();
        QMetaObject::invokeMethod(axProc, "setLinkParamsForBaud", Qt::QueuedConnection,
                                  Q_ARG(int, packetMode));
        QMetaObject::invokeMethod(dwProc, "setMode", Qt::QueuedConnection,
                                  Q_ARG(int, packetMode));
        if (packetEnabled) {
            QMetaObject::invokeMethod(dwProc, "setEnabled", Qt::QueuedConnection,
                                      Q_ARG(bool, true));
        }
    }
#endif

    qInfo() << "Web: USB audio capture configured, sampleRate=" << rigSampleRate
            << "channels=" << format.channelCount();

    // Also configure USB audio output (playback) for TX audio
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QAudioDeviceInfo usbOutDevice;
    bool outFound = false;
    for (const QAudioDeviceInfo &dev : QAudioDeviceInfo::availableDevices(QAudio::AudioOutput)) {
        if (dev.deviceName().contains("USB", Qt::CaseInsensitive) ||
            dev.deviceName().contains("CODEC", Qt::CaseInsensitive)) {
            usbOutDevice = dev;
            outFound = true;
            qInfo() << "Web: Found rig audio output device:" << dev.deviceName();
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
        // Device starts stopped; it is started fresh on each enableMic=true via pre-buffer.
        txAudioConfigured = true;
        qInfo() << "Web: USB audio output configured for TX, channels=" << usbOutputChannels;
    }
#else
    QAudioDevice usbOutDevice;
    bool outFound = false;
    for (const QAudioDevice &dev : QMediaDevices::audioOutputs()) {
        if (dev.description().contains("USB", Qt::CaseInsensitive) ||
            dev.description().contains("CODEC", Qt::CaseInsensitive)) {
            usbOutDevice = dev;
            outFound = true;
            qInfo() << "Web: Found rig audio output device:" << dev.description();
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

void webServer::onAudioStateChanged(QAudio::State state)
{
#ifdef Q_OS_WIN
    if (state == QAudio::StoppedState && usbAudioInput && usbAudioInput->error() != QAudio::NoError) {
        qWarning() << "Web: Audio stopped with error, attempting restart...";
        QTimer::singleShot(1000, this, [this]() {
            if (usbAudioInput && usbAudioInput->state() == QAudio::StoppedState) {
                usbAudioDevice = usbAudioInput->start();
                if (usbAudioDevice) {
                    connect(usbAudioDevice, &QIODevice::readyRead, this, &webServer::readUsbAudio);
                    qInfo() << "Web: Audio restart successful";
                } else {
                    qWarning() << "Web: Audio restart failed";
                }
            }
        });
    }
#else
    Q_UNUSED(state)
#endif
}

void webServer::readUsbAudio()
{
    if (!usbAudioDevice) return;
    // Forward whenever *any* consumer cares: browser audio clients, an
    // active packet modem, or anything we need to drain so the input
    // buffer doesn't pile up.
    bool haveConsumer = !audioClients.isEmpty();
#ifdef PACKET_SUPPORT
    haveConsumer = haveConsumer || packetEnabled;
#endif
    if (!haveConsumer) {
        // Still drain so the input buffer stays healthy.
        usbAudioDevice->readAll();
        return;
    }

    QByteArray data = usbAudioDevice->readAll();
    if (data.isEmpty()) return;

    // If stereo, mix down to mono (average L+R)
    int channels = usbAudioInput ? usbAudioInput->format().channelCount() : 1;
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

    // FreeDV RX path: decode modem tones before sending to browser.
    // This also works during TX when the radio's MONITOR is on,
    // providing a loopback test of the full TX/RX chain.
    // When freedvMonitor is set, bypass decoding to hear raw SSB.
    if (freedvEnabled && !freedvMonitor) {
        audioPacket pkt;
        pkt.data = data;
        pkt.time = QTime::currentTime();
        pkt.seq = audioSeq++;
        pkt.sent = 0;
        pkt.volume = 1.0;
#ifdef RADE_SUPPORT
        if (freedvModeName == "RADE")
            emit sendToRadeRx(pkt);
        else
#endif
#ifdef FREEDV_SUPPORT
            emit sendToFreeDVRx(pkt);
#else
        {}  // no codec available
#endif
        return;
    }

#ifdef PACKET_SUPPORT
    // Fan out to the Dire Wolf demod.  The LAN path feeds packet via the
    // audioConverter (onRxConverted); USB is already PCM Int16 at
    // rigSampleRate, so send it straight in.
    if (packetEnabled) {
        audioPacket pkt;
        pkt.data = data;
        pkt.time = QTime::currentTime();
        pkt.seq = audioSeq;
        pkt.sent = 0;
        pkt.volume = 1.0;
        emit sendToPacketRx(pkt);
    }
#endif

    if (audioClients.isEmpty()) return;

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
