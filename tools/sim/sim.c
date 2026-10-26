/* Simulator core: the real firmware analysis + visualizer code, compiled to WebAssembly
   (see tools/make_simulator.py). */
#include "analysis.h"
#include "visuals.h"
static uint32_t fb[PANEL_W * PANEL_H];
void audio_init(void);
void capture_block(void);                  /* demo_audio.c: built-in chiptune */
__attribute__((export_name("sim_init")))     void sim_init(void) { analysis_init(); visuals_init(); audio_init(); }
__attribute__((export_name("sim_samples")))  uint16_t *sim_samples(void) { return g_samples; }
__attribute__((export_name("sim_demo_hop"))) void sim_demo_hop(void) { capture_block(); }   /* next 16 ms of the demo tune */
__attribute__((export_name("sim_frame")))    uint32_t *sim_frame(int mode) { visuals_frame((vis_t)mode, fb); return fb; }
__attribute__((export_name("sim_gate")))     void sim_gate(int open) { visuals_set_gate(open != 0); }
__attribute__((export_name("sim_modes")))    int sim_modes(void) { return VIS_COUNT; }
__attribute__((export_name("sim_hop")))      int sim_hop(void) { return HOP_SIZE; }
__attribute__((export_name("sim_block")))    int sim_block(void) { return BLOCK_SIZE; }
