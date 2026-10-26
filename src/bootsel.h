/*
 * BOOTSEL without the BOOTSEL key: hold the mode button (GP15) while the Pico powers up
 * (e.g. while plugging in the USB cable). The panel shows a blue frame, then the Pico
 * restarts into its USB drive mode, exactly as if BOOTSEL had been held - handy when the
 * Pico is built into the C64 case.
 * BOOTSEL ohne BOOTSEL-Taste: Modustaster beim Einstecken gedrueckt halten.
 */
#pragma once
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "config.h"
#include "display.h"

static inline void bootsel_check(void) {
    gpio_init(PIN_BUTTON);
    gpio_set_dir(PIN_BUTTON, GPIO_IN);
    gpio_pull_up(PIN_BUTTON);
    sleep_ms(30);                                         /* let the pull-up settle */
    for (int i = 0; i < 10; i++) {                        /* must be held for 100 ms */
        if (gpio_get(PIN_BUTTON)) return;
        sleep_ms(10);
    }
    static uint32_t fb[PANEL_W_MAX * PANEL_H];
    for (int x = 0; x < PANEL_W; x++) { fb[x] = 0x0040FF; fb[(PANEL_H - 1) * PANEL_W + x] = 0x0040FF; }
    for (int y = 0; y < PANEL_H; y++) { fb[y * PANEL_W] = 0x0040FF; fb[y * PANEL_W + PANEL_W - 1] = 0x0040FF; }
    display_show(fb);
    sleep_ms(400);
    reset_usb_boot(0, 0);                                 /* never returns */
}
