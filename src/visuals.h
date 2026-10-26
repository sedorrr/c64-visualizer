/* The visualizer modes. Each frame: analyse the current audio block, render one mode. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "config.h"

typedef enum {
    VIS_SPECTRUM,    /* classic bars, green/yellow/red, falling peak dots          */
    VIS_VU,          /* horizontal level meter with peak marker                   */
    VIS_MIRROR,      /* bass in the centre, highs to both edges, up and down      */
    VIS_WATERFALL,   /* spectrogram: time scrolls to the left, bass at the bottom */
    VIS_SCOPE,       /* oscilloscope: the waveform itself, phosphor afterglow     */
    VIS_FIRE,        /* flames, each column fed by its frequency band             */
    VIS_PLASMA,      /* demoscene plasma: speed = loudness, colours shift with bass, flash on beats */
    VIS_STARFIELD,   /* stars fly outwards from the centre, speed = loudness, burst on beats        */
    VIS_RASTER,      /* C64 raster bars: bass / mid / treble bars swing across the panel            */
    VIS_SCROLLER,    /* demoscene sine scroller: wave height = bass, colours flow, flash on beats   */
    VIS_LOADER,      /* C64 tape loader stripes: calm "pilot tone" in quiet parts, busy on loud ones */
    VIS_LOGO,        /* a word (e.g. "C64") whose letters fill up like equalizer bars               */
    VIS_MELODY,      /* the strongest tone drawn as a scrolling melody line, colour = note          */
    VIS_RAIN,        /* digital rain: louder bands -> more and faster drops                         */
    VIS_LIFE,        /* Conway's Game of Life, new cells seeded on every beat                       */
    VIS_PULSE,       /* every beat sends a shock wave from the centre, calm glow in between         */
    VIS_FOUNTAIN,    /* every band shoots sparks upwards that fall back with gravity                */
    VIS_COUNT
} vis_t;

extern const char *const VIS_NAMES[VIS_COUNT];

void visuals_init(void);
/* texts for the scroller and the logo (not NUL-terminated, len chars); NULL = built-in default */
void visuals_set_texts(const char *scroller, int scroller_len, const char *logo, int logo_len);
/* analyse g_samples and render mode v into fb (PANEL_W * PANEL_H, 0x00RRGGBB) */
void visuals_frame(vis_t v, uint32_t *fb);
/* the same in two steps: analyse once per frame, then render one or more modes (crossfades) */
void visuals_analyse(void);
void visuals_render(vis_t v, uint32_t *fb);
bool visuals_beat(void);                 /* true in the frame a beat was detected */
/* audio gate: false while there is no sound above the threshold - the modes then show silence
   instead of amplifying noise (the level adaption would otherwise blow noise up to full scale) */
void visuals_set_gate(bool open);
