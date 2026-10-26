/* The devices working together: beacons and the device list (devices.c), titles of the PC jukebox along the stream
   chain (C64NP1), two matrices as one picture (wide.c, lead and follow). One phase per process (argv[1]). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "net.h"
#include "wide.h"
#include "display.h"
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "http_client.h"
#include "cfg_v5.h"
void test_udp_from(unsigned char *p, int n, uint32_t from);
extern int64_t FAKE_NOW; extern int FAKE_LINK; extern unsigned char FAKE_FLASH[];
extern uint32_t FAKE_SHOWN[PANEL_PHYS_W * PANEL_H]; extern int FAKE_SHOW_N, FAKE_OVR_ON, FAKE_OVR_PCT, FAKE_OVR_DITHER; extern float FAKE_OVR_LIGHT;
static int fails = 0;
#define EXPECT(c, what) do { if (c) printf("ok   %s\n", what); else { printf("FAIL %s\n", what); fails++; } } while (0)
#define CFG_AT (FAKE_FLASH + 4 * 1024 * 1024 - 2 * 4096)
#define IP(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
static unsigned fnv(const void *d, unsigned long n) { const unsigned char *p = d; unsigned h = 2166136261u; for (unsigned long i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; } return h; }
static void put_v5(int stream, int smode, const char *fwd) {
    v5_t c; memset(&c, 0, sizeof c); c.magic = V5_MAGIC; strcpy(c.ssid, "Fritz"); c.stream = stream; c.smode = smode;
    strcpy(c.u64[0].label, "Elite II"); strcpy(c.u64[0].addr, "192.168.1.64"); strcpy(c.fwd, fwd); strcpy(c.name, "wohnzimmer");
    c.check = fnv(&c, offsetof(v5_t, check)); memcpy(CFG_AT, &c, sizeof c);
}
static void run_ms(int ms) { for (int t = 0; t < ms; t += 10) { FAKE_NOW += 10000; net_task(); } }
static void up(void) { net_init("t"); FAKE_LINK = CYW43_LINK_UP; cyw43_state.netif[0].ip.addr = IP(192, 168, 1, 57); FAKE_NOW += 100000; net_task(); char m[120]; while (net_take_message(m, sizeof m)) ; }
/* the latest datagram to a port and receiver whose payload starts with a prefix (NULL = any), from log entry `from` on */
static const udp_log_t *sent(int from, uint16_t port, uint32_t to, const char *prefix) {
    const udp_log_t *r = NULL;
    for (int i = from; i < UDP_LOG_N; i++) { const udp_log_t *l = &UDP_LOG[i % 256];
        if (l->port == port && (!to || l->to == to) && (!prefix || !memcmp(l->d, prefix, strlen(prefix)))) r = l; }
    return r;
}
static int count(int from, uint16_t port, const char *prefix) {
    int n = 0; for (int i = from; i < UDP_LOG_N; i++) if (UDP_LOG[i % 256].port == port && !memcmp(UDP_LOG[i % 256].d, prefix, strlen(prefix))) n++;
    return n;
}
static void beacon_in(uint32_t from, const char *json) {
    char b[600]; int n = snprintf(b, sizeof b, "C64DEV1 %s", json); test_udp_port(11064, (const uint8_t *)b, n, from);
}
static uint32_t pix(int x, int y) { return (uint32_t)(x + 1) << 16 | (uint32_t)(y + 1) << 8 | 0x5A; }

int main(int argc, char **argv) {
    const char *phase = argc > 1 ? argv[1] : "devices";
    if (!strcmp(phase, "devices")) {
        printf("== device list: beacons, query, foreign devices, forgetting\n");
        put_v5(1, 2, "c64viz2.local"); up();
        int l0 = 0; run_ms(200);
        const udp_log_t *q = sent(l0, 11064, 0xFFFFFFFFu, "C64DEV1?"), *b = sent(l0, 11064, 0xFFFFFFFFu, "C64DEV1 {");
        EXPECT(q && q->len == 8, "at the start: a query to everybody (broadcast, port 11064)");
        char bj[800] = ""; if (b) { memcpy(bj, b->d, (size_t)b->len); bj[b->len] = 0; }
        printf("   beacon: %s\n", bj);
        EXPECT(b && strstr(bj, "\"t\":\"viz\"") && strstr(bj, "\"n\":\"wohnzimmer\"") && strstr(bj, "\"ip\":\"192.168.1.57\""), "and its own beacon: type, name, address");
        EXPECT(strstr(bj, "\"img\":\"c64viz_wifi_ws2812_usb-350mA\""), "beacon names the firmware file it needs");
        EXPECT(strstr(bj, "\"st\":1,\"sm\":2") && strstr(bj, "\"fw\":\"c64viz2.local\"") && strstr(bj, "\"src\":\"192.168.1.64\",\"srcl\":\"Elite II\""), "stream, receiver mode, forwarding, source");
        EXPECT(strstr(bj, "\"wide\":\"\""), "two matrices: off");
        int l1 = UDP_LOG_N; run_ms(9000);
        EXPECT(count(l1, 11064, "C64DEV1 {") == 0, "no beacon before 9.5 s");
        run_ms(2000);
        EXPECT(count(l1, 11064, "C64DEV1 {") == 1, "the next one after about 10 s");
        beacon_in(IP(192, 168, 1, 79), "{\"t\":\"audio\",\"n\":\"c64audio\",\"ip\":\"192.168.1.79\",\"fw\":\"wohnzimmer.local\",\"fp\":250}");
        beacon_in(IP(192, 168, 1, 80), "{\"t\":\"viz\",\"n\":\"bad\"");                        /* not one JSON object */
        beacon_in(IP(192, 168, 1, 81), "{\"t\":\"viz\",\"n\":\"ctl\x01\"}");                  /* control character */
        beacon_in(IP(192, 168, 1, 57), "{\"t\":\"viz\",\"n\":\"me-again\"}");                 /* our own address */
        const char *d = hc_body(hc_req("GET", "/devices", NULL));
        printf("   /devices: %.300s\n", d);
        EXPECT(strstr(d, "{\"list\":[{\"self\":true,\"age\":0,\"j\":\"{\\\"t\\\":\\\"viz\\\",\\\"n\\\":\\\"wohnzimmer\\\""), "list starts with this device (as JSON string)");
        EXPECT(strstr(d, "\\\"n\\\":\\\"c64audio\\\"") && strstr(d, "\\\"fw\\\":\\\"wohnzimmer.local\\\""), "the audio receiver is in the list");
        EXPECT(!strstr(d, "bad") && !strstr(d, "ctl") && !strstr(d, "me-again"), "broken beacons and our own address are not");
        int l2 = UDP_LOG_N; test_udp_port(11064, (const uint8_t *)"C64DEV1?", 8, IP(192, 168, 1, 79)); run_ms(500);
        EXPECT(count(l2, 11064, "C64DEV1 {") == 1, "a query from another device: answered within 0.5 s");
        int l3 = UDP_LOG_N; hc_req("POST", "/devices/query", ""); run_ms(100);
        EXPECT(count(l3, 11064, "C64DEV1?") == 1 && count(l3, 11064, "C64DEV1 {") == 1, "web page \"Neu suchen\": query sent, own beacon too");
        run_ms(36000);
        d = hc_body(hc_req("GET", "/devices", NULL));
        EXPECT(!strstr(d, "c64audio") && strstr(d, "wohnzimmer"), "a device silent for 35 s is forgotten");
        for (int i = 0; i < 20; i++) { char j[80]; snprintf(j, sizeof j, "{\"t\":\"viz\",\"n\":\"dev%d\"}", i); beacon_in(IP(192, 168, 1, 100 + i), j); }
        d = hc_body(hc_req("GET", "/devices", NULL)); int n = 0; for (const char *p = d; (p = strstr(p, "\"age\"")); p++) n++;
        EXPECT(n == 13 && strstr(d, "dev19"), "at most 12 others (the oldest makes room), list stays complete JSON");
    } else if (!strcmp(phase, "relay")) {
        printf("== titles of the PC jukebox along the stream chain\n");
        put_v5(1, 0, "192.168.1.60"); up(); run_ms(100);
        int l0 = UDP_LOG_N;
        const char *a = hc_req("POST", "/jb/np?new=1&title=Commando&author=Rob+Hubbard&secs=200&sid=D400", "");
        const udp_log_t *r = sent(l0, 11001, IP(192, 168, 1, 60), "C64NP1 ");
        char p[800] = ""; if (r) { memcpy(p, r->d, (size_t)r->len); p[r->len] = 0; }
        printf("   sent on: %s\n", p);
        EXPECT(strstr(a, "200 OK") && r && !strcmp(p, "C64NP1 new=1&title=Commando&author=Rob+Hubbard&secs=200&sid=D400"), "accepted and sent on unchanged to the next Pico");
        run_ms(50);
        int l1 = UDP_LOG_N;
        hc_req("POST", "/jb/np?title=Commando&secs=200&el=30", ""); run_ms(50);
        EXPECT(sent(l1, 11001, IP(192, 168, 1, 60), "C64NP1 title=Commando&secs=200&el=30"), "clock updates too");
        /* the next Pico: a title arrives from the one before it */
        static unsigned char m[200]; int k = snprintf((char *)m, sizeof m, "C64NP1 new=1&title=Delta&author=Rob+Hubbard&secs=100");
        int l2 = UDP_LOG_N;
        test_udp_from(m, k, IP(192, 168, 1, 79)); run_ms(50);
        const char *np = hc_body(hc_req("GET", "/jb/np", NULL));
        printf("   /jb/np: %s\n", np);
        EXPECT(strstr(np, "\"on\":true,\"title\":\"Delta\",\"author\":\"Rob Hubbard\",\"dur\":100"), "a relayed title is shown like one from the PC");
        EXPECT(!net_audio_active(), "it is not taken for audio");
        EXPECT(sent(l2, 11001, IP(192, 168, 1, 60), "C64NP1 new=1&title=Delta"), "and goes on along the chain");
        put_v5(1, 0, "");                                      /* (flash only - this process keeps its settings) */
    } else if (!strcmp(phase, "lead")) {
        printf("== two matrices: this Pico draws, partner 192.168.1.60, this matrix on the left\n");
        wide_config(WIDE_LEAD, false, "192.168.1.60", 12);
        put_v5(0, 0, ""); up();
        int l0 = UDP_LOG_N; run_ms(1100);
        EXPECT(count(l0, 11065, "C64WQ") >= 1 && sent(l0, 11065, IP(192, 168, 1, 60), "C64WQ"), "asks the partner (C64WQ) about once a second");
        static uint32_t fb[PANEL_W_MAX * PANEL_H];
        for (int i = 0; i < PANEL_PHYS_W * PANEL_H; i++) fb[i] = 0x111111;
        display_show(fb);
        EXPECT(g_panel_w == PANEL_PHYS_W && FAKE_SHOWN[0] == 0x111111, "no answer: draws for its own matrix only");
        test_udp_port(11065, (const uint8_t *)"C64WA", 5, IP(192, 168, 1, 60));
        display_show(fb);
        EXPECT(g_panel_w == 2 * PANEL_PHYS_W, "partner answers: the next picture is 64 wide");
        int l1 = UDP_LOG_N;
        for (int f = 0; f < 3; f++) {
            for (int y = 0; y < PANEL_H; y++) for (int x = 0; x < 2 * PANEL_PHYS_W; x++) fb[y * 2 * PANEL_PHYS_W + x] = pix(x, y) + (uint32_t)f;
            display_show(fb);
            if (f == 0) EXPECT(FAKE_SHOWN[0] == pix(0, 0) && FAKE_SHOWN[PANEL_PHYS_W * 3 + 5] == pix(5, 3), "own matrix: the left half");
        }
        EXPECT(FAKE_SHOWN[0] == pix(0, 0) + 0, "own half 2 frames late (the partner keeps that much in reserve)");
        const udp_log_t *w = sent(l1, 11065, IP(192, 168, 1, 60), "C64WF");
        EXPECT(w && w->len == 780 && count(l1, 11065, "C64WF") == 3, "every frame: 780 bytes to the partner");
        if (w) {
            uint32_t seq = w->d[5] | w->d[6] << 8 | w->d[7] << 16 | (uint32_t)w->d[8] << 24;
            const uint8_t *px = w->d + 12 + 3 * (2 * PANEL_PHYS_W + 7);                   /* row 2, column 7 of the right half */
            uint32_t c = (uint32_t)px[0] << 16 | (uint32_t)px[1] << 8 | px[2];
            printf("   frame %u: brightness %d %%, light %d/255, flags %d, pixel 39,2 = %06X\n", seq, w->d[9], w->d[10], w->d[11], c);
            EXPECT(seq == 3 && w->d[9] == 45 && w->d[10] == 128 && w->d[11] == 1, "sequence, brightness, light, dithering of this Pico");
            EXPECT(c == pix(39, 2) + 2, "the right half of the picture");
        }
        FAKE_NOW += 3100000; display_show(fb);
        EXPECT(g_panel_w == PANEL_PHYS_W, "partner silent for 3 s: back to its own matrix");
    } else if (!strcmp(phase, "follow")) {
        printf("== two matrices: this Pico shows the half another one sends\n");
        wide_config(WIDE_FOLLOW, false, "", 0);
        put_v5(0, 0, ""); up(); run_ms(100);
        static uint32_t own[PANEL_W_MAX * PANEL_H];
        for (int i = 0; i < PANEL_PHYS_W * PANEL_H; i++) own[i] = 0x0000FF;
        int l0 = UDP_LOG_N;
        test_udp_port(11065, (const uint8_t *)"C64WQ", 5, IP(192, 168, 1, 57 + 1));
        EXPECT(sent(l0, 11065, IP(192, 168, 1, 58), "C64WA"), "answers a lead's question at once");
        static uint8_t fr[780];
        for (int f = 0; f < 4; f++) {
            memset(fr, 0, sizeof fr); memcpy(fr, "C64WF", 5); fr[5] = (uint8_t)(f + 1); fr[9] = 70; fr[10] = 200; fr[11] = 0;
            for (int i = 0; i < PANEL_PHYS_W * PANEL_H; i++) { fr[12 + 3 * i] = (uint8_t)f; fr[13 + 3 * i] = (uint8_t)i; fr[14 + 3 * i] = 0x33; }
            test_udp_port(11065, fr, 780, IP(192, 168, 1, 58));
            FAKE_NOW += 16000; display_show(own);
            if (f == 0) EXPECT(FAKE_SHOWN[0] == 0x0000FF, "one frame: still its own picture (waits for a small reserve)");
        }
        EXPECT((FAKE_SHOWN[5] >> 16) < 4 && (FAKE_SHOWN[5] & 0xFFFF) == (5 << 8 | 0x33), "then the lead's frames");
        EXPECT(FAKE_OVR_ON && FAKE_OVR_PCT == 70 && FAKE_OVR_LIGHT > 0.78f && FAKE_OVR_LIGHT < 0.79f && !FAKE_OVR_DITHER, "with the lead's brightness, light and dithering");
        EXPECT(sent(l0, 11065, IP(192, 168, 1, 58), "C64WA"), "tells the lead it is there");
        uint32_t last = FAKE_SHOWN[5];
        FAKE_NOW += 16000; display_show(own);
        EXPECT(FAKE_SHOWN[5] != 0x0000FF, "a missing frame: the last one stays");
        (void)last;
        FAKE_NOW += 600000; display_show(own);
        EXPECT(FAKE_SHOWN[0] == 0x0000FF && !FAKE_OVR_ON, "no frames for 0.5 s: its own picture and brightness again");
    }
    printf(fails ? "FAILED: %d\n" : "all ecosystem checks passed\n", fails);
    return fails ? 1 : 0;
}
