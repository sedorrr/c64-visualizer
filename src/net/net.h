/*
 * WLAN for the Pico 2 W: web page for texts and settings, audio stream from the Ultimate 64.
 * Only compiled into the c64viz_wifi firmware (C64VIZ_WIFI).
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

enum { NETCMD_NONE = 0, NETCMD_NEXT, NETCMD_CAMERA, NETCMD_BRIGHT, NETCMD_AUTO, NETCMD_CALIBRATE,
       NETCMD_BRIGHT_PCT = 1000 };                       /* + percent: brightness set on the web page */
void net_set_brightness(int pct);               /* current brightness in percent, for the status page */

void net_init(const char *texts);        /* start WLAN (non-blocking); texts = current text source */
void net_task(void);                     /* call once per frame: connection, stream, saving        */
bool net_audio_active(void);             /* audio packets from the Ultimate arrived recently        */
int  net_audio_avail(void);              /* stereo samples waiting (16 kHz)                          */
int  net_audio_read2(uint16_t *l, uint16_t *r, int max);   /* ADC scale (2048 = silence)       */
void net_audio_skip(int n);              /* drop n waiting samples                                   */
bool net_take_message(char *buf, int len);     /* a status line to scroll once ("WLAN 192...")   */
int  net_take_command(void);
void net_set_threshold(int thr);
void net_set_silence(uint32_t ms);       /* how long it has been silent (jukebox: next song) */
bool net_unix_time(int64_t *unix_s);     /* current time from the internet (false until synced) */
void net_set_load(int load_pct, uint32_t busy_max_us, uint32_t frame_max_us);   /* CPU load of core 0 (5 s window) */
void net_set_light(float lux, float factor);   /* light sensor: lux (-1 = none), dimming factor 0..1 */
void net_set_stereo(int state);          /* result of the stereo check (stereo_state_t), -1 = off */
int  net_jb_progress(void);              /* jukebox playing with known length: 0..1000, else -1 */         /* current audio threshold, shown on the status page */             /* button pressed on the web page (NETCMD_*)              */
