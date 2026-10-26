#include "light_calc.h"
#include <math.h>

/* Vishay VEML7700 (datasheet rev. 2022): 0.0042 lux/count at gain x2 and 800 ms; the resolution scales
   with 1/gain and 1/integration time */
#define RES_X2_800 0.0042f

uint16_t light_conf(int range) {
    /* ALS_CONF: bits 12:11 gain (01 = x2, 10 = x1/8), bits 9:6 integration time (0000 = 100 ms),
       persistence 1, interrupt off, bit 0 = 0: sensor on */
    return range == LIGHT_COARSE ? 0x1000 : 0x0800;
}

float light_lux(uint16_t raw, int range) {
    if (range == LIGHT_FINE) return raw * (RES_X2_800 * 8.0f);              /* x2, 100 ms */
    /* x1/8, 100 ms. Above ~1000 lux this gain reads a little low (not linear); irrelevant here, the matrix
       is at full brightness from 300 / 1000 / 3000 lux on (light_target) */
    return raw * (RES_X2_800 * 8.0f * 16.0f);
}

int light_next_range(uint16_t raw, int range) {
    if (range == LIGHT_FINE && raw > 50000) return LIGHT_COARSE;            /* ~1700 lux: close to the end */
    if (range == LIGHT_COARSE && raw < 2000) return LIGHT_FINE;             /* ~1075 lux: back to the fine range */
    return range;
}

#define DARK_LUX 1.0f
#define GAMMA    2.2f
float light_target(float lux, int min_pct, int strength) {
    static const float bright_lux[3] = { 300.0f, 1000.0f, 3000.0f };
    if (min_pct < 1) min_pct = 1;
    if (min_pct > 100) min_pct = 100;
    if (strength < LIGHT_WEAK || strength > LIGHT_STRONG) strength = LIGHT_MEDIUM;
    float lo = min_pct / 100.0f, hi = bright_lux[strength];
    if (lux <= DARK_LUX) return lo;
    if (lux >= hi) return 1.0f;
    float t = logf(lux / DARK_LUX) / logf(hi / DARK_LUX);
    float lo_p = powf(lo, 1.0f / GAMMA);                                     /* the darkest end, perceived */
    return powf(lo_p + (1.0f - lo_p) * t, GAMMA);
}

float light_minwin(light_minwin_t *w, float lux) {
    w->v[w->i] = lux; w->i = (w->i + 1) % LIGHT_MINWIN;
    if (w->n < LIGHT_MINWIN) w->n++;
    float m = lux;
    for (int k = 0; k < w->n; k++) if (w->v[k] < m) m = w->v[k];
    return m;
}

float light_filter(light_filter_t *f, float target, float dt_s) {
    if (!f->init) { f->factor = target; f->init = true; return target; }
    float tau = target > f->factor ? 1.5f : 4.0f;                            /* seconds */
    float a = dt_s / (tau + dt_s);
    f->factor += (target - f->factor) * a;
    return f->factor;
}
