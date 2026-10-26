/*
 * WS2812B RGB matrix driver (PIO + DMA).
 *  - gamma correction: colour values are treated like screen colours (sRGB-ish), so fades,
 *    dim pixels and mixed colours (yellow, orange) look as intended instead of washed out
 *  - temporal dithering: brightness levels between two LED steps are produced by alternating
 *    frames, so dim pixels fade smoothly instead of jumping between a few coarse levels
 *  - the frame is sent by DMA in the background, the CPU is free meanwhile
 *  - current limiter: estimates the panel current every frame and dims if needed
 */
#include <math.h>
#include "display.h"
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"

#define NUM_LEDS (PANEL_PHYS_W * PANEL_H)
#define MA_PER_CHANNEL 20.0f     /* one colour channel at full value, per LED */

static PIO  g_pio = pio0;
static uint g_sm;
static int  g_dma;
static float g_level = WS_MAX_LEVEL * BRIGHTNESS_DEFAULT / 100.0f;   /* 0..255 */
static int   g_pct = BRIGHTNESS_DEFAULT;
static float g_light = 1.0f;                    /* light sensor factor */
void display_set_light(float factor) { g_light = factor < 0.0f ? 0.0f : factor > 1.0f ? 1.0f : factor; }
static uint16_t g_lut[256];                     /* value -> linear LED intensity, 0..65535 */
static uint8_t  g_err[NUM_LEDS * 3];            /* dithering remainder per LED and channel */
static uint32_t g_out[NUM_LEDS];                /* GRB words for the DMA */
static absolute_time_t g_ready_at;              /* when the previous frame is out and latched */
static bool g_dither = WS_DITHER;               /* off in camera mode */
void display_set_dither(bool on) { g_dither = on; }
#ifdef C64VIZ_WIDE
static bool  g_ovr = false, g_ovr_dither = true;
static float g_ovr_level, g_ovr_light;
void display_set_override(bool on, int pct, float light, bool dither) {
    g_ovr = on;
    if (pct < BRIGHT_MIN_PCT) pct = BRIGHT_MIN_PCT;
    if (pct > 100) pct = 100;
    g_ovr_level = WS_MAX_LEVEL * pct / 100.0f;
    g_ovr_light = light < 0.0f ? 0.0f : light > 1.0f ? 1.0f : light;
    g_ovr_dither = dither;
}
int   display_get_brightness(void) { return g_pct; }
float display_get_light(void) { return g_light; }
bool  display_get_dither(void) { return g_dither; }
#endif

/* logical (x,y) -> LED index in the data chain */
static inline int led_index(int x, int y) {
#if WS_FLIP_X
    x = PANEL_PHYS_W - 1 - x;
#endif
#if WS_FLIP_Y
    y = PANEL_H - 1 - y;
#endif
#if WS_COLUMN_MAJOR
    int pos = y;
  #if WS_SERPENTINE
    if (x & 1) pos = PANEL_H - 1 - pos;
  #endif
    return x * PANEL_H + pos;
#else
    int pos = x;
  #if WS_SERPENTINE
    if (y & 1) pos = PANEL_PHYS_W - 1 - pos;
  #endif
    return y * PANEL_PHYS_W + pos;
#endif
}

void display_init(void) {
    for (int i = 0; i < 256; i++) g_lut[i] = (uint16_t)(powf(i / 255.0f, WS_GAMMA) * 65535.0f + 0.5f);

    g_sm = pio_claim_unused_sm(g_pio, true);
    uint offset = pio_add_program(g_pio, &ws2812_program);
    pio_gpio_init(g_pio, PIN_WS2812);
    pio_sm_set_consecutive_pindirs(g_pio, g_sm, PIN_WS2812, 1, true);
    pio_sm_config c = ws2812_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, PIN_WS2812);
    sm_config_set_out_shift(&c, false, true, 24);          /* MSB first, autopull at 24 bit */
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    float div = (float)clock_get_hz(clk_sys) / (800000.0f * (ws2812_T1 + ws2812_T2 + ws2812_T3));
    sm_config_set_clkdiv(&c, div);
    pio_sm_init(g_pio, g_sm, offset, &c);
    pio_sm_set_enabled(g_pio, g_sm, true);

    g_dma = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(g_dma);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pio_get_dreq(g_pio, g_sm, true));
    dma_channel_configure(g_dma, &dc, &g_pio->txf[g_sm], g_out, NUM_LEDS, false);
    g_ready_at = get_absolute_time();
}

void display_set_brightness(int pct) {
    if (pct < BRIGHT_MIN_PCT) pct = BRIGHT_MIN_PCT;
    if (pct > 100) pct = 100;
    g_level = WS_MAX_LEVEL * pct / 100.0f;
    g_pct = pct;
}

/* gamma + brightness + dithering for one channel: returns the LED value 0..255 */
static inline uint32_t channel_out(uint8_t v, uint32_t bq, uint8_t *err, bool dither) {
    uint32_t lin = ((uint32_t)g_lut[v] * bq) >> 16;          /* 8.8 fixed point: LED steps * 256 */
    uint32_t o;
    if (dither) {
        if (lin < 0x80) { *err = 0; return 0; }              /* below half a step: off, no flicker */
        uint32_t sum = lin + *err;
        *err = (uint8_t)(sum & 0xFF);
        o = sum >> 8;
    } else {
        o = (lin + 0x80) >> 8;                               /* camera mode: steady, rounded */
    }
    return o > 255 ? 255 : o;
}

static uint32_t g_wait_us = 0;                            /* time spent waiting for the previous frame */
uint32_t display_take_wait_us(void) { uint32_t w = g_wait_us; g_wait_us = 0; return w; }
#ifdef C64VIZ_WIDE
void display_show_phys(const uint32_t *fb) {                 /* the overlay is drawn on the whole picture (wide.c) */
    float level = g_ovr ? g_ovr_level : g_level, light = g_ovr ? g_ovr_light : g_light;
    bool dither = g_ovr ? g_ovr_dither : g_dither;
#else
static void (*g_overlay)(uint32_t *fb);
static uint32_t g_ovl_buf[PANEL_PHYS_W * PANEL_H];
void display_set_overlay(void (*fn)(uint32_t *fb)) { g_overlay = fn; }

void display_show(const uint32_t *fb_in) {
    const uint32_t *fb = fb_in;
    float level = g_level, light = g_light;
    bool dither = g_dither;
    if (g_overlay) { memcpy(g_ovl_buf, fb_in, sizeof g_ovl_buf); g_overlay(g_ovl_buf); fb = g_ovl_buf; }
#endif
    /* the previous frame must be completely out (DMA + PIO FIFO + latch gap) */
    uint32_t t0 = time_us_32();
    dma_channel_wait_for_finish_blocking(g_dma);
    sleep_until(g_ready_at);
    g_wait_us += time_us_32() - t0;

    /* current estimate on the gamma-corrected values, then the brightness factor */
    float lvl = level * light;                               /* dimmed by the light sensor, but never below the floor */
    float floor_lvl = level < WS_LIGHT_FLOOR ? level : WS_LIGHT_FLOOR;
    float scale = (lvl < floor_lvl ? floor_lvl : lvl) / 255.0f;
    uint32_t sum = 0;
    for (int i = 0; i < NUM_LEDS; i++) {
        uint32_t p = fb[i];
        sum += (uint32_t)g_lut[(p >> 16) & 0xFF] + g_lut[(p >> 8) & 0xFF] + g_lut[p & 0xFF];
    }
    float est_ma = (sum / 65535.0f) * scale * MA_PER_CHANNEL;
    if (est_ma > (float)POWER_LIMIT_MA) scale *= (float)POWER_LIMIT_MA / est_ma;
    uint32_t bq = (uint32_t)(scale * 65536.0f);              /* 0..65536 */

    for (int y = 0; y < PANEL_H; y++) {
        for (int x = 0; x < PANEL_PHYS_W; x++) {
            uint32_t p = fb[y * PANEL_PHYS_W + x];
            int idx = led_index(x, y);
            uint8_t *e = &g_err[idx * 3];
            uint32_t r = channel_out((p >> 16) & 0xFF, bq, &e[0], dither);
            uint32_t g = channel_out((p >> 8) & 0xFF, bq, &e[1], dither);
            uint32_t b = channel_out(p & 0xFF, bq, &e[2], dither);
            g_out[idx] = ((g << 16) | (r << 8) | b) << 8u;  /* WS2812B expects GRB, MSB first */
        }
    }

    dma_channel_transfer_from_buffer_now(g_dma, g_out, NUM_LEDS);
    /* 24 bit at 800 kHz = 30 us per LED, plus > 280 us reset gap */
    g_ready_at = make_timeout_time_us(NUM_LEDS * 30 + 350);
}
