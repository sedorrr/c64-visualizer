/* settings.c: switching/camera belong to one texts.txt, the brightness survives a new texts.txt
   (the web page saves texts.txt and restarts - the slider value must stay) */
#include <stdio.h>
#include <string.h>
#include "settings.h"
extern unsigned char host_flash[]; extern int host_erase_count, host_program_count;
static int fails = 0;
#define EXPECT(c, what) do { if (c) printf("ok   %s\n", what); else { printf("FAIL %s\n", what); fails++; } } while (0)
int main(void) {
    memset(host_flash, 0xFF, 4096);
    saved_settings_t sv;
    settings_load(111, &sv);
    EXPECT(!sv.valid && !sv.same_text, "empty flash: nothing saved");
    settings_save(111, false, 60, true);
    settings_load(111, &sv);
    EXPECT(sv.valid && sv.same_text && sv.brightness == 60 && !sv.cycle_auto && sv.camera, "same texts: everything back");
    settings_load(222, &sv);
    EXPECT(sv.valid && !sv.same_text && sv.brightness == 60, "new texts: record found, marked as from other texts, brightness 60 available");
    int writes = host_program_count;
    settings_save(111, false, 60, true);
    EXPECT(host_program_count == writes, "unchanged values: no flash write");
    for (int i = 0; i < 40; i++) settings_save(111, false, 10 + i, true);
    settings_load(111, &sv);
    EXPECT(sv.brightness == 49 && host_erase_count == 2, "40 saves: last value wins, sector erased every 16 records");
    printf(fails ? "FAILED: %d\n" : "all settings checks passed\n", fails);
    return fails ? 1 : 0;
}
