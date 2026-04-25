#ifndef AX25LINKPROCESSOR_H
#define AX25LINKPROCESSOR_H

#include <QObject>
#include <QByteArray>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <thread>

/*
 *  AX25LinkProcessor
 *
 *  Wraps the vendored Dire Wolf data-link state machine (ax25_link.c +
 *  dlq.c) in a Qt-friendly object.  Owns the dispatch thread that drains
 *  the DLQ, services link timers, and routes events into ax25_link.
 *
 *  Slots are safe to call from any thread (they enqueue onto the DLQ,
 *  which has its own pthread mutex).  Signals are emitted from the
 *  dispatch thread; receivers connect with auto/queued connections.
 *
 *  C-side trampolines installed via wfweb_dw_register_*_callbacks
 *  forward server_link_*, server_rec_conn_data, lm_data_request, and
 *  lm_seize_request into this object via a singleton pointer.
 */
class AX25LinkProcessor : public QObject
{
    Q_OBJECT
public:
    explicit AX25LinkProcessor(QObject *parent = nullptr);
    ~AX25LinkProcessor() override;

    static AX25LinkProcessor *instance();

public slots:
    // Boots ax25_link, dlq, tq; installs C trampolines; spawns dispatcher.
    void start();
    // Stops dispatcher and clears C trampolines.  Safe to call from any thread.
    void stop();

    // Client-app operations.  `client` is a host-assigned id (one per
    // open terminal session, typically).  `digis` is an ordered list of
    // digipeater callsigns; empty for direct.
    void connectRequest   (int client, int chan, const QString &ownCall,
                           const QString &peerCall, const QStringList &digis);
    void disconnectRequest(int client, int chan, const QString &ownCall,
                           const QString &peerCall, const QStringList &digis);
    void sendData         (int client, int chan, const QString &ownCall,
                           const QString &peerCall, const QStringList &digis,
                           int pid, const QByteArray &data);
    void registerCallsign (int client, int chan, const QString &callsign);
    void unregisterCallsign(int client, int chan, const QString &callsign);
    void clientCleanup    (int client);
    void outstandingFramesRequest(int client, int chan,
                                  const QString &ownCall,
                                  const QString &peerCall);

    // Adjust link-layer timing for the current modem baud rate.  At 300 bd
    // a SABM is ~1.4 s of airtime; doubling T1 (frack) keeps the default
    // 10 retries from triggering before the peer can reply.  Called from
    // webServer on every packetSetMode.  Also resizes paclen to a value
    // appropriate for the link (small on HF, large on VHF/UHF).
    void setLinkParamsForBaud(int baud);

    // Snapshot of the AX.25 paclen currently in effect.  Thread-safe;
    // YAPP uses this to size DT chunks so they fit one I-frame.  Returns
    // the static-init default before setLinkParamsForBaud is first called.
    static int currentPaclen();

    // Channel busy/idle from carrier sense (optional; carrier-sense not
    // wired in M2 but the slot is here for future use).
    void channelBusy(int chan, int activity, int status);

    // Confirms PTT is up and modem is idle so ax25_link can drain its
    // pending I-frames.  M2 still relies on the wfweb_tq fast-path
    // (immediate confirm) — this slot is the future hook for the real
    // PTT integration.
    void seizeConfirm(int chan);

signals:
    void linkEstablished(int client, int chan,
                         const QString &peerCall, const QString &ownCall,
                         bool incoming);
    void linkTerminated (int client, int chan,
                         const QString &peerCall, const QString &ownCall,
                         int timeout);
    void rxData         (int client, int chan,
                         const QString &peerCall, const QString &ownCall,
                         int pid, const QByteArray &data);
    void outstandingFrames(int client, int chan,
                           const QString &ownCall, const QString &peerCall,
                           int count);
    // Fired when N(R) advances and `count` outbound I-frames are now
    // acknowledged by the peer.  Drives the terminal UI's transition
    // of TX text from "pending" to "delivered".
    void dataAcked        (int client, int chan,
                           const QString &ownCall, const QString &peerCall,
                           int count);

    // Connected-mode TX: an AX.25 frame (full HDLC payload sans flags
    // and FCS — exactly what ax25_pack produces) ready for the modem.
    void transmitFrameBytes(int chan, int prio, const QByteArray &frame);

    // Channel-seize request (M5 wires this through PTT).
    void seizeRequested(int chan);

private:
    void dispatcherLoop();

    // Resolve a (callsign, chan) pair to the client that registered it.
    // Used by cbCallsignLookup (called from dispatcher thread) to decide
    // whether to accept an inbound SABM.  -1 = not registered.
    int  lookupRegisteredCallsign(const char *callsign);

    // If the first unrepeated digipeater in the frame's path matches one
    // of our registered callsigns, set its H bit and re-emit the packet.
    // Called from the dispatcher loop after lm_data_indication so the
    // local state machine sees the original frame unmodified.
    void maybeDigipeatFrame(struct dlq_item_s *E);

    static AX25LinkProcessor *s_instance;

    // C callbacks (installed once in start()).
    static void cbLinkEstablished(int chan, int client, const char *remote,
                                  const char *own, int incoming);
    static void cbLinkTerminated (int chan, int client, const char *remote,
                                  const char *own, int timeout);
    static void cbRecConnData    (int chan, int client, const char *remote,
                                  const char *own, int pid,
                                  const char *data, int len);
    static void cbOutstanding    (int chan, int client, const char *own,
                                  const char *remote, int count);
    static void cbDataAcked      (int chan, int client, const char *own,
                                  const char *remote, int count);
    static int  cbCallsignLookup (const char *callsign);
    // Signature must match wfweb_tq_data_cb: takes a packet_t (which is
    // the same pointer the upstream layer hands to lm_data_request).
    // We forward-declare the struct in extern "C" to avoid pulling
    // ax25_pad.h into this header.
    static void cbTxFrame        (int chan, int prio, struct packet_s *pp);
    static void cbSeizeRequest   (int chan);

    std::thread       worker_;
    std::atomic<bool> running_{false};

    struct Registration {
        QString call;
        int     chan;
        int     client;
    };
    QVector<Registration> registrations_;
    QMutex                regMutex_;
};

#endif // AX25LINKPROCESSOR_H
