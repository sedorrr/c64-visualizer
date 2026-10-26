/* Backup and restore of the settings (WLAN, web password, Ultimates, forwarding, jukebox, buttons): GET /backup,
   POST /restore on another Pico (checked before anything is written, restart afterwards), a damaged file changes
   nothing, a switched SID socket is never restored. One phase per process (argv[1]). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include "net.h"
#include "pico/cyw43_arch.h"
#include "http_client.h"
#include "cfg_v5.h"
extern int64_t FAKE_NOW; extern unsigned char FAKE_FLASH[];
static int fails = 0;
#define EXPECT(c, what) do { if (c) printf("ok   %s\n", what); else { printf("FAIL %s\n", what); fails++; } } while (0)
#define FLASH (4 * 1024 * 1024)
#define CFG_AT (FAKE_FLASH + FLASH - 2 * 4096)
#define SET_AT (FAKE_FLASH + FLASH - 4096)
#define JBP_AT (FAKE_FLASH + 0x0FF000)
static unsigned fnv(const void *d, unsigned long n) { const unsigned char *p = d; unsigned h = 2166136261u; for (unsigned long i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; } return h; }
typedef struct { unsigned magic; unsigned char src, jbsrc, sidauto, sa_sock; unsigned short sa_orig; unsigned char sidshow, pad;
                 char hvsc[100], user[100], dir[160]; unsigned check; } jbp_t;
static void put_device(const char *name, const char *u64, int bright_byte) {   /* AP mode (no WLAN set): no login needed */
    memset(FAKE_FLASH, 0xFF, FLASH);
    v5_t c; memset(&c, 0, sizeof c); c.magic = V5_MAGIC; strcpy(c.name, name); strcpy(c.webpw, "geheim99");
    strcpy(c.u64[0].label, "Elite II"); strcpy(c.u64[0].addr, u64); strcpy(c.u64[0].pw, "u64pw"); strcpy(c.u64[1].addr, "192.168.1.170");
    c.stream = 1; c.src = 1; strcpy(c.fwd, "c64audio.local"); c.check = fnv(&c, offsetof(v5_t, check)); memcpy(CFG_AT, &c, sizeof c);
    jbp_t j; memset(&j, 0, sizeof j); j.magic = 0x4A425032u; j.sidshow = 1;
    int a = !strcmp(name, "wohnzimmer"); j.sidauto = (unsigned char)a; strcpy(j.dir, a ? "/USB2/SID-Tunes/HVSC 78/GAMES" : "/USB0/Andere");
    j.check = fnv(&j, offsetof(jbp_t, check)); memcpy(JBP_AT, &j, sizeof j);
    for (int i = 0; i < 64; i++) SET_AT[i] = (unsigned char)(bright_byte + i);   /* some button-settings record */
}
static char *field(const char *json, const char *key) {      /* "key":"value" -> malloc'd value */
    char k[40]; snprintf(k, sizeof k, "\"%s\":\"", key); const char *p = strstr(json, k); if (!p) return NULL;
    p += strlen(k); const char *e = strchr(p, '"'); char *o = malloc((size_t)(e - p) + 1); memcpy(o, p, (size_t)(e - p)); o[e - p] = 0; return o;
}
static char g_form[40000];
static void take_backup(void) {                              /* from device A */
    put_device("wohnzimmer", "192.168.1.168", 40); net_init("t");
    const char *a = hc_req("GET", "/backup", NULL), *j = hc_body(a);
    EXPECT(strstr(a, "200 OK") && strstr(j, "\"fmt\":\"c64viz-backup\",\"ver\":1") && strstr(j, "\"name\":\"wohnzimmer\""), "backup: format and name");
    char *n = field(j, "netcfg"), *p = field(j, "jbprefs"), *s = field(j, "settings");
    EXPECT(n && strlen(n) == 2 * sizeof(v5_t) && p && strlen(p) == 2 * sizeof(jbp_t) && s && strlen(s) == 8192, "backup: settings, jukebox and buttons in it");
    snprintf(g_form, sizeof g_form, "netcfg=%s&jbprefs=%s&settings=%s", n, p, s);
}
static const char *restore(const char *form) {
    static char ans[400]; const char *a = hc_req("POST", "/restore", form);
    snprintf(ans, sizeof ans, "%s", a); return ans;
}

int main(int argc, char **argv) {
    const char *phase = argc > 1 ? argv[1] : "restore";
    take_backup();
    if (!strcmp(phase, "restore")) {
        printf("== restored on another Pico\n");
        put_device("kueche", "10.0.0.5", 90); net_init("t");
        const char *a = restore(g_form);
        EXPECT(strstr(a, "200 OK"), "accepted");
        REBOOTED = 0; FAKE_NOW += 20000;
        if (!setjmp(REBOOT_JMP)) net_task();
        v5_t c; memcpy(&c, CFG_AT, sizeof c);
        EXPECT(c.magic == V5_MAGIC && !strcmp(c.name, "wohnzimmer") && !strcmp(c.u64[0].addr, "192.168.1.168") && !strcmp(c.u64[0].pw, "u64pw")
               && !strcmp(c.webpw, "geheim99") && c.src == 1 && !strcmp(c.fwd, "c64audio.local"), "WLAN/Ultimate settings are A's");
        jbp_t j; memcpy(&j, JBP_AT, sizeof j);
        EXPECT(j.magic == 0x4A425032u && !strcmp(j.dir, "/USB2/SID-Tunes/HVSC 78/GAMES") && j.sidauto == 1, "jukebox settings are A's");
        EXPECT(SET_AT[0] == 40 && SET_AT[63] == 103, "button settings are A's");
        EXPECT(!REBOOTED, "no restart yet: the texts may follow");
        int r = 0; for (int t = 0; t < 200 && !r; t++) { FAKE_NOW += 100000; if (!setjmp(REBOOT_JMP)) net_task(); else r = 1; }
        EXPECT(r && FAKE_NOW < 40000000, "restart within 15 s even without texts");
    } else if (!strcmp(phase, "reboot")) {
        printf("== restore without texts: restart at once\n");
        put_device("kueche", "10.0.0.5", 90); net_init("t");
        char f[40100]; snprintf(f, sizeof f, "%s&reboot=1", g_form);
        restore(f); REBOOTED = 0;
        int r = 0; if (!setjmp(REBOOT_JMP)) net_task(); else r = 1;
        EXPECT(r, "restarted");
    } else if (!strcmp(phase, "broken")) {
        printf("== damaged or foreign files change nothing\n");
        put_device("kueche", "10.0.0.5", 90); net_init("t");
        unsigned char before[3 * 4096]; memcpy(before, CFG_AT, 4096); memcpy(before + 4096, SET_AT, 4096); memcpy(before + 8192, JBP_AT, 4096);
        char *f = strdup(g_form); char *h = strstr(f, "netcfg=") + 7 + 100; *h = *h == '0' ? '1' : '0';   /* one digit changed */
        EXPECT(strstr(restore(f), "400") && strstr(restore(f), "WLAN"), "netcfg changed: refused");
        f = strdup(g_form); h = strstr(f, "settings=") + 9 + 8; *h = 'x';
        EXPECT(strstr(restore(f), "400") && strstr(restore(f), "Taster"), "settings not hex: refused");
        f = strdup(g_form); h = strstr(f, "jbprefs=") + 8 + 20; *h = *h == 'a' ? 'b' : 'a';
        EXPECT(strstr(restore(f), "400") && strstr(restore(f), "Jukebox"), "jukebox record changed: refused");
        EXPECT(strstr(restore("hello=1"), "400"), "no backup at all: refused");
        FAKE_NOW += 20000; if (!setjmp(REBOOT_JMP)) net_task();
        EXPECT(!memcmp(before, CFG_AT, 4096) && !memcmp(before + 4096, SET_AT, 4096) && !memcmp(before + 8192, JBP_AT, 4096), "flash untouched");
    } else if (!strcmp(phase, "socket")) {
        printf("== a switched SID socket in a backup is not restored\n");
        put_device("kueche", "10.0.0.5", 90); net_init("t");
        jbp_t j; memcpy(&j, JBP_AT, sizeof j); j.sa_sock = 2; j.sa_orig = 0xD420; j.check = fnv(&j, offsetof(jbp_t, check));
        char hx[2 * sizeof j + 1]; for (size_t i = 0; i < sizeof j; i++) sprintf(hx + 2 * i, "%02x", ((unsigned char *)&j)[i]);
        char *f = strdup(g_form), *p = strstr(f, "jbprefs=") + 8; memcpy(p, hx, 2 * sizeof j);
        EXPECT(strstr(restore(f), "200"), "accepted");
        FAKE_NOW += 20000; if (!setjmp(REBOOT_JMP)) net_task();
        jbp_t r; memcpy(&r, JBP_AT, sizeof r);
        EXPECT(r.sa_sock == 0 && r.sa_orig == 0 && r.check == fnv(&r, offsetof(jbp_t, check)), "socket marker cleared, record valid");
    }
    return fails ? 1 : 0;
}
