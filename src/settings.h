/* Persistent settings in the last 4 KB flash sector (automatic/button switching, brightness, camera).
 * Switching and camera are only used while texts.txt is unchanged: new texts make their @switching / @camera
 * lines win again. The brightness stays, unless the new texts.txt has its own @brightness. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* valid: a record exists; same_text: it was saved with this texts.txt (otherwise only the brightness is kept -
   a texts.txt saved on the web page must not undo the brightness set with the slider) */
typedef struct { bool valid, same_text; bool cycle_auto; int brightness; bool camera; } saved_settings_t;

uint32_t settings_hash(const char *text);                       /* FNV-1a of the settings/text source */
void     settings_load(uint32_t text_hash, saved_settings_t *out);
void     settings_save(uint32_t text_hash, bool cycle_auto, int brightness, bool camera);
/* Pico 2 W: a backup was restored into the sector and the Pico restarts soon - no saving of the old values */
extern volatile bool settings_frozen;
#define SETTINGS_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
