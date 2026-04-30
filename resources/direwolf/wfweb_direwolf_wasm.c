/*
 * wfweb_direwolf_wasm.c
 *
 * Emscripten-side glue for the vendored Direwolf modem subset. Mirrors the
 * Qt-side wfweb_direwolf_stubs.c, but everything that would have called
 * back into wfweb's C++ classes routes to JS via Emscripten EM_JS instead.
 *
 * The exported flat C API (wfweb_dw_*) is what direwolf-modem.js binds to
 * with cwrap(); in JS it looks like a regular module.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten.h>

#include "direwolf.h"
#include "audio.h"
#include "ax25_pad.h"
#include "demod.h"
#include "multi_modem.h"
#include "hdlc_send.h"
#include "gen_tone.h"
#include "textcolor.h"
#include "fsk_demod_state.h"
#include "demod_psk.h"
#include "ais.h"
#include "fx25.h"
#include "il2p.h"
#include "ax25_link.h"
#include "dlq.h"
#include "tq.h"
#include "config.h"
#include "wfweb_dw_server_shim.h"
#include "wfweb_tq.h"

/* --- JS callback trampolines ---------------------------------------------
 *
 * The browser side sets Module.onRxFrame and (during a TX call) reads
 * Module.txBuffer back. We expose plain functions so the C code stays
 * unaware of the JS bridge; EM_JS makes the JS body inline-able.
 */

EM_JS(void, wfweb_dw_emit_rx_frame, (int chan, int alevel, int ptr, int len), {
    if (typeof Module !== 'undefined' && typeof Module.onRxFrame === 'function') {
        var bytes = HEAPU8.slice(ptr, ptr + len);
        Module.onRxFrame(chan, alevel, bytes);
    }
});

/* TX scratch buffer — gen_tone calls audio_put one byte at a time. The
 * stream is little-endian int16 LE PCM at modemRate (48 kHz mono). We
 * accumulate into a growable buffer and let JS pull it after the call
 * returns via wfweb_dw_take_tx_buffer(). */
static unsigned char *tx_buf = NULL;
static size_t tx_buf_len = 0;
static size_t tx_buf_cap = 0;

static void tx_buf_append(unsigned char b) {
    if (tx_buf_len + 1 > tx_buf_cap) {
        size_t newcap = tx_buf_cap ? tx_buf_cap * 2 : 65536;
        while (newcap < tx_buf_len + 1) newcap *= 2;
        unsigned char *p = (unsigned char *)realloc(tx_buf, newcap);
        if (!p) return;
        tx_buf = p;
        tx_buf_cap = newcap;
    }
    tx_buf[tx_buf_len++] = b;
}

/* --- Direwolf shim symbols ----------------------------------------------- */

static int g_log_level = 0;

void text_color_init(int enable_color) { (void)enable_color; }
void text_color_set(dw_color_t c)      { g_log_level = (int)c; }
void text_color_term(void)             { }

int dw_printf(const char *fmt, ...)
{
    /* Direwolf's diagnostics are noisy and not useful for end users. Drop
     * them; wire to console.log later if WASM debugging requires it. */
    (void)fmt;
    return 0;
}

int audio_open(struct audio_s *pa)  { (void)pa; return 0; }
int audio_close(void)               { return 0; }
int audio_get(int a)                { (void)a; return 0; }
int audio_put(int a, int c)         { (void)a; tx_buf_append((unsigned char)(c & 0xff)); return c; }
int audio_flush(int a)              { (void)a; return 0; }
void audio_wait(int a)              { (void)a; }

void ptt_set_debug(int debug)       { (void)debug; }
void ptt_set(int ot, int chan, int signal) { (void)ot; (void)chan; (void)signal; }

/* FX.25 / IL2P FEC stubs (vendored modem subset doesn't ship them) */
void fx25_init(int debug_level)     { (void)debug_level; }
int  fx25_send_frame(int chan, unsigned char *fbuf, int flen, int fx_mode)
                                    { (void)chan; (void)fbuf; (void)flen; (void)fx_mode; return 0; }
void fx25_rec_bit(int chan, int subchan, int slice, int dbit)
                                    { (void)chan; (void)subchan; (void)slice; (void)dbit; }
int  fx25_rec_busy(int chan)        { (void)chan; return 0; }

void il2p_init(int debug)           { (void)debug; }
int  il2p_send_frame(int chan, packet_t pp, int max_fec, int polarity)
                                    { (void)chan; (void)pp; (void)max_fec; (void)polarity; return 0; }
void il2p_rec_bit(int chan, int subchan, int slice, int dbit)
                                    { (void)chan; (void)subchan; (void)slice; (void)dbit; }

/* PSK demod stub (only AFSK + 9600 FSK on this build) */
void demod_psk_init(enum modem_t modem_type, enum v26_e v26_alt,
                    int samples_per_sec, int bps, char profile,
                    struct demodulator_state_s *D)
{
    (void)modem_type; (void)v26_alt; (void)samples_per_sec;
    (void)bps; (void)profile; (void)D;
}
void demod_psk_process_sample(int chan, int subchan, int sam,
                              struct demodulator_state_s *D)
{
    (void)chan; (void)subchan; (void)sam; (void)D;
}

/* AIS stub */
void ais_to_nmea(unsigned char *ais, int ais_len, char *nmea, int nmea_size)
{
    (void)ais; (void)ais_len;
    if (nmea && nmea_size > 0) nmea[0] = '\0';
}
int ais_check_length(int type, int length) { (void)type; (void)length; return 0; }

int get_input(int it, int chan) { (void)it; (void)chan; return -1; }

/* dlq_rec_frame is provided by the real dlq.c (linked in for the
 * connected-mode build). Decoded frames land on the queue and are
 * processed by wfweb_dw_link_step() below. dlq_init() must be called
 * before the first decode — we do it lazily from both wfweb_dw_init()
 * and wfweb_dw_link_init() guarded by g_dlq_inited. */
static int g_dlq_inited = 0;
static void ensure_dlq_inited(void) {
    if (!g_dlq_inited) { dlq_init(0); g_dlq_inited = 1; }
}
int wfweb_dw_link_step(void);  /* forward-decl; defined further down */
/* Set to 1 by wfweb_dw_link_init when JS starts an Ax25Link.  Read by
 * wfweb_dw_process_samples to suppress the RX-path DLQ drain — when the
 * link is active, only the JS setInterval tick is allowed to drain the
 * DLQ, otherwise SABM/retry audio writes into tx_buf can be wiped by the
 * next tick's reset before JS reads them. Defined further down. */
static int g_link_initialized;

/* --- Flat exported API ----------------------------------------------------
 *
 * These functions are what direwolf-modem.js binds with cwrap(). They keep
 * the WASM module's surface area small: configure, push samples, encode a
 * frame, retrieve TX bytes.
 */

static struct audio_s g_cfg;
static int g_cfg_baud = 0;

EMSCRIPTEN_KEEPALIVE
int wfweb_dw_init(int baud, int sample_rate)
{
    if (baud != 300 && baud != 1200 && baud != 9600) return -1;

    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.adev[0].defined = 1;
    g_cfg.adev[0].copy_from = -1;
    g_cfg.adev[0].num_channels = 1;
    g_cfg.adev[0].samples_per_sec = sample_rate;
    g_cfg.adev[0].bits_per_sample = 16;

    int ch = 0;
    g_cfg.chan_medium[ch] = MEDIUM_RADIO;
    if (baud == 9600) {
        g_cfg.achan[ch].modem_type = MODEM_SCRAMBLE;
        g_cfg.achan[ch].mark_freq = 0;
        g_cfg.achan[ch].space_freq = 0;
        g_cfg.achan[ch].baud = 9600;
        strncpy(g_cfg.achan[ch].profiles, " ", sizeof(g_cfg.achan[ch].profiles) - 1);
    } else if (baud == 300) {
        g_cfg.achan[ch].modem_type = MODEM_AFSK;
        g_cfg.achan[ch].mark_freq = 1600;
        g_cfg.achan[ch].space_freq = 1800;
        g_cfg.achan[ch].baud = 300;
        strncpy(g_cfg.achan[ch].profiles, "A", sizeof(g_cfg.achan[ch].profiles) - 1);
    } else {
        g_cfg.achan[ch].modem_type = MODEM_AFSK;
        g_cfg.achan[ch].mark_freq = DEFAULT_MARK_FREQ;
        g_cfg.achan[ch].space_freq = DEFAULT_SPACE_FREQ;
        g_cfg.achan[ch].baud = 1200;
        strncpy(g_cfg.achan[ch].profiles, "A", sizeof(g_cfg.achan[ch].profiles) - 1);
    }
    g_cfg.achan[ch].num_freq = 1;
    g_cfg.achan[ch].offset = 0;
    g_cfg.achan[ch].fix_bits = RETRY_NONE;
    g_cfg.achan[ch].sanity_test = SANITY_APRS;
    g_cfg.achan[ch].passall = 0;
    g_cfg.achan[ch].layer2_xmit = LAYER2_AX25;
    g_cfg.achan[ch].dwait = DEFAULT_DWAIT;
    g_cfg.achan[ch].slottime = DEFAULT_SLOTTIME;
    g_cfg.achan[ch].persist = DEFAULT_PERSIST;
    g_cfg.achan[ch].txdelay = DEFAULT_TXDELAY;
    g_cfg.achan[ch].txtail = DEFAULT_TXTAIL;

    multi_modem_init(&g_cfg);
    gen_tone_init(&g_cfg, 100, 0);
    g_cfg_baud = baud;
    /* hdlc_rec → dlq_rec_frame → append_to_queue asserts dlq_init was
     * run; init it here so the modem-only path (no link layer) works. */
    ensure_dlq_inited();
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int wfweb_dw_baud(void) { return g_cfg_baud; }

/* Push a batch of int16 LE PCM samples through the demodulator. The buffer
 * lives in the WASM heap; JS already memcpy'd bytes there before the call.
 * When no Ax25Link is active we drain the DLQ here so modem-only callers
 * still see modem.onFrame fire. When the link IS active, JS drives
 * wfweb_dw_link_step on its own ~50 ms tick and owns tx_buf — draining
 * the DLQ from this RX path would let dl_connect_request (and timer-driven
 * SABM retries via dl_timer_expiry) write audio into tx_buf, only for the
 * next link tick's reset to wipe it before JS ever reads it. */
EMSCRIPTEN_KEEPALIVE
void wfweb_dw_process_samples(int ptr, int n_samples)
{
    const short *p = (const short *)(intptr_t)ptr;
    for (int i = 0; i < n_samples; i++) {
        multi_modem_process_sample(0, (int)p[i]);
    }
    if (!g_link_initialized) wfweb_dw_link_step();
}

/* Encode a TNC-style "SRC>DST[,VIA,...]:info" UI frame and append the
 * resulting int16 LE PCM bytes to the global TX buffer.  Returns 0 on
 * success, -1 if ax25_from_text rejected the input. */
EMSCRIPTEN_KEEPALIVE
int wfweb_dw_tx_frame(const char *monitor)
{
    if (!monitor) return -1;
    /* ax25_from_text mutates its input — duplicate so we don't write into
     * Emscripten's read-only string memory. */
    char *dup = strdup(monitor);
    if (!dup) return -1;
    packet_t pp = ax25_from_text(dup, 1);
    free(dup);
    if (!pp) return -1;
    layer2_preamble_postamble(0, 32, 0, &g_cfg);
    layer2_send_frame(0, pp, 0, &g_cfg);
    layer2_preamble_postamble(0, 2, 1, &g_cfg);
    ax25_delete(pp);
    return 0;
}

/* JS reads tx_buf out via getValue/HEAPU8.slice between these two calls,
 * then calls reset to free the next frame's headroom. */
EMSCRIPTEN_KEEPALIVE
int wfweb_dw_tx_buffer_ptr(void)  { return (int)(intptr_t)tx_buf; }
EMSCRIPTEN_KEEPALIVE
int wfweb_dw_tx_buffer_len(void)  { return (int)tx_buf_len; }
EMSCRIPTEN_KEEPALIVE
void wfweb_dw_tx_buffer_reset(void) { tx_buf_len = 0; }


/* === AX.25 connected-mode link layer ====================================
 *
 * This section wraps Direwolf's ax25_link.c + dlq.c so JS can drive
 * connected-mode AX.25 (terminal sessions, BBS access, etc.). Mirrors
 * src/ax25linkprocessor.cpp on the C++ side — same callback shape, same
 * dispatcher loop, same dlq_* request API. The big difference is
 * threading: WASM is single-threaded, so JS calls wfweb_dw_link_step()
 * from a setInterval to drain the DLQ + service link timers.
 */

/* --- JS event trampolines ----------------------------------------------- */

EM_JS(void, wfweb_dw_emit_link_event, (int kind, int chan, int client,
                                       const char *remote, const char *own,
                                       int param), {
    if (typeof Module !== 'undefined' && typeof Module.onLinkEvent === 'function') {
        Module.onLinkEvent(kind, chan, client,
            UTF8ToString(remote || 0), UTF8ToString(own || 0), param);
    }
});

EM_JS(void, wfweb_dw_emit_link_data, (int chan, int client, const char *remote,
                                      const char *own, int pid,
                                      int ptr, int len), {
    if (typeof Module !== 'undefined' && typeof Module.onLinkData === 'function') {
        var bytes = HEAPU8.slice(ptr, ptr + len);
        Module.onLinkData(chan, client, UTF8ToString(remote || 0),
            UTF8ToString(own || 0), pid, bytes);
    }
});

EM_JS(int, wfweb_dw_callsign_lookup_js, (const char *callsign), {
    if (typeof Module !== 'undefined' && typeof Module.onCallsignLookup === 'function') {
        return Module.onCallsignLookup(UTF8ToString(callsign || 0));
    }
    return -1;
});

/* Link event "kind" codes used by wfweb_dw_emit_link_event. */
#define WFWEB_LINK_EV_ESTABLISHED 1
#define WFWEB_LINK_EV_TERMINATED  2
#define WFWEB_LINK_EV_OUTSTANDING 3
#define WFWEB_LINK_EV_ACKED       4

/* --- C-side callback shims --------------------------------------------- */

static void on_link_established(int chan, int client, const char *remote,
                                const char *own, int incoming) {
    wfweb_dw_emit_link_event(WFWEB_LINK_EV_ESTABLISHED, chan, client,
                             remote, own, incoming);
}
static void on_link_terminated(int chan, int client, const char *remote,
                               const char *own, int timeout) {
    wfweb_dw_emit_link_event(WFWEB_LINK_EV_TERMINATED, chan, client,
                             remote, own, timeout);
}
static void on_rec_conn_data(int chan, int client, const char *remote,
                             const char *own, int pid,
                             const char *data, int len) {
    wfweb_dw_emit_link_data(chan, client, remote, own, pid,
                            (int)(intptr_t)data, len);
}
static void on_outstanding(int chan, int client, const char *own,
                           const char *remote, int count) {
    /* For OUTSTANDING + ACKED we re-use the link-event channel with own/remote
     * swapped vs. ESTABLISHED — JS sorts it out from the kind. */
    wfweb_dw_emit_link_event(WFWEB_LINK_EV_OUTSTANDING, chan, client,
                             remote, own, count);
}
static void on_data_acked(int chan, int client, const char *own,
                          const char *remote, int count) {
    wfweb_dw_emit_link_event(WFWEB_LINK_EV_ACKED, chan, client,
                             remote, own, count);
}
static int on_callsign_lookup(const char *callsign) {
    return wfweb_dw_callsign_lookup_js(callsign);
}

/* tq callback: ax25_link wants a frame TX'd. We encode it straight to
 * the modem's TX buffer (same path the one-shot APRS TX uses); JS reads
 * tx_buf after wfweb_dw_link_step returns and ships the audio. */
static void on_tq_data(int chan, int prio, packet_t pp) {
    (void)prio;
    if (!pp) return;
    layer2_preamble_postamble(chan, 32, 0, &g_cfg);
    layer2_send_frame(chan, pp, 0, &g_cfg);
    layer2_preamble_postamble(chan, 2, 1, &g_cfg);
    ax25_delete(pp);
}

/* tq seize callback: our channel always seizes immediately (we'll handle
 * PTT timing on the JS side based on tx_buf flushes). */
static void on_tq_seize(int chan) {
    dlq_seize_confirm(chan);
}

/* --- Init / config / step / requests ----------------------------------- */

static struct misc_config_s g_link_cfg;
static int g_link_initialized = 0;

EMSCRIPTEN_KEEPALIVE
int wfweb_dw_link_init(void) {
    if (g_link_initialized) return 0;
    memset(&g_link_cfg, 0, sizeof(g_link_cfg));
    g_link_cfg.frack             = 4;     /* updated dynamically by set_paclen_for_baud */
    g_link_cfg.retry             = 10;
    g_link_cfg.paclen            = 128;
    g_link_cfg.maxframe_basic    = 1;     /* one I-frame per ACK — classic packet */
    g_link_cfg.maxframe_extended = 1;
    g_link_cfg.maxv22            = 0;     /* force v2.0; we don't need SABME */

    ax25_link_init(&g_link_cfg, /*debug*/0, /*stats*/0);
    tq_init(NULL);
    ensure_dlq_inited();

    wfweb_dw_register_server_callbacks(&on_link_established,
                                       &on_link_terminated,
                                       &on_rec_conn_data,
                                       &on_outstanding,
                                       &on_callsign_lookup);
    wfweb_dw_register_data_acked_cb(&on_data_acked);
    wfweb_dw_register_tq_callbacks(&on_tq_data, &on_tq_seize);

    g_link_initialized = 1;
    return 0;
}

/* Sized per the C++ AX25LinkProcessor::setLinkParamsForBaud — frack
 * scaled to the modem's airtime, paclen scaled to typical link MTU. */
EMSCRIPTEN_KEEPALIVE
void wfweb_dw_link_set_baud(int baud) {
    int frack, paclen;
    switch (baud) {
    case 300:  frack = 10; paclen = 64;  break;
    case 1200: frack = 4;  paclen = 128; break;
    case 9600: frack = 3;  paclen = 256; break;
    default:   frack = 4;  paclen = 128; break;
    }
    g_link_cfg.frack  = frack;
    g_link_cfg.paclen = paclen;
}

EMSCRIPTEN_KEEPALIVE
int wfweb_dw_link_paclen(void) { return g_link_cfg.paclen; }

/* Pack two callsigns + an optional digi list into the address array
 * dlq_*_request expects. addrs is fixed-size [10][12] (AX25_MAX_ADDRS *
 * AX25_MAX_ADDR_LEN) — we own the storage on the stack, copy in from
 * a flat null-separated string list (own,peer,via1,via2,…\0).
 * Returns num_addr (2 + #digis). */
static int pack_addrs(char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN],
                      const char *own, const char *peer, const char *digis_csv) {
    memset(addrs, 0, AX25_MAX_ADDRS * AX25_MAX_ADDR_LEN);
    /* slot 0 = AX25_DESTINATION (peer), slot 1 = AX25_SOURCE (own) */
    int n = 0;
    strncpy(addrs[0], peer ? peer : "", AX25_MAX_ADDR_LEN - 1);
    strncpy(addrs[1], own  ? own  : "", AX25_MAX_ADDR_LEN - 1);
    n = 2;
    if (digis_csv && *digis_csv) {
        /* Comma-separated list. mutate-in-place via strtok on a dup. */
        char *dup = strdup(digis_csv);
        if (dup) {
            char *tok, *save;
            for (tok = strtok_r(dup, ",", &save);
                 tok && n < AX25_MAX_ADDRS;
                 tok = strtok_r(NULL, ",", &save)) {
                while (*tok == ' ') tok++;
                if (*tok) {
                    strncpy(addrs[n], tok, AX25_MAX_ADDR_LEN - 1);
                    n++;
                }
            }
            free(dup);
        }
    }
    return n;
}

EMSCRIPTEN_KEEPALIVE
void wfweb_dw_link_register_call(int chan, int client, const char *callsign) {
    if (!callsign) return;
    /* dlq_register_callsign mutates its first arg, so dup. */
    char buf[AX25_MAX_ADDR_LEN];
    strncpy(buf, callsign, AX25_MAX_ADDR_LEN - 1);
    buf[AX25_MAX_ADDR_LEN - 1] = '\0';
    dlq_register_callsign(buf, chan, client);
}
EMSCRIPTEN_KEEPALIVE
void wfweb_dw_link_unregister_call(int chan, int client, const char *callsign) {
    if (!callsign) return;
    char buf[AX25_MAX_ADDR_LEN];
    strncpy(buf, callsign, AX25_MAX_ADDR_LEN - 1);
    buf[AX25_MAX_ADDR_LEN - 1] = '\0';
    dlq_unregister_callsign(buf, chan, client);
}

EMSCRIPTEN_KEEPALIVE
void wfweb_dw_link_connect(int chan, int client, const char *own,
                           const char *peer, const char *digis_csv) {
    char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN];
    int n = pack_addrs(addrs, own, peer, digis_csv);
    dlq_connect_request(addrs, n, chan, client, /*pid*/0xF0);
}

EMSCRIPTEN_KEEPALIVE
void wfweb_dw_link_disconnect(int chan, int client, const char *own,
                              const char *peer, const char *digis_csv) {
    char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN];
    int n = pack_addrs(addrs, own, peer, digis_csv);
    dlq_disconnect_request(addrs, n, chan, client);
}

EMSCRIPTEN_KEEPALIVE
void wfweb_dw_link_send_data(int chan, int client, const char *own,
                             const char *peer, const char *digis_csv,
                             int pid, int data_ptr, int data_len) {
    char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN];
    int n = pack_addrs(addrs, own, peer, digis_csv);
    /* dlq_xmit_data_request copies the buffer into a cdata_t, so the
     * JS side's heap memory is safe to free after this call returns. */
    dlq_xmit_data_request(addrs, n, chan, client, pid,
                          (char *)(intptr_t)data_ptr, data_len);
}

EMSCRIPTEN_KEEPALIVE
void wfweb_dw_link_outstanding_request(int chan, int client,
                                       const char *own, const char *peer) {
    char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN];
    int n = pack_addrs(addrs, own, peer, NULL);
    dlq_outstanding_frames_request(addrs, n, chan, client);
}

EMSCRIPTEN_KEEPALIVE
void wfweb_dw_link_client_cleanup(int client) {
    dlq_client_cleanup(client);
}

/* One iteration of AX25LinkProcessor::dispatcherLoop. JS calls this on a
 * setInterval (~50 ms) when the link is active - services link timers,
 * drains the DLQ, dispatches each item. Always emits RX frames to JS
 * (for the monitor pane); link-specific dl_/lm_ dispatch is gated on
 * link init so the modem-only path can use this to drain the DLQ too. */
EMSCRIPTEN_KEEPALIVE
int wfweb_dw_link_step(void) {
    if (g_link_initialized) dl_timer_expiry();
    int processed = 0;
    for (;;) {
        dlq_item_t *E = dlq_remove();
        if (!E) break;
        switch (E->type) {
        case DLQ_REC_FRAME:
            if (E->pp) {
                /* Surface every RX frame to JS for the monitor pane —
                 * AX25LinkProcessor::dispatcherLoop does the same so
                 * the operator sees connected-mode I-frames too. */
                unsigned char buf[AX25_MAX_PACKET_LEN];
                int n = ax25_pack(E->pp, buf);
                if (n > 0) {
                    wfweb_dw_emit_rx_frame(E->chan, E->alevel.rec,
                                           (int)(intptr_t)buf, n);
                }
                if (g_link_initialized) lm_data_indication(E);
            }
            break;
        case DLQ_CONNECT_REQUEST:
            if (g_link_initialized) dl_connect_request(E); break;
        case DLQ_DISCONNECT_REQUEST:
            if (g_link_initialized) dl_disconnect_request(E); break;
        case DLQ_XMIT_DATA_REQUEST:
            if (g_link_initialized) dl_data_request(E); break;
        case DLQ_REGISTER_CALLSIGN:
            if (g_link_initialized) dl_register_callsign(E); break;
        case DLQ_UNREGISTER_CALLSIGN:
            if (g_link_initialized) dl_unregister_callsign(E); break;
        case DLQ_OUTSTANDING_FRAMES_REQUEST:
            if (g_link_initialized) dl_outstanding_frames_request(E); break;
        case DLQ_CHANNEL_BUSY:
            if (g_link_initialized) lm_channel_busy(E); break;
        case DLQ_SEIZE_CONFIRM:
            if (g_link_initialized) lm_seize_confirm(E); break;
        case DLQ_CLIENT_CLEANUP:
            if (g_link_initialized) dl_client_cleanup(E); break;
        }
        dlq_delete(E);
        processed++;
    }
    return processed;
}
