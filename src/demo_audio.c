/*
 * Demo audio source: a small SID-style chiptune synthesised on the Pico itself, so the
 * visualizer modes can be shown without any audio input. Drop-in replacement for
 * audio_capture.c: capture_block() waits for the next 16 ms frame and slides HOP_SIZE
 * freshly generated samples into g_samples (scaled like the real ADC input).
 *
 * Tune: Am - F - C - G, 125 BPM. 50 Hz square arpeggios, filtered-sounding square bass,
 * sawtooth lead with vibrato from bar 5, kick / snare / hi-hat from noise and sine.
 */
#include <math.h>
#include <string.h>
#include "pico/stdlib.h"
#include "analysis.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define STEP_SAMPLES ((int)(SAMPLE_RATE * 60.0f / 125.0f / 4.0f))   /* one 16th note */
#define LSB_PER_UNIT 1240.0f                                         /* like ~1 V peak at the ADC */

static const uint8_t CHORD[4][3] = { {57, 60, 64}, {53, 57, 60}, {55, 60, 64}, {55, 59, 62} };
static const uint8_t BASS[4] = { 33, 29, 36, 31 };
#define H_ (-1)
static const int8_t MELODY[4][16] = {
    { 76, H_, H_, 74, 72, H_, 69, H_, 72, H_, 74, H_, 76, H_, H_, H_ },
    { 77, H_, H_, 76, 74, H_, 72, H_, 69, H_, 72, H_, 74, H_, H_, H_ },
    { 72, H_, H_, 74, 76, H_, 79, H_, 76, H_, 74, H_, 72, H_, H_, H_ },
    { 74, H_, H_, 72, 71, H_, 67, H_, 71, H_, 74, H_, 79, H_, H_, 0 } };

static float    s_freq[128];
static uint32_t s_n;                                   /* sample counter */
static float    s_ph_arp, s_ph_bass, s_ph_lead, s_ph_kick, s_lead_f;
static bool     s_lead_on;
static int32_t  s_kick_t = 1 << 30, s_snare_t = 1 << 30, s_hat_t = 1 << 30;
static uint32_t s_rng = 22222;
static absolute_time_t s_next;
static bool     s_started;

static inline float noise(void) { s_rng = s_rng * 1664525u + 1013904223u; return (int32_t)s_rng / 2147483648.0f; }
static inline float square(float ph) { return ph - floorf(ph) < 0.5f ? 1.0f : -1.0f; }

static float next_sample(void) {
    int step = (int)(s_n / STEP_SAMPLES), t_in = (int)(s_n % STEP_SAMPLES);
    int bar = (step / 16) % 8, i16 = step % 16;
    const uint8_t *ch = CHORD[bar % 4];

    if (t_in == 0) {                                   /* events at the start of each 16th */
        if (bar >= 4) {
            int v = MELODY[bar % 4][i16];
            if (v > 0) { s_lead_f = s_freq[v]; s_lead_on = true; }
            else if (v == 0) s_lead_on = false;
        } else s_lead_on = false;
        if (i16 == 0 || i16 == 8 || i16 == 10) { s_kick_t = 0; s_ph_kick = 0; }
        if (i16 == 4 || i16 == 12) s_snare_t = 0;
        if (i16 & 1) s_hat_t = 0;
    }
    float out = 0.0f, inv = 1.0f / SAMPLE_RATE;

    int k = (t_in / (STEP_SAMPLES / 6) + step * 6) % 3;                  /* 50 Hz arpeggio */
    s_ph_arp += s_freq[ch[k] + 12] * inv;
    out += 0.05f * square(s_ph_arp);

    int note = BASS[bar % 4] + ((i16 % 4) >= 2 ? 12 : 0);                 /* bass, octave bounce on 8ths */
    float tb = ((i16 % 2) * STEP_SAMPLES + t_in) * inv;
    s_ph_bass += s_freq[note] * inv;
    out += 0.24f * expf(-tb * 8.0f) * square(s_ph_bass);

    if (s_lead_on) {                                                    /* saw lead with vibrato */
        float vib = 1.0f + 0.007f * sinf(2.0f * (float)M_PI * 5.5f * s_n * inv);
        s_ph_lead += s_lead_f * vib * inv;
        out += 0.12f * (2.0f * (s_ph_lead - floorf(s_ph_lead)) - 1.0f);
    }
    if (s_kick_t < SAMPLE_RATE / 5) {
        float t = s_kick_t * inv;
        s_ph_kick += 150.0f * expf(-t * 12.0f) * inv;
        out += 0.55f * sinf(2.0f * (float)M_PI * s_ph_kick) * expf(-t * 25.0f);
    }
    if (s_snare_t < SAMPLE_RATE / 5) out += 0.28f * noise() * expf(-s_snare_t * inv * 30.0f);
    if (s_hat_t < SAMPLE_RATE / 20)  out += 0.06f * noise() * expf(-s_hat_t * inv * 80.0f);
    s_kick_t++; s_snare_t++; s_hat_t++;
    s_n++;
    return out;
}

void audio_init(void) {
    for (int m = 0; m < 128; m++) s_freq[m] = 440.0f * powf(2.0f, (m - 69) / 12.0f);
    for (int i = 0; i < BLOCK_SIZE; i++) g_samples[i] = 2048;
}

void capture_block(void) {
    if (!s_started) { s_next = get_absolute_time(); s_started = true; }
    s_next = delayed_by_us(s_next, (uint64_t)HOP_SIZE * 1000000u / SAMPLE_RATE);
    sleep_until(s_next);                               /* real-time pacing: one hop per 16 ms */
    memmove(g_samples, g_samples + HOP_SIZE, (BLOCK_SIZE - HOP_SIZE) * sizeof(uint16_t));
    uint16_t *dst = g_samples + (BLOCK_SIZE - HOP_SIZE);
    for (int i = 0; i < HOP_SIZE; i++) {
        float v = 2048.0f + next_sample() * LSB_PER_UNIT + noise() * 3.0f;
        dst[i] = (uint16_t)(v < 0 ? 0 : (v > 4095 ? 4095 : v));
    }
    g_clipped = false;
}
