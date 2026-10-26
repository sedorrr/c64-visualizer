/*
 * Settings store: 16 records of 256 bytes (one flash page each) in one 4 KB sector.
 * New records are appended; the sector is erased only when all 16 pages are used,
 * so a button toggle costs one page program and only every 16th save erases.
 */
#include "settings.h"
#include <string.h>

#define PAGE      256
#define SECTOR    4096
#define NPAGES    (SECTOR / PAGE)
#define REC_MAGIC 0xC64A5E77u

typedef struct {
    uint32_t magic, seq, text_hash;
    uint8_t  cycle_auto, brightness, camera, pad;
    uint32_t check;
} rec_t;

#ifdef HOST_TEST
uint8_t host_flash[SECTOR];
int host_erase_count = 0, host_program_count = 0;
#define SECTOR_PTR ((const uint8_t *)host_flash)
static void do_erase(void) { memset(host_flash, 0xFF, SECTOR); host_erase_count++; }
static void do_program(int page, const uint8_t *buf) {
    for (int i = 0; i < PAGE; i++) host_flash[page * PAGE + i] &= buf[i];   /* flash can only clear bits */
    host_program_count++;
}
#else
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/regs/addressmap.h"
#define SETTINGS_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define SECTOR_PTR ((const uint8_t *)(XIP_BASE + SETTINGS_OFFSET))
static void do_erase(void) {
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(SETTINGS_OFFSET, FLASH_SECTOR_SIZE);
    restore_interrupts(irq);
}
static void do_program(int page, const uint8_t *buf) {
    uint32_t irq = save_and_disable_interrupts();
    flash_range_program(SETTINGS_OFFSET + (uint32_t)page * FLASH_PAGE_SIZE, buf, FLASH_PAGE_SIZE);
    restore_interrupts(irq);
}
#endif

static uint32_t rec_check(const rec_t *r) {
    return r->magic ^ (r->seq * 2654435761u) ^ r->text_hash ^ ((uint32_t)r->cycle_auto | ((uint32_t)r->brightness << 8) | ((uint32_t)r->camera << 16)) ^ 0xA5A5A5A5u;
}

uint32_t settings_hash(const char *text) {
    uint32_t h = 2166136261u;
    for (int i = 0; text[i] && i < 65536; i++) { h ^= (uint8_t)text[i]; h *= 16777619u; }
    return h;
}

/* index of the newest valid record, -1 if none; *first_free = first erased page or NPAGES */
static int scan(rec_t *out, int *first_free) {
    int best = -1; uint32_t best_seq = 0; *first_free = NPAGES;
    for (int p = 0; p < NPAGES; p++) {
        rec_t r; memcpy(&r, SECTOR_PTR + p * PAGE, sizeof r);
        if (r.magic == 0xFFFFFFFFu) { if (*first_free == NPAGES) *first_free = p; continue; }
        if (r.magic != REC_MAGIC || r.check != rec_check(&r)) continue;
        if (best < 0 || r.seq > best_seq) { best = p; best_seq = r.seq; *out = r; }
    }
    return best;
}

void settings_load(uint32_t text_hash, saved_settings_t *out) {
    rec_t r; int free_page;
    out->valid = out->same_text = false;
    if (scan(&r, &free_page) < 0) return;
    out->valid = true;
    out->same_text = r.text_hash == text_hash;
    out->cycle_auto = r.cycle_auto != 0;
    out->brightness = r.brightness;
    out->camera = r.camera != 0;
}

volatile bool settings_frozen = false;
void settings_save(uint32_t text_hash, bool cycle_auto, int brightness, bool camera) {
    if (settings_frozen) return;
    rec_t cur; int free_page, have = scan(&cur, &free_page);
    if (have >= 0 && cur.text_hash == text_hash && cur.cycle_auto == (uint8_t)cycle_auto && cur.brightness == (uint8_t)brightness && cur.camera == (uint8_t)camera)
        return;                                              /* nothing changed: no flash write */
    uint8_t buf[PAGE];
    memset(buf, 0xFF, sizeof buf);
    rec_t r = { REC_MAGIC, have >= 0 ? cur.seq + 1 : 1, text_hash, (uint8_t)cycle_auto, (uint8_t)brightness, (uint8_t)camera, 0, 0 };
    r.check = rec_check(&r);
    memcpy(buf, &r, sizeof r);
    if (free_page >= NPAGES) { do_erase(); free_page = 0; }
    do_program(free_page, buf);
}
