/*
 * C64 Sound Visualizer
 * Raspberry Pi Pico 2 (RP2350) + WS2812B 8x32 RGB matrix (or MAX7219 8x8 modules)
 *
 * Nine visualizer modes (see visuals.h): spectrum, vu, mirror, waterfall, scope, fire,
 * plasma, starfield, raster. They change automatically every @cycle seconds, or only by
 * button - set in texts.txt (@switching auto|button) and toggled by holding the button 3 s.
 * The toggle and the brightness are saved in flash and survive a restart (until a
 * changed texts.uf2 is flashed, then its settings apply again).
 *
 * Button (GP15 to GND):
 *   short press          next mode (its name scrolls through first)
 *   double press         camera mode on/off ("Cam an" / "Cam aus"; also shown at every start)
 *   hold 0.7-3 s, release next brightness step
 *   hold 3 s             toggle automatic / button switching ("automatic" / "Button" is shown)
 *
 * Idle texts: after @silence ms without audio the texts from texts.txt (flashed as texts.uf2,
 * see tools/make_texts_uf2.py) scroll in turn; audio or a button press brings the visualizer back.
 * A magenta pixel in the top-right corner means the audio input is CLIPPING.
 */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/regs/addressmap.h"
#include "config.h"
#include "display.h"
#include "bootsel.h"
#include "analysis.h"
#include "visuals.h"
#include "textfx.h"
#include "font5x7.h"
#include "settings.h"
#include "light.h"
#include "hardware/watchdog.h"
#ifdef C64VIZ_WIFI
#include "timeutil.h"
#endif
#ifdef C64VIZ_WIFI
#include "net/net.h"
#include "net/wide.h"
#include "net/news.h"
#endif

static uint32_t g_fb[PANEL_W_MAX * PANEL_H];
static uint32_t g_fb_old[PANEL_W_MAX * PANEL_H];        /* previous mode during a crossfade */

/* ---------------------------------------------------------------- settings / texts */
#define TEXTS_MAGIC      "C64VIZTXT1\n"
#define TEXTS_FLASH_ADDR (XIP_BASE + TEXTS_FLASH_OFFSET)
static const char DEFAULT_TEXTS[] =
    "@silence 8000\n"
    "@cycle 20\n"
    "@switching auto\n"
    "{color:8080FF}{typewriter}READY{cursor}{hold:3000}\n"
    "{color:cyan}C64 {color:white}Sound {color:magenta}Visualizer\n";
static textfx_settings_t g_txt;

#define BLOCK_MS ((HOP_SIZE * 1000 + SAMPLE_RATE / 2) / SAMPLE_RATE)   /* one frame = one new audio hop = 16 ms */

static uint32_t g_silent_ms = 0;
static int      g_audio_hits = 0;
static int      g_brightness = BRIGHTNESS_DEFAULT;   /* percent */
static bool     g_camera = false;                    /* camera mode: no dithering */
static absolute_time_t g_bright_save_at;             /* brightness changed: saved to flash at this time */
static bool     g_bright_dirty = false;

static vis_t    g_modes[VIS_COUNT];     /* playlist from @modes */
static int      g_nmodes = 0, g_mode_idx = 0;
static bool     g_auto = true;
static uint32_t g_cycle_ms = 0;
static uint32_t g_text_hash = 0;      /* identifies the flashed settings/text file */

/* ---------------------------------------------------------------- helpers */
static inline void put_pixel(int x, int y, uint32_t c) {
    if ((unsigned)x < PANEL_W && (unsigned)y < PANEL_H) g_fb[y * PANEL_W + x] = c;
}
static inline void clear_fb(void) { memset(g_fb, 0, sizeof g_fb); }

/* @modes spectrum, fire, scope ...  -> playlist; empty or unknown -> all modes */
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

/* ---------------------------------------------------------------- drive activity (Ultimate LED header) */
static absolute_time_t g_drive_until;
static bool g_drive_enabled = true;

static void drive_init(void) {
    gpio_init(PIN_DRIVE_LED);
    gpio_set_dir(PIN_DRIVE_LED, GPIO_IN);
    gpio_pull_down(PIN_DRIVE_LED);                     /* nothing connected = no activity */
    g_drive_until = get_absolute_time();
}
/* the LED outputs can be PWM (260 kHz on the Ultimate), so look for any high level over ~32 us */
static void drive_poll(void) {
    bool on = false;
    for (int i = 0; i < 32 && !on; i++) { on = gpio_get(PIN_DRIVE_LED); busy_wait_us_32(1); }
    if (on) g_drive_until = make_timeout_time_ms(DRIVE_LED_HOLD_MS);
}
static void drive_overlay(uint32_t *fb) {
#ifdef C64VIZ_WIFI
    int p = net_jb_progress();                         /* jukebox: a point wandering along the bottom row */
    if (p >= 0) {
        int w = (p * (PANEL_W - 1) + 500) / 1000;
        uint32_t *row = fb + (PANEL_H - 1) * PANEL_W;
        for (int x = w - 2; x <= w + 2; x++)               /* two dark LEDs on each side, the rest stays for the modes */
            if (x >= 0 && x < PANEL_W) row[x] = x == w ? 0x00485C : 0;
    }
#endif
    if (!g_drive_enabled || absolute_time_diff_us(get_absolute_time(), g_drive_until) <= 0) return;
    for (int i = 0; i < DRIVE_LED_W; i++) {
        int x = DRIVE_LED_X + i, y = DRIVE_LED_Y;
        if ((unsigned)x < PANEL_W && (unsigned)y < PANEL_H) fb[y * PANEL_W + x] = COL_DRIVE;
    }
}

/* ---------------------------------------------------------------- audio tick */
/* one audio hop (= one frame, 16 ms): capture, update silence timer; true = audio present */
/* Auto threshold: during the first second after power-up (the camera status message is shown
   then) the input noise is measured; "sound" = clearly above it. If music is already playing
   at power-up, the default of 25 stays. @threshold <number> switches this off. */
static float g_thr = 25.0f;
#define CAL_MAX 256                                  /* room for up to 4 s of measurement */
static float g_cal[CAL_MAX];
static int   g_cal_n = -1;                                /* -1 = not measuring */

static void calibration_done(void) {
    static float v[CAL_MAX];
    int n = g_cal_n;
    if (n < 4) { g_cal_n = -1; return; }                 /* too short to judge: keep the default */
    memcpy(v, g_cal, n * sizeof(float));
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && v[j] < v[j - 1]; j--) { float t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
    float noise = v[n / 4];                               /* lower quartile: robust against short sounds */
    if (g_txt.threshold == 0 && noise < 80.0f) {
        g_thr = 2.0f * noise + 8.0f;
        if (g_thr < 10.0f) g_thr = 10.0f;
        if (g_thr > 120.0f) g_thr = 120.0f;
    }
    g_cal_n = -1;                                         /* done */
}

/* CPU load of core 0, per 5 s: wall time between frames minus the idle time measured while waiting for
   audio and for the previous display frame (shown on the status page of the Pico 2 W) */
static void load_tick(void) {
    static uint64_t last = 0, win_start = 0, win_idle = 0;
    static uint32_t busy_max = 0, frame_max = 0;
    uint64_t now = time_us_64();
    uint32_t idle = audio_take_idle_us() + display_take_wait_us();
    if (!last) { last = win_start = now; return; }
    uint32_t frame = (uint32_t)(now - last), busy = frame > idle ? frame - idle : 0;
    last = now; win_idle += idle;
    if (busy > busy_max) busy_max = busy;
    if (frame > frame_max) frame_max = frame;
    if (now - win_start >= 5000000) {
        uint64_t span = now - win_start;
        int load = win_idle >= span ? 0 : (int)(100 - win_idle * 100 / span);
#ifdef C64VIZ_WIFI
        net_set_load(load, busy_max, frame_max);
#else
        (void)load;
#endif
        win_start = now; win_idle = 0; busy_max = frame_max = 0;
    }
}

/* result of the stereo check (stereo.c): once on the matrix when it changes, and on the status page */
static char g_local_msg[96];
static bool g_local_pending = false;
static void stereo_watch(void) {
    static int last = -2, told = -1;                    /* told: the last result shown on the matrix */
    int st = audio_stereo_state();
    if (st == last) return;
    last = st;
#ifdef C64VIZ_WIFI
    net_set_stereo(st);
#endif
    static const char *const say[] = { NULL, "{color:00FF80}Stereo", "{color:FFB000}Mono: rechts leer",
                                       "{color:FFB000}Mono: links leer", "{color:FFB000}Mono: beide gleich",
                                       "{color:FFB000}Mono", "{color:00FF80}Stereo" };   /* the last two: from the SID file */
    if (st > 0 && st < 7) {
        int kind = st == 6 ? 1 : st;                     /* stereo measured or from the file: the same news */
        if (kind != told) {                              /* only a real change, not the same result confirmed again */
            told = kind;
            snprintf(g_local_msg, sizeof g_local_msg, "{speed:35}%s", say[st]); g_local_pending = true;
        }
    }
}

/* light sensor (optional): dims the chosen brightness step in a dark room */
static void light_step(void) {
    float f = light_tick(g_txt.light, g_txt.light_min, g_txt.light_curve, BLOCK_MS / 1000.0f);
    display_set_light(f);
#ifdef C64VIZ_WIFI
    net_set_light(light_lux_now(), f);
#endif
}

static bool audio_update(float rms);
static bool audio_tick(void) {
    capture_block();
    load_tick();
    stereo_watch();
    light_step();
    if (g_bright_dirty && time_reached(g_bright_save_at)) {         /* brightness settled: into the flash */
        g_bright_dirty = false;
        settings_save(g_text_hash, g_auto, g_brightness, g_camera);
    }
    watchdog_update();                                 /* a hang longer than 2 s restarts the Pico */
#ifdef C64VIZ_WIFI
    net_task();                                        /* WLAN: connection, stream, saving from the web page */
#endif
    drive_poll();
    float rms = analyze_rms();
    if (g_cal_n >= 0 && g_cal_n < CAL_MAX) g_cal[g_cal_n++] = rms;   /* noise measurement at power-up */
    return audio_update(rms);
}

/* sound detection for one frame: silence timer, false-alarm learning, audio gate */
static bool audio_update(float rms) {
    if (rms > g_thr) { if (g_audio_hits < 1000) g_audio_hits++; } else g_audio_hits = 0;
    bool present = g_audio_hits >= AUDIO_ON_MS / BLOCK_MS;          /* sound must last a moment: short spikes don't count */
    if (present) g_silent_ms = 0; else if (g_silent_ms < 0x7FFFFFFFu) g_silent_ms += BLOCK_MS;   /* no wrap after 49 days */

    /* learn from false alarms: a short "sound" (< BLIP_MS) only slightly above the threshold that is
       followed by silence again was most likely interference - raise the threshold above it */
    static bool episode = false;
    static uint32_t ep_ms = 0;
    static float ep_peak = 0.0f;
    if (present) {
        if (!episode) { episode = true; ep_ms = 0; ep_peak = 0.0f; }
        ep_ms += BLOCK_MS;
        if (rms > ep_peak) ep_peak = rms;
    } else if (episode && g_silent_ms >= 600) {
        episode = false;
        if (g_txt.threshold == 0 && ep_ms < BLIP_MS && ep_peak < 2.0f * g_thr && g_thr < 120.0f) {
            g_thr = ep_peak * 1.25f;
            if (g_thr > 120.0f) g_thr = 120.0f;
        }
    }
#ifdef C64VIZ_WIFI
    net_set_threshold((int)g_thr);
    net_set_silence(g_silent_ms);
#endif

    /* audio gate with hysteresis: closes after GATE_CLOSE_MS of silence */
    static bool gate = true;
    if (present) gate = true;
    else if (g_silent_ms >= GATE_CLOSE_MS) gate = false;
    visuals_set_gate(gate);
    return present;
}

/* ---------------------------------------------------------------- button */
typedef enum { BTN_NONE, BTN_SHORT, BTN_DOUBLE, BTN_TRIPLE, BTN_LONG, BTN_SWITCH } btn_t;

/* short presses are counted: 1, 2 or 3 presses in a row (each within DOUBLE_PRESS_MS of the last)
   are reported as SHORT, DOUBLE or TRIPLE once no further press follows;
   hold 0.7-3 s and release: LONG; hold 3 s: SWITCH (reported while held) */
static btn_t read_button(void) {
    static bool down = false, switch_done = false;
    static int count = 0;
    static absolute_time_t t_down, t_up;
    bool pressed = !gpio_get(PIN_BUTTON);
    absolute_time_t now = get_absolute_time();
    if (pressed && !down) { down = true; switch_done = false; t_down = now; return BTN_NONE; }
    int64_t held = absolute_time_diff_us(t_down, now);
    if (pressed) {
        if (!switch_done && held >= (int64_t)SWITCH_PRESS_MS * 1000) { switch_done = true; count = 0; return BTN_SWITCH; }
        return BTN_NONE;
    }
    if (down) {                                          /* released */
        down = false;
        if (switch_done) return BTN_NONE;
        if (held >= (int64_t)LONG_PRESS_MS * 1000) { count = 0; return BTN_LONG; }
        if (held > 20000) { count++; t_up = now; }       /* a short press: maybe more follow */
        return BTN_NONE;
    }
    if (count > 0 && absolute_time_diff_us(t_up, now) > (int64_t)DOUBLE_PRESS_MS * 1000) {
        btn_t e = count == 1 ? BTN_SHORT : (count == 2 ? BTN_DOUBLE : BTN_TRIPLE);
        count = 0;
        return e;
    }
    return BTN_NONE;
}

/* new brightness in percent: panel and status page at once, the flash a little later (the web page slider
   sends a stream of values while it is dragged - only the last one is written) */
static void set_brightness(int pct) {
    g_brightness = pct;
    display_set_brightness(g_brightness);
    g_bright_save_at = make_timeout_time_ms(2000); g_bright_dirty = true;
#ifdef C64VIZ_WIFI
    net_set_brightness(g_brightness);
#endif
}

/* button, or a button on the web page (Pico 2 W) */
static btn_t poll_input(void) {
    btn_t e = read_button();
#ifdef C64VIZ_WIFI
    if (e == BTN_NONE) {
        int cmd = net_take_command();
        if (cmd >= NETCMD_BRIGHT_PCT && cmd <= NETCMD_BRIGHT_PCT + 100) {
            int pct = cmd - NETCMD_BRIGHT_PCT;              /* percent as such (no step translation here) */
            if (pct < BRIGHT_MIN_PCT) pct = BRIGHT_MIN_PCT;
            if (pct != g_brightness) set_brightness(pct);
            return BTN_NONE;
        }
        switch (cmd) {
            case NETCMD_NEXT:   e = BTN_SHORT;  break;
            case NETCMD_CAMERA: e = BTN_DOUBLE; break;
            case NETCMD_BRIGHT: e = BTN_LONG;   break;
            case NETCMD_AUTO:   e = BTN_SWITCH; break;
            case NETCMD_CALIBRATE: e = BTN_TRIPLE; break;
            default: break;
        }
    }
#endif
    return e;
}

static bool msg_wait(void) { audio_tick(); return false; }   /* messages always play to the end */

/* measure the input noise for CAL_MS while the panel is lit like a busy visualizer (LED current can
   couple into the audio input, so the threshold must already include that); also on a triple press */
static void run_calibration(void) {
    bool fixed = g_txt.threshold != 0;
    g_txt.threshold = 0;                                  /* an explicit new measurement always applies */
    g_cal_n = 0;
    absolute_time_t cal_end = make_timeout_time_ms(CAL_MS);
    while (!time_reached(cal_end)) {                     /* measured by time, not by frames */
        for (int x = 0; x < PANEL_W; x++) {
            float pos = (float)x / PANEL_W;
            uint32_t c = pos < 0.60f ? COL_GREEN : (pos < 0.85f ? COL_YELLOW : COL_RED);
            for (int y = 0; y < PANEL_H; y++) g_fb[y * PANEL_W + x] = x < PANEL_W / 2 ? c : 0;   /* half the panel, like a VU bar */
        }
        display_show(g_fb);
        audio_tick();
    }
    calibration_done();
    if (fixed) g_txt.threshold = (uint32_t)g_thr;
}


static void show_camera_message(void) {
    textfx_play_str(g_camera ? "{color:green}{speed:35}Cam an{hold:1400}"
                             : "{color:red}{speed:35}Cam aus{hold:1400}", msg_wait, BLOCK_MS);
}

static void show_switching_message(void) {
    textfx_play_str(g_auto ? "{color:green}{speed:28}automatic{hold:700}"
                           : "{color:orange}{speed:40}Button{hold:1200}", msg_wait, BLOCK_MS);
}

static btn_t g_pending = BTN_NONE;     /* button event that ended a text or a name early */

/* name of the current mode scrolls through once; another button press skips ahead */
static bool name_wait(void) {
    audio_tick();
    btn_t ev = poll_input();
    if (ev != BTN_NONE) { g_pending = ev; return true; }
    return false;
}
static void show_mode_name(void) {
    char line[48] = "{color:00C0FF}{speed:22}";
    strncat(line, VIS_NAMES[g_modes[g_mode_idx]], sizeof line - strlen(line) - 1);
    textfx_play_str(line, name_wait, BLOCK_MS);
}

static void handle_button(btn_t ev) {
    switch (ev) {
        case BTN_SHORT:
            g_mode_idx = (g_mode_idx + 1) % g_nmodes; g_cycle_ms = 0;
            if (g_txt.names >= NAMES_MANUAL) show_mode_name();
            break;
        case BTN_LONG:
            set_brightness(bright_next_preset(g_brightness));
            break;
        case BTN_SWITCH:
            g_auto = !g_auto; g_cycle_ms = 0;
            settings_save(g_text_hash, g_auto, g_brightness, g_camera);
            show_switching_message();
            break;
        case BTN_TRIPLE: {                                   /* measure the noise again (silence please) */
            textfx_play_str("{color:yellow}{speed:30}Ruhe bitte{hold:600}", msg_wait, BLOCK_MS);
            run_calibration();
            char line[64];
            snprintf(line, sizeof line, "{color:green}{speed:30}Schwelle %d{hold:1200}", (int)g_thr);
            textfx_play_str(line, msg_wait, BLOCK_MS);
            g_silent_ms = 0;
            break;
        }
        case BTN_DOUBLE:
            g_camera = !g_camera;
            display_set_dither(!g_camera);
            settings_save(g_text_hash, g_auto, g_brightness, g_camera);
            show_camera_message();
            break;
        default: break;
    }
}

/* frame callback of the idle texts: stop on audio or on any button event */
static bool text_frame_wait(void) {
    bool audio = audio_tick();
    btn_t ev = poll_input();
    if (ev != BTN_NONE) { g_pending = ev; return true; }
    return audio;
}

/* ---------------------------------------------------------------- boot animation */
static void boot_test(void) {
    /* Orientation test: red dot top-left, green sweep along the top row, then down the left column */
    clear_fb(); put_pixel(0, 0, COL_RED); display_show(g_fb); sleep_ms(600);
    for (int x = 0; x < PANEL_W; x++) { clear_fb(); put_pixel(x, 0, COL_GREEN); display_show(g_fb); sleep_ms(20); }
    for (int y = 0; y < PANEL_H; y++) { clear_fb(); put_pixel(0, y, COL_GREEN); display_show(g_fb); sleep_ms(60); }
    clear_fb(); display_show(g_fb); sleep_ms(200);
}

/* ---------------------------------------------------------------- main */
/* ---------------------------------------------------------------- idle: sleep, night, clock */
static absolute_time_t g_wake_until;                   /* a button press keeps the matrix awake for a minute */

#ifdef C64VIZ_WIFI
static bool local_minutes(int *mins) {                 /* minutes since midnight, if the clock is synced */
    int64_t now;
    if (!net_unix_time(&now)) return false;
    int h, m;
    unix_to_local(now, g_txt.tz_min, &h, &m, NULL);
    *mins = h * 60 + m;
    return true;
}
#endif

/* the matrix stays dark instead of showing idle texts (music still wakes it) */
static bool idle_dark(void) {
    if (!time_reached(g_wake_until)) return false;
    if (g_txt.sleep_min && g_silent_ms >= g_txt.sleep_min * 60000u) return true;
#ifdef C64VIZ_WIFI
    int mins;
    if (g_txt.night_from >= 0 && local_minutes(&mins)) {
        int a = g_txt.night_from, b = g_txt.night_to;
        if (a <= b ? (mins >= a && mins < b) : (mins >= a || mins < b)) return true;   /* window may pass midnight */
    }
#endif
    return false;
}

/* what the switched-off matrix shows (@sleepstyle): dark, a slowly breathing glow, or the time
   fading in and out - always dim, it is meant for the night */
static uint32_t scale_rgb(uint32_t c, float f) {
    if (f < 0.0f) f = 0.0f; if (f > 1.0f) f = 1.0f;
    return ((uint32_t)(((c >> 16) & 255) * f) << 16) | ((uint32_t)(((c >> 8) & 255) * f) << 8) | (uint32_t)((c & 255) * f);
}
static void draw_sleep(void) {
    static uint32_t t_ms = 0;
    t_ms += BLOCK_MS;
    clear_fb();
    int style = g_txt.sleep_style;
#ifdef C64VIZ_WIFI
    int mins = 0;
    if (style == SLEEP_CLOCK && !local_minutes(&mins)) style = SLEEP_BREATHE;   /* no time yet */
#else
    if (style == SLEEP_CLOCK) style = SLEEP_BREATHE;                             /* the Pico 2 has no clock */
#endif
    if (style == SLEEP_BREATHE) {                     /* soft light in the middle, breathing every 6 s */
        float ph = 0.5f - 0.5f * cosf(t_ms * (2.0f * 3.14159265f / 6000.0f));
        float lvl = 0.03f + 0.32f * ph;
        for (int y = 0; y < PANEL_H; y++) for (int x = 0; x < PANEL_W; x++) {
            float dx = x - (PANEL_W - 1) / 2.0f, dy = y - (PANEL_H - 1) / 2.0f;
            float g = expf(-(dx * dx) / (60.0f + 80.0f * ph) - (dy * dy) / 14.0f);
            g_fb[y * PANEL_W + x] = scale_rgb(g_txt.sleep_color ? g_txt.sleep_color : 0x6C5EFF, lvl * g);
        }
    }
#ifdef C64VIZ_WIFI
    else if (style == SLEEP_CLOCK) {                  /* the time, fading out and in every 8 s */
        char s[8];
        snprintf(s, sizeof s, "%02d:%02d", mins / 60, mins % 60);
        int w = 0, gw;
        for (const char *p = s; *p; p++) { font_glyph_w(*p, &gw); w += gw + (p[1] ? 1 : 0); }
        float ph = 0.5f - 0.5f * cosf(t_ms * (2.0f * 3.14159265f / 8000.0f));
        uint32_t c = scale_rgb(g_txt.sleep_color ? g_txt.sleep_color : 0x00C0FF, 0.08f + 0.42f * ph);
        int x = (PANEL_W - w) / 2, y0 = (PANEL_H - FONT_H) / 2;
        for (const char *p = s; *p; p++) {
            const uint8_t *g = font_glyph_w(*p, &gw);
            for (int k = 0; k < gw; k++, x++)
                for (int r = 0; r < FONT_H; r++) if (g[k] & (1u << r)) g_fb[(y0 + r) * PANEL_W + x] = c;
            x++;
        }
    }
#endif
    display_show(g_fb);
}

/* the time between the idle texts (Pico 2 W, once synced) */
static bool show_clock(void) {
#ifdef C64VIZ_WIFI
    int mins;
    if (!g_txt.clock || !local_minutes(&mins)) return true;
    char line[64];
    snprintf(line, sizeof line, "{color:00C0FF}{speed:35}%02d:%02d{hold:3000}", mins / 60, mins % 60);
    return textfx_play_str(line, text_frame_wait, BLOCK_MS);
#else
    return true;
#endif
}

/* a headline of the retro news from the Pi jukebox after the texts (Pico 2 W, when it sends some) */
static bool show_news(void) {
#ifdef C64VIZ_WIFI
    static int next = 0;
    int n = news_count();
    if (!g_txt.news || n == 0) return true;
    char h[NEWS_LEN + 1], line[NEWS_LEN + 48];
    if (!news_get(next++ % n, h, sizeof h)) return true;
    snprintf(line, sizeof line, "{color:FFD000}{speed:30}+++ %s +++", h);
    return textfx_play_str(line, text_frame_wait, BLOCK_MS);
#else
    return true;
#endif
}

int main(void) {
#ifdef DEBUG_USB
    stdio_init_all();
#endif
    gpio_init(PIN_BUTTON);
    gpio_set_dir(PIN_BUTTON, GPIO_IN);
    gpio_pull_up(PIN_BUTTON);

    display_init();
    bootsel_check();                                   /* button held at power-up -> USB drive mode */
    light_init();
    display_set_brightness(g_brightness);
    drive_init();
    audio_init();
    analysis_init();
    visuals_init();
    boot_test();

    /* settings + texts: from flash if texts.uf2 was flashed, else the built-in default */
    const char *texts = (const char *)TEXTS_FLASH_ADDR;
    if (strncmp(texts, TEXTS_MAGIC, strlen(TEXTS_MAGIC)) == 0) texts += strlen(TEXTS_MAGIC);
    else texts = DEFAULT_TEXTS;
    int ntexts = textfx_load(texts, &g_txt);
    if (g_txt.brightness >= 0) g_brightness = bright_from_saved(g_txt.brightness);
    parse_modes(g_txt.modes, g_txt.modes_len);
    visuals_set_texts(g_txt.scroller, g_txt.scroller_len, g_txt.logo, g_txt.logo_len);
    g_auto = g_txt.cycle_auto;
    g_camera = g_txt.camera;
    if (g_txt.threshold > 0) g_thr = (float)g_txt.threshold;
    g_drive_enabled = g_txt.drive_led;
    audio_set_stereo(g_txt.stereo);                     /* @stereo: second input / stereo stream, checked */
    display_set_overlay(drive_overlay);                 /* shown over modes, idle texts and messages */
#ifdef C64VIZ_WIDE
    wide_config(g_txt.wide, g_txt.wide_right, g_txt.wide_partner, g_txt.wide_partner_len);   /* two matrices as one picture */
#endif

    /* saved button settings win, as long as the same text file is flashed */
    g_text_hash = settings_hash(texts);
    saved_settings_t sv;
    settings_load(g_text_hash, &sv);
    if (sv.valid && sv.same_text) { g_auto = sv.cycle_auto; g_brightness = bright_from_saved(sv.brightness); g_camera = sv.camera; }
    else if (sv.valid && g_txt.brightness < 0) g_brightness = bright_from_saved(sv.brightness);   /* new texts, no @brightness */
    display_set_brightness(g_brightness);
    display_set_dither(!g_camera);
#ifdef C64VIZ_WIFI
    net_set_brightness(g_brightness);
#endif

#ifdef C64VIZ_WIFI
    net_init(texts);                                     /* WLAN starts in the background */
#endif
    watchdog_enable(2000, true);                         /* fed every frame in audio_tick() */
    /* measure the input noise for ~1 s while the panel is lit like a busy visualizer: LED current can
       couple into the audio input, so the threshold must already include that */
    if (g_txt.threshold == 0) run_calibration();     /* automatic threshold unless @threshold is fixed */
    clear_fb(); display_show(g_fb);
    show_camera_message();

    int clip_hold = 0, text_idx = 0, fade = 0;
    vis_t fade_from = g_modes[0];
    bool text_mode = false;

    while (true) {
        bool audio = audio_tick();
        btn_t ev = g_pending != BTN_NONE ? g_pending : poll_input();
        g_pending = BTN_NONE;
        if (ev != BTN_NONE) { g_silent_ms = 0; text_mode = false; g_wake_until = make_timeout_time_ms(60000); handle_button(ev); continue; }

        if (g_local_pending) { g_local_pending = false; textfx_play_str(g_local_msg, msg_wait, BLOCK_MS); continue; }
#ifdef C64VIZ_WIFI
        {
            char nm[176], line[200];
            if (net_take_message(nm, sizeof nm)) {
                if (nm[0] == '{') snprintf(line, sizeof line, "%s", nm);   /* message brings its own colours (jukebox) */
                else snprintf(line, sizeof line, "{color:00C0FF}{speed:35}%s", nm);
                textfx_play_str(line, msg_wait, BLOCK_MS);
                continue;
            }
        }
#endif
        if (text_mode) {
            if (audio) { text_mode = false; continue; }
            if (idle_dark()) {                                  /* sleep / night: dark (or breathing / clock) until music or a button */
                draw_sleep();
                continue;
            }
            bool ok = true;
            if (ntexts > 0) {
                ok = textfx_play(text_idx, text_frame_wait, BLOCK_MS);
                if (ok) text_idx = (text_idx + 1) % ntexts;
            } else {
                clear_fb(); display_show(g_fb);                 /* no texts: dark (else the last frame stays frozen) */
            }
            if (ok && (text_idx == 0 || ntexts > 0)) ok = show_clock();   /* the time after every text */
            if (ok) ok = show_news();                           /* and a headline, if the Pi sends news */
            if (!ok) text_mode = false;                         /* audio or button */
            continue;
        }

        visuals_analyse();
        visuals_render(g_modes[g_mode_idx], g_fb);
        if (fade > 0) {                                     /* crossfade from the previous mode */
            visuals_render(fade_from, g_fb_old);
            float t = (float)fade / FADE_FRAMES;            /* 1 -> 0: weight of the old picture */
            for (int i = 0; i < PANEL_W * PANEL_H; i++) {
                uint32_t a = g_fb_old[i], b = g_fb[i], r = 0;
                for (int sh = 0; sh <= 16; sh += 8)
                    r |= (uint32_t)(((a >> sh) & 0xFF) * t + ((b >> sh) & 0xFF) * (1.0f - t) + 0.5f) << sh;
                g_fb[i] = r;
            }
            fade--;
        }
        if (g_clipped) clip_hold = (int)(12 / FRAME_K);
        if (clip_hold > 0) { clip_hold--; put_pixel(PANEL_W - 1, 0, COL_CLIP); }
        display_show(g_fb);

        if (g_auto && g_nmodes > 1 && audio) {                  /* count only while music plays */
            g_cycle_ms += BLOCK_MS;
            /* time is up: switch on the next beat (at most BEAT_WAIT_MS later), with a crossfade */
            if (g_cycle_ms >= g_txt.cycle_s * 1000u && (visuals_beat() || g_cycle_ms >= g_txt.cycle_s * 1000u + BEAT_WAIT_MS)) {
                g_cycle_ms = 0;
                fade_from = g_modes[g_mode_idx];
                g_mode_idx = (g_mode_idx + 1) % g_nmodes;
                if (g_txt.names == NAMES_ALWAYS) show_mode_name();
                else fade = FADE_FRAMES;
            }
        }
        if (g_silent_ms >= g_txt.silence_ms) { text_mode = true; text_idx = 0; }   /* idle: texts, clock or dark */
    }
}
