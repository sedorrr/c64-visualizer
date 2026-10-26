/* Two matrices as one picture: every mode drawn 64 wide (and back to 32) with music-like test audio.
   Built with -fsanitize=address: no mode may write outside its buffers. Writes wide_modes.ppm (all modes, one strip
   per mode, frames 150 and 300) for a look. Checks that every mode uses both halves of the wide picture. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "analysis.h"
#include "visuals.h"
int g_panel_w = PANEL_PHYS_W;
static uint32_t fb[PANEL_W_MAX * PANEL_H];
static void audio(int f) {                      /* bass beat + a melody + chords: something for every mode */
    memmove(g_samples, g_samples + HOP_SIZE, (BLOCK_SIZE - HOP_SIZE) * 2);
    for (int i = 0; i < HOP_SIZE; i++) {
        double t = (f * HOP_SIZE + i) / 16000.0, beat = fmod(t, 0.5) < 0.08 ? 1.0 : 0.25;
        double mel = 440.0 * pow(2.0, ((int)(t * 4) % 8) / 12.0);
        double v = 700 * beat * sin(2 * M_PI * 70 * t) + 350 * sin(2 * M_PI * mel * t) + 200 * sin(2 * M_PI * 1300 * t) * (0.5 + 0.5 * sin(t * 5))
                 + 120 * sin(2 * M_PI * 3100 * t) + ((rand() % 21) - 10);
        g_samples[BLOCK_SIZE - HOP_SIZE + i] = (uint16_t)(2048 + v);
    }
}
int main(void) {
    const int W = 2 * PANEL_PHYS_W, S = 4;                 /* 4x4 pixels per LED in the picture */
    FILE *o = fopen("wide_modes.ppm", "wb");
    int rows = VIS_COUNT * 2;
    fprintf(o, "P6 %d %d 255\n", W * S, rows * (PANEL_H + 1) * S);
    static uint8_t img[64 * 4 * 3 * 1000];
    int fails = 0;
    for (int m = 0; m < VIS_COUNT; m++) {
        analysis_init(); visuals_init(); visuals_set_gate(true); srand(1);
        g_panel_w = PANEL_PHYS_W;
        for (int f = 0; f < 60; f++) { audio(f); visuals_frame((vis_t)m, fb); }   /* narrow first, then wider */
        g_panel_w = W;
        int left = 0, right = 0;
        for (int f = 60; f <= 300; f++) {
            audio(f); visuals_frame((vis_t)m, fb);
            for (int y = 0; y < PANEL_H; y++) for (int x = 0; x < W; x++) if (fb[y * W + x]) { if (x < PANEL_PHYS_W) left++; else right++; }
            if (f == 150 || f == 300) {
                for (int y = 0; y < (PANEL_H + 1) * S; y++) {
                    memset(img, 0x20, (size_t)W * S * 3);
                    for (int x = 0; x < W * S; x++) {
                        int lx = x / S, ly = y / S; if (ly >= PANEL_H || x % S == S - 1 || y % S == S - 1) continue;
                        uint32_t c = fb[ly * W + lx]; img[3 * x] = c >> 16; img[3 * x + 1] = c >> 8; img[3 * x + 2] = c;
                        if (lx == PANEL_PHYS_W && !c) { img[3 * x] = 0x50; }        /* the seam between the matrices */
                    }
                    fwrite(img, 1, (size_t)W * S * 3, o);
                }
            }
        }
        g_panel_w = PANEL_PHYS_W;                            /* partner gone: narrow again */
        for (int f = 0; f < 30; f++) { audio(f); visuals_frame((vis_t)m, fb); }
        int ok = left > 0 && right > 0 && right > left / 6;
        printf("%-10s lit pixels left %6d  right %6d %s\n", VIS_NAMES[m], left, right, ok ? "" : "  <- one half stays dark");
        if (!ok) fails++;
    }
    fclose(o);
    printf(fails ? "FAILED: %d modes\n" : "all modes use both matrices\n", fails);
    return fails ? 1 : 0;
}
