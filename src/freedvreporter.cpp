#include "freedvreporter.h"
#include "logcategories.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QUrl>

FreeDVReporter::FreeDVReporter(QObject *parent)
    : SpotReporter(parent)
{
    socket_ = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(socket_, &QWebSocket::connected, this, &FreeDVReporter::onWsConnected);
    connect(socket_, &QWebSocket::disconnected, this, &FreeDVReporter::onWsDisconnected);
    connect(socket_, &QWebSocket::textMessageReceived, this, &FreeDVReporter::onWsTextMessage);
    connect(socket_, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this, &FreeDVReporter::onWsError);

    pingTimer_ = new QTimer(this);
    pingTimer_->setSingleShot(true);
    connect(pingTimer_, &QTimer::timeout, this, &FreeDVReporter::onPingTimeout);

    reconnectTimer_ = new QTimer(this);
    reconnectTimer_->setSingleShot(true);
    connect(reconnectTimer_, &QTimer::timeout, this, &FreeDVReporter::onReconnectTimer);

    freqDebounceTimer_ = new QTimer(this);
    freqDebounceTimer_->setSingleShot(true);
    freqDebounceTimer_->setInterval(500);
    connect(freqDebounceTimer_, &QTimer::timeout, this, &FreeDVReporter::onFreqDebounce);
}

FreeDVReporter::~FreeDVReporter()
{
    disconnectFromService();
}

void FreeDVReporter::setStation(const QString &callsign, const QString &grid)
{
    bool changed = (callsign_ != callsign || grid_ != grid);
    callsign_ = callsign;
    grid_ = grid;

    // If connected with different credentials, reconnect
    if (changed && state_ == Connected) {
        disconnectFromService();
        connectToService();
    }
}

void FreeDVReporter::connectToService()
{
    if (callsign_.isEmpty() || grid_.isEmpty()) {
        qWarning() << "FreeDVReporter: cannot connect without callsign and grid";
        return;
    }
    if (state_ == Connected || state_ == Connecting)
        return;

    intentionalDisconnect_ = false;
    reconnectDelay_ = 2000;
    fullyConnected_ = false;
    setState(Connecting);

    QUrl url;
    url.setScheme("ws");
    url.setHost(DEFAULT_HOST);
    url.setPort(DEFAULT_PORT);
    url.setPath("/socket.io/");
    url.setQuery("EIO=4&transport=websocket");

    qInfo() << "FreeDVReporter: connecting to" << url.toString();
    socket_->open(url);
}

void FreeDVReporter::disconnectFromService()
{
    intentionalDisconnect_ = true;
    fullyConnected_ = false;
    reconnectTimer_->stop();
    pingTimer_->stop();
    freqDebounceTimer_->stop();

    if (socket_->state() != QAbstractSocket::UnconnectedState) {
        socket_->close();
    }
    setState(Disconnected);
}

void FreeDVReporter::updateFrequency(quint64 hz)
{
    pendingFreq_ = hz;
    if (!freqDebounceTimer_->isActive())
        freqDebounceTimer_->start();
}

void FreeDVReporter::updateTx(const QString &mode, bool transmitting)
{
    lastMode_ = mode;
    lastTx_ = transmitting;

    if (!fullyConnected_) return;

    QJsonObject data;
    data["mode"] = reporterModeName(mode);
    data["transmitting"] = transmitting;
    sioEmit("tx_report", data);
}

void FreeDVReporter::updateRxSpot(const QString &callsign, const QString &mode, int snr)
{
    if (!fullyConnected_) {
        qInfo() << "FreeDVReporter: rx_report skipped (not connected), callsign=" << callsign;
        return;
    }

    QJsonObject data;
    data["callsign"] = callsign;
    data["mode"] = reporterModeName(mode);
    data["snr"] = snr;
    sioEmit("rx_report", data);
    qInfo() << "FreeDVReporter: sent rx_report callsign=" << callsign
            << "mode=" << reporterModeName(mode) << "snr=" << snr;
}

QString FreeDVReporter::reporterModeName(const QString &mode)
{
    if (mode == "RADE")  return QStringLiteral("RADEV1");
    // Standard FreeDV modes: "700D" -> "FreeDV 700D"
    return QString("FreeDV %1").arg(mode);
}

void FreeDVReporter::setMessage(const QString &message)
{
    lastMessage_ = message;
    if (!fullyConnected_) return;

    QJsonObject data;
    data["message"] = message;
    sioEmit("message_update", data);
}

// --- Socket.IO framing ---

void FreeDVReporter::onWsConnected()
{
    qInfo() << "FreeDVReporter: WebSocket connected, awaiting Engine.IO open";
    // Wait for Engine.IO open packet (type 0)
}

void FreeDVReporter::onWsDisconnected()
{
    pingTimer_->stop();
    fullyConnected_ = false;

    if (!intentionalDisconnect_) {
        qWarning() << "FreeDVReporter: disconnected unexpectedly";
        setState(Error);
        scheduleReconnect();
    } else {
        setState(Disconnected);
    }
}

void FreeDVReporter::onWsTextMessage(const QString &message)
{
    if (message.isEmpty()) return;

    QChar type = message.at(0);

    switch (type.toLatin1()) {
    case '0': {
        // Engine.IO open: 0{"sid":"...","pingInterval":5000,"pingTimeout":5000,...}
        QJsonDocument doc = QJsonDocument::fromJson(message.mid(1).toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            int pingInterval = obj["pingInterval"].toInt(5000);
            pingTimeout_ = obj["pingTimeout"].toInt(20000);
            Q_UNUSED(pingInterval);
            // Start ping watchdog — server should ping within pingInterval + pingTimeout
            pingTimer_->setInterval(pingTimeout_ + 5000);
            pingTimer_->start();
        }

        // Send Socket.IO namespace connect with auth
        QJsonObject auth;
        auth["role"] = "report";
        auth["callsign"] = callsign_;
        auth["grid_square"] = grid_;
        auth["version"] = QString("wfweb %1").arg(
            QStringLiteral(WFWEB_VERSION));
        auth["rx_only"] = false;
#ifdef Q_OS_LINUX
        auth["os"] = "linux";
#elif defined(Q_OS_MACOS)
        auth["os"] = "macOS";
#elif defined(Q_OS_WIN)
        auth["os"] = "windows";
#else
        auth["os"] = "unknown";
#endif
        auth["protocol_version"] = PROTOCOL_VERSION;

        QString connectMsg = "40" + QString::fromUtf8(
            QJsonDocument(auth).toJson(QJsonDocument::Compact));
        socket_->sendTextMessage(connectMsg);
        qInfo() << "FreeDVReporter: sent auth for" << callsign_;
        break;
    }
    case '2':
        // Engine.IO ping — respond with pong
        socket_->sendTextMessage("3");
        pingTimer_->start(); // reset watchdog
        break;
    case '3':
        // Engine.IO pong (shouldn't happen, but reset watchdog anyway)
        pingTimer_->start();
        break;
    case '4': {
        // Socket.IO packet
        if (message.length() < 2) break;
        QChar sioType = message.at(1);

        if (sioType == '0') {
            // Socket.IO connect ACK: 40{"sid":"..."}
            qInfo() << "FreeDVReporter: namespace connected";
            // The server will send events now; connection_successful comes as a 42 event
        }
        else if (sioType == '2') {
            // Socket.IO event: 42["event_name",{data}]
            QString payload = message.mid(2);
            QJsonDocument doc = QJsonDocument::fromJson(payload.toUtf8());
            if (doc.isArray()) {
                QJsonArray arr = doc.array();
                QString event = arr.at(0).toString();

                if (event == "connection_successful") {
                    qInfo() << "FreeDVReporter: connection successful, reporting as"
                            << callsign_ << grid_;
                    fullyConnected_ = true;
                    reconnectDelay_ = 2000; // reset backoff on success
                    setState(Connected);
                    resendState();
                }
                // Other events (new_connection, freq_change, etc.) ignored for now
            }
        }
        break;
    }
    case '1':
        // Engine.IO close
        qInfo() << "FreeDVReporter: server closed connection";
        socket_->close();
        break;
    default:
        break;
    }
}

void FreeDVReporter::onWsError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    qWarning() << "FreeDVReporter: WebSocket error:" << socket_->errorString();
    if (state_ != Disconnected) {
        setState(Error);
        scheduleReconnect();
    }
}

void FreeDVReporter::onPingTimeout()
{
    qWarning() << "FreeDVReporter: ping timeout, reconnecting";
    fullyConnected_ = false;
    socket_->close();
    // onWsDisconnected will trigger reconnect
}

void FreeDVReporter::onReconnectTimer()
{
    if (intentionalDisconnect_) return;
    qInfo() << "FreeDVReporter: attempting reconnect (delay was" << reconnectDelay_ << "ms)";
    connectToService();
}

void FreeDVReporter::onFreqDebounce()
{
    if (pendingFreq_ == lastFreq_)
        return;

    lastFreq_ = pendingFreq_;

    if (!fullyConnected_) return;

    QJsonObject data;
    data["freq"] = (qint64)lastFreq_;
    sioEmit("freq_change", data);
}

void FreeDVReporter::setState(State s)
{
    if (state_ != s) {
        state_ = s;
        emit stateChanged(s);
    }
}

void FreeDVReporter::sioEmit(const QString &event, const QJsonObject &data)
{
    if (socket_->state() != QAbstractSocket::ConnectedState) return;

    QJsonArray arr;
    arr.append(event);
    arr.append(data);
    QString msg = "42" + QString::fromUtf8(
        QJsonDocument(arr).toJson(QJsonDocument::Compact));
    socket_->sendTextMessage(msg);
}

void FreeDVReporter::sioEmitNoPayload(const QString &event)
{
    if (socket_->state() != QAbstractSocket::ConnectedState) return;

    QJsonArray arr;
    arr.append(event);
    arr.append(QJsonObject());
    QString msg = "42" + QString::fromUtf8(
        QJsonDocument(arr).toJson(QJsonDocument::Compact));
    socket_->sendTextMessage(msg);
}

void FreeDVReporter::resendState()
{
    // After connection_successful, re-send cached state
    if (lastFreq_ > 0) {
        QJsonObject data;
        data["freq"] = (qint64)lastFreq_;
        sioEmit("freq_change", data);
    }

    if (!lastMode_.isEmpty()) {
        QJsonObject data;
        data["mode"] = reporterModeName(lastMode_);
        data["transmitting"] = lastTx_;
        sioEmit("tx_report", data);
    }

    if (!lastMessage_.isEmpty()) {
        QJsonObject data;
        data["message"] = lastMessage_;
        sioEmit("message_update", data);
    }
}

void FreeDVReporter::scheduleReconnect()
{
    if (intentionalDisconnect_) return;

    reconnectTimer_->start(reconnectDelay_);
    // Exponential backoff capped at 60s
    reconnectDelay_ = qMin(reconnectDelay_ * 2, 60000);
}
