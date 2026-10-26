/*
 * Text engine with control codes.  Text-Engine mit Steuercodes.
 *
 * File format / Dateiformat:
 *   # comment                       lines starting with # are ignored, empty lines too
 *   @silence 8000                   header settings (see textfx_settings_t)
 *   text line with {codes}          every other line is one scrolling text
 *
 * Codes (apply to the FOLLOWING word unless noted):
 *   {color:RRGGBB} {color:red}      solid colour for the following text
 *   {rainbow}                       moving rainbow for the following text
 *   {blink} {blink:n}               the next word stops centred and blinks n times (default 3)
 *   {glow} {glow:ms}                the next word stops centred and glows like hot metal
 *   {explode}                       the PREVIOUS word stops centred (after its other effects) and explodes
 *   {hold:ms}                       the PREVIOUS word stops centred for ms (animation keeps running)
 *   {pause:ms}                      screen goes dark for ms, then the text continues
 *   {speed:ms}                      scroll speed from here on (ms per pixel)
 *   {typewriter} {typewriter:ms}    the next word stops centred and is typed letter by letter (default 120 ms)
 *   {wave}  {wave:0}                letters bob up and down from here on (off with :0)
 *   {sparkle} {sparkle:0}           random white glints in the letters from here on (off with :0)
 *   {cursor}                        blinking C64 cursor block right after the previous word, e.g. READY.{cursor}
 * Colour names: red green blue yellow orange white cyan magenta purple pink
 */
#include "textfx.h"
#include "config.h"
#include "display.h"
#include "font5x7.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define MAX_LINES   32
#define MAX_COLS    640          /* strip columns per line (about 100 characters) */
#define MAX_WORDS   96
#define CHAR_GAP    1
#define CURSOR_W    4            /* C64 cursor block width: "READY" + cursor fills the 32 columns exactly */

typedef struct {
    int16_t  first, last;        /* strip columns of the word                 */
    uint8_t  blink, glow, explode, hidden;
    uint16_t glow_ms, hold_ms, pause_ms, speed_ms;
    uint8_t  rainbow, wave, sparkle, cursor;   /* wave/sparkle: style "from here on"; cursor: C64 cursor block appended */
    uint8_t  typing;             /* typewriter: 0 = no, 1 = waiting (invisible), 2 = typed   */
    uint16_t type_ms;            /* ms per letter                                            */
    uint32_t colour;
} word_t;

static const char *g_lines[MAX_LINES];
static int         g_nlines;
static uint16_t    g_default_speed = 60;

/* current strip */
static uint8_t  s_bits[MAX_COLS];
static uint8_t  s_word[MAX_COLS];
static uint8_t  s_cur[MAX_COLS];        /* 1 = column belongs to a blinking C64 cursor block */
static int      s_ncols;
static word_t   s_words[MAX_WORDS];
static int      s_nwords;
static uint32_t g_fb[PANEL_W_MAX * PANEL_H];
static uint16_t g_hue;
static float    g_huef;
static float    g_k = 1.0f;          /* frame period / 32 ms: effects are tuned for 32 ms frames */
static uint32_t g_time_ms;           /* runs with the frames: cursor blink, wave */
static int      s_reveal_word = -1, s_reveal_cols = 0;   /* typewriter: only the first columns of this word */
static uint32_t g_rng = 0x1234567u;
static float frand(void) { g_rng = g_rng * 1664525u + 1013904223u; return (g_rng >> 8) / 16777216.0f; }

/* ------------------------------------------------------------ colours */
static uint32_t hsv(uint16_t h, uint8_t s, uint8_t v) {
    uint8_t region = (uint8_t)(h / 60), rem = (uint8_t)((h % 60) * 255 / 60);
    uint8_t p = (uint8_t)(v * (255 - s) / 255);
    uint8_t q = (uint8_t)(v * (255 - (s * rem) / 255) / 255);
    uint8_t t = (uint8_t)(v * (255 - (s * (255 - rem)) / 255) / 255);
    uint8_t r, g, b;
    switch (region) {
        case 0:  r = v; g = t; b = p; break;
        case 1:  r = q; g = v; b = p; break;
        case 2:  r = p; g = v; b = t; break;
        case 3:  r = p; g = q; b = v; break;
        case 4:  r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}
static uint32_t scale_colour(uint32_t c, float f) {
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    uint32_t r = (uint32_t)(((c >> 16) & 0xFF) * f), g = (uint32_t)(((c >> 8) & 0xFF) * f), b = (uint32_t)((c & 0xFF) * f);
    return (r << 16) | (g << 8) | b;
}
static uint32_t heat(float h) {
    if (h < 0.0f) h = 0.0f;
    if (h > 1.0f) h = 1.0f;
    float r, g, b;
    if (h < 0.4f)      { r = 0.35f + h / 0.4f * 0.65f; g = h / 0.4f * 0.15f;                 b = 0.0f; }
    else if (h < 0.8f) { r = 1.0f;                     g = 0.15f + (h - 0.4f) / 0.4f * 0.75f; b = 0.0f; }
    else               { r = 1.0f;                     g = 0.9f + (h - 0.8f) / 0.2f * 0.1f;  b = (h - 0.8f) / 0.2f; }
    return ((uint32_t)(r * 255) << 16) | ((uint32_t)(g * 255) << 8) | (uint32_t)(b * 255);
}
static uint32_t named_colour(const char *s, int n) {
    static const struct { const char *name; uint32_t c; } tab[] = {
        {"red",0xFF0000},{"green",0x00FF00},{"blue",0x0000FF},{"yellow",0xFFD000},{"orange",0xFF6000},
        {"white",0xFFFFFF},{"cyan",0x00FFFF},{"magenta",0xFF00FF},{"purple",0x8000FF},{"pink",0xFF4090}};
    for (unsigned i = 0; i < sizeof tab / sizeof tab[0]; i++)
        if ((int)strlen(tab[i].name) == n && !strncmp(tab[i].name, s, n)) return tab[i].c;
    if (n == 6) { char h[7]; memcpy(h, s, 6); h[6] = 0; return (uint32_t)strtoul(h, NULL, 16); }
    return 0xFFFFFF;
}

/* ------------------------------------------------------------ loading */
int textfx_load(const char *src, textfx_settings_t *st) {
    st->silence_ms = 8000; st->speed_ms = 60; st->brightness = -1; st->threshold = 0; st->camera = false;
    st->cycle_s = CYCLE_DEFAULT_S; st->cycle_auto = true; st->modes = NULL; st->modes_len = 0; st->drive_led = true; st->stereo = false; st->light = true; st->light_min = LIGHT_MIN_DEFAULT; st->light_curve = 1; st->names = NAMES_MANUAL;
    st->sleep_min = 0; st->sleep_style = SLEEP_OFF; st->sleep_color = 0; st->night_from = st->night_to = -1; st->clock = true; st->news = true; st->tz_min = TZ_EUROPE;
    st->scroller = NULL; st->scroller_len = 0; st->logo = NULL; st->logo_len = 0;
    st->wide = 0; st->wide_right = false; st->wide_partner = NULL; st->wide_partner_len = 0;
    g_nlines = 0;
    const char *p = src;
    while (*p && g_nlines < MAX_LINES) {
        while (*p == ' ' || *p == '\t') p++;
        const char *line = p;
        while (*p && *p != '\n' && *p != '\r') p++;
        int len = (int)(p - line);
        while (*p == '\n' || *p == '\r') p++;
        if (len == 0 || line[0] == '#') continue;
        if (line[0] == '@') {
            const char *key = line + 1, *v = key;
            while (v < line + len && *v != ' ' && *v != '\t') v++;
            int klen = (int)(v - key);
            while (v < line + len && (*v == ' ' || *v == '\t')) v++;
            int vlen = (int)(line + len - v);
            while (vlen > 0 && (v[vlen - 1] == ' ' || v[vlen - 1] == '\t')) vlen--;
            uint32_t num = (uint32_t)strtoul(v, NULL, 10);
            #define KEY(k) (klen == (int)sizeof(k) - 1 && !strncmp(key, k, klen))
            if (KEY("silence"))         st->silence_ms = num;
            else if (KEY("speed"))      st->speed_ms = num ? num : 60;
            else if (KEY("brightness")) st->brightness = (int)num;
            else if (KEY("threshold"))  st->threshold = num;                 /* "auto" parses as 0 = automatic */
            else if (KEY("camera"))     st->camera = (vlen >= 2 && !strncmp(v, "on", 2));
            else if (KEY("cycle"))      st->cycle_s = num ? num : CYCLE_DEFAULT_S;
            else if (KEY("switching"))  st->cycle_auto = !(vlen >= 6 && !strncmp(v, "button", 6));
            else if (KEY("modes"))      { st->modes = v; st->modes_len = vlen; }
            else if (KEY("driveled"))   st->drive_led = !(vlen >= 3 && !strncmp(v, "off", 3));
            else if (KEY("stereo"))     st->stereo = (vlen >= 2 && !strncmp(v, "on", 2));
            else if (KEY("lightmin"))   st->light_min = num < 1 ? 1 : num > 100 ? 100 : (int)num;
            else if (KEY("light"))      st->light = !(vlen >= 3 && !strncmp(v, "off", 3));
            else if (KEY("lightcurve")) st->light_curve = (vlen >= 4 && !strncmp(v, "weak", 4)) ? 0 : (vlen >= 6 && !strncmp(v, "strong", 6)) ? 2 : 1;
            else if (KEY("scroller"))   { st->scroller = v; st->scroller_len = vlen; }
            else if (KEY("logo"))       { st->logo = v; st->logo_len = vlen; }
            else if (KEY("sleepstyle")) st->sleep_style = (vlen >= 7 && !strncmp(v, "breathe", 7)) ? SLEEP_BREATHE
                                                        : (vlen >= 5 && !strncmp(v, "clock", 5)) ? SLEEP_CLOCK : SLEEP_OFF;
            else if (KEY("sleepcolor")) st->sleep_color = (vlen >= 4 && !strncmp(v, "auto", 4)) ? 0 : named_colour(v, vlen);
            else if (KEY("sleep"))      st->sleep_min = (uint16_t)num;
            else if (KEY("clock"))      st->clock = !(vlen >= 3 && !strncmp(v, "off", 3));
            else if (KEY("news"))       st->news = !(vlen >= 3 && !strncmp(v, "off", 3));
            else if (KEY("night")) {                                  /* 23:00-07:00 or off */
                int h1, m1, h2, m2;
                if (sscanf(v, "%d:%d-%d:%d", &h1, &m1, &h2, &m2) == 4) { st->night_from = (int16_t)(h1 * 60 + m1); st->night_to = (int16_t)(h2 * 60 + m2); }
                else st->night_from = st->night_to = -1;
            }
            else if (KEY("timezone")) {                               /* europe | utc | +1 | -5 | +5:30 */
                if (vlen >= 6 && !strncmp(v, "europe", 6)) st->tz_min = TZ_EUROPE;
                else if (vlen >= 3 && !strncmp(v, "utc", 3)) st->tz_min = 0;
                else {
                    int hh = 0, mm = 0; sscanf(v, "%d:%d", &hh, &mm);
                    if (hh < -14) hh = -14;
                    if (hh > 14) hh = 14;                             /* the world's time zones: -12 .. +14 h */
                    if (mm < 0 || mm > 59) mm = 0;
                    st->tz_min = (int16_t)(hh * 60 + (hh < 0 || v[0] == '-' ? -mm : mm));
                }
            }
            else if (KEY("wide"))       st->wide = (vlen >= 4 && !strncmp(v, "lead", 4)) ? 1 : (vlen >= 6 && !strncmp(v, "follow", 6)) ? 2 : 0;
            else if (KEY("wideside"))   st->wide_right = vlen >= 5 && !strncmp(v, "right", 5);
            else if (KEY("widepartner")) { st->wide_partner = v; st->wide_partner_len = vlen; }
            else if (KEY("names"))      st->names = (vlen >= 3 && !strncmp(v, "off", 3)) ? NAMES_OFF
                                                  : (vlen >= 6 && !strncmp(v, "always", 6)) ? NAMES_ALWAYS : NAMES_MANUAL;
            #undef KEY
            continue;
        }
        g_lines[g_nlines++] = line;    /* points into src, terminated by \n or \0 */
    }
    g_default_speed = (uint16_t)st->speed_ms;
    return g_nlines;
}
int textfx_count(void) { return g_nlines; }

/* build the column strip + word table for one line */
static void build_strip(const char *line, uint16_t default_speed) {
    s_ncols = 0; s_nwords = 0;
    word_t cur;                                   /* attributes collected for the next word */
    memset(&cur, 0, sizeof cur);
    cur.colour = 0xFFFFFF; cur.speed_ms = default_speed;
    bool in_word = false;
    const char *p = line;
    while (*p && *p != '\n' && *p != '\r' && s_ncols < MAX_COLS - 8) {
        if (*p == '{') {                          /* control code */
            const char *e = p + 1; while (*e && *e != '}' && *e != '\n') e++;
            if (*e != '}') break;
            const char *name = p + 1, *arg = NULL; int nlen = (int)(e - name), alen = 0;
            for (const char *c = name; c < e; c++) if (*c == ':') { nlen = (int)(c - name); arg = c + 1; alen = (int)(e - arg); break; }
            uint32_t num = arg ? (uint32_t)strtoul(arg, NULL, 10) : 0;
            word_t *prev = (s_nwords > 0) ? &s_words[s_nwords - 1] : NULL;
            if (!strncmp(name, "color", nlen) || !strncmp(name, "colour", nlen)) { cur.colour = arg ? named_colour(arg, alen) : 0xFFFFFF; cur.rainbow = 0; }
            else if (!strncmp(name, "rainbow", nlen)) cur.rainbow = 1;
            else if (!strncmp(name, "blink", nlen))   cur.blink = (uint8_t)(num ? num : 3);
            else if (!strncmp(name, "glow", nlen))  { cur.glow = 1; cur.glow_ms = (uint16_t)(num ? num : 3000); }
            else if (!strncmp(name, "speed", nlen))   cur.speed_ms = (uint16_t)(num ? num : default_speed);
            else if (!strncmp(name, "explode", nlen)) { if (prev) prev->explode = 1; }
            else if (!strncmp(name, "hold", nlen))    { if (prev) prev->hold_ms = (uint16_t)num; }
            else if (!strncmp(name, "pause", nlen))   { if (prev) prev->pause_ms = (uint16_t)num; }
            else if (!strncmp(name, "typewriter", nlen)) cur.type_ms = (uint16_t)(num ? num : 120);
            else if (!strncmp(name, "wave", nlen))    cur.wave = !(arg && num == 0);        /* {wave:0} switches it off */
            else if (!strncmp(name, "sparkle", nlen)) cur.sparkle = !(arg && num == 0);
            else if (!strncmp(name, "cursor", nlen)) {                                      /* C64 cursor block after the word */
                if (!prev || s_ncols == 0 || s_word[s_ncols - 1] == 0xFF) {                 /* no word right before: cursor alone */
                    if (s_nwords >= MAX_WORDS) break;
                    if (s_ncols > 0 && s_word[s_ncols - 1] != 0xFF)
                        for (int k = 0; k < 3 && s_ncols < MAX_COLS; k++) { s_bits[s_ncols] = 0; s_word[s_ncols] = 0xFF; s_cur[s_ncols] = 0; s_ncols++; }
                    s_words[s_nwords] = cur;
                    s_words[s_nwords].first = s_words[s_nwords].last = (int16_t)s_ncols;
                    s_words[s_nwords].explode = 0; s_words[s_nwords].hold_ms = 0; s_words[s_nwords].pause_ms = 0;
                    s_words[s_nwords].hidden = 0; s_words[s_nwords].typing = 0;
                    cur.blink = 0; cur.glow = 0; cur.type_ms = 0;
                    s_nwords++;
                } else {
                    for (int k = 0; k < CHAR_GAP && s_ncols < MAX_COLS; k++) { s_bits[s_ncols] = 0; s_word[s_ncols] = (uint8_t)(s_nwords - 1); s_cur[s_ncols] = 0; s_ncols++; }
                }
                for (int k = 0; k < CURSOR_W && s_ncols < MAX_COLS; k++) { s_bits[s_ncols] = 0x7F; s_word[s_ncols] = (uint8_t)(s_nwords - 1); s_cur[s_ncols] = 1; s_ncols++; }
                s_words[s_nwords - 1].last = (int16_t)(s_ncols - 1);
                s_words[s_nwords - 1].cursor = 1;
                p = e + 1;
                in_word = true;                   /* nothing glued to the cursor without a space */
                continue;
            }
            p = e + 1;
            in_word = false;
            continue;
        }
        char ch = *p++;
        if (ch == ' ' || ch == '\t') {            /* word separator: 1 empty column + normal gap */
            in_word = false;
            for (int k = 0; k < 3 && s_ncols < MAX_COLS; k++) { s_bits[s_ncols] = 0; s_word[s_ncols] = 0xFF; s_cur[s_ncols] = 0; s_ncols++; }
            continue;
        }
        if (!in_word) {                           /* start a new word with the collected attributes */
            if (s_nwords >= MAX_WORDS) break;
            if (s_ncols > 0 && s_word[s_ncols - 1] != 0xFF)   /* word boundary made by a code without a space: add the gap */
                for (int k = 0; k < 3 && s_ncols < MAX_COLS; k++) { s_bits[s_ncols] = 0; s_word[s_ncols] = 0xFF; s_cur[s_ncols] = 0; s_ncols++; }
            s_words[s_nwords] = cur;
            s_words[s_nwords].first = (int16_t)s_ncols;
            s_words[s_nwords].last  = (int16_t)s_ncols;
            s_words[s_nwords].explode = 0; s_words[s_nwords].hold_ms = 0; s_words[s_nwords].pause_ms = 0; s_words[s_nwords].hidden = 0;
            s_words[s_nwords].cursor = 0;
            s_words[s_nwords].typing = cur.type_ms ? 1 : 0;
            cur.blink = 0; cur.glow = 0; cur.type_ms = 0;   /* one-shot attributes */
            s_nwords++;
            in_word = true;
        } else if (s_ncols < MAX_COLS) {          /* gap between characters of the same word */
            for (int k = 0; k < CHAR_GAP; k++) { s_bits[s_ncols] = 0; s_word[s_ncols] = (uint8_t)(s_nwords - 1); s_cur[s_ncols] = 0; s_ncols++; }
        }
        int gw; const uint8_t *g = font_glyph_w(ch, &gw);
        for (int c = 0; c < gw && s_ncols < MAX_COLS; c++) { s_bits[s_ncols] = g[c]; s_word[s_ncols] = (uint8_t)(s_nwords - 1); s_cur[s_ncols] = 0; s_ncols++; }
        s_words[s_nwords - 1].last = (int16_t)(s_ncols - 1);
    }
}

/* ------------------------------------------------------------ rendering */
static inline void put(int x, int y, uint32_t c) {
    if (x >= 0 && x < PANEL_W && y >= 0 && y < PANEL_H) g_fb[y * PANEL_W + x] = c;
}

static uint32_t word_colour(const word_t *w, int x) {
    return w->rainbow ? hsv((uint16_t)((g_hue + x * 6) % 360), 255, 255) : w->colour;
}

/* draw the strip at `offset`; `skip` = word index not to draw (-1 = none) */
static void draw_strip(int offset, int skip) {
    int y0 = (PANEL_H - FONT_H) / 2;
    bool cursor_on = (g_time_ms / 330) % 2 == 0;                 /* C64 cursor: ~1.5 blinks per second */
    for (int x = 0; x < PANEL_W; x++) {
        int col = x - offset;
        if (col < 0 || col >= s_ncols) continue;
        int wi = s_word[col];
        if (wi == 0xFF || wi == skip || s_words[wi].hidden) continue;
        const word_t *w = &s_words[wi];
        if (w->typing == 1 && !(wi == s_reveal_word && col < w->first + s_reveal_cols)) continue;   /* not typed yet */
        if (s_cur[col] && !cursor_on) continue;
        uint8_t bits = s_bits[col];
        if (!bits) continue;
        uint32_t c = word_colour(w, x);
        int yo = (w->wave && sinf(g_time_ms * 0.006f + x * 0.55f) > 0.0f) ? 1 : 0;   /* wave: letters bob by one row */
        if (PANEL_H - FONT_H < 1) yo = 0;
        for (int r = 0; r < FONT_H; r++) if (bits & (1u << r)) {
            uint32_t cc = (w->sparkle && frand() < 0.06f) ? 0xFFFFFF : c;          /* sparkle: random glints */
            put(x, y0 + r + (y0 == 0 ? yo : 0), cc);
        }
    }
}

static void show(void) { display_show(g_fb); g_time_ms += (uint32_t)(32.0f * g_k + 0.5f); }
static void clear(void) { memset(g_fb, 0, sizeof g_fb); }

/* glow: the word `wi` drawn as hot metal with halo, everything else normal */
static void draw_glow(int offset, int wi, float t, float intensity) {
    static uint8_t mask[PANEL_W_MAX * PANEL_H];
    memset(mask, 0, sizeof mask);
    int y0 = (PANEL_H - FONT_H) / 2;
    for (int x = 0; x < PANEL_W; x++) {
        int col = x - offset;
        if (col < 0 || col >= s_ncols || s_word[col] != wi) continue;
        for (int r = 0; r < FONT_H; r++) if (s_bits[col] & (1u << r)) mask[(y0 + r) * PANEL_W + x] = 1;
    }
    clear();
    draw_strip(offset, wi);
    float pulse = 0.65f + 0.35f * sinf(t * 2.6f);
    for (int py = 0; py < PANEL_H; py++) for (int px = 0; px < PANEL_W; px++) {
        float flicker = 0.85f + frand() * 0.15f;
        if (mask[py * PANEL_W + px]) {
            float h = 0.55f + 0.45f * pulse * (0.7f + 0.3f * sinf(t * 4.0f + px * 0.5f));
            g_fb[py * PANEL_W + px] = scale_colour(heat(h), intensity * flicker);
            continue;
        }
        if (g_fb[py * PANEL_W + px]) continue;             /* other words keep their colour */
        int best = 99;
        for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) {
            int nx = px + dx, ny = py + dy;
            if (nx < 0 || nx >= PANEL_W || ny < 0 || ny >= PANEL_H) continue;
            if (mask[ny * PANEL_W + nx]) { int d = abs(dx) + abs(dy); if (d < best) best = d; }
        }
        if (best == 1)      g_fb[py * PANEL_W + px] = scale_colour(heat(0.25f), (0.22f + 0.18f * pulse) * intensity * flicker);
        else if (best == 2) g_fb[py * PANEL_W + px] = scale_colour(heat(0.10f), (0.06f + 0.06f * pulse) * intensity * flicker);
    }
}

/* ------------------------------------------------------------ explosion of word `wi` */
#define SPARKS 70
typedef struct { float x, y, vx, vy, life, fade; uint32_t colour; } particle_t;
static particle_t g_p[PANEL_PHYS_W * PANEL_H + SPARKS];   /* a word wider than one matrix: its first pixels fly */

static bool explode(int offset, int wi, textfx_wait_fn wait) {
    clear(); draw_strip(offset, -1);
    /* particles only from the exploding word's pixels */
    int n = 0;
    float cx = 0, cy = (PANEL_H - 1) / 2.0f; int cnt = 0;
    for (int y = 0; y < PANEL_H; y++) for (int x = 0; x < PANEL_W; x++) {
        int col = x - offset;
        if (col < 0 || col >= s_ncols || s_word[col] != wi || !g_fb[y * PANEL_W + x]) continue;
        cx += x; cnt++;
    }
    if (!cnt) return true;
    cx /= cnt;
    for (int y = 0; y < PANEL_H; y++) for (int x = 0; x < PANEL_W; x++) {
        int col = x - offset;
        if (col < 0 || col >= s_ncols || s_word[col] != wi) continue;
        uint32_t c = g_fb[y * PANEL_W + x];
        if (!c || n >= PANEL_PHYS_W * PANEL_H) continue;
        float dx = x - cx, dy = y - cy, d = sqrtf(dx * dx + dy * dy);
        if (d < 0.5f) { dx = frand() - 0.5f; dy = frand() - 0.5f; d = 0.7f; }
        float speed = 0.55f + frand() * 0.85f;
        g_p[n].x = (float)x; g_p[n].y = (float)y;
        g_p[n].vx = dx / d * speed * 1.7f + (frand() - 0.5f) * 0.9f;
        g_p[n].vy = dy / d * speed * 0.55f + (frand() - 0.5f) * 0.5f;
        g_p[n].life = 1.0f; g_p[n].fade = 0.025f + frand() * 0.03f; g_p[n].colour = c; n++;
    }
    for (int i = 0; i < SPARKS; i++) {
        float a = frand() * 6.2832f, speed = 0.9f + frand() * 1.6f;
        g_p[n].x = cx + (frand() - 0.5f) * 6.0f; g_p[n].y = cy + (frand() - 0.5f) * 2.0f;
        g_p[n].vx = cosf(a) * speed * 1.6f; g_p[n].vy = sinf(a) * speed * 0.6f;
        g_p[n].life = 0.6f + frand() * 0.4f; g_p[n].fade = 0.05f + frand() * 0.06f;
        g_p[n].colour = (i & 1) ? 0xFFFFFF : 0xFFD040; n++;
    }
    s_words[wi].hidden = 1;
    /* flash white, then orange fireball */
    for (int i = 0; i < PANEL_W * PANEL_H; i++) g_fb[i] = 0xFFFFFF;
    for (int r = 0; r < (int)(1.0f / g_k + 0.5f); r++) { show(); if (wait()) return false; }
    for (int y = 0; y < PANEL_H; y++) for (int x = 0; x < PANEL_W; x++)
        g_fb[y * PANEL_W + x] = scale_colour(0xFF6010, 1.0f - fabsf(x - cx) / (PANEL_W / 2.0f) * 0.8f);
    for (int r = 0; r < (int)(1.0f / g_k + 0.5f); r++) { show(); if (wait()) return false; }
    for (int f = 0; f < (int)(60.0f / g_k); f++) {
        clear(); draw_strip(offset, -1);
        int alive = 0;
        for (int i = 0; i < n; i++) {
            particle_t *p = &g_p[i];
            float damp = powf(0.985f, g_k);
            p->x += p->vx * g_k; p->y += p->vy * g_k; p->vy += 0.03f * g_k; p->vx *= damp; p->vy *= damp; p->life -= p->fade * g_k;
            if (p->life <= 0.0f) continue;
            int px = (int)lroundf(p->x), py = (int)lroundf(p->y);
            if (px < 0 || px >= PANEL_W || py < 0 || py >= PANEL_H) continue;
            alive++;
            uint32_t c = scale_colour(p->colour, p->life), old = g_fb[py * PANEL_W + px];
            if ((c & 0xFF) + ((c >> 8) & 0xFF) + ((c >> 16) & 0xFF) > (old & 0xFF) + ((old >> 8) & 0xFF) + ((old >> 16) & 0xFF))
                g_fb[py * PANEL_W + px] = c;
        }
        show();
        if (!alive) break;
        if (wait()) return false;
    }
    return true;
}

/* ------------------------------------------------------------ player */
#define STEP_HUE 3

static bool wait_ms(uint32_t ms, uint32_t frame_ms, textfx_wait_fn wait, int offset) {
    for (uint32_t t = 0; t < ms; t += frame_ms) {
        g_huef = fmodf(g_huef + STEP_HUE * g_k, 360.0f); g_hue = (uint16_t)g_huef;
        clear(); draw_strip(offset, -1); show();
        if (wait()) return false;
    }
    return true;
}

/* run the effects attached to word `wi` while it is stopped at `offset` */
static bool run_effects(int wi, int offset, uint32_t frame_ms, textfx_wait_fn wait) {
    word_t *w = &s_words[wi];
    if (w->typing == 1) {                                     /* typewriter: letters appear one by one */
        int n = w->last - w->first + 1;
        s_reveal_word = wi;
        for (int c = 1; c <= n; c++) {
            int col = w->first + c - 1;
            bool end_of_char = (c == n) || s_bits[col + 1] == 0 || s_cur[col + 1];
            if (!end_of_char || s_cur[col]) continue;          /* reveal whole characters; the cursor comes last */
            s_reveal_cols = c;
            for (uint32_t t = 0; t < w->type_ms; t += frame_ms) {
                clear(); draw_strip(offset, -1);
                if (w->cursor) {                              /* C64-style: the cursor runs ahead of the text */
                    int x0 = offset + w->first + c + CHAR_GAP, y0 = (PANEL_H - FONT_H) / 2;
                    for (int k = 0; k < CURSOR_W; k++) for (int r = 0; r < FONT_H; r++) put(x0 + k, y0 + r, word_colour(w, x0 + k));
                }
                show();
                if (wait()) { s_reveal_word = -1; return false; }
            }
        }
        s_reveal_word = -1;
        w->typing = 2;
    }
    if (w->hold_ms && !wait_ms(w->hold_ms, frame_ms, wait, offset)) return false;
    for (int b = 0; b < w->blink; b++) {
        clear(); draw_strip(offset, wi); show();
        for (uint32_t t = 0; t < 220; t += frame_ms) if (wait()) return false;
        clear(); draw_strip(offset, -1); show();
        for (uint32_t t = 0; t < 220; t += frame_ms) if (wait()) return false;
    }
    if (w->glow) {
        float t = 0.0f;
        for (uint32_t ms = 0; ms < w->glow_ms; ms += frame_ms) {
            draw_glow(offset, wi, t, 1.0f); show(); t += frame_ms / 1000.0f;
            if (wait()) return false;
        }
        const int nf = (int)(25.0f / g_k);
        for (int f = nf; f >= 0; f--) {                       /* fade the glow back to the word's colour */
            draw_glow(offset, wi, t, (float)f / nf);
            if (f < nf / 2) draw_strip(offset, -1);               /* normal colour shows through */
            show(); t += frame_ms / 1000.0f;
            if (wait()) return false;
        }
    }
    if (w->explode && !explode(offset, wi, wait)) return false;
    if (w->pause_ms) {
        clear(); show();
        for (uint32_t t = 0; t < w->pause_ms; t += frame_ms) if (wait()) return false;
    }
    return true;
}

bool textfx_play(int index, textfx_wait_fn wait, uint32_t frame_ms) {
    if (index < 0 || index >= g_nlines) return true;
    return textfx_play_str(g_lines[index], wait, frame_ms);
}

bool textfx_play_str(const char *line, textfx_wait_fn wait, uint32_t frame_ms) {
    g_k = frame_ms / 32.0f;
    build_strip(line, g_default_speed);
    if (!s_ncols) return true;

    /* stop offset for each word: centred on the panel (or left edge on screen if wider) */
    int stop_at[MAX_WORDS];
    for (int i = 0; i < s_nwords; i++) {
        int w = s_words[i].last - s_words[i].first + 1;
        int o = (PANEL_W - w) / 2 - s_words[i].first;
        if (w > PANEL_W) o = -s_words[i].first;
        stop_at[i] = o;
    }

    uint32_t acc = 0;                 /* ms accumulated towards the next pixel step */
    int offset = PANEL_W;
    if (s_nwords > 0 && s_words[0].typing == 1) offset = stop_at[0] + 1;   /* typewriter first: start typing at once */
    int cur_speed = g_default_speed;
    while (offset > -s_ncols) {
        /* speed of the word currently at the panel's left edge (or the first word) */
        int col = -offset; if (col < 0) col = 0; if (col >= s_ncols) col = s_ncols - 1;
        while (col < s_ncols && s_word[col] == 0xFF) col++;
        if (col < s_ncols) cur_speed = s_words[s_word[col]].speed_ms;

        clear(); draw_strip(offset, -1); show();
        g_huef = fmodf(g_huef + STEP_HUE * g_k, 360.0f); g_hue = (uint16_t)g_huef;
        if (wait()) return false;
        acc += frame_ms;
        while (acc >= (uint32_t)cur_speed && offset > -s_ncols) {
            acc -= cur_speed; offset--;
            /* effects of any word that has just reached its stop position */
            for (int i = 0; i < s_nwords; i++) {
                word_t *w = &s_words[i];
                if (stop_at[i] != offset || w->hidden) continue;
                if (w->blink || w->glow || w->explode || w->hold_ms || w->pause_ms || w->typing == 1) {
                    clear(); draw_strip(offset, -1); show();
                    if (!run_effects(i, offset, frame_ms, wait)) return false;
                    acc = 0;
                }
            }
        }
    }
    clear(); show();
    return true;
}
