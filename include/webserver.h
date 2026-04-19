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
#ifdef FREEDV_SUPPORT
#include "freedvprocessor.h"
#include "freedvreporter.h"
#endif
#ifdef RADE_SUPPORT
#include "radeprocessor.h"
#endif
#ifdef PACKET_SUPPORT
#include "direwolfprocessor.h"
#endif

#ifdef Q_OS_MACOS
class TlsProxyWorker;
#endif

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
    void requestLanDisconnect();
    void requestLanReconnect();
    void setupConverter(QAudioFormat inFormat, codecType inCodec,
                        QAudioFormat outFormat, codecType outCodec,
                        quint8 opusComplexity, quint8 resampleQuality);
    void sendToConverter(audioPacket audio);
    void setupTxConverter(QAudioFormat inFormat, codecType inCodec,
                          QAudioFormat outFormat, codecType outCodec,
                          quint8 opusComplexity, quint8 resampleQuality);
    void sendToTxConverter(audioPacket audio);
    void haveTxAudioData(audioPacket audio);
#ifdef FREEDV_SUPPORT
    void setupFreeDV(int mode, quint32 radioSampleRate);
    void sendToFreeDVRx(audioPacket audio);
    void sendToFreeDVTx(audioPacket audio);
#endif
#ifdef RADE_SUPPORT
    void setupRade(quint32 radioSampleRate);
    void sendToRadeRx(audioPacket audio);
    void sendToRadeTx(audioPacket audio);
#endif
#ifdef PACKET_SUPPORT
    void setupPacket(quint32 radioSampleRate);
    void sendToPacketRx(audioPacket audio);
#endif

public slots:
    void init(quint16 httpPort, quint16 wsPort);
    void receiveRigCaps(rigCapabilities* caps);
    void receiveRxAudio(audioPacket audio);
    void setupAudio(quint8 codec, quint32 sampleRate);
    void setupUsbAudio(quint32 sampleRate);
    void setLanInfo(bool isLan, bool connected);
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

#ifdef FREEDV_SUPPORT
    // FreeDV codec2
    void onFreeDVRxReady(audioPacket audio);
    void onFreeDVTxReady(audioPacket audio);
    void onFreeDVStats(float snr, bool sync);
    void onFreeDVRxCallsign(const QString &callsign);
    void onReporterStateChanged(int state);
#endif
    void drainFreeDVTxBuffer();

#ifdef RADE_SUPPORT
    // RADE V1
    void onRadeRxReady(audioPacket audio);
    void onRadeTxReady(audioPacket audio);
    void onRadeStats(float snr, bool sync, float freqOffset);
    void onRadeRxCallsign(const QString &callsign);
#endif

#ifdef PACKET_SUPPORT
    // Dire Wolf packet (AX.25 / APRS)
    void onPacketRxDecoded(int chan, QJsonObject frame);
#endif


private:
    void serveStaticFile(QTcpSocket *socket, const QString &path);
    void sendHttpResponse(QTcpSocket *socket, int statusCode, const QString &statusText,
                         const QByteArray &contentType, const QByteArray &body);
    void handleRestRequest(QTcpSocket *socket, const QString &method,
                           const QString &path, const QByteArray &body);
    void sendRestResponse(QTcpSocket *socket, int statusCode, const QJsonObject &json);
    QJsonObject buildInfoJson() const;
    void sendJsonToAll(const QJsonObject &obj);
    void sendJsonTo(QWebSocket *client, const QJsonObject &obj);
    void startReporterSnrTimer();
    void stopReporterSnrTimer();
    void sendBinaryToAll(const QByteArray &data);
    void sendBinaryToAudioClients(const QByteArray &data);
    void handleCommand(QWebSocket *client, const QJsonObject &cmd);
    void requestVfoUpdate();
    void disableFreeDV();
    bool isFreeDVCompatibleMode(rigMode_t mk) const;
    void sendCurrentState(QWebSocket *client);
    QString modeToString(modeInfo m);
    modeInfo stringToMode(const QString &mode);
    QJsonObject buildStatusJson();
    codecType codecByteToType(quint8 codec);
    bool setupSsl();

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
    bool lanMode = false;
    bool lanConnected = false;

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

    // FreeDV processing
#ifdef FREEDV_SUPPORT
    FreeDVProcessor *freedvProcessor = nullptr;
    QThread *freedvThread = nullptr;
    FreeDVReporter *freedvReporter = nullptr;
    QString reporterCallsign;
    QString reporterGrid;
    bool reporterEnabled = false;
#endif
    bool freedvEnabled = false;
    int freedvMode = 0;
#ifdef RADE_SUPPORT
    QString freedvModeName = QStringLiteral("RADE");
#else
    QString freedvModeName = QStringLiteral("700D");
#endif
    float freedvSNR = 0.0f;
    bool freedvSync = false;
    QTimer *reporterSnrTimer = nullptr;    // periodic SNR updates to FreeDV Reporter
    QString lastReportedCallsign;          // callsign to include in periodic reports
    QByteArray freedvTxBuffer;
    QTimer *freedvTxDrainTimer = nullptr;
    bool freedvTxActive = false;  // true once ALSA restarted for FreeDV TX
    float freedvTxGain = 0.25f;   // ALC-controlled gain applied to modem output
    bool freedvMonitor = false;   // bypass FreeDV RX to hear raw SSB

#ifdef RADE_SUPPORT
    // RADE V1 processing
    RadeProcessor *radeProcessor = nullptr;
    QThread *radeThread = nullptr;
    float freedvFreqOffset = 0.0f;  // RADE frequency offset estimate
    QString radeRxCallsign;          // last decoded callsign from EOO
    bool radeEooDraining = false;    // true while draining EOO frame to ALSA
    QTimer *radeCallsignClearTimer = nullptr;  // delayed UI clear after decode
#endif

#ifdef PACKET_SUPPORT
    // Dire Wolf packet (AX.25 / APRS)
    DireWolfProcessor *dwProc = nullptr;
    QThread *dwThread = nullptr;
    bool packetEnabled = false;
    int  packetMode = 1200;    // 300 / 1200 / 9600 — single active modem
#endif

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
