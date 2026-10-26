/* the matrix for host tests of wide.c: remembers what would be shown */
#include <string.h>
#include "display.h"
uint32_t FAKE_SHOWN[PANEL_PHYS_W * PANEL_H]; int FAKE_SHOW_N;
int FAKE_OVR_ON, FAKE_OVR_PCT; float FAKE_OVR_LIGHT; int FAKE_OVR_DITHER;
int FAKE_BRIGHT = 45; float FAKE_LIGHT = 0.5f; int FAKE_DITHER = 1;
void display_show_phys(const uint32_t *fb) { memcpy(FAKE_SHOWN, fb, sizeof FAKE_SHOWN); FAKE_SHOW_N++; }
void display_set_override(bool on, int pct, float light, bool dither) { FAKE_OVR_ON = on; FAKE_OVR_PCT = pct; FAKE_OVR_LIGHT = light; FAKE_OVR_DITHER = dither; }
int   display_get_brightness(void) { return FAKE_BRIGHT; }
float display_get_light(void) { return FAKE_LIGHT; }
bool  display_get_dither(void) { return FAKE_DITHER; }
