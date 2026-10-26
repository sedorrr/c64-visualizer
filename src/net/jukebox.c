/*
 * SID jukebox on the Pico 2 W
 *  - browses the Ultimate's storage (e.g. a USB stick) over FTP and plays SID files from it
 *    (REST: PUT /v1/runners:sidplay?file=...); the playlist lives in the Pico, so it keeps
 *    playing when the web page is closed
 *  - song lengths: a small database in flash, built from the HVSC "Songlengths.md5" and/or a
 *    user file "<md5>=<seconds>" (Ultimate64 SID Player / SID-Jukebox), read from the stick
 *    over FTP or uploaded from the PC; user entries take precedence
 *
 * lwIP callbacks (FTP) run in the background and only move data and set flags; flash writes,
 * REST requests and the playlist logic run in jb_task() on the main loop.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/rand.h"
#include "lwip/tcp.h"
#include "lwip/ip4_addr.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "hardware/regs/addressmap.h"
#include "md5.h"
#include "net_internal.h"
#include "jukebox.h"
#include "diag.h"
#include "analysis.h"

/* ================================================================== song length database (flash) */
#define SLDB_OFFSET 0x100000u                 /* 1 MB .. 3 MB: far behind the firmware, before the texts */
#define SLDB_SIZE   0x200000u
#define SLDB_MAGIC  0x534C4442u
typedef struct { uint32_t magic, count, data_len, check; char hvsc[100], user[100]; } sldb_hdr_t;

static const sldb_hdr_t *hdr(void) { return (const sldb_hdr_t *)(XIP_BASE + SLDB_OFFSET); }
static bool sldb_valid(void) {
    const sldb_hdr_t *h = hdr();
    return h->magic == SLDB_MAGIC && h->check == (h->count ^ h->data_len ^ 0x5A5A5A5Au) && h->data_len < SLDB_SIZE;
}

/* record: md5[0..7], n, n x uint16 seconds (little endian) */
int sldb_lookup(const uint8_t md5[16], int song, int *count) {
    if (!sldb_valid()) return -1;
    const uint8_t *p = (const uint8_t *)(XIP_BASE + SLDB_OFFSET + 4096), *end = p + hdr()->data_len;
    while (p + 9 <= end) {
        int n = p[8];
        if (!memcmp(p, md5, 8)) {
            if (count) *count = n;
            int i = song > 0 ? song - 1 : 0;
            if (i >= n) i = n == 1 ? 0 : -1;               /* user file: one value for the whole tune */
            return i < 0 ? -1 : (int)(p[9 + 2 * i] | (p[10 + 2 * i] << 8));
        }
        p += 9 + 2 * n;
    }
    return -1;
}

/* ---- remembered jukebox settings (own flash sector below the database) */
#define PREFS_OFFSET (SLDB_OFFSET - 4096)
#define PREFS_MAGIC_V1 0x4A425031u
#define PREFS_MAGIC    0x4A425032u
typedef struct { uint32_t magic; uint8_t src, jbsrc, pad[2]; char hvsc[100], user[100], dir[160]; uint32_t check; } prefs_v1_t;
/* v2: + SID addresses - switching on/off, and while a socket is switched: which one and its original address
   (so a restart of the Pico in the middle of a tune still restores it) */
typedef struct {
    uint32_t magic; uint8_t src, jbsrc, sidauto, sa_sock; uint16_t sa_orig; uint8_t sidshow, pad;
    char hvsc[100], user[100], dir[160]; uint32_t check;
} prefs_t;
static prefs_t prefs;
static volatile bool prefs_dirty = false;
static uint32_t fnv_bytes(const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p; uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}
static uint32_t prefs_check(const prefs_t *p) { return fnv_bytes(p, offsetof(prefs_t, check)); }

/* ---- writer (main loop only) */
static uint8_t  w_sec[4096];
static uint32_t w_base, w_fill, w_total, w_count;
static bool     w_open = false, w_overflow = false;
static char     w_src_hvsc[100], w_src_user[100];

static void flash_sector(uint32_t off, const uint8_t *data) {
    watchdog_update();
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(off, FLASH_SECTOR_SIZE);
    restore_interrupts(irq);
    for (uint32_t p = 0; p < FLASH_SECTOR_SIZE; p += FLASH_PAGE_SIZE) {
        irq = save_and_disable_interrupts();
        flash_range_program(off + p, data + p, FLASH_PAGE_SIZE);
        restore_interrupts(irq);
    }
}
static void w_begin(void) {
    memset(w_sec, 0xFF, sizeof w_sec);
    flash_sector(SLDB_OFFSET, w_sec);                    /* header gone: the database is invalid while importing */
    w_base = SLDB_OFFSET + 4096; w_fill = 0; w_total = 0; w_count = 0; w_open = true; w_overflow = false;
}
static void w_put(const uint8_t *d, uint32_t n) {
    while (n && w_open) {
        if (w_base + 4096 > SLDB_OFFSET + SLDB_SIZE) { w_overflow = true; return; }
        uint32_t k = 4096 - w_fill; if (k > n) k = n;
        memcpy(w_sec + w_fill, d, k); w_fill += k; d += k; n -= k; w_total += k;
        if (w_fill == 4096) { flash_sector(w_base, w_sec); w_base += 4096; w_fill = 0; }
    }
}
static void w_record(const uint8_t md5_8[8], int n, const uint16_t *secs) {
    uint8_t r[9 + 2 * 64];
    if (n > 64) n = 64;
    memcpy(r, md5_8, 8); r[8] = (uint8_t)n;
    for (int i = 0; i < n; i++) { r[9 + 2 * i] = (uint8_t)secs[i]; r[10 + 2 * i] = (uint8_t)(secs[i] >> 8); }
    w_put(r, 9 + 2 * n);
    w_count++;
}
static void w_end(void) {
    if (!w_open) return;
    if (w_fill) { memset(w_sec + w_fill, 0xFF, 4096 - w_fill); flash_sector(w_base, w_sec); }
    memset(w_sec, 0xFF, sizeof w_sec);
    sldb_hdr_t h; memset(&h, 0, sizeof h);
    h.magic = SLDB_MAGIC; h.count = w_count; h.data_len = w_total; h.check = w_count ^ w_total ^ 0x5A5A5A5Au;
    strncpy(h.hvsc, w_src_hvsc, sizeof h.hvsc - 1); strncpy(h.user, w_src_user, sizeof h.user - 1);
    memcpy(w_sec, &h, sizeof h);
    flash_sector(SLDB_OFFSET, w_sec);
    w_open = false;
}

/* ---- text parser: "<md5>=m:ss m:ss.mmm ..." (HVSC) or "<md5>=<seconds>" (user file) */
typedef struct { char line[600]; int n; } lineparser_t;
static bool hexbyte(const char *s, uint8_t *v) {
    int r = 0;
    for (int i = 0; i < 2; i++) {
        char c = s[i]; r <<= 4;
        if (c >= '0' && c <= '9') r |= c - '0'; else if (c >= 'a' && c <= 'f') r |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') r |= c - 'A' + 10; else return false;
    }
    *v = (uint8_t)r; return true;
}
/* parses one line into md5 + times; returns number of times (0 = no record) */
static int parse_line(const char *s, uint8_t md5_8[8], uint16_t *secs) {
    while (*s == ' ' || *s == '\t') s++;
    if (strlen(s) < 34 || s[32] != '=') return 0;
    for (int i = 0; i < 8; i++) if (!hexbyte(s + 2 * i, &md5_8[i])) return 0;
    for (int i = 8; i < 16; i++) { uint8_t d; if (!hexbyte(s + 2 * i, &d)) return 0; }
    const char *p = s + 33;
    int n = 0;
    while (*p && n < 64) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '\r' || *p == '\n') break;
        long a = strtol(p, (char **)&p, 10), sec;
        if (*p == ':') {                                  /* m:ss[.mmm] */
            p++;
            long b = strtol(p, (char **)&p, 10);
            sec = a * 60 + b;
            if (*p == '.') {                              /* fraction: ".5" = 500 ms, ".600" = 600 ms */
                p++; int ms = 0, d = 0;
                while (*p >= '0' && *p <= '9') { if (d < 3) { ms = ms * 10 + (*p - '0'); d++; } p++; }
                while (d < 3) { ms *= 10; d++; }
                if (ms >= 500) sec++;
            }
        } else sec = a;                                   /* plain seconds */
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;   /* "(G)" etc. */
        if (sec < 0) sec = 0; if (sec > 65535) sec = 65535;
        secs[n++] = (uint16_t)sec;
    }
    return n;
}

/* staging ring between the FTP callbacks (producer) and the flash writer (main loop) */
#define STAGE 16384
#define LBUF 57344                                         /* HVSC folders: DEMOS/A-F ~34 KB, DEMOS/UNKNOWN ~51 KB */
/* the staging ring (song lengths from the stick) and the FTP listing are never in use at the same time:
   one FTP job at a time, browsing/scanning is refused during an import, and an import does not start
   while a listing is still in use - so they share one buffer (saves 16 KB of RAM) */
static union { uint8_t stage[STAGE]; char lbuf[LBUF]; } s_buf;
static volatile uint32_t st_head = 0, st_tail = 0;
static uint32_t stage_free(void) { return STAGE - (st_head - st_tail); }
static lineparser_t lp;
static void stage_line(void) {
    lp.line[lp.n] = 0;
    uint8_t m[8]; uint16_t secs[64];
    int n = parse_line(lp.line, m, secs);
    lp.n = 0;
    if (!n) return;
    uint8_t r[9 + 128];
    memcpy(r, m, 8); r[8] = (uint8_t)n;
    for (int i = 0; i < n; i++) { r[9 + 2 * i] = (uint8_t)secs[i]; r[10 + 2 * i] = (uint8_t)(secs[i] >> 8); }
    int len = 9 + 2 * n;
    if ((int)stage_free() < len + 2) return;              /* cannot happen: the FTP flow control keeps room */
    s_buf.stage[st_head % STAGE] = (uint8_t)len; st_head++;
    for (int i = 0; i < len; i++) { s_buf.stage[st_head % STAGE] = r[i]; st_head++; }
}
static void stage_feed(const uint8_t *d, int n) {
    for (int i = 0; i < n; i++) {
        char c = (char)d[i];
        if (c == '\n') stage_line();
        else if (lp.n < (int)sizeof lp.line - 1) lp.line[lp.n++] = c;
    }
}
static void stage_drain(void) {                          /* main loop: staged records -> flash */
    while (st_tail != st_head) {
        int len = s_buf.stage[st_tail % STAGE];
        uint8_t r[9 + 128];
        for (int i = 0; i < len; i++) r[i] = s_buf.stage[(st_tail + 1 + i) % STAGE];
        st_tail += 1 + len;
        w_put(r, len); w_count++;
    }
}

/* ================================================================== FTP client */
typedef enum { F_IDLE, F_GREET, F_USER, F_PASS, F_TYPE, F_PASV, F_DATA, F_XFER } fstate_t;
typedef enum { J_LIST, J_RETR } jkind_t;
static struct {
    fstate_t st;
    jkind_t kind;
    char path[200];
    void (*on_data)(const uint8_t *, int);
    int  need;                                             /* flow control: bytes that must fit before taking data */
    bool (*room)(int);
    struct tcp_pcb *ctrl, *data;
    char line[300]; int ln;
    bool ctrl_done, data_closed, ok;
    volatile bool finished;
    absolute_time_t started, activity;                     /* activity: last data from the Ultimate */
    char err[64];
    const char *stage;                                     /* shown on the web page while it runs */
    char last[64];                                         /* last reply of the FTP server (for errors) */
} F;

static void ftp_send(const char *cmd) {
    char b[260];
    int n = snprintf(b, sizeof b, "%s\r\n", cmd);
    DIAG("ftp: > %s", strncmp(cmd, "PASS ", 5) ? cmd : "PASS ***");
    if (F.ctrl) { tcp_write(F.ctrl, b, (u16_t)n, TCP_WRITE_FLAG_COPY); tcp_output(F.ctrl); }
}
static void ftp_close_pcb(struct tcp_pcb **pp) {
    struct tcp_pcb *p = *pp;
    if (!p) return;
    tcp_arg(p, NULL); tcp_recv(p, NULL); tcp_err(p, NULL); tcp_sent(p, NULL);
    if (tcp_close(p) != ERR_OK) tcp_abort(p);
    *pp = NULL;
}
static void ftp_finish(bool ok, const char *err) {
    if (F.st == F_IDLE) return;
    DIAG("ftp: Ende ok=%d st=%d err='%s' last='%s'", ok, F.st, err ? err : "", F.last);
    F.stage = "";
    if (ok && F.ctrl) ftp_send("QUIT");
    ftp_close_pcb(&F.data); ftp_close_pcb(&F.ctrl);
    F.ok = ok;
    if (err) snprintf(F.err, sizeof F.err, "%s%s%s", err, F.last[0] ? " - " : "", F.last); else F.err[0] = 0;
    F.st = F_IDLE;
    F.finished = true;                                     /* jb_task picks it up */
}
static volatile bool f_parsing = false;                    /* jb_task is still reading the result of a job */
static void ftp_try_finish(void) { if (F.ctrl_done && F.data_closed) ftp_finish(true, NULL); }

static uint32_t d_rx = 0;                                  /* bytes received on the data channel (diagnostics) */
static err_t data_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)arg; (void)err;
    if (!p) { DIAG("ftp: Datenkanal zu, %lu Bytes empfangen", (unsigned long)d_rx); ftp_close_pcb(&F.data); F.data_closed = true; ftp_try_finish(); return ERR_OK; }
    if (F.room && !F.room(p->tot_len)) return ERR_MEM;     /* writer busy: lwIP keeps the data and retries */
    if (!d_rx) DIAG("ftp: erste Daten (%d Bytes)", p->tot_len);
    d_rx += p->tot_len;
    F.activity = get_absolute_time(); F.stage = "empfange Daten";
    for (struct pbuf *q = p; q; q = q->next) if (F.on_data) F.on_data((const uint8_t *)q->payload, q->len);
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}
static void data_err(void *arg, err_t err) { (void)arg; DIAG("ftp: Datenkanal-Fehler err=%d", (int)err); F.data = NULL; ftp_finish(false, "Datenverbindung abgebrochen"); }
static err_t data_connected(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)arg; (void)pcb;
    DIAG("ftp: Datenkanal verbunden (err=%d)", (int)err);
    if (err != ERR_OK) { ftp_finish(false, "Datenverbindung fehlgeschlagen"); return ERR_OK; }
    char cmd[220];
    snprintf(cmd, sizeof cmd, "%s %s", F.kind == J_LIST ? "LIST" : "RETR", F.path);
    ftp_send(cmd);
    F.st = F_XFER; F.stage = F.kind == J_LIST ? "lese Ordner" : "lade Datei";
    return ERR_OK;
}

static void ctrl_reply(int code, const char *text) {
    F.activity = get_absolute_time();
    snprintf(F.last, sizeof F.last, "%d %s", code, text);
    switch (F.st) {
        case F_GREET: if (code == 220) { ftp_send("USER anonymous"); F.st = F_USER; F.stage = "melde an"; } else ftp_finish(false, "FTP nicht bereit"); break;
        case F_USER:
            if (code == 230) { ftp_send("TYPE I"); F.st = F_TYPE; }
            else if (code == 331) { char b[80]; snprintf(b, sizeof b, "PASS %s", net_u64_pw()[0] ? net_u64_pw() : "pico"); ftp_send(b); F.st = F_PASS; }
            else ftp_finish(false, "FTP-Anmeldung abgelehnt");
            break;
        case F_PASS: if (code == 230) { ftp_send("TYPE I"); F.st = F_TYPE; } else ftp_finish(false, "FTP-Passwort falsch"); break;
        case F_TYPE: ftp_send("PASV"); F.st = F_PASV; F.stage = "öffne Datenkanal"; break;
        case F_PASV: {
            /* "227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)": only the port is used - the data connection goes
               to the same address as the control connection (the Ultimate may name another interface) */
            int h1, h2, h3, h4, p1 = -1, p2 = -1;
            for (const char *p = text; *p; p++)
                if (*p >= '0' && *p <= '9' && sscanf(p, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) == 6) break;
            if (code != 227 || p1 < 0 || p2 < 0) { ftp_finish(false, "PASV fehlgeschlagen"); break; }
            ip_addr_t a;
            if (!F.ctrl) { ftp_finish(false, "FTP-Verbindung weg"); break; }
            ip_addr_copy(a, F.ctrl->remote_ip);
            DIAG("ftp: PASV -> verbinde %s:%d", ipaddr_ntoa(&a), p1 * 256 + p2);
            F.data = tcp_new_ip_type(IPADDR_TYPE_V4);
            if (!F.data) { ftp_finish(false, "kein Speicher"); break; }
            tcp_recv(F.data, data_recv); tcp_err(F.data, data_err);
            F.st = F_DATA;
            if (tcp_connect(F.data, &a, (u16_t)(p1 * 256 + p2), data_connected) != ERR_OK) ftp_finish(false, "Datenverbindung fehlgeschlagen");
            break;
        }
        case F_DATA: case F_XFER:
            if (code == 150 || code == 125) break;
            if (code == 226 || code == 250) { F.ctrl_done = true; ftp_try_finish(); break; }
            if (code >= 400) ftp_finish(false, code == 550 ? "Datei/Ordner nicht gefunden" : "FTP-Fehler");
            break;
        default: break;
    }
}
static err_t ctrl_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)arg; (void)err;
    if (!p) { DIAG("ftp: Steuerkanal vom Server geschlossen (st=%d)", F.st); F.ctrl = NULL; tcp_arg(pcb, NULL); tcp_close(pcb); if (F.st != F_IDLE) ftp_finish(F.ctrl_done && F.data_closed, "FTP-Verbindung beendet"); return ERR_OK; }
    for (struct pbuf *q = p; q; q = q->next) {
        const char *d = (const char *)q->payload;
        for (int i = 0; i < q->len; i++) {
            if (d[i] == '\n') {
                F.line[F.ln] = 0;
                DIAG("ftp: < %s", F.line);
                if (F.ln >= 4 && F.line[3] == ' ') ctrl_reply(atoi(F.line), F.line + 4);   /* "123-" = continued */
                F.ln = 0;
            } else if (d[i] != '\r' && F.ln < (int)sizeof F.line - 1) F.line[F.ln++] = d[i];
        }
    }
    if (F.ctrl) tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}
static void ctrl_err(void *arg, err_t err) { (void)arg; DIAG("ftp: Steuerkanal-Fehler err=%d (st=%d)", (int)err, F.st); F.ctrl = NULL; ftp_finish(false, "Ultimate per FTP nicht erreichbar"); }
static err_t ctrl_connected(void *arg, struct tcp_pcb *pcb, err_t err) { (void)arg; (void)pcb; DIAG("ftp: Steuerkanal verbunden (err=%d)", (int)err); if (err != ERR_OK) ftp_finish(false, "FTP nicht erreichbar"); return ERR_OK; }

/* start an FTP job (main loop) */
static bool ftp_start(jkind_t kind, const char *path, void (*on_data)(const uint8_t *, int), bool (*room)(int)) {
    if (F.st != F_IDLE) { DIAG("ftp: start abgelehnt - noch beschäftigt (st=%d)", F.st); return false; }
    ip_addr_t a;
    if (!ipaddr_aton(net_u64_ip(), &a)) { DIAG("ftp: Ultimate-IP '%s' ungültig", net_u64_ip()); return false; }
    DIAG("ftp: start %s '%s' -> %s:21", kind == J_LIST ? "LIST" : "RETR", path, net_u64_ip());
    d_rx = 0;
    memset(&F, 0, sizeof F);
    F.kind = kind; strncpy(F.path, path, sizeof F.path - 1); F.on_data = on_data; F.room = room;
    F.started = F.activity = get_absolute_time();
    F.stage = "verbinde mit dem FTP-Dienst";
    cyw43_arch_lwip_begin();
    F.ctrl = tcp_new_ip_type(IPADDR_TYPE_V4);
    bool ok = false;
    if (F.ctrl) {
        tcp_recv(F.ctrl, ctrl_recv); tcp_err(F.ctrl, ctrl_err);
        F.st = F_GREET;
        ok = tcp_connect(F.ctrl, &a, 21, ctrl_connected) == ERR_OK;
        if (!ok) { tcp_abort(F.ctrl); F.ctrl = NULL; F.st = F_IDLE; }
    }
    cyw43_arch_lwip_end();
    if (!ok) DIAG("ftp: tcp_connect fehlgeschlagen");
    return ok;
}

/* ================================================================== directory listing */
static int llen;
static bool lfull;                                         /* raw listing no longer fit into the listing buffer */
static void list_data(const uint8_t *d, int n) { while (n-- > 0 && llen < LBUF - 1) s_buf.lbuf[llen++] = (char)*d++; if (n >= 0) lfull = true; }

/* one listing line -> name and directory flag (Unix "ls -l", DOS "<DIR>" or bare names) */
static bool list_line(char *line, const char **name, bool *dir) {
    int n = (int)strlen(line);
    while (n && (line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
    if (!n) return false;
    if (strchr("d-l", line[0]) && n > 10 && strchr("rwx-", line[1])) {
        const char *p = line; int f = 0;
        while (*p && f < 8) { while (*p && *p != ' ') p++; while (*p == ' ') p++; f++; }
        *name = p; *dir = line[0] == 'd';
    } else if (strstr(line, "<DIR>")) {
        const char *p = strstr(line, "<DIR>") + 5; while (*p == ' ') p++;
        *name = p; *dir = true;
    } else { *name = line; *dir = strchr(line, '.') == NULL; }
    return **name && strcmp(*name, ".") && strcmp(*name, "..");
}
static bool is_sid(const char *s) {
    size_t n = strlen(s);
    return n > 4 && (s[n - 4] == '.') && (s[n - 3] | 32) == 's' && (s[n - 2] | 32) == 'i' && (s[n - 1] | 32) == 'd';
}

/* ---- browsing for the web page */
#define LS_MAX 720                                         /* DEMOS/UNKNOWN has ~700 entries */
static char ls_path[200];
static const char *ls_name[LS_MAX];                        /* points into the listing buffer (strtok-terminated lines) */
static bool ls_dir[LS_MAX];
static int  ls_n, ls_state;                                /* 0 idle, 1 busy, 2 ok, 3 error */
static bool ls_trunc;                                      /* listing was cut off (buffer or LS_MAX) */
static char ls_err[64];

/* ================================================================== playlist (in the Pico) */
#define PL_MAX 320                                         /* fits the user's largest shared playlist (260) */
static char pl[PL_MAX][160];
static int  pl_n, pl_order[PL_MAX], pl_pos = -1;
static bool pl_shuffle, pl_silence = true;
static int  pl_minutes = 3;
#define SCANQ 48                                           /* folders waiting to be scanned (with sub-folders) */
static char scan_q[SCANQ][160]; static int scan_qn;
static bool scan_sub;
static bool scan_drop;                                     /* a sub-folder was skipped because the queue was full */
/* shared-playlist import (Ultimate64 SID Player JSON): the browser sends the file names, the Pico
   scans the chosen folder and keeps matches by name (lower-cased) in playlist order */
static uint32_t imp_hash[PL_MAX];                          /* FNV of the lower-cased wanted file names */
static int  imp_n = 0;
static bool imp_active = false;
static char imp_msg[80] = "";                              /* scan/import result shown on the page */
static uint32_t name_hash_n(const char *s, int n) {
    uint32_t h = 2166136261u;
    while (n-- > 0) { uint32_t c = (uint8_t)*s++; if (c >= 'A' && c <= 'Z') c += 32; h ^= c; h *= 16777619u; }
    return h;
}
typedef enum { JB_IDLE, JB_SCAN, JB_LOAD, JB_START, JB_PLAY, JB_ERR } jbst_t;
static jbst_t jb = JB_IDLE;
static char jb_msg[64] = "";
static md5_ctx_t cur_md5; static uint8_t cur_head[0x7C]; static int cur_got;
static char cur_title[33], cur_author[33];
static int  cur_dur = -1, cur_song = 1, cur_songs = 1;
static uint8_t cur_md5bin[16];
static char cur_md5hex[33] = "";
static volatile int want_song = 0;                        /* sub-tune picked on the web page (1..cur_songs) */
static absolute_time_t cur_start;
static volatile bool want_next, want_prev, want_stop;
static volatile int  want_goto = -1;                      /* play this playlist position (clicked on the web page) */
static int load_fails = 0;                                /* titles in a row that could not be loaded */

static void retr_data(const uint8_t *d, int n) {
    md5_update(&cur_md5, d, (size_t)n);
    for (int i = 0; i < n && cur_got < (int)sizeof cur_head; i++) cur_head[cur_got++] = d[i];
}
static void shuffle_order(void) {
    for (int i = 0; i < pl_n; i++) pl_order[i] = i;
    if (pl_shuffle) for (int i = pl_n - 1; i > 0; i--) { int j = (int)(get_rand_32() % (uint32_t)(i + 1)); int t = pl_order[i]; pl_order[i] = pl_order[j]; pl_order[j] = t; }
}
static void join_path(char *out, int n, const char *dir, const char *name) {
    size_t l = strlen(dir);
    snprintf(out, (size_t)n, "%s%s%s", dir, (l && dir[l - 1] == '/') ? "" : "/", name);
}
static void start_load(void) {
    if (pl_n == 0) { jb = JB_IDLE; return; }
    want_song = 0;
    md5_init(&cur_md5); cur_got = 0;
    if (ftp_start(J_RETR, pl[pl_order[pl_pos]], retr_data, NULL)) { jb = JB_LOAD; snprintf(jb_msg, sizeof jb_msg, "lade Titel %d von %d", pl_pos + 1, pl_n); }
    else { jb = JB_ERR; strcpy(jb_msg, "FTP nicht erreichbar"); }
}
static void urlencode(char *out, int n, const char *s) {
    int k = 0;
    for (; *s && k < n - 4; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || strchr("/-_.~", c)) out[k++] = (char)c;
        else k += snprintf(out + k, (size_t)(n - k), "%%%02X", c);
    }
    out[k] = 0;
}
static void title_message_of(const char *title, const char *author, int dur) {
    char t[33], a[33], m[150];
    snprintf(t, sizeof t, "%s", title); snprintf(a, sizeof a, "%s", author);
    for (char *p = t; *p; p++) if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || strchr(" !.-:", *p))) *p = ' ';
    for (char *p = a; *p; p++) if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || strchr(" !.-:", *p))) *p = ' ';
    int k = snprintf(m, sizeof m, "{color:FFD000}{speed:35}%s {color:00C0FF}%s", t, a);
    if (dur > 0 && k < (int)sizeof m - 20)                /* playing time from the song length files */
        k += snprintf(m + k, sizeof m - (size_t)k, " {color:FFFFFF}%d:%02d", dur / 60, dur % 60);
    const char *note = jb_sidaddr_note();                 /* addresses switched for this tune (option sidshow) */
    if (note[0] && k < (int)sizeof m - 60) snprintf(m + k, sizeof m - (size_t)k, " {color:80C0FF}%s", note);
    net_post_message(m);
}
static void title_message(void) { title_message_of(cur_title, cur_author, cur_dur); }

/* now playing from an external player (the Ultimate64 SID Jukebox on the PC plays the tunes itself and
   tells the Pico what runs): POST /jb/np. The web route only stores the request, jb_task takes it over. */
typedef struct { char title[33], author[33], sids[24]; int dur, el, song, songs, chips; bool fresh, stop; } np_t;
static np_t np_in, np;                                     /* np_in: waiting for jb_task; np: the current tune */
static volatile bool np_new = false, np_kill = false;    /* np_kill: our own jukebox starts */
static bool np_ousted = false;                             /* ...and took over: only a new tune from the PC counts again */
static bool np_on = false;
static absolute_time_t np_start, np_heard;                 /* when np.el was reported, last message from the player */
static bool np_title_due = false, np_cfg_wait = false;     /* title waits for the socket check (option sidshow) */
static absolute_time_t np_title_at;


/* ================================================================== SID addresses (sockets only)
   A tune that needs a SID at an address no socket has gets the one spare socket switched while it plays;
   afterwards (stop, next tune without that need, 60 s silence, restart) the original address is restored.
   Only the unambiguous case is handled: exactly one address missing, exactly one socket not needed. */
typedef enum { SA_IDLE, SA_REVERT, SA_GET, SA_SET, SA_RELEASE, SA_POLL } sa_state_t;
static sa_state_t sa_st = SA_IDLE;
static volatile bool sa_want = false, sa_rel = false;
static uint8_t sa_hdr[0x7C]; static volatile int sa_hlen;
static uint16_t sa_req[3], sa_cur = 0, sa_new = 0; static int sa_nreq = 0;
static uint32_t sa_ticket; static absolute_time_t sa_retry;
static char sa_msg[64] = "";
/* showing the addresses (option sidshow): the sockets as last read, a note for the next title, polling */
static uint16_t sa_known[5];                              /* socket 1..4, 0 = unmapped / unknown */
static bool sa_online = false, sa_changed = false;
static absolute_time_t sa_poll_at;
static bool sa_poll_np = false;                           /* this poll was started for an external tune (np) */
static char sa_note[48] = "";
static void sa_cfg_text(char *o, int n) {                 /* "SID 1: D400  SID 2: D500" (the font has no $) */
    int k = 0; o[0] = 0;
    for (int i = 1; i <= 4 && k < n - 16; i++)
        if (sa_known[i]) k += snprintf(o + k, (size_t)(n - k), "%sSID %d: %04X", k ? "  " : "", i, sa_known[i]);
}
static void sa_show(void) {                               /* scroll the current addresses once */
    char t[40], m[80]; sa_cfg_text(t, sizeof t);
    if (!t[0]) return;
    snprintf(m, sizeof m, "{color:80C0FF}{speed:35}%s", t);
    net_post_message(m);
}
const char *jb_sidaddr_note(void) {                        /* appended to the next title, then cleared */
    static char n[48]; strcpy(n, sa_note); sa_note[0] = 0; return n;
}
static bool sa_valid(uint8_t b) { return !(b & 1) && ((b >= 0x42 && b <= 0x7F) || (b >= 0xE0 && b <= 0xFE)); }
static int sid_addresses(const uint8_t *h, int len, uint16_t out[3]) {
    int n = 0, ver = len > 5 ? (h[4] << 8 | h[5]) : 1;
    out[n++] = 0xD400;
    if (ver >= 3 && len > 0x7A && sa_valid(h[0x7A])) out[n++] = (uint16_t)(0xD000 + (h[0x7A] << 4));
    if (ver >= 4 && len > 0x7B && sa_valid(h[0x7B])) out[n++] = (uint16_t)(0xD000 + (h[0x7B] << 4));
    return n;
}
void jb_sidaddr_need(const uint8_t *h, int len) {
    if (!prefs.sidauto && !prefs.sa_sock) return;         /* switched off and nothing to restore: the tune starts at once */
    int n = len < (int)sizeof sa_hdr ? len : (int)sizeof sa_hdr;
    memcpy(sa_hdr, h, (size_t)n); sa_hlen = n; sa_want = true;
}
bool jb_sidaddr_ready(void) { return !sa_want && sa_st == SA_IDLE; }
int jb_sid_chips(const uint8_t *h, int len) { uint16_t a[3]; return sid_addresses(h, len, a); }
/* stereo display from the SID header: set when a tune starts, taken back when nothing plays any more */
static bool jb_hinted = false;
/* "nothing plays any more": a minute of silence - counted from the last tune start, so a tune that starts
   after a long pause is not ended before its first note arrives */
static absolute_time_t jb_tune_at;
static bool jb_long_silence(void) {
    return net_silent_ms() >= 60000 && absolute_time_diff_us(jb_tune_at, get_absolute_time()) >= 60000000;
}
void jb_stereo_hint(int chips) { audio_stereo_hint(chips); jb_hinted = chips > 0; if (chips > 0) jb_tune_at = get_absolute_time(); }
void jb_sidaddr_release(void) { sa_rel = true; }
static bool sa_needs(uint16_t a) { for (int i = 0; i < sa_nreq; i++) if (sa_req[i] == a) return true; return false; }
static void sa_put(int sock, uint16_t addr) {
    char p[120];
    snprintf(p, sizeof p, "/v1/configs/SID%%20Addressing/SID%%20Socket%%20%d%%20Address?value=%%24%04X", sock, addr);
    sa_ticket = net_rest_put(p);
}
static uint16_t sa_socket_addr(const char *body, int n) {   /* "SID Socket n Address": "$D420" -> 0xD420, 0 = unmapped */
    char key[32]; snprintf(key, sizeof key, "\"SID Socket %d Address\"", n);
    const char *p = strstr(body, key);
    if (!p || !(p = strchr(p + strlen(key), '"')) || p[1] != '$') return 0;
    return (uint16_t)strtoul(p + 2, NULL, 16);
}
static void sa_task(void) {
    const char *body; int code;
    switch (sa_st) {
    case SA_IDLE:
        if (!time_reached(sa_retry) || !net_rest_idle()) break;
        if (sa_rel) {
            sa_rel = false;
            if (prefs.sa_sock) { sa_put(prefs.sa_sock, prefs.sa_orig); sa_st = SA_RELEASE; }
            break;
        }
        if (sa_want) {
            uint8_t h[sizeof sa_hdr]; int n = sa_hlen; memcpy(h, sa_hdr, sizeof h);
            sa_want = false;
            sa_nreq = sid_addresses(h, n, sa_req);
            if (prefs.sa_sock && !(prefs.sidauto && sa_cur && sa_needs(sa_cur))) {   /* the new tune does not need it */
                memcpy(sa_hdr, h, sizeof h); sa_put(prefs.sa_sock, prefs.sa_orig); sa_st = SA_REVERT; break;
            }
            if (prefs.sidauto && !prefs.sa_sock) { sa_ticket = net_rest_get("/v1/configs/SID%20Addressing"); sa_st = SA_GET; }
        } else if (prefs.sa_sock && jb_long_silence()) sa_rel = true;   /* nothing plays any more */
        else if (prefs.sidshow && !prefs.sa_sock && time_reached(sa_poll_at)) {
            sa_ticket = net_rest_get("/v1/configs/SID%20Addressing"); sa_st = SA_POLL; sa_poll_np = np_cfg_wait;
        }
        break;
    case SA_REVERT: case SA_RELEASE:
        if (!net_rest_done(sa_ticket)) break;
        code = net_rest_result(&body);
        if (code == 200) {
            snprintf(sa_msg, sizeof sa_msg, "Sockel %d zurück auf $%04X", prefs.sa_sock, prefs.sa_orig);
            DIAG("sid: %s", sa_msg);
            sa_known[prefs.sa_sock] = prefs.sa_orig;
            prefs.sa_sock = 0; prefs_dirty = true; sa_cur = 0;
            if (sa_st == SA_REVERT) { sa_want = true; sa_changed = true; }   /* now check the new tune against the original setup */
            else if (prefs.sidshow) sa_show();            /* playback ended: show the restored addresses */
        } else {
            snprintf(sa_msg, sizeof sa_msg, "Zurückstellen fehlgeschlagen (%d)", code);
            DIAG("sid: %s", sa_msg); sa_retry = make_timeout_time_ms(30000);
        }
        sa_st = SA_IDLE;
        break;
    case SA_GET: {
        if (!net_rest_done(sa_ticket)) break;
        code = net_rest_result(&body); sa_st = SA_IDLE;
        if (code != 200) { snprintf(sa_msg, sizeof sa_msg, "SID-Konfiguration nicht lesbar (%d)", code); break; }
        uint16_t sock[5] = { 0 }; int miss = 0, spare = 0, which = 0; uint16_t need = 0;
        for (int i = 1; i <= 4; i++) sa_known[i] = sock[i] = sa_socket_addr(body, i);
        sa_online = true;
        for (int r = 0; r < sa_nreq; r++) {
            bool have = false; for (int i = 1; i <= 4; i++) if (sock[i] == sa_req[r]) have = true;
            if (!have) { miss++; need = sa_req[r]; }
        }
        for (int i = 1; i <= 4; i++) if (sock[i] && !sa_needs(sock[i])) { spare++; which = i; }
        if (!miss || miss > 1 || spare != 1) {
            if (!miss) sa_msg[0] = 0; else snprintf(sa_msg, sizeof sa_msg, "SID-Adressen nicht eindeutig - nicht umgestellt");
            if (sa_changed && prefs.sidshow) sa_cfg_text(sa_note, sizeof sa_note);   /* reverted for this tune */
            sa_changed = false;
            break;
        }
        prefs.sa_sock = (uint8_t)which; prefs.sa_orig = sock[which]; prefs_dirty = true;   /* remembered before switching */
        sa_new = need; sa_put(which, need); sa_st = SA_SET;
        break; }
    case SA_SET:
        if (!net_rest_done(sa_ticket)) break;
        code = net_rest_result(&body); sa_st = SA_IDLE;
        if (code == 200) {
            sa_cur = sa_new; sa_known[prefs.sa_sock] = sa_new; jb_tune_at = get_absolute_time();   /* the tune starts now */
            snprintf(sa_msg, sizeof sa_msg, "Sockel %d für dieses Stück auf $%04X", prefs.sa_sock, sa_new);
            if (prefs.sidshow) sa_cfg_text(sa_note, sizeof sa_note);   /* shown with the title */
        }
        else { snprintf(sa_msg, sizeof sa_msg, "Umstellen fehlgeschlagen (%d)", code); prefs.sa_sock = 0; prefs_dirty = true; }
        DIAG("sid: %s", sa_msg); sa_changed = false;
        break;
    case SA_POLL: {                                       /* option sidshow: who changed what, is the Ultimate there */
        if (!net_rest_done(sa_ticket)) break;
        code = net_rest_result(&body); sa_st = SA_IDLE;
        bool for_np = sa_poll_np && np_cfg_wait;
        sa_poll_np = false;
        if (for_np) np_cfg_wait = false;                  /* the waiting title goes out now, with or without a note */
        if (code != 200) { sa_online = false; sa_poll_at = make_timeout_time_ms(30000); break; }
        uint16_t now[5] = { 0 }; bool diff = false;
        for (int i = 1; i <= 4; i++) { now[i] = sa_socket_addr(body, i); if (now[i] != sa_known[i]) diff = true; }
        memcpy(sa_known, now, sizeof now);
        if (!sa_online || diff) {                         /* switched on, or changed elsewhere (e.g. by the PC jukebox) */
            if (for_np) sa_cfg_text(sa_note, sizeof sa_note);   /* shown with the title of the external tune */
            else sa_show();
            DIAG("sid: Belegung %04X %04X", now[1], now[2]);
        }
        sa_online = true; sa_poll_at = make_timeout_time_ms(60000);
        break; }
    }
}

/* ================================================================== now playing from an external player */
static int64_t np_elapsed_ms(void) { return (int64_t)np.el * 1000 + absolute_time_diff_us(np_start, get_absolute_time()) / 1000; }
static int np_elapsed(void) { return (int)(np_elapsed_ms() / 1000); }
void jb_np_cancel(void) { np_kill = true; }
static void np_end(const char *why) {
    if (!np_on) return;
    np_on = false; np_title_due = false; np_cfg_wait = false;
    jb_stereo_hint(0);
    DIAG("np: Ende (%s)", why);
}
static void np_task(void) {
    if (np_kill) { np_kill = false; np_ousted = true; np_end("eigene Jukebox startet"); }
    if (np_new) {
        np_t m = np_in; np_new = false;
        np_heard = get_absolute_time();
        if (m.stop) {
            np_end("Stopp vom PC");
            if (prefs.sidshow && !prefs.sa_sock) sa_poll_at = make_timeout_time_ms(2000);   /* the PC puts its addresses back */
        } else {
            if (!m.fresh && !np_on && np_ousted) return;  /* clock update of a PC tune the own jukebox replaced */
            np_ousted = false;
            bool again = np_on && !m.fresh;                /* update of the running tune (or the PC sends it again) */
            if (jb != JB_IDLE) {                           /* the PC took over the Ultimate: our own playlist ends, no reset */
                if ((jb == JB_SCAN || jb == JB_LOAD) && F.st != F_IDLE) {   /* its folder scan or download is not needed any more */
                    cyw43_arch_lwip_begin(); ftp_finish(false, "abgebrochen"); cyw43_arch_lwip_end();
                }
                want_goto = -1; imp_active = false; pl_n = 0; pl_pos = -1; jb = JB_IDLE;
                strcpy(jb_msg, "Jukebox am PC spielt");
                jb_sidaddr_release();
            }
            np = m; np_on = true;
            np_start = get_absolute_time();                /* the clock: el seconds at this moment */
            if (np.el < 0) np.el = 0;
            if (!again) {
                DIAG("np: '%s' - '%s' %d s, %d SID(s) %s", np.title, np.author, np.dur, np.chips, np.sids);
                jb_stereo_hint(np.chips);                  /* mono or stereo from the PC's header */
                np_title_due = true;
                if (prefs.sidshow && !prefs.sa_sock) {     /* first look whether the PC switched the sockets */
                    np_cfg_wait = true; sa_poll_at = get_absolute_time(); np_title_at = make_timeout_time_ms(3000);
                } else np_cfg_wait = false;
            }
        }
    }
    if (np_title_due && (!np_cfg_wait || time_reached(np_title_at))) {
        np_title_due = false; np_cfg_wait = false;
        title_message_of(np.title, np.author, np.dur);
    }
    if (np_on) {                                           /* the PC went away without "stop": end after the tune */
        bool quiet = absolute_time_diff_us(np_heard, get_absolute_time()) > 75000000;
        bool over = np.dur > 0 ? np_elapsed() > np.dur + 5 : true;
        if (quiet && over) np_end("nichts mehr vom PC");
    }
}

/* ================================================================== main loop */
static int sldb_job = 0;                                 /* stick import: 0 none, 1 user file, 2 HVSC file */
static char sldb_msg[64] = "";
static volatile int pc_chunk_len = -1;                    /* PC import: chunk waiting in the body buffer */
static const uint8_t *pc_chunk;
static volatile bool pc_begin, pc_end, stick_begin;
static volatile bool sldb_clear_req = false;              /* web page asked to erase the database */
static bool room_stage(int n) { (void)n; return stage_free() > 4096; }

static void sldb_next_stick_job(void) {
    while (sldb_job < 2) {
        sldb_job++;
        const char *p = sldb_job == 1 ? w_src_user : w_src_hvsc;
        if (!p[0]) continue;
        lp.n = 0;
        if (ftp_start(J_RETR, p, stage_feed, room_stage)) { snprintf(sldb_msg, sizeof sldb_msg, "lese %s", sldb_job == 1 ? "eigene Datei" : "HVSC-Datei"); return; }
        strcpy(sldb_msg, "FTP nicht erreichbar");
    }
    stage_drain(); w_end(); sldb_job = 0;
    snprintf(sldb_msg, sizeof sldb_msg, w_overflow ? "zu groß - nur teilweise gespeichert" : "%lu Einträge gespeichert", (unsigned long)w_count);
}

void jb_init(void) {
    memset(&F, 0, sizeof F);
    memcpy(&prefs, (const void *)(XIP_BASE + PREFS_OFFSET), sizeof prefs);
    if (prefs.magic != PREFS_MAGIC || prefs.check != prefs_check(&prefs)) {
        prefs_v1_t old;                                   /* the layout before v2: keep folder and file paths */
        memcpy(&old, (const void *)(XIP_BASE + PREFS_OFFSET), sizeof old);
        memset(&prefs, 0, sizeof prefs); strcpy(prefs.dir, "/");
        if (old.magic == PREFS_MAGIC_V1 && old.check == fnv_bytes(&old, offsetof(prefs_v1_t, check))) {
            prefs.src = old.src; prefs.jbsrc = old.jbsrc;
            memcpy(prefs.hvsc, old.hvsc, sizeof prefs.hvsc); memcpy(prefs.user, old.user, sizeof prefs.user);
            memcpy(prefs.dir, old.dir, sizeof prefs.dir);
            prefs_dirty = true;                           /* saved in the new layout by jb_task */
        }
    }
    prefs.hvsc[99] = prefs.user[99] = prefs.dir[159] = 0;
    if (prefs.sa_sock) jb_sidaddr_release();             /* a switched socket from before the restart: restore it */
}
static void prefs_save(void) {                            /* main loop */
    if (w_open) return;                                   /* the import uses w_sec - save afterwards */
    prefs_dirty = false;                                  /* before copying: a change during the erase is saved next time */
    prefs.magic = PREFS_MAGIC; prefs.check = prefs_check(&prefs);
    memset(w_sec, 0xFF, sizeof w_sec);
    memcpy(w_sec, &prefs, sizeof prefs);
    flash_sector(PREFS_OFFSET, w_sec);
}

void jb_task(void) {
    /* PC import: begin / chunks / end (flash writes only here) */
    if (pc_begin) { pc_begin = false; w_begin(); strcpy(sldb_msg, "empfange vom PC ..."); }
    if (pc_chunk_len >= 0) { w_put(pc_chunk, (uint32_t)pc_chunk_len); pc_chunk_len = -1; }
    if (pc_end) { pc_end = false; w_count = 0;
        /* count the records that were written */
        w_end();
        const uint8_t *p = (const uint8_t *)(XIP_BASE + SLDB_OFFSET + 4096), *e = p + hdr()->data_len; uint32_t c = 0;
        while (p + 9 <= e) { c++; p += 9 + 2 * p[8]; }
        memset(w_sec, 0xFF, sizeof w_sec);
        sldb_hdr_t h; memcpy(&h, hdr(), sizeof h); h.count = c; h.check = h.count ^ h.data_len ^ 0x5A5A5A5Au;
        memcpy(w_sec, &h, sizeof h); flash_sector(SLDB_OFFSET, w_sec);
        snprintf(sldb_msg, sizeof sldb_msg, "%lu Einträge gespeichert", (unsigned long)c);
    }
    if (stick_begin && F.st == F_IDLE && !F.finished && jb != JB_SCAN && ls_state != 1) {
        stick_begin = false;
        if (ls_state == 2) { ls_state = 0; ls_n = 0; }       /* the import reuses the listing buffer */
        w_begin(); st_head = st_tail = 0; sldb_job = 0; sldb_next_stick_job();
    }
    if (sldb_clear_req && !w_open && !sldb_job && pc_chunk_len < 0 && !pc_begin && !pc_end && !stick_begin) {
        sldb_clear_req = false;
        watchdog_update();                                 /* erasing the header sector invalidates the database */
        uint32_t irq = save_and_disable_interrupts();
        flash_range_erase(SLDB_OFFSET, FLASH_SECTOR_SIZE);
        restore_interrupts(irq);
        strcpy(sldb_msg, "Datenbank gelöscht");
        DIAG("sldb: Datenbank gelöscht");
    }
    if (sldb_job) stage_drain();
    if (prefs_dirty && !w_open && !sldb_job) prefs_save();
    sa_task();
    np_task();

    if (F.st != F_IDLE && absolute_time_diff_us(F.activity, get_absolute_time()) > 15000000) {   /* 15 s without an answer */
        char e[64]; snprintf(e, sizeof e, "keine Antwort (%s)", F.stage ? F.stage : "?");
        DIAG("ftp: ZEITLIMIT 15 s - st=%d stage='%s' last='%s' rx=%lu", F.st, F.stage ? F.stage : "", F.last, (unsigned long)d_rx);
        cyw43_arch_lwip_begin(); ftp_finish(false, e); cyw43_arch_lwip_end();
    }

    if (F.finished) {                                       /* an FTP job has ended */
        F.finished = false;
        f_parsing = true;                                  /* routes must not restart FTP while lbuf is being read */
        if (sldb_job) { if (!F.ok) snprintf(sldb_msg, sizeof sldb_msg, "%s", F.err); sldb_next_stick_job(); }
        else if (ls_state == 1 && F.kind == J_LIST && jb != JB_SCAN) {
            s_buf.lbuf[llen] = 0; ls_n = 0; ls_trunc = lfull;
            DIAG("ls: Ergebnis ok=%d, %d Bytes%s", F.ok, llen, lfull ? " (Puffer voll, gekürzt)" : "");
            if (F.ok) {
                char *line = strtok(s_buf.lbuf, "\n");
                for (; line && ls_n < LS_MAX; line = strtok(NULL, "\n")) {
                    const char *nm; bool d = false;
                    bool used = list_line(line, &nm, &d);
                    DIAG("ls: roh '%s'%s%s%s", line, used ? " -> " : " (ignoriert)", used ? (d ? "[D] " : "    ") : "", used ? nm : "");
                    diag_task();                           /* main loop: flush so long listings don't overflow the ring */
                    if (!used) continue;
                    ls_name[ls_n] = nm; ls_dir[ls_n] = d; ls_n++;
                }
                if (line) ls_trunc = true;                 /* more lines than LS_MAX entries */
                ls_state = 2;
            } else { ls_state = 3; strncpy(ls_err, F.err, sizeof ls_err - 1); }
        } else if (jb == JB_SCAN) {
            s_buf.lbuf[llen] = 0;
            DIAG("scan: '%s' ok=%d, %d Bytes, bisher %d Titel, %d Ordner offen", F.path, F.ok, llen, pl_n, scan_qn);
            char dir[160]; strncpy(dir, F.path, sizeof dir - 1); dir[159] = 0;
            if (F.ok) for (char *line = strtok(s_buf.lbuf, "\n"); line; line = strtok(NULL, "\n")) {
                const char *nm; bool d;
                if (!list_line(line, &nm, &d)) continue;
                if (d && scan_sub && scan_qn < SCANQ) join_path(scan_q[scan_qn++], 160, dir, nm);
                else if (d && scan_sub) scan_drop = true;  /* queue full: this branch of the tree is skipped */
                else if (!d && is_sid(nm) && imp_active) {    /* playlist import: keep wanted names, in order */
                    uint32_t h = name_hash_n(nm, (int)strlen(nm));
                    for (int i = 0; i < imp_n; i++)
                        if (imp_hash[i] == h && !pl[i][0]) { join_path(pl[i], 160, dir, nm); break; }
                }
                else if (!d && is_sid(nm) && pl_n < PL_MAX) join_path(pl[pl_n++], 160, dir, nm);
            }
            if (scan_qn > 0 && (imp_active || pl_n < PL_MAX)) {   /* next folder */
                char next[160]; strcpy(next, scan_q[--scan_qn]);
                llen = 0; lfull = false;
                if (ls_state == 2) { ls_state = 0; ls_n = 0; }   /* ls_name points into the listing buffer */
                if (!ftp_start(J_LIST, next, list_data, NULL)) { imp_active = false; jb = JB_ERR; strcpy(jb_msg, "FTP nicht erreichbar"); }
            } else {
                if (imp_active) {                             /* close the gaps, keep the playlist order */
                    imp_active = false;
                    for (int i = 0; i < imp_n; i++) if (pl[i][0]) { if (pl_n != i) strcpy(pl[pl_n], pl[i]); pl_n++; }
                    snprintf(imp_msg, sizeof imp_msg, "Playlist: %d von %d Titeln gefunden%s", pl_n, imp_n,
                             scan_drop ? " - nicht alles durchsucht (zu viele Unterordner)" : "");
                    DIAG("import: %s", imp_msg);
                } else if (scan_drop) {
                    snprintf(imp_msg, sizeof imp_msg, "nicht alles durchsucht - zu viele Unterordner");
                    DIAG("scan: %s", imp_msg);
                }
                if (pl_n == 0) { jb = JB_ERR; strcpy(jb_msg, imp_n ? "kein Titel der Playlist gefunden" : "keine SID-Dateien gefunden"); }
                else { shuffle_order(); pl_pos = 0; start_load(); }
            }
        } else if (jb == JB_LOAD) {
            if (!F.ok || cur_got < 0x7C || (memcmp(cur_head, "PSID", 4) && memcmp(cur_head, "RSID", 4))) {
                snprintf(jb_msg, sizeof jb_msg, "übersprungen: %s", F.ok ? "keine SID-Datei" : F.err);
                if (++load_fails >= pl_n) {                  /* a whole round failed: stop instead of retrying forever */
                    jb = JB_ERR;
                    snprintf(jb_msg, sizeof jb_msg, "kein Titel ladbar: %s", F.ok ? "keine SID-Datei" : F.err);
                    DIAG("jb: %s", jb_msg);
                } else {
                    if (++pl_pos >= pl_n) pl_pos = 0;
                    start_load();
                }
            } else {
                load_fails = 0;
                uint8_t d[16]; md5_final(&cur_md5, d);
                memcpy(cur_md5bin, d, 16);
                memcpy(cur_title, cur_head + 0x16, 32); cur_title[32] = 0;
                memcpy(cur_author, cur_head + 0x36, 32); cur_author[32] = 0;
                cur_song = (cur_head[0x10] << 8) | cur_head[0x11]; if (cur_song < 1) cur_song = 1;
                cur_songs = (cur_head[0x0E] << 8) | cur_head[0x0F]; if (cur_songs < 1) cur_songs = 1;
                cur_dur = sldb_lookup(d, cur_song, NULL);
                for (int i = 0; i < 16; i++) snprintf(cur_md5hex + 2 * i, 3, "%02x", d[i]);
                jb_sidaddr_need(cur_head, cur_got);       /* switch a socket first if the tune needs it */
                jb = JB_START;
            }
        }
        f_parsing = false;
    }

    if (jb == JB_START && net_rest_idle() && jb_sidaddr_ready()) {   /* ask the Ultimate to play it */
        char path[400], enc[360];
        urlencode(enc, sizeof enc, pl[pl_order[pl_pos]]);
        snprintf(path, sizeof path, "/v1/runners:sidplay?file=%s", enc);
        net_rest_put(path);
        title_message();
        jb_stereo_hint(jb_sid_chips(cur_head, cur_got));   /* mono or stereo from the header */
        cur_start = get_absolute_time();
        jb = JB_PLAY;
        strcpy(jb_msg, "spielt");
    }

    if (want_song > 0 && jb == JB_PLAY && net_rest_idle()) {   /* sub-tune picked: play the same file again */
        cur_song = want_song; want_song = 0;
        cur_dur = sldb_lookup(cur_md5bin, cur_song, NULL);
        char path[400], enc[360];
        urlencode(enc, sizeof enc, pl[pl_order[pl_pos]]);
        snprintf(path, sizeof path, "/v1/runners:sidplay?file=%s&songnr=%d", enc, cur_song);
        net_rest_put(path);
        title_message();
        cur_start = get_absolute_time();
    }

    if (jb_hinted && jb_long_silence()) { jb_stereo_hint(0); np_end("60 s Stille"); }   /* nothing plays any more: measure again */
    if (want_stop) { want_stop = false; want_goto = -1; imp_active = false; jb_sidaddr_release(); jb_stereo_hint(0); jb = JB_IDLE; pl_n = 0; pl_pos = -1; strcpy(jb_msg, "gestoppt"); if (net_rest_idle()) net_rest_put("/v1/machine:reset"); }
    if (jb == JB_PLAY || jb == JB_ERR) {
        int64_t el = absolute_time_diff_us(cur_start, get_absolute_time()) / 1000000;
        int limit = cur_dur > 0 ? cur_dur + 2 : pl_minutes * 60;
        bool silent_end = pl_silence && el > 15 && net_silent_ms() >= 5000;
        int g = want_goto;
        if (pl_n > 0 && g >= 0 && g < pl_n) {             /* a title clicked in the list on the web page */
            want_goto = -1; want_next = want_prev = false; load_fails = 0;
            pl_pos = g;
            start_load();
        } else if (pl_n > 0 && (want_next || want_prev || (jb == JB_PLAY && (el >= limit || silent_end)))) {
            if (want_next || want_prev) load_fails = 0;
            pl_pos = want_prev ? (pl_pos + pl_n - 1) % pl_n : (pl_pos + 1) % pl_n;
            want_next = want_prev = false;
            start_load();
        }
        if (g >= pl_n) want_goto = -1;
    }
}

/* ================================================================== web routes */
static char out[32768];
static void qparam(const char *path, const char *key, char *o, int n) {
    o[0] = 0;
    const char *q = strchr(path, '?');
    size_t kl = strlen(key);
    for (const char *p = q ? q + 1 : NULL; p && *p; ) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            p += kl + 1; int k = 0;
            while (*p && *p != '&' && k < n - 1) {
                if (*p == '%' && p[1] && p[2]) { char h[3] = { p[1], p[2], 0 }; o[k++] = (char)strtol(h, NULL, 16); p += 3; }
                else if (*p == '+') { o[k++] = ' '; p++; }
                else o[k++] = *p++;
            }
            o[k] = 0; return;
        }
        p = strchr(p, '&'); if (p) p++;
    }
}
static int jstr(char *o, int n, const char *s) {           /* JSON string with escaping */
    int k = 0;
    if (k < n) o[k++] = '"';
    for (; *s && k < n - 3; s++) { if (*s == '"' || *s == '\\') o[k++] = '\\'; o[k++] = (unsigned char)*s < 32 ? ' ' : *s; }
    if (k < n) o[k++] = '"';
    return k;
}
bool jb_busy_body(void) { return pc_chunk_len >= 0; }
/* switching to another Ultimate is only safe while ours does nothing on the old one: a playlist would go on
   on the new device, and a switched SID socket must be put back on the device it was switched on */
bool jb_idle(void) {
    return (jb == JB_IDLE || jb == JB_ERR) && F.st == F_IDLE && sa_st == SA_IDLE && !sa_want && !prefs.sa_sock && !sldb_job;
}
int jb_prefs_backup(uint8_t *out, int max) {
    prefs_t p = prefs;
    p.sa_sock = 0; p.sa_orig = 0;                          /* a switched socket belongs to this moment, not to a backup */
    p.magic = PREFS_MAGIC; p.check = prefs_check(&p);
    if (max < (int)sizeof p) return 0;
    memcpy(out, &p, sizeof p); return (int)sizeof p;
}
bool jb_prefs_restore(const uint8_t *in, int n, bool write) {
    prefs_t p;
    if (n != (int)sizeof p) return false;
    memcpy(&p, in, sizeof p);
    if (p.magic != PREFS_MAGIC || p.check != prefs_check(&p)) return false;
    if (!write) return true;
    p.sa_sock = 0; p.sa_orig = 0;
    p.hvsc[99] = p.user[99] = p.dir[159] = 0;
    prefs = p; prefs_save();                               /* main loop */
    return true;
}
void jb_source_changed(void) {
    memset(sa_known, 0, sizeof sa_known); sa_online = false;   /* sidshow: the new device's sockets */
    sa_poll_at = get_absolute_time();
    ls_state = 0; ls_n = 0;                                    /* a folder listing belonged to the old device */
}
static int np_json(char *o, int n) {                       /* the tune of the PC jukebox, for the web page */
    int k = snprintf(o, (size_t)n, "{\"on\":%s,\"title\":", np_on ? "true" : "false");
    k += jstr(o + k, n - k, np_on ? np.title : "");
    k += snprintf(o + k, (size_t)(n - k), ",\"author\":"); k += jstr(o + k, n - k, np_on ? np.author : "");
    k += snprintf(o + k, (size_t)(n - k), ",\"dur\":%d,\"elapsed\":%d,\"song\":%d,\"songs\":%d,\"chips\":%d,\"sids\":",
                  np_on ? np.dur : -1, np_on ? np_elapsed() : 0, np_on ? np.song : 0, np_on ? np.songs : 0, np_on ? np.chips : 0);
    k += jstr(o + k, n - k, np_on ? np.sids : "");
    k += snprintf(o + k, (size_t)(n - k), "}");
    return k;
}

int jb_progress(void) {
    if (np_on) {                                           /* a tune of the PC jukebox */
        if (np.dur <= 0) return -1;
        int64_t p = np_elapsed_ms() / np.dur;
        return p > 1000 ? 1000 : p < 0 ? 0 : (int)p;
    }
    if (jb != JB_PLAY || cur_dur <= 0) return -1;
    int64_t el = absolute_time_diff_us(cur_start, get_absolute_time()) / 1000;
    int64_t p = el / cur_dur;                              /* ms elapsed / s duration = per mille */
    return p > 1000 ? 1000 : (int)p;
}

/* now playing from an external player: title, author, secs (length, 0 = unknown), el (seconds played),
   song/songs, sid=D400,D420 (addresses of the tune) or chips=n, new=1 for a tune that just started;
   stop=1 when it stopped. Without new=1 it only updates the clock (sent again every 30 s).
   The same from the HTTP route and from a message relayed along the stream chain (net.c, "C64NP1 <query>"). */
static int np_submit(const char *path, int *olen) {
    char v[200];
    if (np_new) { *olen = snprintf(out, sizeof out, "{\"ok\":false,\"msg\":\"beschäftigt\"}"); return 409; }
    np_t m; memset(&m, 0, sizeof m);
    char t[16];
    qparam(path, "stop", t, sizeof t); m.stop = t[0] == '1';
    if (!m.stop) {
        qparam(path, "title", v, sizeof v); snprintf(m.title, sizeof m.title, "%s", v);
        qparam(path, "author", v, sizeof v); snprintf(m.author, sizeof m.author, "%s", v);
        qparam(path, "secs", t, sizeof t); m.dur = t[0] ? atoi(t) : -1; if (m.dur <= 0) m.dur = -1;
        qparam(path, "el", t, sizeof t); m.el = atoi(t);
        qparam(path, "song", t, sizeof t); m.song = atoi(t);
        qparam(path, "songs", t, sizeof t); m.songs = atoi(t);
        qparam(path, "new", t, sizeof t); m.fresh = t[0] == '1';
        qparam(path, "sid", v, sizeof v);                  /* "D400,D420" or "$D400 $D420": count and keep them */
        int n = 0, k2 = 0;
        for (const char *p = v; *p && n < 3; ) {
            while (*p && !isxdigit((unsigned char)*p)) p++;
            if (!*p) break;
            char *e; unsigned long a = strtoul(p, &e, 16);
            if (e == p) break;
            if (a >= 0xD000 && a <= 0xDFFF) { k2 += snprintf(m.sids + k2, sizeof m.sids - (size_t)k2, "%s%04lX", n ? " " : "", a); n++; }
            p = e;
        }
        qparam(path, "chips", t, sizeof t);
        m.chips = t[0] ? atoi(t) : (n ? n : 1);
        if (m.chips < 1) m.chips = 1;
        if (m.chips > 3) m.chips = 3;
        if (!m.title[0] && !m.author[0]) { *olen = snprintf(out, sizeof out, "{\"ok\":false,\"msg\":\"title fehlt\"}"); return 400; }
    }
    np_in = m; np_new = true;
    *olen = snprintf(out, sizeof out, "{\"ok\":true}"); return 200;
}

/* a title relayed by the visualizer before this one in the stream chain (lwIP context, from audio_recv) */
void jb_np_relayed(const char *query) {
    static char path[780];
    int dummy;
    snprintf(path, sizeof path, "/jb/np?%s", query);
    np_submit(path, &dummy);
}

int jb_route(bool post, const char *path, const char *body, int len, const char **o, int *olen, const char **type) {
    if (strncmp(path, "/jb/", 4) && strncmp(path, "/sldb/", 6)) return 0;
    *type = "application/json"; *o = out;
    char v[200];
    int k = 0;
    if (post && !strncmp(path, "/jb/ls", 6)) {
        if (F.st != F_IDLE || f_parsing || sldb_job) { DIAG("ls: abgelehnt - FTP beschäftigt (st=%d, sldb=%d)", F.st, sldb_job); *olen = snprintf(out, sizeof out, "{\"ok\":false}"); return 409; }
        qparam(path, "path", v, sizeof v); if (!v[0]) strcpy(v, "/");
        DIAG("ls: Anfrage von der Webseite: '%s'", v);
        strcpy(ls_path, v); ls_state = 1; ls_n = 0; llen = 0; lfull = false;
        if (!ftp_start(J_LIST, v, list_data, NULL)) { ls_state = 3; strcpy(ls_err, "FTP nicht erreichbar"); }
        *olen = snprintf(out, sizeof out, "{\"ok\":true}"); return 200;
    }
    if (!post && !strncmp(path, "/jb/ls", 6)) {
        k += snprintf(out + k, sizeof out - k, "{\"state\":%d,\"stage\":", ls_state);
        k += jstr(out + k, (int)sizeof out - k, ls_state == 1 && F.stage ? F.stage : "");
        k += snprintf(out + k, sizeof out - k, ",\"path\":");
        k += jstr(out + k, (int)sizeof out - k, ls_path);
        k += snprintf(out + k, sizeof out - k, ",\"err\":"); k += jstr(out + k, (int)sizeof out - k, ls_err);
        k += snprintf(out + k, sizeof out - k, ",\"items\":[");
        int i = 0;
        for (; i < ls_n && k < (int)sizeof out - 300; i++) {                 /* a name needs at most ~140 bytes */
            k += snprintf(out + k, sizeof out - k, "%s{\"d\":%d,\"n\":", i ? "," : "", ls_dir[i]);
            k += jstr(out + k, (int)sizeof out - k, ls_name[i]);
            k += snprintf(out + k, sizeof out - k, "}");
        }
        k += snprintf(out + k, sizeof out - k, "],\"trunc\":%s}", (ls_trunc || i < ls_n) ? "true" : "false");
        *olen = k; return 200;
    }
    if (post && !strncmp(path, "/jb/play", 8)) {
        if (F.st != F_IDLE || f_parsing || sldb_job) { *olen = snprintf(out, sizeof out, "{\"ok\":false,\"msg\":\"beschäftigt\"}"); return 409; }
        qparam(path, "path", v, sizeof v);
        char t[8];
        qparam(path, "sub", t, sizeof t); scan_sub = t[0] == '1';
        qparam(path, "shuffle", t, sizeof t); pl_shuffle = t[0] == '1';
        qparam(path, "silence", t, sizeof t); pl_silence = t[0] != '0';
        qparam(path, "min", t, sizeof t); pl_minutes = atoi(t) > 0 ? atoi(t) : 3;
        pl_n = 0; scan_qn = 0; pl_pos = -1; llen = 0; lfull = false; want_goto = -1;
        imp_n = 0; imp_active = false; imp_msg[0] = 0; scan_drop = false; load_fails = 0;
        if (ls_state == 2) { ls_state = 0; ls_n = 0; }     /* the scan reuses the listing buffer, ls_name points into it */
        DIAG("jb: play '%s' sub=%d shuffle=%d", v, scan_sub, pl_shuffle);
        np_kill = true;
        if (!is_sid(v) && strcmp(prefs.dir, v)) { strncpy(prefs.dir, v, sizeof prefs.dir - 1); prefs_dirty = true; }
        if (is_sid(v)) { strcpy(pl[pl_n++], v); shuffle_order(); pl_pos = 0; start_load(); }
        else if (ftp_start(J_LIST, v, list_data, NULL)) { jb = JB_SCAN; strcpy(jb_msg, "lese Ordner ..."); }
        else { jb = JB_ERR; strcpy(jb_msg, "FTP nicht erreichbar"); }
        *olen = snprintf(out, sizeof out, "{\"ok\":true}"); return 200;
    }
    if (post && !strncmp(path, "/jb/prefs", 9)) {          /* source of the song length files and their paths */
        char t[8]; qparam(path, "src", t, sizeof t);
        if (t[0]) prefs.src = t[0] == '1';
        qparam(path, "sidauto", t, sizeof t); if (t[0]) prefs.sidauto = t[0] == '1';
        qparam(path, "sidshow", t, sizeof t);
        if (t[0]) { bool on = t[0] == '1'; if (on && !prefs.sidshow) { sa_online = false; sa_poll_at = get_absolute_time(); } prefs.sidshow = on; }
        qparam(path, "jbsrc", t, sizeof t); if (t[0]) prefs.jbsrc = t[0] == '1';
        if (strstr(path, "dir=")) qparam(path, "dir", prefs.dir, sizeof prefs.dir);
        if (strstr(path, "hvsc=")) qparam(path, "hvsc", prefs.hvsc, sizeof prefs.hvsc);
        if (strstr(path, "user=")) qparam(path, "user", prefs.user, sizeof prefs.user);
        prefs_dirty = true;
        *olen = snprintf(out, sizeof out, "{\"ok\":true}"); return 200;
    }
    /* the playlist in the Pico (stick source), in play order: file names, current position */
    if (!post && !strcmp(path, "/jb/list")) {
        bool full = true;
        k += snprintf(out + k, sizeof out - k, "{\"pos\":%d,\"items\":[", pl_pos);
        for (int i = 0; i < pl_n; i++) {
            if (k > (int)sizeof out - 220) { full = false; break; }   /* keep room for one name + the tail */
            const char *p = pl[pl_order[i]], *base = strrchr(p, '/');
            k += snprintf(out + k, sizeof out - k, "%s", i ? "," : "");
            k += jstr(out + k, (int)sizeof out - k, base ? base + 1 : p);
        }
        k += snprintf(out + k, sizeof out - k, "],\"trunc\":%s}", full ? "false" : "true");
        *olen = k; return 200;
    }
    if (post && !strncmp(path, "/jb/goto", 8)) {
        char t[8]; qparam(path, "i", t, sizeof t);
        int i = atoi(t);
        if (!pl_n || i < 0 || i >= pl_n) { *olen = snprintf(out, sizeof out, "{\"ok\":false}"); return 400; }
        want_goto = i;
        *olen = snprintf(out, sizeof out, "{\"ok\":true}"); return 200;
    }
    /* shared playlist of the Ultimate64 SID Player: the browser sends the file names (one per line),
       the Pico scans the chosen folder and keeps the matches in playlist order (misses are dropped) */
    if (post && !strncmp(path, "/jb/plimport", 12)) {
        if (F.st != F_IDLE || f_parsing || sldb_job) { *olen = snprintf(out, sizeof out, "{\"ok\":false,\"msg\":\"beschäftigt\"}"); return 409; }
        if (!body || len <= 0) { *olen = snprintf(out, sizeof out, "{\"ok\":false}"); return 400; }
        qparam(path, "path", v, sizeof v); if (!v[0]) strcpy(v, "/");
        char t[8];
        qparam(path, "sub", t, sizeof t); scan_sub = t[0] != '0';
        qparam(path, "shuffle", t, sizeof t); pl_shuffle = t[0] == '1';
        qparam(path, "silence", t, sizeof t); pl_silence = t[0] != '0';
        qparam(path, "min", t, sizeof t); pl_minutes = atoi(t) > 0 ? atoi(t) : 3;
        imp_n = 0; imp_msg[0] = 0;
        for (const char *p = body, *e = body + len; p < e && imp_n < PL_MAX; ) {
            const char *nl = memchr(p, '\n', (size_t)(e - p));
            int l = nl ? (int)(nl - p) : (int)(e - p);
            while (l > 0 && (p[l - 1] == '\r' || p[l - 1] == ' ')) l--;
            if (l > 0) { imp_hash[imp_n] = name_hash_n(p, l); pl[imp_n][0] = 0; imp_n++; }
            p = nl ? nl + 1 : e;
        }
        if (!imp_n) { *olen = snprintf(out, sizeof out, "{\"ok\":false}"); return 400; }
        pl_n = 0; scan_qn = 0; pl_pos = -1; llen = 0; lfull = false; want_goto = -1; scan_drop = false; load_fails = 0;
        if (ls_state == 2) { ls_state = 0; ls_n = 0; }     /* the scan reuses the listing buffer, ls_name points into it */
        if (strcmp(prefs.dir, v)) { strncpy(prefs.dir, v, sizeof prefs.dir - 1); prefs_dirty = true; }
        imp_active = true; np_kill = true;
        DIAG("import: %d Titel, suche in '%s' (sub=%d)", imp_n, v, scan_sub);
        if (ftp_start(J_LIST, v, list_data, NULL)) { jb = JB_SCAN; strcpy(jb_msg, "suche Titel der Playlist ..."); }
        else { imp_active = false; jb = JB_ERR; strcpy(jb_msg, "FTP nicht erreichbar"); }
        *olen = snprintf(out, sizeof out, "{\"ok\":true,\"n\":%d}", imp_n); return 200;
    }
    if (post && !strncmp(path, "/jb/song", 8)) {           /* sub-tune of the playing title */
        char t[8]; qparam(path, "n", t, sizeof t);
        int n = atoi(t);
        if (jb < JB_START || n < 1 || n > cur_songs) { *olen = snprintf(out, sizeof out, "{\"ok\":false}"); return 400; }
        want_song = n;
        *olen = snprintf(out, sizeof out, "{\"ok\":true}"); return 200;
    }
    if (post && !strncmp(path, "/jb/np", 6) && (path[6] == '?' || !path[6])) {
        int code = np_submit(path, olen);
        if (code == 200 && path[6] == '?') net_relay_np(path + 7);   /* on to the next visualizer in the chain */
        return code;
    }
    if (!post && !strcmp(path, "/jb/np")) {
        *olen = np_json(out, (int)sizeof out); return 200;
    }
    if (post && !strcmp(path, "/jb/next")) { want_next = true; *olen = snprintf(out, sizeof out, "{\"ok\":true}"); return 200; }
    if (post && !strcmp(path, "/jb/prev")) { want_prev = true; *olen = snprintf(out, sizeof out, "{\"ok\":true}"); return 200; }
    if (post && !strcmp(path, "/jb/stop")) { want_stop = true; *olen = snprintf(out, sizeof out, "{\"ok\":true}"); return 200; }
    if (post && !strncmp(path, "/jb/shuffle", 11)) {
        char t[8]; qparam(path, "on", t, sizeof t);
        pl_shuffle = t[0] == '1';
        if (pl_n) { int cur = pl_pos >= 0 ? pl_order[pl_pos] : -1; shuffle_order(); for (int i = 0; i < pl_n; i++) if (pl_order[i] == cur) pl_pos = i; }
        *olen = snprintf(out, sizeof out, "{\"ok\":true}"); return 200;
    }
    if (!post && !strcmp(path, "/jb/state")) {
        static const char *st[] = { "idle", "scan", "load", "start", "play", "error" };
        int64_t el = jb == JB_PLAY ? absolute_time_diff_us(cur_start, get_absolute_time()) / 1000000 : 0;
        k += snprintf(out + k, sizeof out - k, "{\"state\":\"%s\",\"msg\":", st[jb]); k += jstr(out + k, (int)sizeof out - k, jb_msg);
        k += snprintf(out + k, sizeof out - k, ",\"pos\":%d,\"count\":%d,\"dur\":%d,\"elapsed\":%ld,\"shuffle\":%s,\"song\":%d,\"songs\":%d,\"title\":",
                      pl_pos, pl_n, cur_dur, (long)el, pl_shuffle ? "true" : "false",
                      jb >= JB_START ? cur_song : 0, jb >= JB_START ? cur_songs : 0);
        k += jstr(out + k, (int)sizeof out - k, jb >= JB_START ? cur_title : "");
        k += snprintf(out + k, sizeof out - k, ",\"author\":"); k += jstr(out + k, (int)sizeof out - k, jb >= JB_START ? cur_author : "");
        k += snprintf(out + k, sizeof out - k, ",\"file\":"); k += jstr(out + k, (int)sizeof out - k, pl_pos >= 0 && pl_n ? pl[pl_order[pl_pos]] : "");
        k += snprintf(out + k, sizeof out - k, ",\"md5\":"); k += jstr(out + k, (int)sizeof out - k, jb >= JB_START ? cur_md5hex : "");
        k += snprintf(out + k, sizeof out - k, ",\"imp\":"); k += jstr(out + k, (int)sizeof out - k, imp_msg);
        k += snprintf(out + k, sizeof out - k, ",\"sidaddr\":"); k += jstr(out + k, (int)sizeof out - k, sa_msg);
        { char t[40]; sa_cfg_text(t, sizeof t); k += snprintf(out + k, sizeof out - k, ",\"sidcfg\":"); k += jstr(out + k, (int)sizeof out - k, t); }
        k += snprintf(out + k, sizeof out - k, ",\"np\":"); k += np_json(out + k, (int)sizeof out - k);   /* tune of the PC jukebox */
        k += snprintf(out + k, sizeof out - k, ",\"prefs\":{\"src\":%d,\"jbsrc\":%d,\"sidauto\":%d,\"sidshow\":%d,\"hvsc\":", prefs.src, prefs.jbsrc, prefs.sidauto, prefs.sidshow); k += jstr(out + k, (int)sizeof out - k, prefs.hvsc);
        k += snprintf(out + k, sizeof out - k, ",\"user\":"); k += jstr(out + k, (int)sizeof out - k, prefs.user);
        k += snprintf(out + k, sizeof out - k, ",\"dir\":"); k += jstr(out + k, (int)sizeof out - k, prefs.dir);
        k += snprintf(out + k, sizeof out - k, "}");
        k += snprintf(out + k, sizeof out - k, ",\"sldb\":{\"count\":%lu,\"busy\":%s,\"chunk\":%s,\"msg\":", (unsigned long)(sldb_valid() ? hdr()->count : 0),
                      (sldb_job || w_open || pc_chunk_len >= 0 || pc_begin || pc_end || stick_begin || sldb_clear_req) ? "true" : "false", pc_chunk_len >= 0 ? "true" : "false");
        k += jstr(out + k, (int)sizeof out - k, sldb_msg);
        k += snprintf(out + k, sizeof out - k, ",\"hvsc\":"); k += jstr(out + k, (int)sizeof out - k, sldb_valid() ? hdr()->hvsc : "");
        k += snprintf(out + k, sizeof out - k, ",\"user\":"); k += jstr(out + k, (int)sizeof out - k, sldb_valid() ? hdr()->user : "");
        k += snprintf(out + k, sizeof out - k, "}}");
        *olen = k; return 200;
    }
    /* song lengths from the USB stick: paths of the user file and the HVSC file */
    if (post && !strncmp(path, "/sldb/stick", 11)) {
        if (F.st != F_IDLE || f_parsing || sldb_job || w_open || jb == JB_SCAN || ls_state == 1) { *olen = snprintf(out, sizeof out, "{\"ok\":false}"); return 409; }
        qparam(path, "user", w_src_user, sizeof w_src_user); qparam(path, "hvsc", w_src_hvsc, sizeof w_src_hvsc);
        if (!w_src_user[0] && !w_src_hvsc[0]) { *olen = snprintf(out, sizeof out, "{\"ok\":false}"); return 400; }
        strncpy(prefs.user, w_src_user, sizeof prefs.user - 1); strncpy(prefs.hvsc, w_src_hvsc, sizeof prefs.hvsc - 1);
        prefs.src = 0; prefs_dirty = true;
        stick_begin = true;                                /* jb_task erases and starts */
        *olen = snprintf(out, sizeof out, "{\"ok\":true}"); return 200;
    }
    /* song lengths from the PC: the browser parses the files and sends binary records */
    if (post && !strncmp(path, "/sldb/begin", 11)) {
        if (sldb_job || w_open) { *olen = snprintf(out, sizeof out, "{\"ok\":false}"); return 409; }
        qparam(path, "user", w_src_user, sizeof w_src_user); qparam(path, "hvsc", w_src_hvsc, sizeof w_src_hvsc);
        pc_begin = true; *olen = snprintf(out, sizeof out, "{\"ok\":true}"); return 200;
    }
    if (post && !strcmp(path, "/sldb/chunk")) {
        if (!body || len <= 0 || pc_chunk_len >= 0) { *olen = snprintf(out, sizeof out, "{\"ok\":false}"); return 409; }
        pc_chunk = (const uint8_t *)body; pc_chunk_len = len;
        *olen = snprintf(out, sizeof out, "{\"ok\":true}"); return 200;
    }
    if (post && !strcmp(path, "/sldb/end")) { pc_end = true; *olen = snprintf(out, sizeof out, "{\"ok\":true}"); return 200; }
    /* erase the song length database (confirmed on the web page); jb_task erases the header sector */
    if (post && !strcmp(path, "/sldb/clear")) {
        if (sldb_job || w_open || pc_chunk_len >= 0 || pc_begin || pc_end || stick_begin) { *olen = snprintf(out, sizeof out, "{\"ok\":false}"); return 409; }
        sldb_clear_req = true;
        *olen = snprintf(out, sizeof out, "{\"ok\":true}"); return 200;
    }
    *olen = snprintf(out, sizeof out, "{\"ok\":false}"); return 404;
}
