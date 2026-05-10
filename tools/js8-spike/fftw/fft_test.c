/* Mirrors JS8.cpp's FFT usage: single-precision (fftwf_*),
 * 1D forward complex DFT created with FFTW_ESTIMATE. */
#include <fftw3.h>
#include <math.h>
#include <stdio.h>

int main(void) {
    const int N = 64;
    fftwf_complex *in  = fftwf_alloc_complex(N);
    fftwf_complex *out = fftwf_alloc_complex(N);
    fftwf_plan plan = fftwf_plan_dft_1d(N, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
    if (!plan) { fprintf(stderr, "plan failed\n"); return 1; }

    /* Pure tone at bin 5 — DFT magnitude should peak at bin 5 (and N-5). */
    for (int i = 0; i < N; ++i) {
        in[i][0] = cosf(2.0f * (float)M_PI * 5.0f * (float)i / (float)N);
        in[i][1] = sinf(2.0f * (float)M_PI * 5.0f * (float)i / (float)N);
    }
    fftwf_execute(plan);

    /* Print bins 0..10 magnitude — bin 5 should be ~N, others ~0. */
    int peak = -1; float peakMag = 0;
    for (int i = 0; i < N; ++i) {
        float mag = sqrtf(out[i][0]*out[i][0] + out[i][1]*out[i][1]);
        if (mag > peakMag) { peakMag = mag; peak = i; }
        if (i < 10) printf("bin %2d: %.2f\n", i, mag);
    }
    printf("peak bin=%d magnitude=%.2f (expected bin=5, mag~=%d)\n",
           peak, peakMag, N);

    fftwf_destroy_plan(plan);
    fftwf_free(in); fftwf_free(out);
    return (peak == 5 && peakMag > 0.99f * N) ? 0 : 1;
}
