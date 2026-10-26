/*
 * config.h - all tunable settings of the C64 sound visualizer
 * config.h - alle einstellbaren Werte des C64-Sound-Visualizers
 */
#pragma once
#include <stdint.h>

#if !defined(DISPLAY_WS2812) && !defined(DISPLAY_MAX7219)
#define DISPLAY_WS2812
#endif

/* ------------------------------------------------------------------ */
/* Pins (GPIO numbers, not physical pin numbers)                       */
/* ------------------------------------------------------------------ */
#define PIN_BUTTON    15   /* button to GND (internal pull-up)            physical pin 20 */
#define PIN_WS2812    16   /* WS2812B data (via 74AHCT125 + 330R)         physical pin 21 */
#define PIN_SPI_CS    17   /* MAX7219 CS/LOAD                             physical pin 22 */
#define PIN_SPI_SCK   18   /* MAX7219 CLK  (SPI0 SCK)                     physical pin 24 */
#define PIN_SPI_MOSI  19   /* MAX7219 DIN  (SPI0 TX)                      physical pin 25 */
#define ADC_GPIO      26   /* audio input = ADC0                          physical pin 31 */
#define ADC_CHANNEL   0
#define ADC_GPIO_R    27   /* optional right channel (@stereo on) = ADC1   physical pin 32 */
#define ADC_CHANNEL_R 1
#define PIN_STEREO_BIAS 28 /* @stereo on: driven high = 3.3 V for the right channel's upper bias resistor, so the
                              right input needs no second wire on ADC_VREF / 3V3 (plug-in, no soldering) physical pin 34 */
#define PIN_DRIVE_LED 14   /* drive activity from the Ultimate's LED header (via 1k) physical pin 19 */
#define PIN_LIGHT_SDA  4   /* optional VEML7700 light sensor, I2C0 SDA    physical pin 6  */
#define PIN_LIGHT_SCL  5   /*                                 I2C0 SCL    physical pin 7  */
#define PIN_LIGHT_PWR  3   /* optional supply for the light sensor: driven high = 3.3 V, so VIN can go to pin 5 when
                              3V3 (pin 36) is taken - pins 5-8 = VIN SDA SCL GND in one row   physical pin 5  */
#define LIGHT_I2C     i2c0

/* ------------------------------------------------------------------ */
/* Panel geometry: logical (0,0) = top-left, x to the right, y down    */
/* ------------------------------------------------------------------ */
#if defined(DISPLAY_WS2812)
  #define PANEL_PHYS_W 32
#else
  #ifndef MAX7219_MODULES
  #define MAX7219_MODULES 4
  #endif
  #define PANEL_PHYS_W (8 * MAX7219_MODULES)
#endif
/* PANEL_PHYS_W = the matrix on this Pico (display driver). PANEL_W = the picture the modes and texts draw:
   the same, or - two matrices as one picture (Pico 2 W, @wide lead) - twice as wide, chosen at run time.
   Buffers are sized PANEL_W_MAX. */
#ifdef C64VIZ_WIDE
  #define PANEL_W_MAX (2 * PANEL_PHYS_W)
  extern int g_panel_w;
  #define PANEL_W g_panel_w
#else
  #define PANEL_W_MAX PANEL_PHYS_W
  #define PANEL_W PANEL_PHYS_W
#endif
#define PANEL_H 8

/* ------------------------------------------------------------------ */
/* Orientation. If the boot animation looks wrong, change these.       */
/* Ausrichtung. Sieht die Startanimation falsch aus, hier anpassen.    */
/* Boot animation: red pixel top-left, then green sweep left->right    */
/* along the top row, then down the left column.                       */
/* ------------------------------------------------------------------ */
/* WS2812 8x32 flexible panel: usually wired in columns, zig-zag */
#define WS_COLUMN_MAJOR 1   /* 1 = data runs along columns, 0 = along rows */
#define WS_SERPENTINE   1   /* 1 = every 2nd column/row runs backwards      */
#define WS_FLIP_X       0   /* mirror left/right (use if DIN is on the right) */
#define WS_FLIP_Y       0   /* mirror top/bottom                             */

/* MAX7219 modules (FC-16 style 4-in-1 or single 8x8) */
#define MAX_SWAP_XY         0  /* 1 = digit registers are columns instead of rows */
#define MAX_FLIP_X          0  /* mirror inside each 8x8 module                   */
#define MAX_FLIP_Y          0
#define MAX_REVERSE_MODULES 0  /* 1 = leftmost module is the one FARTHEST from DIN */

/* ------------------------------------------------------------------ */
/* Brightness steps (long press on button cycles through them)         */
/* ------------------------------------------------------------------ */
/* brightness in percent (web page slider); the button steps through the presets */
#define BRIGHT_PRESETS       { 7, 20, 45, 100 }   /* the former four steps                                  */
#define BRIGHT_PRESET_COUNT  4
#define BRIGHTNESS_DEFAULT   20                   /* percent                                                */
#define BRIGHT_MIN_PCT        4                   /* above the old steps 0..3, so stored values stay unambiguous */
#define WS_MAX_LEVEL        110   /* WS2812 level at 100 % (0..255, applied after gamma; the current limiter still applies) */
#define MAX_MAX_LEVEL        15   /* MAX7219 intensity at 100 %                                                           */
/* automatic brightness (VEML7700, @light on): the set brightness applies in daylight, in the dark (1 lux) it is
   dimmed down to @lightmin percent; @lightcurve weak/medium/strong = full brightness from 300/1000/3000 lux
   (curve: light_target in light_calc.c) */
#define LIGHT_MIN_DEFAULT   10     /* @lightmin default, percent of the power                */
#define WS_LIGHT_FLOOR       4     /* WS2812: never dimmed below this level (of 255)       */

#ifndef POWER_LIMIT_MA
#define POWER_LIMIT_MA 350   /* WS2812 estimated current cap in mA */
#endif
#define WS_GAMMA   2.2f      /* LED gamma: makes fades and dim colours look like on a screen */
#define WS_DITHER  1         /* temporal dithering: in-between brightness levels for dim pixels */
#define FIRE_LINEAR 1        /* fire keeps its original look (its colours were tuned without gamma) */

/* ------------------------------------------------------------------ */
/* Audio capture / analysis                                            */
/* ------------------------------------------------------------------ */
#define SAMPLE_RATE     16000    /* effective sample rate in Hz                        */
#define ADC_OVERSAMPLE  4        /* ADC runs at 4x and is averaged (simple anti-alias) */
#define BLOCK_SIZE      512      /* samples per analysis window (32 ms)                */
#define HOP_SIZE        256      /* new samples per frame: windows overlap by half -> 16 ms = 62.5 fps */
#define FRAME_MS        ((float)HOP_SIZE * 1000.0f / SAMPLE_RATE)
#define FRAME_K         (FRAME_MS / 32.0f)   /* the per-frame constants below are tuned for 32 ms frames;
                                                the code scales them with FRAME_K, so changing HOP_SIZE
                                                keeps all animations at the same speed */

#define BAND_COLS_PHYS ((PANEL_PHYS_W >= 32) ? 2 : 1)
#define NUM_BANDS  (PANEL_PHYS_W / BAND_COLS_PHYS)   /* 16 on a 32-wide matrix (fixed: the analysis) */
#define BAND_COLS  (PANEL_W / NUM_BANDS)      /* columns per spectrum band: 2, or 4 across two matrices */
#define FREQ_MIN   100.0f        /* lowest band centre in Hz  */
#define FREQ_MAX   6000.0f       /* highest band centre in Hz */

#define MIN_REF          40.0f   /* peak reference never drops below this amplitude (ADC LSB) = noise floor guard */
#define NOISE_GATE_LSB    6.0f   /* assumed noise floor (rms, ADC LSB); everything below is shown as silence */
#define SPECTRUM_TILT_DB_PER_OCT 3.0f  /* boosts higher bands (SID energy falls with frequency) */

/* Adaptive level mapping (see visuals.c). Lower MIN_SPAN / MARGIN = livelier, more jumpy. */
#define DYN_MAX_SPAN      36.0f  /* dB: largest range ever shown between empty and full           */
#ifndef SPEC_REF_FALL_DB
#define SPEC_REF_FALL_DB  0.15f  /* dB per frame the spectrum peak reference falls (~4.7 dB/s)     */
#endif
#ifndef SPEC_FLOOR_RATE
#define SPEC_FLOOR_RATE   0.06f  /* how fast the average level follows the music (0..1 per frame)  */
#endif
#ifndef SPEC_FLOOR_MARGIN
#define SPEC_FLOOR_MARGIN 6.0f   /* dB below the average level that counts as "empty" (higher = taller bars) */
#endif
#ifndef SPEC_MIN_SPAN
#define SPEC_MIN_SPAN    12.0f   /* dB: smallest range between empty and full                     */
#endif
#ifndef VU_REF_FALL_DB
#define VU_REF_FALL_DB    0.04f
#endif
#ifndef VU_FLOOR_RATE
#define VU_FLOOR_RATE     0.02f
#endif
#ifndef VU_FLOOR_MARGIN
#define VU_FLOOR_MARGIN   1.5f
#endif
#ifndef VU_MIN_SPAN
#define VU_MIN_SPAN       5.0f
#endif

#define WF_THRESHOLD      0.40f  /* waterfall: levels below this stay dark        */
#ifndef WF_PALETTE
#define WF_PALETTE        0      /* waterfall colours: 0 = water (blue/cyan/white), 1 = heat (red/yellow) */
#endif
#define BAR_FALL          0.7f   /* rows per frame the bars fall                    */
#define PEAK_HOLD_FRAMES  14
#define PEAK_FALL         0.25f
#ifndef VU_FALL
#define VU_FALL           2.4f   /* columns per frame                               */
#endif
#ifndef BAND_BAR_W
#define BAND_BAR_W        1      /* 1 = thin bars with a gap, 2 = touching bars     */
#endif

#define CLIP_LOW   20            /* raw ADC value below this  = clipping */
#define CLIP_HIGH  4075          /* raw ADC value above this  = clipping */

#define DOUBLE_PRESS_MS 350    /* short presses this close together are counted: 2 = camera, 3 = noise measurement */
#define FADE_FRAMES   20       /* crossfade length on automatic mode changes (x 16 ms)            */
#define CAL_MS        1500     /* noise measurement at power-up (panel half lit), max 4000        */
#define AUDIO_ON_MS   160      /* sound must last this long to count (short interference spikes don't) */
#define BLIP_MS       1500     /* shorter "sound" barely above the threshold = interference: threshold rises */
#define GATE_CLOSE_MS 300      /* no sound above the threshold this long: modes show silence      */
#define BEAT_WAIT_MS  3000     /* automatic change waits at most this long for a beat             */
#define LONG_PRESS_MS 700      /* hold longer than this and release: next brightness step     */
#define SWITCH_PRESS_MS 3000   /* hold this long: toggle automatic / button switching of modes */
#define CYCLE_DEFAULT_S 20     /* default seconds per mode in automatic switching               */

/* Flash offset (from the start of flash) where texts.uf2 is written. Must match tools/make_texts_uf2.py */
#define TEXTS_FLASH_OFFSET 0x300000
#define TEXTS_MAX_BYTES    0x10000

/* Colours 0x00RRGGBB */
#define COL_GREEN   0x00C800
#define COL_YELLOW  0xC89000
#define COL_RED     0xE00000
#define COL_PEAK    0x8080A0
#define COL_CLIP    0xFF00FF
#define COL_SCOPE     0x40FF90
#define COL_DRIVE     0xFF1800   /* drive activity indicator: red like the 1541 activity LED */

/* Drive activity indicator (optional, see README): lit while the Ultimate's drive LED is on */
#define DRIVE_LED_HOLD_MS 150     /* keeps short flashes visible                       */
#define DRIVE_LED_X       0       /* position and width of the indicator on the panel  */
#define DRIVE_LED_Y       0
#define DRIVE_LED_W       2
#define COL_SCOPE_DIM 0x148C40
#define COL_SCOPE_R     0x50A0FF   /* stereo: right channel in the oscilloscope */
#define COL_SCOPE_R_DIM 0x183C8C
