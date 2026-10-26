/* Device name in the network: netcfg v2 -> v4 migration, /config field, access point SSID, DHCP host name,
   mDNS probing with "-2" when another visualizer already uses the name. One phase per process (argv[1]). */
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <stddef.h>
#include "net.h"
#include "mdns_lite.h"
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "cfg_v5.h"
struct tcp_pcb; struct tcp_pcb *test_connect(void); void test_send(struct tcp_pcb *p, const char *d, int split);
extern int64_t FAKE_NOW; extern int FAKE_LINK; extern jmp_buf REBOOT_JMP; extern int REBOOTED; extern unsigned char FAKE_FLASH[];
extern char FAKE_AP_SSID[40]; extern unsigned char UDP_SENT[1024]; extern int UDP_SENT_LEN;
extern void (*MDNSRECV)(void*, struct udp_pcb*, struct pbuf*, const ip_addr_t*, u16_t);
typedef struct { void *arg; int closed; char out[131072]; int outlen; } fakepcb;
static int fails = 0;
#define EXPECT(c, what) do { if (c) printf("ok   %s\n", what); else { printf("FAIL %s\n", what); fails++; } } while (0)
#define CFG_AT (FAKE_FLASH + 4 * 1024 * 1024 - 2 * 4096)
static unsigned fnv(const void *d, unsigned long n) { const unsigned char *p = d; unsigned h = 2166136261u; for (unsigned long i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; } return h; }
typedef struct { unsigned magic; char ssid[33], pass[65], u64[64], u64pw[33], webpw[33]; unsigned char stream, smode, pad; unsigned check; } v2_t;
typedef struct { unsigned magic; char ssid[33], pass[65], u64[64], u64pw[33], webpw[33], name[17]; unsigned char stream, smode, pad; unsigned check; } v3_t;
typedef struct { unsigned magic; char ssid[33], pass[65], u64[64], u64pw[33], webpw[33], name[17]; unsigned char stream, smode, pad; char fwd[64]; unsigned check; } v4_t;
static void put_v3(const char *ssid, const char *name) {
    v3_t c; memset(&c, 0, sizeof c); c.magic = 0xC64E7C03u; strcpy(c.ssid, ssid); strcpy(c.u64, "192.168.1.168"); strcpy(c.name, name); c.stream = 1;
    c.check = fnv(&c, offsetof(v3_t, check)); memcpy(CFG_AT, &c, sizeof c);
}
static const char *get(const char *path) {
    struct tcp_pcb *c = test_connect(); char rq[200]; snprintf(rq, sizeof rq, "GET %s HTTP/1.1\r\n\r\n", path); test_send(c, rq, 9);
    char *b = strstr(((fakepcb *)c)->out, "\r\n\r\n"); return b ? b + 4 : "";
}
static int post_config(const char *body) {
    struct tcp_pcb *c = test_connect(); static char rq[600];
    snprintf(rq, sizeof rq, "POST /config HTTP/1.1\r\nContent-Length: %d\r\n\r\n%s", (int)strlen(body), body);
    REBOOTED = 0;
    if (!setjmp(REBOOT_JMP)) { test_send(c, rq, 40); net_task(); }
    printf("   POST /config %s -> %.60s\n", body, strstr(((fakepcb *)c)->out, "\r\n\r\n") + 4);
    return REBOOTED;
}
static void mdns_in(const uint8_t *m, int n) { struct pbuf b = { 0, (void *)m, (u16_t)n, (u16_t)n }; ip_addr_t a; a.addr = 0; MDNSRECV(NULL, NULL, &b, &a, 5353); }
static int sent_is_query_for(const char *name) {               /* last UDP packet: our probe "A <name>.local?" */
    if (UDP_SENT_LEN < 12 || (UDP_SENT[2] & 0x80)) return 0;
    char got[40]; int k = 12, o = 0; while (UDP_SENT[k] && o < 38) { int l = UDP_SENT[k++]; memcpy(got + o, UDP_SENT + k, l); o += l; k += l; got[o++] = '.'; }
    got[o ? o - 1 : 0] = 0; char want[40]; snprintf(want, sizeof want, "%s.local", name); return !strcmp(got, want);
}
/* an answer "A <name>.local = 192.168.1.231" from the other visualizer */
static int other_answer(uint8_t *r, const char *name) {
    int k = 0; memset(r, 0, 12); r[2] = 0x84; r[7] = 1; k = 12;
    const char *s = name; r[k++] = (uint8_t)strlen(s); memcpy(r + k, s, strlen(s)); k += strlen(s);
    r[k++] = 5; memcpy(r + k, "local", 5); k += 5; r[k++] = 0;
    r[k++] = 0; r[k++] = 1; r[k++] = 0x80; r[k++] = 1; r[k++] = 0; r[k++] = 0; r[k++] = 0; r[k++] = 120; r[k++] = 0; r[k++] = 4;
    r[k++] = 192; r[k++] = 168; r[k++] = 3; r[k++] = 231; return k;
}
static void run(int ms) { for (int t = 0; t < ms; t += 16) { FAKE_NOW += 16000; net_task(); } }

int main(int argc, char **argv) {
    const char *phase = argc > 1 ? argv[1] : "migrate";
    char m[120];
    if (!strcmp(phase, "migrate")) {
        printf("== settings of the previous firmware (v2) are kept, name = default\n");
        v2_t o; memset(&o, 0, sizeof o); o.magic = 0xC64E7C02u; strcpy(o.u64, "192.168.1.168"); strcpy(o.webpw, "geheim99"); o.stream = 1; o.smode = 0;
        o.check = fnv(&o, offsetof(v2_t, check)); memcpy(CFG_AT, &o, sizeof o);   /* no ssid: own access point, no login */
        net_init("x"); while (net_take_message(m, sizeof m)) ;
        const char *st = get("/status"); printf("   status: ...%s\n", strstr(st, "\"name\""));
        EXPECT(strstr(st, "\"name\":\"c64viz\",\"cfgname\":\"\""), "name: c64viz (default)");
        EXPECT(!strcmp(FAKE_AP_SSID, "C64-Visualizer"), "access point keeps its old name");
        EXPECT(strstr(st, "\"u64\":\"192.168.1.168\"") && strstr(st, "\"stream\":true") && strstr(st, "\"pwdefault\":false"), "Ultimate, stream and web password taken over");
        EXPECT(post_config("ssid=&u64=192.168.1.168&stream=1&smode=0&name=Wohn+zimmer") == 0, "invalid name refused, no restart");
        EXPECT(post_config("ssid=&u64=192.168.1.168&stream=1&smode=0&name=-x") == 0, "name starting with - refused");
        EXPECT(post_config("ssid=&u64=192.168.1.168&stream=1&smode=0&name=Wohnzimmer") == 1, "valid name saved, restart");
        v5_t n; memcpy(&n, CFG_AT, sizeof n);
        { v5_t n5; memcpy(&n5, CFG_AT, sizeof n5);
          EXPECT(n5.magic == V5_MAGIC && n5.check == fnv(&n5, offsetof(v5_t, check)), "stored in the current layout (v5)"); }
        EXPECT(!strcmp(n.name, "wohnzimmer") && !strcmp(n.webpw, "geheim99") && n.stream == 1, "name in lower case, password still there");
    } else if (!strcmp(phase, "default") || !strcmp(phase, "nofield")) {   /* one save per process (restart) */
        put_v3("", "kueche");
        net_init("x");
        bool def = !strcmp(phase, "default");
        EXPECT(post_config(def ? "ssid=&u64=192.168.1.168&stream=1&smode=0&name=C64VIZ" : "ssid=&u64=192.168.1.168&stream=1&smode=0") == 1, "saved");
        v5_t n; memcpy(&n, CFG_AT, sizeof n);
        if (def) EXPECT(n.name[0] == 0, "C64VIZ = the default: stored as empty");
        else EXPECT(!strcmp(n.name, "kueche"), "form without the name field (older page): name kept");
    } else if (!strcmp(phase, "ap")) {
        printf("== own access point with a name\n");
        put_v3("", "test");
        net_init("x"); net_take_message(m, sizeof m);
        printf("   access point: %s, matrix: %s\n", FAKE_AP_SSID, m);
        EXPECT(!strcmp(FAKE_AP_SSID, "C64-Visualizer-test"), "access point C64-Visualizer-test");
        EXPECT(strstr(m, "C64-Visualizer-test 192.168.4.1") != NULL, "matrix shows it");
        EXPECT(strstr(get("/status"), "\"wifi\":\"eigener Zugangspunkt C64-Visualizer-test\""), "status shows it");
    } else if (!strcmp(phase, "free") || !strcmp(phase, "taken")) {
        bool taken = !strcmp(phase, "taken");
        printf("== home WLAN, name %s\n", taken ? "already used by another visualizer" : "free");
        put_v3("MeinWLAN", "test");
        net_init("x");
        EXPECT(cyw43_state.netif[0].hostname && !strcmp(cyw43_state.netif[0].hostname, "test"), "DHCP host name test (the router's list)");
        FAKE_LINK = CYW43_LINK_UP; cyw43_state.netif[0].ip.addr = 224u << 24 | 1u << 16 | 168u << 8 | 192u;
        FAKE_NOW += 100000; net_task(); FAKE_NOW += 16000; net_task();
        EXPECT(sent_is_query_for("test"), "first asks whether test.local is taken");
        EXPECT(!net_take_message(m, sizeof m), "no message before the name is settled");
        if (taken) { uint8_t r[80]; int k = other_answer(r, "test"); mdns_in(r, k); }
        run(300);
        if (taken) {
            EXPECT(sent_is_query_for("test-2"), "taken: asks for test-2 next");
            run(3000);
            EXPECT(!strcmp(mdnsl_name(), "test-2") && mdnsl_ready(), "now reachable as test-2.local");
        } else {
            run(3000);
            EXPECT(!strcmp(mdnsl_name(), "test") && mdnsl_ready(), "reachable as test.local");
            EXPECT(UDP_SENT[2] & 0x80, "announced its address afterwards");
        }
        int got = 0; while (net_take_message(m, sizeof m)) got = 1;
        printf("   matrix: %s\n", got ? m : "-");
        EXPECT(got && strstr(m, taken ? "test-2.local" : "test.local") && strstr(m, "192.168.1.224"), "matrix shows IP and name once");
        if (taken) {                                           /* the other one asks for "test" later: we do not answer */
            uint8_t q[64]; int k = 12; memset(q, 0, 12); q[5] = 1; q[k++] = 4; memcpy(q + k, "test", 4); k += 4; q[k++] = 5; memcpy(q + k, "local", 5); k += 5; q[k++] = 0; q[k++] = 0; q[k++] = 1; q[k++] = 0; q[k++] = 1;
            UDP_SENT_LEN = 0; mdns_in(q, k);
            EXPECT(UDP_SENT_LEN == 0, "questions for test.local are left to the other device");
            k = 12; q[k++] = 6; memcpy(q + k, "test-2", 6); k += 6; q[k++] = 5; memcpy(q + k, "local", 5); k += 5; q[k++] = 0; q[k++] = 0; q[k++] = 1; q[k++] = 0; q[k++] = 1;
            mdns_in(q, k);
            EXPECT(UDP_SENT_LEN > 0 && UDP_SENT[2] & 0x80, "questions for test-2.local are answered");
        }
    }
    printf(fails ? "FAILED: %d\n" : "all device-name checks passed\n", fails);
    return fails ? 1 : 0;
}
