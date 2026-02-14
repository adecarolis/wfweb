#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QMimeDatabase>
#include <QtWebSockets/QWebSocketServer>
#include <QtWebSockets/QWebSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>

#include "cachingqueue.h"

class webServer : public QObject
{
    Q_OBJECT

public:
    explicit webServer(QObject *parent = nullptr);
    ~webServer();

signals:
    void closed();

public slots:
    void init(quint16 httpPort, quint16 wsPort);
    void receiveRigCaps(rigCapabilities* caps);

private slots:
    // HTTP
    void onHttpConnection();
    void onHttpReadyRead();
    void onHttpDisconnected();

    // WebSocket
    void onWsNewConnection();
    void onWsTextMessage(QString message);
    void onWsDisconnected();

    // Cache
    void receiveCache(cacheItem item);

    // Periodic status push
    void sendPeriodicStatus();

private:
    void serveStaticFile(QTcpSocket *socket, const QString &path);
    void sendHttpResponse(QTcpSocket *socket, int statusCode, const QString &statusText,
                         const QByteArray &contentType, const QByteArray &body);
    void sendJsonToAll(const QJsonObject &obj);
    void sendJsonTo(QWebSocket *client, const QJsonObject &obj);
    void sendBinaryToAll(const QByteArray &data);
    void handleCommand(QWebSocket *client, const QJsonObject &cmd);
    void sendCurrentState(QWebSocket *client);
    QString modeToString(modeInfo m);
    modeInfo stringToMode(const QString &mode);
    QJsonObject buildStatusJson();

    QTcpServer *httpServer = nullptr;
    QWebSocketServer *wsServer = nullptr;
    QList<QWebSocket *> wsClients;
    cachingQueue *queue = nullptr;
    rigCapabilities *rigCaps = nullptr;
    QTimer *statusTimer = nullptr;
    QMimeDatabase mimeDb;
};

#endif // WEBSERVER_H
