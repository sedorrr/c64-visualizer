/*
 * Visualizer modes + adaptive level mapping.
 *
 * Level mapping ("dynamics"): each meter keeps two running values in dB:
 *   ref   - the recent peak (jumps up instantly, falls slowly)
 *   floor - the recent average level (smoothed)
 * A value is shown as 0..1 between (floor - margin) and ref. The display therefore always
 * spreads the music over the whole height: average passages sit in the lower half, peaks
 * reach the top, whatever the absolute volume.
 */
#include "visuals.h"
#include "analysis.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "font5x7.h"
#include "font3x5.h"

const char *const VIS_NAMES[VIS_COUNT] = { "spectrum", "vu", "mirror", "waterfall", "scope", "fire", "plasma", "starfield", "raster",
                                            "scroller", "loader", "logo", "melody", "rain", "life", "pulse", "fountain" };

static float    s_amp[NUM_BANDS];
static float    s_level[NUM_BANDS];      /* 0..1, adaptive */
static float    s_vu;                    /* 0..1, adaptive */
/* stereo (g_stereo): the mono values above are the energy average of both sides; these are the sides */
static float    s_amp_l[NUM_BANDS], s_amp_r[NUM_BANDS];
static float    s_level_l[NUM_BANDS], s_level_r[NUM_BANDS], s_vu_l, s_vu_r;
static uint32_t s_frame;
static float    s_bass, s_mid, s_treble;  /* 0..1, adaptive, from s_level      */
static float    s_beat;                   /* 1 on a detected beat, decays to 0  */
static uint32_t s_rng = 0x2545F491u;
static float frand(void) { s_rng = s_rng * 1664525u + 1013904223u; return (s_rng >> 8) / 16777216.0f; }

/* ------------------------------------------------------------ colour helpers */
static uint32_t rgb(float r, float g, float b) {
    r = r < 0 ? 0 : (r > 1 ? 1 : r);
    g = g < 0 ? 0 : (g > 1 ? 1 : g);
    b = b < 0 ? 0 : (b > 1 ? 1 : b);
    return ((uint32_t)(r * 255) << 16) | ((uint32_t)(g * 255) << 8) | (uint32_t)(b * 255);
}
static uint32_t scale_colour(uint32_t c, float f) {
    if (f <= 0.0f) return 0;
    if (f > 1.0f) f = 1.0f;
    return ((uint32_t)(((c >> 16) & 0xFF) * f) << 16) | ((uint32_t)(((c >> 8) & 0xFF) * f) << 8) | (uint32_t)((c & 0xFF) * f);
}
static uint32_t hsv(float h, float v) {                        /* h in degrees, full saturation */
    h = fmodf(h, 360.0f); if (h < 0) h += 360.0f;
    float x = 1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f);
    float r = 0, g = 0, b = 0;
    if (h < 60) { r = 1; g = x; } else if (h < 120) { r = x; g = 1; } else if (h < 180) { g = 1; b = x; }
    else if (h < 240) { g = x; b = 1; } else if (h < 300) { r = x; b = 1; } else { r = 1; b = x; }
    return rgb(r * v, g * v, b * v);
}
static uint32_t heat(float h) {                                /* dark red -> orange -> yellow -> white */
    h = h < 0 ? 0 : (h > 1 ? 1 : h);
    if (h < 0.4f) return rgb(0.35f + h / 0.4f * 0.65f, h / 0.4f * 0.15f, 0);
    if (h < 0.8f) return rgb(1, 0.15f + (h - 0.4f) / 0.4f * 0.75f, 0);
    return rgb(1, 0.9f + (h - 0.8f) / 0.2f * 0.1f, (h - 0.8f) / 0.2f);
}
static uint32_t water(float h) {                               /* deep blue -> blue -> cyan -> white foam */
    static const float st[4][4] = { {0.00f, 0.00f, 0.02f, 0.25f}, {0.35f, 0.00f, 0.25f, 1.00f},
                                    {0.70f, 0.10f, 0.80f, 1.00f}, {1.00f, 0.85f, 1.00f, 1.00f} };
    h = h < 0 ? 0 : (h > 1 ? 1 : h);
    int i = h < st[1][0] ? 0 : (h < st[2][0] ? 1 : 2);
    float f = (h - st[i][0]) / (st[i + 1][0] - st[i][0]);
    return rgb(st[i][1] + (st[i + 1][1] - st[i][1]) * f, st[i][2] + (st[i + 1][2] - st[i][2]) * f,
               st[i][3] + (st[i + 1][3] - st[i][3]) * f);
}
static inline void put(uint32_t *fb, int x, int y, uint32_t c) {
    if ((unsigned)x < PANEL_W && (unsigned)y < PANEL_H) fb[y * PANEL_W + x] = c;
}

/* ------------------------------------------------------------ dynamics */
typedef struct { float ref, floor; } dyn_t;
static dyn_t s_sdyn, s_vdyn;
static float s_min_ref_db;

/* per-frame constants are tuned for 32 ms frames; scaled here to the real frame period */
#define K FRAME_K
static float s_spec_fall, s_spec_rate, s_vu_fall, s_vu_rate;
static float s_beat_decay, s_bass_rate, s_trail, s_span_decay;
static int   s_beat_cooldown, s_peak_hold, s_vu_hold;
static inline float rate_k(float r) { return 1.0f - powf(1.0f - r, K); }   /* smoothing factor per frame */

/* inverse of the LED gamma: a mode that was designed for plain (linear) LED values passes its
   colours through this, so after the driver's gamma correction the LEDs show exactly the old look */
static uint8_t s_ungamma[256];
static inline uint32_t ungamma(uint32_t c) {
    return ((uint32_t)s_ungamma[(c >> 16) & 0xFF] << 16) | ((uint32_t)s_ungamma[(c >> 8) & 0xFF] << 8) | s_ungamma[c & 0xFF];
}

static inline float to_db(float a) { return a > 1e-3f ? 20.0f * log10f(a) : -200.0f; }

static void dyn_update(dyn_t *d, float peak_db, float mean_db, float fall_db, float rate) {
    if (peak_db > d->ref) d->ref = peak_db; else d->ref -= fall_db;
    if (d->ref < s_min_ref_db) d->ref = s_min_ref_db;
    if (mean_db < d->ref - DYN_MAX_SPAN) mean_db = d->ref - DYN_MAX_SPAN;
    if (mean_db > d->ref) mean_db = d->ref;
    d->floor += (mean_db - d->floor) * rate;
}
static float dyn_map(const dyn_t *d, float db, float margin, float min_span) {
    float lo = d->floor - margin;
    if (lo > d->ref - min_span) lo = d->ref - min_span;
    if (lo < d->ref - DYN_MAX_SPAN) lo = d->ref - DYN_MAX_SPAN;
    float l = (db - lo) / (d->ref - lo);
    return l < 0.0f ? 0.0f : (l > 1.0f ? 1.0f : l);
}

void visuals_init(void) {
    s_min_ref_db = 20.0f * log10f(MIN_REF);
    for (int i = 0; i < 256; i++) s_ungamma[i] = (uint8_t)(255.0f * powf(i / 255.0f, 1.0f / WS_GAMMA) + 0.5f);
    s_spec_fall = SPEC_REF_FALL_DB * K;  s_spec_rate = rate_k(SPEC_FLOOR_RATE);
    s_vu_fall   = VU_REF_FALL_DB * K;    s_vu_rate   = rate_k(VU_FLOOR_RATE);
    s_beat_decay = powf(0.75f, K);       s_bass_rate = rate_k(0.15f);
    s_trail      = powf(0.35f, K);       s_span_decay = powf(0.97f, K);
    s_beat_cooldown = (int)(6.0f / K + 0.5f);
    s_peak_hold     = (int)(PEAK_HOLD_FRAMES / K + 0.5f);
    s_vu_hold       = (int)(20.0f / K + 0.5f);
    s_sdyn.ref = s_vdyn.ref = s_min_ref_db;
    s_sdyn.floor = s_vdyn.floor = s_min_ref_db - 12.0f;
}

static bool s_gate = true;                    /* false = no audio: everything is shown as silence */
void visuals_set_gate(bool open) { s_gate = open; }

static void analyse(void) {
    if (g_stereo) {
        analyze_spectrum_of(g_samples_r, s_amp_r);
        analyze_spectrum_of(g_samples, s_amp_l);           /* left last: the melody mode reads its FFT bins */
        for (int b = 0; b < NUM_BANDS; b++) s_amp[b] = sqrtf((s_amp_l[b] * s_amp_l[b] + s_amp_r[b] * s_amp_r[b]) * 0.5f);
    } else analyze_spectrum(s_amp);
    if (!s_gate) for (int b = 0; b < NUM_BANDS; b++) s_amp[b] = s_amp_l[b] = s_amp_r[b] = 0.0f;   /* noise stays dark */
    float db[NUM_BANDS], peak = -200.0f, sum = 0.0f;
    for (int b = 0; b < NUM_BANDS; b++) {
        db[b] = to_db(s_amp[b]);
        if (db[b] > peak) peak = db[b];
    }
    float lim = (peak > s_sdyn.ref ? peak : s_sdyn.ref) - DYN_MAX_SPAN;
    for (int b = 0; b < NUM_BANDS; b++) sum += db[b] > lim ? db[b] : lim;
    dyn_update(&s_sdyn, peak, sum / NUM_BANDS, s_spec_fall, s_spec_rate);
    for (int b = 0; b < NUM_BANDS; b++)
        s_level[b] = s_amp[b] > 0.0f ? dyn_map(&s_sdyn, db[b], SPEC_FLOOR_MARGIN, SPEC_MIN_SPAN) : 0.0f;
    if (g_stereo) for (int b = 0; b < NUM_BANDS; b++) {     /* both sides on the same scale: the balance stays visible */
        s_level_l[b] = s_amp_l[b] > 0.0f ? dyn_map(&s_sdyn, to_db(s_amp_l[b]), SPEC_FLOOR_MARGIN, SPEC_MIN_SPAN) : 0.0f;
        s_level_r[b] = s_amp_r[b] > 0.0f ? dyn_map(&s_sdyn, to_db(s_amp_r[b]), SPEC_FLOOR_MARGIN, SPEC_MIN_SPAN) : 0.0f;
    }

    float rl = s_gate ? analyze_rms() : 0.0f, rr = (s_gate && g_stereo) ? analyze_rms_of(g_samples_r) : 0.0f;
    float rdb = to_db(g_stereo ? sqrtf((rl * rl + rr * rr) * 0.5f) : rl);
    dyn_update(&s_vdyn, rdb, rdb, s_vu_fall, s_vu_rate);
    s_vu = rdb > -100.0f ? dyn_map(&s_vdyn, rdb, VU_FLOOR_MARGIN, VU_MIN_SPAN) : 0.0f;
    if (g_stereo) {
        float dl = to_db(rl), dr = to_db(rr);
        s_vu_l = dl > -100.0f ? dyn_map(&s_vdyn, dl, VU_FLOOR_MARGIN, VU_MIN_SPAN) : 0.0f;
        s_vu_r = dr > -100.0f ? dyn_map(&s_vdyn, dr, VU_FLOOR_MARGIN, VU_MIN_SPAN) : 0.0f;
    }

    /* band groups and a simple beat detector on the bass */
    float g[3] = {0, 0, 0};
    const int cut1 = NUM_BANDS * 3 / 16, cut2 = NUM_BANDS * 10 / 16;      /* ~100-220 Hz, ~220-1800 Hz, above */
    for (int b = 0; b < NUM_BANDS; b++) { int k = b < cut1 ? 0 : (b < cut2 ? 1 : 2); if (s_level[b] > g[k]) g[k] = s_level[b]; }
    s_bass = g[0]; s_mid = g[1]; s_treble = g[2];
    static float bass_avg = 0.0f; static int cooldown = 0;
    s_beat *= s_beat_decay;
    if (cooldown > 0) cooldown--;
    else if (s_bass > 0.55f && s_bass > bass_avg + 0.22f) { s_beat = 1.0f; cooldown = s_beat_cooldown; }
    bass_avg += (s_bass - bass_avg) * s_bass_rate;
}

/* ------------------------------------------------------------ 1: spectrum bars */
static uint32_t colour_for_row(int r) {
    if (r < (PANEL_H * 5) / 8) return COL_GREEN;
    if (r < (PANEL_H * 7) / 8) return COL_YELLOW;
    return COL_RED;
}
/* stereo: left channel on the left half, right on the right half, bass in the middle, highs to the edges */
static void render_spectrum_stereo(uint32_t *fb) {
    static float bar[PANEL_W_MAX], peak[PANEL_W_MAX];
    static int hold[PANEL_W_MAX];
    const int half = PANEL_W / 2;
    for (int x = 0; x < PANEL_W; x++) {
        int c = x < half ? half - 1 - x : x - half;            /* 0 = next to the middle */
        float h = (x < half ? s_level_l : s_level_r)[c * NUM_BANDS / half] * PANEL_H;
        if (h >= bar[x]) bar[x] = h; else { bar[x] -= BAR_FALL * K; if (bar[x] < 0) bar[x] = 0; }
        if (bar[x] >= peak[x]) { peak[x] = bar[x]; hold[x] = s_peak_hold; }
        else if (hold[x] > 0) hold[x]--;
        else { peak[x] -= PEAK_FALL * K; if (peak[x] < bar[x]) peak[x] = bar[x]; }
        int rows = (int)(bar[x] + 0.5f), prow = (int)(peak[x] + 0.5f) - 1;
        for (int r = 0; r < rows; r++) put(fb, x, PANEL_H - 1 - r, colour_for_row(r));
        if (prow >= rows && prow < PANEL_H) put(fb, x, PANEL_H - 1 - prow, COL_PEAK);
    }
}
static void render_spectrum(uint32_t *fb) {
    if (g_stereo) { render_spectrum_stereo(fb); return; }
    static float bar[NUM_BANDS], peak[NUM_BANDS];
    static int hold[NUM_BANDS];
    for (int b = 0; b < NUM_BANDS; b++) {
        float h = s_level[b] * PANEL_H;
        if (h >= bar[b]) bar[b] = h; else { bar[b] -= BAR_FALL * K; if (bar[b] < 0) bar[b] = 0; }
        if (bar[b] >= peak[b]) { peak[b] = bar[b]; hold[b] = s_peak_hold; }
        else if (hold[b] > 0) hold[b]--;
        else { peak[b] -= PEAK_FALL * K; if (peak[b] < bar[b]) peak[b] = bar[b]; }
        int rows = (int)(bar[b] + 0.5f), prow = (int)(peak[b] + 0.5f) - 1;
        for (int c = 0; c < BAND_BAR_W * BAND_COLS / BAND_COLS_PHYS && c < BAND_COLS; c++) {   /* across two matrices: twice as wide */
            int x = b * BAND_COLS + c;
            for (int r = 0; r < rows; r++) put(fb, x, PANEL_H - 1 - r, colour_for_row(r));
            if (prow >= rows && prow < PANEL_H) put(fb, x, PANEL_H - 1 - prow, COL_PEAK);
        }
    }
}

/* ------------------------------------------------------------ 2: VU */
/* one meter in rows y0..y1-1; ch selects its state (0 = mono/left, 1 = right) */
static void vu_meter(uint32_t *fb, float level, int ch, int y0, int y1) {
    static float bar[2], peak[2];
    static int hold[2];
    float len = level * PANEL_W;
    if (len >= bar[ch]) bar[ch] = len; else { bar[ch] -= VU_FALL * K; if (bar[ch] < 0) bar[ch] = 0; }
    if (bar[ch] >= peak[ch]) { peak[ch] = bar[ch]; hold[ch] = s_vu_hold; }
    else if (hold[ch] > 0) hold[ch]--;
    else { peak[ch] -= 0.4f * K; if (peak[ch] < bar[ch]) peak[ch] = bar[ch]; }
    int cols = (int)(bar[ch] + 0.5f), pcol = (int)(peak[ch] + 0.5f) - 1;
    for (int x = 0; x < cols; x++) {
        float pos = (float)x / PANEL_W;
        uint32_t c = pos < 0.60f ? COL_GREEN : (pos < 0.85f ? COL_YELLOW : COL_RED);
        for (int y = y0; y < y1; y++) put(fb, x, y, c);
    }
    if (pcol >= cols && pcol < PANEL_W) for (int y = y0; y < y1; y++) put(fb, pcol, y, COL_PEAK);
}
static void render_vu(uint32_t *fb) {
    if (g_stereo) { vu_meter(fb, s_vu_l, 0, 0, PANEL_H / 2); vu_meter(fb, s_vu_r, 1, PANEL_H / 2, PANEL_H); }   /* left on top */
    else vu_meter(fb, s_vu, 0, 0, PANEL_H);
}

/* ------------------------------------------------------------ 3: mirror */
static void render_mirror(uint32_t *fb) {
    static float h[PANEL_W_MAX / 2];
    const int half = PANEL_W / 2, top = PANEL_H / 2;         /* rows top-1 .. 0 go up, top .. H-1 go down */
    float hue0 = s_frame * 1.5f * K;
    static float hr[PANEL_W_MAX / 2];                              /* stereo: the right half follows the right channel */
    for (int c = 0; c < half; c++) {
        int b = c * NUM_BANDS / half;
        for (int side = 0; side < (g_stereo ? 2 : 1); side++) {
            float *hh = side ? hr : h;
            float t = (g_stereo ? (side ? s_level_r : s_level_l) : s_level)[b] * top;
            if (t >= hh[c]) hh[c] = t; else { hh[c] -= 0.35f * K; if (hh[c] < 0) hh[c] = 0; }
        }
        uint32_t col = hsv(hue0 + c * (240.0f / half), 1.0f);
        int nl = (int)(h[c] + 0.5f), nr = g_stereo ? (int)(hr[c] + 0.5f) : nl;
        for (int k = 0; k < nl; k++) {
            uint32_t ck = scale_colour(col, 1.0f - 0.18f * k);  /* slightly darker towards the tips */
            put(fb, half - 1 - c, top - 1 - k, ck); put(fb, half - 1 - c, top + k, ck);
        }
        for (int k = 0; k < nr; k++) {
            uint32_t ck = scale_colour(col, 1.0f - 0.18f * k);
            put(fb, half + c, top - 1 - k, ck);     put(fb, half + c, top + k, ck);
        }
    }
}

/* ------------------------------------------------------------ 4: waterfall */
static void render_waterfall(uint32_t *fb) {
    static uint32_t wf[PANEL_W_MAX * PANEL_H];
    static float pend[PANEL_H], acc = 0.0f;                     /* loudest level since the last scroll step */
    for (int r = 0; r < PANEL_H; r++)                           /* r = 0: bottom row = bass */
        for (int b = r * NUM_BANDS / PANEL_H; b < (r + 1) * NUM_BANDS / PANEL_H; b++) if (s_level[b] > pend[r]) pend[r] = s_level[b];
    for (acc += K; acc >= 1.0f; acc -= 1.0f) {                  /* scroll at the same speed for any frame rate */
        for (int y = 0; y < PANEL_H; y++) {
            memmove(&wf[y * PANEL_W], &wf[y * PANEL_W + 1], (PANEL_W - 1) * sizeof(uint32_t));
            float q = (pend[PANEL_H - 1 - y] - WF_THRESHOLD) / (1.0f - WF_THRESHOLD);   /* only the louder part lights up */
            wf[y * PANEL_W + PANEL_W - 1] = q <= 0.0f ? 0 : scale_colour(WF_PALETTE == 0 ? water(0.15f + 0.85f * q) : heat(0.15f + 0.85f * q), 0.35f + 0.65f * q);
        }
        for (int r = 0; r < PANEL_H; r++) pend[r] = 0.0f;
    }
    memcpy(fb, wf, (size_t)(PANEL_W * PANEL_H) * sizeof(uint32_t));
}

/* ------------------------------------------------------------ 5: oscilloscope */
static void render_scope(uint32_t *fb) {
    static uint32_t trail[PANEL_W_MAX * PANEL_H];
    static float span = 60.0f;
    const int step = 4;                                        /* 32 columns x 4 samples = 8 ms of audio */
    float mean = block_mean();
    int start = 0;
    for (int i = 0; i < BLOCK_SIZE / 2; i++)                   /* trigger on a rising zero crossing */
        if (g_samples[i] < mean && g_samples[i + 1] >= mean) { start = i; break; }
    float pk = 0.0f, mean_r = g_stereo ? block_mean_of(g_samples_r) : 0.0f;
    for (int x = 0; x < PANEL_W && s_gate; x++) {
        float d = fabsf(g_samples[start + x * step] - mean); if (d > pk) pk = d;
        if (g_stereo) { d = fabsf(g_samples_r[start + x * step] - mean_r); if (d > pk) pk = d; }
    }
    if (pk > span) span = pk; else span = span * s_span_decay + pk * (1.0f - s_span_decay);
    if (span < 40.0f) span = 40.0f;

    for (int i = 0; i < PANEL_W * PANEL_H; i++) trail[i] = scale_colour(trail[i], s_trail);   /* afterglow */
    const float mid = (PANEL_H - 1) / 2.0f;
    for (int ch = g_stereo ? 1 : 0; ch >= 0; ch--) {         /* stereo: right in blue first, left in green on top */
        const uint16_t *x0 = ch ? g_samples_r : g_samples;
        float m = ch ? mean_r : mean;
        uint32_t c_on = ch ? COL_SCOPE_R : COL_SCOPE, c_dim = ch ? COL_SCOPE_R_DIM : COL_SCOPE_DIM;
        int prev = -1;
        for (int x = 0; x < PANEL_W; x++) {
            float d = s_gate ? (x0[start + x * step] - m) / span : 0.0f;   /* no audio: flat line */
            int y = (int)lroundf(mid - d * mid);
            y = y < 0 ? 0 : (y >= PANEL_H ? PANEL_H - 1 : y);
            if (prev >= 0) {                                   /* connect steep parts with a vertical line */
                int a = prev < y ? prev : y, e = prev < y ? y : prev;
                for (int yy = a; yy <= e; yy++) trail[yy * PANEL_W + x] = c_dim;
            }
            trail[y * PANEL_W + x] = c_on;
            prev = y;
        }
    }
    memcpy(fb, trail, (size_t)(PANEL_W * PANEL_H) * sizeof(uint32_t));
}

/* ------------------------------------------------------------ 6: fire */
static void render_fire(uint32_t *fb) {
    static float heatbuf[(PANEL_H + 2) * PANEL_W_MAX];
    static float prev[PANEL_H * PANEL_W_MAX];                       /* previous simulation step, for blending */
    static float acc = 0.0f;
    #define HB(x, y) heatbuf[(y) * PANEL_W + (x)]
    for (acc += K; acc >= 1.0f; acc -= 1.0f) {                  /* simulate at the 32 ms base rate */
        memcpy(prev, heatbuf, sizeof prev);
        for (int x = 0; x < PANEL_W; x++) {                    /* two seed rows below the panel */
            float lv = s_level[x * NUM_BANDS / PANEL_W];
            float s = lv * (0.85f + 0.3f * frand()) * 1.5f;
            HB(x, PANEL_H) = s; HB(x, PANEL_H + 1) = s * 0.9f;
        }
        for (int y = 0; y < PANEL_H; y++) for (int x = 0; x < PANEL_W; x++) {
            float l = HB(x > 0 ? x - 1 : x, y + 1), m = HB(x, y + 1), r = HB(x < PANEL_W - 1 ? x + 1 : x, y + 1);
            float below2 = HB(x, y + 2 <= PANEL_H + 1 ? y + 2 : PANEL_H + 1);
            float v = (l + 2.0f * m + r + below2) / 5.0f - (0.03f + 0.045f * frand());
            HB(x, y) = v < 0.0f ? 0.0f : v;
        }
    }
    for (int y = 0; y < PANEL_H; y++) for (int x = 0; x < PANEL_W; x++) {
        float p = prev[y * PANEL_W + x];
        float v = p + (HB(x, y) - p) * acc;                     /* blend between the last two steps: smooth at 62 fps */
        uint32_t c = v < 0.06f ? 0 : scale_colour(heat(v * 1.1f), 0.25f + 0.75f * (v > 1 ? 1 : v));
        fb[y * PANEL_W + x] = FIRE_LINEAR ? ungamma(c) : c;
    }
    #undef HB
}

/* ------------------------------------------------------------ 7: plasma */
static void render_plasma(uint32_t *fb) {
    static float t = 0.0f, hue = 0.0f;
    t   += (0.03f + 0.22f * s_vu) * K;                         /* louder = faster */
    hue += (0.4f + 6.0f * s_bass) * K;                         /* bass pushes the colours round */
    float v_all = 0.18f + 0.62f * s_vu + 0.2f * s_beat;
    for (int y = 0; y < PANEL_H; y++) for (int x = 0; x < PANEL_W; x++) {
        float dx = x - (PANEL_W - 1) / 2.0f + 6.0f * sinf(t * 0.7f), dy = (y - 3.5f) * 2.0f;
        float v = sinf(x * 0.28f + t) + sinf(y * 0.55f - t * 1.3f)
                + sinf((x + y) * 0.17f + t * 0.8f) + sinf(sqrtf(dx * dx + dy * dy) * 0.35f - t * 1.7f);
        float bright = v_all * (0.55f + 0.45f * sinf(v * 1.6f));
        fb[y * PANEL_W + x] = hsv(hue + v * 55.0f, bright);
    }
}

/* ------------------------------------------------------------ 8: starfield */
#define NSTARS 48
typedef struct { float x, y, v; int dir; } star_t;
static void star_spawn(star_t *s) {
    s->x = PANEL_W / 2.0f + (frand() - 0.5f) * 3.0f;
    s->y = frand() * PANEL_H;
    s->v = 0.25f + frand() * 0.75f;
    s->dir = frand() < 0.5f ? -1 : 1;
}
static void render_starfield(uint32_t *fb) {
    static star_t st[NSTARS];
    static bool init = false;
    if (!init) { for (int i = 0; i < NSTARS; i++) { star_spawn(&st[i]); st[i].x = frand() * PANEL_W; } init = true; }
    float speed = 0.08f + 0.9f * s_vu * s_vu + 0.8f * s_beat;
    uint32_t warm = rgb(1.0f, 0.75f, 0.35f);
    for (int i = 0; i < NSTARS; i++) {
        star_t *s = &st[i];
        float dist = fabsf(s->x - PANEL_W / 2.0f) / (PANEL_W / 2.0f);   /* 0 centre .. 1 edge */
        s->x += s->dir * s->v * speed * K * (0.4f + 1.6f * dist);      /* accelerate outwards: depth */
        if (s->x < -1.0f || s->x > PANEL_W) { star_spawn(s); continue; }
        float b = (0.45f + 0.55f * dist) * (0.6f + 0.4f * s->v);
        uint32_t c = s_beat > 0.3f && (i & 3) == 0 ? warm : rgb(0.85f, 0.92f, 1.0f);
        int px = (int)s->x, py = (int)s->y;
        put(fb, px, py, scale_colour(c, b));
        if (speed * s->v > 0.35f) put(fb, px - s->dir, py, scale_colour(c, b * 0.35f));   /* streak when fast */
    }
}

/* ------------------------------------------------------------ 9: C64 raster bars */
static void render_raster(uint32_t *fb) {
    /* colour gradients per bar: edge, body, core (C64-demo style copper colours) */
    static const uint32_t pal[3][3] = {
        { 0x8B3A10, 0xFF7A20, 0xFFD040 },                      /* bass:   rust / orange / yellow   */
        { 0x2020A0, 0x4060FF, 0x70D0FF },                      /* mid:    blue / light blue / cyan */
        { 0x7A1A9A, 0xE040C0, 0xFFB0F0 },                      /* treble: purple / pink / rose     */
    };
    static float ph[3] = { 0.0f, 2.1f, 4.2f };
    const float lv[3] = { s_bass, s_mid, s_treble };
    for (int k = 0; k < 3; k++) {
        ph[k] += (0.05f + 0.07f * k + 0.25f * lv[k]) * K;      /* each bar swings at its own pace */
        float cx = (PANEL_W - 1) / 2.0f + sinf(ph[k]) * (PANEL_W / 2.0f - 3.0f);
        float bright = 0.15f + 0.85f * lv[k] + 0.3f * (k == 0 ? s_beat : 0.0f);
        for (int d = -3; d <= 3; d++) {                        /* 7 columns: edge, body, core, body, edge */
            int ad = d < 0 ? -d : d;
            uint32_t c = ad == 0 ? pal[k][2] : ad == 1 ? pal[k][1] : ad == 2 ? scale_colour(pal[k][1], 0.6f) : scale_colour(pal[k][0], 0.6f);
            int x = (int)lroundf(cx) + d;
            for (int y = 0; y < PANEL_H; y++) {
                float shade = 1.0f - 0.12f * fabsf(y - 3.5f);  /* rounded look top and bottom */
                uint32_t old = (x >= 0 && x < PANEL_W) ? fb[y * PANEL_W + x] : 0, nc = scale_colour(c, bright * shade);
                if (((nc >> 16) & 0xFF) + ((nc >> 8) & 0xFF) + (nc & 0xFF) >= ((old >> 16) & 0xFF) + ((old >> 8) & 0xFF) + (old & 0xFF))
                    put(fb, x, y, nc);                         /* the brighter bar wins where they cross */
            }
        }
    }
}

/* ------------------------------------------------------------ texts for scroller and logo */
static const char *s_scr = "C64 SOUND VISUALIZER - GREETINGS TO ALL SID FANS -";
static int         s_scr_len = 51;
static const char *s_logo = "C64";
static int         s_logo_len = 3;
void visuals_set_texts(const char *scroller, int scroller_len, const char *logo, int logo_len) {
    if (scroller && scroller_len > 0) { s_scr = scroller; s_scr_len = scroller_len; }
    if (logo && logo_len > 0)         { s_logo = logo;    s_logo_len = logo_len; }
}
static uint32_t mix_white(uint32_t c, float f) {             /* blend towards white */
    if (f <= 0) return c;
    if (f > 1) f = 1;
    uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    r += (uint32_t)((255 - r) * f); g += (uint32_t)((255 - g) * f); b += (uint32_t)((255 - b) * f);
    return (r << 16) | (g << 8) | b;
}
static inline uint32_t brighter(uint32_t a, uint32_t b) {     /* the brighter of two colours */
    uint32_t sa = ((a >> 16) & 0xFF) + ((a >> 8) & 0xFF) + (a & 0xFF), sb = ((b >> 16) & 0xFF) + ((b >> 8) & 0xFF) + (b & 0xFF);
    return sb > sa ? b : a;
}

/* ------------------------------------------------------------ 10: sine scroller */
static void render_scroller(uint32_t *fb) {
    static float off = 1e9f, phase = 0.0f;
    if (off > PANEL_W) off = PANEL_W;                          /* start (and a narrower picture: start again) */
    const int total = s_scr_len * 4;                           /* 3 columns + 1 gap per character */
    off -= (0.8f + 0.6f * s_vu) * K;                           /* louder = a bit faster */
    if (off < -total) off = PANEL_W;
    phase += (0.08f + 0.25f * s_mid) * K;
    float amp = 1.5f * (0.3f + 0.7f * s_bass);                 /* wave height follows the bass */
    float hue0 = s_frame * 2.0f * K;
    int xo = (int)floorf(off);
    for (int x = 0; x < PANEL_W; x++) {
        int col = x - xo;
        if (col < 0 || col >= total || col % 4 == 3) continue;
        uint8_t bits = font3x5_col(s_scr[col / 4], col % 4);
        if (!bits) continue;
        int cx = xo + (col / 4) * 4 + 1;                       /* centre column of this character */
        int y0 = (int)lroundf(1.5f + amp * sinf(phase + cx * 0.30f));  /* whole letters bob: stays readable */
        uint32_t c = mix_white(hsv(hue0 + x * 9.0f, 1.0f), 0.6f * s_beat);
        for (int r = 0; r < 5; r++) if (bits & (1u << r)) put(fb, x, y0 + r, c);
    }
}

/* ------------------------------------------------------------ 11: C64 tape loader stripes */
static uint32_t c64col(int i) {                               /* Pepto palette, brightened for LEDs */
    static const uint32_t P[16] = { 0x000000, 0xFFFFFF, 0x68372B, 0x70A4B2, 0x6F3D86, 0x588D43, 0x352879, 0xB8C76F,
                                    0x6F4F25, 0x433900, 0x9A6759, 0x444444, 0x6C6C6C, 0x9AD284, 0x6C5EB5, 0x959595 };
    uint32_t c = P[i & 15], r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF, m = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (!m) return 0;
    return ((r * 255 / m) << 16) | ((g * 255 / m) << 8) | (b * 255 / m);
}
static void render_loader(uint32_t *fb) {
    static const uint8_t pairs[][2] = { {3, 2}, {14, 6}, {7, 0}, {1, 0}, {13, 5}, {4, 0}, {10, 9} };   /* classic loader colours */
    static uint8_t col[PANEL_W_MAX];
    static int run = 0, pair = 0;
    static uint8_t cur = 0;
    static float acc = 0.0f;
    if (s_beat > 0.999f) pair = (int)(frand() * (sizeof pairs / sizeof pairs[0]));
    for (acc += (0.4f + 3.6f * s_vu * s_vu) * K; acc >= 1.0f; acc -= 1.0f) {   /* scroll: faster when loud */
        memmove(col, col + 1, PANEL_W - 1);
        if (run <= 0) { cur ^= 1; run = 1 + (int)(frand() * (1.0f + 8.0f * (1.0f - s_vu))); }  /* quiet = wide "pilot tone" */
        col[PANEL_W - 1] = cur;
        run--;
    }
    for (int x = 0; x < PANEL_W; x++) {
        uint32_t c = c64col(pairs[pair][col[x]]);
        for (int y = 0; y < PANEL_H; y++) put(fb, x, y, c);
    }
}

/* ------------------------------------------------------------ 12: logo equalizer */
static void render_logo(uint32_t *fb) {
    static float lvl[PANEL_W_MAX];
    static uint8_t mask[PANEL_W_MAX];                              /* column bits of the (scaled) word */
    static const char *built = NULL;
    static int built_w = 0;
    if (built != s_logo || built_w != PANEL_W) {               /* build the scaled word once (and for a new width) */
        built = s_logo; built_w = PANEL_W;
        memset(mask, 0, sizeof mask);
        int w = 0;
        for (int i = 0; i < s_logo_len; i++) { int gw; font_glyph_w(s_logo[i], &gw); w += gw; }
        int gaps = s_logo_len - 1, sx = 1;
        while ((sx + 1) * w + gaps <= PANEL_W) sx++;           /* widest integer stretch that fits */
        int x = (PANEL_W - (sx * w + gaps)) / 2;
        for (int i = 0; i < s_logo_len && x < PANEL_W; i++) {
            int gw; const uint8_t *g = font_glyph_w(s_logo[i], &gw);
            for (int c = 0; c < gw; c++) for (int k = 0; k < sx; k++, x++) if (x >= 0 && x < PANEL_W) mask[x] = g[c];
            x += 1;
        }
    }
    const int y0 = (PANEL_H - FONT_H) / 2;
    for (int x = 0; x < PANEL_W; x++) {
        float t = s_level[x * NUM_BANDS / PANEL_W] * FONT_H;   /* fill height in glyph rows */
        if (t >= lvl[x]) lvl[x] = t; else { lvl[x] -= BAR_FALL * K; if (lvl[x] < 0) lvl[x] = 0; }
        for (int r = 0; r < FONT_H; r++) {
            if (!(mask[x] & (1u << r))) continue;
            int from_bottom = FONT_H - 1 - r;
            uint32_t c = from_bottom < lvl[x] ? colour_for_row(from_bottom * PANEL_H / FONT_H) : 0x40404C;   /* dim outline: the word stays readable */
            put(fb, x, y0 + r, c);
        }
    }
}

/* ------------------------------------------------------------ 13: melody line */
static void render_melody(uint32_t *fb) {
    static uint32_t roll[PANEL_W_MAX * PANEL_H];
    static float acc = 0.0f, center = 72.0f, note = 0.0f, strength = 0.0f;
    static int prev_row = -1;
    /* strongest tone between 250 Hz and 2 kHz, refined by parabolic interpolation */
    const int k0 = (int)(250.0f * BLOCK_SIZE / SAMPLE_RATE), k1 = (int)(2000.0f * BLOCK_SIZE / SAMPLE_RATE);
    int km = k0; float pm = 0.0f, sum = 0.0f;
    for (int k = k0; k <= k1; k++) { float p = analysis_bin_power(k); sum += p; if (p > pm) { pm = p; km = k; } }
    float clarity = pm / (sum / (k1 - k0 + 1) + 1e-6f);
    if (s_vu > 0.05f && clarity > 4.0f) {
        float a = logf(analysis_bin_power(km - 1) + 1e-6f), b = logf(pm + 1e-6f), c = logf(analysis_bin_power(km + 1) + 1e-6f);
        float den = a - 2.0f * b + c;
        float d = den < -1e-6f ? 0.5f * (a - c) / den : 0.0f;   /* only for a real peak */
        d = d < -0.5f ? -0.5f : (d > 0.5f ? 0.5f : d);
        float f = (km + d) * SAMPLE_RATE / BLOCK_SIZE;
        static float hist[3];                                  /* median of the last 3 detections: steadier line */
        hist[0] = hist[1]; hist[1] = hist[2]; hist[2] = 69.0f + 12.0f * log2f(f / 440.0f);
        float m0 = hist[0], m1 = hist[1], m2 = hist[2];
        note = (m0 > m1) ? ((m1 > m2) ? m1 : (m0 > m2 ? m2 : m0)) : ((m0 > m2) ? m0 : (m1 > m2 ? m2 : m1));
        strength = s_vu;
        center += (note - center) * rate_k(0.04f);            /* the view follows the melody slowly */
    } else strength = 0.0f;
    for (acc += K; acc >= 1.0f; acc -= 1.0f) {                 /* scroll at the 32 ms base rate */
        for (int i = 0; i < PANEL_W * PANEL_H; i++) if (i % PANEL_W != PANEL_W - 1) roll[i] = scale_colour(roll[i + 1], 0.985f);  /* older = slightly dimmer */
        for (int y = 0; y < PANEL_H; y++) roll[y * PANEL_W + PANEL_W - 1] = 0;
        if (strength > 0.0f) {
            int row = (int)lroundf((PANEL_H - 1) / 2.0f - (note - center) / 2.5f);   /* 2.5 semitones per row: ~1.5 octaves visible */
            row = row < 0 ? 0 : (row >= PANEL_H ? PANEL_H - 1 : row);
            float pc = fmodf(note, 12.0f); if (pc < 0) pc += 12.0f;
            uint32_t col = hsv(pc * 30.0f, 0.6f + 0.4f * strength);     /* every note name has its colour */
            if (prev_row >= 0 && prev_row != row && abs(prev_row - row) <= 2) {   /* connect small steps, leave leaps open */
                int a = prev_row < row ? prev_row : row, e = prev_row < row ? row : prev_row;
                for (int y = a; y <= e; y++) roll[y * PANEL_W + PANEL_W - 1] = scale_colour(col, 0.35f);
            }
            roll[row * PANEL_W + PANEL_W - 1] = col;
            if (row + 1 < PANEL_H) roll[(row + 1) * PANEL_W + PANEL_W - 1] = brighter(roll[(row + 1) * PANEL_W + PANEL_W - 1], scale_colour(col, 0.3f));   /* soft glow below */
            prev_row = row;
        } else prev_row = -1;
    }
    memcpy(fb, roll, (size_t)(PANEL_W * PANEL_H) * sizeof(uint32_t));
}

/* ------------------------------------------------------------ 14: digital rain */
static void render_rain(uint32_t *fb) {
    typedef struct { float y, v; int len; bool on; } drop_t;
    static drop_t d[PANEL_W_MAX][2];
    for (int x = 0; x < PANEL_W; x++) {
        float l = s_level[x * NUM_BANDS / PANEL_W];
        float p = (0.01f + 0.25f * l * l) * K + (s_beat > 0.999f ? 0.35f : 0.0f);
        for (int k = 0; k < 2; k++) {
            drop_t *q = &d[x][k];
            if (!q->on) {
                if (frand() < p && (k == 0 || !d[x][0].on || d[x][0].y > 4.0f)) {
                    q->on = true; q->y = -1.0f; q->v = 0.25f + 0.55f * l + 0.1f * frand(); q->len = 2 + (int)(frand() * 4.0f);
                    p = 0.0f;                                  /* at most one new drop per column and frame */
                }
                continue;
            }
            q->y += q->v * K;
            if (q->y - q->len > PANEL_H) { q->on = false; continue; }
            int head = (int)floorf(q->y);
            for (int i = 0; i <= q->len; i++) {
                int y = head - i;
                if (y < 0 || y >= PANEL_H) continue;
                uint32_t c = i == 0 ? 0xC8FFC8 : scale_colour(0x00FF40, 0.8f * (1.0f - (float)i / (q->len + 1)));
                fb[y * PANEL_W + x] = brighter(fb[y * PANEL_W + x], c);
            }
        }
    }
}

/* ------------------------------------------------------------ 15: Game of Life */
static void render_life(uint32_t *fb) {
    static uint8_t grid[PANEL_W_MAX * PANEL_H], age[PANEL_W_MAX * PANEL_H];
    static float acc = 0.0f;
    int pop = 0;
    for (int i = 0; i < PANEL_W * PANEL_H; i++) pop += grid[i];
    if (s_beat > 0.999f || pop < 6) {                          /* seed on every beat, and when life dies out */
        int bx = 0; float bl = -1.0f;
        for (int b = 0; b < NUM_BANDS; b++) if (s_level[b] > bl) { bl = s_level[b]; bx = b * PANEL_W / NUM_BANDS; }
        int n = pop < 6 ? 24 : 6;
        for (int i = 0; i < n; i++) {
            int x = pop < 6 ? (int)(frand() * PANEL_W) : (bx + (int)(frand() * 5.0f) - 2 + PANEL_W) % PANEL_W;
            int y = (int)(frand() * PANEL_H);
            grid[y * PANEL_W + x] = 1; age[y * PANEL_W + x] = 0;
        }
    }
    for (acc += (0.25f + 0.5f * s_vu) * K; acc >= 1.0f; acc -= 1.0f) {   /* 8..23 generations per second */
        static uint8_t nx[PANEL_W_MAX * PANEL_H];
        for (int y = 0; y < PANEL_H; y++) for (int x = 0; x < PANEL_W; x++) {
            int n = 0;
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++)
                if (dx || dy) n += grid[((y + dy + PANEL_H) % PANEL_H) * PANEL_W + (x + dx + PANEL_W) % PANEL_W];
            int i = y * PANEL_W + x;
            nx[i] = grid[i] ? (n == 2 || n == 3) : (n == 3);
            age[i] = nx[i] ? (grid[i] ? (age[i] < 255 ? age[i] + 1 : 255) : 0) : 0;
        }
        memcpy(grid, nx, sizeof grid);
    }
    for (int y = 0; y < PANEL_H; y++) for (int x = 0; x < PANEL_W; x++) {
        int i = y * PANEL_W + x;
        if (!grid[i]) continue;
        uint32_t c = hsv(x * (240.0f / PANEL_W) + s_frame * K, 0.3f + 0.7f * s_level[x * NUM_BANDS / PANEL_W]);
        fb[i] = age[i] == 0 ? mix_white(c, 0.5f) : c;          /* newborn cells flash */
    }
}

/* ------------------------------------------------------------ 16: beat pulse */
static void render_pulse(uint32_t *fb) {
    typedef struct { float r, hue; bool on; } ring_t;
    static ring_t ring[4];
    static float hue = 0.0f;
    hue += (0.5f + 4.0f * s_mid) * K;
    if (s_beat > 0.999f) {                                      /* every beat sends a shock wave */
        int slot = 0;
        for (int i = 0; i < 4; i++) { if (!ring[i].on) { slot = i; break; } if (ring[i].r > ring[slot].r) slot = i; }
        ring[slot].on = true; ring[slot].r = 0.0f; ring[slot].hue = hue + 120.0f * s_bass;
    }
    const float cx = (PANEL_W - 1) / 2.0f, cy = (PANEL_H - 1) / 2.0f, rmax = PANEL_W / 2.0f + 4.0f;
    uint32_t bg = hsv(hue + 180.0f, 0.04f + 0.2f * s_mid);      /* calm glow in the colour of the mids */
    for (int i = 0; i < PANEL_W * PANEL_H; i++) fb[i] = bg;
    for (int i = 0; i < 4; i++) {
        if (!ring[i].on) continue;
        ring[i].r += (0.8f + 0.7f * s_vu) * K;
        if (ring[i].r > rmax) { ring[i].on = false; continue; }
        float fade = 1.0f - ring[i].r / rmax;
        for (int y = 0; y < PANEL_H; y++) for (int x = 0; x < PANEL_W; x++) {
            float dx = x - cx, dy = (y - cy) * 2.5f;            /* stretched: the wave reaches the ends of the panel */
            float diff = fabsf(sqrtf(dx * dx + dy * dy) - ring[i].r);
            if (diff < 1.5f) fb[y * PANEL_W + x] = brighter(fb[y * PANEL_W + x], hsv(ring[i].hue, (1.0f - diff / 1.5f) * fade));
        }
    }
    for (int y = 3; y <= 4; y++) for (int x = PANEL_W / 2 - 1; x <= PANEL_W / 2; x++)  /* the core beats with the bass */
        fb[y * PANEL_W + x] = brighter(fb[y * PANEL_W + x], hsv(hue, s_bass));
}

/* ------------------------------------------------------------ 17: particle fountain */
static void render_fountain(uint32_t *fb) {
    typedef struct { float x, y, vx, vy, life, hue; bool on; } part_t;
    #define NPART 160
    static part_t p[NPART];
    static int next = 0;
    for (int b = 0; b < NUM_BANDS; b++) {                       /* every band shoots sparks, louder = more and higher */
        float lv = s_level[b];
        float n = (lv * lv * 1.2f + 0.6f * s_beat * lv) * K;
        int cnt = (int)n + (frand() < n - (int)n ? 1 : 0);
        for (int k = 0; k < cnt; k++) {
            part_t *q = &p[next]; next = (next + 1) % NPART;
            q->on = true; q->life = 1.0f;
            q->x = b * BAND_COLS + 0.5f + (frand() - 0.5f) * 0.8f; q->y = PANEL_H - 0.5f;
            q->vx = (frand() - 0.5f) * 0.25f; q->vy = -(0.35f + 0.55f * lv + 0.1f * frand());
            q->hue = b * (240.0f / NUM_BANDS) + s_frame * 0.5f * K;
        }
    }
    for (int i = 0; i < NPART; i++) {
        part_t *q = &p[i];
        if (!q->on) continue;
        q->x += q->vx * K; q->y += q->vy * K; q->vy += 0.045f * K; q->life -= 0.02f * K;
        if (q->y >= PANEL_H || q->life <= 0.0f) { q->on = false; continue; }
        int x = (int)floorf(q->x), y = (int)floorf(q->y);
        if ((unsigned)x < PANEL_W && (unsigned)y < PANEL_H)
            fb[y * PANEL_W + x] = brighter(fb[y * PANEL_W + x], hsv(q->hue, 0.25f + 0.75f * q->life));
    }
    #undef NPART
}

/* ------------------------------------------------------------ dispatcher */
void visuals_analyse(void) {
    analyse();
    s_frame++;
}
bool visuals_beat(void) { return s_beat > 0.999f; }

void visuals_render(vis_t v, uint32_t *fb) {
    memset(fb, 0, PANEL_W * PANEL_H * sizeof(uint32_t));
    switch (v) {
        case VIS_SPECTRUM:  render_spectrum(fb);  break;
        case VIS_VU:        render_vu(fb);        break;
        case VIS_MIRROR:    render_mirror(fb);    break;
        case VIS_WATERFALL: render_waterfall(fb); break;
        case VIS_SCOPE:     render_scope(fb);     break;
        case VIS_FIRE:      render_fire(fb);      break;
        case VIS_PLASMA:    render_plasma(fb);    break;
        case VIS_STARFIELD: render_starfield(fb); break;
        case VIS_RASTER:    render_raster(fb);    break;
        case VIS_SCROLLER:  render_scroller(fb);  break;
        case VIS_LOADER:    render_loader(fb);    break;
        case VIS_LOGO:      render_logo(fb);      break;
        case VIS_MELODY:    render_melody(fb);    break;
        case VIS_RAIN:      render_rain(fb);      break;
        case VIS_LIFE:      render_life(fb);      break;
        case VIS_PULSE:     render_pulse(fb);     break;
        case VIS_FOUNTAIN:  render_fountain(fb);  break;
        default: break;
    }
}

void visuals_frame(vis_t v, uint32_t *fb) {
    visuals_analyse();
    visuals_render(v, fb);
}
