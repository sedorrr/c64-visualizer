/*
 * C64 Sound Visualizer - DEMO firmware
 * Needs no audio input: a small SID-style chiptune is synthesised on the Pico (demo_audio.c).
 *
 * Sequence, repeated forever:
 *   1. all texts from texts.uf2 (or the built-in demo texts), one after another
 *   2. every visualizer mode for DEMO_MODE_S seconds (order from @modes), each announced
 *      by its name when DEMO_SHOW_NAMES is 1
 *
 * Button (GP15 to GND, optional): short press = skip to the next item,
 * hold 0.7 s and release = next brightness step.
 */
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/regs/addressmap.h"
#include "config.h"
#include "display.h"
#include "bootsel.h"
#include "analysis.h"
#include "visuals.h"
#include "textfx.h"
#include "settings.h"
#include "hardware/watchdog.h"

#ifndef DEMO_MODE_S
#define DEMO_MODE_S     5      /* seconds per visualizer mode */
#endif
#ifndef DEMO_SHOW_NAMES
#define DEMO_SHOW_NAMES 1      /* scroll the mode name before each mode */
#endif

#define TEXTS_MAGIC      "C64VIZTXT1\n"
#define TEXTS_FLASH_ADDR (XIP_BASE + TEXTS_FLASH_OFFSET)
#define BLOCK_MS ((HOP_SIZE * 1000 + SAMPLE_RATE / 2) / SAMPLE_RATE)

static const char DEFAULT_TEXTS[] =
    "{color:8080FF}{typewriter}READY{cursor}{hold:3000}\n"
    "{color:cyan}C64 {color:white}Sound {color:magenta}Visualizer\n";

static uint32_t g_fb[PANEL_W * PANEL_H];
static textfx_settings_t g_txt;
static vis_t g_modes[VIS_COUNT];
static int   g_nmodes = 0;
static int   g_brightness = BRIGHTNESS_DEFAULT;

static inline void put_pixel(int x, int y, uint32_t c) {
    if ((unsigned)x < PANEL_W && (unsigned)y < PANEL_H) g_fb[y * PANEL_W + x] = c;
}
static inline void clear_fb(void) { memset(g_fb, 0, sizeof g_fb); }

static void parse_modes(const char *s, int len) {
    g_nmodes = 0;
    int i = 0;
    while (s && i < len && g_nmodes < VIS_COUNT) {
        while (i < len && (s[i] == ' ' || s[i] == ',' || s[i] == '\t')) i++;
        int st = i;
        while (i < len && s[i] != ' ' && s[i] != ',' && s[i] != '\t') i++;
        int n = i - st;
        for (int m = 0; m < VIS_COUNT && n > 0; m++)
            if ((int)strlen(VIS_NAMES[m]) == n && !strncmp(VIS_NAMES[m], s + st, n)) { g_modes[g_nmodes++] = (vis_t)m; break; }
    }
    if (g_nmodes == 0) for (int m = 0; m < VIS_COUNT; m++) g_modes[g_nmodes++] = (vis_t)m;
}

/* button: true = skip requested (short press); a long press changes the brightness */
static bool button_skip(void) {
    static bool down = false;
    static absolute_time_t t_down;
    bool pressed = !gpio_get(PIN_BUTTON);
    if (pressed && !down) { down = true; t_down = get_absolute_time(); return false; }
    if (!pressed && down) {
        down = false;
        int64_t held = absolute_time_diff_us(t_down, get_absolute_time());
        if (held >= (int64_t)LONG_PRESS_MS * 1000) {
            g_brightness = bright_next_preset(g_brightness);
            display_set_brightness(g_brightness);
        } else if (held > 20000) return true;
    }
    return false;
}

/* frame callback for the text engine: one 16 ms tick of the demo tune, abort on button */
static bool demo_wait(void) {
    capture_block();
    watchdog_update();
    return button_skip();
}

static void show_name(const char *name) {
    char line[48];
    strcpy(line, "{color:00C0FF}{speed:22}");
    strncat(line, name, sizeof line - strlen(line) - 1);
    textfx_play_str(line, demo_wait, BLOCK_MS);
}

int main(void) {
    gpio_init(PIN_BUTTON);
    gpio_set_dir(PIN_BUTTON, GPIO_IN);
    gpio_pull_up(PIN_BUTTON);

    display_init();
    bootsel_check();                                   /* button held at power-up -> USB drive mode */
    audio_init();
    analysis_init();
    visuals_init();

    const char *texts = (const char *)TEXTS_FLASH_ADDR;
    if (strncmp(texts, TEXTS_MAGIC, strlen(TEXTS_MAGIC)) == 0) texts += strlen(TEXTS_MAGIC);
    else texts = DEFAULT_TEXTS;
    int ntexts = textfx_load(texts, &g_txt);
    if (g_txt.brightness >= 0) g_brightness = bright_from_saved(g_txt.brightness);
    display_set_brightness(g_brightness);

    /* camera mode: as saved by the visualizer firmware (double press there), else @camera */
    saved_settings_t sv;
    settings_load(settings_hash(texts), &sv);
    bool camera = sv.valid && sv.same_text ? sv.camera : g_txt.camera;
    display_set_dither(!camera);
    watchdog_enable(2000, true);
    textfx_play_str(camera ? "{color:green}{speed:35}Cam an{hold:1400}" : "{color:red}{speed:35}Cam aus{hold:1400}", demo_wait, BLOCK_MS);
    parse_modes(g_txt.modes, g_txt.modes_len);
    visuals_set_texts(g_txt.scroller, g_txt.scroller_len, g_txt.logo, g_txt.logo_len);

    while (true) {
        for (int i = 0; i < ntexts; i++) textfx_play(i, demo_wait, BLOCK_MS);

        for (int m = 0; m < g_nmodes; m++) {
            if (DEMO_SHOW_NAMES) show_name(VIS_NAMES[g_modes[m]]);
            for (uint32_t ms = 0; ms < DEMO_MODE_S * 1000u; ms += BLOCK_MS) {
                capture_block();
    watchdog_update();
                visuals_frame(g_modes[m], g_fb);
                display_show(g_fb);
                if (button_skip()) break;
            }
        }
    }
}
