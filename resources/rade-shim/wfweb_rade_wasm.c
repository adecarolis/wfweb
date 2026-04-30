/*
 * wfweb_rade_wasm.c — RADE V1 (radae_nopy) glue for the wfweb browser build.
 *
 * Mirrors the public API surface used by src/radeprocessor.cpp, but exposed
 * as plain C functions that JS can drive via cwrap()/ccall().  JS owns
 * Hilbert-transform, 16-bit↔float conversion, and all buffering/threading;
 * this shim only owns the C-side state (rade, lpcnet encoder, fargan).
 *
 * Sample-rate conversion is deliberately exposed via wf_resampler_* so the
 * JS side runs the same speex code path as the desktop build (radeprocessor
 * uses speex with quality=5).
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <emscripten.h>

#include "rade_api.h"
#include "rade_dsp.h"
#include "rade_text.h"
#include "speex_resampler.h"

#include <lpcnet.h>
#include <fargan.h>

/* Single-instance globals — same constraint as the C++ build (Direwolf and
 * RADE both have process-global state).  One RADE per page. */
static struct rade *g_rade            = NULL;
static LPCNetEncState *g_lpcnet       = NULL;
static FARGANState *g_fargan          = NULL;
static rade_text_t g_text             = NULL;

static int g_arch_flags               = 0;
static int g_n_features_in_out        = 0;
static int g_n_tx_out                 = 0;
static int g_n_tx_eoo_out             = 0;
static int g_n_eoo_bits               = 0;
static int g_nin_max                  = 0;
static int g_fargan_warmup_frames     = 0;
static int g_fargan_ready             = 0;
static float g_fargan_warmup_buf[5 * NB_TOTAL_FEATURES];

/* JS-visible callback for decoded callsigns from EOO bits. */
EM_JS(void, wfweb_rade_text_rx_js, (int txt_ptr, int length), {
    if (typeof Module !== 'undefined' && typeof Module.onRadeText === 'function') {
        Module.onRadeText(UTF8ToString(txt_ptr, length));
    }
});

static void on_text_rx(rade_text_t rt, const char *txt_ptr, int length, void *state) {
    (void)rt; (void)state;
    wfweb_rade_text_rx_js((int)(uintptr_t)txt_ptr, length);
}

/* ----- init / close ------------------------------------------------------ */

EMSCRIPTEN_KEEPALIVE
int wfweb_rade_init(void) {
    if (g_rade) return 0;

    rade_initialize();
    g_rade = rade_open(NULL, RADE_USE_C_ENCODER | RADE_USE_C_DECODER | RADE_VERBOSE_0);
    if (!g_rade) return -1;

    g_n_features_in_out = rade_n_features_in_out(g_rade);
    g_n_tx_out          = rade_n_tx_out(g_rade);
    g_n_tx_eoo_out      = rade_n_tx_eoo_out(g_rade);
    g_n_eoo_bits        = rade_n_eoo_bits(g_rade);
    g_nin_max           = rade_nin_max(g_rade);

    g_lpcnet = lpcnet_encoder_create();
    if (!g_lpcnet) { rade_close(g_rade); g_rade = NULL; return -2; }
    g_arch_flags = 0;  /* WASM has no x86/arm RTCD; force generic path. */

    g_fargan = (FARGANState *)calloc(1, sizeof(FARGANState));
    if (!g_fargan) {
        lpcnet_encoder_destroy(g_lpcnet); g_lpcnet = NULL;
        rade_close(g_rade); g_rade = NULL;
        return -3;
    }
    fargan_init(g_fargan);
    g_fargan_ready         = 0;
    g_fargan_warmup_frames = 0;

    g_text = rade_text_create();
    rade_text_set_rx_callback(g_text, on_text_rx, NULL);

    return 0;
}

EMSCRIPTEN_KEEPALIVE
void wfweb_rade_close(void) {
    if (g_text)   { rade_text_destroy(g_text); g_text = NULL; }
    if (g_rade)   { rade_close(g_rade); g_rade = NULL; }
    if (g_lpcnet) { lpcnet_encoder_destroy(g_lpcnet); g_lpcnet = NULL; }
    if (g_fargan) { free(g_fargan); g_fargan = NULL; }
    g_fargan_ready = 0;
    g_fargan_warmup_frames = 0;
}

/* ----- size queries ------------------------------------------------------ */

EMSCRIPTEN_KEEPALIVE int wfweb_rade_n_features_in_out(void) { return g_n_features_in_out; }
EMSCRIPTEN_KEEPALIVE int wfweb_rade_n_tx_out(void)          { return g_n_tx_out; }
EMSCRIPTEN_KEEPALIVE int wfweb_rade_n_tx_eoo_out(void)      { return g_n_tx_eoo_out; }
EMSCRIPTEN_KEEPALIVE int wfweb_rade_n_eoo_bits(void)        { return g_n_eoo_bits; }
EMSCRIPTEN_KEEPALIVE int wfweb_rade_nin_max(void)           { return g_nin_max; }
EMSCRIPTEN_KEEPALIVE int wfweb_rade_nin(void)               { return g_rade ? rade_nin(g_rade) : 0; }
EMSCRIPTEN_KEEPALIVE int wfweb_rade_lpcnet_frame_size(void) { return LPCNET_FRAME_SIZE; }
EMSCRIPTEN_KEEPALIVE int wfweb_rade_nb_total_features(void) { return NB_TOTAL_FEATURES; }
EMSCRIPTEN_KEEPALIVE int wfweb_rade_nb_features(void)       { return NB_FEATURES; }

/* ----- TX feature extraction (LPCNet) ------------------------------------
 * pcm160_in: LPCNET_FRAME_SIZE int16 samples at 16 kHz.
 * features_out: NB_TOTAL_FEATURES floats.
 */
EMSCRIPTEN_KEEPALIVE
void wfweb_rade_extract_features(const int16_t *pcm160_in, float *features_out) {
    if (!g_lpcnet) return;
    lpcnet_compute_single_frame_features(g_lpcnet,
        (opus_int16 *)pcm160_in, features_out, g_arch_flags);
}

/* ----- TX modem encode ---------------------------------------------------
 * features_in: framesPerMf * NB_TOTAL_FEATURES floats.
 * iq_out: nTxOut RADE_COMP samples written as interleaved float pairs.
 * returns nIQ pairs written (== rade_tx return value).
 */
EMSCRIPTEN_KEEPALIVE
int wfweb_rade_tx(const float *features_in, float *iq_out_pairs) {
    if (!g_rade) return 0;
    return rade_tx(g_rade, (RADE_COMP *)iq_out_pairs, (float *)features_in);
}

EMSCRIPTEN_KEEPALIVE
int wfweb_rade_tx_eoo(float *iq_out_pairs) {
    if (!g_rade) return 0;
    return rade_tx_eoo(g_rade, (RADE_COMP *)iq_out_pairs);
}

EMSCRIPTEN_KEEPALIVE
void wfweb_rade_set_eoo_bits(const float *eoo_bits) {
    if (!g_rade) return;
    rade_tx_set_eoo_bits(g_rade, (float *)eoo_bits);
}

/* ----- RX modem decode ---------------------------------------------------
 * iq_in_pairs: nin RADE_COMP samples (interleaved float pairs)
 * features_out, eoo_out: caller-allocated buffers sized to n_features_in_out
 *                        and n_eoo_bits respectively.
 * has_eoo_out (1 int): set non-zero if eoo_out was written.
 * returns count of features written (>= 0).
 */
EMSCRIPTEN_KEEPALIVE
int wfweb_rade_rx(const float *iq_in_pairs, float *features_out,
                  int *has_eoo_out, float *eoo_out) {
    if (!g_rade) { *has_eoo_out = 0; return 0; }
    return rade_rx(g_rade, features_out, has_eoo_out, eoo_out,
                   (RADE_COMP *)iq_in_pairs);
}

EMSCRIPTEN_KEEPALIVE int   wfweb_rade_sync(void)         { return g_rade ? rade_sync(g_rade) : 0; }
EMSCRIPTEN_KEEPALIVE float wfweb_rade_freq_offset(void)  { return g_rade ? rade_freq_offset(g_rade) : 0.0f; }
EMSCRIPTEN_KEEPALIVE int   wfweb_rade_snr(void)          { return g_rade ? rade_snrdB_3k_est(g_rade) : 0; }

/* ----- FARGAN vocoder (RX synthesis) -------------------------------------
 * The first 5 frames go through the warmup path (matches radeprocessor.cpp).
 */
EMSCRIPTEN_KEEPALIVE
void wfweb_rade_fargan_reset(void) {
    if (g_fargan) fargan_init(g_fargan);
    g_fargan_ready = 0;
    g_fargan_warmup_frames = 0;
}

EMSCRIPTEN_KEEPALIVE
int wfweb_rade_fargan_warmup(const float *features) {
    if (!g_fargan) return -1;
    if (g_fargan_ready) return 1;
    memcpy(g_fargan_warmup_buf + g_fargan_warmup_frames * NB_TOTAL_FEATURES,
           features, NB_TOTAL_FEATURES * sizeof(float));
    g_fargan_warmup_frames++;
    if (g_fargan_warmup_frames >= 5) {
        float packed[5 * NB_FEATURES];
        for (int i = 0; i < 5; i++)
            memcpy(packed + i * NB_FEATURES,
                   g_fargan_warmup_buf + i * NB_TOTAL_FEATURES,
                   NB_FEATURES * sizeof(float));
        float zeros[LPCNET_FRAME_SIZE] = {0};
        fargan_cont(g_fargan, zeros, packed);
        g_fargan_ready = 1;
        return 1;
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE
void wfweb_rade_fargan_synth(const float *features, int16_t *pcm160_out) {
    if (!g_fargan || !g_fargan_ready) return;
    float fpcm[LPCNET_FRAME_SIZE];
    fargan_synthesize(g_fargan, fpcm, (float *)features);
    for (int i = 0; i < LPCNET_FRAME_SIZE; i++) {
        float v = fpcm[i] * 32768.0f;
        if (v > 32767.0f) v = 32767.0f;
        if (v < -32767.0f) v = -32767.0f;
        pcm160_out[i] = (int16_t)floorf(0.5f + v);
    }
}

/* ----- rade_text (callsign-in-EOO) --------------------------------------- */

EMSCRIPTEN_KEEPALIVE
void *wfweb_rade_text_create(void) {
    rade_text_t t = rade_text_create();
    if (t) rade_text_set_rx_callback(t, on_text_rx, NULL);
    return t;
}

EMSCRIPTEN_KEEPALIVE
void wfweb_rade_text_destroy(void *t) {
    if (t) rade_text_destroy((rade_text_t)t);
}

/* Encodes callsign into the eoo_bits buffer (size = n_eoo_bits). */
EMSCRIPTEN_KEEPALIVE
void wfweb_rade_text_encode(const char *callsign, int len, float *eoo_bits, int n) {
    if (!g_text) return;
    rade_text_generate_tx_string(g_text, callsign, len, eoo_bits, n);
}

/* Decode soft-decision EOO symbols.  On a successful decode the rx callback
 * runs and emits to JS via wfweb_rade_text_rx_js. */
EMSCRIPTEN_KEEPALIVE
void wfweb_rade_text_decode(float *symbols, int n) {
    if (!g_text) return;
    rade_text_rx(g_text, symbols, n);
}

/* ----- speex resampler --------------------------------------------------- */

EMSCRIPTEN_KEEPALIVE
void *wfweb_rade_resampler_init(uint32_t in_rate, uint32_t out_rate, int quality) {
    int err = 0;
    SpeexResamplerState *r = wf_resampler_init(1, in_rate, out_rate, quality, &err);
    return (err == 0) ? r : NULL;
}

EMSCRIPTEN_KEEPALIVE
void wfweb_rade_resampler_destroy(void *r) {
    if (r) wf_resampler_destroy((SpeexResamplerState *)r);
}

/* in/out are float buffers. in_len_inout and out_len_inout point to ints
 * holding capacity in / consumed/produced out, mirroring speex semantics. */
EMSCRIPTEN_KEEPALIVE
int wfweb_rade_resampler_process(void *r, const float *in, int *in_len_inout,
                                 float *out, int *out_len_inout) {
    if (!r) return -1;
    spx_uint32_t inLen = *in_len_inout;
    spx_uint32_t outLen = *out_len_inout;
    int rc = wf_resampler_process_float((SpeexResamplerState *)r, 0,
                                        in, &inLen, out, &outLen);
    *in_len_inout = (int)inLen;
    *out_len_inout = (int)outLen;
    return rc;
}
