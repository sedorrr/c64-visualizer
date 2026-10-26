/* WLAN trouble: the own access point (fallback when the home WLAN was not found at the start) tries the home WLAN
   again every 5 minutes - not while someone uses the page there; a lost link in the home WLAN is joined again
   without falling back to the access point. At the start, "at the router but no DHCP address" (NOIP) is joined
   again a few times before the access point; a refused password goes there at once. One phase per process (argv[1]). */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include "net.h"
#include "pico/cyw43_arch.h"
struct tcp_pcb; struct tcp_pcb *test_connect(void); void test_send(struct tcp_pcb *p, const char *d, int split);
const char *test_rest_sent(void); extern struct tcp_pcb *LAST_REST;
extern int64_t FAKE_NOW; extern int FAKE_LINK; extern unsigned char FAKE_FLASH[]; extern char FAKE_AP_SSID[40];
extern int FAKE_AP_ON, FAKE_AP_STARTS, FAKE_CONNECTS, FAKE_LEAVES;
typedef struct { void *arg; int closed; char out[131072]; int outlen; } fakepcb;
static int fails = 0;
#define EXPECT(c, what) do { if (c) printf("ok   %s\n", what); else { printf("FAIL %s\n", what); fails++; } } while (0)
#define CFG_AT (FAKE_FLASH + 4 * 1024 * 1024 - 2 * 4096)
static unsigned fnv(const void *d, unsigned long n) { const unsigned char *p = d; unsigned h = 2166136261u; for (unsigned long i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; } return h; }
typedef struct { unsigned magic; char ssid[33], pass[65], u64[64], u64pw[33], webpw[33], name[17]; unsigned char stream, smode, pad; char fwd[64]; unsigned check; } v4_t;
static void put_cfg(void) {
    v4_t c; memset(&c, 0, sizeof c); c.magic = 0xC64E7C04u; strcpy(c.ssid, "Fritz"); strcpy(c.u64, "192.168.1.64"); c.stream = 1;
    c.check = fnv(&c, offsetof(v4_t, check)); memcpy(CFG_AT, &c, sizeof c);
}
static void run_s(double s) { for (double t = 0; t < s; t += 0.1) { FAKE_NOW += 100000; net_task(); } }
static int status_has(const char *what) {           /* on the access point the page needs no password */
    struct tcp_pcb *c = test_connect(); test_send(c, "GET /status HTTP/1.1\r\n\r\n", 9);
    return strstr(((fakepcb *)c)->out, what) != NULL;
}
static int messages(void) { char m[120]; int n = 0; while (net_take_message(m, sizeof m)) n++; return n; }

int main(int argc, char **argv) {
    const char *phase = argc > 1 ? argv[1] : "ap";
    put_cfg();
    if (!strcmp(phase, "ap")) {
        printf("== home WLAN not found at the start -> own access point, back to the home WLAN later\n");
        FAKE_LINK = CYW43_LINK_NONET; net_init("t"); messages(); run_s(4);
        EXPECT(!FAKE_AP_ON && FAKE_CONNECTS == 2 && FAKE_LEAVES == 1, "not found: left and joined once more (a scan can miss it)");
        run_s(2);
        EXPECT(FAKE_AP_ON && !strcmp(FAKE_AP_SSID, "C64-Visualizer") && FAKE_CONNECTS == 2, "still not found: own access point");
        EXPECT(messages() == 1, "matrix shows the access point");
        FAKE_LINK = 0;                                   /* the next tries hang in "joining" until the 20 s timeout */
        int c0 = FAKE_CONNECTS; run_s(290);
        EXPECT(FAKE_CONNECTS == c0 && FAKE_AP_ON, "4:50 later still the access point (no retry yet)");
        run_s(15);
        EXPECT(FAKE_CONNECTS == c0 + 1 && !FAKE_AP_ON, "after 5 min: access point off, joins the home WLAN again");
        run_s(25);
        EXPECT(FAKE_AP_ON && FAKE_AP_STARTS == 2, "still not there: back to the access point after 20 s");
        EXPECT(messages() == 0, "no second access point message on the matrix");
        run_s(300);
        EXPECT(FAKE_CONNECTS == c0 + 2, "next try 5 min later");
        FAKE_LINK = CYW43_LINK_UP; cyw43_state.netif[0].ip.addr = 57u << 24 | 1u << 16 | 168u << 8 | 192u; run_s(1);
        EXPECT(!FAKE_AP_ON && cyw43_state.netif[0].hostname && !strcmp(cyw43_state.netif[0].hostname, "c64viz"), "home WLAN found: host name set again");
        EXPECT(LAST_REST && strstr(test_rest_sent(), "audio:start?ip=192.168.1.57:11001"), "stream requested from the Ultimate");
        run_s(1);
        struct tcp_pcb *c = test_connect(); test_send(c, "GET /status HTTP/1.1\r\n\r\n", 9);
        EXPECT(strstr(((fakepcb *)c)->out, "401") != NULL, "page needs the password again (home WLAN)");
    } else if (!strcmp(phase, "busy")) {
        printf("== someone sets the Pico up on its access point: never cut off\n");
        FAKE_LINK = CYW43_LINK_NONET; net_init("t"); run_s(6);
        int c0 = FAKE_CONNECTS;
        for (int i = 0; i < 9; i++) { run_s(60); EXPECT(status_has("\"sta\":false"), i == 0 ? "page used every minute on the access point" : "  ... page used"); }
        EXPECT(FAKE_CONNECTS == c0 && FAKE_AP_ON, "9 min in use: no retry, access point stays");
        run_s(240);
        EXPECT(FAKE_CONNECTS == c0 && FAKE_AP_ON, "4 min after the last use: still waiting");
        run_s(70);
        EXPECT(FAKE_CONNECTS == c0 + 2 && FAKE_AP_ON, "5 min quiet: tries the home WLAN (twice, not found: access point again)");
    } else if (!strcmp(phase, "lost")) {
        printf("== in the home WLAN, link lost (router restarts): joins again, no access point\n");
        FAKE_LINK = CYW43_LINK_UP; cyw43_state.netif[0].ip.addr = 57u << 24 | 1u << 16 | 168u << 8 | 192u;
        net_init("t"); run_s(2);
        int c0 = FAKE_CONNECTS;
        FAKE_LINK = 0; run_s(8);
        EXPECT(FAKE_CONNECTS == c0, "8 s without link: waits (short dropouts happen)");
        run_s(4);
        EXPECT(FAKE_CONNECTS == c0 + 1 && FAKE_LEAVES == 1, "after 10 s: leaves, joins again a second later");
        FAKE_LINK = CYW43_LINK_NONET; run_s(60);
        EXPECT(!FAKE_AP_ON && FAKE_AP_STARTS == 0, "router still away for a minute: no access point");
        EXPECT(FAKE_CONNECTS >= c0 + 3 && FAKE_CONNECTS <= c0 + 14, "keeps trying (every 5-20 s, not in a loop)");
        struct tcp_pcb *c = test_connect(); test_send(c, "GET /status HTTP/1.1\r\n\r\n", 9);
        EXPECT(strstr(((fakepcb *)c)->out, "401") != NULL, "page needs the password while reconnecting");
        FAKE_LINK = CYW43_LINK_UP; run_s(2);
        EXPECT(LAST_REST && strstr(test_rest_sent(), "audio:start"), "back: stream requested again");
        int c1 = FAKE_CONNECTS; run_s(60);
        EXPECT(FAKE_CONNECTS == c1, "stays connected");
    } else if (!strcmp(phase, "noip")) {
        printf("== start: at the router, but DHCP never answers -> joins again, access point only after 3 joins\n");
        FAKE_LINK = CYW43_LINK_NOIP; net_init("t"); messages(); run_s(9);
        EXPECT(FAKE_CONNECTS == 1 && FAKE_LEAVES == 0 && !FAKE_AP_ON, "9 s without address: waits");
        run_s(3);
        EXPECT(FAKE_CONNECTS == 2 && FAKE_LEAVES == 1 && !FAKE_AP_ON, "after 10 s: leaves and joins again (no access point)");
        run_s(18);
        EXPECT(FAKE_CONNECTS == 3 && FAKE_LEAVES == 2 && !FAKE_AP_ON, "30 s: third join, still no access point");
        run_s(5);
        EXPECT(FAKE_AP_ON && FAKE_AP_STARTS == 1 && FAKE_CONNECTS == 3, "3 joins without address: own access point");
        EXPECT(messages() == 1, "matrix shows the access point");
    } else if (!strcmp(phase, "noip_ok")) {
        printf("== start: DHCP hangs (seen after a firmware update), the second join gets an address\n");
        FAKE_LINK = CYW43_LINK_NOIP; net_init("t"); run_s(12);
        EXPECT(FAKE_CONNECTS == 2 && !FAKE_AP_ON, "hung in NOIP: joined again");
        FAKE_LINK = CYW43_LINK_UP; cyw43_state.netif[0].ip.addr = 57u << 24 | 1u << 16 | 168u << 8 | 192u; run_s(2);
        EXPECT(FAKE_AP_STARTS == 0, "never on the access point");
        EXPECT(LAST_REST && strstr(test_rest_sent(), "audio:start?ip=192.168.1.57:11001"), "in the home WLAN: stream requested");
        int c1 = FAKE_CONNECTS; run_s(60);
        EXPECT(FAKE_CONNECTS == c1 && FAKE_AP_STARTS == 0, "stays connected");
    } else if (!strcmp(phase, "join")) {
        printf("== start: never gets past joining -> access point after 20 s, no second join\n");
        FAKE_LINK = CYW43_LINK_JOIN; net_init("t"); run_s(19);
        EXPECT(!FAKE_AP_ON && FAKE_CONNECTS == 1, "19 s joining: waits");
        run_s(2);
        EXPECT(FAKE_AP_ON && FAKE_CONNECTS == 1 && FAKE_LEAVES == 0, "20 s: own access point");
    } else if (!strcmp(phase, "badauth")) {
        printf("== start: password refused -> access point at once (no further joins)\n");
        FAKE_LINK = CYW43_LINK_BADAUTH; net_init("t"); run_s(1.5);
        EXPECT(!FAKE_AP_ON, "1.5 s: not yet (the driver can turn a bad auth back to good)");
        run_s(1);
        EXPECT(FAKE_AP_ON && FAKE_CONNECTS == 1 && FAKE_LEAVES == 0, "refused for 2 s: own access point, joined once");
    } else if (!strcmp(phase, "authflip")) {
        printf("== start: a bad auth that turns good (driver) -> no access point\n");
        FAKE_LINK = CYW43_LINK_BADAUTH; net_init("t"); run_s(1);
        FAKE_LINK = CYW43_LINK_JOIN; run_s(1);
        FAKE_LINK = CYW43_LINK_UP; cyw43_state.netif[0].ip.addr = 57u << 24 | 1u << 16 | 168u << 8 | 192u; run_s(1);
        EXPECT(FAKE_AP_STARTS == 0 && FAKE_CONNECTS == 1, "connected, never on the access point");
    } else if (!strcmp(phase, "lostnoip")) {
        printf("== in the home WLAN, then no address for 2 min (DHCP lease lost): joins again and again, no access point\n");
        FAKE_LINK = CYW43_LINK_UP; cyw43_state.netif[0].ip.addr = 57u << 24 | 1u << 16 | 168u << 8 | 192u;
        net_init("t"); run_s(2);
        int c0 = FAKE_CONNECTS;
        FAKE_LINK = CYW43_LINK_NOIP; run_s(120);
        EXPECT(FAKE_AP_STARTS == 0, "never the access point");
        EXPECT(FAKE_CONNECTS >= c0 + 5 && FAKE_CONNECTS <= c0 + 12 && FAKE_LEAVES == FAKE_CONNECTS - c0, "left and joined every ~11 s");
        FAKE_LINK = CYW43_LINK_UP; run_s(2);
        int c1 = FAKE_CONNECTS; run_s(60);
        EXPECT(FAKE_CONNECTS == c1, "address back: stays connected");
    }
    return fails ? 1 : 0;
}
