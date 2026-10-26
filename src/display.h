#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "config.h"

/* Framebuffer: PANEL_W * PANEL_H pixels, row-major, 0x00RRGGBB. Monochrome displays: non-zero = on.
   With two matrices as one picture (C64VIZ_WIDE, src/net/wide.c) display_show takes the whole picture and
   display_show_phys gets this matrix's PANEL_PHYS_W columns. */
void display_init(void);
void display_set_brightness(int pct);           /* BRIGHT_MIN_PCT .. 100 */
/* stored or written values: 0..3 are the steps of older firmware / @brightness, the rest is percent */
static inline int bright_from_saved(int v) {
    static const uint8_t presets[BRIGHT_PRESET_COUNT] = BRIGHT_PRESETS;
    if (v >= 0 && v < BRIGHT_PRESET_COUNT) return presets[v];
    return v < BRIGHT_MIN_PCT ? BRIGHT_MIN_PCT : v > 100 ? 100 : v;
}
static inline int bright_next_preset(int pct) {   /* button: the next preset above, then back to the first */
    static const uint8_t presets[BRIGHT_PRESET_COUNT] = BRIGHT_PRESETS;
    for (int i = 0; i < BRIGHT_PRESET_COUNT; i++) if (presets[i] > pct) return presets[i];
    return presets[0];
}
void display_set_light(float factor);           /* light sensor dimming 0..1 on top of the step (1 = none) */
void display_show(const uint32_t *fb);
#ifdef C64VIZ_WIDE
void display_show_phys(const uint32_t *fb);     /* PANEL_PHYS_W x PANEL_H, no overlay */
/* the other half of a picture: shown with the brightness of the Pico that drew it (on = false: own values again) */
void display_set_override(bool on, int pct, float light, bool dither);
int   display_get_brightness(void);
float display_get_light(void);
bool  display_get_dither(void);
#endif
/* optional overlay drawn on top of every frame (texts, modes, messages): e.g. a status pixel */
void display_set_overlay(void (*fn)(uint32_t *fb));
uint32_t display_take_wait_us(void);       /* time spent waiting for the previous frame since the last call */
/* camera mode: false = no temporal dithering (steady LEDs on video); no effect on MAX7219 */
void display_set_dither(bool on);
