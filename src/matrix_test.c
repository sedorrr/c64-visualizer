/*
 * Matrix test / demo:
 *   1. "READY" scrolls in from the right with a moving rainbow and stops centred.
 *   2. It holds (rainbow keeps moving), then blinks BLINK_COUNT times.
 *   3. It explodes: every lit pixel flies outwards from the centre and fades out.
 *   4. "C64 Sound Visualizer" scrolls through, then "SID" scrolls in, stops centred and
 *      GLOWS like hot metal (pulsing white/yellow core, orange/red halo, flicker),
 *      then fades out. Everything repeats.
 *
 * Matrix-Test / Demo: "READY" scrollt herein, bleibt stehen, blinkt, zerplatzt wie eine
 * Explosion (mit Funken), danach laeuft "C64 Sound Visualizer" durch und "SID" bleibt
 * gluehend stehen. Dann von vorn.
 *
 * Only the panel, its power and GP16 (data) are needed. No audio, no button.
 * A word that stops centred must fit the 32 columns ("READY" 29, "SID" 17; a/n/t/u are narrow).
 */
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "config.h"
#include "display.h"
#include "bootsel.h"
#include "font5x7.h"

#define WORD1         "READY"
#define TEXT2         "C64 Sound Visualizer"
#define WORD3         "SID"
#define SCROLL_MS     60      /* ms per pixel step                          */
#define HOLD_MS       1200    /* pause with running rainbow before blinking */
#define BLINK_COUNT   3
#define BLINK_MS      220     /* off / on time per blink phase              */
#define EXPLODE_MS    40      /* ms per explosion frame                     */
#define EXPLODE_FRAMES 60
#define SPARKS        70      /* extra fast sparks added to the explosion   */
#define GLOW_MS       3500    /* how long "SID" glows                       */
#define GLOW_FRAME_MS 35
#define PAUSE_MS      600     /* dark pause between the two parts           */
#define CHAR_GAP      1
#define HUE_PER_COL   6       /* colour change across the text              */
#define HUE_PER_FRAME 3       /* colour change over time                    */

static uint32_t g_fb[PANEL_W * PANEL_H];

/* ------------------------------------------------------------ colour */
static uint32_t hsv(uint16_t h, uint8_t s, uint8_t v) {  /* h 0..359 */
    uint8_t region = (uint8_t)(h / 60), rem = (uint8_t)((h % 60) * 255 / 60);
    uint8_t p = (uint8_t)(v * (255 - s) / 255);
    uint8_t q = (uint8_t)(v * (255 - (s * rem) / 255) / 255);
    uint8_t t = (uint8_t)(v * (255 - (s * (255 - rem)) / 255) / 255);
    uint8_t r, g, b;
    switch (region) {
        case 0:  r = v; g = t; b = p; break;
        case 1:  r = q; g = v; b = p; break;
        case 2:  r = p; g = v; b = t; break;
        case 3:  r = p; g = q; b = v; break;
        case 4:  r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static uint32_t scale_colour(uint32_t c, float f) {
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    uint32_t r = (uint32_t)(((c >> 16) & 0xFF) * f), g = (uint32_t)(((c >> 8) & 0xFF) * f), b = (uint32_t)((c & 0xFF) * f);
    return (r << 16) | (g << 8) | b;
}

/* tiny deterministic PRNG (no libc rand needed) */
static uint32_t g_rng = 0x1234567u;
static float frand(void) { g_rng = g_rng * 1664525u + 1013904223u; return (g_rng >> 8) / 16777216.0f; }

/* ------------------------------------------------------------ text */
static inline void put(int x, int y, uint32_t c) {
    if (x >= 0 && x < PANEL_W && y >= 0 && y < PANEL_H) g_fb[y * PANEL_W + x] = c;
}

static void draw_column(int x, uint8_t bits, uint32_t colour) {
    int y0 = (PANEL_H - FONT_H) / 2;
    for (int r = 0; r < FONT_H; r++)
        if (bits & (1u << r)) put(x, y0 + r, colour);
}

static int text_width(const char *text) {
    int w = 0;
    for (int i = 0; text[i]; i++) { int gw; font_glyph_w(text[i], &gw); w += gw + (i ? CHAR_GAP : 0); }
    return w;
}

static void render_text(const char *text, int offset, uint16_t hue0, bool visible) {
    memset(g_fb, 0, sizeof g_fb);
    if (visible) {
        int x = offset;
        for (int i = 0; text[i]; i++) {
            int gw;
            const uint8_t *g = font_glyph_w(text[i], &gw);
            for (int c = 0; c < gw; c++, x++)
                draw_column(x, g[c], hsv((uint16_t)((hue0 + x * HUE_PER_COL) % 360), 255, 255));
            x += CHAR_GAP;
        }
    }
    display_show(g_fb);
}

/* ------------------------------------------------------------ explosion */
typedef struct { float x, y, vx, vy, life, fade; uint32_t colour; } particle_t;
static particle_t g_p[PANEL_W * PANEL_H + SPARKS];

static void explode(void) {
    /* every lit pixel of the current framebuffer becomes a particle */
    int n = 0;
    float cx = (PANEL_W - 1) / 2.0f, cy = (PANEL_H - 1) / 2.0f;
    for (int y = 0; y < PANEL_H; y++) for (int x = 0; x < PANEL_W; x++) {
        uint32_t c = g_fb[y * PANEL_W + x];
        if (!c) continue;
        float dx = x - cx, dy = y - cy, d = sqrtf(dx * dx + dy * dy);
        if (d < 0.5f) { dx = frand() - 0.5f; dy = frand() - 0.5f; d = 0.7f; }
        float speed = 0.55f + frand() * 0.85f;                    /* px per frame, outward */
        g_p[n].x = (float)x; g_p[n].y = (float)y;
        g_p[n].vx = dx / d * speed * 1.7f + (frand() - 0.5f) * 0.9f;  /* wide panel: push harder sideways */
        g_p[n].vy = dy / d * speed * 0.55f + (frand() - 0.5f) * 0.5f;
        g_p[n].life = 1.0f;
        g_p[n].fade = 0.025f + frand() * 0.03f;
        g_p[n].colour = c;
        n++;
    }
    /* extra sparks from the centre: fast, short-lived, white/yellow */
    for (int i = 0; i < SPARKS; i++) {
        float a = frand() * 6.2832f, speed = 0.9f + frand() * 1.6f;
        g_p[n].x = cx + (frand() - 0.5f) * 6.0f; g_p[n].y = cy + (frand() - 0.5f) * 2.0f;
        g_p[n].vx = cosf(a) * speed * 1.6f; g_p[n].vy = sinf(a) * speed * 0.6f;
        g_p[n].life = 0.6f + frand() * 0.4f;
        g_p[n].fade = 0.05f + frand() * 0.06f;
        g_p[n].colour = (i & 1) ? 0xFFFFFF : 0xFFD040;
        n++;
    }
    /* two-frame flash: white, then orange "fireball" */
    for (int y = 0; y < PANEL_H; y++) for (int x = 0; x < PANEL_W; x++) g_fb[y * PANEL_W + x] = 0xFFFFFF;
    display_show(g_fb); sleep_ms(45);
    for (int y = 0; y < PANEL_H; y++) for (int x = 0; x < PANEL_W; x++) {
        float d = fabsf(x - cx) / cx;                            /* bright in the middle, dark at the edges */
        g_fb[y * PANEL_W + x] = scale_colour(0xFF6010, 1.0f - d * 0.8f);
    }
    display_show(g_fb); sleep_ms(45);

    for (int f = 0; f < EXPLODE_FRAMES; f++) {
        memset(g_fb, 0, sizeof g_fb);
        int alive = 0;
        for (int i = 0; i < n; i++) {
            particle_t *p = &g_p[i];
            p->x += p->vx; p->y += p->vy;
            p->vy += 0.03f;                                       /* a little gravity */
            p->vx *= 0.985f; p->vy *= 0.985f;
            p->life -= p->fade;
            if (p->life <= 0.0f) continue;
            int px = (int)lroundf(p->x), py = (int)lroundf(p->y);
            if (px < 0 || px >= PANEL_W || py < 0 || py >= PANEL_H) continue;
            alive++;
            uint32_t c = scale_colour(p->colour, p->life);
            /* additive-ish: keep the brighter of overlapping particles */
            uint32_t old = g_fb[py * PANEL_W + px];
            if ((c & 0xFF) + ((c >> 8) & 0xFF) + ((c >> 16) & 0xFF) > (old & 0xFF) + ((old >> 8) & 0xFF) + ((old >> 16) & 0xFF))
                g_fb[py * PANEL_W + px] = c;
        }
        display_show(g_fb);
        if (!alive) break;
        sleep_ms(EXPLODE_MS);
    }
    memset(g_fb, 0, sizeof g_fb);
    display_show(g_fb);
}

/* ------------------------------------------------------------ glow ("SID") */
/* palette from dark red (0) over orange and yellow to white (1) */
static uint32_t heat(float h) {
    if (h < 0.0f) h = 0.0f;
    if (h > 1.0f) h = 1.0f;
    float r, g, b;
    if (h < 0.4f)      { r = 0.35f + h / 0.4f * 0.65f; g = h / 0.4f * 0.15f;          b = 0.0f; }
    else if (h < 0.8f) { r = 1.0f;                     g = 0.15f + (h - 0.4f) / 0.4f * 0.75f; b = 0.0f; }
    else               { r = 1.0f;                     g = 0.9f + (h - 0.8f) / 0.2f * 0.1f;  b = (h - 0.8f) / 0.2f; }
    return ((uint32_t)(r * 255) << 16) | ((uint32_t)(g * 255) << 8) | (uint32_t)(b * 255);
}

static void glow_frame(const char *text, int offset, float t, float intensity) {
    static uint8_t mask[PANEL_W * PANEL_H];
    memset(mask, 0, sizeof mask);
    /* letter mask */
    int x = offset, y0 = (PANEL_H - FONT_H) / 2;
    for (int i = 0; text[i]; i++) {
        int gw;
        const uint8_t *g = font_glyph_w(text[i], &gw);
        for (int c = 0; c < gw; c++, x++)
            for (int r = 0; r < FONT_H; r++)
                if ((g[c] & (1u << r)) && x >= 0 && x < PANEL_W) mask[(y0 + r) * PANEL_W + x] = 1;
        x += CHAR_GAP;
    }
    memset(g_fb, 0, sizeof g_fb);
    float pulse = 0.65f + 0.35f * sinf(t * 2.6f);                        /* slow breathing */
    for (int py = 0; py < PANEL_H; py++) for (int px = 0; px < PANEL_W; px++) {
        float flicker = 0.85f + frand() * 0.15f;
        if (mask[py * PANEL_W + px]) {
            /* letters: hot, breathing between yellow and white, with a warm wave along x */
            float h = 0.55f + 0.45f * pulse * (0.7f + 0.3f * sinf(t * 4.0f + px * 0.5f));
            g_fb[py * PANEL_W + px] = scale_colour(heat(h), intensity * flicker);
            continue;
        }
        /* halo: nearest lit letter pixel within 2 px */
        int best = 99;
        for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) {
            int nx = px + dx, ny = py + dy;
            if (nx < 0 || nx >= PANEL_W || ny < 0 || ny >= PANEL_H) continue;
            if (mask[ny * PANEL_W + nx]) { int d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy); if (d < best) best = d; }
        }
        if (best == 1)      g_fb[py * PANEL_W + px] = scale_colour(heat(0.25f), (0.22f + 0.18f * pulse) * intensity * flicker);
        else if (best == 2) g_fb[py * PANEL_W + px] = scale_colour(heat(0.10f), (0.06f + 0.06f * pulse) * intensity * flicker);
    }
    display_show(g_fb);
}

/* ------------------------------------------------------------ main */
int main(void) {
    display_init();
    bootsel_check();                                   /* button held at power-up -> USB drive mode */
    display_set_brightness(BRIGHTNESS_DEFAULT);

    int w1 = text_width(WORD1), w2 = text_width(TEXT2), w3 = text_width(WORD3);
    int hold_at = (PANEL_W - w1) / 2;
    int hold3   = (PANEL_W - w3) / 2;
    if (hold_at < 0) hold_at = 0;
    if (hold3 < 0) hold3 = 0;
    uint16_t hue0 = 0;

    while (true) {
        /* 1) "READY" scrolls in and stops centred */
        for (int offset = PANEL_W; offset >= hold_at; offset--) {
            render_text(WORD1, offset, hue0, true);
            hue0 = (uint16_t)((hue0 + HUE_PER_FRAME) % 360);
            sleep_ms(SCROLL_MS);
        }
        /* 2) hold with running rainbow, then blink */
        for (int t = 0; t < HOLD_MS; t += SCROLL_MS) {
            render_text(WORD1, hold_at, hue0, true);
            hue0 = (uint16_t)((hue0 + HUE_PER_FRAME) % 360);
            sleep_ms(SCROLL_MS);
        }
        for (int b = 0; b < BLINK_COUNT; b++) {
            render_text(WORD1, hold_at, hue0, false);
            sleep_ms(BLINK_MS);
            hue0 = (uint16_t)((hue0 + HUE_PER_FRAME * 4) % 360);
            render_text(WORD1, hold_at, hue0, true);
            sleep_ms(BLINK_MS);
        }
        /* 3) explosion (works on whatever is in the framebuffer: the lit "READY") */
        explode();
        sleep_ms(PAUSE_MS);

        /* 4) "C64 Sound Visualizer" scrolls through */
        for (int offset = PANEL_W; offset > -w2; offset--) {
            render_text(TEXT2, offset, hue0, true);
            hue0 = (uint16_t)((hue0 + HUE_PER_FRAME) % 360);
            sleep_ms(SCROLL_MS);
        }
        /* 5) "SID" scrolls in already glowing, stops centred, glows, fades out */
        float t = 0.0f;
        for (int offset = PANEL_W; offset >= hold3; offset--) {
            glow_frame(WORD3, offset, t, 1.0f); t += SCROLL_MS / 1000.0f;
            sleep_ms(SCROLL_MS);
        }
        for (int ms = 0; ms < GLOW_MS; ms += GLOW_FRAME_MS) {
            glow_frame(WORD3, hold3, t, 1.0f); t += GLOW_FRAME_MS / 1000.0f;
            sleep_ms(GLOW_FRAME_MS);
        }
        for (int f = 30; f >= 0; f--) {                       /* fade out ~1 s */
            glow_frame(WORD3, hold3, t, f / 30.0f); t += GLOW_FRAME_MS / 1000.0f;
            sleep_ms(GLOW_FRAME_MS);
        }
        sleep_ms(PAUSE_MS);
    }
}
