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
#include <QQueue>
#include <QHash>
#include <QSet>
#include <QThread>
#include <QUrlQuery>
#include <QSslSocket>
#include <QSslKey>
#include <QSslCertificate>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QCryptographicHash>

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

#ifdef Q_OS_MACOS
class TlsProxyWorker;
#endif

struct WsprNetUploadItem {
    QByteArray body;
    QByteArray key;
    QString batchId;
    QString functionName;
    QString summary;
    qint64 createdUtcMs = 0;
    qint64 slotStartUtcMs = 0;
    qint64 nextAttemptUtcMs = 0;
    bool spot = false;
    int retries = 0;
};

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
    void requestPowerOn();
    void requestPowerOff();
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
    void onAudioStateChanged(QAudio::State state);
    void onWsprNetReply(QNetworkReply *reply);


private:
    void serveStaticFile(QTcpSocket *socket, const QString &path);
    void sendHttpResponse(QTcpSocket *socket, int statusCode, const QString &statusText,
                         const QByteArray &contentType, const QByteArray &body);
    void handleRestRequest(QTcpSocket *socket, const QString &method,
                           const QString &path, const QByteArray &body);
    void sendRestResponse(QTcpSocket *socket, int statusCode, const QJsonObject &json);
    QJsonObject buildInfoJson() const;
    QJsonObject buildWsprNetStatusJson();
    QJsonObject buildWsprDecodeTelemetryJson() const;
    void recordWsprDecodeTelemetry(const QJsonObject &payload, const QString &clientAddr);
    void recordWsprNetEvent(const QJsonObject &event);
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
    bool enqueueWsprNetItem(const WsprNetUploadItem &item);
    void pruneWsprNetRecent();
    void pumpWsprNetQueue();

    QHash<QTcpSocket*, QByteArray> socketBuffers; // accumulate partial TCP reads

    QTcpServer *httpServer = nullptr;
    QTcpServer *restServer = nullptr;  // plain HTTP for microcontrollers/scripts (SSL mode only)
    QWebSocketServer *wsServer = nullptr;
    QList<QWebSocket *> wsClients;
    cachingQueue *queue = nullptr;
    rigCapabilities *rigCaps = nullptr;
    QTimer *statusTimer = nullptr;
    QMimeDatabase mimeDb;
    bool rigPoweredOn = true;

    // SSL
    bool sslEnabled = false;
    QSslCertificate sslCert;
    QSslKey sslKey;
#ifdef Q_OS_MACOS
    QString sslCertPath;
    QString sslKeyPath;
    // OpenSSL TLS proxy (bypasses Secure Transport CertificateRequest issue)
    bool useOpenSslProxy = false;
    TlsProxyWorker *tlsProxyWorker = nullptr;
    QThread *tlsProxyThread = nullptr;
    quint16 internalHttpPort = 0;
#endif

    // Audio streaming (LAN: converter-based, USB: direct capture)
    audioConverter *rxConverter = nullptr;
    QThread *rxConverterThread = nullptr;
    quint8 rigCodec = 0;
    quint32 rigSampleRate = 0;
    bool audioConfigured = false;
    QString audioErrorReason;
    quint16 audioSeq = 0;
    QSet<QWebSocket *> audioClients;
    QNetworkAccessManager *wsprNetManager = nullptr;
    QTimer *wsprNetTimer = nullptr;
    QQueue<WsprNetUploadItem> wsprNetQueue;
    QHash<QNetworkReply*, WsprNetUploadItem> wsprNetReplies;
    QHash<QByteArray, qint64> wsprNetRecentAccepted;
    QSet<QByteArray> wsprNetPendingKeys;
    QString wsprNetLastResult;
    QString wsprNetLastError;
    QJsonObject wsprNetLatestEvent;
    QJsonArray wsprNetRecentEvents;
    qint64 wsprNetUpdatedMs = 0;
    QJsonObject wsprDecodeTelemetryLatest;
    QJsonArray wsprDecodeTelemetryRecent;
    qint64 wsprDecodeTelemetryUpdatedMs = 0;

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
    QTimer *usbAudioPollTimer = nullptr;
    qint64 usbAudioStreamStartUtcMs = 0;
    qint64 usbAudioSamplesStreamed = 0;

    // TX audio (USB output to rig)
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QAudioOutput *usbAudioOutput = nullptr;
#else
    QAudioSink *usbAudioOutput = nullptr;
#endif
    QIODevice *usbAudioOutputDevice = nullptr;
    bool txAudioConfigured = false;
    int usbOutputChannels = 1;
    // Pre-TX buffer: accumulate chunks before starting ALSA so it begins with
    // a full buffer instead of racing the drain rate from the first byte.
    QByteArray preTxBuffer;
    bool preTxBuffering = false;
    bool txAudioActive = false; // true once pre-buffer is flushed; stops device on IdleState

    // DATA MOD OFF auto-switch to USB when web mic is active
    rigInput savedDataOffMod;
    bool dataOffModSaved = false;
    QWebSocket *micActiveClient = nullptr;

    // Memory channel scanning
    QMap<quint32, memoryType> memories;  // key = (group << 16) | channel
    bool memoryScanActive = false;
    int memoryScanCurrent = 0;
    int memoryScanEnd = 0;
    int memoryScanGroup = 0;
    QTimer *memoryScanTimer = nullptr;
    QJsonObject memoryToJson(const memoryType &mem);
    QString modeRegToString(quint8 reg);
    void scanNextMemory();
};

#endif // WEBSERVER_H
