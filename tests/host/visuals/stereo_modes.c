/* stereo modes: a tone only on one side must light only that side (spectrum, VU, mirror, scope) */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "analysis.h"
#include "visuals.h"
static int fails = 0;
static void check(int ok, const char *what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }
static uint32_t fb[PANEL_W * PANEL_H];

static void frames(vis_t v, int left_on, int right_on, int n) {
    static long t = 0;
    for (int f = 0; f < n; f++) {
        memmove(g_samples, g_samples + HOP_SIZE, (BLOCK_SIZE - HOP_SIZE) * 2);
        memmove(g_samples_r, g_samples_r + HOP_SIZE, (BLOCK_SIZE - HOP_SIZE) * 2);
        for (int i = 0; i < HOP_SIZE; i++, t++) {
            float a = 600.0f * sinf(2 * (float)M_PI * 300.0f * t / SAMPLE_RATE);
            g_samples[BLOCK_SIZE - HOP_SIZE + i]   = (uint16_t)(2048 + (left_on ? a : 0));
            g_samples_r[BLOCK_SIZE - HOP_SIZE + i] = (uint16_t)(2048 + (right_on ? a : 0));
        }
        visuals_frame(v, fb);
    }
}
static int lit(int x0, int x1, int y0, int y1) {
    int n = 0;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) if (fb[y * PANEL_W + x]) n++;
    return n;
}
int main(void) {
    analysis_init(); visuals_init(); visuals_set_gate(true);
    for (int i = 0; i < BLOCK_SIZE; i++) g_samples[i] = g_samples_r[i] = 2048;   /* rest position, like audio_init() */
    const int H = PANEL_W / 2, V = PANEL_H / 2;
    g_stereo = true;
    frames(VIS_SPECTRUM, 1, 0, 60);
    printf("spectrum, left only: left half %d px, right half %d px\n", lit(0, H, 0, PANEL_H), lit(H, PANEL_W, 0, PANEL_H));
    check(lit(0, H, 0, PANEL_H) > 0 && lit(H, PANEL_W, 0, PANEL_H) == 0, "spectrum: only the left half");
    frames(VIS_SPECTRUM, 0, 1, 200);                       /* 3 s: the peak marks of the left side fall */
    check(lit(H, PANEL_W, 0, PANEL_H) > 0 && lit(0, H, 0, PANEL_H) == 0, "spectrum: right only -> only the right half");
    frames(VIS_VU, 1, 0, 200);
    printf("VU, left only: top %d px, bottom %d px\n", lit(0, PANEL_W, 0, V), lit(0, PANEL_W, V, PANEL_H));
    check(lit(0, PANEL_W, 0, V) > 0 && lit(0, PANEL_W, V, PANEL_H) == 0, "VU: left meter on top");
    frames(VIS_VU, 1, 1, 60);
    check(lit(0, PANEL_W, 0, V) == lit(0, PANEL_W, V, PANEL_H), "VU: equal signals -> equal meters");
    frames(VIS_MIRROR, 0, 1, 200);
    check(lit(H, PANEL_W, 0, PANEL_H) > 0 && lit(0, H, 0, PANEL_H) == 0, "mirror: right only -> only the right half");
    frames(VIS_SCOPE, 1, 0, 30);
    int green = 0, blue = 0;
    for (int i = 0; i < PANEL_W * PANEL_H; i++) { if (fb[i] == COL_SCOPE) green++; if (fb[i] == COL_SCOPE_R) blue++; }
    printf("scope, left only: %d green (left) px, %d blue (right) px\n", green, blue);
    check(green >= PANEL_W - 2 && blue > 0, "scope: both traces drawn (right is a flat blue line)");

    g_stereo = false;
    frames(VIS_SPECTRUM, 1, 0, 60);
    int a = lit(0, PANEL_W, 0, PANEL_H);
    frames(VIS_VU, 1, 0, 60);
    check(a > 0 && lit(0, PANEL_W, 0, V) == lit(0, PANEL_W, V, PANEL_H), "mono: spectrum as before, one meter over the full height");
    printf("%s\n", fails ? "SOME STEREO MODE CHECKS FAILED" : "all stereo mode checks passed");
    return fails ? 1 : 0;
}
