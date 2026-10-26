/* Retro news from the Pi jukebox (news.c): POST /news and C64NEWS1 over UDP, cleaned for the matrix font, sent on
   along the stream chain, too long / busy refused, forgotten after 3 h, cleared by an empty list.
   One phase per process (argv[1]). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "net.h"
#include "news.h"
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "http_client.h"
#include "cfg_v5.h"
void test_udp_from(unsigned char *p, int n, uint32_t from);
extern int64_t FAKE_NOW; extern int FAKE_LINK; extern unsigned char FAKE_FLASH[];
static int fails = 0;
#define EXPECT(c, what) do { if (c) printf("ok   %s\n", what); else { printf("FAIL %s\n", what); fails++; } } while (0)
#define CFG_AT (FAKE_FLASH + 4 * 1024 * 1024 - 2 * 4096)
#define IP(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
static unsigned fnv(const void *d, unsigned long n) { const unsigned char *p = d; unsigned h = 2166136261u; for (unsigned long i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; } return h; }
static void put_v5(int stream, const char *fwd) {
    v5_t c; memset(&c, 0, sizeof c); c.magic = V5_MAGIC; strcpy(c.ssid, "Fritz"); c.stream = stream; c.smode = 0;
    strcpy(c.u64[0].label, "Elite II"); strcpy(c.u64[0].addr, "192.168.1.64"); strcpy(c.fwd, fwd); strcpy(c.name, "wohnzimmer");
    c.check = fnv(&c, offsetof(v5_t, check)); memcpy(CFG_AT, &c, sizeof c);
}
static void run_ms(int ms) { for (int t = 0; t < ms; t += 10) { FAKE_NOW += 10000; net_task(); } }
static void up(void) { net_init("t"); FAKE_LINK = CYW43_LINK_UP; cyw43_state.netif[0].ip.addr = IP(192, 168, 1, 57); FAKE_NOW += 100000; net_task(); char m[120]; while (net_take_message(m, sizeof m)) ; }
static const udp_log_t *sent(int from, uint16_t port, uint32_t to, const char *prefix) {
    const udp_log_t *r = NULL;
    for (int i = from; i < UDP_LOG_N; i++) { const udp_log_t *l = &UDP_LOG[i % 256];
        if (l->port == port && (!to || l->to == to) && (!prefix || !memcmp(l->d, prefix, strlen(prefix)))) r = l; }
    return r;
}
static const char *head(int i) { static char b[NEWS_LEN + 1]; if (!news_get(i, b, sizeof b)) strcpy(b, "(none)"); return b; }

int main(int argc, char **argv) {
    const char *phase = argc > 1 ? argv[1] : "http";
    if (!strcmp(phase, "http")) {
        printf("== POST /news from the Pi jukebox\n");
        put_v5(1, "192.168.1.60"); up(); run_ms(100);
        int l0 = UDP_LOG_N;
        const char *body = "Lemon64: Neues Spiel für den C64 – „Übermütig“ erschienen!\n"
                           "CSDb: X'2026 {color:red} results (50% done) & more?\n\n   \n"
                           "Ein sehr langer Titel, der über die Grenze der Laufschrift hinausgeht und deshalb gekürzt werden muss";
        const char *a = hc_req("POST", "/news", body);
        EXPECT(strstr(a, "200 OK"), "accepted");
        const udp_log_t *r = sent(l0, 11001, IP(192, 168, 1, 60), NEWS_MAGIC);
        EXPECT(r && r->len == 9 + (int)strlen(body) && !memcmp(r->d + 9, body, strlen(body)), "sent on unchanged to the next Pico (stream port)");
        EXPECT(news_count() == 0, "taken over by the main program only (net_task)");
        run_ms(20);
        for (int i = 0; i < 3; i++) printf("   %s\n", head(i));
        EXPECT(news_count() == 3, "three headlines (the empty lines dropped)");
        EXPECT(!strcmp(head(0), "Lemon64: Neues Spiel fuer den C64 Uebermuetig erschienen!"), "umlauts spelt out, other UTF-8 dropped");
        EXPECT(!strcmp(head(1), "CSDb: X'2026 color:red results (50% done) & more?"), "{ } removed (no codes from outside), symbols kept");
        EXPECT(strlen(head(2)) <= NEWS_LEN && !strncmp(head(2), "Ein sehr langer Titel", 21), "a long one is cut to fit one text line");
        const char *st = hc_body(hc_req("GET", "/status", NULL));
        EXPECT(strstr(st, "\"news\":3"), "/status counts them");
        EXPECT(strstr(hc_req("POST", "/news", ""), "200 OK") && (run_ms(20), news_count() == 0), "an empty list clears them");
        static char big[900]; memset(big, 'x', sizeof big - 1);
        EXPECT(strstr(hc_req("POST", "/news", big), "413"), "more than 760 bytes: refused");
    } else if (!strcmp(phase, "busy")) {
        printf("== a second list before the first is taken over\n");
        put_v5(1, ""); up(); run_ms(100);
        EXPECT(strstr(hc_req("POST", "/news", "Erste"), "200 OK"), "first list accepted");
        EXPECT(strstr(hc_req("POST", "/news", "Zweite"), "409"), "second: busy, the Pi sends again");
        run_ms(20);
        EXPECT(strstr(hc_req("POST", "/news", "Zweite"), "200 OK") && (run_ms(20), !strcmp(head(0), "Zweite")), "after net_task: accepted");
    } else if (!strcmp(phase, "udp")) {
        printf("== C64NEWS1 over UDP (from the Pi or the Pico before this one), forgotten after 3 h\n");
        put_v5(1, "192.168.1.60"); up(); run_ms(100);
        static unsigned char m[300]; int k = snprintf((char *)m, sizeof m, "%sCSDb: Neue Demo\nLemon64: Neues Spiel", NEWS_MAGIC);
        int l0 = UDP_LOG_N;
        test_udp_from(m, k, IP(192, 168, 1, 78)); run_ms(20);
        EXPECT(news_count() == 2 && !strcmp(head(1), "Lemon64: Neues Spiel"), "taken over");
        EXPECT(!net_audio_active(), "not taken for audio");
        EXPECT(sent(l0, 11001, IP(192, 168, 1, 60), NEWS_MAGIC "CSDb"), "and sent on along the chain");
        run_ms(1000);
        FAKE_NOW += (int64_t)NEWS_KEEP_MS * 1000 - 1500000;      /* 1 s passed already */
        EXPECT(news_count() == 2, "still there just before 3 h");
        FAKE_NOW += 1000000;
        EXPECT(news_count() == 0, "gone after 3 h without a new list");
    }
    printf(fails ? "%d FAILED\n" : "all ok\n", fails);
    return fails ? 1 : 0;
}
