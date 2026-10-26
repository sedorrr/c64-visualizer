/* Text engine: parses a text file with control codes and plays scrolling texts with effects. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Called once per animation frame. Should block for one frame period (e.g. capture one
 * audio block). Return true to abort the current text (audio detected). */
typedef bool (*textfx_wait_fn)(void);

enum { NAMES_OFF = 0, NAMES_MANUAL = 1, NAMES_ALWAYS = 2 };
#define TZ_EUROPE (-32768)
enum { SLEEP_OFF = 0, SLEEP_BREATHE = 1, SLEEP_CLOCK = 2 };

/* Settings read from the '@' header lines of the text file. */
typedef struct {
    uint32_t silence_ms;   /* @silence  : ms without audio before texts start (default 8000) */
    uint32_t speed_ms;     /* @speed    : ms per scrolled pixel (default 60)                 */
    int      brightness;   /* @brightness: percent (0..3 = the former steps), -1 = keep (default -1) */
    uint32_t threshold;    /* @threshold: rms level (ADC LSB) that counts as "audio"; 0 = auto (default) */
    bool     camera;       /* @camera   : on / off (default) - camera mode: no dithering            */
    uint32_t cycle_s;      /* @cycle    : seconds per visualizer mode in automatic switching (default 20) */
    bool     cycle_auto;   /* @switching: auto (default) or button                                      */
    const char *modes;     /* @modes    : comma separated list of modes, NULL = all                     */
    bool     drive_led;    /* @driveled : on (default) / off - drive activity indicator              */
    bool     stereo;       /* @stereo   : on / off (default) - second input GP27 resp. stereo stream */
    bool     light;        /* @light    : on (default) / off - automatic brightness with the VEML7700 sensor */
    int      light_min;    /* @lightmin : power in the dark, percent of the set brightness (default 10)     */
    int      light_curve;  /* @lightcurve: weak / medium (default) / strong - full brightness from 300/1000/3000 lux */
    int      names;        /* @names    : off / manual (default) / always - scroll the mode name on a switch */
    uint16_t sleep_min;    /* @sleep    : matrix off after this many minutes of silence (0 = never)          */
    uint8_t  sleep_style;  /* @sleepstyle: off / breathe / clock - what the "off" matrix shows             */
    uint32_t sleep_color;  /* @sleepcolor: RRGGBB or a colour name; 0 = auto (blue breathing, cyan clock)   */
    int16_t  night_from, night_to;   /* @night 23:00-07:00 (minutes of the day, -1 = none; Pico 2 W)     */
    bool     clock;        /* @clock    : on / off - time between the idle texts (Pico 2 W)                  */
    bool     news;         /* @news     : on (default) / off - retro news from the Pi jukebox between the texts */
    int16_t  tz_min;       /* @timezone : offset in minutes; TZ_EUROPE = CET/CEST with summer time         */
    const char *scroller;  /* @scroller : text of the sine scroller mode (NULL = built-in)          */
    int      scroller_len;
    const char *logo;      /* @logo     : word of the logo equalizer mode (NULL = "C64")            */
    int      logo_len;
    int      modes_len;
    int      wide;         /* @wide     : off (default) / lead / follow - two matrices as one picture (Pico 2 W)   */
    bool     wide_right;   /* @wideside : left (default) / right - where this Pico's matrix is (lead)           */
    const char *wide_partner;   /* @widepartner: name or address of the other Pico (lead)                       */
    int      wide_partner_len;
} textfx_settings_t;

/* Parse the text source (NUL-terminated). Returns the number of playable text lines. */
int  textfx_load(const char *src, textfx_settings_t *settings);
int  textfx_count(void);
/* Play line `index`. Returns true if it played to the end, false if aborted by wait(). */
bool textfx_play(int index, textfx_wait_fn wait, uint32_t frame_ms);
/* Play an arbitrary line with control codes (e.g. a short status message). */
bool textfx_play_str(const char *line, textfx_wait_fn wait, uint32_t frame_ms);
