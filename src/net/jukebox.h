/* SID jukebox: USB stick of the Ultimate over FTP, playlist in the Pico, song length database. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
void jb_init(void);
void jb_task(void);                                   /* main loop (from net_task) */
/* web routes /jb/... and /sldb/...: HTTP status, or 0 if the path is not ours */
int  jb_route(bool post, const char *path, const char *body, int len, const char **out, int *outlen, const char **type);
bool jb_busy_body(void);                              /* the shared request body buffer is still needed */
int  jb_progress(void);                               /* playing with known length: 0..1000 (per mille), else -1 */
/* SID addresses (only the sockets, temporary): called before a tune is started from the PC upload too */
void jb_sidaddr_need(const uint8_t *sid_header, int len);   /* the next tune; any context */
bool jb_sidaddr_ready(void);                          /* the check for it is done: the tune may start */
void jb_sidaddr_release(void);                        /* playback ended: back to the original addresses */
int  jb_sid_chips(const uint8_t *sid_header, int len);   /* SIDs the tune uses (1-3) */
void jb_stereo_hint(int chips);                       /* tell the stereo display, 0 = playback ended */
void jb_np_cancel(void);
void jb_np_relayed(const char *query);              /* a title relayed along the stream chain (lwIP context) */
bool jb_idle(void);
/* backup of the jukebox settings: the stored record (no switched SID socket in it); restore = check + store */
int  jb_prefs_backup(uint8_t *out, int max);          /* bytes written */
bool jb_prefs_restore(const uint8_t *in, int n, bool write);   /* write = false: only check */                                   /* own jukebox not playing, no SID address waiting to be restored */
void jb_source_changed(void);                         /* another Ultimate is the source now: read its sockets again */                              /* something else plays now: forget the PC jukebox's tune */
const char *jb_sidaddr_note(void);                    /* "SID 1: D400  SID 2: D420" switched for this tune, once */
int  sldb_lookup(const uint8_t md5[16], int song, int *count);   /* seconds, -1 = unknown */
