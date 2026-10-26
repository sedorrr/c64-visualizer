/* FFT band analysis and RMS - portable C, also used by the host tests. */
#include <math.h>
#include "analysis.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

uint16_t g_samples[BLOCK_SIZE];
uint16_t g_samples_r[BLOCK_SIZE];
bool     g_stereo = false;
bool     g_clipped;

/* ---------------------------------------------------------------- analysis */
#if (BLOCK_SIZE & (BLOCK_SIZE - 1)) != 0
#error "BLOCK_SIZE must be a power of two (FFT)"
#endif

static float    g_window[BLOCK_SIZE];
static float    g_win_sum;
static float    g_tw_cos[BLOCK_SIZE / 2], g_tw_sin[BLOCK_SIZE / 2];
static uint16_t g_bitrev[BLOCK_SIZE];
static float    g_re[BLOCK_SIZE], g_im[BLOCK_SIZE];
static int      g_band_lo[NUM_BANDS], g_band_hi[NUM_BANDS];   /* FFT bin range of each band */
static float    g_band_gain[NUM_BANDS];
static float    g_band_gate[NUM_BANDS];                       /* expected noise amplitude per band */

void analysis_init(void) {
    /* Hann window reduces spectral leakage */
    g_win_sum = 0.0f;
    for (int i = 0; i < BLOCK_SIZE; i++) {
        g_window[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / (BLOCK_SIZE - 1)));
        g_win_sum += g_window[i];
    }
    /* FFT tables */
    int bits = 0;
    while ((1 << bits) < BLOCK_SIZE) bits++;
    for (int i = 0; i < BLOCK_SIZE; i++) {
        int r = 0;
        for (int b = 0; b < bits; b++) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        g_bitrev[i] = (uint16_t)r;
    }
    for (int k = 0; k < BLOCK_SIZE / 2; k++) {
        float a = -2.0f * (float)M_PI * k / BLOCK_SIZE;
        g_tw_cos[k] = cosf(a);
        g_tw_sin[k] = sinf(a);
    }
    /* log-spaced bands: every band covers the whole range between its neighbours, so no tone falls through a gap */
    float ratio = powf(FREQ_MAX / FREQ_MIN, 1.0f / (NUM_BANDS - 1));
    float df = (float)SAMPLE_RATE / BLOCK_SIZE;
    for (int b = 0; b < NUM_BANDS; b++) {
        float c    = FREQ_MIN * powf(ratio, (float)b);
        float f_lo = c / sqrtf(ratio);
        float f_hi = c * sqrtf(ratio);
        int lo = (int)(f_lo / df + 0.5f);
        int hi = (int)(f_hi / df + 0.5f) - 1;
        if (lo < 1) lo = 1;
        if (hi < lo) hi = lo;
        if (hi > BLOCK_SIZE / 2 - 1) hi = BLOCK_SIZE / 2 - 1;
        g_band_lo[b] = lo;
        g_band_hi[b] = hi;
        g_band_gain[b] = powf(c / 1000.0f, SPECTRUM_TILT_DB_PER_OCT / 6.0206f);   /* SID spectrum falls with frequency */
        /* white noise of NOISE_GATE_LSB rms gives ~0.0625*sigma per FFT bin; a band sums sqrt(bins) of them */
        g_band_gate[b] = NOISE_GATE_LSB * 0.0625f * sqrtf((float)(hi - lo + 1)) * 1.5f;
    }
}

float block_mean_of(const uint16_t *x) {
    float m = 0.0f;
    for (int i = 0; i < BLOCK_SIZE; i++) m += x[i];
    return m / BLOCK_SIZE;
}
float block_mean(void) { return block_mean_of(g_samples); }

/* amplitude (in ADC LSB) per band: FFT, then energy summed over all bins of the band */
void analyze_spectrum_of(const uint16_t *x, float *amp) {
    float mean = block_mean_of(x);
    for (int i = 0; i < BLOCK_SIZE; i++) {
        g_re[g_bitrev[i]] = ((float)x[i] - mean) * g_window[i];
        g_im[g_bitrev[i]] = 0.0f;
    }
    for (int len = 2; len <= BLOCK_SIZE; len <<= 1) {              /* iterative radix-2 FFT */
        int half = len >> 1, step = BLOCK_SIZE / len;
        for (int i = 0; i < BLOCK_SIZE; i += len) {
            for (int j = 0; j < half; j++) {
                float wr = g_tw_cos[j * step], wi = g_tw_sin[j * step];
                int a = i + j, b = a + half;
                float vr = g_re[b] * wr - g_im[b] * wi;
                float vi = g_re[b] * wi + g_im[b] * wr;
                g_re[b] = g_re[a] - vr;  g_im[b] = g_im[a] - vi;
                g_re[a] += vr;           g_im[a] += vi;
            }
        }
    }
    const float scale = 2.0f / g_win_sum / 1.2247f;                /* bin -> amplitude; 1.2247 = sqrt(1.5): Hann main-lobe energy */
    for (int b = 0; b < NUM_BANDS; b++) {
        float e = 0.0f;
        for (int k = g_band_lo[b]; k <= g_band_hi[b]; k++) e += g_re[k] * g_re[k] + g_im[k] * g_im[k];
        float a = sqrtf(e) * scale;
        a = (a > g_band_gate[b]) ? (a - g_band_gate[b]) : 0.0f;      /* noise gate */
        amp[b] = a * g_band_gain[b];
    }
}

void analyze_spectrum(float *amp) { analyze_spectrum_of(g_samples, amp); }

float analyze_rms(void) { return analyze_rms_of(g_samples); }
float analyze_rms_of(const uint16_t *x) {
    float mean = block_mean_of(x), sum = 0.0f;
    for (int i = 0; i < BLOCK_SIZE; i++) {
        float d = (float)x[i] - mean;
        sum += d * d;
    }
    float rms = sqrtf(sum / BLOCK_SIZE);
    return (rms > NOISE_GATE_LSB) ? (rms - NOISE_GATE_LSB) : 0.0f;      /* noise gate */
}


/* power of one FFT bin of the last analyze_spectrum() call (bins 0 .. BLOCK_SIZE/2-1) */
float analysis_bin_power(int k) {
    if (k < 0 || k >= BLOCK_SIZE / 2) return 0.0f;
    return g_re[k] * g_re[k] + g_im[k] * g_im[k];
}

