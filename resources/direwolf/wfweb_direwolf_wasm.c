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

/* hdlc_rec / hdlc_rec2 push decoded frames here. We don't have ax25_link
 * vendored on the WASM build (yet), so route directly to JS as the
 * "got a frame" hook. Same packet_t shape; we serialize via ax25_pack. */
void dlq_rec_frame (int chan, int subchan, int slice, packet_t pp,
                    alevel_t alevel, fec_type_t fec_type, retry_t retries,
                    char *spectrum)
{
    (void)subchan; (void)slice; (void)fec_type; (void)retries; (void)spectrum;
    if (!pp) return;
    unsigned char buf[AX25_MAX_PACKET_LEN];
    int n = ax25_pack(pp, buf);
    if (n > 0) {
        wfweb_dw_emit_rx_frame(chan, alevel.rec, (int)(intptr_t)buf, n);
    }
    ax25_delete(pp);
}

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
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int wfweb_dw_baud(void) { return g_cfg_baud; }

/* Push a batch of int16 LE PCM samples through the demodulator. The buffer
 * lives in the WASM heap; JS already memcpy'd bytes there before the call. */
EMSCRIPTEN_KEEPALIVE
void wfweb_dw_process_samples(int ptr, int n_samples)
{
    const short *p = (const short *)(intptr_t)ptr;
    for (int i = 0; i < n_samples; i++) {
        multi_modem_process_sample(0, (int)p[i]);
    }
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
