/* Jitter buffer for the Ultimate's audio stream. Portable C (host-tested).
   WLAN delivers the packets in bursts; without a reserve every gap made a frame wait (up to 41 ms
   measured). Now the frames run on the Pico's own clock and take each 16 ms hop from a reserve of
   ~40 ms. The two clocks differ slightly (Ultimate 47983/3 = 15994 Hz, Pico 16000 Hz): the frame
   period is steered within +-2.5 % so the reserve stays where it should be. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define JB_TARGET   (HOP_SIZE * 5 / 2)     /* samples waiting before a hop is taken: 2.5 hops = 40 ms */
#define JB_SKIP_AT  (JB_TARGET + 1024)     /* far too much waiting (e.g. after a pause): drop the excess */

#define JB_WAIT     (-1)                   /* jbuf_take: fewer than a hop waiting - wait for the packets */

typedef struct {
    bool     prefill;                      /* collecting the reserve after a (re)start; frames show silence */
    float    fill_avg;                     /* smoothed reserve after taking a hop */
    float    base_us;                      /* slow part of the steering (the clock difference) */
    int32_t  period_us;                    /* steered frame period */
    uint32_t waits, skipped;               /* for the diagnostics */
} jbuf_t;

void jbuf_reset(jbuf_t *j);
/* once per frame, with the number of samples waiting: HOP_SIZE = take one hop; 0 = still collecting the
   reserve (show silence); JB_WAIT = a long burst used the reserve up: wait for the packets (at most
   ~40 ms) and take what has arrived. *skip = samples to drop first (far too many waiting). */
int  jbuf_take(jbuf_t *j, int avail, int *skip);
