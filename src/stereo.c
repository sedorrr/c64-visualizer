/* Stereo check - see stereo.h */
#include <math.h>
#include <string.h>
#include "stereo.h"
#include "config.h"

#define WIN_HOPS     125         /* ~2 s at 16 ms per hop */
#define MIN_AUDIO    60          /* a window counts only with at least ~1 s of music in it */
#define LIVE_RMS     (NOISE_GATE_LSB * 2.0f)   /* a channel below this carries no signal */
#define EMPTY_RATIO  0.06f       /* a channel below 6 % (-24 dB) of the other one counts as empty */
#define COPY_CORR    0.97f       /* correlation above this = the same signal on both sides */
#define TO_STEREO    2           /* windows in a row needed to switch to stereo ... */
#define TO_MONO      30          /* ... and back to mono (~60 s of music): a stereo tune has long passages with
                                    only one SID, which look like mono (the Ultimate pans the SIDs only partly) */
#define TO_EMPTY     5           /* tune with 2+ SIDs: one side must be silent ~10 s before it counts as mono */

void stereo_reset(stereo_t *s) { memset(s, 0, sizeof *s); s->state = ST_CHECK; s->pending = ST_CHECK; }

void stereo_hint(stereo_t *s, int chips) {
    s->hint = chips;
    s->run = 0;
    if (chips == 1) s->state = ST_FILE_MONO;
    else if (chips >= 2) s->state = ST_FILE_STEREO;
    else {
        if (s->state == ST_FILE_MONO || s->state == ST_FILE_STEREO) s->state = ST_CHECK;
        s->decide = true;
    }
    s->pending = s->state;
}

const char *stereo_name(stereo_state_t st) {
    switch (st) {
        case ST_STEREO:  return "Stereo";
        case ST_EMPTY_R: return "rechts kein Signal - Mono";
        case ST_EMPTY_L: return "links kein Signal - Mono";
        case ST_COPY:    return "rechts = links - Mono";
        case ST_FILE_MONO:   return "Mono (1 SID laut Datei)";
        case ST_FILE_STEREO: return "Stereo (mehrere SIDs laut Datei)";
        default:         return "prüfe ...";
    }
}

static stereo_state_t verdict(const stereo_t *s, float *corr, float *ratio) {
    float n = (float)s->audio_hops;
    float rl = sqrtf((float)s->sll / n), rr = sqrtf((float)s->srr / n);
    *corr = (s->sll > 0 && s->srr > 0) ? (float)(s->slr / sqrt(s->sll * s->srr)) : 0.0f;
    *ratio = rl > 0.0f ? rr / rl : 0.0f;
    if (rr < LIVE_RMS || rr < rl * EMPTY_RATIO) return ST_EMPTY_R;
    if (rl < LIVE_RMS || rl < rr * EMPTY_RATIO) return ST_EMPTY_L;
    if (*corr > COPY_CORR) return ST_COPY;
    return ST_STEREO;
}

stereo_state_t stereo_update(stereo_t *s, const uint16_t *l, const uint16_t *r, int n) {
    s->changed = false; s->window = false;
    float ml = 0, mr = 0;
    for (int i = 0; i < n; i++) { ml += l[i]; mr += r[i]; }
    ml /= n; mr /= n;
    double ll = 0, rr = 0, lr = 0;
    for (int i = 0; i < n; i++) { double a = l[i] - ml, b = r[i] - mr; ll += a * a; rr += b * b; lr += a * b; }
    /* only hops with music count: silence says nothing about the second channel */
    float hop_l = sqrtf((float)(ll / n)), hop_r = sqrtf((float)(rr / n));
    if (hop_l > LIVE_RMS || hop_r > LIVE_RMS) { s->sll += ll / n; s->srr += rr / n; s->slr += lr / n; s->audio_hops++; }
    if (++s->hops < WIN_HOPS) return s->state;

    if (s->audio_hops >= MIN_AUDIO) {
        stereo_state_t v = verdict(s, &s->corr, &s->ratio);
        int need;
        if (s->hint == 1) v = ST_FILE_MONO;                /* one SID: mono, whatever the panning */
        else if (s->hint >= 2 && v != ST_EMPTY_R && v != ST_EMPTY_L) v = ST_FILE_STEREO;   /* "same" = a one-SID passage */
        s->window = true; s->verdict = v;                  /* after the header is taken into account */
        if (v == s->pending) s->run++; else { s->pending = v; s->run = 1; }
        if (s->hint >= 2) need = v == ST_FILE_STEREO ? 1 : TO_EMPTY;
        else need = (s->state == ST_CHECK || s->decide) ? 1 : (v == ST_STEREO ? TO_STEREO : TO_MONO);
        s->decide = false;
        if (v != s->state && s->run >= need) { s->state = v; s->changed = true; }
    }
    s->hops = 0; s->audio_hops = 0; s->sll = s->srr = s->slr = 0;
    return s->state;
}
