/* Jitter buffer for the audio stream - see jbuf.h */
#include "config.h"
#include "jbuf.h"

#define PERIOD_US   ((int32_t)((int64_t)HOP_SIZE * 1000000 / SAMPLE_RATE))   /* 16000 */
#define REST        (JB_TARGET - HOP_SIZE)                                      /* reserve left after a hop */
#define PERIOD_MIN  ((float)(PERIOD_US - PERIOD_US / 40))                       /* +-2.5 % */
#define PERIOD_MAX  ((float)(PERIOD_US + PERIOD_US / 40))
#define KP          0.5f         /* us of frame period per sample of reserve error: corrects in ~2 s */
#define KI          0.0004f      /* slow part: takes over the clock difference within ~20 s */

void jbuf_reset(jbuf_t *j) {
    j->prefill = true; j->fill_avg = (float)REST; j->base_us = (float)PERIOD_US; j->period_us = PERIOD_US;
    j->waits = 0; j->skipped = 0;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

int jbuf_take(jbuf_t *j, int avail, int *skip) {
    *skip = 0;
    if (j->prefill) {                                       /* stream (re)started: collect the reserve first */
        if (avail < JB_TARGET) return 0;
        j->prefill = false; j->fill_avg = (float)(avail - HOP_SIZE);
    }
    if (avail > JB_SKIP_AT) { *skip = avail - JB_TARGET; avail = JB_TARGET; j->skipped++; j->fill_avg = (float)REST; }
    if (avail < HOP_SIZE) { j->waits++; return JB_WAIT; }   /* reserve used up by a long burst: wait as before */
    /* steer the frame period: more waiting than the reserve -> shorter frames, less -> longer frames */
    float err = (j->fill_avg += ((float)(avail - HOP_SIZE) - j->fill_avg) / 16.0f) - (float)REST;
    j->base_us = clampf(j->base_us - err * KI, PERIOD_MIN, PERIOD_MAX);
    j->period_us = (int32_t)clampf(j->base_us - err * KP, PERIOD_MIN, PERIOD_MAX);
    return HOP_SIZE;
}
