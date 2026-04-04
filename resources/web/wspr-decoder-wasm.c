#include <emscripten/emscripten.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wspr/fano.h"
#include "wspr/wsprsim_utils.h"
#include "wspr/wsprd_utils.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_RESULTS 100
#define MAX_CANDIDATES 200
#define MAX_POINTS 65536
#define WSPR_SAMPLE_RATE 12000
#define WSPR_DECIMATION 32
#define WSPR_BASEBAND_RATE (WSPR_SAMPLE_RATE / WSPR_DECIMATION)
#define WSPR_FIR_TAPS 129
#define WSPR_SYMBOL_COUNT 162
#define WSPR_SYMBOL_SAMPLES 256
#define WSPR_CENTER_TONE_OFFSET 1.5f

extern float metric_tables[5][256];

unsigned char pr3[162] =
{
    1,1,0,0,0,0,0,0,1,0,0,0,1,1,1,0,0,0,1,0,
    0,1,0,1,1,1,1,0,0,0,0,0,0,0,1,0,0,1,0,1,
    0,0,0,0,0,0,1,0,1,1,0,0,1,1,0,1,0,0,0,1,
    1,0,1,0,0,0,0,1,1,0,1,0,1,0,1,0,1,0,0,1,
    0,0,1,0,1,1,0,0,0,1,1,0,1,0,1,0,0,0,1,0,
    0,0,0,0,1,0,0,1,0,0,1,1,1,0,1,1,0,0,1,1,
    0,1,0,0,0,1,1,1,0,0,0,0,0,1,0,1,0,0,1,1,
    0,0,0,0,0,0,0,1,1,0,1,0,1,1,0,0,0,1,1,0,
    0,0
};

typedef struct {
    float freq;
    float snr;
    int shift;
    float drift;
    float sync;
} WsprCandidate;

typedef struct {
    float sync;
    float snr;
    float dt;
    double freq_mhz;
    int drift;
    unsigned int cycles;
    int jitter;
    int blocksize;
    unsigned int metric;
    char message[24];
    char callsign[16];
    char grid[8];
    int dbm;
} WsprResult;

static WsprResult g_results[MAX_RESULTS];
static int g_result_count = 0;
static char g_error[256] = "";
static int g_mettab[2][256];
static float g_fir_taps[WSPR_FIR_TAPS];
static int g_initialized = 0;
static char g_hashtab[32768 * 13];
static char g_loctab[32768 * 5];
static int g_debug_search_low_hz = 0;
static int g_debug_search_high_hz = 0;
static int g_debug_sample_count = 0;
static int g_debug_decimated_point_count = 0;
static int g_debug_fft_count = 0;
static int g_debug_peak_candidate_count = 0;
static int g_debug_filtered_candidate_count = 0;
static int g_debug_refined_candidate_count = 0;
static int g_debug_decode_pass_count = 0;
int printdata = 0;

static void set_error(const char *msg)
{
    if (!msg) msg = "unknown";
    snprintf(g_error, sizeof(g_error), "%s", msg);
}

static void clear_error(void)
{
    g_error[0] = '\0';
}

static void reset_debug_state(void)
{
    g_debug_search_low_hz = 0;
    g_debug_search_high_hz = 0;
    g_debug_sample_count = 0;
    g_debug_decimated_point_count = 0;
    g_debug_fft_count = 0;
    g_debug_peak_candidate_count = 0;
    g_debug_filtered_candidate_count = 0;
    g_debug_refined_candidate_count = 0;
    g_debug_decode_pass_count = 0;
}

static int reverse_bits(int x, int bits)
{
    int y = 0;
    for (int i = 0; i < bits; ++i) {
        y = (y << 1) | ((x >> i) & 1);
    }
    return y;
}

static void fft_inplace(float *re, float *im, int n)
{
    int levels = 0;
    for (int temp = n; temp > 1; temp >>= 1) levels++;

    for (int i = 0; i < n; ++i) {
        int j = reverse_bits(i, levels);
        if (j > i) {
            float tr = re[i];
            float ti = im[i];
            re[i] = re[j];
            im[i] = im[j];
            re[j] = tr;
            im[j] = ti;
        }
    }

    for (int size = 2; size <= n; size <<= 1) {
        const int half = size >> 1;
        const float theta = -2.0f * (float)M_PI / (float)size;
        const float wpr = cosf(theta);
        const float wpi = sinf(theta);
        for (int i = 0; i < n; i += size) {
            float wr = 1.0f;
            float wi = 0.0f;
            for (int j = 0; j < half; ++j) {
                const int even = i + j;
                const int odd = even + half;
                const float tr = wr * re[odd] - wi * im[odd];
                const float ti = wr * im[odd] + wi * re[odd];
                re[odd] = re[even] - tr;
                im[odd] = im[even] - ti;
                re[even] += tr;
                im[even] += ti;
                const float next_wr = wr * wpr - wi * wpi;
                wi = wr * wpi + wi * wpr;
                wr = next_wr;
            }
        }
    }
}

static void init_fir(void)
{
    const float cutoff_hz = 170.0f;
    const float fc = cutoff_hz / (float)WSPR_SAMPLE_RATE;
    const int mid = (WSPR_FIR_TAPS - 1) / 2;
    float norm = 0.0f;

    for (int i = 0; i < WSPR_FIR_TAPS; ++i) {
        const int m = i - mid;
        const float x = 2.0f * fc * (float)m;
        const float sinc = (m == 0) ? (2.0f * fc) : (sinf((float)M_PI * x) / ((float)M_PI * (float)m));
        const float win = 0.54f - 0.46f * cosf((2.0f * (float)M_PI * (float)i) / (float)(WSPR_FIR_TAPS - 1));
        g_fir_taps[i] = sinc * win;
        norm += g_fir_taps[i];
    }

    if (fabsf(norm) < 1e-9f) norm = 1.0f;
    for (int i = 0; i < WSPR_FIR_TAPS; ++i) g_fir_taps[i] /= norm;
}

static void init_decoder(void)
{
    if (g_initialized) return;
    for (int i = 0; i < 256; ++i) {
        const float bias = 0.45f;
        g_mettab[0][i] = (int)lrintf(10.0f * (metric_tables[2][i] - bias));
        g_mettab[1][i] = (int)lrintf(10.0f * (metric_tables[2][255 - i] - bias));
    }
    memset(g_hashtab, 0, sizeof(g_hashtab));
    memset(g_loctab, 0, sizeof(g_loctab));
    init_fir();
    g_initialized = 1;
}

static int preprocess_samples(const float *samples,
                              int sample_count,
                              int audio_frequency_hz,
                              float *idat,
                              float *qdat)
{
    float *mix_i = NULL;
    float *mix_q = NULL;
    int out_count = 0;

    if (!samples || sample_count <= WSPR_FIR_TAPS) return 0;

    mix_i = (float *)malloc((size_t)sample_count * sizeof(float));
    mix_q = (float *)malloc((size_t)sample_count * sizeof(float));
    if (!mix_i || !mix_q) {
        free(mix_i);
        free(mix_q);
        return 0;
    }

    double phase = 0.0;
    const double dphase = -2.0 * M_PI * (double)audio_frequency_hz / (double)WSPR_SAMPLE_RATE;
    for (int i = 0; i < sample_count; ++i) {
        const float sample = samples[i];
        mix_i[i] = sample * (float)cos(phase);
        mix_q[i] = sample * (float)sin(phase);
        phase += dphase;
        if (phase > M_PI || phase < -M_PI) phase = fmod(phase, 2.0 * M_PI);
    }

    const int half = (WSPR_FIR_TAPS - 1) / 2;
    for (int center = half; center + half < sample_count && out_count < MAX_POINTS; center += WSPR_DECIMATION) {
        float acc_i = 0.0f;
        float acc_q = 0.0f;
        for (int k = 0; k < WSPR_FIR_TAPS; ++k) {
            const int idx = center + k - half;
            const float tap = g_fir_taps[k];
            acc_i += mix_i[idx] * tap;
            acc_q += mix_q[idx] * tap;
        }
        idat[out_count] = acc_i;
        qdat[out_count] = acc_q;
        out_count++;
    }

    free(mix_i);
    free(mix_q);
    return out_count;
}

void sync_and_demodulate(float *id, float *qd, long np,
                         unsigned char *symbols, float *f1, int ifmin, int ifmax, float fstep,
                         int *shift1, int lagmin, int lagmax, int lagstep,
                         float *drift1, int symfac, float *sync, int mode)
{
    static float fplast = -10000.0f;
    static float dt = 1.0f / 375.0f;
    static float df = 375.0f / 256.0f;
    const float pi = (float)M_PI;
    const float twopidt = 2.0f * pi * dt;
    const float df15 = df * 1.5f;
    const float df05 = df * 0.5f;

    int i, j, k, lag, ifreq;
    float i0[162], q0[162], i1[162], q1[162], i2[162], q2[162], i3[162], q3[162];
    float p0, p1, p2, p3, cmet, totp, syncmax, fac;
    float c0[256], s0[256], c1[256], s1[256], c2[256], s2[256], c3[256], s3[256];
    float dphi0, cdphi0, sdphi0, dphi1, cdphi1, sdphi1, dphi2, cdphi2, sdphi2, dphi3, cdphi3, sdphi3;
    float f0 = 0.0f, fp, ss, fbest = 0.0f, fsum = 0.0f, f2sum = 0.0f, fsymb[162];
    int best_shift = 0;

    syncmax = -1e30f;
    if (mode == 0) { ifmin = 0; ifmax = 0; fstep = 0.0f; f0 = *f1; }
    if (mode == 1) { lagmin = *shift1; lagmax = *shift1; f0 = *f1; }
    if (mode == 2) { lagmin = *shift1; lagmax = *shift1; ifmin = 0; ifmax = 0; f0 = *f1; }

    for (ifreq = ifmin; ifreq <= ifmax; ifreq++) {
        f0 = *f1 + ifreq * fstep;
        for (lag = lagmin; lag <= lagmax; lag += lagstep) {
            ss = 0.0f;
            totp = 0.0f;
            for (i = 0; i < 162; i++) {
                fp = f0 + (*drift1 / 2.0f) * ((float)i - 81.0f) / 81.0f;
                if (i == 0 || (fp != fplast)) {
                    dphi0 = twopidt * (fp - df15);
                    cdphi0 = cosf(dphi0);
                    sdphi0 = sinf(dphi0);

                    dphi1 = twopidt * (fp - df05);
                    cdphi1 = cosf(dphi1);
                    sdphi1 = sinf(dphi1);

                    dphi2 = twopidt * (fp + df05);
                    cdphi2 = cosf(dphi2);
                    sdphi2 = sinf(dphi2);

                    dphi3 = twopidt * (fp + df15);
                    cdphi3 = cosf(dphi3);
                    sdphi3 = sinf(dphi3);

                    c0[0] = 1.0f; s0[0] = 0.0f;
                    c1[0] = 1.0f; s1[0] = 0.0f;
                    c2[0] = 1.0f; s2[0] = 0.0f;
                    c3[0] = 1.0f; s3[0] = 0.0f;
                    for (j = 1; j < 256; j++) {
                        c0[j] = c0[j - 1] * cdphi0 - s0[j - 1] * sdphi0;
                        s0[j] = c0[j - 1] * sdphi0 + s0[j - 1] * cdphi0;
                        c1[j] = c1[j - 1] * cdphi1 - s1[j - 1] * sdphi1;
                        s1[j] = c1[j - 1] * sdphi1 + s1[j - 1] * cdphi1;
                        c2[j] = c2[j - 1] * cdphi2 - s2[j - 1] * sdphi2;
                        s2[j] = c2[j - 1] * sdphi2 + s2[j - 1] * cdphi2;
                        c3[j] = c3[j - 1] * cdphi3 - s3[j - 1] * sdphi3;
                        s3[j] = c3[j - 1] * sdphi3 + s3[j - 1] * cdphi3;
                    }
                    fplast = fp;
                }

                i0[i] = q0[i] = i1[i] = q1[i] = i2[i] = q2[i] = i3[i] = q3[i] = 0.0f;
                for (j = 0; j < 256; j++) {
                    k = lag + i * 256 + j;
                    if (k > 0 && k < np) {
                        i0[i] += id[k] * c0[j] + qd[k] * s0[j];
                        q0[i] += -id[k] * s0[j] + qd[k] * c0[j];
                        i1[i] += id[k] * c1[j] + qd[k] * s1[j];
                        q1[i] += -id[k] * s1[j] + qd[k] * c1[j];
                        i2[i] += id[k] * c2[j] + qd[k] * s2[j];
                        q2[i] += -id[k] * s2[j] + qd[k] * c2[j];
                        i3[i] += id[k] * c3[j] + qd[k] * s3[j];
                        q3[i] += -id[k] * s3[j] + qd[k] * c3[j];
                    }
                }

                p0 = sqrtf(i0[i] * i0[i] + q0[i] * q0[i]);
                p1 = sqrtf(i1[i] * i1[i] + q1[i] * q1[i]);
                p2 = sqrtf(i2[i] * i2[i] + q2[i] * q2[i]);
                p3 = sqrtf(i3[i] * i3[i] + q3[i] * q3[i]);

                totp += p0 + p1 + p2 + p3;
                cmet = (p1 + p3) - (p0 + p2);
                ss = (pr3[i] == 1) ? (ss + cmet) : (ss - cmet);
                if (mode == 2) {
                    fsymb[i] = (pr3[i] == 1) ? (p3 - p1) : (p2 - p0);
                }
            }

            if (totp <= 0.0f) continue;
            ss = ss / totp;
            if (ss > syncmax) {
                syncmax = ss;
                best_shift = lag;
                fbest = f0;
            }
        }
    }

    if (mode <= 1) {
        *sync = syncmax;
        *shift1 = best_shift;
        *f1 = fbest;
        return;
    }

    *sync = syncmax;
    for (i = 0; i < 162; i++) {
        fsum += fsymb[i] / 162.0f;
        f2sum += fsymb[i] * fsymb[i] / 162.0f;
    }
    fac = sqrtf(fmaxf(f2sum - fsum * fsum, 1e-12f));
    for (i = 0; i < 162; i++) {
        fsymb[i] = symfac * fsymb[i] / fac;
        if (fsymb[i] > 127.0f) fsymb[i] = 127.0f;
        if (fsymb[i] < -128.0f) fsymb[i] = -128.0f;
        symbols[i] = (unsigned char)(fsymb[i] + 128.0f);
    }
}

void noncoherent_sequence_detection(float *id, float *qd, long np,
                                    unsigned char *symbols, float *f1, int *shift1,
                                    float *drift1, int symfac, int *nblocksize, int *bitmetric)
{
    static float fplast = -10000.0f;
    static float dt = 1.0f / 375.0f;
    static float df = 375.0f / 256.0f;
    const float pi = (float)M_PI;
    const float twopidt = 2.0f * pi * dt;
    const float df15 = df * 1.5f;
    const float df05 = df * 0.5f;

    int i, j, k, lag, itone, ib, b, nblock, nseq, imask;
    float xi[512], xq[512];
    float is[4][162], qs[4][162], cf[4][162], sf[4][162], cm, sm, cmp, smp;
    float p[512], fac, xm1, xm0;
    float c0[257], s0[257], c1[257], s1[257], c2[257], s2[257], c3[257], s3[257];
    float dphi0, cdphi0, sdphi0, dphi1, cdphi1, sdphi1, dphi2, cdphi2, sdphi2, dphi3, cdphi3, sdphi3;
    float f0, fp, fsum = 0.0f, f2sum = 0.0f, fsymb[162];

    f0 = *f1;
    lag = *shift1;
    nblock = *nblocksize;
    nseq = 1 << nblock;
    int bitbybit = *bitmetric;

    for (i = 0; i < 162; i++) {
        fp = f0 + (*drift1 / 2.0f) * ((float)i - 81.0f) / 81.0f;
        if (i == 0 || (fp != fplast)) {
            dphi0 = twopidt * (fp - df15);
            cdphi0 = cosf(dphi0);
            sdphi0 = sinf(dphi0);

            dphi1 = twopidt * (fp - df05);
            cdphi1 = cosf(dphi1);
            sdphi1 = sinf(dphi1);

            dphi2 = twopidt * (fp + df05);
            cdphi2 = cosf(dphi2);
            sdphi2 = sinf(dphi2);

            dphi3 = twopidt * (fp + df15);
            cdphi3 = cosf(dphi3);
            sdphi3 = sinf(dphi3);

            c0[0] = 1.0f; s0[0] = 0.0f;
            c1[0] = 1.0f; s1[0] = 0.0f;
            c2[0] = 1.0f; s2[0] = 0.0f;
            c3[0] = 1.0f; s3[0] = 0.0f;
            for (j = 1; j < 257; j++) {
                c0[j] = c0[j - 1] * cdphi0 - s0[j - 1] * sdphi0;
                s0[j] = c0[j - 1] * sdphi0 + s0[j - 1] * cdphi0;
                c1[j] = c1[j - 1] * cdphi1 - s1[j - 1] * sdphi1;
                s1[j] = c1[j - 1] * sdphi1 + s1[j - 1] * cdphi1;
                c2[j] = c2[j - 1] * cdphi2 - s2[j - 1] * sdphi2;
                s2[j] = c2[j - 1] * sdphi2 + s2[j - 1] * cdphi2;
                c3[j] = c3[j - 1] * cdphi3 - s3[j - 1] * sdphi3;
                s3[j] = c3[j - 1] * sdphi3 + s3[j - 1] * cdphi3;
            }
            fplast = fp;
        }

        cf[0][i] = c0[256]; sf[0][i] = s0[256];
        cf[1][i] = c1[256]; sf[1][i] = s1[256];
        cf[2][i] = c2[256]; sf[2][i] = s2[256];
        cf[3][i] = c3[256]; sf[3][i] = s3[256];

        is[0][i] = qs[0][i] = is[1][i] = qs[1][i] = is[2][i] = qs[2][i] = is[3][i] = qs[3][i] = 0.0f;
        for (j = 0; j < 256; j++) {
            k = lag + i * 256 + j;
            if (k > 0 && k < np) {
                is[0][i] += id[k] * c0[j] + qd[k] * s0[j];
                qs[0][i] += -id[k] * s0[j] + qd[k] * c0[j];
                is[1][i] += id[k] * c1[j] + qd[k] * s1[j];
                qs[1][i] += -id[k] * s1[j] + qd[k] * c1[j];
                is[2][i] += id[k] * c2[j] + qd[k] * s2[j];
                qs[2][i] += -id[k] * s2[j] + qd[k] * c2[j];
                is[3][i] += id[k] * c3[j] + qd[k] * s3[j];
                qs[3][i] += -id[k] * s3[j] + qd[k] * c3[j];
            }
        }
    }

    for (i = 0; i < 162; i += nblock) {
        for (j = 0; j < nseq; j++) {
            xi[j] = 0.0f;
            xq[j] = 0.0f;
            cm = 1.0f;
            sm = 0.0f;
            for (ib = 0; ib < nblock; ib++) {
                b = (j & (1 << (nblock - 1 - ib))) >> (nblock - 1 - ib);
                itone = pr3[i + ib] + 2 * b;
                xi[j] += is[itone][i + ib] * cm + qs[itone][i + ib] * sm;
                xq[j] += qs[itone][i + ib] * cm - is[itone][i + ib] * sm;
                cmp = cf[itone][i + ib] * cm - sf[itone][i + ib] * sm;
                smp = sf[itone][i + ib] * cm + cf[itone][i + ib] * sm;
                cm = cmp;
                sm = smp;
            }
            p[j] = sqrtf(xi[j] * xi[j] + xq[j] * xq[j]);
        }
        for (ib = 0; ib < nblock; ib++) {
            imask = 1 << (nblock - 1 - ib);
            xm1 = 0.0f;
            xm0 = 0.0f;
            for (j = 0; j < nseq; j++) {
                if ((j & imask) != 0) {
                    if (p[j] > xm1) xm1 = p[j];
                } else {
                    if (p[j] > xm0) xm0 = p[j];
                }
            }
            fsymb[i + ib] = xm1 - xm0;
            if (bitbybit == 1) {
                const float denom = (xm1 > xm0) ? xm1 : xm0;
                if (denom > 1e-6f) fsymb[i + ib] /= denom;
            }
        }
    }

    for (i = 0; i < 162; i++) {
        fsum += fsymb[i] / 162.0f;
        f2sum += fsymb[i] * fsymb[i] / 162.0f;
    }
    fac = sqrtf(fmaxf(f2sum - fsum * fsum, 1e-12f));
    for (i = 0; i < 162; i++) {
        fsymb[i] = symfac * fsymb[i] / fac;
        if (fsymb[i] > 127.0f) fsymb[i] = 127.0f;
        if (fsymb[i] < -128.0f) fsymb[i] = -128.0f;
        symbols[i] = (unsigned char)(fsymb[i] + 128.0f);
    }
}

static void subtract_signal2(float *id, float *qd, long np,
                             float f0, int shift0, float drift0, unsigned char *channel_symbols)
{
    enum {
        kNsym = 162,
        kNsPerSym = 256,
        kNfilt = 360,
        kNc2 = 45000
    };
    const float dt = 1.0f / 375.0f;
    const float df = 375.0f / 256.0f;
    const float pi = (float)M_PI;
    const float twopidt = 2.0f * pi * dt;
    float phi = 0.0f;
    const int nsig = kNsym * kNsPerSym;

    float *refi = (float *)calloc((size_t)kNc2, sizeof(float));
    float *refq = (float *)calloc((size_t)kNc2, sizeof(float));
    float *ci = (float *)calloc((size_t)kNc2, sizeof(float));
    float *cq = (float *)calloc((size_t)kNc2, sizeof(float));
    float *cfi = (float *)calloc((size_t)kNc2, sizeof(float));
    float *cfq = (float *)calloc((size_t)kNc2, sizeof(float));
    if (!refi || !refq || !ci || !cq || !cfi || !cfq) goto cleanup;

    for (int i = 0; i < kNsym; ++i) {
        const float cs = (float)channel_symbols[i];
        const float dphi = twopidt * (
            f0 + (drift0 / 2.0f) * ((float)i - (float)kNsym / 2.0f) / ((float)kNsym / 2.0f)
            + (cs - 1.5f) * df
        );
        for (int j = 0; j < kNsPerSym; ++j) {
            const int ii = kNsPerSym * i + j;
            refi[ii] = cosf(phi);
            refq[ii] = sinf(phi);
            phi += dphi;
        }
    }

    float w[kNfilt];
    float partialsum[kNfilt];
    float norm = 0.0f;
    for (int i = 0; i < kNfilt; ++i) partialsum[i] = 0.0f;
    for (int i = 0; i < kNfilt; ++i) {
        w[i] = sinf(pi * (float)i / (float)(kNfilt - 1));
        norm += w[i];
    }
    if (fabsf(norm) < 1e-9f) norm = 1.0f;
    for (int i = 0; i < kNfilt; ++i) w[i] /= norm;
    for (int i = 1; i < kNfilt; ++i) partialsum[i] = partialsum[i - 1] + w[i];

    for (int i = 0; i < kNsym * kNsPerSym; ++i) {
        const int k = shift0 + i;
        if (k > 0 && k < np) {
            ci[i + kNfilt] = id[k] * refi[i] + qd[k] * refq[i];
            cq[i + kNfilt] = qd[k] * refi[i] - id[k] * refq[i];
        }
    }

    for (int i = kNfilt / 2; i < kNc2 - kNfilt / 2; ++i) {
        cfi[i] = 0.0f;
        cfq[i] = 0.0f;
        for (int j = 0; j < kNfilt; ++j) {
            cfi[i] += w[j] * ci[i - kNfilt / 2 + j];
            cfq[i] += w[j] * cq[i - kNfilt / 2 + j];
        }
    }

    for (int i = 0; i < nsig; ++i) {
        if (i < kNfilt / 2) norm = partialsum[kNfilt / 2 + i];
        else if (i > (nsig - 1 - kNfilt / 2)) norm = partialsum[kNfilt / 2 + nsig - 1 - i];
        else norm = 1.0f;
        const int k = shift0 + i;
        const int j = i + kNfilt;
        if (k > 0 && k < np) {
            id[k] -= (cfi[j] * refi[i] - cfq[j] * refq[i]) / norm;
            qd[k] -= (cfi[j] * refq[i] + cfq[j] * refi[i]) / norm;
        }
    }

cleanup:
    free(refi);
    free(refq);
    free(ci);
    free(cq);
    free(cfi);
    free(cfq);
}

static int parse_message_fields(const char *message, const char *callsign, char *grid_out, int *dbm_out)
{
    char buffer[24];
    char *parts[4] = {0};
    int count = 0;

    if (grid_out) grid_out[0] = '\0';
    if (dbm_out) *dbm_out = 0;
    if (!message || !callsign) return 0;

    snprintf(buffer, sizeof(buffer), "%s", message);
    char *tok = strtok(buffer, " ");
    while (tok && count < 4) {
        parts[count++] = tok;
        tok = strtok(NULL, " ");
    }
    if (count < 2) return 0;

    if (dbm_out) *dbm_out = atoi(parts[count - 1]);
    if (grid_out && count >= 3 && strlen(parts[1]) >= 4) {
        snprintf(grid_out, 8, "%s", parts[1]);
    }
    return 1;
}

static int result_freq_compare(const void *lhs, const void *rhs)
{
    const WsprResult *a = (const WsprResult *)lhs;
    const WsprResult *b = (const WsprResult *)rhs;
    if (a->freq_mhz < b->freq_mhz) return -1;
    if (a->freq_mhz > b->freq_mhz) return 1;
    return 0;
}

EMSCRIPTEN_KEEPALIVE
void wfweb_wspr_clear_hashes(void)
{
    memset(g_hashtab, 0, sizeof(g_hashtab));
    memset(g_loctab, 0, sizeof(g_loctab));
}

EMSCRIPTEN_KEEPALIVE
int wfweb_wspr_init(void)
{
    init_decoder();
    clear_error();
    return 1;
}

EMSCRIPTEN_KEEPALIVE
int wfweb_wspr_decode(float *samples,
                      int sample_count,
                      double dialfreq_mhz,
                      int audio_frequency_hz,
                      int search_half_width_hz)
{
    float *idat = NULL;
    float *qdat = NULL;
    float *ps = NULL;
    unsigned char *symbols = NULL;
    unsigned char *decdata = NULL;
    char *callsign = NULL;
    char *call_loc_pow = NULL;
    char *grid = NULL;
    int retval = 0;

    init_decoder();
    clear_error();
    reset_debug_state();
    g_result_count = 0;

    if (!samples || sample_count <= 0) {
        set_error("missing samples");
        return 0;
    }
    if (sample_count < WSPR_SAMPLE_RATE * 110) {
        set_error("not enough audio");
        return 0;
    }
    if (audio_frequency_hz <= 0) audio_frequency_hz = 1500;
    if (search_half_width_hz <= 0) search_half_width_hz = 150;
    if (search_half_width_hz < 50) search_half_width_hz = 50;
    if (search_half_width_hz > 150) search_half_width_hz = 150;

    g_debug_search_low_hz = -search_half_width_hz;
    g_debug_search_high_hz = search_half_width_hz;
    g_debug_sample_count = sample_count;

    idat = (float *)calloc(MAX_POINTS, sizeof(float));
    qdat = (float *)calloc(MAX_POINTS, sizeof(float));
    symbols = (unsigned char *)calloc(162, sizeof(unsigned char));
    decdata = (unsigned char *)calloc(11, sizeof(unsigned char));
    callsign = (char *)calloc(13, sizeof(char));
    call_loc_pow = (char *)calloc(24, sizeof(char));
    grid = (char *)calloc(8, sizeof(char));
    if (!idat || !qdat || !symbols || !decdata || !callsign || !call_loc_pow || !grid) {
        set_error("memory allocation failed");
        goto cleanup;
    }

    const int npoints = preprocess_samples(samples, sample_count, audio_frequency_hz, idat, qdat);
    g_debug_decimated_point_count = npoints;
    if (npoints < 42000) {
        set_error("preprocess failed");
        goto cleanup;
    }

    const float df = 375.0f / 512.0f;
    const float dt = 1.0f / 375.0f;
    const int nffts = 4 * (npoints / 512) - 1;
    g_debug_fft_count = nffts;
    if (nffts <= 0) {
        set_error("insufficient decimated data");
        goto cleanup;
    }

    ps = (float *)calloc((size_t)512 * (size_t)nffts, sizeof(float));
    if (!ps) {
        set_error("spectrum allocation failed");
        goto cleanup;
    }

    char allcalls[MAX_RESULTS][13];
    float allfreqs[MAX_RESULTS];
    memset(allcalls, 0, sizeof(allcalls));
    memset(allfreqs, 0, sizeof(allfreqs));
    float re[512];
    float im[512];
    float window[512];
    for (int i = 0; i < 512; ++i) window[i] = sinf(0.006147931f * (float)i);

    int decoded_in_previous_pass = 1;
    for (int decode_pass = 0; decode_pass < 2 && g_result_count < MAX_RESULTS; ++decode_pass) {
        if (decode_pass > 0 && !decoded_in_previous_pass) break;
        decoded_in_previous_pass = 0;
        g_debug_decode_pass_count = decode_pass + 1;

        for (int i = 0; i < nffts; ++i) {
            for (int j = 0; j < 512; ++j) {
                const int k = i * 128 + j;
                re[j] = idat[k] * window[j];
                im[j] = qdat[k] * window[j];
            }
            fft_inplace(re, im, 512);
            for (int j = 0; j < 512; ++j) {
                const int k = (j + 256) & 511;
                const float power = re[k] * re[k] + im[k] * im[k];
                ps[j * nffts + i] = power;
            }
        }

        float psavg[512];
        memset(psavg, 0, sizeof(psavg));
        for (int i = 0; i < nffts; ++i) {
            for (int j = 0; j < 512; ++j) {
                psavg[j] += ps[j * nffts + i];
            }
        }

        float smspec[411];
        for (int i = 0; i < 411; ++i) {
            smspec[i] = 0.0f;
            for (int j = -3; j <= 3; ++j) {
                const int k = 256 - 205 + i + j;
                smspec[i] += psavg[k];
            }
        }

        float tmpsort[411];
        memcpy(tmpsort, smspec, sizeof(tmpsort));
        qsort(tmpsort, 411, sizeof(float), floatcomp);
        float noise_level = tmpsort[122];
        if (noise_level <= 0.0f) noise_level = 1e-6f;

        const float min_snr = powf(10.0f, -8.0f / 10.0f);
        for (int j = 0; j < 411; ++j) {
            smspec[j] = smspec[j] / noise_level - 1.0f;
            if (smspec[j] < min_snr) smspec[j] = 0.1f * min_snr;
        }

        WsprCandidate candidates[MAX_CANDIDATES];
        memset(candidates, 0, sizeof(candidates));
        int npk = 0;
        const float snr_scaling_factor = 26.3f;
        for (int j = 1; j < 410 && npk < MAX_CANDIDATES; ++j) {
            if (smspec[j] > smspec[j - 1] && smspec[j] > smspec[j + 1]) {
                candidates[npk].freq = (j - 205) * df;
                candidates[npk].snr = 10.0f * log10f(smspec[j]) - snr_scaling_factor;
                npk++;
            }
        }

        g_debug_peak_candidate_count += npk;

        const float fmin = (float)g_debug_search_low_hz;
        const float fmax = (float)g_debug_search_high_hz;
        int filtered = 0;
        for (int j = 0; j < npk; ++j) {
            if (candidates[j].freq >= fmin && candidates[j].freq <= fmax) {
                candidates[filtered++] = candidates[j];
            }
        }
        npk = filtered;
        g_debug_filtered_candidate_count += npk;

        for (int pass = 1; pass <= npk - 1; ++pass) {
            for (int k = 0; k < npk - pass; ++k) {
                if (candidates[k].snr < candidates[k + 1].snr) {
                    WsprCandidate tmp = candidates[k];
                    candidates[k] = candidates[k + 1];
                    candidates[k + 1] = tmp;
                }
            }
        }

        const int maxdrift = 4;
        for (int j = 0; j < npk; ++j) {
            float smax = -1e30f;
            const int if0 = (int)lrintf(candidates[j].freq / df + 256.0f);
            for (int ifr = if0 - 2; ifr <= if0 + 2; ++ifr) {
                for (int k0 = -10; k0 < 22; ++k0) {
                    for (int idrift = -maxdrift; idrift <= maxdrift; ++idrift) {
                        float ss = 0.0f;
                        float pow = 0.0f;
                        for (int k = 0; k < 162; ++k) {
                            const int ifd = (int)lrintf(ifr + (((float)k - 81.0f) / 81.0f) * ((float)idrift) / (2.0f * df));
                            const int kindex = k0 + 2 * k;
                            if (ifd - 3 < 0 || ifd + 3 >= 512) continue;
                            if (kindex < 0 || kindex >= nffts) continue;
                            float p0 = sqrtf(ps[(ifd - 3) * nffts + kindex]);
                            float p1 = sqrtf(ps[(ifd - 1) * nffts + kindex]);
                            float p2 = sqrtf(ps[(ifd + 1) * nffts + kindex]);
                            float p3 = sqrtf(ps[(ifd + 3) * nffts + kindex]);
                            ss += (2 * pr3[k] - 1) * ((p1 + p3) - (p0 + p2));
                            pow += p0 + p1 + p2 + p3;
                        }
                        if (pow <= 0.0f) continue;
                        const float sync1 = ss / pow;
                        if (sync1 > smax) {
                            smax = sync1;
                            candidates[j].shift = 128 * (k0 + 1);
                            candidates[j].drift = (float)idrift;
                            candidates[j].freq = (ifr - 256) * df;
                            candidates[j].sync = sync1;
                        }
                    }
                }
            }
        }

        const float minsync1 = 0.10f;
        const float minsync2 = 0.12f;
        for (int j = 0; j < npk; ++j) {
            float f1 = candidates[j].freq;
            float drift1 = candidates[j].drift;
            int shift1 = candidates[j].shift;
            float sync1 = candidates[j].sync;

            int ifmin = 0, ifmax = 0;
            float fstep = 0.0f;
            int lagmin = shift1 - 128;
            int lagmax = shift1 + 128;
            int lagstep = 64;

            sync_and_demodulate(idat, qdat, npoints, symbols, &f1, ifmin, ifmax, fstep, &shift1,
                                lagmin, lagmax, lagstep, &drift1, 50, &sync1, 0);

            fstep = 0.25f; ifmin = -2; ifmax = 2;
            sync_and_demodulate(idat, qdat, npoints, symbols, &f1, ifmin, ifmax, fstep, &shift1,
                                lagmin, lagmax, lagstep, &drift1, 50, &sync1, 1);

            if (sync1 > minsync1) {
                lagmin = shift1 - 32;
                lagmax = shift1 + 32;
                lagstep = 16;
                fstep = 0.0f; ifmin = 0; ifmax = 0;
                sync_and_demodulate(idat, qdat, npoints, symbols, &f1, ifmin, ifmax, fstep, &shift1,
                                    lagmin, lagmax, lagstep, &drift1, 50, &sync1, 0);
                fstep = 0.05f; ifmin = -2; ifmax = 2;
                sync_and_demodulate(idat, qdat, npoints, symbols, &f1, ifmin, ifmax, fstep, &shift1,
                                    lagmin, lagmax, lagstep, &drift1, 50, &sync1, 1);
                candidates[j].freq = f1;
                candidates[j].shift = shift1;
                candidates[j].drift = drift1;
                candidates[j].sync = sync1;
            }
        }

        WsprCandidate refined[MAX_CANDIDATES];
        int nwat = 0;
        for (int j = 0; j < npk; ++j) {
            int dupe = -1;
            for (int k = 0; k < nwat; ++k) {
                if (fabsf(candidates[j].freq - refined[k].freq) < 0.05f &&
                    abs(candidates[j].shift - refined[k].shift) < 16) {
                    dupe = k;
                    break;
                }
            }
            if (dupe >= 0) {
                if (candidates[j].sync > refined[dupe].sync) refined[dupe] = candidates[j];
            } else if (candidates[j].sync > minsync2 && nwat < MAX_CANDIDATES) {
                refined[nwat++] = candidates[j];
            }
        }
        g_debug_refined_candidate_count += nwat;

        for (int j = 0; j < nwat && g_result_count < MAX_RESULTS; ++j) {
            float f1 = refined[j].freq;
            float drift1 = refined[j].drift;
            int shift1 = refined[j].shift;
            int not_decoded = 1;
            int cycles = 0;
            unsigned int metric = 0;
            unsigned int maxnp = 0;

            for (int ib = 1; ib <= 4 && not_decoded; ++ib) {
                int blocksize = (ib < 4) ? ib : 1;
                int bitmetric = (ib == 4) ? 1 : 0;
                for (int idt = 0; idt <= (128 / 8) && not_decoded; ++idt) {
                    int ii = (idt + 1) / 2;
                    if (idt % 2 == 1) ii = -ii;
                    ii *= 8;
                    int jittered_shift = shift1 + ii;

                    noncoherent_sequence_detection(idat, qdat, npoints, symbols, &f1,
                                                   &jittered_shift, &drift1, 50, &blocksize, &bitmetric);

                    float sq = 0.0f;
                    for (int i = 0; i < 162; ++i) {
                        const float y = (float)symbols[i] - 128.0f;
                        sq += y * y;
                    }
                    const float rms = sqrtf(sq / 162.0f);
                    const float minrms = 52.0f * (50.0f / 64.0f);
                    if (rms <= minrms) continue;

                    deinterleave(symbols);
                    const int rc = fano(&metric, (unsigned int *)&cycles, &maxnp, decdata, symbols, 81,
                                        g_mettab, 60, 10000);
                    if (rc != 0) continue;

                    signed char message[11];
                    for (int i = 0; i < 11; ++i) {
                        message[i] = (decdata[i] > 127) ? (signed char)(decdata[i] - 256) : (signed char)decdata[i];
                    }

                    memset(callsign, 0, 13);
                    memset(call_loc_pow, 0, 24);
                    memset(grid, 0, 8);
                    int noprint = unpk_(message, g_hashtab, g_loctab, call_loc_pow, callsign);
                    if (noprint) continue;

                    int dupe = 0;
                    for (int i = 0; i < g_result_count; ++i) {
                        if (!strcmp(callsign, allcalls[i]) && fabsf(f1 - allfreqs[i]) < 4.0f) {
                            dupe = 1;
                            break;
                        }
                    }
                    if (dupe) {
                        not_decoded = 0;
                        break;
                    }

                    WsprResult *out = &g_results[g_result_count];
                    memset(out, 0, sizeof(*out));
                    out->sync = refined[j].sync;
                    out->snr = refined[j].snr;
                    out->dt = jittered_shift * dt - 1.0f;
                    out->freq_mhz = dialfreq_mhz + ((double)audio_frequency_hz + (double)f1) / 1000000.0;
                    out->drift = (int)lrintf(drift1);
                    out->cycles = (unsigned int)cycles;
                    out->jitter = ii;
                    out->blocksize = blocksize + 3 * bitmetric;
                    out->metric = metric;
                    snprintf(out->message, sizeof(out->message), "%s", call_loc_pow);
                    snprintf(out->callsign, sizeof(out->callsign), "%s", callsign);
                    parse_message_fields(call_loc_pow, callsign, out->grid, &out->dbm);

                    snprintf(allcalls[g_result_count], sizeof(allcalls[g_result_count]), "%s", callsign);
                    allfreqs[g_result_count] = f1;
                    g_result_count++;
                    decoded_in_previous_pass = 1;
                    not_decoded = 0;

                    unsigned char channel_symbols[162];
                    if (get_wspr_channel_symbols(call_loc_pow, g_hashtab, g_loctab, channel_symbols)) {
                        subtract_signal2(idat, qdat, npoints, f1, shift1, drift1, channel_symbols);
                    }
                }
            }
        }
    }

    if (g_result_count > 1) {
        qsort(g_results, (size_t)g_result_count, sizeof(WsprResult), result_freq_compare);
    }

    retval = 1;

cleanup:
    free(idat);
    free(qdat);
    free(ps);
    free(symbols);
    free(decdata);
    free(callsign);
    free(call_loc_pow);
    free(grid);
    return retval;
}

EMSCRIPTEN_KEEPALIVE
int wfweb_wspr_get_result_count(void)
{
    return g_result_count;
}

EMSCRIPTEN_KEEPALIVE
int wfweb_wspr_get_debug_search_low_hz(void)
{
    return g_debug_search_low_hz;
}

EMSCRIPTEN_KEEPALIVE
int wfweb_wspr_get_debug_search_high_hz(void)
{
    return g_debug_search_high_hz;
}

EMSCRIPTEN_KEEPALIVE
int wfweb_wspr_get_debug_sample_count(void)
{
    return g_debug_sample_count;
}

EMSCRIPTEN_KEEPALIVE
int wfweb_wspr_get_debug_decimated_point_count(void)
{
    return g_debug_decimated_point_count;
}

EMSCRIPTEN_KEEPALIVE
int wfweb_wspr_get_debug_fft_count(void)
{
    return g_debug_fft_count;
}

EMSCRIPTEN_KEEPALIVE
int wfweb_wspr_get_debug_peak_candidate_count(void)
{
    return g_debug_peak_candidate_count;
}

EMSCRIPTEN_KEEPALIVE
int wfweb_wspr_get_debug_filtered_candidate_count(void)
{
    return g_debug_filtered_candidate_count;
}

EMSCRIPTEN_KEEPALIVE
int wfweb_wspr_get_debug_refined_candidate_count(void)
{
    return g_debug_refined_candidate_count;
}

EMSCRIPTEN_KEEPALIVE
int wfweb_wspr_get_debug_decode_pass_count(void)
{
    return g_debug_decode_pass_count;
}

EMSCRIPTEN_KEEPALIVE
const char *wfweb_wspr_get_error(void)
{
    return g_error;
}

EMSCRIPTEN_KEEPALIVE
double wfweb_wspr_get_result_freq_mhz(int index)
{
    if (index < 0 || index >= g_result_count) return 0.0;
    return g_results[index].freq_mhz;
}

EMSCRIPTEN_KEEPALIVE
float wfweb_wspr_get_result_snr(int index)
{
    if (index < 0 || index >= g_result_count) return 0.0f;
    return g_results[index].snr;
}

EMSCRIPTEN_KEEPALIVE
float wfweb_wspr_get_result_dt(int index)
{
    if (index < 0 || index >= g_result_count) return 0.0f;
    return g_results[index].dt;
}

EMSCRIPTEN_KEEPALIVE
float wfweb_wspr_get_result_sync(int index)
{
    if (index < 0 || index >= g_result_count) return 0.0f;
    return g_results[index].sync;
}

EMSCRIPTEN_KEEPALIVE
int wfweb_wspr_get_result_drift(int index)
{
    if (index < 0 || index >= g_result_count) return 0;
    return g_results[index].drift;
}

EMSCRIPTEN_KEEPALIVE
int wfweb_wspr_get_result_dbm(int index)
{
    if (index < 0 || index >= g_result_count) return 0;
    return g_results[index].dbm;
}

EMSCRIPTEN_KEEPALIVE
const char *wfweb_wspr_get_result_message(int index)
{
    if (index < 0 || index >= g_result_count) return "";
    return g_results[index].message;
}

EMSCRIPTEN_KEEPALIVE
const char *wfweb_wspr_get_result_callsign(int index)
{
    if (index < 0 || index >= g_result_count) return "";
    return g_results[index].callsign;
}

EMSCRIPTEN_KEEPALIVE
const char *wfweb_wspr_get_result_grid(int index)
{
    if (index < 0 || index >= g_result_count) return "";
    return g_results[index].grid;
}
