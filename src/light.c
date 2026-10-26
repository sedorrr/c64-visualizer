/* VEML7700 light sensor (I2C0, GP4/GP5): presence check, automatic range, smoothed brightness factor. */
#include "light.h"
#include "light_calc.h"
#include "config.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#ifdef C64VIZ_WIFI
#include "diag.h"
#else
#define DIAG(...) do { } while (0)
#endif

#define ADDR      0x10
#define REG_CONF  0x00
#define REG_ALS   0x04
#define I2C_TIMEOUT_US 3000

static bool  s_present = false;
static int   s_range = LIGHT_FINE;
static int   s_fails = 0;
static bool  s_skip = false;                 /* the next value still comes from the old range */
static float s_lux = -1.0f;
static float s_last_lux = -1.0f;             /* last value before the sensor was lost */
static absolute_time_t s_next, s_lost_at;
static bool  s_ever = false;                 /* a value has been measured since power-up */
static light_filter_t s_filter;
static light_minwin_t s_win;                 /* room light = minimum of the last 8 s (the matrix lights the sensor too) */
static float s_raw_lux = -1.0f;              /* the latest single reading (diagnostics) */
static float s_factor = 1.0f;

static bool write_conf(int range) {
    uint16_t v = light_conf(range);
    uint8_t b[3] = { REG_CONF, (uint8_t)(v & 0xFF), (uint8_t)(v >> 8) };
    return i2c_write_timeout_us(LIGHT_I2C, ADDR, b, 3, false, I2C_TIMEOUT_US) == 3;
}

static bool read_als(uint16_t *raw) {
    uint8_t reg = REG_ALS, b[2];
    if (i2c_write_timeout_us(LIGHT_I2C, ADDR, &reg, 1, true, I2C_TIMEOUT_US) != 1) return false;
    if (i2c_read_timeout_us(LIGHT_I2C, ADDR, b, 2, false, I2C_TIMEOUT_US) != 2) return false;
    *raw = (uint16_t)(b[0] | b[1] << 8);
    return true;
}

static void bus_setup(void) {
    i2c_init(LIGHT_I2C, 100 * 1000);
    gpio_set_function(PIN_LIGHT_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_LIGHT_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_LIGHT_SDA);                 /* the module has its own pull-ups; these help with long wires */
    gpio_pull_up(PIN_LIGHT_SCL);
}
/* a sensor plugged in (or a wire wobbling) in the middle of a transfer can hold SDA low; after a timeout the SDK
   leaves the controller as it is - so before looking for the sensor: 9 clocks on SCL and a stop, then a fresh
   controller (~0.1 ms) */
static void bus_recover(void) {
    i2c_deinit(LIGHT_I2C);
    gpio_init(PIN_LIGHT_SDA); gpio_set_dir(PIN_LIGHT_SDA, GPIO_IN); gpio_pull_up(PIN_LIGHT_SDA);
    gpio_init(PIN_LIGHT_SCL); gpio_put(PIN_LIGHT_SCL, 1); gpio_set_dir(PIN_LIGHT_SCL, GPIO_OUT);
    for (int i = 0; i < 9 && !gpio_get(PIN_LIGHT_SDA); i++) {
        gpio_put(PIN_LIGHT_SCL, 0); busy_wait_us_32(5); gpio_put(PIN_LIGHT_SCL, 1); busy_wait_us_32(5);
    }
    gpio_put(PIN_LIGHT_SDA, 0); gpio_set_dir(PIN_LIGHT_SDA, GPIO_OUT); busy_wait_us_32(5);   /* stop: SDA up while SCL is up */
    gpio_set_dir(PIN_LIGHT_SDA, GPIO_IN); busy_wait_us_32(5);
    bus_setup();
}

/* no answer: what do the two wires look like? Reported (on change) so a wiring fault can be found without a meter.
   With the Pico's weak pull-up: low = something pulls the line to GND (module without power - its pull-ups hang on
   an unpowered VIN - or the wire on a GND pin). With the weak pull-down: high = the module's pull-ups are there
   (powered and connected). The line is driven low first, so the RP2350 pad latch (erratum E9) cannot hold it high. */
#ifdef C64VIZ_WIFI
static const char *line_state(uint pin) {
    gpio_init(pin); gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin); busy_wait_us_32(50);
    bool up = gpio_get(pin);
    gpio_put(pin, 0); gpio_set_dir(pin, GPIO_OUT); busy_wait_us_32(10);
    gpio_set_dir(pin, GPIO_IN); gpio_pull_down(pin); busy_wait_us_32(50);
    bool held = gpio_get(pin);
    gpio_pull_up(pin);
    return held ? "ok" : up ? "offen" : "an GND";
}
static void probe_lines(void) {
    static const char *last_sda = 0, *last_scl = 0;
    const char *sda = line_state(PIN_LIGHT_SDA), *scl = line_state(PIN_LIGHT_SCL);
    bus_setup();
    if (sda == last_sda && scl == last_scl) return;
    last_sda = sda; last_scl = scl;
    DIAG("licht: kein Sensor - SDA (GP4) %s, SCL (GP5) %s, VIN-Ausgang GP3 %s", sda, scl,
         gpio_get(PIN_LIGHT_PWR) ? "an" : "AUS");
}
#else
static void probe_lines(void) { }          /* no log without WLAN */
#endif

void light_init(void) {
    /* supply on GP3 for a sensor whose VIN is not on 3V3 OUT (the module needs < 0.1 mA, a GPIO gives 12 mA);
       harmless when VIN is on pin 36 and GP3 stays free. The first look for the sensor is 100 ms later. */
    gpio_init(PIN_LIGHT_PWR);
    gpio_set_drive_strength(PIN_LIGHT_PWR, GPIO_DRIVE_STRENGTH_12MA);
    gpio_put(PIN_LIGHT_PWR, 1);
    gpio_set_dir(PIN_LIGHT_PWR, GPIO_OUT);
    bus_setup();
    s_next = make_timeout_time_ms(100);
}

bool  light_present(void) { return s_present; }
float light_lux_now(void) { return s_present ? s_lux : -1.0f; }

float light_tick(bool enabled, int min_pct, int strength, float frame_s) {
    if (time_reached(s_next)) {
        if (!s_present) {                        /* look for the sensor (also plugged in later) */
            s_next = make_timeout_time_ms(5000);
            bus_recover();
            if (write_conf(s_range = LIGHT_FINE)) {
                s_present = true; s_fails = 0; s_skip = true;
                s_next = make_timeout_time_ms(250);
                DIAG("licht: VEML7700 gefunden");
            } else probe_lines();
        } else {
            s_next = make_timeout_time_ms(250);
            uint16_t raw;
            if (!read_als(&raw)) {
                if (++s_fails >= 3) {
                    s_present = false; s_last_lux = s_lux; s_lux = -1.0f; s_lost_at = get_absolute_time();
                    DIAG("licht: Sensor antwortet nicht mehr");
                }
            } else if (s_skip) {
                s_fails = 0; s_skip = false;
            } else {
                s_fails = 0;
                int nr = light_next_range(raw, s_range);
                if (nr != s_range) {
                    if (write_conf(nr)) { s_range = nr; s_skip = true; }
                } else if (to_ms_since_boot(get_absolute_time()) >= 5000) {
                    /* the first seconds are skipped: the boot animation and the noise calibration light the panel
                       brightly, and the sensor saw that (63 lx instead of 10 lx on the test Pico) */
                    if (!s_ever) s_filter.init = false;            /* first value since power-up: jump there */
                    s_ever = true;
                    s_raw_lux = light_lux(raw, s_range);
                    s_lux = light_minwin(&s_win, s_raw_lux);
                }
            }
        }
    }
    /* a lost sensor (loose wire) keeps its last value for a minute before the matrix slowly goes back to the set
       brightness - no jump to full brightness in a dark room, and none back down when it answers again */
    float lux = s_present ? s_lux
              : (s_last_lux >= 0.0f && absolute_time_diff_us(s_lost_at, get_absolute_time()) < 60000000) ? s_last_lux : -1.0f;
    float target = (enabled && lux >= 0.0f) ? light_target(lux, min_pct, strength) : 1.0f;
    s_factor = light_filter(&s_filter, target, frame_s);
    return s_factor;
}
