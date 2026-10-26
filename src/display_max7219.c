/* MAX7219 driver for 1..N chained 8x8 modules (SPI0, software CS). */
#include "display.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <string.h>

#define REG_DIGIT0      0x01
#define REG_DECODEMODE  0x09
#define REG_INTENSITY   0x0A
#define REG_SCANLIMIT   0x0B
#define REG_SHUTDOWN    0x0C
#define REG_DISPLAYTEST 0x0F


/* same register/value to every module in the chain */
static void write_all(uint8_t reg, uint8_t val) {
    uint8_t buf[2 * MAX7219_MODULES];
    for (int m = 0; m < MAX7219_MODULES; m++) { buf[2 * m] = reg; buf[2 * m + 1] = val; }
    gpio_put(PIN_SPI_CS, 0);
    spi_write_blocking(spi0, buf, sizeof buf);
    gpio_put(PIN_SPI_CS, 1);
}

void display_init(void) {
    spi_init(spi0, 1000 * 1000);
    gpio_set_function(PIN_SPI_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SPI_MOSI, GPIO_FUNC_SPI);
    gpio_init(PIN_SPI_CS);
    gpio_set_dir(PIN_SPI_CS, GPIO_OUT);
    gpio_put(PIN_SPI_CS, 1);

    write_all(REG_DISPLAYTEST, 0x00);
    write_all(REG_SCANLIMIT, 0x07);
    write_all(REG_DECODEMODE, 0x00);
    write_all(REG_INTENSITY, (uint8_t)(MAX_MAX_LEVEL * BRIGHTNESS_DEFAULT / 100));
    write_all(REG_SHUTDOWN, 0x01);
    for (int d = 0; d < 8; d++) write_all(REG_DIGIT0 + d, 0x00);
}

static int g_pct = BRIGHTNESS_DEFAULT, g_int = -1;
static float g_light = 1.0f;
static void apply_intensity(void) {
    float want = MAX_MAX_LEVEL * g_pct / 100.0f * g_light;   /* 0 is still visible on the MAX7219 (1/32 duty) */
    if (g_int >= 0 && want > g_int - 0.8f && want < g_int + 0.8f) return;   /* hysteresis: sensor noise must not toggle a step */
    int v = (int)(want + 0.5f);
    if (v > 15) v = 15;
    if (v != g_int) { g_int = v; write_all(REG_INTENSITY, (uint8_t)v); }
}
void display_set_brightness(int pct) {
    if (pct < BRIGHT_MIN_PCT) pct = BRIGHT_MIN_PCT;
    if (pct > 100) pct = 100;
    g_pct = pct;
    g_int = -1;                                            /* a new setting applies at once, without hysteresis */
    apply_intensity();
}
void display_set_light(float factor) { g_light = factor < 0.0f ? 0.0f : factor > 1.0f ? 1.0f : factor; apply_intensity(); }

static void (*g_overlay)(uint32_t *fb);
static uint32_t g_ovl_buf[PANEL_PHYS_W * PANEL_H];
void display_set_overlay(void (*fn)(uint32_t *fb)) { g_overlay = fn; }
void display_set_dither(bool on) { (void)on; }

uint32_t display_take_wait_us(void) { return 0; }       /* SPI output does not wait for a previous frame */

void display_show(const uint32_t *fb_in) {
    const uint32_t *fb = fb_in;
    if (g_overlay) { memcpy(g_ovl_buf, fb_in, sizeof g_ovl_buf); g_overlay(g_ovl_buf); fb = g_ovl_buf; }
    uint8_t regs[MAX7219_MODULES][8];
    memset(regs, 0, sizeof regs);

    for (int y = 0; y < PANEL_H; y++) {
        for (int x = 0; x < PANEL_PHYS_W; x++) {
            uint32_t p = fb[y * PANEL_PHYS_W + x];
            if (((p >> 16) & 0xFF) + ((p >> 8) & 0xFF) + (p & 0xFF) < 120) continue;   /* monochrome: dim pixels (trails, embers) stay off */
            int module = x / 8, lx = x % 8, ly = y;
#if MAX_FLIP_X
            lx = 7 - lx;
#endif
#if MAX_FLIP_Y
            ly = 7 - ly;
#endif
#if MAX_REVERSE_MODULES
            module = MAX7219_MODULES - 1 - module;
#endif
#if MAX_SWAP_XY
            regs[module][lx] |= (uint8_t)(1u << ly);
#else
            regs[module][ly] |= (uint8_t)(1u << lx);
#endif
        }
    }

    /* module index 0 = nearest to DIN; the farthest module must be shifted in first */
    for (int d = 0; d < 8; d++) {
        uint8_t buf[2 * MAX7219_MODULES];
        for (int i = 0; i < MAX7219_MODULES; i++) {
            int m = MAX7219_MODULES - 1 - i;
            buf[2 * i]     = REG_DIGIT0 + d;
            buf[2 * i + 1] = regs[m][d];
        }
        gpio_put(PIN_SPI_CS, 0);
        spi_write_blocking(spi0, buf, sizeof buf);
        gpio_put(PIN_SPI_CS, 1);
    }
}
