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
#include <QSet>
#include <QThread>
#include <QSslSocket>
#include <QSslKey>
#include <QSslCertificate>

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
#include <QAudioDeviceInfo>
#include <QAudioInput>
#include <QAudioOutput>
#else
#include <QMediaDevices>
#include <QAudioDevice>
#include <QAudioSource>
#include <QAudioSink>
#endif

#include "cachingqueue.h"
#include "audioconverter.h"

// SSL-enabled TCP server for HTTPS
class SslTcpServer : public QTcpServer {
public:
    QSslCertificate cert;
    QSslKey key;
    explicit SslTcpServer(QObject *parent = nullptr) : QTcpServer(parent) {}
protected:
    void incomingConnection(qintptr handle) override {
        QSslSocket *socket = new QSslSocket(this);
        if (socket->setSocketDescriptor(handle)) {
            socket->setLocalCertificate(cert);
            socket->setPrivateKey(key);
            socket->startServerEncryption();
            addPendingConnection(socket);
        } else {
            delete socket;
        }
    }
};

class webServer : public QObject
{
    Q_OBJECT

public:
    explicit webServer(QObject *parent = nullptr);
    ~webServer();

signals:
    void closed();
    void setupConverter(QAudioFormat inFormat, codecType inCodec,
                        QAudioFormat outFormat, codecType outCodec,
                        quint8 opusComplexity, quint8 resampleQuality);
    void sendToConverter(audioPacket audio);
    void setupTxConverter(QAudioFormat inFormat, codecType inCodec,
                          QAudioFormat outFormat, codecType outCodec,
                          quint8 opusComplexity, quint8 resampleQuality);
    void sendToTxConverter(audioPacket audio);
    void haveTxAudioData(audioPacket audio);

public slots:
    void init(quint16 httpPort, quint16 wsPort);
    void receiveRigCaps(rigCapabilities* caps);
    void receiveRxAudio(audioPacket audio);
    void setupAudio(quint8 codec, quint32 sampleRate);
    void setupUsbAudio(quint32 sampleRate);

private slots:
    // HTTP
    void onHttpConnection();
    void onHttpReadyRead();
    void onHttpDisconnected();

    // WebSocket
    void onWsNewConnection();
    void onWsTextMessage(QString message);
    void onWsBinaryMessage(QByteArray message);
    void onWsDisconnected();

    // Cache
    void receiveCache(cacheItem item);

    // Periodic status push
    void sendPeriodicStatus();

    // Audio converter output
    void onRxConverted(audioPacket audio);
    void onTxConverted(audioPacket audio);

    // USB audio capture
    void readUsbAudio();

private:
    void serveStaticFile(QTcpSocket *socket, const QString &path);
    void sendHttpResponse(QTcpSocket *socket, int statusCode, const QString &statusText,
                         const QByteArray &contentType, const QByteArray &body);
    void sendJsonToAll(const QJsonObject &obj);
    void sendJsonTo(QWebSocket *client, const QJsonObject &obj);
    void sendBinaryToAll(const QByteArray &data);
    void sendBinaryToAudioClients(const QByteArray &data);
    void handleCommand(QWebSocket *client, const QJsonObject &cmd);
    void requestVfoUpdate();
    void sendCurrentState(QWebSocket *client);
    QString modeToString(modeInfo m);
    modeInfo stringToMode(const QString &mode);
    QJsonObject buildStatusJson();
    codecType codecByteToType(quint8 codec);
    bool setupSsl();

    QTcpServer *httpServer = nullptr;
    QWebSocketServer *wsServer = nullptr;
    QList<QWebSocket *> wsClients;
    cachingQueue *queue = nullptr;
    rigCapabilities *rigCaps = nullptr;
    QTimer *statusTimer = nullptr;
    QMimeDatabase mimeDb;

    // SSL
    bool sslEnabled = false;
    QSslCertificate sslCert;
    QSslKey sslKey;

    // Audio streaming (LAN: converter-based, USB: direct capture)
    audioConverter *rxConverter = nullptr;
    QThread *rxConverterThread = nullptr;
    quint8 rigCodec = 0;
    quint32 rigSampleRate = 0;
    bool audioConfigured = false;
    QString audioErrorReason;
    quint16 audioSeq = 0;
    QSet<QWebSocket *> audioClients;

    // TX audio converter (LAN path: PCM → rig codec)
    audioConverter *txConverter = nullptr;
    QThread *txConverterThread = nullptr;

    // USB direct audio capture
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QAudioInput *usbAudioInput = nullptr;
#else
    QAudioSource *usbAudioInput = nullptr;
#endif
    QIODevice *usbAudioDevice = nullptr;

    // TX audio (USB output to rig)
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QAudioOutput *usbAudioOutput = nullptr;
#else
    QAudioSink *usbAudioOutput = nullptr;
#endif
    QIODevice *usbAudioOutputDevice = nullptr;
    bool txAudioConfigured = false;
    int usbOutputChannels = 1;

    // DATA MOD OFF auto-switch to USB when web mic is active
    rigInput savedDataOffMod;
    bool dataOffModSaved = false;
    QWebSocket *micActiveClient = nullptr;
};

#endif // WEBSERVER_H
