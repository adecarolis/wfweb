/*
 *  wfweb_direwolf_stubs.c
 *
 *  Glue between the vendored Dire Wolf modem/HDLC/AX.25 core and the host
 *  wfweb application.  Provides:
 *
 *   - dw_printf / text_color_*  -> Qt logging (via wfweb_dw_log trampoline)
 *   - audio_put                  -> TX sample hook (feeds the active
 *                                   DireWolfProcessor's TX PCM buffer)
 *   - audio_get / audio_flush / audio_wait / audio_open / audio_close
 *                                -> no-ops (we own the audio bus)
 *   - ptt_set / ptt_init / ptt_term
 *                                -> no-ops (PTT is driven by wfweb's
 *                                   cachingQueue, not Dire Wolf)
 *   - fx25_* / il2p_*            -> no-op stubs so hdlc_rec / hdlc_send
 *                                   link without pulling in FEC sources
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "direwolf.h"
#include "textcolor.h"
#include "ax25_pad.h"
#include "audio.h"
#include "dlq.h"
#include "fx25.h"
#include "il2p.h"
#include "fsk_demod_state.h"    /* for struct demodulator_state_s */
#include "demod_psk.h"
#include "ais.h"

/* ----- trampolines implemented in direwolfprocessor.cpp ----- */
extern void wfweb_dw_log(int level, const char *msg);
extern int  wfweb_dw_tx_put_byte(int adev, int byte);
extern void wfweb_dw_rx_frame(int chan, int subchan, int slice,
                              const unsigned char *ax25, int len,
                              int alevel_rx_biased, int alevel_mark,
                              int alevel_space, int fec_type, int retries);

/* ----- textcolor / logging ----- */

static int g_dw_log_level = 0;

void text_color_init(int enable_color) { (void)enable_color; }
void text_color_set(dw_color_t c)      { g_dw_log_level = (int)c; }
void text_color_term(void)             { }

int dw_printf(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    /* Strip single trailing newline so Qt logging doesn't double-space. */
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';

    wfweb_dw_log(g_dw_log_level, buf);
    return n;
}

/* ----- audio hooks ----- */

int audio_open(struct audio_s *pa)     { (void)pa; return 0; }
int audio_close(void)                  { return 0; }
int audio_get(int a)                   { (void)a; return 0; }
int audio_put(int a, int c)            { return wfweb_dw_tx_put_byte(a, c); }
int audio_flush(int a)                 { (void)a; return 0; }
void audio_wait(int a)                 { (void)a; }

/* ----- PTT ----- */

void ptt_set_debug(int debug)          { (void)debug; }
void ptt_set(int ot, int chan, int signal)
{
    (void)ot; (void)chan; (void)signal;
    /* PTT is handled in webserver.cpp via cachingQueue. */
}

/* ----- FX.25 FEC (not vendored) ----- */

void fx25_init(int debug_level)                                 { (void)debug_level; }
int  fx25_send_frame(int chan, unsigned char *fbuf, int flen, int fx_mode)
                                                                 { (void)chan; (void)fbuf; (void)flen; (void)fx_mode; return 0; }
void fx25_rec_bit(int chan, int subchan, int slice, int dbit)   { (void)chan; (void)subchan; (void)slice; (void)dbit; }
int  fx25_rec_busy(int chan)                                    { (void)chan; return 0; }

/* ----- IL2P FEC (not vendored) ----- */

void il2p_init(int debug)                                       { (void)debug; }
int  il2p_send_frame(int chan, packet_t pp, int max_fec, int polarity)
                                                                 { (void)chan; (void)pp; (void)max_fec; (void)polarity; return 0; }
void il2p_rec_bit(int chan, int subchan, int slice, int dbit)   { (void)chan; (void)subchan; (void)slice; (void)dbit; }

/* ----- PSK demod (not vendored; only AFSK + 9600 FSK in v1) ----- */

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

/* ----- AIS (not vendored) ----- */

void ais_to_nmea(unsigned char *ais, int ais_len, char *nmea, int nmea_size)
{
    (void)ais; (void)ais_len;
    if (nmea && nmea_size > 0) nmea[0] = '\0';
}
int ais_check_length(int type, int length)
{
    (void)type; (void)length;
    return 0;
}

/* ----- Misc PTT inputs (TXINH pin, not used) ----- */

int get_input(int it, int chan)
{
    (void)it; (void)chan;
    return -1;      /* "not configured" sentinel */
}

/* dlq_rec_frame is now provided by the real dlq.c (vendored from upstream
 * Dire Wolf for connected-mode AX.25).  Frames placed on the DLQ by the
 * modem are drained by AX25LinkProcessor (src/ax25linkprocessor.cpp),
 * which dispatches DLQ_REC_FRAME both to lm_data_indication (connected
 * mode) and to the existing wfweb_dw_rx_frame trampoline (APRS UI). */

/* ----- strlcpy_debug / strlcat_debug -----
 *
 * direwolf.h routes strlcpy()/strlcat() through these wrappers (DEBUG_STRL=1)
 * unless HAVE_STRLCPY / HAVE_STRLCAT are defined.  Direwolf only auto-detects
 * those on glibc >= 2.38, leaving Debian 12 (glibc 2.36) and Windows MSVC
 * with unresolved references.  Provide BSD-style implementations here so the
 * link succeeds on every supported target.  On macOS we set HAVE_STRLCPY via
 * wfweb.pro (libc already has the real ones), so these definitions are
 * compiled but unused. */

size_t strlcpy_debug(char *dst, const char *src, size_t siz,
                     const char *file, const char *func, int line)
{
    (void)file; (void)func; (void)line;
    size_t srclen = strlen(src);
    if (siz != 0) {
        size_t copy = (srclen < siz - 1) ? srclen : siz - 1;
        memcpy(dst, src, copy);
        dst[copy] = '\0';
    }
    return srclen;
}

size_t strlcat_debug(char *dst, const char *src, size_t siz,
                     const char *file, const char *func, int line)
{
    (void)file; (void)func; (void)line;
    size_t dstlen = strnlen(dst, siz);
    size_t srclen = strlen(src);
    if (dstlen >= siz) return siz + srclen;
    size_t copy = siz - dstlen - 1;
    if (srclen < copy) copy = srclen;
    memcpy(dst + dstlen, src, copy);
    dst[dstlen + copy] = '\0';
    return dstlen + srclen;
}
