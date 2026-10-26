/* stereo check: synthetic left/right signals for every case seen in practice */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "config.h"
#include "stereo.h"
static int fails = 0;
static void check(int ok, const char *what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }

static long t = 0;
static float noise(float a) { return a * ((rand() % 2001) / 1000.0f - 1.0f); }
/* a SID-like voice: square wave with a slow melody, amplitude ~400 LSB */
static float tune(int which, double n) {
    float f = which ? 330.0f : 220.0f, step = (float)((long)n / 4000 % 4);
    f *= 1.0f + 0.25f * step;
    float ph = fmodf((float)(n * f / SAMPLE_RATE), 1.0f);
    return (ph < 0.5f ? 1.0f : -1.0f) * 400.0f;
}
/* feeds seconds of audio: right = (copy of left * k, delay in samples) + own voice * v + noise */
static stereo_state_t feed(stereo_t *s, float seconds, int left_on, float copy_k, float own_v, float r_noise, float delay) {
    uint16_t l[HOP_SIZE], r[HOP_SIZE];
    stereo_state_t st = s->state;
    for (int h = 0; h < (int)(seconds * SAMPLE_RATE / HOP_SIZE); h++) {
        for (int i = 0; i < HOP_SIZE; i++, t++) {
            float a = left_on ? tune(0, t) : 0.0f;
            float b = (left_on ? copy_k * tune(0, t - delay) : 0.0f) + own_v * tune(1, t) + noise(r_noise);
            l[i] = (uint16_t)(2048 + a + noise(2));
            r[i] = (uint16_t)(2048 + b + noise(2));
        }
        st = stereo_update(s, l, r, HOP_SIZE);
    }
    return st;
}
int main(void) {
    stereo_t s; srand(1);
    printf("== decisions after ~2-4 s of music\n");
    stereo_reset(&s); check(feed(&s, 4, 1, 0, 1, 0, 0) == ST_STEREO, "two different voices -> stereo");
    stereo_reset(&s); check(feed(&s, 4, 1, 0.7f, 0.5f, 0, 0) == ST_STEREO, "partly the same (panned mix) -> stereo");
    printf("   (correlation of the panned mix: %.2f)\n", s.corr);
    stereo_reset(&s); check(feed(&s, 4, 1, 0, 0, 3, 0) == ST_EMPTY_R, "GP27 with bias but no signal -> right empty");
    stereo_reset(&s); check(feed(&s, 4, 0, 0, 1, 0, 0) == ST_EMPTY_L, "signal only on the right -> left empty");
    stereo_reset(&s); check(feed(&s, 4, 1, 1, 0, 0, 0) == ST_COPY, "the same signal on both sides -> copy");
    stereo_reset(&s); check(feed(&s, 4, 1, 0.35f, 0, 3, 0.125f) == ST_COPY, "crosstalk of an open input (weaker, 7.8 us late) -> copy");
    printf("   (correlation of the crosstalk: %.2f, level right/left %.2f)\n", s.corr, s.ratio);
    stereo_reset(&s); check(feed(&s, 10, 0, 0, 0, 0, 0) == ST_CHECK, "silence: still checking (says nothing)");

    printf("== no flicker\n");
    stereo_reset(&s); feed(&s, 6, 1, 0, 1, 0, 0);
    check(feed(&s, 5, 1, 1, 0, 0, 0) == ST_STEREO, "stereo tune with a 5 s mono passage stays stereo");
    check(feed(&s, 30, 1, 0.45f, 0, 0, 0) == ST_STEREO, "... also 30 s with only one SID (partly panned: right = 45 % of left)");
    check(feed(&s, 40, 1, 0.45f, 0, 0, 0) == ST_COPY, "... after more than 60 s of only one SID it switches to mono");
    stereo_reset(&s); feed(&s, 6, 1, 0, 1, 0, 0);
    stereo_hint(&s, 0);
    check(feed(&s, 2.5f, 1, 1, 0, 0, 0) == ST_COPY, "the jukebox starts a mono tune: decided after the first window");
    stereo_hint(&s, 0);
    check(feed(&s, 2.5f, 1, 0, 1, 0, 0) == ST_STEREO, "and the next stereo tune: stereo after the first window");
    stereo_reset(&s); feed(&s, 6, 1, 1, 0, 0, 0);
    check(feed(&s, 8, 1, 0, 1, 0, 0) == ST_STEREO, "and back to stereo within a few seconds of real stereo");
    stereo_reset(&s); feed(&s, 6, 1, 0, 1, 0, 0);
    check(feed(&s, 30, 0, 0, 0, 0, 0) == ST_STEREO, "a pause between tunes keeps the state");
    printf("== the SID header from the jukebox decides\n");
    stereo_reset(&s); stereo_hint(&s, 2);
    check(s.state == ST_FILE_STEREO, "2 SIDs: stereo at once, before any music");
    check(feed(&s, 30, 1, 0.45f, 0, 0, 0) == ST_FILE_STEREO, "2 SIDs: 30 s with only one SID stays stereo");
    check(feed(&s, 6, 1, 0, 0, 3, 0) == ST_FILE_STEREO, "2 SIDs: right silent for 6 s - still stereo");
    check(feed(&s, 8, 1, 0, 0, 3, 0) == ST_EMPTY_R, "2 SIDs: right silent for ~10 s - nothing arrives, mono");
    check(feed(&s, 3, 1, 0, 1, 0, 0) == ST_FILE_STEREO, "2 SIDs: right plays again - stereo after one window");
    stereo_hint(&s, 1);
    check(s.state == ST_FILE_MONO, "1 SID: mono at once");
    check(feed(&s, 20, 1, 0, 1, 0, 0) == ST_FILE_MONO, "1 SID: stays mono even with different signals");
    stereo_hint(&s, 0);
    check(s.state == ST_CHECK, "playback ended: measuring again");
    check(feed(&s, 2.5f, 1, 0, 1, 0, 0) == ST_STEREO, "... and the first window decides");
    printf("%s\n", fails ? "SOME STEREO CHECKS FAILED" : "all stereo checks passed");
    return fails ? 1 : 0;
}
