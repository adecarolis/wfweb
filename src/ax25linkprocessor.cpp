#include "ax25linkprocessor.h"

#include <QDebug>
#include <QLoggingCategory>

#include <cstring>

extern "C" {
#include "direwolf.h"
#include "ax25_pad.h"
#include "ax25_link.h"
#include "dlq.h"
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
    s_misc_config.frack             = 4;
    s_misc_config.retry             = 10;
    s_misc_config.paclen            = 128;
    s_misc_config.maxframe_basic    = 4;
    s_misc_config.maxframe_extended = 32;
    s_misc_config.maxv22            = 3;

    ax25_link_init(&s_misc_config, /*debug=*/0, /*stats=*/0);
    tq_init(nullptr);
    dlq_init(0);

    s_instance = this;
    wfweb_dw_register_server_callbacks(&cbLinkEstablished,
                                       &cbLinkTerminated,
                                       &cbRecConnData,
                                       &cbOutstanding,
                                       &cbCallsignLookup);
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
    wfweb_dw_register_tq_callbacks(nullptr, nullptr);
    s_instance = nullptr;

    qCInfo(logAx25) << "AX25LinkProcessor stopped";
}

// Pack a QStringList of [peer, digi1, digi2, ...] into the fixed-size
// AX.25 address array Dire Wolf expects.  num_addr returns 2 + #digis
// (slot 0 = our call, slot 1 = peer, slots 2.. = path).
static int packAddresses(const QString &ownCall, const QString &peerCall,
                         const QStringList &digis,
                         char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN])
{
    std::memset(addrs, 0, sizeof(char) * AX25_MAX_ADDRS * AX25_MAX_ADDR_LEN);

    // ax25_link uses AX25_SOURCE (0) for own call, AX25_DESTINATION (1) for peer.
    // (See OWNCALL/PEERCALL #defines in ax25_link.c.)
    auto put = [&](int slot, const QString &call) {
        QByteArray ba = call.toUpper().toLatin1();
        int n = qMin(ba.size(), AX25_MAX_ADDR_LEN - 1);
        std::memcpy(addrs[slot], ba.constData(), n);
        addrs[slot][n] = '\0';
    };

    put(0, ownCall);
    put(1, peerCall);
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
