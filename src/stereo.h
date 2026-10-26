/* Stereo check: does the second channel really carry its own signal? Portable C (host-tested).
   Fed with every new hop of both channels; decides in windows of ~2 s of music:
     empty - one channel has no signal (nothing connected, or only one side wired)
     copy  - both carry the same signal (mono source, or crosstalk of an open ADC input)
     stereo - two different signals
   Only "stereo" switches the display to stereo; a change back needs several windows in a row,
   so a short mono passage in a stereo tune does not make the display flicker. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum { ST_CHECK, ST_STEREO, ST_EMPTY_R, ST_EMPTY_L, ST_COPY,
               ST_FILE_MONO, ST_FILE_STEREO } stereo_state_t;   /* FILE: from the SID header (jukebox) */

typedef struct {
    stereo_state_t state;
    int    hops, audio_hops, run;                  /* window position, hops with music, verdicts in a row */
    stereo_state_t pending;                        /* verdict that is being confirmed */
    double sll, srr, slr;                          /* sums over the window (DC removed per hop) */
    float  corr, ratio;                            /* last window: correlation, rms right / rms left */
    bool   changed;                                /* the state changed with the last hop (for messages) */
    bool   window;                                 /* a window with enough music was judged with the last hop */
    stereo_state_t verdict;                        /* ... and this was its verdict (run = how many in a row) */
    bool   decide;                                 /* a new tune started: the next window decides at once */
    int    hint;                                   /* SIDs of the tune from its header (jukebox), 0 = unknown */
} stereo_t;

void stereo_reset(stereo_t *s);
/* one hop of n samples per channel (12-bit ADC scale); returns the current state */
stereo_state_t stereo_update(stereo_t *s, const uint16_t *l, const uint16_t *r, int n);
const char *stereo_name(stereo_state_t st);        /* German text for status page and matrix */
/* the jukebox started a tune with this many SIDs (header): 1 = mono for sure, 2-3 = stereo unless one side
   carries nothing at all; 0 = no header any more (stop, silence): measure again, the next window decides */
void stereo_hint(stereo_t *s, int chips);
