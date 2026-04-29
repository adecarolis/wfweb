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
#include "freedvprocessor.h"
#include "freedvreporter.h"
#include "pskreporter.h"
#include "radeprocessor.h"
#include "direwolfprocessor.h"
#include "ax25linkprocessor.h"
#include "aprsprocessor.h"

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
    void setupFreeDV(int mode, quint32 radioSampleRate);
    void sendToFreeDVRx(audioPacket audio);
    void sendToFreeDVTx(audioPacket audio);
    void setupRade(quint32 radioSampleRate);
    void sendToRadeRx(audioPacket audio);
    void sendToRadeTx(audioPacket audio);
    void setupPacket(quint32 radioSampleRate);
    void sendToPacketRx(audioPacket audio);

public slots:
    void init(quint16 httpPort, quint16 wsPort);
    void receiveRigCaps(rigCapabilities* caps);
    void receiveRxAudio(audioPacket audio);
    void setupAudio(quint8 codec, quint32 sampleRate);
    void setupUsbAudio(quint32 sampleRate, QString preferredInputName = QString(),
                       QString preferredOutputName = QString());
    void setLanInfo(bool isLan, bool connected);
    // Settings file used for persistent prefs written from the web layer.
    // Empty = use QSettings defaults (QCoreApplication org/app name).
    void setSettingsFile(const QString &path);
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

    // USB audio output (TX): drives precise PKT unkey via QAudioOutput's
    // own state machine — fires when Qt's internal queue actually underruns.
    void onUsbAudioOutputStateChanged(QAudio::State state);

    // FreeDV codec2
    void onFreeDVRxReady(audioPacket audio);
    void onFreeDVTxReady(audioPacket audio);
    void onFreeDVStats(float snr, bool sync);
    void onFreeDVRxCallsign(const QString &callsign);
    void onReporterStateChanged(int state);
    void onPskReporterStateChanged(int state);
    void drainFreeDVTxBuffer();

    // RADE V1
    void onRadeRxReady(audioPacket audio);
    void onRadeTxReady(audioPacket audio);
    void onRadeStats(float snr, bool sync, float freqOffset);
    void onRadeRxCallsign(const QString &callsign);

    // Dire Wolf packet (AX.25 / APRS)
    void onPacketRxDecoded(int chan, QJsonObject frame);
    void onPacketTxDecoded(int chan, QJsonObject frame);
    void onPacketTxReady(audioPacket audio);
    void onPacketTxFailed(QString reason);
    void drainPacketLanTxBuffer();
    void drainPacketUsbTxBuffer();

    // APRS station database / beacon scheduler
    void onAprsStationUpdated(QJsonObject station);
    void onAprsStationsCleared();
    void onAprsBeaconRequested(QString src, QString dst,
                               QStringList path, QString info);


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
    // Notify clients of the current state of either reporter.  The displayed
    // state distinguishes "waiting" (checkbox on but the matching mode isn't
    // active) from "connecting" (actually trying to reach the server).
    void notifyFreedvReporterStatus();
    void notifyPskReporterStatus();
    void sendBinaryToAll(const QByteArray &data);
    void sendBinaryToAudioClients(const QByteArray &data);
    // Common TX-audio writer.  Takes mono int16 LE PCM, expands to stereo
    // if the device requires it, optionally applies freedvTxGain (ALC-
    // controlled), and writes to usbAudioOutputDevice.  Called from
    // onWsBinaryMessage (mic / FT8 / voice — applyGain=false) and
    // drainPacketUsbTxBuffer (PKT — applyGain=true).  Same writer for
    // both, which is the structural unification of TX audio plumbing.
    void txWritePcmFrame(const QByteArray &pcmMonoLE, bool applyGain);
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
    FreeDVProcessor *freedvProcessor = nullptr;
    QThread *freedvThread = nullptr;
    FreeDVReporter *freedvReporter = nullptr;
    PskReporter *pskReporter = nullptr;
    QString reporterCallsign;
    QString reporterGrid;
    bool reporterEnabled = false;       // FreeDV Reporter toggle
    bool pskReporterEnabled = false;    // pskreporter.info toggle (FT8/FT4)
    bool digiActive = false;            // browser DIGI panel open (FT8/FT4)
    QString digiMode = QStringLiteral("FT8");
    bool freedvEnabled = false;
    int freedvMode = 0;
    QString freedvModeName = QStringLiteral("RADE");
    float freedvSNR = 0.0f;
    bool freedvSync = false;
    QTimer *reporterSnrTimer = nullptr;    // periodic SNR updates to FreeDV Reporter
    QString lastReportedCallsign;          // callsign to include in periodic reports
    QByteArray freedvTxBuffer;
    QTimer *freedvTxDrainTimer = nullptr;
    bool freedvTxActive = false;  // true once ALSA restarted for FreeDV TX
    float freedvTxGain = 0.25f;   // ALC-controlled gain applied to modem output
    bool freedvMonitor = false;   // bypass FreeDV RX to hear raw SSB

    // RADE V1 processing
    RadeProcessor *radeProcessor = nullptr;
    QThread *radeThread = nullptr;
    float freedvFreqOffset = 0.0f;  // RADE frequency offset estimate
    QString radeRxCallsign;          // last decoded callsign from EOO
    bool radeEooDraining = false;    // true while draining EOO frame to ALSA
    QTimer *radeCallsignClearTimer = nullptr;  // delayed UI clear after decode

    // Dire Wolf packet (AX.25 / APRS)
    DireWolfProcessor *dwProc = nullptr;
    QThread *dwThread = nullptr;
    AX25LinkProcessor *axProc = nullptr;
    AprsProcessor *aprsProc = nullptr;
    bool packetEnabled = false;
    int  packetMode = 300;     // 300 / 1200 / 9600 — single active modem
    // PKT TX in flight (any path).  Held from the first audio burst until
    // the rig has been unkeyed.  Replaces packetTxDraining — broader name
    // reflecting that PKT now has its own pipeline (no longer borrowing
    // FreeDV's drain machinery).
    bool packetTxBusy = false;

    // PKT USB pacer: dwThread delivers a whole frame in one audioPacket;
    // we feed it to ALSA in 10 ms slices via this timer.  Separate from
    // FreeDV's so the two codecs don't fight over a shared buffer/flag.
    QByteArray packetUsbTxBuffer;
    QTimer *packetUsbTxDrainTimer = nullptr;
    bool packetUsbTxActive = false;     // ALSA started for PKT TX
    bool packetTxAwaitingIdle = false;  // app buffer drained; waiting for
                                        // QAudioOutput → IdleState before
                                        // unkeying (replaces the old 300 ms
                                        // guess on USB).

    // PKT LAN pacer: converter's Opus path requires fixed frame sizes, so
    // the burst is fed in 20 ms slices at wall-clock rate.
    QByteArray packetLanTxBuffer;
    QTimer *packetLanTxTimer = nullptr;
    int packetLanTxChunkBytes = 0;

    // ---- AX.25 connected-mode terminal sessions ----
    // Sessions live server-side and survive WebSocket disconnects so a
    // browser refresh keeps the session active.  ax25_link's "client" id
    // is fixed at 1 — the server is one logical app — and individual
    // sessions are keyed by sid (server-issued) and uniquely identified
    // to ax25_link by the (own, peer, chan) tuple.
    // Minimal YAPP receive-side state.  One instance per session.  Feeds
    // bytes of an incoming AX.25 data stream through a 4-state framer that
    // splits YAPP control frames (SOH <len> <type> <data>) from ordinary
    // terminal text; YAPP frames are dispatched by type and ordinary bytes
    // fall through to the scrollback.
    // YAPP packet kinds, one per leading control byte on the wire.
    // ENQ(0x05)=SI  ACK(0x06)=RR or AF  SOH(0x01)=HD  STX(0x02)=DT
    // ETX(0x03)=EF  CAN(0x18)=CN        Per IW3FQG YAPP spec.
    enum class YappKind { SI, RR, RF, AF, AT, CA, HD, DT, EF, ET, CN };

    struct YappRxState {
        // AwaitSISig / AwaitACKSub / AwaitEFSig consume the second
        // signature byte (0x01 / 0x01|0x03 / 0x01) that confirms the
        // fixed-form packets.  AwaitLen + AwaitData collect the body
        // of the length-prefixed packets (HD/DT/CN); pendingKind
        // remembers which kind the leading control byte signalled.
        enum Phase {
            Idle,
            AwaitSISig,
            AwaitACKSub,
            AwaitEFSig,
            AwaitETSig,
            AwaitLen,
            AwaitData
        } phase = Idle;
        YappKind   pendingKind = YappKind::HD;
        int        remaining = 0;
        QByteArray buffer;

        // Current file being received (HD seen, EF not yet)
        QString    filename;
        qint64     filesize = 0;
        QByteArray fileBuf;
        bool       active = false;
        qint64     lastProgressMs = 0;
    };

    // Per-session transfer tracking — shared by TX and RX so the UI can
    // show a single "transfer in progress" widget.  TX side updates are
    // driven by yappSendFile as frames are queued; RX side by
    // yappHandleFrame as DT blocks arrive.
    struct XferState {
        bool       active = false;
        QString    dir;          // "tx" or "rx"
        QString    name;
        qint64     total = 0;
        qint64     done = 0;
        qint64     lastProgressMs = 0;
        bool       abortPending = false;  // TX only
        // SI/RR handshake state:
        //   "awaiting_rr"      — TX: SI sent, waiting for peer RR
        //   "awaiting_accept"  — RX: SI received, waiting for user accept
        //   "active"           — data flowing (HD/DT/EF)
        QString    phase;
        QByteArray pendingData;  // TX: buffered file bytes until RR arrives
        // TX-side pump: the YAPP frame list, queued one at a time
        // through onPacketTxReady so at most one frame sits in
        // ax25_link's I-frame queue.  Makes Abort take effect within
        // one frame instead of waiting for a pre-queued backlog to
        // drain on the air.
        QList<QByteArray> pendingYappFrames;
        int        framesPlanned = 0;
        int        framesSent = 0;
    };

    struct TerminalSession {
        QString sid;
        int     chan = 0;
        QString ownCall;
        QString peerCall;
        QStringList digis;
        enum State { Disconnected, Connecting, Connected, Disconnecting };
        State   state = Disconnected;
        QList<QJsonObject> scrollback;        // capped via TERM_SCROLLBACK_MAX
        qint64  createdMs = 0;
        qint64  lastActivityMs = 0;
        bool    incoming = false;
        YappRxState yapp;
        XferState   xfer;
    };
    QMap<QString, TerminalSession*> termSessions;
    int termNextSidNum = 1;
    static constexpr int TERM_FIXED_CLIENT = 1;
    static constexpr int TERM_SCROLLBACK_MAX = 1000;
    QSet<QString> termRegisteredOwnCalls;   // tracked so we don't double-register

    // QSettings backing file — populated by servermain after getSettingsFilePath
    // resolves the -s flag.  Empty means "use the default (QSettings org/app)".
    QString packetSettingsFile_;
    void    packetLoadSettings();
    void    packetSaveSettings();

    QString termMakeSid();
    QJsonObject termSessionToJson(const TerminalSession *s) const;
    QJsonObject termScrollbackEntry(const QString &dir, const QByteArray &data);
    // YAPP helpers
    void       yappFeedIncoming(TerminalSession *s, const QByteArray &chunk,
                                QByteArray &textOut);
    void       yappHandleFrame(TerminalSession *s, YappKind kind, const QByteArray &data);
    void       yappSendFile   (TerminalSession *s, const QString &name,
                                const QByteArray &data);
    void       yappStartDataPhase(TerminalSession *s);
    void       yappPumpNextFrame(TerminalSession *s);
    TerminalSession *yappFindActiveTxXfer();
    void       yappSendRR     (TerminalSession *s);   // ACK 0x01
    void       yappSendRF     (TerminalSession *s);   // ACK 0x02
    void       yappSendAF     (TerminalSession *s);   // ACK 0x03
    void       yappSendAT     (TerminalSession *s);   // ACK 0x04
    void       yappSendAB     (TerminalSession *s);   // CN with no reason
    void       yappAbortSend  (TerminalSession *s);
    void       xferBroadcastStart(const TerminalSession *s);
    void       xferBroadcastProgress(TerminalSession *s, bool force = false);
    void       xferBroadcastEnd(const TerminalSession *s, bool aborted);
    // Per-kind frame builders.  Each returns the exact bytes to queue
    // into the AX.25 I-frame info field.
    static QByteArray yappBuildSI();                              // ENQ 01
    static QByteArray yappBuildRR();                              // ACK 01
    static QByteArray yappBuildRF();                              // ACK 02
    static QByteArray yappBuildAF();                              // ACK 03
    static QByteArray yappBuildAT();                              // ACK 04
    static QByteArray yappBuildHD(const QString &name, qint64 size);  // SOH len data
    static QByteArray yappBuildDT(const QByteArray &chunk);        // STX len data
    static QByteArray yappBuildEF();                              // ETX 01
    static QByteArray yappBuildET();                              // EOT 01
    static QByteArray yappBuildCN(const QString &reason = {});     // CAN len [reason]
    void termAppendScrollback(TerminalSession *s, const QJsonObject &entry);
    void termBroadcastSession(const TerminalSession *s);
    void termBroadcastData(const QString &sid, const QJsonObject &entry);
    TerminalSession *termFindByEndpoints(int chan, const QString &own, const QString &peer);
    void termEnsureRegistered(int chan, const QString &ownCall);

private slots:
    // AX25LinkProcessor signal handlers (queued).
    void onAxLinkEstablished(int client, int chan,
                             const QString &peerCall, const QString &ownCall,
                             bool incoming);
    void onAxLinkTerminated (int client, int chan,
                             const QString &peerCall, const QString &ownCall,
                             int timeout);
    void onAxRxData         (int client, int chan,
                             const QString &peerCall, const QString &ownCall,
                             int pid, const QByteArray &data);
    void onAxDataAcked      (int client, int chan,
                             const QString &ownCall, const QString &peerCall,
                             int count);

private:

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
