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
#include <QElapsedTimer>
#include <QSet>
#include <QThread>
#include <QSslSocket>
#include <QSslKey>
#include <QSslCertificate>
#include <QUdpSocket>
#include <QUrlQuery>
#include <QSettings>
#include <QMutex>
#include <QPointer>
#include <memory>
#include <atomic>
#include <functional>
#include "logbook.h"
#include "access.h"

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
#include "memorystore.h"

#ifdef Q_OS_MACOS
class TlsProxyWorker;
#endif

// SSL-enabled TCP server for HTTPS
class SslTcpServer : public QTcpServer {
public:
    QSslCertificate cert;
    QList<QSslCertificate> chain;   // cert first, then intermediates (--cert fullchain.pem)
    QSslKey key;
    explicit SslTcpServer(QObject *parent = nullptr) : QTcpServer(parent) {}
protected:
    void incomingConnection(qintptr handle) override {
        QSslSocket *socket = new QSslSocket(this);
        if (socket->setSocketDescriptor(handle)) {
            if (chain.size() > 1) socket->setLocalCertificateChain(chain);
            else socket->setLocalCertificate(cert);
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

    // State for the terminal status page, which reads it from the main thread
    // while this object lives on the web thread — hence the atomics.
    enum ListenState { ListenPending, ListenOk, ListenFailed };
    ListenState webListenState() const { return ListenState(webListen_.load()); }
    ListenState restListenState() const { return ListenState(restListen_.load()); }
    bool httpsActive() const { return httpsActive_.load(); }
    int browserCount() const { return browserCount_.load(); }
    // Certificate the page is served with: wfweb's own (the browser warns
    // once) or one handed in with --cert/--key.
    bool selfSignedCert() const { return selfSigned_.load(); }
    // Shared Station (--shared): access control on; account count; the
    // first-run / recovery setup code while one is valid (empty otherwise).
    bool sharedMode() const { return sharedMode_.load(); }
    int sharedUserCount() const { return sharedUsers_.load(); }
    QString sharedSetupCode() const;

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
    // --name tag: sent to the browser as rigInfo.name so the top bar and
    // tab title show it instead of the rig model.
    void setInstanceName(const QString &name);
    // Logbook path and remote-logging (WSJT-X UDP) setup from settings +
    // command-line overrides; runs once on the web thread at startup.
    void configureLogbook(const QString &logbookOverride,
                          const QString &remoteLogOverride,
                          bool noRemoteLog, bool remoteLogDecodes);
    // Where received SSTV pictures are kept (--sstv-dir, SstvDir= in the
    // settings file, default <data dir>/sstv); runs once on the web thread.
    void configureSstvDir(const QString &sstvDirOverride);
    // --cert/--key (or Web/CertFile, Web/KeyFile): serve this certificate
    // instead of the generated self-signed one.  Must run before init().
    void configureTls(const QString &certFile, const QString &keyFile);
    // Shared Station: --shared / Shared/Enabled, --shared-dir / Shared/Dir,
    // --shared-reset-admin, --trust-proxy / Shared/TrustProxy.  Must run
    // before init() so the first connection is already gated.
    void configureShared(bool enableOverride, const QString &dirOverride,
                         bool resetAdmin, const QString &trustProxyOverride);
    // PTT requested via rigctld (Hamlib TCP). Routed through the same
    // setPTT path the WebSocket clients use, so RADE EOO synthesis,
    // packet TX gating and ALC meter polling stay coherent.
    void onRigCtlPtt(bool on);
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
    void modemLanTxDrain();
    void usbTxPump();

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
    void serveStaticFile(QTcpSocket *socket, const QString &path, const QByteArray &headerSection);
    // Issue #69: the plain-HTTP REST port used to hand back the SPA for any
    // non-/api/v1 GET, which left a browser stuck retrying a WebSocket that
    // only exists on the HTTPS port.  Serve a human-readable signpost instead.
    void serveRestInfoPage(QTcpSocket *socket, const QByteArray &headerSection);
    void sendHttpResponse(QTcpSocket *socket, int statusCode, const QString &statusText,
                          const QByteArray &contentType, const QByteArray &body,
                          const QByteArray &extraHeaders = QByteArray());
    void handleRestRequest(QTcpSocket *socket, const QString &method,
                           const QString &path, const QUrlQuery &query,
                           const QByteArray &body);
    void sendRestResponse(QTcpSocket *socket, int statusCode, const QJsonObject &json,
                          const QByteArray &extraHeaders = QByteArray());

    // --- HTTP connection hygiene -------------------------------------------
    // Every socket from accept to close: a request that never completes its
    // headers (slowloris, a TLS client that never handshakes) is cut after
    // kHttpHeaderTimeoutMs, a client that stops reading its response after
    // kHttpDrainStallMs, and no more than kHttpMaxConnections are kept at
    // once.  Sockets handed to the WebSocket server leave the table.
    struct HttpConn {
        qint64 openedMs = 0;
        bool requestDone = false;      // response handed to the socket
        qint64 lastProgressMs = 0;     // bytesToWrite() last went down
        qint64 lastBytesToWrite = 0;
    };
    QHash<QTcpSocket *, HttpConn> httpConns;
    QTimer *httpSweepTimer = nullptr;
    void sweepHttpConnections();
    void httpRequestDone(QTcpSocket *socket);
    qint64 httpPendingWriteBytes() const;
    static const int kHttpMaxConnections = 128;
    // Responses are buffered whole in the socket; a client that asks for
    // the CW model (15 MB) on every connection it may open and reads none
    // of it would otherwise hold kHttpMaxConnections x that in memory.
    static const qint64 kHttpMaxPendingWriteBytes = 128 * 1024 * 1024;
    // WebSocket clients: a message bigger than anything the page sends is
    // refused by Qt before it is buffered; spectrum / audio frames are
    // dropped for a client more than kWsBinaryPendingBytes behind (it is
    // not keeping up; fresher frames serve it better), and one that lets
    // kWsMaxPendingBytes pile up is not reading at all and is cut, else a
    // visitor who connects and never reads holds the whole stream in RAM.
    static const quint64 kWsMaxIncomingBytes = 16 * 1024 * 1024;
    static const qint64 kWsBinaryPendingBytes = 2 * 1024 * 1024;
    static const qint64 kWsMaxPendingBytes = 8 * 1024 * 1024;
    void sweepWsClients();
    // The write backlog of a socket: QSslSocket::bytesToWrite() counts only
    // what is still to be encrypted; what is encrypted and unsent sits in
    // encryptedBytesToWrite(), which is where a stalled reader's data is.
    static qint64 pendingWriteBytes(QTcpSocket *socket);
    qint64 wsPendingBytes(QWebSocket *client) const;
    // QWebSocket hides its socket (and bytesToWrite() has the same blind
    // spot), so the QTcpSocket handed to the WebSocket server is kept, by
    // peer address and port until the QWebSocket exists, then by it.
    QHash<QString, QPointer<QTcpSocket>> wsHandover_;
    QHash<QWebSocket *, QPointer<QTcpSocket>> wsRawSocket_;
    static QString peerKey(const QHostAddress &addr, quint16 port) { return addr.toString() + ':' + QString::number(port); }
    static const int kHttpHeaderTimeoutMs = 10000;
    static const int kHttpDrainStallMs = 60000;
    static QByteArray headerValue(const QByteArray &headerSection, const QByteArray &name);
    static QString hostOf(const QString &hostOrOrigin);
    // TLS on this socket itself (QSslSocket, or the macOS proxy's backend).
    bool connectionIsSecure(QTcpSocket *socket) const;
    static QString peerIp(const QHostAddress &addr);

    // --- Shared Station ----------------------------------------------------
    // What a request arrives as: the client address (through a trusted
    // proxy's X-Forwarded-For), whether it is on TLS (or the proxy says
    // https), which port, and who it is once its cookie or bearer token is
    // resolved.
    struct RequestContext {
        QString ip;
        QString userAgent;
        bool secure = false;
        bool restPort = false;
        AccessPrincipal who;
    };
    static QJsonObject userJson(const AccessPrincipal &who);
    void closeUserClients(const QString &userId, const QString &reason);
    RequestContext requestContext(QTcpSocket *socket, const QByteArray &headerSection);
    bool trustedProxy(const QHostAddress &peer) const;
    void handleAuthRequest(QTcpSocket *socket, const QString &method, const QString &path,
                           const QByteArray &body, const RequestContext &ctx);
    void handleSharedUsersRequest(QTcpSocket *socket, const QString &method, const QString &path,
                                  const QByteArray &body, const RequestContext &ctx);
    QJsonObject authMeJson(const RequestContext &ctx) const;
    QByteArray sessionCookieHeader(const QByteArray &token, bool secure) const;
    void closeSessionClients(const QByteArray &sessionHash, const QString &reason);
    void sharedHousekeeping();
    void updateSharedStats();
    // PBKDF2 takes hundreds of milliseconds: run it on a worker thread and
    // call back on this thread, where the socket is answered.
    void hashPasswordAsync(const QString &password, std::function<void(QString)> done);
    void verifyPasswordAsync(const QString &password, const QString &hash, std::function<void(bool)> done);

    std::unique_ptr<AccessControl> access_;
    std::atomic<bool> sharedMode_{false};
    std::atomic<int> sharedUsers_{0};
    mutable QMutex sharedMutex_;
    QString sharedSetupCode_;
    QList<QPair<QHostAddress, int>> trustedProxies_;
    // One record per WebSocket: who it is (a visitor without a session has
    // a principal too, unauthenticated, with the role the policy gives
    // visitors), a short id the admin's connection list refers to, where
    // from and since when.
    struct WsClient {
        AccessPrincipal who;
        QString id;
        QString ip;
        qint64 sinceMs = 0;
        bool warnedTxAudio = false;   // one 'denied' per stream of 0x03 frames, not per frame
    };
    QHash<QWebSocket *, WsClient> wsInfo;
    QTimer *sharedTimer = nullptr;

    // --- policy: admin-settable, kept in the settings file ---------------
    Access::Role sharedAnonymous_ = Access::Role::None;   // what a visitor without a session may do
    bool txInhibit_ = false;                              // nobody keys the rig
    QStringList txInhibitBands_;                          // ... or nobody on these bands (rig-file names)
    QString inhibitedBand();                              // the inhibited band the VFO is on, or empty
    QString txBlockedReason();                            // why nobody may transmit now, or empty
    int txTimeoutSecs_ = 600;                             // a key held this long is released
    int maxClients_ = 20;                                 // non-admin connections at once
    QJsonObject policyJson() const;
    void applyPolicy(const QJsonObject &in, const QString &by);
    void reroleAnonymousClients();
    void broadcastPolicy();
    QJsonArray connectionsJson() const;
    void handleSharedPolicyRequest(QTcpSocket *socket, const QString &method, const QByteArray &body,
                                   const RequestContext &ctx);
    void handleSharedConnectionsRequest(QTcpSocket *socket, const QString &method, const QString &path,
                                        const RequestContext &ctx);

    // --- the gate ------------------------------------------------------------
    Access::Role clientRole(QWebSocket *client) const;    // Admin in open mode
    void deny(QWebSocket *client, const QString &cmd, Access::Cap cap, const QString &reason = QString());
    // Every key-down and key-up on record, and the time-out behind them.
    void pttAccounting(bool on, QWebSocket *client);
    void forceUnkey(const QString &reason);
    QString operatingText();
    QTimer *txTimeoutTimer = nullptr;
    qint64 pttSinceMs = 0;
    QString pttWho;
    QString restWho_;      // the REST caller while handleRestRequest runs, for the PTT audit

    // --- the seat: one browser drives the radio at a time ---------------
    // Whoever holds it tunes and, with the role for it, keys; everyone else
    // watches.  It is taken when free, asked for when held (the holder is
    // prompted; an unanswered request is granted after seatRequestSecs_),
    // kept for seatGraceSecs_ by a holder whose socket drops (a reload, a
    // phone off the network), given up on idleness, and offered down a
    // queue when it frees.  Admins take it outright.
    struct Seat {
        QWebSocket *holder = nullptr;
        qint64 sinceMs = 0;
        qint64 lastActivityMs = 0;
        bool privateMode = false;        // the holder's choice, when the policy allows it
        QByteArray graceSession;         // a dropped holder's session, reserved until graceUntilMs
        QString graceName;
        qint64 graceUntilMs = 0;
        QWebSocket *requester = nullptr; // the request the holder has not answered
        qint64 requestDeadlineMs = 0;    // 0 = never granted unasked
        QList<QWebSocket *> queue;       // waiting, first in line first
        QWebSocket *offerTo = nullptr;   // the head of the queue, while the seat is free for them
        qint64 offerUntilMs = 0;
        bool warnedIdle = false;
        bool warnedMax = false;
    } seat_;
    bool seatEnabled_ = true;
    int seatIdleSecs_ = 600;
    int seatGraceSecs_ = 30;
    int seatRequestSecs_ = 60;
    int seatOfferSecs_ = 30;
    int seatMaxSecs_ = 0;                // seat time while others wait; 0 = unlimited
    bool seatPreemptHigher_ = false;     // a higher role may take the seat after a 30 s warning
    bool listenIn_ = true;               // others hear and see while someone has the seat
    bool allowPrivate_ = false;          // the holder may switch the others' audio and scope off
    QTimer *seatTimer = nullptr;
    void seatTick();
    // Reservations (phase 4): the seat follows the schedule.  During a
    // slot its owner takes the seat at will, is given it at the start when
    // connected, and a free seat waits for them for kReservationHoldSecs;
    // their seat is never handed on unasked while the slot runs.
    std::unique_ptr<Reservations> reservations_;
    static const int kReservationHoldSecs = 15 * 60;
    QString seatResKey_;        // the occurrence the start hand-over was done for
    QString seatResWarnKey_;    // the occurrence the five-minute warning went out for
    Occurrence activeReservation() const;
    bool ownsSlot(QWebSocket *client, const Occurrence &occ) const;
    QWebSocket *socketOfUser(const QString &userId) const;
    QJsonObject occurrenceJson(const Occurrence &occ) const;
    void handleSharedReservationsRequest(QTcpSocket *socket, const QString &method, const QString &path,
                                         const QByteArray &body, const RequestContext &ctx);
    bool seatRequired(QWebSocket *client, const QString &cmd, Access::Cap cap) const;
    bool seatHides(QWebSocket *client) const;     // no audio / spectrum for this client right now
    bool seatHolderIs(const QString &userId) const;
    void denySeat(QWebSocket *client, const QString &cmd);
    QString seatLabel(QWebSocket *client) const;  // "W1ABC" / "visitor"
    void seatGive(QWebSocket *client, const QString &how);
    void seatFree(const QString &why, bool tellHolder = true);
    void seatOfferNext();
    void seatLeaveQueue(QWebSocket *client);
    void seatOnDisconnect(QWebSocket *client, const AccessPrincipal &who);
    void seatOnConnect(QWebSocket *client);
    void seatCommand(QWebSocket *client, const QString &type, const QJsonObject &cmd);
    QJsonObject seatJson(QWebSocket *client) const;
    void broadcastSeat();
    void handleSharedSeatRequest(QTcpSocket *socket, const QString &method, const RequestContext &ctx);
    // SSTV image gallery: /api/v1/sstv/images (list, upload, fetch, delete)
    void handleSstvRequest(QTcpSocket *socket, const QString &method, const QString &path,
                           const QUrlQuery &query, const QByteArray &body);
    QJsonArray sstvGalleryList() const;
    void sstvGalleryChanged();
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
    // usbTxPump (mic / FT8 / voice — applyGain=false) and
    // drainPacketUsbTxBuffer (PKT — applyGain=true).  Same writer for
    // both, which is the structural unification of TX audio plumbing.
    void txWritePcmFrame(const QByteArray &pcmMonoLE, bool applyGain);
    // Browser-frame coalescing in front of txWritePcmFrame (see
    // usbTxPending): bytes of mono PCM per device write, the pending-
    // buffer reset for when the mic session ends or another TX path takes
    // the device.
    int usbTxPeriodBytes() const;
    void usbTxReset();
    void handleCommand(QWebSocket *client, const QJsonObject &cmd);
    // One station callsign + grid for the whole server, persisted under
    // [Station] in the settings file and pushed to every browser.  Replaces
    // the old per-browser value that the last client to connect used to
    // overwrite (a stale tab could silently swap the call on every QSO).
    void applyStationCallsign(const QString &call, const QString &grid, bool persist);
    // CW keyer macros: [CW] Macro1..Macro10 in the settings file, one set for
    // every browser (#141).  applyCwMacros replaces the whole set, persists
    // it and pushes it to every client.
    void loadCwMacros();
    void applyCwMacros(const QJsonObject &macros, const char *who);
    QJsonObject cwMacrosJson() const;
    std::unique_ptr<QSettings> openSettings() const;
    // Station logbook (see logbook.h).  These wrap the Logbook mutations
    // with the WebSocket delta events and the WSJT-X emission.
    // `who` is the account behind the change (a socket's principal, the
    // REST caller); nullptr is rigctld / an internal caller, and everyone
    // without --shared.
    bool logbookAdd(QsoRecord &r, const AccessPrincipal *who = nullptr);
    bool logbookUpdate(const QString &id, QsoRecord r, const AccessPrincipal *who = nullptr);
    bool logbookRemove(const QString &id, const AccessPrincipal *who = nullptr);
    void logbookReset();
    QJsonObject logbookSummary(const AccessPrincipal *who = nullptr) const;
    // Shared Station, callsigns and the log (phase 6).  One predicate each
    // for "may see", "may change" and "may export" a record; every route
    // and every broadcast asks them.  Without --shared, or for a nullptr
    // principal, all three say yes.
    //   see:    admins everything; an account the club log (records on the
    //           configured station call) and what it logged itself; a
    //           visitor nothing.
    //   change: admins everything; an account what it logged itself.
    //   export: admins the club log; an account (admins with own=true) the
    //           QSOs it made on its own call.
    bool isClubCall(const QString &stationCall) const;
    bool logSees(const AccessPrincipal *who, const QsoRecord &r) const;
    bool logMayChange(const AccessPrincipal *who, const QsoRecord &r) const;
    bool logMayExport(const AccessPrincipal *who, const QsoRecord &r, bool own) const;
    Logbook::Filter logFilter(const AccessPrincipal *who) const;
    Logbook::Filter logExportFilter(const AccessPrincipal *who, bool own) const;
    void logCounts(const AccessPrincipal *who, int *count, int *unexported) const;
    QJsonObject qsoJson(const QsoRecord &r) const;   // + operatorName in shared mode
    // A log message to every socket that may see `about` (all of them when
    // nullptr), built per viewer: the counts in it are the viewer's own.
    void logbookSend(const QsoRecord *about, const std::function<QJsonObject(const AccessPrincipal *)> &make);
    QString logWho(const AccessPrincipal *who) const;
    const AccessPrincipal *restPrincipal_ = nullptr;   // the REST caller while handleRestRequest runs (shared mode)
    QString restIp_;
    // The on-air call: the configured station call (reporterCallsign),
    // except while an account set to transmit under its own callsign holds
    // the seat.  Never persisted.  applyOnAirCall() pushes a change to
    // everything that identifies the station: RADE's EOO callsign, the
    // FreeDV and PSK reporters, every page (stationChanged) and the audit
    // log; the logbook stamp and the remote log read onAirCall() directly.
    QString onAirCall() const;
    QWebSocket *onAirHolder() const;          // the holder transmitting under its own call, or nullptr
    void applyOnAirCall(bool stationChanged = false);
    QJsonObject stationJson() const;          // the stationChanged message
    void addOnAirInfo(QJsonObject &o) const;  // clubCallsign / onAirOwn / onAirOperator, shared mode only
    // A browser's source call for packet (APRS beacon, AX.25 terminal)
    // must be the on-air call, any SSID.  True = refused and answered.
    bool onAirDenies(QWebSocket *client, const QString &cmd, const QString &call);
    // An account set to its own call but without one cannot be on the air.
    bool seatEligible(QWebSocket *client, const QString &cmd);
    // An admin edited an account: its open sockets follow, and so does
    // the on-air call when it holds the seat.
    void refreshUserClients(const QString &userId);
    QString onAirApplied_;
    QString onAirAppliedBy_;
    bool logbookClear(const char *who);
    void wsjtxSendHeartbeat();
    void wsjtxSendStatus();
    void wsjtxSendQso(const QsoRecord &qso);
    void wsjtxSendDecode(const QJsonObject &cmd);
    void wsjtxSendClose();
    void wsjtxSendDatagram(const QByteArray &packet);
    bool configureWsjtxTarget(const QString &target);
    void requestVfoUpdate();
    void disableFreeDV();
    bool isFreeDVCompatibleMode(rigMode_t mk) const;
    void sendCurrentState(QWebSocket *client);
    QString modeToString(modeInfo m);
    // JSON for a cached rig value, or null when the rig had nothing to report
    // (blank memory channel): Hz == 0 / a default modeInfo / filter 0xff.
    static QJsonValue freqJson(const freqt &f);
    QJsonValue modeJson(const modeInfo &m);
    static QJsonValue filterJson(const modeInfo &m);
    modeInfo stringToMode(const QString &mode);
    QJsonObject buildStatusJson();
    // Operating freq / mode with the fallbacks memory mode needs (#138).
    cacheItem operatingFreqCache(const vfoCommandType &t);
    cacheItem operatingModeCache(const vfoCommandType &t);
    codecType codecByteToType(quint8 codec);
    bool setupSsl();

    QHash<QTcpSocket*, QByteArray> socketBuffers; // accumulate partial TCP reads

    QTcpServer *httpServer = nullptr;
    QTcpServer *restServer = nullptr;  // plain HTTP for microcontrollers/scripts (SSL mode only)
    quint16 webHttpsPort = 0;          // configured HTTPS/web-UI port, for the REST signpost (#69)
    QWebSocketServer *wsServer = nullptr;
    QList<QWebSocket *> wsClients;
    cachingQueue *queue = nullptr;
    rigCapabilities *rigCaps = nullptr;
    QTimer *statusTimer = nullptr;
    QMimeDatabase mimeDb;
    bool rigPoweredOn = true;
    bool lanMode = false;
    bool lanConnected = false;
    // The rig declares the ATU command but keeps refusing its status read
    // (IC-705 with no AH-705 attached): hide the tuner until it answers.
    bool tunerRejected = false;

    // Which meter the browser's second TX bar is showing. SWR and ALC are
    // polled unconditionally, so only Comp/Vd/Id need a poll of their own —
    // adding all three permanently would just starve the slower polls.
    funcs txMeterFunc = funcNone;

    // Locally tracked active VFO/receiver. Mirrors cachingQueue::rigState.vfo
    // but is read/written entirely on webThread, so receiveCache() can route
    // funcSelectedFreq/funcUnselectedFreq without re-locking the queue mutex
    // (see issue #31 / commit 126f0f0c for the deadlock this avoids).
    // Updated from: WebSocket selectVFO handler (predictively), incoming
    // funcSelectVFO/funcVFOMainSelect/funcVFOSubSelect cache events, and
    // reconciled from queue->getState() inside sendPeriodicStatus().
    vfo_t activeVfoLocal = vfoA;
    uchar activeReceiver = 0;
    // Mirror of "the rig is in memory mode", kept next to activeVfoLocal for
    // the same reason: receiveCache() must not call queue->getState(). In
    // memory mode the frequency replies describe the recalled channel, not
    // either VFO, so they must not land in the browser's VFO A/B slots (#108).
    bool memModeLocal = false;

    std::atomic<int> webListen_{ListenPending};
    std::atomic<int> restListen_{ListenPending};
    std::atomic<bool> httpsActive_{false};
    std::atomic<int> browserCount_{0};

    // SSL
    bool sslEnabled = false;
    QSslCertificate sslCert;
    QList<QSslCertificate> sslChain;
    QSslKey sslKey;
    // A configuration the server cannot run with (a certificate file that is
    // not there, --shared without TLS): logged, then the process exits.
    void fatalConfig(const QString &message);
    QJsonObject accessJson(const AccessPrincipal &who) const;
    QString tlsCertFile_;              // --cert / Web/CertFile; empty = generated self-signed
    QString tlsKeyFile_;
    std::atomic<bool> selfSigned_{true};
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
    // Browser TX frames are far smaller than the device's period: the mic
    // worklet posts one 128-sample quantum (2.7 ms) per WebSocket message,
    // the FT8/JS8 pacer 512 samples.  Qt's WinMM backend gives every
    // write() its own 40 ms wave block whatever its size and drops what it
    // cannot place, so writing them one by one starved the device of all
    // but ~13 ms in every 200 ms and chopped the speech (#136).  Frames
    // queue here and usbTxPump writes them one full period at a time.
    QByteArray usbTxPending;          // mono int16 LE awaiting a full period
    QTimer *usbTxPumpTimer = nullptr; // keeps draining once the device was full
    qint64 usbTxLastFrameMs = 0;      // wall clock of the last browser frame
    bool usbTxOverflowLogged = false;

    // Single-writer rule for browser TX audio: wall-clock of the last
    // modem-tagged (header byte 1 & 0x01) frame.  Untagged mic frames are
    // dropped while within 300 ms of this — see onWsBinaryMessage().
    qint64 lastModemAudioMs = 0;

    // SHA1 fingerprint (first 12 hex chars) of the embedded SPA assets.
    // Sent in getStatus replies so an open tab can detect a server upgrade
    // on WS reconnect and reload itself — see init() and index.html.
    QString spaBuildId;

    // DATA MOD OFF auto-switch to USB when web mic is active
    rigInput savedDataOffMod;
    bool dataOffModSaved = false;
    // Speech compressor taken out for wfweb's own digital TX (#147): the
    // over runs in plain USB/LSB, where the rig keeps the voice COMP on.
    // compRestoreOn = turn it back on when the over ends.
    bool compHeld = false;
    bool compRestoreOn = false;
    // The rig's SSB transmit filter (TBW, CI-V 16 58) held at WIDE for the
    // same overs. tbwAwait = its setting is being read; tbwRestore = the
    // setting to put back (1 MID, 2 NAR), -1 when there is nothing to undo.
    bool tbwHeld = false;
    bool tbwAwait = false;
    int tbwRestore = -1;
    int tbwPhase = 0;          // step of the hold's read, see tbwHoldPoll()
    QDateTime tbwAskedAt;
    QWebSocket *micActiveClient = nullptr;
    QWebSocket *pttClient = nullptr;   // browser that last keyed the rig; unkeyed if it vanishes (#144)

    // Browser-mic diagnostics.  A browser whose default input is the rig's
    // own USB codec streams the receiver audio back into the transmitter,
    // which is silent on the air, and nothing in the log said so.  The
    // browser now names the device it captures with enableMic; the server
    // logs it, warns (log + micWarning to the client) when it is the rig's
    // capture device, and logs the level of the mic frames it receives
    // every 5 s and at session end so a silent source is visible too.
    QString rigCaptureDeviceName;   // the input device setupUsbAudio() picked
    QString micDeviceLabel;         // what the browser said it captures from
    qint64 micStatBytes = 0;
    int micStatPeak = 0;
    qint64 micStatLastLogMs = 0;
    void micStatsAccumulate(const QByteArray &pcm);
    void micStatsLog(const char *why);

    // Pick the DATA MOD OFF input matching our audio transport: "LAN"/"WLAN"
    // by name when LAN-connected, otherwise USB by type (see issue #72 —
    // the LAN input's numeric type is inconsistent across .rig files).
    // Returns false if rigCaps lists no suitable input.
    bool pickDataModInput(rigInput &out) const;
    funcs micLevelFunc() const;     // USB or LAN mod level: the MIC slider's
    // Queue the DATA MOD OFF switch for a packet TX.  Every packet keying
    // site must call this before PTT: unlike the mic path there is no
    // enableMic step, so nothing else routes the modulator to the audio
    // transport — without it the radio keys a dead carrier (silent FM).
    void queuePacketModInput();
    void holdCompressorForDigitalTx(const char *why);
    void releaseCompressorHold();
    void holdTxBandwidth(const char *why);
    void tbwHoldPoll(const char *why, int tries);
    void releaseTxBandwidth();
    bool packetModInputLogged = false;

    // One-shot log guard for the LAN web-mic TX path (issue #72): logs once per
    // mic session that browser frames are arriving and being coalesced.
    bool micLanTxLogged = false;

    // LAN web-mic TX coalescing (issue #72): the browser streams tiny
    // 128-sample frames; the rig's UDP audio jitter buffer wants ~20 ms
    // chunks (as wfview's mic and the packet modem send). Accumulate browser
    // frames here and emit 20 ms slices. micLanTxChunkBytes is the 20 ms size
    // at the rig sample rate, computed lazily on the first frame.
    QByteArray micLanTxBuffer;
    int micLanTxChunkBytes = 0;

    // LAN pacer for the voice modems (FreeDV / RADE, issue #158).  The
    // encoders hand over a whole modem frame at a time (RADE: 120 ms) and a
    // LAN rig takes TX audio as 20 ms slices: with the frame sent as one
    // block RADE keyed the IC-705 and IC-9700 with no modulation at all.
    // Frames queue here and modemLanTxDrain() sends the slices on the
    // wall clock.
    QByteArray modemLanTxBuffer;
    QTimer *modemLanTxTimer = nullptr;
    QElapsedTimer modemLanTxClock;
    qint64 modemLanTxSent = 0;      // bytes sent since the clock started
    bool modemLanTxLogged = false;  // one line per mic session
    int modemLanTxSlices = 0;       // sent this session, for the closing log line
    int modemLanTxStarts = 0;       // clock starts: 1 + the times it ran dry
    int lanTxChunkBytes() const;    // 20 ms of mono PCM at the rig rate
    void modemLanTxQueue(const QByteArray &pcm);
    void modemLanTxReset();
    int modemLanTxPendingMs() const;

    // FreeDV processing
    FreeDVProcessor *freedvProcessor = nullptr;
    QThread *freedvThread = nullptr;
    FreeDVReporter *freedvReporter = nullptr;
    PskReporter *pskReporter = nullptr;
    QString reporterCallsign;
    QString reporterGrid;
    QMap<int, QString> cwMacros_;       // slot (1..10) -> macro text
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
    QTimer *packetTxIdleWatchdog = nullptr;  // backstop: some Qt5/ALSA
                                        // configurations (raw hw: devices
                                        // opened via nearestFormat) never
                                        // emit IdleState — fire the unkey
                                        // anyway so PTT can't latch on.

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
    QString instanceName_;
    Logbook logbook_;
    bool logbookPersistent_ = true;   // false: container without a volume, file dies with it
    QString sstvDir_;                 // received SSTV pictures, <id>.png + <id>.json; empty = unavailable
    bool sstvPersistent_ = true;
    QUdpSocket *wsjtxSocket_ = nullptr;
    QHostAddress wsjtxAddress_;
    quint16 wsjtxPort_ = 2237;
    bool wsjtxEnabled_ = false;
    bool wsjtxDecodes_ = false;
    QTimer *wsjtxHeartbeatTimer_ = nullptr;
    QString wsjtxId_ = QStringLiteral("wfweb");
    QString wsjtxTarget_;
    // Two browsers decoding the same audio would forward the same decode
    // twice; remember what went out recently, keyed on time+df+text.
    QHash<QString, qint64> wsjtxDecodesSent_;
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
    QSet<quint32> memoryScanSeen;         // keys reported by the running scan
    bool memoryScanActive = false;
    int memoryScanCurrent = 0;
    int memoryScanEnd = 0;
    int memoryScanGroup = 0;
    QTimer *memoryScanTimer = nullptr;
    QJsonObject memoryToJson(const memoryType &mem);
    QString modeRegToString(quint8 reg) const;
    void scanNextMemory();
    bool recallMemoryOnRig(int channel, int group, QString *error = nullptr);
    bool memoryContentsSupported() const;
    void sendMemoryError(QWebSocket *client, const QString &error);

    // wfweb's own channels, for rigs that can't store one (issue #114).
    // Used exactly when memoryContentsSupported() is false, so the two paths
    // never both apply and the browser talks the same protocol to either.
    MemoryStore memStore;
    QJsonObject localMemoryToJson(const localMemory &m) const;
    void sendLocalMemories(QWebSocket *client);
    bool writeLocalMemory(int channel, const QString &name, QString *error);
    bool recallLocalMemory(int channel, QString *error);
    // Replay a stored frequency + mode onto the current VFO. Shared by the
    // local-memory recall and by recallMemoryOnRig()'s no-select-command
    // fallback, which does exactly the same thing from the rig's own cache.
    void applyMemoryToVfo(qint64 hz, quint8 modeReg, quint8 filter, quint8 datamode);

    // setBand write-back: the band stacking register being left and the VFO
    // state to store in it. Filled by the setBand handler, consumed by the
    // matching 1A 01 reply in receiveCache() (read-modify-write).
    struct { int bsr = 0; int reg = 0; qint64 hz = 0; quint8 mode = 0; quint8 filter = 0; } pendingBsrSave;
    // Set right before queueing the write-back: queue->add() echoes the
    // value through receiveCache() synchronously, and that echo carries a
    // real frequency, so it is otherwise indistinguishable from a reply.
    bool bsrWriteEchoPending = false;

    // Repeater access tone (TONE / TSQL / DTCS). Rigs speak one of two
    // dialects — the single "Tone Squelch Type" register (IC-705/9700/905) or
    // independent TONE/TSQL/DTCS booleans (IC-7300/7610/…) — and these two
    // helpers hide the difference behind one rptAccessTxRx_t.
    // Tone state is mirrored into these members by receiveCache() rather than
    // read back out of the queue on demand. receiveCache() can run
    // *synchronously inside* cachingQueue::receiveValue() — Qt picks a direct
    // connection when the emit happens on the receiver's own thread, which is
    // exactly what applyToneMode() does — and the queue mutex is held for the
    // duration. Calling queue->getCache() from there deadlocks.
    rptAccessTxRx_t toneModeCache = ratrNN;   // the 0x16 0x5D dialect
    bool toneFlagTone = false;                // the 0x16 0x42/0x43/0x4B dialect
    bool toneFlagTsql = false;
    bool toneFlagDtcs = false;
    rptAccessTxRx_t currentToneMode() const;
    void applyToneMode(rptAccessTxRx_t mode);
    // The boolean dialect needs three writes (and answers three reads) for one
    // mode, so the folded value is briefly nonsense — TONE on its way to TSQL
    // reads as TONE(T)/TSQL(R) in between. Clients are told once things settle.
    int slowPollTick = 0;        // 5 s tick in sendPeriodicStatus (duplex offset, tuner status, AGC)
    QTimer *toneModeNotifyTimer = nullptr;
    void scheduleToneModeNotify();
    void addToneCaps(QJsonObject &o) const;
    void addAgcCaps(QJsonObject &o) const;
    bool hasAgc() const;
    // The rig keeps an AGC setting per mode, so a mode change makes the
    // cached one stale: the last mode seen, to re-read AGC when it changes.
    int agcModeSeen = -1;
    void agcFollowMode(const cacheItem &item);
    void addBandCaps(QJsonObject &o) const;
    void addTxMeterCaps(QJsonObject &o) const;
    void addTxMeterReadings(QJsonObject &o) const;
    void applyTxMeterPoll();
    void addToneStatus(QJsonObject &o);
    bool toneCommandsAvailable() const;
};

#endif // WEBSERVER_H
