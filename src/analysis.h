/* Audio capture (Pico ADC) and analysis (FFT bands, RMS). The analysis part is portable C. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "config.h"

extern uint16_t g_samples[BLOCK_SIZE];   /* last captured block, raw 12-bit ADC values (mono, or left) */
extern uint16_t g_samples_r[BLOCK_SIZE]; /* right channel, valid while g_stereo is set               */
extern bool     g_stereo;                /* two different channels confirmed (stereo.c)            */
extern bool     g_clipped;               /* last block touched the ADC limits          */

void  analysis_init(void);
float block_mean(void);
void  analyze_spectrum(float *amp);      /* NUM_BANDS band amplitudes in ADC LSB       */
float analyze_rms(void);                 /* rms in ADC LSB, noise-gated                 */
void  analyze_spectrum_of(const uint16_t *x, float *amp);   /* the same for any block, e.g. g_samples_r */
float analyze_rms_of(const uint16_t *x);
float block_mean_of(const uint16_t *x);
float analysis_bin_power(int k);         /* FFT bin power of the last spectrum analysis */

/* Pico only (audio_capture.c) */
void  audio_init(void);
void  capture_block(void);              /* waits for the next HOP_SIZE samples (16 ms), slides them in */
uint32_t audio_take_idle_us(void);      /* idle time measured while waiting since the last call */
void  audio_set_stereo(bool on);        /* @stereo: read the second channel (GP27 / stream) and check it */
int   audio_stereo_state(void);         /* stereo_state_t of the check, -1 = stereo off */
void  audio_stereo_hint(int chips);     /* SIDs of the tune the jukebox started, 0 = none (Pico 2 W) */
uint32_t audio_resyncs(void);           /* stereo ADC restarts after a lost value */
uint32_t audio_stream_waits(void);      /* Pico 2 W: frames that had to wait for stream packets */
