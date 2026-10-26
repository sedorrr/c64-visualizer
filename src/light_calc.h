/* Light sensor maths (VEML7700): raw counts -> lux, automatic range, lux -> dimming factor.
   Portable C without hardware access (host-tested); the I2C part is in light.c. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* two measuring ranges, both with 100 ms integration time:
   LIGHT_FINE   gain x2   - 0.0336 lux per count, up to ~2200 lux (rooms, also very dark ones)
   LIGHT_COARSE gain x1/8 - 0.5376 lux per count, up to ~35000 lux (sunlight on the sensor) */
enum { LIGHT_FINE = 0, LIGHT_COARSE = 1 };

uint16_t light_conf(int range);                        /* value for the ALS_CONF register (sensor on) */
float    light_lux(uint16_t raw, int range);
int      light_next_range(uint16_t raw, int range);     /* switch near the ends, with hysteresis */

/* lux -> factor for the matrix power: min_pct/100 in the dark (<= 1 lux), 1.0 from the strength's upper end
   (weak 300, medium 1000, strong 3000 lux). In between the curve runs evenly in *perceived* brightness on a
   log lux scale and is then turned into power (gamma 2.2): the eye compresses brightness so strongly that a
   curve straight in power hardly shows in a lit room (82 % power reads as nearly full brightness). */
enum { LIGHT_WEAK = 0, LIGHT_MEDIUM = 1, LIGHT_STRONG = 2 };
float    light_target(float lux, int min_pct, int strength);

/* the room light without the matrix: the sensor also catches light of the matrix itself (measured on the test
   Pico: 0 -> 46 lux within a second with idle texts at night). Texts and visualizer always have dark moments, so the
   smallest value of the last 80 readings (20 s - one bright idle text lasted over 8 s) is the room light; a lamp
   switched off counts at once, one switched on after at most 20 s. */
#define LIGHT_MINWIN 80
typedef struct { float v[LIGHT_MINWIN]; int n, i; } light_minwin_t;
float    light_minwin(light_minwin_t *w, float lux);           /* add a reading, return the window minimum */

/* smoothing: brighter quickly (a lamp is switched on), darker slowly (no flicker when someone walks past) */
typedef struct { float factor; bool init; } light_filter_t;
float    light_filter(light_filter_t *f, float target, float dt_s);
