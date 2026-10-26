/* Light sensor maths (light_calc.c): lux conversion, range switching, dimming curve, smoothing,
   and the @light / @lightmin settings. */
#include <stdio.h>
#include <math.h>
#include "light_calc.h"
#include "textfx.c"
void display_init(void){} void display_set_brightness(int s){(void)s;} void display_show(const uint32_t *fb){(void)fb;}
static int fails = 0;
#define EXPECT(c, what) do { if (c) printf("ok   %s\n", what); else { printf("FAIL %s\n", what); fails++; } } while (0)
static int near(float a, float b, float tol) { return fabsf(a - b) <= tol; }

int main(void) {
    /* conversion: fine range 0.0336 lux/count, coarse 16x that */
    printf("fine 3000 counts = %.1f lux, coarse 3000 counts = %.1f lux, coarse 60000 = %.0f lux\n",
           light_lux(3000, LIGHT_FINE), light_lux(3000, LIGHT_COARSE), light_lux(60000, LIGHT_COARSE));
    EXPECT(near(light_lux(3000, LIGHT_FINE), 100.8f, 0.1f), "fine range: 0.0336 lux per count");
    EXPECT(near(light_lux(1000, LIGHT_COARSE), 537.6f, 0.5f), "coarse range below 1000 lux: linear");
    EXPECT(near(light_lux(65535, LIGHT_COARSE), 35232.0f, 1.0f), "coarse range: full scale ~35000 lux (sunlight), no runaway");
    EXPECT(light_conf(LIGHT_FINE) == 0x0800 && light_conf(LIGHT_COARSE) == 0x1000, "ALS_CONF: gain x2 / x1/8, 100 ms, sensor on");

    /* range switching with hysteresis: the same light must give about the same lux in both ranges */
    EXPECT(light_next_range(50001, LIGHT_FINE) == LIGHT_COARSE && light_next_range(49999, LIGHT_FINE) == LIGHT_FINE, "fine -> coarse near the end");
    EXPECT(light_next_range(1999, LIGHT_COARSE) == LIGHT_FINE && light_next_range(2001, LIGHT_COARSE) == LIGHT_COARSE, "coarse -> fine below ~1075 lux");
    float sw_up = light_lux(50001, LIGHT_FINE), sw_down = light_lux(1999, LIGHT_COARSE);
    printf("switch points: up at %.0f lux, down at %.0f lux\n", sw_up, sw_down);
    EXPECT(sw_down < sw_up * 0.8f, "hysteresis between the switch points (no toggling)");

    /* dimming curve: @lightmin in the dark (1 lux), full from 300/1000/3000 lux, even in perceived brightness */
    EXPECT(near(light_target(0.0f, 10, LIGHT_MEDIUM), 0.10f, 1e-5f) && near(light_target(1.0f, 10, LIGHT_MEDIUM), 0.10f, 1e-5f), "dark: @lightmin (10 %)");
    EXPECT(near(light_target(1000.0f, 10, LIGHT_MEDIUM), 1.0f, 1e-6f) && near(light_target(300.0f, 10, LIGHT_WEAK), 1.0f, 1e-6f)
           && near(light_target(3000.0f, 10, LIGHT_STRONG), 1.0f, 1e-6f) && near(light_target(50000.0f, 10, LIGHT_STRONG), 1.0f, 1e-6f), "full from 300 / 1000 / 3000 lux");
    float lx[] = { 1, 10, 100, 300, 1000 };
    printf("   lux    weak  medium  strong   (@lightmin 10)\n");
    for (int i = 0; i < 5; i++) printf("  %5.0f  %4.0f %%  %4.0f %%  %4.0f %%\n", lx[i], light_target(lx[i], 10, 0) * 100, light_target(lx[i], 10, 1) * 100, light_target(lx[i], 10, 2) * 100);
    EXPECT(near(light_target(100.0f, 10, LIGHT_MEDIUM), 0.585f, 0.01f) && near(light_target(10.0f, 10, LIGHT_MEDIUM), 0.287f, 0.01f), "medium: 10 lux 29 %, 100 lux 58 % (as proposed)");
    EXPECT(near(light_target(100.0f, 10, LIGHT_WEAK), 0.746f, 0.01f) && near(light_target(100.0f, 10, LIGHT_STRONG), 0.491f, 0.01f), "100 lux: weak 75 %, strong 49 %");
    EXPECT(near(light_target(1.0f, 0, LIGHT_MEDIUM), 0.01f, 1e-5f) && near(light_target(1.0f, 500, LIGHT_MEDIUM), 1.0f, 1e-6f), "@lightmin clamped to 1..100");
    EXPECT(near(light_target(37.0f, 100, LIGHT_STRONG), 1.0f, 1e-5f), "@lightmin 100: never dimmed");
    EXPECT(near(light_target(100.0f, 10, 7), light_target(100.0f, 10, LIGHT_MEDIUM), 1e-6f), "unknown strength -> medium");
    int mono = 1, order = 1;
    for (int k = 0; k < 3; k++) { float prev = 0; for (float l = 0.3f; l < 5000; l *= 1.2f) { float t = light_target(l, 10, k); if (t < prev - 1e-6f) mono = 0; prev = t; } }
    for (float l = 0.3f; l < 5000; l *= 1.2f) if (light_target(l, 10, 2) > light_target(l, 10, 1) + 1e-6f || light_target(l, 10, 1) > light_target(l, 10, 0) + 1e-6f) order = 0;
    EXPECT(mono, "brighter room never gives a darker matrix (all strengths)");
    EXPECT(order, "strong dims at least as much as medium, medium as much as weak");

    /* room light = minimum of the last 32 readings: the matrix's own light on the sensor drops out */
    light_minwin_t w = { 0 }; float m = 0;
    for (int i = 0; i < 100; i++) m = light_minwin(&w, (i % 4 == 0) ? 2.0f : 45.0f);   /* matrix flashes, room 2 lux */
    EXPECT(near(m, 2.0f, 1e-6f), "matrix light on the sensor (2 lux room, 45 lux flashes) -> 2 lux");
    m = light_minwin(&w, 0.5f); EXPECT(near(m, 0.5f, 1e-6f), "lamp off: counts at once");
    int k = 0; for (; k < 100 && light_minwin(&w, 300.0f) < 299.0f; k++) ;
    printf("lamp on (300 lux): taken after %d readings (%.2f s)\n", k + 1, (k + 1) * 0.25);
    EXPECT(k + 1 == LIGHT_MINWIN, "lamp on: taken after the window (80 readings = 20 s)");

    /* smoothing: first value at once, brighter within ~2 s, darker more slowly */
    light_filter_t f = { 0 };
    EXPECT(near(light_filter(&f, 0.3f, 0.016f), 0.3f, 1e-6f), "first value taken at once (no fade at power-up)");
    int up = 0; while (light_filter(&f, 1.0f, 0.016f) < 0.95f) up++;
    int down = 0; while (light_filter(&f, 0.3f, 0.016f) > 0.335f) down++;
    printf("0.3 -> 0.95: %.1f s, 1.0 -> 0.335: %.1f s\n", up * 0.016, down * 0.016);
    EXPECT(up * 0.016 > 2.0 && up * 0.016 < 6.0, "lamp switched on: brighter within a few seconds");
    EXPECT(down > up, "darker more slowly than brighter (someone walks past: no flicker)");
    light_filter_t g = { 0 }; light_filter(&g, 0.5f, 0.016f);
    float x = 0; for (int i = 0; i < 60; i++) x = light_filter(&g, (i & 1) ? 0.55f : 0.45f, 0.016f);   /* noisy readings */
    EXPECT(near(x, 0.5f, 0.01f), "noisy readings average out (no visible flicker)");

    /* settings */
    textfx_settings_t st;
    textfx_load("x\n", &st);
    EXPECT(st.light && st.light_min == 10 && st.light_curve == LIGHT_MEDIUM, "default: @light on, @lightmin 10, @lightcurve medium");
    textfx_load("@lightcurve weak\nx\n", &st);   EXPECT(st.light_curve == LIGHT_WEAK, "@lightcurve weak");
    textfx_load("@lightcurve strong\nx\n", &st); EXPECT(st.light_curve == LIGHT_STRONG, "@lightcurve strong");
    textfx_load("@lightcurve xyz\nx\n", &st);    EXPECT(st.light_curve == LIGHT_MEDIUM, "@lightcurve unknown -> medium");
    textfx_load("@light off\n@lightmin 40\nx\n", &st);
    EXPECT(!st.light && st.light_min == 40, "@light off, @lightmin 40");
    textfx_load("@lightmin 0\nx\n", &st);  EXPECT(st.light_min == 1, "@lightmin 0 -> 1");
    textfx_load("@lightmin 250\nx\n", &st); EXPECT(st.light_min == 100, "@lightmin 250 -> 100");

    /* brightness in percent: values of older firmware (steps 0..3) and the button presets */
    EXPECT(bright_from_saved(0) == 7 && bright_from_saved(1) == 20 && bright_from_saved(2) == 45 && bright_from_saved(3) == 100,
           "old steps 0..3 (flash, @brightness) -> 7/20/45/100 %");
    EXPECT(bright_from_saved(50) == 50 && bright_from_saved(250) == 100 && bright_from_saved(4) == 4, "percent kept (from 4), above 100 clamped");
    EXPECT(bright_next_preset(7) == 20 && bright_next_preset(20) == 45 && bright_next_preset(45) == 100 && bright_next_preset(100) == 7,
           "button: 7 -> 20 -> 45 -> 100 -> 7");
    EXPECT(bright_next_preset(30) == 45 && bright_next_preset(4) == 7 && bright_next_preset(99) == 100, "button from a slider value: next preset above");
    textfx_load("@brightness 2\nx\n", &st); EXPECT(bright_from_saved(st.brightness) == 45, "@brightness 2 (old step) -> 45 %");
    textfx_load("@brightness 60\nx\n", &st); EXPECT(bright_from_saved(st.brightness) == 60, "@brightness 60 -> 60 %");

    printf(fails ? "FAILED: %d\n" : "all light checks passed\n", fails);
    return fails ? 1 : 0;
}
