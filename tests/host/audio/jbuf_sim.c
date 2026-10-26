/* jitter buffer: 10 minutes of simulated stream with WLAN bursts, clock drift and a long gap */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "config.h"
#include "jbuf.h"
static int fails = 0;
static void check(int ok, const char *what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }

/* packets of 64 samples (192 stereo frames / 3) sent at the Ultimate's rate, delivered with WLAN jitter:
   usually 1-6 ms late, every ~3 s a burst gap of up to gap_ms where packets queue and then arrive together */
typedef struct { double rate, gap_ms, long_gap_at; } scen_t;
typedef struct { uint32_t waits, skipped, frames, held; double max_wait_ms, avg_wait_ms, max_late_ms; int32_t pmin, pmax; } res_t;

static res_t run(scen_t sc, double seconds, unsigned seed) {
    srand(seed);
    jbuf_t j; jbuf_reset(&j);
    double pkt_dt = 64.0 / sc.rate, next_send = 0, burst_until = -1, next_burst = 3.0;
    double delivered = 0, consumed = 0, sum_wait = 0;
    res_t r = {0}; r.pmin = 1 << 30; r.pmax = 0;
    /* pending packets: delivery times in a small queue */
    static double q[4096]; int qh = 0, qt = 0;
    for (double frame_t = 0; frame_t < seconds; ) {
        /* send everything up to frame_t, compute delivery time */
        while (next_send <= frame_t) {
            double d = next_send + (1 + rand() % 6) / 1000.0;
            if (next_send >= next_burst) { burst_until = next_send + (sc.gap_ms * (0.5 + 0.5 * (rand() % 100) / 100.0)) / 1000.0; next_burst += 2.0 + (rand() % 200) / 100.0; }
            if (next_send < burst_until) d = burst_until + 0.001;                 /* held back by the access point */
            if (sc.long_gap_at > 0 && next_send >= sc.long_gap_at && next_send < sc.long_gap_at + 0.2) d = sc.long_gap_at + 0.2;
            q[qt++ % 4096] = d; next_send += pkt_dt;
        }
        while (qh != qt && q[qh % 4096] <= frame_t) { delivered += 64; qh++; }
        int avail = (int)(delivered - consumed), skip;
        int take = jbuf_take(&j, avail, &skip);
        consumed += skip;
        if (take == JB_WAIT) {                       /* like capture_block: wait for packets, at most 40 ms */
            double until = frame_t + 0.040, t0 = frame_t;
            while ((int)(delivered - consumed) < HOP_SIZE && frame_t < until) {
                frame_t += 0.00025;
                while (qh != qt && q[qh % 4096] <= frame_t) { delivered += 64; qh++; }
            }
            int got = (int)(delivered - consumed); if (got > HOP_SIZE) got = HOP_SIZE;
            r.held += HOP_SIZE - got; consumed += got;
            if (frame_t > 1.0 && (frame_t - t0) * 1000 > r.max_late_ms) r.max_late_ms = (frame_t - t0) * 1000;
            continue;                                /* the next frame follows right away, like on the Pico */
        }
        consumed += take;
        if (!j.prefill && take && frame_t > 1.0) { double w = (avail - skip - take) / 16.0; sum_wait += w; if (w > r.max_wait_ms) r.max_wait_ms = w; r.frames++; }
        if (frame_t > 1.0) { if (j.period_us < r.pmin) r.pmin = j.period_us; if (j.period_us > r.pmax) r.pmax = j.period_us; }
        frame_t += j.period_us / 1e6;
    }
    r.waits = j.waits; r.skipped = j.skipped; r.avg_wait_ms = r.frames ? sum_wait / r.frames : 0;
    return r;
}
static void show(const char *name, res_t r) {
    printf("%-42s waits %3u (longest %4.1f ms), held %u samples, skipped %u, reserve avg %.1f ms, period %d..%d us\n",
           name, r.waits, r.max_late_ms, r.held, r.skipped, r.avg_wait_ms, r.pmin, r.pmax);
}
/* the old way for comparison: every frame waits until a whole hop has arrived (at most 40 ms) */
static void run_old(double seconds, unsigned seed, int *late, double *worst) {
    srand(seed);
    double pkt_dt = 64.0 / (47983.0 / 3), next_send = 0, burst_until = -1, next_burst = 3.0, delivered = 0, consumed = 0, t = 0, last = 0;
    static double q[4096]; int qh = 0, qt = 0; *late = 0; *worst = 0;
    while (t < seconds) {
        while (next_send <= t + 0.05) {
            double d = next_send + (1 + rand() % 6) / 1000.0;
            if (next_send >= next_burst) { burst_until = next_send + (30 * (0.5 + 0.5 * (rand() % 100) / 100.0)) / 1000.0; next_burst += 2.0 + (rand() % 200) / 100.0; }
            if (next_send < burst_until) d = burst_until + 0.001;
            q[qt++ % 4096] = d; next_send += pkt_dt;
        }
        double until = t + 0.040;
        while (t < until) { while (qh != qt && q[qh % 4096] <= t) { delivered += 64; qh++; } if (delivered - consumed >= HOP_SIZE) break; t += 0.00025; }
        consumed += HOP_SIZE; if (consumed > delivered) consumed = delivered;
        double frame = (t - last) * 1000; last = t;
        if (t > 1.0) { if (frame > 20.0) (*late)++; if (frame > *worst) *worst = frame; }
        t += 0.002;                                   /* the frame's own work */
    }
}
int main(void) {
    res_t a = run((scen_t){ 47983.0 / 3, 30, 0 }, 600, 1);
    show("Ultimate clock, bursts up to 30 ms, 10 min", a);
    check(a.held == 0, "no gaps in the sound data");
    check(a.skipped == 0, "nothing dropped");
    check(a.avg_wait_ms > 18 && a.avg_wait_ms < 30, "reserve stays at the target (24 ms)");
    check(a.pmin >= 15600 && a.pmax <= 16400, "frame period within +-2.5 %");
    int old_late; double old_worst; run_old(600, 1, &old_late, &old_worst);
    printf("   compare: frames later than 20 ms - before %d (longest %.0f ms), now %u (longest wait %.1f ms)\n", old_late, old_worst, a.waits, a.max_late_ms);
    check(a.waits * 10 < (unsigned)old_late, "at least 10x fewer late frames than before");

    res_t b = run((scen_t){ 16040, 30, 0 }, 600, 2);
    show("sender 0.25 % fast, 10 min", b);
    check(b.skipped == 0 && b.avg_wait_ms < 30, "faster sender: the reserve does not grow");
    res_t c = run((scen_t){ 15960, 30, 0 }, 600, 3);
    show("sender 0.25 % slow, 10 min", c);
    check(c.avg_wait_ms > 18 && c.waits * 10 < (unsigned)old_late, "slower sender: the reserve stays at the target");

    res_t d = run((scen_t){ 47983.0 / 3, 30, 120 }, 300, 4);
    show("200 ms stream pause after 2 min", d);
    check(d.held > 0 && d.held < 4000, "the pause shows as a short gap ...");
    check(d.avg_wait_ms > 18 && d.avg_wait_ms < 30 && d.skipped <= 1, "... then back at the target");

    res_t e = run((scen_t){ 47983.0 / 3, 45, 0 }, 600, 5);
    show("bursts up to 45 ms (worse than measured)", e);
    printf("%s\n", fails ? "SOME JITTER BUFFER CHECKS FAILED" : "all jitter buffer checks passed");
    return fails ? 1 : 0;
}
