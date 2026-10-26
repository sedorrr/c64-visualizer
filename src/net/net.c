/*
 * WLAN for the Pico 2 W
 *  - connects to the home WLAN; without settings (or if that fails) it opens its own
 *    access point "C64-Visualizer" (password "commodore64", page at http://192.168.4.1)
 *  - small web server: edit texts.txt, WLAN / Ultimate settings, buttons
 *  - receives the Ultimate 64 audio stream (UDP port 11001, 16-bit stereo ~48 kHz),
 *    mixes it to mono, decimates 3:1 to 16 kHz and hands it to the visualizer
 *  - asks the Ultimate to start the stream via its REST API (PUT /v1/streams/audio:start)
 *
 * lwIP runs in the background (pico_cyw43_arch_lwip_threadsafe_background). Its callbacks only
 * copy data and set flags; flash writes happen in net_task() on the main loop.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/igmp.h"
#include "lwip/apps/sntp.h"
#include "lwip/dns.h"
#include "mdns_lite.h"
#include "stereo.h"
#include "analysis.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "hardware/regs/addressmap.h"
#include "dhcpserver.h"
#include "config.h"
#include "net.h"
#include "web_page.h"
#include "help_page.h"
#include "md5.h"
#include "jukebox.h"
#include "net_internal.h"
#include "diag.h"
#include "ota.h"
#include "u64scan.h"
#include "devices.h"
#include "news.h"
#include "wide.h"
#include "settings.h"
#include "pico/rand.h"

#define AP_SSID        "C64-Visualizer"
#define AP_PASS        "commodore64"
#define AUDIO_PORT     11001                     /* default port of the Ultimate's audio stream */
#define MCAST_DEFAULT  "239.0.1.65"              /* multicast group if the Ultimate has none configured */
#define CONNECT_TIMEOUT_MS 20000                  /* one join: give up after 20 s (unless the router answered) */
#define NOIP_TIMEOUT_MS    10000                  /* at the router, but no address from DHCP: join again after 10 s */
#define JOIN_TRIES         3                      /* the router answered (status NOIP): joins before the access point */
#define LEAVE_MS           1000                   /* leave -> join: let the driver's disassoc event pass first (it would
                                                     clear the state of the new join) */
#define FINAL_MS           2000                   /* BADAUTH/NONET/FAIL count once they hold this long (the driver
                                                     turns a bad auth back to good when a good one follows) */
#ifndef AP_RETRY_MS
#define AP_RETRY_MS    300000                    /* own access point: try the home WLAN again after 5 min */
#endif
#define NETCFG_OFFSET  (PICO_FLASH_SIZE_BYTES - 2 * FLASH_SECTOR_SIZE)   /* sector before the button settings */
#define NETCFG_MAGIC_V1 0xC64E7C01u                      /* old layout without web password */
#define NETCFG_MAGIC_V2 0xC64E7C02u                      /* old layout without device name */
#define NETCFG_MAGIC_V3 0xC64E7C03u                      /* old layout without forwarding */
#define NETCFG_MAGIC_V4 0xC64E7C04u                      /* old layout with one Ultimate */
#define NETCFG_MAGIC   0xC64E7C05u
#define WEB_PW_DEFAULT "commodore64"                     /* password of the web page until it is changed */
#define TEXTS_MAGIC    "C64VIZTXT1\n"

/* ------------------------------------------------------------ stored settings */
typedef struct {                                         /* previous firmware: read once and migrated */
    uint32_t magic;
    char     ssid[33], pass[65], u64[64], u64pw[33];
    uint8_t  stream, pad[2];
    uint32_t check;
} netcfg_v1_t;

typedef struct {                                         /* v2: read once and migrated */
    uint32_t magic;
    char     ssid[33], pass[65], u64[64], u64pw[33], webpw[33];
    uint8_t  stream, smode, pad;
    uint32_t check;
} netcfg_v2_t;

typedef struct {                                         /* v3: read once and migrated */
    uint32_t magic;
    char     ssid[33], pass[65], u64[64], u64pw[33], webpw[33];
    char     name[MDNSL_NAME_MAX + 1];
    uint8_t  stream, smode, pad;
    uint32_t check;
} netcfg_v3_t;

/* smode: where the stream comes from */
enum { SMODE_UNICAST = 0,                                /* the Ultimate streams to this Pico */
       SMODE_MULTICAST = 1,                              /* to a multicast group (several receivers, often slow in WLAN) */
       SMODE_FORWARDED = 2 };                            /* another device forwards it - never ask the Ultimate */

typedef struct {                                         /* v4: read once and migrated */
    uint32_t magic;
    char     ssid[33], pass[65], u64[64], u64pw[33], webpw[33];
    char     name[MDNSL_NAME_MAX + 1];
    uint8_t  stream, smode, pad;
    char     fwd[64];
    uint32_t check;
} netcfg_v4_t;
_Static_assert(offsetof(netcfg_v4_t, fwd) == offsetof(netcfg_v3_t, check), "v3 is the start of v4");

/* several Ultimates (Ultimate 64 / Elite / C64 Ultimate ...), one of them is the source */
#define U64_MAX 4
typedef struct { char label[17], addr[64], pw[33]; } u64cfg_t;
typedef struct {
    uint32_t magic;
    char     ssid[33], pass[65], webpw[33];
    char     name[MDNSL_NAME_MAX + 1];                    /* device name in the network ("" = c64viz) */
    u64cfg_t u64[U64_MAX];                               /* addr "" = slot not used */
    uint8_t  stream, smode, src, pad;                    /* smode: SMODE_*, src: the Ultimate in use */
    char     fwd[64];                                    /* pass every stream packet on to (IP or <name>.local), "" = off */
    uint32_t check;
} netcfg_t;
_Static_assert(sizeof(netcfg_t) <= FLASH_SECTOR_SIZE, "settings must fit into one flash sector");

static netcfg_t g_cfg;

static uint32_t fnv(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}
static uint32_t cfg_check(const netcfg_t *c) { return fnv(c, offsetof(netcfg_t, check)); }

/* layouts v1..v4 (one Ultimate) -> v4 struct; false = nothing valid stored */
static bool cfg_load_old(netcfg_v4_t *o) {
    const void *flash = (const void *)(XIP_BASE + NETCFG_OFFSET);
    netcfg_v4_t v4; memcpy(&v4, flash, sizeof v4);
    netcfg_v2_t v2; memcpy(&v2, flash, sizeof v2);
    netcfg_v3_t v3; memcpy(&v3, flash, sizeof v3);
    memset(o, 0, sizeof *o);
    if (v4.magic == NETCFG_MAGIC_V4 && v4.check == fnv(&v4, offsetof(netcfg_v4_t, check))) {
        *o = v4;
    } else if (v3.magic == NETCFG_MAGIC_V3 && v3.check == fnv(&v3, offsetof(netcfg_v3_t, check))) {
        memcpy(o, &v3, offsetof(netcfg_v3_t, check));    /* everything stays, forwarding is new (off) */
    } else if (v2.magic == NETCFG_MAGIC_V2 && v2.check == fnv(&v2, offsetof(netcfg_v2_t, check))) {
        memcpy(o->ssid, v2.ssid, sizeof v2.ssid); memcpy(o->pass, v2.pass, sizeof v2.pass);   /* the name is new */
        memcpy(o->u64, v2.u64, sizeof v2.u64); memcpy(o->u64pw, v2.u64pw, sizeof v2.u64pw);
        memcpy(o->webpw, v2.webpw, sizeof v2.webpw);
        o->stream = v2.stream; o->smode = v2.smode;
    } else {
        netcfg_v1_t old;
        memcpy(&old, flash, sizeof old);
        if (!(old.magic == NETCFG_MAGIC_V1 && old.check == fnv(&old, offsetof(netcfg_v1_t, check)))) return false;
        memcpy(o->ssid, old.ssid, sizeof old.ssid);       /* keep WLAN and Ultimate settings */
        memcpy(o->pass, old.pass, sizeof old.pass);
        memcpy(o->u64, old.u64, sizeof old.u64);
        memcpy(o->u64pw, old.u64pw, sizeof old.u64pw);
        o->stream = old.stream;
    }
    return true;
}
static void cfg_load(void) {
    memcpy(&g_cfg, (const void *)(XIP_BASE + NETCFG_OFFSET), sizeof g_cfg);
    if (!(g_cfg.magic == NETCFG_MAGIC && g_cfg.check == cfg_check(&g_cfg))) {
        netcfg_v4_t o;
        memset(&g_cfg, 0, sizeof g_cfg);                   /* nothing valid: stream off after a reset */
        if (cfg_load_old(&o)) {                            /* one Ultimate -> slot 1 */
            memcpy(g_cfg.ssid, o.ssid, sizeof o.ssid); memcpy(g_cfg.pass, o.pass, sizeof o.pass);
            memcpy(g_cfg.webpw, o.webpw, sizeof o.webpw); memcpy(g_cfg.name, o.name, sizeof o.name);
            memcpy(g_cfg.u64[0].addr, o.u64, sizeof o.u64); memcpy(g_cfg.u64[0].pw, o.u64pw, sizeof o.u64pw);
            g_cfg.stream = o.stream; g_cfg.smode = o.smode; memcpy(g_cfg.fwd, o.fwd, sizeof o.fwd);
        }
    }
    g_cfg.ssid[32] = g_cfg.pass[64] = g_cfg.webpw[32] = 0;
    for (int i = 0; i < U64_MAX; i++) { g_cfg.u64[i].label[16] = g_cfg.u64[i].addr[63] = g_cfg.u64[i].pw[32] = 0; }
    g_cfg.name[MDNSL_NAME_MAX] = 0; g_cfg.fwd[63] = 0;
    if (!mdnsl_name_valid(g_cfg.name)) g_cfg.name[0] = 0;
    if (g_cfg.smode > SMODE_FORWARDED) g_cfg.smode = SMODE_UNICAST;
    if (g_cfg.src >= U64_MAX || !g_cfg.u64[g_cfg.src].addr[0]) {   /* the chosen one was removed: first configured */
        g_cfg.src = 0;
        for (int i = 0; i < U64_MAX; i++) if (g_cfg.u64[i].addr[0]) { g_cfg.src = (uint8_t)i; break; }
    }
}
static u64cfg_t *cur_u64(void) { return &g_cfg.u64[g_cfg.src < U64_MAX ? g_cfg.src : 0]; }
static uint32_t ip_u32(const char *a) { ip_addr_t x; return ipaddr_aton(a, &x) ? ip4_addr_get_u32(ip_2_ip4(&x)) : 0; }
static const char *base_name(void) { return g_cfg.name[0] ? g_cfg.name : "c64viz"; }
static char g_ap_ssid[33];                               /* "C64-Visualizer" or "C64-Visualizer-<name>" */
static char g_hostname[MDNSL_NAME_MAX + 1];              /* DHCP host name (the router's device list) */
static bool g_name_told = true;                          /* the matrix still has to show IP and name */
static const char *web_password(void) { return g_cfg.webpw[0] ? g_cfg.webpw : WEB_PW_DEFAULT; }

/* erase + program a flash region; called from the main loop only */
static void flash_write(uint32_t offset, const uint8_t *data, uint32_t len) {
    static uint8_t page[FLASH_PAGE_SIZE];
    uint32_t sectors = (len + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    for (uint32_t s = 0; s < sectors; s++) {
        watchdog_update();
        uint32_t irq = save_and_disable_interrupts();
        flash_range_erase(offset + s * FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
        restore_interrupts(irq);
    }
    for (uint32_t pos = 0; pos < len; pos += FLASH_PAGE_SIZE) {
        uint32_t n = len - pos < FLASH_PAGE_SIZE ? len - pos : FLASH_PAGE_SIZE;
        memset(page, 0xFF, sizeof page);
        memcpy(page, data + pos, n);
        uint32_t irq = save_and_disable_interrupts();
        flash_range_program(offset + pos, page, FLASH_PAGE_SIZE);
        restore_interrupts(irq);
        if ((pos & 0xFFF) == 0) watchdog_update();
    }
}

/* ------------------------------------------------------------ state */
typedef enum { NS_OFF, NS_CONNECTING, NS_STA, NS_AP } nstate_t;
static nstate_t g_state = NS_OFF;
static absolute_time_t g_t_connect, g_t_rest, g_t_pps;
static absolute_time_t g_t_retry;                        /* own access point: next try of the home WLAN */
static bool g_was_sta = false;                           /* was in the home WLAN: on trouble connect again, no access point */
static int g_tries;                                      /* joins since connect_sta()/reconnect_sta() */
static bool g_saw_noip;                                  /* ... and one of them got to the router (status NOIP) */
static bool g_leaving;                                   /* left the WLAN, joins again at g_t_join */
static bool g_noip;                                      /* this join got to the router (at g_t_noip) */
static absolute_time_t g_t_join, g_t_noip, g_t_final;
static int g_final_st;                                   /* BADAUTH/NONET/FAIL seen since g_t_final (0 = none) */
static volatile bool g_pending_restore = false;          /* backup in g_body ("netcfg=..&jbprefs=..&settings=.."), checked */
static absolute_time_t g_restart_at;                     /* restored: restart at the latest then (the texts may follow first) */
static bool g_restart_due = false;
static volatile int g_pending_src = -1;                  /* web page chose another Ultimate */
static bool g_force_req = false;                         /* new source: ask it for the stream at once */
static volatile uint32_t g_web_ms = 0;                   /* last web request (ms since boot, 0 = none yet) */
static char g_ip[16] = "-";
static const char *g_texts = "";
static dhcp_server_t g_dhcp;
static bool g_servers_up = false;

static char g_msg[176];
static volatile bool g_msg_pending = false;
static bool g_mcwarn = false;                            /* multicast watch tripped (status + matrix hint) */
static absolute_time_t g_t_mcwatch, g_t_mcmsg;
static volatile int g_cmd = NETCMD_NONE;
static volatile int g_thr_show = 0;
static volatile uint32_t g_silent_show = 0;
void net_set_silence(uint32_t ms) { g_silent_show = ms; }
uint32_t net_silent_ms(void) { return g_silent_show; }

/* ---- time from the internet (SNTP, pool.ntp.org) ---- */
static volatile uint32_t g_sync_unix = 0;               /* Unix seconds at the last sync */
static volatile uint64_t g_sync_us = 0;                 /* time_us_64() at that moment   */
void net_sntp_set_time(unsigned long sec) { g_sync_unix = (uint32_t)sec; g_sync_us = time_us_64(); }
bool net_unix_time(int64_t *unix_s) {
    if (!g_sync_unix) return false;
    *unix_s = (int64_t)g_sync_unix + (int64_t)((time_us_64() - g_sync_us) / 1000000u);
    return true;
}
void net_set_threshold(int thr) { g_thr_show = thr; }
static int g_stereo_st = -1;                             /* stereo check (stereo_state_t), -1 = stereo off */
void net_set_stereo(int state) {
    if (state != g_stereo_st) DIAG("audio: Stereo-Prüfung -> %s", state < 0 ? "aus" : stereo_name((stereo_state_t)state));
    g_stereo_st = state;
}
static int g_bright = -1;                                /* brightness in percent (button / web page) */
void net_set_brightness(int pct) { g_bright = pct; }
static int g_lux10 = -10, g_light_pct = 100;             /* light sensor: lux x 10 (-10 = no sensor), matrix dimmed to % */
void net_set_light(float lux, float factor) {
    int l = lux < 0.0f ? -10 : (int)(lux * 10.0f + 0.5f);
    if ((l < 0) != (g_lux10 < 0)) DIAG("licht: %s", l < 0 ? "kein Sensor" : "Sensor liefert Werte");
    int pct = (int)(factor * 100.0f + 0.5f);
    static int logged = -100;
    if (l >= 0 && (pct >= logged + 5 || pct <= logged - 5)) {            /* only clear changes, not every reading */
        DIAG("licht: %d,%d lx -> Matrix %d %%", l / 10, l % 10, pct); logged = pct;
    }
    g_lux10 = l; g_light_pct = pct;
}
static int g_load = -1;                                  /* CPU load of core 0 in %, -1 = not measured yet */
static uint32_t g_busy_max, g_frame_max, g_stall_max;   /* µs: worst work per frame, longest frame (5 s), longest since start */
void net_set_load(int load_pct, uint32_t busy_max_us, uint32_t frame_max_us) {
    g_load = load_pct; g_busy_max = busy_max_us; g_frame_max = frame_max_us;
    if (frame_max_us > g_stall_max) g_stall_max = frame_max_us;
    if (frame_max_us > 40000)                            /* more than 2.5 frames: the display stood still */
        DIAG("last: Bild stand %lu ms (Auslastung %d %%, Arbeit max. %lu ms)", (unsigned long)(frame_max_us / 1000),
             load_pct, (unsigned long)(busy_max_us / 1000));
}

/* pending saves (filled by the web server, written by net_task) */
static char g_body[TEXTS_MAX_BYTES];
static volatile int g_pending_texts = -1;      /* length of new texts in g_body, -1 = none */
static netcfg_t g_newcfg;
static volatile bool g_pending_cfg = false;

static void set_message(const char *m) {
    strncpy(g_msg, m, sizeof g_msg - 1);
    g_msg[sizeof g_msg - 1] = 0;
    g_msg_pending = true;
}

/* ------------------------------------------------------------ audio stream (UDP) */
#define RING 2048                                        /* pairs (left << 16 | right), 128 ms */
static uint32_t g_ring[RING];
static volatile uint32_t g_wr = 0, g_rd = 0;
static volatile uint32_t g_pkts = 0, g_last_pkt_us = 0;
static uint32_t g_pps = 0, g_pkts_prev = 0;

static volatile uint32_t g_gap_max_us = 0;               /* longest pause between stream packets (watch window) */

/* forwarding: every packet goes on unchanged to a second visualizer (the Ultimate streams to one address only,
   multicast clogs many WLANs - see CLAUDE.md). Addresses as u32 copies for the lwIP context. */
static volatile uint32_t g_fwd_ip = 0, g_my_ip = 0, g_fwd_pkts = 0, g_fwd_err = 0;
/* one stream at a time: the sender whose packets come is kept; another one is ignored until the first has been
   quiet for 0.5 s (two Ultimates streaming here at once, or a forwarder and an Ultimate, would mix) */
#define LOCK_US 500000u
static volatile uint32_t g_lock_ip = 0, g_lock_us = 0, g_rejected = 0;
static volatile uint32_t g_src_last_us = 0;               /* last packet straight from the Ultimate in use */
static volatile uint32_t g_src_ip = 0;                    /* its address (copy for the lwIP context) */
static uint32_t g_fpps = 0, g_fwd_prev = 0;
static struct udp_pcb *g_audio_pcb;                       /* also sends titles along the chain */

static void audio_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    (void)arg; (void)port;
    static uint8_t buf[772];
    static int32_t acc_l = 0, acc_r = 0;
    static int cnt = 0;
    if (!g_cfg.stream) { pbuf_free(p); return; }         /* stream switched off: ignore packets */
    if (g_pkts && net_audio_active()) {                  /* multicast watch: bursty delivery shows as long gaps (not a pause) */
        uint32_t gap = time_us_32() - g_last_pkt_us;
        if (gap > g_gap_max_us) g_gap_max_us = gap;
    }
    uint16_t len = p->tot_len;
    uint16_t n = pbuf_copy_partial(p, buf, sizeof buf, 0);
    pbuf_free(p);
    uint32_t from = addr ? ip4_addr_get_u32(ip_2_ip4(addr)) : 0, now_us = time_us_32();
    if (from && from != g_lock_ip) {
        if (g_lock_ip && now_us - g_lock_us < LOCK_US) { g_rejected++; return; }   /* another stream is live */
        g_lock_ip = from;
    }
    g_lock_us = now_us;
    if (from && from == g_src_ip) g_src_last_us = now_us;
    /* pass it on (also the Ultimate's 2-byte start packet), never back to where it came from or to itself */
    uint32_t to_ip = g_fwd_ip;
    if (to_ip && to_ip != from && to_ip != g_my_ip && len == n) {
        struct pbuf *q = pbuf_alloc(PBUF_TRANSPORT, n, PBUF_RAM);
        if (q) {
            memcpy(q->payload, buf, n);
            ip_addr_t to; ip4_addr_set_u32(ip_2_ip4(&to), to_ip);
            if (udp_sendto(pcb, q, &to, AUDIO_PORT) == ERR_OK) g_fwd_pkts++; else g_fwd_err++;
            pbuf_free(q);
        } else g_fwd_err++;
    }
    /* a title of the PC jukebox, relayed by the visualizer before this one (net_relay_np) - not audio */
    if (n > 7 && n != 770 && n < sizeof buf && !memcmp(buf, "C64NP1 ", 7)) { buf[n] = 0; jb_np_relayed((const char *)buf + 7); return; }
    /* retro news from the Pi jukebox (straight or along the chain) - not audio either */
    if (n >= 9 && n != 770 && len == n && !memcmp(buf, NEWS_MAGIC, 9)) { news_put((const char *)buf + 9, n - 9); return; }
    if (n < 6) return;
    int frames = (n - 2) / 4;                                 /* 2-byte sequence number, then L/R int16 */
    for (int i = 0; i < frames; i++) {
        const uint8_t *s = buf + 2 + 4 * i;
        int16_t l = (int16_t)(s[0] | (s[1] << 8)), r = (int16_t)(s[2] | (s[3] << 8));
        acc_l += l; acc_r += r;
        if (++cnt == 3) {                                     /* 3 stereo frames -> 1 stereo sample (16 kHz) */
            int32_t vl = 2048 + acc_l / 48, vr = 2048 + acc_r / 48;   /* average of 3, then 16 bit -> 12 bit scale */
            vl = vl < 0 ? 0 : (vl > 4095 ? 4095 : vl);
            vr = vr < 0 ? 0 : (vr > 4095 ? 4095 : vr);
            uint32_t w = g_wr;
            if (w - g_rd < RING) { g_ring[w % RING] = (uint32_t)vl << 16 | (uint32_t)vr; g_wr = w + 1; }
            acc_l = acc_r = 0; cnt = 0;
        }
    }
    g_pkts++;
    g_last_pkt_us = time_us_32();
}

/* "Now playing" reached this Pico over the web page (from the PC jukebox or the audio receiver): send it on to
   the next visualizer like the audio, as "C64NP1 <query>" to its stream port - it needs no password there.
   lwIP context (web route). */
void net_relay_np(const char *query) {
    uint32_t to_ip = g_fwd_ip;
    int n = (int)strlen(query);
    if (!g_audio_pcb || !to_ip || to_ip == g_my_ip || n > 760) return;
    struct pbuf *q = pbuf_alloc(PBUF_TRANSPORT, (u16_t)(n + 7), PBUF_RAM);
    if (!q) return;
    memcpy(q->payload, "C64NP1 ", 7); memcpy((char *)q->payload + 7, query, (size_t)n);
    ip_addr_t to; ip4_addr_set_u32(ip_2_ip4(&to), to_ip);
    udp_sendto(g_audio_pcb, q, &to, AUDIO_PORT);
    pbuf_free(q);
}

/* retro news reached this Pico over the web page: on to the next visualizer like the titles (lwIP context) */
static void relay_news(const char *text, int n) {
    uint32_t to_ip = g_fwd_ip;
    if (!g_audio_pcb || !to_ip || to_ip == g_my_ip || n < 0 || n > 760) return;
    struct pbuf *q = pbuf_alloc(PBUF_TRANSPORT, (u16_t)(n + 9), PBUF_RAM);
    if (!q) return;
    memcpy(q->payload, NEWS_MAGIC, 9); memcpy((char *)q->payload + 9, text, (size_t)n);
    ip_addr_t to; ip4_addr_set_u32(ip_2_ip4(&to), to_ip);
    udp_sendto(g_audio_pcb, q, &to, AUDIO_PORT);
    pbuf_free(q);
}

bool net_audio_active(void) {
    return g_pkts > 0 && (uint32_t)(time_us_32() - g_last_pkt_us) < 250000u;
}

/* the jitter buffer in audio_capture.c decides how much to take; see jbuf.h */
int net_audio_avail(void) { return (int)(g_wr - g_rd); }
void net_audio_skip(int n) { uint32_t a = g_wr - g_rd; g_rd += (uint32_t)n < a ? (uint32_t)n : a; }
int net_audio_read2(uint16_t *l, uint16_t *r, int max) {
    int n = 0;
    while (n < max && g_rd != g_wr) { uint32_t v = g_ring[g_rd % RING]; l[n] = (uint16_t)(v >> 16); r[n] = (uint16_t)v; n++; g_rd++; }
    return n;
}

/* ------------------------------------------------------------ REST client: talk to the Ultimate
   RQ_CONFIG: GET /v1/configs/Data%20Streams - read the configured audio destination (multicast group)
   RQ_START:  PUT /v1/streams/audio:start?ip=<this Pico or the group>:11001 */
enum { RQ_NONE, RQ_CONFIG, RQ_START, RQ_UPLOAD, RQ_PUT, RQ_GET, RQ_STOP };
static u64cfg_t g_stop_u64;                              /* RQ_STOP: the previous source, it stops streaming to us */
/* every request ends with rest_done(). A GET or PUT from net_rest_get/put carries a ticket, so the jukebox
   can tell its own answer from the answer to any other request that ran in between */
static uint32_t g_ticket = 0, g_put_ticket = 0, g_get_ticket = 0, g_cur_ticket = 0;
/* the last finished tickets with their HTTP status: a button command or a stream request that runs before the
   jukebox has looked at its answer must not make that answer disappear (the jukebox then waited forever) */
#define DONE_N 4
static volatile uint32_t g_done_t[DONE_N];
static volatile int g_done_c[DONE_N], g_done_i = 0;
static uint32_t g_sel_ticket = 0;                        /* the ticket net_rest_done() last confirmed */
static volatile uint32_t g_get_unread = 0;               /* a GET answer whose body (g_rbuf) is still needed */
static absolute_time_t g_get_unread_at;
static volatile int g_rq;
static void rest_done(int code) {
    if (!g_cur_ticket) return;
    int i = g_done_i; g_done_t[i] = g_cur_ticket; g_done_c[i] = code; g_done_i = (i + 1) % DONE_N;
    if (g_cur_ticket == g_get_ticket) { g_get_unread = g_cur_ticket; g_get_unread_at = get_absolute_time(); }
}
/* no new request may overwrite g_rbuf while a GET answer is unread (at most 2 s, in case nobody reads it) */
static bool rest_blocked(void) {
    if (!g_get_unread) return false;
    if (absolute_time_diff_us(g_get_unread_at, get_absolute_time()) > 2000000) { g_get_unread = 0; return false; }
    return true;
}
static struct tcp_pcb *g_rest = NULL;
static absolute_time_t g_rest_t0;                        /* start of the request in flight (timeout) */
static char g_rest_req[700];
static char g_rbuf[1600];
static int  g_rlen = 0;
static char g_u64state[48] = "nicht eingerichtet";
static char g_group[16] = "";                            /* multicast group in use */
static bool g_cfg_tried = false, g_joined = false;

/* SID jukebox: file received from the web page, forwarded to the Ultimate (POST /v1/runners:sidplay) */
#define SID_BOUNDARY "----c64vizSIDupload"
static volatile int g_pending_sid = -1;                  /* length of the SID file in g_body, -1 = none */
static int  g_sid_song = 0;                              /* sub-tune (1..n), 0 = default             */
static int  g_up_pos = 0, g_up_total = 0;                /* bytes of the upload already queued       */
static char g_up_pre[200];
static int  g_up_pre_len = 0;
static char g_sid_title[160] = "";
static char g_put_path[400];                             /* RQ_PUT: e.g. /v1/machine:reset */
static char g_get_path[160];                             /* RQ_GET: e.g. /v1/configs/SID%20Addressing */
static volatile bool g_pending_get = false;
static char g_cmd_path[40];                              /* buttons reset / pause / resume: an own slot, so a */
static volatile bool g_pending_cmd = false;              /* click never overwrites a request of the jukebox  */
static volatile bool g_pending_put = false;

static bool is_multicast(const char *ip) {
    int a = atoi(ip);
    return strchr(ip, '.') && a >= 224 && a <= 239;
}

static void rest_finish(void) {                          /* complete response received (lwIP context) */
    g_rbuf[g_rlen] = 0;
    int code = (strncmp(g_rbuf, "HTTP/1.", 7) == 0) ? atoi(g_rbuf + 9) : 0;
    DIAG("rest: Antwort %d (rq=%d, %d Bytes)", code, g_rq, g_rlen);
    if (g_rq == RQ_CONFIG) {
        char grp[16] = "";
        const char *a = code == 200 ? strstr(g_rbuf, "udio") : NULL;   /* the "Stream Audio to" item */
        if (a && (a = strchr(a, ':')) && (a = strchr(a, '"'))) {
            int k = 0;
            for (a++; *a && *a != '"' && *a != ':' && k < 15; a++) grp[k++] = *a;
            grp[k] = 0;
        }
        strcpy(g_group, is_multicast(grp) ? grp : MCAST_DEFAULT);
        g_cfg_tried = true;
        if (code == 403) strcpy(g_u64state, "Passwort falsch (403)");
    } else if (g_rq == RQ_UPLOAD) {
        if (code == 200)      strcpy(g_u64state, "SID wird gespielt");
        else if (code == 403) strcpy(g_u64state, "Passwort falsch (403)");
        else                  snprintf(g_u64state, sizeof g_u64state, "SID: Fehler %d", code);
        g_pending_sid = -1;                              /* g_body is free again */
    } else if (g_rq == RQ_PUT) {
        if (code == 200) strcpy(g_u64state, "OK");
        else if (code)   snprintf(g_u64state, sizeof g_u64state, "Fehler %d", code);
    } else if (g_rq == RQ_START) {
        if (code == 200)      strcpy(g_u64state, "Stream angefordert");
        else if (code == 403) strcpy(g_u64state, "Passwort falsch (403)");
        else if (code)        snprintf(g_u64state, sizeof g_u64state, "Fehler %d", code);
    }
    g_rq = RQ_NONE;
    rest_done(code);
}
static bool rest_close(struct tcp_pcb *pcb) {             /* true = the pcb had to be aborted (callers return ERR_ABRT) */
    tcp_arg(pcb, NULL); tcp_recv(pcb, NULL); tcp_err(pcb, NULL); tcp_sent(pcb, NULL); tcp_poll(pcb, NULL, 0);
    g_rest = NULL;
    if (tcp_close(pcb) == ERR_OK) return false;
    tcp_abort(pcb);
    return true;
}
static err_t rest_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)arg; (void)err;
    if (!p) { rest_finish(); return rest_close(pcb) ? ERR_ABRT : ERR_OK; }   /* the Ultimate closed: response complete */
    int room = (int)sizeof g_rbuf - 1 - g_rlen;
    if (room > 0) g_rlen += pbuf_copy_partial(p, g_rbuf + g_rlen, (u16_t)(p->tot_len < room ? p->tot_len : room), 0);
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}
static void rest_err(void *arg, err_t err) {
    (void)arg;
    DIAG("rest: Verbindungsfehler err=%d (rq=%d)", (int)err, g_rq);
    g_rest = NULL;                                            /* pcb is already freed by lwIP */
    if (g_rq == RQ_UPLOAD) g_pending_sid = -1;
    if (g_rq == RQ_CONFIG) { strcpy(g_group, MCAST_DEFAULT); g_cfg_tried = true; }
    if (g_rq != RQ_STOP) strcpy(g_u64state, "nicht erreichbar");
    g_rq = RQ_NONE;
    rest_done(0);
}
/* upload: header + multipart preamble + SID data + closing boundary, as fast as the send buffer allows */
static void upload_more(struct tcp_pcb *pcb) {
    static const char post[] = "\r\n--" SID_BOUNDARY "--\r\n";
    int data_len = g_pending_sid, total = g_up_pre_len + data_len + (int)sizeof post - 1;
    while (g_up_pos < total) {
        int room = tcp_sndbuf(pcb);
        if (room <= 0) break;
        const char *src; int left;
        if (g_up_pos < g_up_pre_len) { src = g_up_pre + g_up_pos; left = g_up_pre_len - g_up_pos; }
        else if (g_up_pos < g_up_pre_len + data_len) { src = g_body + (g_up_pos - g_up_pre_len); left = g_up_pre_len + data_len - g_up_pos; }
        else { src = post + (g_up_pos - g_up_pre_len - data_len); left = total - g_up_pos; }
        int k = left < room ? left : room;
        if (k > 1460) k = 1460;
        if (tcp_write(pcb, src, (u16_t)k, TCP_WRITE_FLAG_COPY) != ERR_OK) break;
        g_up_pos += k;
    }
    tcp_output(pcb);
}
static err_t rest_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    (void)arg; (void)len;
    if (g_rq == RQ_UPLOAD) upload_more(pcb);
    return ERR_OK;
}
static err_t rest_poll(void *arg, struct tcp_pcb *pcb) {   /* every second: give up after 10 s (upload 30 s) */
    (void)arg;
    int64_t age = absolute_time_diff_us(g_rest_t0, get_absolute_time());
    if (age < (g_rq == RQ_UPLOAD ? 30000000 : 10000000)) return ERR_OK;
    DIAG("rest: keine Antwort nach %d s (rq=%d) - abgebrochen", (int)(age / 1000000), g_rq);
    tcp_arg(pcb, NULL); tcp_recv(pcb, NULL); tcp_err(pcb, NULL); tcp_sent(pcb, NULL); tcp_poll(pcb, NULL, 0);
    tcp_abort(pcb);
    g_rest = NULL;
    if (g_rq == RQ_UPLOAD) g_pending_sid = -1;
    if (g_rq == RQ_CONFIG) { strcpy(g_group, MCAST_DEFAULT); g_cfg_tried = true; }
    if (g_rq != RQ_STOP) strcpy(g_u64state, "keine Antwort");
    g_rq = RQ_NONE;
    rest_done(0);
    return ERR_ABRT;
}
static err_t rest_connected(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)arg;
    if (err != ERR_OK) { bool ab = rest_close(pcb); if (g_rq != RQ_STOP) strcpy(g_u64state, "nicht erreichbar"); g_rq = RQ_NONE; rest_done(0); return ab ? ERR_ABRT : ERR_OK; }
    tcp_write(pcb, g_rest_req, strlen(g_rest_req), TCP_WRITE_FLAG_COPY);
    if (g_rq == RQ_UPLOAD) { tcp_sent(pcb, rest_sent); upload_more(pcb); }
    tcp_output(pcb);
    return ERR_OK;
}
/* called from net_task (main loop) */
static void rest_request(int kind) {
    ip_addr_t addr;
    const u64cfg_t *u = kind == RQ_STOP ? &g_stop_u64 : cur_u64();
    if (!ipaddr_aton(u->addr, &addr)) {
        strcpy(g_u64state, "IP-Adresse ungültig");
        g_cur_ticket = kind == RQ_PUT ? g_put_ticket : (kind == RQ_GET ? g_get_ticket : 0);
        rest_done(0); return;
    }
    char pw[64] = "";
    if (u->pw[0]) snprintf(pw, sizeof pw, "X-Password: %s\r\n", u->pw);
    if (kind == RQ_CONFIG)
        snprintf(g_rest_req, sizeof g_rest_req,
                 "GET /v1/configs/Data%%20Streams HTTP/1.1\r\nHost: %s\r\n%sConnection: close\r\n\r\n", u->addr, pw);
    else if (kind == RQ_PUT)
        snprintf(g_rest_req, sizeof g_rest_req,
                 "PUT %s HTTP/1.1\r\nHost: %s\r\n%sContent-Length: 0\r\nConnection: close\r\n\r\n", g_put_path, u->addr, pw);
    else if (kind == RQ_GET)
        snprintf(g_rest_req, sizeof g_rest_req,
                 "GET %s HTTP/1.1\r\nHost: %s\r\n%sConnection: close\r\n\r\n", g_get_path, u->addr, pw);
    else if (kind == RQ_UPLOAD) {
        g_up_pre_len = snprintf(g_up_pre, sizeof g_up_pre,
                 "--" SID_BOUNDARY "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"tune.sid\"\r\n"
                 "Content-Type: application/octet-stream\r\n\r\n");
        g_up_total = g_up_pre_len + g_pending_sid + (int)strlen("\r\n--" SID_BOUNDARY "--\r\n");
        g_up_pos = 0;
        char song[24] = "";
        if (g_sid_song > 0) snprintf(song, sizeof song, "?songnr=%d", g_sid_song);
        snprintf(g_rest_req, sizeof g_rest_req,
                 "POST /v1/runners:sidplay%s HTTP/1.1\r\nHost: %s\r\n%sContent-Type: multipart/form-data; boundary=" SID_BOUNDARY
                 "\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", song, u->addr, pw, g_up_total);
    }
    else if (kind == RQ_STOP)
        snprintf(g_rest_req, sizeof g_rest_req,
                 "PUT /v1/streams/audio:stop HTTP/1.1\r\nHost: %s\r\n%sContent-Length: 0\r\nConnection: close\r\n\r\n", u->addr, pw);
    else
        snprintf(g_rest_req, sizeof g_rest_req,
                 "PUT /v1/streams/audio:start?ip=%s:%d HTTP/1.1\r\nHost: %s\r\n%sContent-Length: 0\r\nConnection: close\r\n\r\n",
                 g_cfg.smode == SMODE_MULTICAST ? g_group : g_ip, AUDIO_PORT, u->addr, pw);
    { char *e = strchr(g_rest_req, '\r'); DIAG("rest: %.*s", e ? (int)(e - g_rest_req) : 40, g_rest_req); }
    cyw43_arch_lwip_begin();
    if (g_rest) { tcp_abort(g_rest); g_rest = NULL; }
    g_rlen = 0;
    g_rq = kind;
    g_cur_ticket = kind == RQ_PUT ? g_put_ticket : (kind == RQ_GET ? g_get_ticket : 0);
    g_rest = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (g_rest) {
        tcp_recv(g_rest, rest_recv);
        tcp_err(g_rest, rest_err);
        tcp_poll(g_rest, rest_poll, 2);                   /* 2 x 0.5 s */
        g_rest_t0 = get_absolute_time();
        if (tcp_connect(g_rest, &addr, 80, rest_connected) != ERR_OK) { tcp_abort(g_rest); g_rest = NULL; g_rq = RQ_NONE; rest_done(0); }
        else if (kind == RQ_START) strcpy(g_u64state, "verbinde ...");
    } else { g_rq = RQ_NONE; rest_done(0); }
    cyw43_arch_lwip_end();
}

int net_jb_progress(void) { return jb_progress(); }
char *net_body_buffer(void) { return g_body; }
const char *net_u64_ip(void) { return cur_u64()->addr; }
const char *net_u64_pw(void) { return cur_u64()->pw; }
void net_post_message(const char *m) { set_message(m); }
bool net_rest_idle(void) { return g_rq == RQ_NONE && !g_pending_put && !g_pending_get && !g_pending_cmd && !rest_blocked(); }
uint32_t net_rest_get(const char *pq) {
    strncpy(g_get_path, pq, sizeof g_get_path - 1); g_get_path[sizeof g_get_path - 1] = 0;
    g_get_ticket = ++g_ticket; g_pending_get = true; return g_get_ticket;
}
bool net_rest_done(uint32_t ticket) {
    for (int i = 0; i < DONE_N; i++) if (g_done_t[i] == ticket && ticket) { g_sel_ticket = ticket; return true; }
    return false;
}
/* result of the request net_rest_done() just confirmed: HTTP status (0 = no answer), *body = the answer after
   the header (only meaningful for a GET: its body is kept until it has been read here) */
int net_rest_result(const char **body) {
    int code = 0;
    for (int i = 0; i < DONE_N; i++) if (g_done_t[i] == g_sel_ticket) { code = g_done_c[i]; g_done_t[i] = 0; }
    const char *b = strstr(g_rbuf, "\r\n\r\n");
    if (body) *body = b ? b + 4 : "";
    if (g_get_unread == g_sel_ticket) g_get_unread = 0;
    return code;
}
uint32_t net_rest_put(const char *pq) {
    strncpy(g_put_path, pq, sizeof g_put_path - 1); g_put_path[sizeof g_put_path - 1] = 0;
    g_put_ticket = ++g_ticket; g_pending_put = true; return g_put_ticket;
}

/* multicast: join the group so the WLAN chip and lwIP let its packets through */
static void join_group(void) {
    ip4_addr_t grp;
    if (g_joined || !ip4addr_aton(g_group, &grp)) return;
    cyw43_arch_lwip_begin();
    g_joined = igmp_joingroup_netif(&cyw43_state.netif[CYW43_ITF_STA], &grp) == ERR_OK;
    cyw43_arch_lwip_end();
}

/* ------------------------------------------------------------ web server */
#define REQ_MAX 2560
typedef struct {
    char  req[REQ_MAX];
    int   req_len, content_len, body_len;
    bool  header_done, is_post, body_to_g, done;
    char  path[400];                                           /* with query string (jukebox paths can be long) */
    char  auth[900];                                           /* value of the Authorization header */
    char  method[8];
    char  hdr[320];
    char  dyn[768];                                            /* small dynamic bodies (status JSON, replies) */
    const char *tx_body;
    int   tx_hdr_len, tx_body_len, tx_pos;                     /* bytes of header+body already queued */
    char *own;                                                 /* body allocated for this answer (freed with the connection) */
    int   poll_pos;                                            /* tx_pos at the last poll (progress check) */
} conn_t;

static int g_conns = 0;

static void *g_body_owner = NULL;                        /* the connection whose POST body is going into g_body */
static bool conn_close(struct tcp_pcb *pcb, conn_t *c) {  /* true = aborted: the lwIP callback must return ERR_ABRT */
    tcp_arg(pcb, NULL); tcp_recv(pcb, NULL); tcp_sent(pcb, NULL); tcp_err(pcb, NULL); tcp_poll(pcb, NULL, 0);
    if (c) { if (g_body_owner == c) g_body_owner = NULL; free(c->own); free(c); g_conns--; }
    if (tcp_close(pcb) == ERR_OK) return false;
    tcp_abort(pcb);
    return true;
}

static void send_more(struct tcp_pcb *pcb, conn_t *c) {
    int total = c->tx_hdr_len + c->tx_body_len;
    while (c->tx_pos < total) {
        int room = tcp_sndbuf(pcb);
        if (room <= 0) break;
        const char *src; int left;
        if (c->tx_pos < c->tx_hdr_len) { src = c->hdr + c->tx_pos; left = c->tx_hdr_len - c->tx_pos; }
        else { src = c->tx_body + (c->tx_pos - c->tx_hdr_len); left = total - c->tx_pos; }
        int n = left < room ? left : room;
        if (n > 1460) n = 1460;
        if (tcp_write(pcb, src, n, TCP_WRITE_FLAG_COPY | (c->tx_pos + n < total ? TCP_WRITE_FLAG_MORE : 0)) != ERR_OK) break;
        c->tx_pos += n;
    }
    tcp_output(pcb);
}

static void respond(struct tcp_pcb *pcb, conn_t *c, int code, const char *type, const char *body, int len) {
    c->tx_hdr_len = snprintf(c->hdr, sizeof c->hdr,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
        code, code == 200 ? "OK" : (code == 404 ? "Not Found" : "Error"), type, len);
    c->tx_body = body; c->tx_body_len = len; c->tx_pos = 0; c->done = true;
    send_more(pcb, c);
}
static void reply_text(struct tcp_pcb *pcb, conn_t *c, int code, const char *t) {
    strncpy(c->dyn, t, sizeof c->dyn - 1);
    c->dyn[sizeof c->dyn - 1] = 0;
    respond(pcb, c, code, "text/plain; charset=utf-8", c->dyn, (int)strlen(c->dyn));
}

/* text for a JSON string: " and \ escaped, control characters dropped */
static void jesc(char *o, int n, const char *s) {
    int k = 0;
    for (; *s && k < n - 2; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch < 32) continue;
        if (ch == '"' || ch == '\\') { if (k >= n - 3) break; o[k++] = '\\'; }
        o[k++] = (char)ch;
    }
    o[k] = 0;
}

/* x-www-form-urlencoded: copy the value of key into out */
/* backup: binary blocks as hex text */
static int hex_out(char *o, const void *d, int n) {
    static const char hx[] = "0123456789abcdef";
    const uint8_t *b = (const uint8_t *)d;
    for (int i = 0; i < n; i++) { o[2 * i] = hx[b[i] >> 4]; o[2 * i + 1] = hx[b[i] & 15]; }
    return 2 * n;
}
static int hex_val(char c) { return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1)); }
static int hex_in(uint8_t *o, int max, const char *h, int len) {   /* bytes, -1 = not hex / too long */
    if (len & 1 || len / 2 > max) return -1;
    for (int i = 0; i < len / 2; i++) {
        int a = hex_val(h[2 * i]), b = hex_val(h[2 * i + 1]);
        if (a < 0 || b < 0) return -1;
        o[i] = (uint8_t)(a << 4 | b);
    }
    return len / 2;
}
static const char *form_ptr(const char *body, const char *key, int *len) {   /* raw value (no decoding), NULL = missing */
    size_t kl = strlen(key);
    for (const char *p = body; p && *p; p = strchr(p, '&'), p = p ? p + 1 : NULL)
        if (!strncmp(p, key, kl) && p[kl] == '=') { p += kl + 1; const char *e = strchr(p, '&'); *len = e ? (int)(e - p) : (int)strlen(p); return p; }
    return NULL;
}
static bool form_has(const char *body, const char *key) {   /* the form carries this field (maybe empty) */
    size_t kl = strlen(key);
    for (const char *p = body; p && *p; p = strchr(p, '&'), p = p ? p + 1 : NULL)
        if (!strncmp(p, key, kl) && p[kl] == '=') return true;
    return false;
}
static void form_value(const char *body, const char *key, char *out, int outlen) {
    out[0] = 0;
    size_t kl = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            p += kl + 1;
            int n = 0;
            while (*p && *p != '&' && n < outlen - 1) {
                if (*p == '+') { out[n++] = ' '; p++; }
                else if (*p == '%' && p[1] && p[2]) { char h[3] = { p[1], p[2], 0 }; out[n++] = (char)strtol(h, NULL, 16); p += 3; }
                else out[n++] = *p++;
            }
            out[n] = 0;
            return;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
}

/* ---- HTTP Digest authentication (RFC 7616, MD5, qop=auth) ----
   The password never travels over the network: the browser sends a hash over password,
   a server nonce, a counter and its own nonce. Nonces live 1 hour; every counter value
   may be used once (replay protection). The user name is not checked. */
#define REALM      "C64 Visualizer"
#define NONCES     6
#define NONCE_LIFE_US (3600ull * 1000000ull)
typedef struct { char n[33]; uint64_t born; uint32_t max_nc, seen; bool used; } nonce_t;
static nonce_t g_nonce[NONCES];
static int g_nonce_next = 0;

static const char *new_nonce(void) {
    nonce_t *e = &g_nonce[g_nonce_next];
    g_nonce_next = (g_nonce_next + 1) % NONCES;
    snprintf(e->n, sizeof e->n, "%08lx%08lx%08lx%08lx", (unsigned long)get_rand_32(), (unsigned long)get_rand_32(),
             (unsigned long)get_rand_32(), (unsigned long)time_us_32());
    e->born = time_us_64(); e->max_nc = 0; e->seen = 0; e->used = true;
    return e->n;
}

/* copy the value of key (quoted or not) from a Digest header */
static bool digest_param(const char *h, const char *key, char *out, int outlen) {
    size_t kl = strlen(key);
    for (const char *p = h; (p = strstr(p, key)) != NULL; p += kl) {
        if (p != h && p[-1] != ' ' && p[-1] != ',') continue;  /* "nonce" must not match "cnonce" */
        const char *v = p + kl;
        while (*v == ' ') v++;
        if (*v != '=') continue;
        v++;
        while (*v == ' ') v++;
        bool q = *v == '"';
        if (q) v++;
        int n = 0;
        while (*v && n < outlen - 1 && (q ? *v != '"' : (*v != ',' && *v != ' ' && *v != '\r'))) out[n++] = *v++;
        out[n] = 0;
        return true;
    }
    return false;
}

/* 1 = ok, 0 = wrong / missing, -1 = right password but unknown or expired nonce (stale) */
static int auth_check(const conn_t *c) {
    if (strncmp(c->auth, "Digest ", 7)) return 0;
    const char *h = c->auth + 7;
    char user[64], realm[40], nonce[40], uri[400], resp[40], qop[16], nc[12], cnonce[64];
    if (!digest_param(h, "username", user, sizeof user) || !digest_param(h, "realm", realm, sizeof realm) ||
        !digest_param(h, "nonce", nonce, sizeof nonce) || !digest_param(h, "uri", uri, sizeof uri) ||
        !digest_param(h, "response", resp, sizeof resp) || !digest_param(h, "qop", qop, sizeof qop) ||
        !digest_param(h, "nc", nc, sizeof nc) || !digest_param(h, "cnonce", cnonce, sizeof cnonce)) return 0;
    if (strcmp(realm, REALM) || strcmp(qop, "auth") || strcmp(uri, c->path)) return 0;
    char buf[520], ha1[33], ha2[33], expect[33];
    snprintf(buf, sizeof buf, "%s:%s:%s", user, REALM, web_password());
    md5_hex(buf, strlen(buf), ha1);
    snprintf(buf, sizeof buf, "%s:%s", c->method, uri);
    md5_hex(buf, strlen(buf), ha2);
    snprintf(buf, sizeof buf, "%s:%s:%s:%s:%s:%s", ha1, nonce, nc, cnonce, qop, ha2);
    md5_hex(buf, strlen(buf), expect);
    if (strcmp(expect, resp)) return 0;                   /* wrong password */
    for (int i = 0; i < NONCES; i++) {
        nonce_t *e = &g_nonce[i];
        if (!e->used || strcmp(e->n, nonce)) continue;
        if (time_us_64() - e->born > NONCE_LIFE_US) return -1;
        uint32_t v = (uint32_t)strtoul(nc, NULL, 16);
        if (v > e->max_nc) {                              /* new highest counter: slide the window */
            uint32_t shift = v - e->max_nc;
            e->seen = shift >= 32 ? 0 : e->seen << shift;
            e->seen |= 1u;
            e->max_nc = v;
            return 1;
        }
        uint32_t back = e->max_nc - v;                    /* parallel requests may arrive out of order */
        if (back < 32 && !(e->seen & (1u << back))) { e->seen |= 1u << back; return 1; }
        return 0;                                         /* counter used before: replay */
    }
    return -1;                                            /* nonce unknown (e.g. after a restart) */
}

/* the firmware file this device needs for an update (tools/update_all.py picks it by this name) */
#define IMG_STR2(x) #x
#define IMG_STR(x) IMG_STR2(x)
#ifdef DEBUG_USB
#define IMG_DEBUG "_debug"
#else
#define IMG_DEBUG ""
#endif
#define IMG_NAME "c64viz_wifi_ws2812_" IMG_POWER "-" IMG_STR(POWER_LIMIT_MA) "mA" IMG_DEBUG
#if POWER_LIMIT_MA >= 1000
#define IMG_POWER "psu"
#else
#define IMG_POWER "usb"
#endif

static void ip_str(char *o, int n, uint32_t a) {
    if (!a) { o[0] = 0; return; }
    snprintf(o, (size_t)n, "%lu.%lu.%lu.%lu", (unsigned long)(a & 255), (unsigned long)((a >> 8) & 255),
             (unsigned long)((a >> 16) & 255), (unsigned long)(a >> 24));
}

/* the beacon of this Pico (devices.c): who it is and how the stream flows through it */
int net_self_json(char *o, int n) {
    static char e_fwd[70], e_src[70], e_lbl[40], from[16], fwip[16], wide[120];
    wide_json(wide, sizeof wide);
    jesc(e_fwd, sizeof e_fwd, g_cfg.fwd); jesc(e_src, sizeof e_src, cur_u64()->addr); jesc(e_lbl, sizeof e_lbl, cur_u64()->label);
    ip_str(from, sizeof from, net_audio_active() ? g_lock_ip : 0);
    ip_str(fwip, sizeof fwip, g_fwd_ip);
    return snprintf(o, (size_t)n,
        "{\"t\":\"viz\",\"n\":\"%s\",\"ip\":\"%s\",\"b\":\"%s\",\"img\":\"" IMG_NAME "\",\"up\":%lu,"
        "\"st\":%d,\"sm\":%d,\"rx\":%lu,\"from\":\"%s\",\"fw\":\"%s\",\"fwip\":\"%s\",\"fp\":%lu,"
        "\"src\":\"%s\",\"srcl\":\"%s\",%s}",
        mdnsl_name(), g_ip, ota_build(), (unsigned long)(to_ms_since_boot(get_absolute_time()) / 1000),
        g_cfg.stream ? 1 : 0, g_cfg.smode, (unsigned long)g_pps, from, e_fwd, fwip, (unsigned long)g_fpps,
        e_src, e_lbl, wide);
}

static void respond_401(struct tcp_pcb *pcb, conn_t *c, bool stale) {
    static const char body[] = "Passwort erforderlich (Benutzername beliebig).";
    c->tx_hdr_len = snprintf(c->hdr, sizeof c->hdr,
        "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Digest realm=\"" REALM "\", qop=\"auth\", algorithm=MD5, "
        "nonce=\"%s\"%s\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
        new_nonce(), stale ? ", stale=true" : "", (int)sizeof body - 1);
    c->tx_body = body; c->tx_body_len = (int)sizeof body - 1; c->tx_pos = 0; c->done = true;
    send_more(pcb, c);
}

static void handle(struct tcp_pcb *pcb, conn_t *c) {
    /* in the home network the page needs the password; on the Pico's own access point the
       WLAN password already protects it (and that is the way back if the password is forgotten) */
    g_web_ms = to_ms_since_boot(get_absolute_time()) | 1;
    if (g_state != NS_AP) {                               /* (also while reconnecting to the home WLAN) */
        int a = auth_check(c);
        if (a != 1) { respond_401(pcb, c, a < 0); return; }
    }
    if (!c->is_post && !strcmp(c->path, "/")) {
        respond(pcb, c, 200, "text/html; charset=utf-8", WEB_PAGE, (int)sizeof WEB_PAGE - 1);
    } else if (!c->is_post && !strcmp(c->path, "/hilfe")) {
        /* the manual (docs/HANDBUCH.md) for the "Hilfe" tab, stored gzip-compressed: 15 KB instead of 45 */
        respond(pcb, c, 200, "text/html; charset=utf-8\r\nContent-Encoding: gzip", (const char *)HELP_GZ, HELP_GZ_LEN);
    } else if (!c->is_post && !strcmp(c->path, "/devices")) {
        /* the C64 devices in the network (beacons, devices.c) - a copy only for as long as it is being sent */
        int max = 12288;
        while (!(c->own = malloc((size_t)max)) && max > 2048) max /= 2;
        int n = c->own ? dev_json(c->own, max) : 0;
        respond(pcb, c, c->own ? 200 : 503, "application/json", c->own ? c->own : "", n);
    } else if (c->is_post && !strcmp(c->path, "/devices/query")) {
        dev_query();
        reply_text(pcb, c, 200, "OK");
    } else if (!c->is_post && !strcmp(c->path, "/texts")) {
        respond(pcb, c, 200, "text/plain; charset=utf-8", g_texts, (int)strlen(g_texts));
    } else if (!c->is_post && !strcmp(c->path, "/diag")) {
        /* a copy of the ring, only for as long as this answer is being sent */
        int max = 12288;
        while (!(c->own = malloc((size_t)max)) && max > 1024) max /= 2;
        int n = c->own ? diag_snapshot(c->own, max) : 0;
        respond(pcb, c, c->own ? 200 : 503, "text/plain; charset=utf-8", c->own ? c->own : "", n);
    } else if (!c->is_post && !strcmp(c->path, "/status")) {
        /* static buffers: the web routes run one at a time on the 2 KB main stack (lwIP interrupt) */
        static char ap_text[56]; snprintf(ap_text, sizeof ap_text, "eigener Zugangspunkt %s", g_ap_ssid);
        /* user text (SSID, address) JSON-escaped: a " or \ in the WLAN name broke the whole status display */
        static char e_wifi[120], e_ssid[70], e_u64[130], e_state[100], fwdip[16];
        strcpy(fwdip, "-");
        jesc(e_wifi, sizeof e_wifi, g_state == NS_STA ? g_cfg.ssid : (g_state == NS_AP ? ap_text : "verbinde ..."));
        jesc(e_ssid, sizeof e_ssid, g_cfg.ssid); jesc(e_u64, sizeof e_u64, cur_u64()->addr); jesc(e_state, sizeof e_state, g_u64state);
        if (g_fwd_ip) { uint32_t f = g_fwd_ip; snprintf(fwdip, sizeof fwdip, "%lu.%lu.%lu.%lu", (unsigned long)(f & 255),
                                  (unsigned long)((f >> 8) & 255), (unsigned long)((f >> 16) & 255), (unsigned long)(f >> 24)); }
        static char e_list[700]; int kl = 0;                      /* the Ultimates: name, address, password set */
        for (int i = 0; i < U64_MAX; i++) {
            static char el[40], ea[130];
            jesc(el, sizeof el, g_cfg.u64[i].label); jesc(ea, sizeof ea, g_cfg.u64[i].addr);
            kl += snprintf(e_list + kl, sizeof e_list - (size_t)kl, "%s{\"label\":\"%s\",\"addr\":\"%s\",\"pw\":%s}",
                           i ? "," : "", el, ea, g_cfg.u64[i].pw[0] ? "true" : "false");
        }
        static char wide[120]; wide_json(wide, sizeof wide);  /* two matrices: role, partner, working */
        int cap = 2560;
        char *buf = c->own = malloc((size_t)cap);              /* freed with the connection */
        if (!buf) { buf = c->dyn; cap = (int)sizeof c->dyn; }
        int n = snprintf(buf, (size_t)cap,
            "{\"wifi\":\"%s\",\"ip\":\"%s\",\"net\":%s,\"pps\":%lu,\"u64state\":\"%s\",\"ssid\":\"%s\",\"u64\":\"%s\",\"stream\":%s,\"thr\":%d,\"pwdefault\":%s,\"sta\":%s,\"smode\":%d,\"group\":\"%s\",\"silent\":%lu,\"sidbusy\":%s,\"mcwarn\":%s,\"build\":\"%s\",\"load\":%d,\"busymax\":%lu,\"framemax\":%lu,\"stallmax\":%lu,\"stereo\":\"%s\",\"waits\":%lu,\"lux10\":%d,\"lightpct\":%d,\"name\":\"%s\",\"cfgname\":\"%s\",\"bright\":%d,\"fwd\":\"%s\",\"fwdip\":\"%s\",\"fpps\":%lu,\"fwderr\":%lu,\"src\":%d,\"u64list\":[%s],\"news\":%d,\"scanning\":%s,%s}",
            e_wifi, g_ip, net_audio_active() ? "true" : "false", (unsigned long)g_pps, e_state,
            e_ssid, e_u64, g_cfg.stream ? "true" : "false", g_thr_show,
            g_cfg.webpw[0] ? "false" : "true", g_state == NS_STA ? "true" : "false",
            g_cfg.smode, g_group[0] ? g_group : "-", (unsigned long)g_silent_show, g_pending_sid >= 0 ? "true" : "false",
            g_mcwarn ? "true" : "false", ota_build(), g_load, (unsigned long)g_busy_max, (unsigned long)g_frame_max,
            (unsigned long)g_stall_max, g_stereo_st < 0 ? "" : stereo_name((stereo_state_t)g_stereo_st),
            (unsigned long)audio_stream_waits(), g_lux10, g_light_pct,
            g_state == NS_STA ? mdnsl_name() : base_name(), g_cfg.name, g_bright,
            g_cfg.fwd, fwdip, (unsigned long)g_fpps, (unsigned long)g_fwd_err, g_cfg.src, e_list, news_count(),
            u64scan_running() ? "true" : "false", wide);
        if (n >= cap) n = cap - 1;
        respond(pcb, c, 200, "application/json", buf, n);
    } else if (c->is_post && !strncmp(c->path, "/cmd", 4)) {
        const char *q = strstr(c->path, "c=");
        if (q) {
            q += 2;
            if (!strcmp(q, "next")) g_cmd = NETCMD_NEXT;
            else if (!strcmp(q, "camera")) g_cmd = NETCMD_CAMERA;
            else if (!strcmp(q, "bright")) g_cmd = NETCMD_BRIGHT;
            else if (!strncmp(q, "bright&p=", 9)) {           /* percent from the slider */
                int p = atoi(q + 9);
                if (q[9] >= '0' && q[9] <= '9' && p >= BRIGHT_MIN_PCT && p <= 100) g_cmd = NETCMD_BRIGHT_PCT + p;
                else { reply_text(pcb, c, 400, "Helligkeit: 4 bis 100 %."); return; }
            }
            else if (!strcmp(q, "auto")) g_cmd = NETCMD_AUTO;
            else if (!strcmp(q, "calibrate")) g_cmd = NETCMD_CALIBRATE;
            else if (!strcmp(q, "u64reset"))  { strcpy(g_cmd_path, "/v1/machine:reset");  g_pending_cmd = true; jb_sidaddr_release(); jb_stereo_hint(0); }
            else if (!strcmp(q, "u64pause"))  { strcpy(g_cmd_path, "/v1/machine:pause");  g_pending_cmd = true; }
            else if (!strcmp(q, "u64resume")) { strcpy(g_cmd_path, "/v1/machine:resume"); g_pending_cmd = true; }
        }
        reply_text(pcb, c, 200, "OK");
    } else if (c->is_post && !strcmp(c->path, "/news")) {          /* retro news from the Pi jukebox */
        int n = c->body_to_g ? c->body_len : 0;
        if (c->content_len > 760) reply_text(pcb, c, 413, "Höchstens 760 Byte.");
        else if ((c->content_len && !c->body_to_g) || !news_put(g_body, n))
            reply_text(pcb, c, 409, "busy");                       /* the Pi sends again */
        else { relay_news(g_body, n); reply_text(pcb, c, 200, "OK"); }
    } else if (c->is_post && !strcmp(c->path, "/texts")) {
        if (!c->body_to_g) reply_text(pcb, c, 400, "Text zu groß oder leer.");
        else { g_pending_texts = c->body_len; reply_text(pcb, c, 200, "Gespeichert - der Pico startet neu."); }
    } else if (c->is_post && !strncmp(c->path, "/sid", 4)) {
        /* SID jukebox: the browser sends one SID file; it goes to the Ultimate from net_task() */
        if (!c->body_to_g && c->content_len > 0 && c->content_len < (int)sizeof g_body - 32) {
            reply_text(pcb, c, 409, "Beschäftigt - die vorige SID wird noch übertragen."); return;   /* the page retries */
        }
        if (!c->body_to_g || c->body_len < 0x7C || (memcmp(g_body, "PSID", 4) && memcmp(g_body, "RSID", 4))) {
            reply_text(pcb, c, 400, "Keine gültige SID-Datei."); return;
        }
        if (!cur_u64()->addr[0]) { reply_text(pcb, c, 400, "Keine IP-Adresse des Ultimate eingetragen."); return; }
        const char *s = strstr(c->path, "song=");
        g_sid_song = s ? atoi(s + 5) : 0;
        /* title and author from the SID header (32 bytes each at 0x16 and 0x36) for the matrix */
        char title[33], author[33];
        memcpy(title, g_body + 0x16, 32); title[32] = 0;
        memcpy(author, g_body + 0x36, 32); author[32] = 0;
        snprintf(g_sid_title, sizeof g_sid_title, "{color:FFD000}{speed:35}%s {color:00C0FF}%s", title, author);
        for (char *p = g_sid_title; *p; p++) {                 /* characters the font doesn't know -> space */
            unsigned char ch = (unsigned char)*p;
            if (*p == '{') { while (*p && *p != '}') p++; if (!*p) break; continue; }
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || strchr(" !.-:", ch))) *p = ' ';
        }
        { char *d = g_sid_title; bool sp = false;                /* collapse runs of spaces */
          for (char *s2 = g_sid_title; *s2; s2++) { if (*s2 == ' ' && sp) continue; sp = *s2 == ' '; *d++ = *s2; } *d = 0; }
        g_pending_sid = c->body_len;
        jb_sidaddr_need((const uint8_t *)g_body, c->body_len);   /* switches a socket first if the tune needs it */
        jb_np_cancel();                                   /* the web page plays now, not the PC jukebox */
        { uint8_t d[16]; md5_ctx_t m; md5_init(&m); md5_update(&m, g_body, (size_t)c->body_len); md5_final(&m, d);
          int song = g_sid_song > 0 ? g_sid_song : ((uint8_t)g_body[0x10] << 8 | (uint8_t)g_body[0x11]);
          int secs = sldb_lookup(d, song > 0 ? song : 1, NULL);
          if (secs > 0) {                                  /* playing time known: show it with the title */
              size_t l = strlen(g_sid_title);
              snprintf(g_sid_title + l, sizeof g_sid_title - l, " {color:FFFFFF}%d:%02d", secs / 60, secs % 60);
          }
          char hx[33]; for (int i = 0; i < 16; i++) snprintf(hx + 2 * i, 3, "%02x", d[i]);
          int n = snprintf(c->dyn, sizeof c->dyn, "{\"ok\":true,\"secs\":%d,\"md5\":\"%s\"}", secs, hx);
          respond(pcb, c, 200, "application/json", c->dyn, n); }
    } else if (!c->is_post && !strcmp(c->path, "/backup")) {
        /* settings as a file (the web page adds texts.txt): WLAN, web password, Ultimates, forwarding (netcfg),
           jukebox settings, button settings (brightness ...). Not the song length database - it is read from its files. */
        uint8_t jp[512]; int jn = jb_prefs_backup(jp, (int)sizeof jp);
        int cap = 2 * ((int)sizeof(netcfg_t) + jn + FLASH_SECTOR_SIZE) + 300;
        char *b = c->own = malloc((size_t)cap);
        if (!b) { reply_text(pcb, c, 503, "Speicher knapp - bitte gleich nochmal."); return; }
        static netcfg_t w; w = g_cfg; w.magic = NETCFG_MAGIC; w.check = cfg_check(&w);
        int k = snprintf(b, (size_t)cap, "{\"fmt\":\"c64viz-backup\",\"ver\":1,\"build\":\"%s\",\"name\":\"%s\",\"netcfg\":\"", ota_build(), base_name());
        k += hex_out(b + k, &w, (int)sizeof w);
        k += snprintf(b + k, (size_t)(cap - k), "\",\"jbprefs\":\"");
        k += hex_out(b + k, jp, jn);
        k += snprintf(b + k, (size_t)(cap - k), "\",\"settings\":\"");
        k += hex_out(b + k, (const void *)(XIP_BASE + SETTINGS_FLASH_OFFSET), FLASH_SECTOR_SIZE);
        k += snprintf(b + k, (size_t)(cap - k), "\"}");
        respond(pcb, c, 200, "application/json", b, k);
    } else if (c->is_post && !strcmp(c->path, "/restore")) {
        /* everything is checked before anything is written: a broken or foreign file changes nothing */
        if (!c->body_to_g) { reply_text(pcb, c, 400, "Ungültige Daten."); return; }
        g_body[c->body_len] = 0;
        int ln = 0, lj = 0, ls = 0;
        const char *hn = form_ptr(g_body, "netcfg", &ln), *hj = form_ptr(g_body, "jbprefs", &lj), *hs = form_ptr(g_body, "settings", &ls);
        static netcfg_t n; static uint8_t jbp[512];        /* static: off the 2 KB stack */
        bool ok = hn && hex_in((uint8_t *)&n, (int)sizeof n, hn, ln) == (int)sizeof n && n.magic == NETCFG_MAGIC && n.check == cfg_check(&n);
        if (!ok) { reply_text(pcb, c, 400, "Die Sicherung passt nicht (WLAN-/Ultimate-Einstellungen fehlen oder sind beschädigt)."); return; }
        if (hj) { int jn = hex_in(jbp, (int)sizeof jbp, hj, lj); if (jn < 0 || !jb_prefs_restore(jbp, jn, false)) { reply_text(pcb, c, 400, "Die Sicherung passt nicht (Jukebox-Einstellungen beschädigt)."); return; } }
        bool hs_ok = hs && ls == 2 * FLASH_SECTOR_SIZE;
        for (int i = 0; hs_ok && i < ls; i++) hs_ok = hex_val(hs[i]) >= 0;
        if (hs && !hs_ok) { reply_text(pcb, c, 400, "Die Sicherung passt nicht (Taster-Einstellungen beschädigt)."); return; }
        g_pending_restore = true;
        reply_text(pcb, c, 200, "Einstellungen wiederhergestellt - der Pico startet gleich neu.");
    } else if (c->is_post && !strncmp(c->path, "/src?i=", 7)) {      /* another Ultimate as the source */
        int i = atoi(c->path + 7);
        if (c->path[7] < '0' || c->path[7] > '9' || i >= U64_MAX || !g_cfg.u64[i].addr[0]) { reply_text(pcb, c, 400, "Dieses Ultimate ist nicht eingetragen."); return; }
        if (!jb_idle()) { reply_text(pcb, c, 409, "Die Jukebox spielt gerade (oder stellt SID-Adressen zurück) - erst stoppen."); return; }
        g_pending_src = i;
        reply_text(pcb, c, 200, "Quelle wird gewechselt.");
    } else if (!strcmp(c->path, "/u64/scan")) {               /* search the home network for Ultimates */
        if (c->is_post) {
            if (g_state != NS_STA) { reply_text(pcb, c, 409, "Die Suche geht nur im Heim-WLAN."); return; }
            u64scan_start(ip4_addr_get_u32(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA])));   /* only sets variables - the main loop works */
        }
        char *b = c->own = malloc(1600);                    /* up to 8 devices (the small per-connection buffer holds ~4) */
        if (!b) { reply_text(pcb, c, 503, "Speicher knapp - bitte gleich nochmal."); return; }
        int n = u64scan_json(b, 1600);
        respond(pcb, c, 200, "application/json", b, n);
    } else if (c->is_post && !strcmp(c->path, "/config")) {
        if (!c->body_to_g) { reply_text(pcb, c, 400, "Ungültige Daten."); return; }
        g_body[c->body_len] = 0;
        static netcfg_t n; n = g_cfg;                       /* static: the web routes run on the 2 KB main stack (lwIP interrupt) */
        char v[72];
        form_value(g_body, "ssid", n.ssid, sizeof n.ssid);
        form_value(g_body, "pass", v, sizeof v);  if (v[0]) { strncpy(n.pass, v, sizeof n.pass - 1); }
        if (form_has(g_body, "a0")) {                       /* the list of Ultimates: l<i> name, a<i> IP, p<i> password */
            for (int i = 0; i < U64_MAX; i++) {
                char k[4] = { 'a', (char)('0' + i), 0 };
                u64cfg_t *u = &n.u64[i];
                form_value(g_body, k, v, sizeof v);
                char *t = v; while (*t == ' ') t++;
                for (char *e = t + strlen(t); e > t && e[-1] == ' '; ) *--e = 0;
                ip_addr_t chk;
                if (t[0] && (strlen(t) >= sizeof u->addr || !ipaddr_aton(t, &chk))) {
                    char m[80]; snprintf(m, sizeof m, "Ultimate %d: bitte eine IP-Adresse eintragen (z. B. 192.168.1.64).", i + 1);
                    reply_text(pcb, c, 400, m); return;
                }
                memset(u->addr, 0, sizeof u->addr); strcpy(u->addr, t);
                bool empty = !u->addr[0];                        /* (t points into v, which is used again below) */
                k[0] = 'l'; form_value(g_body, k, v, sizeof v);
                memset(u->label, 0, sizeof u->label); snprintf(u->label, sizeof u->label, "%s", v);
                k[0] = 'p'; form_value(g_body, k, v, sizeof v);          /* empty = unchanged */
                if (v[0]) { memset(u->pw, 0, sizeof u->pw); snprintf(u->pw, sizeof u->pw, "%s", v); }
                char kx[5] = { 'x', 'p', (char)('0' + i), 0 };
                form_value(g_body, kx, v, sizeof v);
                if (v[0] == '1' || empty) memset(u->pw, 0, sizeof u->pw);   /* "remove password", or slot emptied */
            }
            if (form_has(g_body, "src")) { form_value(g_body, "src", v, sizeof v); if (v[0] >= '0' && v[0] < '0' + U64_MAX) n.src = (uint8_t)(v[0] - '0'); }
        } else if (form_has(g_body, "u64")) {              /* older page / tools: one address = the source in use */
            u64cfg_t *u = &n.u64[n.src < U64_MAX ? n.src : 0];
            form_value(g_body, "u64", u->addr, sizeof u->addr);
            form_value(g_body, "u64pw", v, sizeof v); if (v[0]) { memset(u->pw, 0, sizeof u->pw); snprintf(u->pw, sizeof u->pw, "%s", v); }
        }
        form_value(g_body, "stream", v, sizeof v); n.stream = v[0] == '1';
        form_value(g_body, "smode", v, sizeof v);  n.smode = v[0] >= '0' && v[0] <= '2' ? (uint8_t)(v[0] - '0') : SMODE_UNICAST;
        if (strstr(g_body, "fwd=")) {                        /* forward to: IP or name, "" = off (older page: kept) */
            form_value(g_body, "fwd", v, sizeof v);
            char *t = v; while (*t == ' ') t++;
            for (char *e = t + strlen(t); e > t && e[-1] == ' '; ) *--e = 0;
            bool ok = strlen(t) < sizeof n.fwd;
            for (char *q = t; *q && ok; q++) {
                if (*q >= 'A' && *q <= 'Z') *q += 32;
                ok = (*q >= 'a' && *q <= 'z') || (*q >= '0' && *q <= '9') || *q == '.' || *q == '-';
            }
            if (!ok) { reply_text(pcb, c, 400, "Weiterleiten an: IP-Adresse oder Name (z. B. c64viz-2.local), nur a-z, 0-9, Punkt und Bindestrich."); return; }
            memset(n.fwd, 0, sizeof n.fwd); strcpy(n.fwd, t);
        }
        if (strstr(g_body, "name=")) {                       /* device name: lower case; empty = c64viz */
            form_value(g_body, "name", v, sizeof v);
            for (char *p = v; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
            if (v[0] && (!mdnsl_name_valid(v) || !strcmp(v, "c64viz"))) {
                if (!mdnsl_name_valid(v)) { reply_text(pcb, c, 400, "Gerätename: 1 bis 16 Zeichen, nur a-z, 0-9 und Bindestrich (nicht am Anfang oder Ende)."); return; }
                v[0] = 0;                                    /* "c64viz" is the default anyway */
            }
            memset(n.name, 0, sizeof n.name); strcpy(n.name, v);
        }
        form_value(g_body, "webpw", v, sizeof v);
        if (v[0]) {
            if (strlen(v) < 6 || strlen(v) > 32) { reply_text(pcb, c, 400, "Das Passwort für die Webseite muss 6 bis 32 Zeichen lang sein."); return; }
            strncpy(n.webpw, v, sizeof n.webpw - 1);
        }
        g_newcfg = n;
        g_pending_cfg = true;
        reply_text(pcb, c, 200, "Gespeichert - der Pico startet neu und verbindet sich.");
    } else {
        const char *o, *type; int olen;
        int code = ota_route(c->is_post, c->path, c->body_to_g ? g_body : NULL, c->body_len, &o, &olen, &type);
        if (!code) code = jb_route(c->is_post, c->path, c->body_to_g ? g_body : NULL, c->body_len, &o, &olen, &type);
        if (code) {
            /* the routes answer from one shared buffer; a long answer is sent over several callbacks, so a second
               request (another tab) would overwrite it half-way - every connection sends its own copy */
            char *copy = olen <= (int)sizeof c->dyn ? c->dyn : (c->own = malloc((size_t)olen));
            if (!copy) { reply_text(pcb, c, 503, "Speicher knapp - bitte gleich nochmal."); return; }
            memcpy(copy, o, (size_t)olen);
            respond(pcb, c, code, type, copy, olen);
        }
        else reply_text(pcb, c, 404, "Nicht gefunden");
    }
}

static err_t http_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    (void)len;
    conn_t *c = (conn_t *)arg;
    if (!c) return ERR_OK;
    send_more(pcb, c);
    if (c->done && c->tx_pos >= c->tx_hdr_len + c->tx_body_len && tcp_sndqueuelen(pcb) == 0) return conn_close(pcb, c) ? ERR_ABRT : ERR_OK;
    return ERR_OK;
}

static err_t http_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)err;
    conn_t *c = (conn_t *)arg;
    if (!p) return conn_close(pcb, c) ? ERR_ABRT : ERR_OK;
    tcp_recved(pcb, p->tot_len);
    if (!c || c->done) { pbuf_free(p); return ERR_OK; }
    for (struct pbuf *q = p; q; q = q->next) {
        const char *d = (const char *)q->payload;
        for (int i = 0; i < q->len; i++) {
            if (!c->header_done) {
                if (c->req_len < REQ_MAX - 1) c->req[c->req_len++] = d[i];
                c->req[c->req_len] = 0;
                if (c->req_len >= 4 && !memcmp(c->req + c->req_len - 4, "\r\n\r\n", 4)) {   /* end of the header */
                    c->header_done = true;
                    c->is_post = !strncmp(c->req, "POST ", 5);
                    const char *u = strchr(c->req, ' ');
                    int n = 0;
                    if (u) { u++; while (*u && *u != ' ' && n < (int)sizeof c->path - 1) c->path[n++] = *u++; }
                    c->path[n] = 0;
                    { int k = 0; while (c->req[k] && c->req[k] != ' ' && k < 7) { c->method[k] = c->req[k]; k++; } c->method[k] = 0; }
                    const char *au = strstr(c->req, "Authorization: ");
                    if (au) {
                        au += 15;
                        int k = 0;
                        while (au[k] && au[k] != '\r' && k < (int)sizeof c->auth - 1) { c->auth[k] = au[k]; k++; }
                        c->auth[k] = 0;
                    }
                    const char *cl = strstr(c->req, "Content-Length:");
                    if (!cl) cl = strstr(c->req, "content-length:");
                    c->content_len = cl ? atoi(cl + 15) : 0;
                    /* bodies (texts, settings) go straight into the big shared buffer */
                    c->body_to_g = c->is_post && c->content_len > 0 && c->content_len < (int)sizeof g_body - 32
                                   && g_pending_texts < 0 && !g_pending_cfg && !g_pending_restore && g_pending_sid < 0 && !jb_busy_body()
                                   && !ota_busy_body() && !g_body_owner;
                    if (c->body_to_g) g_body_owner = c;           /* a second POST at the same time must not mix in */
                }
            } else if (c->body_len < c->content_len) {
                if (c->body_to_g) g_body[c->body_len] = d[i];
                c->body_len++;
            }
        }
    }
    pbuf_free(p);
    if (c->header_done && c->body_len >= c->content_len) {
        handle(pcb, c);                                      /* the pending flags guard g_body from here on */
        if (g_body_owner == c) g_body_owner = NULL;
    }
    return ERR_OK;
}

static void http_err(void *arg, err_t err) {
    (void)err;
    conn_t *c = (conn_t *)arg;
    if (c) { if (g_body_owner == c) g_body_owner = NULL; free(c->own); free(c); g_conns--; }   /* the pcb is gone already */
}
static err_t http_poll(void *arg, struct tcp_pcb *pcb) {      /* every ~5 s */
    conn_t *c = (conn_t *)arg;
    if (c && c->done && c->tx_pos < c->tx_hdr_len + c->tx_body_len && c->tx_pos != c->poll_pos) {
        c->poll_pos = c->tx_pos;                              /* an answer still being sent (memory was short): go on */
        send_more(pcb, c);
        return ERR_OK;
    }
    return conn_close(pcb, c) ? ERR_ABRT : ERR_OK;            /* no progress for ~5 s: give up */
}
static err_t http_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)arg;
    if (err != ERR_OK || !pcb) return ERR_VAL;
    conn_t *c = g_conns < 4 ? (conn_t *)calloc(1, sizeof(conn_t)) : NULL;
    if (!c) { tcp_abort(pcb); return ERR_ABRT; }
    g_conns++;
    tcp_arg(pcb, c);
    tcp_recv(pcb, http_recv);
    tcp_sent(pcb, http_sent);
    tcp_err(pcb, http_err);
    tcp_poll(pcb, http_poll, 10);
    return ERR_OK;
}

/* a name or address looked up for the lWIP context: the forwarding target and the partner of two matrices
   (<name>.local is asked by mDNS), looked up again every minute */
typedef struct { char host[64]; ip_addr_t ip; volatile bool have, busy; absolute_time_t next; } res_t;
static res_t g_res, g_res_wide;
static void res_cb(const char *name, const ip_addr_t *ip, void *arg) {     /* lwIP context */
    res_t *r = (res_t *)arg;
    if (strcmp(name, r->host)) return;                  /* answer for a name that is no longer wanted */
    r->busy = false;
    if (ip) { r->ip = *ip; r->have = true; r->next = make_timeout_time_ms(60000); }
    else r->next = make_timeout_time_ms(5000);            /* not found: try again soon, keep an older answer */
}
static uint32_t resolve(res_t *r, const char *host) {
    if (strcmp(r->host, host)) {
        snprintf(r->host, sizeof r->host, "%s", host);
        r->have = r->busy = false; r->next = get_absolute_time();
    }
    if (r->host[0] && !r->busy && time_reached(r->next)) {
        ip_addr_t a;
        if (ipaddr_aton(r->host, &a)) { r->ip = a; r->have = true; r->next = make_timeout_time_ms(3600000); }
        else {
            r->busy = true;
            r->next = make_timeout_time_ms(10000);         /* in case no answer ever comes */
            cyw43_arch_lwip_begin();
            err_t e = dns_gethostbyname(r->host, &a, res_cb, r);
            cyw43_arch_lwip_end();
            if (e == ERR_OK) { r->ip = a; r->have = true; r->busy = false; r->next = make_timeout_time_ms(60000); }
            else if (e != ERR_INPROGRESS) { r->busy = false; r->next = make_timeout_time_ms(5000); }
        }
    }
    return r->host[0] && r->have ? ip4_addr_get_u32(ip_2_ip4(&r->ip)) : 0;
}
uint32_t net_resolve_wide(const char *host) { return resolve(&g_res_wide, host); }
static void fwd_task(void) {
    uint32_t ip = resolve(&g_res, g_cfg.stream ? g_cfg.fwd : "");
    g_my_ip = ip4_addr_get_u32(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA]));
    g_fwd_ip = ip;
}

static void start_servers(void) {
    if (g_servers_up) return;
    cyw43_arch_lwip_begin();
    struct tcp_pcb *l = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (l && tcp_bind(l, IP_ANY_TYPE, 80) == ERR_OK) {
        l = tcp_listen_with_backlog(l, 4);
        tcp_accept(l, http_accept);
    }
    if (g_cfg.stream) {                                   /* listen for the Ultimate's audio only if it is wanted */
        struct udp_pcb *u = udp_new_ip_type(IPADDR_TYPE_ANY);
        if (u && udp_bind(u, IP_ANY_TYPE, AUDIO_PORT) == ERR_OK) { udp_recv(u, audio_recv, NULL); g_audio_pcb = u; }
    }
    cyw43_arch_lwip_end();
    g_servers_up = true;
}

/* ------------------------------------------------------------ connection handling */
static void start_ap(void) {
    static bool told = false;
    g_t_retry = make_timeout_time_ms(AP_RETRY_MS);
    cyw43_arch_disable_sta_mode();
    cyw43_arch_enable_ap_mode(g_ap_ssid, AP_PASS, CYW43_AUTH_WPA2_AES_PSK);
    ip_addr_t gw, mask;
    IP4_ADDR(ip_2_ip4(&gw), 192, 168, 4, 1);
    IP4_ADDR(ip_2_ip4(&mask), 255, 255, 255, 0);
    cyw43_arch_lwip_begin();
    dhcp_server_init(&g_dhcp, &cyw43_state.netif[CYW43_ITF_AP], &gw, &mask);
    cyw43_arch_lwip_end();
    strcpy(g_ip, "192.168.4.1");
    g_state = NS_AP;
    start_servers();
    DIAG("wlan: eigener Zugangspunkt %s (192.168.4.1)", g_ap_ssid);
    if (!told) {                                          /* not again with every retry of the home WLAN */
        told = true;
        char m[64]; snprintf(m, sizeof m, "WLAN %s 192.168.4.1", g_ap_ssid);
        set_message(m);
    }
}

static void join_sta(void) {
    cyw43_arch_wifi_connect_async(g_cfg.ssid, g_cfg.pass[0] ? g_cfg.pass : NULL,
                                  g_cfg.pass[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN);
    g_t_connect = get_absolute_time();
    g_tries++; g_leaving = false; g_final_st = 0; g_noip = false;
}
/* leave the WLAN (also ends a join that hangs) and join again a second later */
static void rejoin_sta(absolute_time_t now, const char *why) {
    DIAG("wlan: %s - verbinde neu (Versuch %d)", why, g_tries + 1);
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    g_leaving = true; g_t_join = delayed_by_ms(now, LEAVE_MS);
    g_state = NS_CONNECTING;
}
/* join the home WLAN (at the start, and again from the access point) */
static void connect_sta(void) {
    cyw43_arch_enable_sta_mode();
    cyw43_arch_lwip_begin();                             /* the name the router shows (DHCP), before the link is up */
    netif_set_hostname(&cyw43_state.netif[CYW43_ITF_STA], g_hostname);
    cyw43_arch_lwip_end();
    cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);          /* see below: no power saving */
    g_tries = 0; g_saw_noip = false;
    join_sta();
    g_state = NS_CONNECTING;
}
/* lost the home WLAN (router restarted, out of range): connect again; the servers keep running */
static void reconnect_sta(absolute_time_t now) {
    g_was_sta = true; g_tries = 0; g_saw_noip = false;
    rejoin_sta(now, "Verbindung verloren");
}

void net_init(const char *texts) {
    g_texts = texts ? texts : "";
    cfg_load();
    g_src_ip = ip_u32(cur_u64()->addr);
    jb_init();
    DIAG("net: init - ssid='%s' u64=%d '%s' stream=%d smode=%d fwd='%s'", g_cfg.ssid, g_cfg.src + 1, cur_u64()->addr, g_cfg.stream, g_cfg.smode, g_cfg.fwd);
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_GERMANY)) { DIAG("net: cyw43-Init fehlgeschlagen"); g_state = NS_OFF; return; }
    g_t_pps = get_absolute_time();
    mdnsl_set_name(base_name());
    if (g_cfg.name[0]) snprintf(g_ap_ssid, sizeof g_ap_ssid, "%s-%s", AP_SSID, g_cfg.name); else strcpy(g_ap_ssid, AP_SSID);
    strcpy(g_hostname, base_name());
    if (!g_cfg.ssid[0]) { start_ap(); return; }
#ifdef TEST_WIFI_HOOKS                                   /* device test: start on the access point as if the WLAN was away */
    start_ap(); return;
#endif
    connect_sta();
}

/* the web page chose another Ultimate: the old one stops streaming to us (only if it really does - it may stream
   to the PC), the choice is stored (no restart), the new one is asked at once */
static void switch_source(absolute_time_t now) {
    int i = g_pending_src; g_pending_src = -1;
    if (i < 0 || i >= U64_MAX || i == g_cfg.src || !g_cfg.u64[i].addr[0]) return;
    bool old_streams = g_src_last_us && time_us_32() - g_src_last_us < 1000000u;
    g_stop_u64 = *cur_u64();
    g_cfg.src = (uint8_t)i;
    g_src_ip = ip_u32(cur_u64()->addr); g_src_last_us = 0;
    g_lock_ip = g_src_ip; g_lock_us = time_us_32();      /* from now on the new one's packets are the ones used */
    static netcfg_t w; w = g_cfg; w.magic = NETCFG_MAGIC; w.check = cfg_check(&w);
    flash_write(NETCFG_OFFSET, (const uint8_t *)&w, sizeof w);
    DIAG("net: Quelle %d '%s' (vorher '%s'%s)", i + 1, cur_u64()->addr, g_stop_u64.addr, old_streams ? ", stoppe ihn" : "");
    if (old_streams && g_cfg.smode != SMODE_FORWARDED) rest_request(RQ_STOP);
    g_cfg_tried = false; g_force_req = true; g_t_rest = now;
    strcpy(g_u64state, g_cfg.stream ? "wird gestartet" : "Stream aus");
    jb_source_changed();
    char m[96]; snprintf(m, sizeof m, "{color:00C0FF}Quelle: %s", cur_u64()->label[0] ? cur_u64()->label : cur_u64()->addr);
    set_message(m);
}

void net_task(void) {
    absolute_time_t now = get_absolute_time();
    diag_task();                                           /* flush pending diagnostics to USB serial */
    news_task();                                           /* retro news from the Pi: take over a new list */
    ota_task();                                            /* web firmware update: flash writes, check, apply */

    /* saves requested on the web page: write to flash, then restart */
    if (g_pending_texts >= 0) {
        /* built in place in g_body (nothing else may use it now, and the Pico restarts afterwards):
           drop the CRs, then put the magic in front */
        int ml = (int)strlen(TEXTS_MAGIC), n = 0;
        for (int i = 0; i < g_pending_texts && ml + n < (int)sizeof g_body - 1; i++)
            if (g_body[i] != '\r') g_body[n++] = g_body[i];
        memmove(g_body + ml, g_body, (size_t)n);
        memcpy(g_body, TEXTS_MAGIC, (size_t)ml);
        g_body[ml + n] = 0;
        flash_write(TEXTS_FLASH_OFFSET, (const uint8_t *)g_body, (uint32_t)(ml + n + 1));
        watchdog_reboot(0, 0, 100);
        while (1) tight_loop_contents();
    }
    if (g_pending_restore) {                                /* checked by the route: decode and store, restart soon */
        static uint8_t jbp[512];
        int l = 0; const char *h;
        if ((h = form_ptr(g_body, "netcfg", &l)) && hex_in((uint8_t *)&g_cfg, (int)sizeof g_cfg, h, l) == (int)sizeof g_cfg)
            flash_write(NETCFG_OFFSET, (const uint8_t *)&g_cfg, sizeof g_cfg);   /* RAM too: a later save keeps it */
        if ((h = form_ptr(g_body, "jbprefs", &l))) { int n = hex_in(jbp, (int)sizeof jbp, h, l); if (n > 0) jb_prefs_restore(jbp, n, true); }
        bool now_restart = form_has(g_body, "reboot");
        if ((h = form_ptr(g_body, "settings", &l)) && hex_in((uint8_t *)h, l / 2, h, l) == FLASH_SECTOR_SIZE) {   /* in place: byte i <- hex 2i */
            settings_frozen = true;                          /* the old brightness etc. must not be saved over it */
            flash_write(SETTINGS_FLASH_OFFSET, (const uint8_t *)h, FLASH_SECTOR_SIZE);
        }
        DIAG("net: Sicherung eingespielt%s", now_restart ? ", Neustart" : " - warte auf die Lauftexte");
        g_pending_restore = false;
        if (now_restart) { watchdog_reboot(0, 0, 100); while (1) tight_loop_contents(); }
        g_restart_due = true; g_restart_at = make_timeout_time_ms(15000);   /* texts.txt follows (it restarts), else now */
    }
    if (g_restart_due && time_reached(g_restart_at)) { watchdog_reboot(0, 0, 100); while (1) tight_loop_contents(); }
    if (g_pending_cfg) {
        g_newcfg.magic = NETCFG_MAGIC;
        g_newcfg.check = cfg_check(&g_newcfg);
        flash_write(NETCFG_OFFSET, (const uint8_t *)&g_newcfg, sizeof g_newcfg);
        watchdog_reboot(0, 0, 100);
        while (1) tight_loop_contents();
    }

    if (absolute_time_diff_us(g_t_pps, now) >= 1000000) {      /* packets per second for the status page */
        g_pps = g_pkts - g_pkts_prev;
        g_pkts_prev = g_pkts;
        g_fpps = g_fwd_pkts - g_fwd_prev;
        g_fwd_prev = g_fwd_pkts;
        g_t_pps = now;
    }

    /* multicast watch: on many routers multicast is sent in slow bursts - long gaps between stream
       packets mean the WLAN is clogging up (web page/jukebox become unreachable, see v39) */
    if (absolute_time_diff_us(g_t_mcwatch, now) >= 5000000) {
        g_t_mcwatch = now;
        uint32_t gap = g_gap_max_us;
        g_gap_max_us = 0;
        if (g_cfg.smode != SMODE_MULTICAST || !g_cfg.stream || !net_audio_active()) g_mcwarn = false;
        else if (gap > 400000) {
            if (!g_mcwarn) DIAG("net: Multicast-Wächter - Paketlücke %lu ms", (unsigned long)(gap / 1000));
            g_mcwarn = true;
            if (absolute_time_diff_us(g_t_mcmsg, now) >= 600000000ll) {   /* matrix hint at most every 10 min */
                g_t_mcmsg = now;
                set_message("{color:FF4000}Multicast stoert das WLAN - Unicast empfohlen");
            }
        }
    }

    switch (g_state) {
        case NS_CONNECTING: {
            if (g_leaving) { if (time_reached(g_t_join)) join_sta(); break; }
            int st = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
            static int st_prev = -99;
            if (st != st_prev) { DIAG("wlan: Verbindungsstatus %d -> %d", st_prev, st); st_prev = st; }
            if (st == CYW43_LINK_UP) {
                strncpy(g_ip, ip4addr_ntoa(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA])), sizeof g_ip - 1);
                g_state = NS_STA;
                DIAG("wlan: verbunden mit '%s', IP %s (Versuch %d)", g_cfg.ssid, g_ip, g_tries);
                start_servers();
                dev_start();                              /* beacons: the device list on every web page */
                wide_start();                             /* two matrices as one picture (if switched on) */
                /* no WLAN power saving: in power-save mode the router holds back packets for the sleeping
                   chip - with the audio stream (250 packets/s) that builds a queue of seconds and the web
                   page and ping time out. The Pico runs from USB power anyway. */
                cyw43_arch_lwip_begin();
                cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
                cyw43_arch_lwip_end();
                cyw43_arch_lwip_begin();                      /* clock: time from the internet */
                static bool sntp_on = false;                  /* (once - after a reconnect it keeps running) */
                if (!sntp_on) { sntp_on = true; sntp_setoperatingmode(SNTP_OPMODE_POLL); sntp_setservername(0, "pool.ntp.org"); sntp_init(); }
                mdnsl_start(&cyw43_state.netif[CYW43_ITF_STA]);   /* <name>.local, first checks it is free */
                cyw43_arch_lwip_end();
                g_name_told = false;                          /* IP + name on the matrix once the name is settled */
                g_t_rest = make_timeout_time_ms(500);
                strcpy(g_u64state, cur_u64()->addr[0] ? (g_cfg.stream ? "wird gestartet" : "Stream aus") : "nicht eingerichtet");
                break;
            }
            /* NOIP = at the router, only DHCP is missing: that is no "home WLAN away" (seen after a firmware update:
               18 s in NOIP, then 5 min on the access point) - leave and join again, the access point only after
               JOIN_TRIES such joins */
            int64_t since = absolute_time_diff_us(g_t_connect, now);
            if (st == CYW43_LINK_NOIP && !g_noip) { g_noip = true; g_t_noip = now; g_saw_noip = true; }
            bool noip_out = st == CYW43_LINK_NOIP && absolute_time_diff_us(g_t_noip, now) > NOIP_TIMEOUT_MS * 1000ll;
            bool timeout = st != CYW43_LINK_NOIP && since > CONNECT_TIMEOUT_MS * 1000ll;   /* NOIP has its own time */
            bool final = st == CYW43_LINK_FAIL || st == CYW43_LINK_BADAUTH || st == CYW43_LINK_NONET;
            if (!final) g_final_st = 0;
            else if (st != g_final_st) { g_final_st = st; g_t_final = now; }
            bool final_held = final && absolute_time_diff_us(g_t_final, now) > FINAL_MS * 1000ll;
            const char *why = noip_out ? "keine Adresse vom Router (DHCP)"
                            : st == CYW43_LINK_NONET ? "Heim-WLAN nicht gefunden"
                            : st == CYW43_LINK_BADAUTH ? "Passwort abgelehnt"
                            : st == CYW43_LINK_FAIL ? "Anmeldung fehlgeschlagen" : "keine Verbindung in 20 s";
            if (g_was_sta) {                                    /* was in the home WLAN: keep trying, no access point */
                if (timeout || noip_out || (final && since > 5000000)) rejoin_sta(now, why);
            } else if (st == CYW43_LINK_BADAUTH && final_held) {
                DIAG("wlan: '%s': Passwort abgelehnt - eigener Zugangspunkt", g_cfg.ssid);
                start_ap();                                     /* wrong password: set it up on the access point */
            } else if (final_held) {                            /* NONET/FAIL: once more (a scan can miss the router) */
                if (g_tries < 2 || (g_saw_noip && g_tries < JOIN_TRIES)) rejoin_sta(now, why);
                else { DIAG("wlan: '%s': %s (%d Versuche) - eigener Zugangspunkt", g_cfg.ssid, why, g_tries); start_ap(); }
            } else if (noip_out || timeout) {
                if (g_saw_noip && g_tries < JOIN_TRIES) rejoin_sta(now, why);
                else {
                    DIAG(g_saw_noip ? "wlan: '%s': nach %d Versuchen keine Adresse vom Router - eigener Zugangspunkt"
                                    : "wlan: '%s': nach %d Versuch(en) nie ueber die Anmeldung hinaus - eigener Zugangspunkt",
                         g_cfg.ssid, g_tries);
                    start_ap();                                 /* home WLAN not reachable: own access point */
                }
            }
            break;
        }
        case NS_AP:
            /* fell back to the own access point although a home WLAN is set (router not up yet after a power cut,
               WLAN clogged by a multicast stream): try it again every 5 minutes - but not while someone uses the
               page on the access point (setting it up there must never be cut off) */
            if (g_cfg.ssid[0] && time_reached(g_t_retry)) {
                uint32_t web = g_web_ms;
                uint32_t idle = to_ms_since_boot(now) - web;
                if (web && idle < AP_RETRY_MS) { g_t_retry = make_timeout_time_ms(AP_RETRY_MS - idle); break; }   /* 5 min after the last use */
                DIAG("wlan: versuche das Heim-WLAN erneut");
                cyw43_arch_lwip_begin(); dhcp_server_deinit(&g_dhcp); cyw43_arch_lwip_end();
                cyw43_arch_disable_ap_mode();
                connect_sta();
            }
            break;
        case NS_STA: {
            /* link lost for 10 s: connect again (the WLAN chip does not do it by itself) */
            static absolute_time_t down_since; static bool down = false;
#ifdef TEST_WIFI_HOOKS                                   /* device test: leave the WLAN once, 60 s after joining */
            static absolute_time_t joined; static int drop = 0;
            if (!drop) { joined = now; drop = 1; }
            else if (drop == 1 && absolute_time_diff_us(joined, now) > 60000000) {
                drop = 2; DIAG("test: verlasse das WLAN"); cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
            }
#endif
            if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
                if (!down) { down = true; down_since = now; }
                else if (absolute_time_diff_us(down_since, now) > 10000000) {
                    down = false;
                    reconnect_sta(now);
                    break;
                }
            } else down = false;
            if (!g_name_told) {
                cyw43_arch_lwip_begin(); mdnsl_task(); cyw43_arch_lwip_end();
                if (mdnsl_ready()) {
                    g_name_told = true;
                    bool other = strcmp(mdnsl_name(), base_name()) != 0;
                    DIAG("net: mDNS als %s.local%s", mdnsl_name(), other ? " (Name war schon vergeben)" : "");
                    char m[80];
                    snprintf(m, sizeof m, "WLAN %s  %s.local", g_ip, mdnsl_name());
                    set_message(m);
                }
            }
            if (g_pending_sid >= 0 && g_rq == RQ_NONE && !rest_blocked() && g_up_total == 0 && jb_sidaddr_ready()) {   /* jukebox: forward the SID */
                rest_request(RQ_UPLOAD);
                if (g_rq == RQ_UPLOAD) {
                    const char *note = jb_sidaddr_note();   /* addresses switched for this tune (option sidshow) */
                    size_t l = strlen(g_sid_title);
                    if (note[0] && l + strlen(note) + 20 < sizeof g_sid_title)
                        snprintf(g_sid_title + l, sizeof g_sid_title - l, " {color:80C0FF}%s", note);
                    set_message(g_sid_title);
                    jb_stereo_hint(jb_sid_chips((const uint8_t *)g_body, g_pending_sid));   /* mono or stereo from the header */
                }
                else g_pending_sid = -1;
            }
            if (g_rq == RQ_NONE) g_up_total = 0;
            if (g_pending_put && g_rq == RQ_NONE && !rest_blocked()) { g_pending_put = false; rest_request(RQ_PUT); }
            if (g_pending_get && g_rq == RQ_NONE && !rest_blocked()) { g_pending_get = false; rest_request(RQ_GET); }
            if (g_pending_cmd && g_rq == RQ_NONE && !g_pending_put && !rest_blocked()) {   /* a button: through the PUT slot, without a ticket */
                g_pending_cmd = false; strcpy(g_put_path, g_cmd_path); g_put_ticket = 0; rest_request(RQ_PUT);
            }
            jb_task();
            u64scan_task();
            if (g_pending_src >= 0 && g_rq == RQ_NONE && !rest_blocked()) switch_source(now);
            /* ask the Ultimate for the stream; repeat every 10 s while no audio arrives (e.g. after its reboot) */
            /* (not when another device forwards the stream: asking would take it away from that device) */
            if (g_cfg.stream && cur_u64()->addr[0] && g_cfg.smode != SMODE_FORWARDED && (!net_audio_active() || g_force_req)
                && (time_reached(g_t_rest) || g_force_req) && g_rq == RQ_NONE && !rest_blocked()) {
                g_force_req = false;
                if (g_cfg.smode == SMODE_MULTICAST && !g_cfg_tried) {   /* multicast: first ask which group the Ultimate uses */
                    rest_request(RQ_CONFIG);
                    g_t_rest = make_timeout_time_ms(3000);
                } else {
                    if (g_cfg.smode == SMODE_MULTICAST) join_group();
                    rest_request(RQ_START);
                    g_t_rest = make_timeout_time_ms(10000);
                }
            }
            /* (also "wird gestartet": the Ultimate may still stream to this Pico from before a restart - then it is
               never asked again and the state stayed at "wird gestartet") */
            if (net_audio_active() && (!strcmp(g_u64state, "Stream angefordert") || !strcmp(g_u64state, "wird gestartet")))
                strcpy(g_u64state, "Stream läuft");
            if (g_cfg.stream && g_cfg.smode == SMODE_FORWARDED)
                strcpy(g_u64state, net_audio_active() ? "Stream kommt weitergeleitet" : "wartet auf weitergeleiteten Stream");
            fwd_task();
            dev_task();
            wide_task();
            break;
        }
        default:
            break;
    }
}

bool net_take_message(char *buf, int len) {
    if (!g_msg_pending) return false;
    strncpy(buf, g_msg, len - 1);
    buf[len - 1] = 0;
    g_msg_pending = false;
    return true;
}

int net_take_command(void) {
    int c = g_cmd;
    g_cmd = NETCMD_NONE;
    return c;
}
