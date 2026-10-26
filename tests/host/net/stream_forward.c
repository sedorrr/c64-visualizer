/* Stream forwarding to a second visualizer (the Ultimate streams to one address only, multicast clogs the WLAN):
   netcfg v3 -> v4 migration, every packet passed on unchanged, never back to the sender, name lookup, /config
   field, and the receiving Pico ("forwarded" mode) never asks the Ultimate for the stream. One phase per process. */
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include "net.h"
#include "md5.h"
#include "cfg_v5.h"
#include "pico/cyw43_arch.h"
struct tcp_pcb; struct tcp_pcb *test_connect(void); void test_send(struct tcp_pcb *p, const char *d, int split);
void test_udp_from(unsigned char *p, int n, uint32_t from); void test_dns_answer(uint32_t ip);
const char *test_rest_sent(void); extern struct tcp_pcb *LAST_REST;
extern int64_t FAKE_NOW; extern int FAKE_LINK; extern jmp_buf REBOOT_JMP; extern int REBOOTED; extern unsigned char FAKE_FLASH[];
extern unsigned char UDP_SENT[1024]; extern int UDP_SENT_LEN, UDP_SENT_COUNT; extern unsigned short UDP_SENT_PORT;
extern struct { uint32_t addr; } UDP_SENT_TO; extern char DNS_ASKED[64]; extern int DNS_ASK_COUNT;
typedef struct { void *arg; int closed; char out[131072]; int outlen; } fakepcb;
static int fails = 0;
#define EXPECT(c, what) do { if (c) printf("ok   %s\n", what); else { printf("FAIL %s\n", what); fails++; } } while (0)
#define CFG_AT (FAKE_FLASH + 4 * 1024 * 1024 - 2 * 4096)
#define IP(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
static unsigned fnv(const void *d, unsigned long n) { const unsigned char *p = d; unsigned h = 2166136261u; for (unsigned long i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; } return h; }
typedef struct { unsigned magic; char ssid[33], pass[65], u64[64], u64pw[33], webpw[33], name[17]; unsigned char stream, smode, pad; unsigned check; } v3_t;
typedef struct { unsigned magic; char ssid[33], pass[65], u64[64], u64pw[33], webpw[33], name[17]; unsigned char stream, smode, pad; char fwd[64]; unsigned check; } v4_t;
static void put_v4(int smode, const char *fwd) {
    v4_t c; memset(&c, 0, sizeof c); c.magic = 0xC64E7C04u; strcpy(c.ssid, "Fritz"); strcpy(c.u64, "192.168.1.64"); c.stream = 1; c.smode = smode;
    strcpy(c.fwd, fwd); c.check = fnv(&c, offsetof(v4_t, check)); memcpy(CFG_AT, &c, sizeof c);
}
/* Digest login with the default password (the page asks for it in the home WLAN) */
static void hexmd5(const char *in, char out[33]) {
    md5_ctx_t m; uint8_t d[16]; md5_init(&m); md5_update(&m, in, strlen(in)); md5_final(&m, d);
    for (int i = 0; i < 16; i++) sprintf(out + 2 * i, "%02x", d[i]);
}
static char g_auth[600];
static const char *auth(const char *method, const char *uri) {
    static char nonce[64]; static int nc = 0;
    if (!nonce[0]) {                                         /* first request: 401 with the nonce */
        struct tcp_pcb *c = test_connect(); test_send(c, "GET / HTTP/1.1\r\n\r\n", 9);
        char *n = strstr(((fakepcb *)c)->out, "nonce=\""); if (!n) return "";
        n += 7; int k = 0; while (n[k] != '"' && k < 63) { nonce[k] = n[k]; k++; } nonce[k] = 0;
    }
    char ha1[33], ha2[33], r[33], b[300], ncs[9]; snprintf(ncs, sizeof ncs, "%08x", ++nc);
    hexmd5("admin:C64 Visualizer:commodore64", ha1); snprintf(b, sizeof b, "%s:%s", method, uri); hexmd5(b, ha2);
    snprintf(b, sizeof b, "%s:%s:%s:abc:auth:%s", ha1, nonce, ncs, ha2); hexmd5(b, r);
    snprintf(g_auth, sizeof g_auth, "Authorization: Digest username=\"admin\", realm=\"C64 Visualizer\", nonce=\"%s\", uri=\"%s\", "
             "algorithm=MD5, response=\"%s\", qop=auth, nc=%s, cnonce=\"abc\"\r\n", nonce, uri, r, ncs);
    return g_auth;
}
static const char *get(const char *path) {
    struct tcp_pcb *c = test_connect(); char rq[900]; snprintf(rq, sizeof rq, "GET %s HTTP/1.1\r\n%s\r\n", path, auth("GET", path)); test_send(c, rq, 9);
    char *b = strstr(((fakepcb *)c)->out, "\r\n\r\n"); return b ? b + 4 : "";
}
static int post_config(const char *body) {
    struct tcp_pcb *c = test_connect(); static char rq[1400];
    snprintf(rq, sizeof rq, "POST /config HTTP/1.1\r\n%sContent-Length: %d\r\n\r\n%s", auth("POST", "/config"), (int)strlen(body), body);
    REBOOTED = 0;
    if (!setjmp(REBOOT_JMP)) { test_send(c, rq, 40); net_task(); }
    printf("   POST /config %s -> %.70s\n", body, strstr(((fakepcb *)c)->out, "\r\n\r\n") + 4);
    return REBOOTED;
}
static void run(int ms) { for (int t = 0; t < ms; t += 16) { FAKE_NOW += 16000; net_task(); } }
static void up(void) {   /* WLAN connects, this Pico gets 192.168.1.57 */
    net_init("t"); FAKE_LINK = CYW43_LINK_UP; cyw43_state.netif[0].ip.addr = IP(192, 168, 1, 57); FAKE_NOW += 100000; net_task();
}
static unsigned char pkt[770];
static void packet(uint32_t from) { for (int i = 0; i < 770; i++) pkt[i] = (unsigned char)(i * 7 + UDP_SENT_COUNT); test_udp_from(pkt, 770, from); }

int main(int argc, char **argv) {
    const char *phase = argc > 1 ? argv[1] : "forward";
    if (!strcmp(phase, "migrate")) {
        printf("== settings of the previous firmware (v3) are kept, forwarding off\n");
        v3_t o; memset(&o, 0, sizeof o); o.magic = 0xC64E7C03u; strcpy(o.ssid, "Fritz"); strcpy(o.u64, "192.168.1.168"); strcpy(o.name, "kueche");
        o.stream = 1; o.smode = 1; o.check = fnv(&o, offsetof(v3_t, check)); memcpy(CFG_AT, &o, sizeof o);
        up();
        const char *st = get("/status");
        EXPECT(strstr(st, "\"cfgname\":\"kueche\"") && strstr(st, "\"smode\":1") && strstr(st, "\"stream\":true") && strstr(st, "\"u64\":\"192.168.1.168\""), "name, multicast, stream, Ultimate taken over");
        EXPECT(strstr(st, "\"fwd\":\"\",\"fwdip\":\"-\""), "forwarding off");
    } else if (!strcmp(phase, "forward")) {
        printf("== main Pico: Ultimate -> this Pico (unicast) -> 192.168.1.58\n");
        put_v4(0, "192.168.1.58"); up(); run(100);
        UDP_SENT_COUNT = 0; packet(IP(192, 168, 1, 64));
        EXPECT(UDP_SENT_COUNT == 1 && UDP_SENT_TO.addr == IP(192, 168, 1, 58) && UDP_SENT_PORT == 11001, "packet passed on to 192.168.1.58:11001");
        EXPECT(UDP_SENT_LEN == 770 && !memcmp(UDP_SENT, pkt, 770), "unchanged (770 bytes)");
        EXPECT(net_audio_active(), "and used here as well");
        unsigned char two[2] = { 1, 0 }; UDP_SENT_COUNT = 0; test_udp_from(two, 2, IP(192, 168, 1, 64));
        EXPECT(UDP_SENT_COUNT == 1 && UDP_SENT_LEN == 2, "the Ultimate's 2-byte start packet too");
        UDP_SENT_COUNT = 0; packet(IP(192, 168, 1, 58));
        EXPECT(UDP_SENT_COUNT == 0, "never back to the device it came from");
        for (int i = 0; i < 250; i++) { FAKE_NOW += 4000; packet(IP(192, 168, 1, 64)); net_task(); }
        run(1100);
        const char *st = get("/status"); printf("   status: ...%s\n", strstr(st, "\"fwd\""));
        EXPECT(strstr(st, "\"fwd\":\"192.168.1.58\",\"fwdip\":\"192.168.1.58\"") && strstr(st, "\"fpps\":"), "status shows target and rate");
        EXPECT(strstr(st, "\"fwderr\":0"), "no send errors");
        EXPECT(strstr(test_rest_sent(), "audio:start?ip=192.168.1.57:11001") != NULL, "it asked the Ultimate for the stream itself (unicast)");
    } else if (!strcmp(phase, "running")) {
        printf("== after a restart the Ultimate still streams here: never asked, state still \"Stream läuft\"\n");
        put_v4(0, ""); net_init("t"); FAKE_LINK = CYW43_LINK_UP; cyw43_state.netif[0].ip.addr = IP(192, 168, 1, 57);
        for (int i = 0; i < 100; i++) { FAKE_NOW += 4000; packet(IP(192, 168, 1, 64)); net_task(); }
        const char *st = get("/status");
        EXPECT(strstr(st, "\"u64state\":\"Stream läuft\""), "state: stream running (not \"wird gestartet\")");
    } else if (!strcmp(phase, "self")) {
        printf("== target = own address: nothing is sent (no loop)\n");
        put_v4(0, "192.168.1.57"); up(); run(100);
        UDP_SENT_COUNT = 0; packet(IP(192, 168, 1, 64));
        EXPECT(UDP_SENT_COUNT == 0, "not sent to itself");
    } else if (!strcmp(phase, "name")) {
        printf("== target as a name: looked up by mDNS first\n");
        put_v4(0, "c64viz-2.local"); up(); run(100);
        EXPECT(!strcmp(DNS_ASKED, "c64viz-2.local"), "asks for c64viz-2.local");
        UDP_SENT_COUNT = 0; packet(IP(192, 168, 1, 64));
        EXPECT(UDP_SENT_COUNT == 0, "nothing sent before the answer");
        test_dns_answer(IP(192, 168, 1, 59)); run(50);
        UDP_SENT_COUNT = 0; packet(IP(192, 168, 1, 64));
        EXPECT(UDP_SENT_COUNT == 1 && UDP_SENT_TO.addr == IP(192, 168, 1, 59), "then sent to the address found");
        int asked = DNS_ASK_COUNT; run(61000);
        EXPECT(DNS_ASK_COUNT > asked, "asked again after a minute (the address may change)");
    } else if (!strcmp(phase, "receiver")) {
        printf("== second Pico, mode \"forwarded\": never asks the Ultimate (would take the stream away)\n");
        put_v4(2, ""); up();
        run(30000);
        EXPECT(!LAST_REST || !strstr(test_rest_sent(), "audio:start"), "no audio:start in 30 s");
        const char *st = get("/status");
        EXPECT(strstr(st, "\"u64state\":\"wartet auf weitergeleiteten Stream\""), "status: waiting for the forwarded stream");
        for (int i = 0; i < 20; i++) { FAKE_NOW += 4000; packet(IP(192, 168, 1, 57)); }
        net_task();
        EXPECT(net_audio_active(), "packets from the main Pico are used");
        st = get("/status");
        EXPECT(strstr(st, "\"u64state\":\"Stream kommt weitergeleitet\""), "status: stream comes forwarded");
    } else if (!strcmp(phase, "config")) {
        printf("== /config: target and mode saved, bad target refused\n");
        put_v4(0, ""); up();
        EXPECT(post_config("ssid=Fritz&u64=192.168.1.64&stream=1&smode=0&fwd=c64viz/2") == 0, "invalid target refused");
        EXPECT(post_config("ssid=Fritz&u64=192.168.1.64&stream=1&smode=2&fwd=+C64Viz-2.Local+") == 1, "saved, restart");
        v5_t n; memcpy(&n, CFG_AT, sizeof n);
        EXPECT(n.magic == V5_MAGIC && n.check == fnv(&n, offsetof(v5_t, check)), "stored in layout v5");
        EXPECT(!strcmp(n.fwd, "c64viz-2.local") && n.smode == 2 && !strcmp(n.ssid, "Fritz"), "target trimmed + lower case, mode 2");
    } else if (!strcmp(phase, "keep")) {
        printf("== older page without the field: target kept\n");
        put_v4(0, "192.168.1.58"); up();
        EXPECT(post_config("ssid=Fritz&u64=192.168.1.64&stream=1&smode=0") == 1, "saved");
        v5_t n; memcpy(&n, CFG_AT, sizeof n);
        EXPECT(!strcmp(n.fwd, "192.168.1.58"), "target kept");
    }
    return fails ? 1 : 0;
}
