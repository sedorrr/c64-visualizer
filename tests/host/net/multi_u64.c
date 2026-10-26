/* Several Ultimates: settings v4 -> v5 (one address -> slot 1), the list in /config, switching the source (the old
   one is stopped, the new one asked at once, the choice stored without a restart), one stream at a time, and the
   network search. One phase per process (argv[1]). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "net.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "http_client.h"
#include "cfg_v5.h"
void test_udp_from(unsigned char *p, int n, uint32_t from); void test_rest_reply(const char *r);
const char *test_rest_sent(void); extern struct tcp_pcb *LAST_REST, *PCBS[1024]; extern int NPCBS;
extern int64_t FAKE_NOW; extern int FAKE_LINK; extern unsigned char FAKE_FLASH[];
extern int (*FAKE_HOST_UP)(uint32_t ip);
static int fails = 0;
#define EXPECT(c, what) do { if (c) printf("ok   %s\n", what); else { printf("FAIL %s\n", what); fails++; } } while (0)
#define CFG_AT (FAKE_FLASH + 4 * 1024 * 1024 - 2 * 4096)
#define IP(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
static unsigned fnv(const void *d, unsigned long n) { const unsigned char *p = d; unsigned h = 2166136261u; for (unsigned long i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; } return h; }
typedef struct { unsigned magic; char ssid[33], pass[65], u64[64], u64pw[33], webpw[33], name[17]; unsigned char stream, smode, pad; char fwd[64]; unsigned check; } v4_t;
static void put_v5(int stream, int src) {
    v5_t c; memset(&c, 0, sizeof c); c.magic = V5_MAGIC; strcpy(c.ssid, "Fritz"); c.stream = stream; c.src = src;
    strcpy(c.u64[0].label, "Elite II"); strcpy(c.u64[0].addr, "192.168.1.64");
    strcpy(c.u64[1].label, "C64 Ultimate"); strcpy(c.u64[1].addr, "192.168.1.65"); strcpy(c.u64[1].pw, "geheim");
    c.check = fnv(&c, offsetof(v5_t, check)); memcpy(CFG_AT, &c, sizeof c);
}
static void run_ms(int ms) { for (int t = 0; t < ms; t += 10) { FAKE_NOW += 10000; net_task(); } }
static void up(void) { net_init("t"); FAKE_LINK = CYW43_LINK_UP; cyw43_state.netif[0].ip.addr = IP(192, 168, 1, 57); FAKE_NOW += 100000; net_task(); char m[120]; while (net_take_message(m, sizeof m)) ; }
static unsigned char pkt[770];
static void packet(uint32_t from) { test_udp_from(pkt, 770, from); }
static const char *sent_host(void) { static char h[40]; const char *p = strstr(((hc_pcb *)LAST_REST)->out, "Host: "); h[0] = 0; if (p) sscanf(p + 6, "%39[^\r]", h); return h; }

/* the network search: these addresses answer */
/* like at home: the Ultimate 64 on Ethernet .64 and WLAN .80 (it names its wifi_mac), the C64 Ultimate on WLAN .70 and
   Ethernet .71 (no MAC fields - its Ethernet MAC starts with 02:15:41), a router .10 */
static int host_up(uint32_t ip) { return ip == IP(192, 168, 1, 64) || ip == IP(192, 168, 1, 80) || ip == IP(192, 168, 1, 70) || ip == IP(192, 168, 1, 71) || ip == IP(192, 168, 1, 10); }
#include "lwip/etharp.h"
extern struct { uint32_t ip; struct eth_addr mac; } FAKE_ARP[8]; extern int FAKE_NARP;
static void arp(uint32_t ip, const char *m) { unsigned b[6]; sscanf(m, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]);
    FAKE_ARP[FAKE_NARP].ip = ip; for (int i = 0; i < 6; i++) FAKE_ARP[FAKE_NARP].mac.addr[i] = (uint8_t)b[i]; FAKE_NARP++; }
static void answer_scan(void) {
    for (int i = 0; i < NPCBS; i++) {
        struct tcp_pcb *p = PCBS[i];
        if (p->port != 80 || p->seen || ((hc_pcb *)p)->outlen == 0 || !host_up(p->remote_ip.addr) || !p->recv) continue;
        p->seen = 1;
        uint32_t a = p->remote_ip.addr;
        const char *r = a == IP(192, 168, 1, 64) || a == IP(192, 168, 1, 80)
            ? "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\n  \"product\" : \"Ultimate 64 Elite\",\n  \"firmware_version\" : \"3.15a\",\n  \"hostname\" : \"Elite \\\"II\\\"\",\n"
              "  \"unique_id\" : \"ABC123\",\n  \"ethernet_mac\" : \"02:15:41:11:22:33\",\n  \"wifi_mac\" : \"12:34:56:78:9A:BC\",\n  \"errors\" : []\n}"
            : a == IP(192, 168, 1, 70) || a == IP(192, 168, 1, 71)
            ? "HTTP/1.1 200 OK\r\n\r\n{\"product\":\"C64 Ultimate\",\"hostname\":\"C64U\",\"unique_id\":\"DEF456\"}"
            : "HTTP/1.1 200 OK\r\n\r\n<html>FRITZ!Box</html>";
        err_t (*rc)(void *, struct tcp_pcb *, struct pbuf *, err_t) = (void *)p->recv;
        struct pbuf b = { 0, (void *)r, (uint16_t)strlen(r), (uint16_t)strlen(r) };
        rc(p->arg, p, &b, 0); if (p->recv) rc(p->arg, p, NULL, 0);
    }
}

int main(int argc, char **argv) {
    const char *phase = argc > 1 ? argv[1] : "migrate";
    for (int i = 2; i < 770; i += 2) { pkt[i] = 0x10; pkt[i + 1] = 0x20; }
    if (!strcmp(phase, "migrate")) {
        printf("== settings v4 (one Ultimate) -> v5: it becomes Ultimate 1\n");
        v4_t o; memset(&o, 0, sizeof o); o.magic = 0xC64E7C04u; strcpy(o.ssid, "Fritz"); strcpy(o.u64, "192.168.1.168"); strcpy(o.u64pw, "pw1");
        strcpy(o.name, "kueche"); strcpy(o.fwd, "c64viz-2.local"); o.stream = 1; o.smode = 2;
        o.check = fnv(&o, offsetof(v4_t, check)); memcpy(CFG_AT, &o, sizeof o);
        up();
        const char *st = hc_body(hc_req("GET", "/status", NULL)); printf("   %s\n", strstr(st, "\"src\""));
        EXPECT(strstr(st, "\"u64\":\"192.168.1.168\"") && strstr(st, "\"src\":0,\"u64list\":[{\"label\":\"\",\"addr\":\"192.168.1.168\",\"pw\":true}"), "Ultimate 1 = the old address, password kept");
        EXPECT(strstr(st, "{\"label\":\"\",\"addr\":\"\",\"pw\":false}]"), "slots 2-4 empty");
        EXPECT(strstr(st, "\"cfgname\":\"kueche\"") && strstr(st, "\"smode\":2") && strstr(st, "\"fwd\":\"c64viz-2.local\""), "name, mode and forwarding kept");
    } else if (!strcmp(phase, "config")) {
        printf("== /config with the list of Ultimates\n");
        put_v5(0, 0); up();
        const char *a = hc_req("POST", "/config", "ssid=Fritz&stream=0&smode=0&l0=Elite&a0=192.168.1.64&p0=&l1=Neu&a1=ultimate.local&p1=&l2=&a2=&p2=&l3=&a3=&p3=");
        EXPECT(strstr(a, "400") && strstr(a, "Ultimate 2"), "name instead of an IP refused (Ultimate 2)");
        a = hc_req("POST", "/config", "ssid=Fritz&stream=0&smode=0&l0=Elite+II&a0=+192.168.1.64+&p0=&l1=C64U&a1=192.168.1.66&p1=&xp1=1&l2=Alt&a2=192.168.1.41&p2=pw3&l3=&a3=&p3=&src=2");
        EXPECT(strstr(a, "200"), "saved");
        v5_t n; memcpy(&n, CFG_AT, sizeof n);
        EXPECT(n.magic == V5_MAGIC && n.check == fnv(&n, offsetof(v5_t, check)), "layout v5");
        EXPECT(!strcmp(n.u64[0].addr, "192.168.1.64") && !strcmp(n.u64[0].label, "Elite II"), "Ultimate 1: address trimmed, name");
        EXPECT(!strcmp(n.u64[1].addr, "192.168.1.66") && n.u64[1].pw[0] == 0, "Ultimate 2: new address, password removed");
        EXPECT(!strcmp(n.u64[2].pw, "pw3") && n.src == 2, "Ultimate 3: password set, chosen as the source");
    } else if (!strcmp(phase, "switch")) {
        printf("== switching the source: old one stopped, new one asked at once, stored without restart\n");
        put_v5(1, 0); up(); run_ms(600);
        EXPECT(strstr(test_rest_sent(), "audio:start") && !strcmp(sent_host(), "192.168.1.64"), "start: Ultimate 1 asked");
        test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}");
        for (int i = 0; i < 50; i++) { FAKE_NOW += 4000; packet(IP(192, 168, 1, 64)); net_task(); }
        EXPECT(net_audio_active(), "Ultimate 1 streams");
        const char *a = hc_req("POST", "/src?i=3", NULL);
        EXPECT(strstr(a, "400"), "empty slot refused");
        a = hc_req("POST", "/src?i=1", NULL);
        EXPECT(strstr(a, "200"), "Ultimate 2 chosen");
        FAKE_NOW += 4000; packet(IP(192, 168, 1, 64)); net_task();
        EXPECT(strstr(test_rest_sent(), "audio:stop") && !strcmp(sent_host(), "192.168.1.64"), "Ultimate 1 told to stop");
        v5_t n; memcpy(&n, CFG_AT, sizeof n);
        EXPECT(n.src == 1 && n.check == fnv(&n, offsetof(v5_t, check)), "choice stored in flash (no restart)");
        net_audio_skip(net_audio_avail());
        FAKE_NOW += 4000; packet(IP(192, 168, 1, 64));
        EXPECT(net_audio_avail() == 0, "Ultimate 1's packets are not used any more");
        test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}"); net_task();
        EXPECT(strstr(test_rest_sent(), "audio:start") && !strcmp(sent_host(), "192.168.1.65") && strstr(((hc_pcb *)LAST_REST)->out, "X-Password: geheim"),
               "Ultimate 2 asked at once (with its password)");
        FAKE_NOW += 4000; packet(IP(192, 168, 1, 65));
        EXPECT(net_audio_avail() > 0, "Ultimate 2's packets are used");
        const char *st = hc_body(hc_req("GET", "/status", NULL));
        EXPECT(strstr(st, "\"u64\":\"192.168.1.65\"") && strstr(st, "\"src\":1"), "status: Ultimate 2 in use");
    } else if (!strcmp(phase, "lock")) {
        printf("== two senders at once: only the first one is used\n");
        put_v5(1, 0); up(); run_ms(600);
        for (int i = 0; i < 20; i++) { FAKE_NOW += 4000; packet(IP(192, 168, 1, 64)); }
        net_audio_skip(net_audio_avail());
        FAKE_NOW += 2000; packet(IP(192, 168, 1, 79));
        EXPECT(net_audio_avail() == 0, "a second sender is ignored while the first one streams");
        FAKE_NOW += 600000; packet(IP(192, 168, 1, 79));
        EXPECT(net_audio_avail() > 0, "first one quiet for 0.6 s: the second one is taken");
    } else if (!strcmp(phase, "scan")) {
        printf("== network search\n");
        put_v5(0, 0); up();
        const char *a = hc_req("POST", "/u64/scan", NULL);
        EXPECT(strstr(a, "\"running\":true"), "search started");
        FAKE_HOST_UP = host_up;
        arp(IP(192, 168, 1, 64), "02:15:41:11:22:33"); arp(IP(192, 168, 1, 80), "12:34:56:78:9a:bc");
        arp(IP(192, 168, 1, 70), "3c:dc:75:65:43:21"); arp(IP(192, 168, 1, 71), "02:15:41:44:55:66");
        int connects = NPCBS;
        for (int t = 0; t < 60000 && strstr(hc_body(hc_req("GET", "/u64/scan", NULL)), "\"running\":true"); t += 20) { FAKE_NOW += 20000; net_task(); answer_scan(); }
        const char *r = hc_body(hc_req("GET", "/u64/scan", NULL)); printf("   %s\n", r);
        EXPECT(strstr(r, "\"running\":false,\"done\":254"), "all 254 addresses done");
        EXPECT(strstr(r, "{\"ip\":\"192.168.1.64\",\"alt\":\"192.168.1.80\",\"link\":\"eth\",\"name\":\"Elite  II \",\"product\":\"Ultimate 64 Elite\"}"),
               "Ultimate 64 Elite once, Ethernet address first, WLAN as the other (quotes in the name made safe)");
        EXPECT(strstr(r, "{\"ip\":\"192.168.1.71\",\"alt\":\"192.168.1.70\",\"link\":\"eth\",\"name\":\"C64U\",\"product\":\"C64 Ultimate\"}"),
               "C64 Ultimate once: Ethernet (found second, MAC 02:15:41) preferred over WLAN");
        EXPECT(strstr(r, "\"found\":[{") && !strstr(r, "},{\"ip\":\"192.168.1.64") && strstr(r, "}]}") && (int)(strstr(r, "}]}") - strstr(r, "\"found\"")) < 260, "two entries, not four");
        EXPECT(!strstr(r, "192.168.1.10"), "the router is not an Ultimate");
        int own = 0; for (int i = connects; i < NPCBS; i++) if (PCBS[i]->remote_ip.addr == IP(192, 168, 1, 57)) own++;
        EXPECT(own == 0, "own address not asked");
        EXPECT(FAKE_NOW < 60000000, "done within a minute");
        printf("   took %.1f s (simulated)\n", FAKE_NOW / 1e6);
    }
    return fails ? 1 : 0;
}
