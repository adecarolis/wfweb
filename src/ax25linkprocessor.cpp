#include "ax25linkprocessor.h"
#include "direwolfprocessor.h"

#include <QDebug>
#include <QLoggingCategory>

#include <cstring>

extern "C" {
#include "direwolf.h"
#include "ax25_pad.h"
#include "ax25_link.h"
#include "tq.h"
#include "config.h"
#include "wfweb_dw_server_shim.h"
#include "wfweb_tq.h"
}

Q_LOGGING_CATEGORY(logAx25, "ax25.link")

AX25LinkProcessor *AX25LinkProcessor::s_instance = nullptr;

// Static config kept for the lifetime of the process — ax25_link_init()
// stores the pointer and reads from it on every link state change.
static struct misc_config_s s_misc_config;

// Mirror of s_misc_config.paclen, exposed thread-safely so YAPP (running
// on the webserver thread) can size its DT chunks to fit the current
// link's max info field.  Updated whenever setLinkParamsForBaud runs.
static std::atomic<int> s_currentPaclen{128};

AX25LinkProcessor::AX25LinkProcessor(QObject *parent)
    : QObject(parent)
{
}

AX25LinkProcessor::~AX25LinkProcessor()
{
    stop();
}

AX25LinkProcessor *AX25LinkProcessor::instance() { return s_instance; }

void AX25LinkProcessor::start()
{
    if (running_.load()) return;

    // Conservative defaults that match Dire Wolf's documented defaults.
    // The web UI may override these later via prefs.
    std::memset(&s_misc_config, 0, sizeof(s_misc_config));
    s_misc_config.frack             = 4;     // updated dynamically by setLinkParamsForBaud
    s_misc_config.retry             = 10;
    s_misc_config.paclen            = 128;
    s_currentPaclen.store(s_misc_config.paclen);
    // k_maxframe = 1 gives strict one-frame-one-ACK ping-pong, which is the
    // classic BBS-era behaviour most packet operators expect: each I-frame
    // keys the radio briefly, the peer ACKs, the channel is free for others.
    // Larger windows (up to 7 v2.0, 127 v2.2) give higher throughput by
    // pipelining, but then ACKs get batched at the end of a burst.
    s_misc_config.maxframe_basic    = 1;
    s_misc_config.maxframe_extended = 1;
    // maxv22 = 0 disables v2.2/SABME — forces plain v2.0, modulo 8, so we
    // don't get the v2.2 extended window behaviour sneaking back in.
    s_misc_config.maxv22            = 0;

    ax25_link_init(&s_misc_config, /*debug=*/0, /*stats=*/0);
    tq_init(nullptr);
    // Shared once-flag with DireWolfProcessor::init(); dlq_init() is not
    // safe to call twice (pthread_mutex_init on an initialized mutex).
    DireWolfProcessor::ensureDlqInitialized();

    s_instance = this;
    wfweb_dw_register_server_callbacks(&cbLinkEstablished,
                                       &cbLinkTerminated,
                                       &cbRecConnData,
                                       &cbOutstanding,
                                       &cbCallsignLookup);
    wfweb_dw_register_data_acked_cb(&cbDataAcked);
    wfweb_dw_register_tq_callbacks(&cbTxFrame, &cbSeizeRequest);

    running_.store(true);
    worker_ = std::thread(&AX25LinkProcessor::dispatcherLoop, this);

    qCInfo(logAx25) << "AX25LinkProcessor started";
}

void AX25LinkProcessor::stop()
{
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();

    wfweb_dw_register_server_callbacks(nullptr, nullptr, nullptr, nullptr, nullptr);
    wfweb_dw_register_data_acked_cb(nullptr);
    wfweb_dw_register_tq_callbacks(nullptr, nullptr);
    s_instance = nullptr;

    qCInfo(logAx25) << "AX25LinkProcessor stopped";
}

// Pack into the fixed-size AX.25 address array Dire Wolf expects.
// AX.25 slot layout (from ax25_pad.h):
//   slot 0 = AX25_DESTINATION  (PEERCALL in ax25_link)
//   slot 1 = AX25_SOURCE       (OWNCALL  in ax25_link)
//   slots 2..9 = AX25_REPEATER_1..8 (digipeater path, in order)
// num_addr returns 2 + #digis.
static int packAddresses(const QString &ownCall, const QString &peerCall,
                         const QStringList &digis,
                         char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN])
{
    std::memset(addrs, 0, sizeof(char) * AX25_MAX_ADDRS * AX25_MAX_ADDR_LEN);

    auto put = [&](int slot, const QString &call) {
        QByteArray ba = call.toUpper().toLatin1();
        int n = qMin(ba.size(), AX25_MAX_ADDR_LEN - 1);
        std::memcpy(addrs[slot], ba.constData(), n);
        addrs[slot][n] = '\0';
    };

    put(0, peerCall);   // AX25_DESTINATION / PEERCALL
    put(1, ownCall);    // AX25_SOURCE      / OWNCALL
    int n = 2;
    for (const QString &d : digis) {
        if (n >= AX25_MAX_ADDRS) break;
        put(n++, d);
    }
    return n;
}

void AX25LinkProcessor::connectRequest(int client, int chan,
                                       const QString &ownCall,
                                       const QString &peerCall,
                                       const QStringList &digis)
{
    char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN];
    int n = packAddresses(ownCall, peerCall, digis, addrs);
    dlq_connect_request(addrs, n, chan, client, /*pid*/ 0xF0);
}

void AX25LinkProcessor::disconnectRequest(int client, int chan,
                                          const QString &ownCall,
                                          const QString &peerCall,
                                          const QStringList &digis)
{
    char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN];
    int n = packAddresses(ownCall, peerCall, digis, addrs);
    dlq_disconnect_request(addrs, n, chan, client);
}

void AX25LinkProcessor::sendData(int client, int chan,
                                 const QString &ownCall,
                                 const QString &peerCall,
                                 const QStringList &digis,
                                 int pid, const QByteArray &data)
{
    char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN];
    int n = packAddresses(ownCall, peerCall, digis, addrs);
    // dlq_xmit_data_request copies the buffer into a cdata_t, so the
    // QByteArray can safely go out of scope after this call returns.
    dlq_xmit_data_request(addrs, n, chan, client, pid,
                          const_cast<char *>(data.constData()), data.size());
}

void AX25LinkProcessor::registerCallsign(int client, int chan, const QString &callsign)
{
    {
        QMutexLocker lock(&regMutex_);
        registrations_.append({callsign.toUpper(), chan, client});
    }
    QByteArray ba = callsign.toUpper().toLatin1();
    dlq_register_callsign(ba.data(), chan, client);
}

void AX25LinkProcessor::unregisterCallsign(int client, int chan, const QString &callsign)
{
    {
        QMutexLocker lock(&regMutex_);
        QString want = callsign.toUpper();
        for (int i = registrations_.size() - 1; i >= 0; --i) {
            if (registrations_[i].client == client &&
                registrations_[i].chan == chan &&
                registrations_[i].call == want) {
                registrations_.remove(i);
            }
        }
    }
    QByteArray ba = callsign.toUpper().toLatin1();
    dlq_unregister_callsign(ba.data(), chan, client);
}

void AX25LinkProcessor::clientCleanup(int client)
{
    {
        QMutexLocker lock(&regMutex_);
        for (int i = registrations_.size() - 1; i >= 0; --i) {
            if (registrations_[i].client == client) registrations_.remove(i);
        }
    }
    dlq_client_cleanup(client);
}

void AX25LinkProcessor::outstandingFramesRequest(int client, int chan,
                                                 const QString &ownCall,
                                                 const QString &peerCall)
{
    char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN];
    int n = packAddresses(ownCall, peerCall, {}, addrs);
    dlq_outstanding_frames_request(addrs, n, chan, client);
}

void AX25LinkProcessor::channelBusy(int chan, int activity, int status)
{
    dlq_channel_busy(chan, activity, status);
}

void AX25LinkProcessor::setLinkParamsForBaud(int baud)
{
    // T1 (frack) sized so two full transmit cycles + processing fit before
    // the first retry.  Numbers are for no-digi paths; ax25_link multiplies
    // by (2*num_digis + 1) internally.
    int frack;
    int paclen;
    switch (baud) {
    case 300:
        // HF: noisy, slow.  Common HF practice (SV1BSX guide, Modern-Ham
        // BPQ configs) is paclen=32 or 64; pick 64 for usable throughput
        // while keeping per-frame airtime under ~2 s so a single bit
        // error doesn't waste a long send.
        frack  = 10;  // ~1.4 s airtime per SABM, leave room
        paclen = 64;
        break;
    case 1200:
        // VHF FM: clean and fast.  Classic 128 fits one I-frame in the
        // typical FM-packet airtime budget.
        frack  = 4;   // ~225 ms airtime, default works fine
        paclen = 128;
        break;
    case 9600:
        // VHF/UHF FSK: very clean and fast.  256 is the de-facto default.
        frack  = 3;
        paclen = 256;
        break;
    default:
        frack  = 4;
        paclen = 128;
        break;
    }
    s_misc_config.frack  = frack;
    s_misc_config.paclen = paclen;
    s_currentPaclen.store(paclen);
    qCInfo(logAx25) << "Link params: baud=" << baud
                    << "frack=" << frack
                    << "paclen=" << paclen;
}

int AX25LinkProcessor::currentPaclen()
{
    return s_currentPaclen.load();
}

void AX25LinkProcessor::seizeConfirm(int chan)
{
    dlq_seize_confirm(chan);
}

int AX25LinkProcessor::lookupRegisteredCallsign(const char *callsign)
{
    if (!callsign) return -1;
    QString want = QString::fromLatin1(callsign).toUpper();
    QMutexLocker lock(&regMutex_);
    for (const Registration &r : registrations_) {
        if (r.call == want) return r.client;
    }
    return -1;
}

void AX25LinkProcessor::maybeDigipeatFrame(struct dlq_item_s *E)
{
    if (!E || !E->pp) return;

    int slot = ax25_get_first_not_repeated(E->pp);
    if (slot < 0) return;   // no unrepeated digis

    char call[AX25_MAX_ADDR_LEN];
    std::memset(call, 0, sizeof(call));
    ax25_get_addr_with_ssid(E->pp, slot, call);
    QString callQ = QString::fromLatin1(call).toUpper();

    bool isOurs = false;
    {
        QMutexLocker lock(&regMutex_);
        for (const Registration &r : registrations_) {
            if (r.call == callQ) { isOurs = true; break; }
        }
    }
    if (!isOurs) return;

    // Flip the H bit on the slot, re-pack, and hand to the CSMA queue.
    // We modify E->pp in place here — safe because this runs AFTER
    // lm_data_indication in the dispatcher loop, and dlq_delete will
    // free the packet regardless.
    ax25_set_h(E->pp, slot);

    unsigned char frame[AX25_MAX_PACKET_LEN];
    int flen = ax25_pack(E->pp, frame);
    if (flen <= 0) return;

    QByteArray ba(reinterpret_cast<const char *>(frame), flen);
    emit transmitFrameBytes(E->chan, /*prio*/ 0, ba);
    qCInfo(logAx25) << "Digipeat via" << callQ
                    << "chan=" << E->chan << "len=" << flen;
}

// ----- C trampolines -----

extern "C" void wfweb_dw_rx_frame(int chan, int subchan, int slice,
                                  const unsigned char *ax25, int len,
                                  int alevel_rx_biased, int alevel_mark,
                                  int alevel_space, int fec_type, int retries);

void AX25LinkProcessor::dispatcherLoop()
{
    while (running_.load()) {
        // Wait for either an event or the next pending link timer.  On a
        // 0-return ("event"), we still call dl_timer_expiry — it costs
        // nothing when no timer is due.
        double tnext = ax25_link_get_next_timer_expiry();
        dlq_wait_while_empty(tnext);
        if (!running_.load()) break;

        dl_timer_expiry();

        dlq_item_t *E = dlq_remove();
        if (E == nullptr) continue;

        switch (E->type) {
        case DLQ_REC_FRAME:
            if (E->pp != nullptr) {
                // 1) APRS UI monitor path — same shape the old stub fed
                //    DireWolfProcessor.  Doing this before lm_data_indication
                //    means the UI sees every frame, including those that
                //    later get consumed by the connected-mode SM.
                unsigned char frame[AX25_MAX_PACKET_LEN];
                int flen = ax25_pack(E->pp, frame);
                if (flen > 0) {
                    wfweb_dw_rx_frame(E->chan, E->subchan, E->slice,
                                      frame, flen,
                                      E->alevel.rec, E->alevel.mark,
                                      E->alevel.space,
                                      E->fec_type, E->retries);
                }
                // 2) Connected-mode dispatch.
                lm_data_indication(E);
                // 3) Digipeat if we're the next unused hop.  Runs after
                //    local dispatch so the SM sees the unmodified frame.
                maybeDigipeatFrame(E);
            }
            break;
        case DLQ_CONNECT_REQUEST:            dl_connect_request(E); break;
        case DLQ_DISCONNECT_REQUEST:         dl_disconnect_request(E); break;
        case DLQ_XMIT_DATA_REQUEST:          dl_data_request(E); break;
        case DLQ_REGISTER_CALLSIGN:          dl_register_callsign(E); break;
        case DLQ_UNREGISTER_CALLSIGN:        dl_unregister_callsign(E); break;
        case DLQ_OUTSTANDING_FRAMES_REQUEST: dl_outstanding_frames_request(E); break;
        case DLQ_CHANNEL_BUSY:               lm_channel_busy(E); break;
        case DLQ_SEIZE_CONFIRM:              lm_seize_confirm(E); break;
        case DLQ_CLIENT_CLEANUP:             dl_client_cleanup(E); break;
        }

        dlq_delete(E);
    }
}

// All of these run on the dispatcher thread, so emitting Qt signals
// will use queued connections to reach the webserver thread.

void AX25LinkProcessor::cbLinkEstablished(int chan, int client,
                                          const char *remote, const char *own,
                                          int incoming)
{
    if (!s_instance) return;
    emit s_instance->linkEstablished(client, chan,
                                     QString::fromLatin1(remote ? remote : ""),
                                     QString::fromLatin1(own    ? own    : ""),
                                     incoming != 0);
}

void AX25LinkProcessor::cbLinkTerminated(int chan, int client,
                                         const char *remote, const char *own,
                                         int timeout)
{
    if (!s_instance) return;
    emit s_instance->linkTerminated(client, chan,
                                    QString::fromLatin1(remote ? remote : ""),
                                    QString::fromLatin1(own    ? own    : ""),
                                    timeout);
}

void AX25LinkProcessor::cbRecConnData(int chan, int client,
                                      const char *remote, const char *own,
                                      int pid, const char *data, int len)
{
    if (!s_instance) return;
    emit s_instance->rxData(client, chan,
                            QString::fromLatin1(remote ? remote : ""),
                            QString::fromLatin1(own    ? own    : ""),
                            pid,
                            QByteArray(data, len));
}

void AX25LinkProcessor::cbOutstanding(int chan, int client,
                                      const char *own, const char *remote,
                                      int count)
{
    if (!s_instance) return;
    emit s_instance->outstandingFrames(client, chan,
                                       QString::fromLatin1(own    ? own    : ""),
                                       QString::fromLatin1(remote ? remote : ""),
                                       count);
}

void AX25LinkProcessor::cbDataAcked(int chan, int client,
                                    const char *own, const char *remote,
                                    int count)
{
    if (!s_instance) return;
    emit s_instance->dataAcked(client, chan,
                               QString::fromLatin1(own    ? own    : ""),
                               QString::fromLatin1(remote ? remote : ""),
                               count);
}

int AX25LinkProcessor::cbCallsignLookup(const char *callsign)
{
    if (!s_instance) return -1;
    return s_instance->lookupRegisteredCallsign(callsign);
}

void AX25LinkProcessor::cbTxFrame(int chan, int prio, struct packet_s *pp)
{
    if (pp == nullptr) return;

    unsigned char frame[AX25_MAX_PACKET_LEN];
    int flen = ax25_pack(pp, frame);
    QByteArray ba;
    if (flen > 0) ba = QByteArray(reinterpret_cast<const char *>(frame), flen);

    // wfweb_tq hands ownership of the packet_t to us — release it.
    ax25_delete(pp);

    qCInfo(logAx25) << "TX frame chan=" << chan << "prio=" << prio
                    << "bytes=" << flen;

    if (!s_instance || flen <= 0) return;
    emit s_instance->transmitFrameBytes(chan, prio, ba);
}

void AX25LinkProcessor::cbSeizeRequest(int chan)
{
    if (!s_instance) {
        // Fallback: keep the DLSM unblocked even with no host wiring.
        dlq_seize_confirm(chan);
        return;
    }
    emit s_instance->seizeRequested(chan);
    // M2: until the real PTT path is wired in M5, immediately confirm
    // so the DLSM can drain.  This matches the behaviour we had in
    // wfweb_tq's no-callback fallback.
    dlq_seize_confirm(chan);
}
