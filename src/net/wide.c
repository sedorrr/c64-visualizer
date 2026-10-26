/* Two matrices as one picture: see wide.h. Takes over display_show / display_set_overlay of the WLAN firmware. */
#include "wide.h"
#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "config.h"
#include "display.h"
#include "net_internal.h"
#include "diag.h"

int g_panel_w = PANEL_PHYS_W;                     /* the picture the modes draw (config.h: PANEL_W) */

#define HALF_PX   (PANEL_PHYS_W * PANEL_H)
#define FRAME_LEN (12 + HALF_PX * 3)              /* "C64WF", seq u32, brightness %, light 0..255, flags, RGB */
#define DELAY     2                               /* lead shows its own half 2 frames late: the partner's reserve */
#define QN        (DELAY + 2)                     /* partner: frames waiting (more than DELAY + 2 are skipped anyway) */
#define ALIVE_MS  3000                            /* lead: partner answered within this time = draw wide */
#define FOLLOW_MS 500                             /* partner: frames within this time = show them */

static int  s_role = WIDE_OFF;
static bool s_right = false;
static char s_partner[64];
static struct udp_pcb *s_pcb;
static void (*s_overlay)(uint32_t *fb);
void display_set_overlay(void (*fn)(uint32_t *fb)) { s_overlay = fn; }

/* lead */
static volatile uint32_t s_alive_ms, s_partner_ip;
static uint32_t s_seq, s_probe_ms;
static bool s_wide_shown = false;
static uint32_t s_ring[DELAY + 1][HALF_PX];
static int s_ri = 0;
/* partner: frames from the lead (written in the lwIP context, read by display_show) */
static uint8_t s_q[QN][FRAME_LEN];
static volatile uint32_t s_qw = 0, s_qr = 0, s_last_ms = 0, s_reply_ms = 0;
static bool s_started = false, s_ovr = false;
static uint32_t s_last_half[HALF_PX];

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

void wide_config(int role, bool right, const char *partner, int partner_len) {
    s_role = role; s_right = right;
    int n = partner_len < (int)sizeof s_partner - 1 ? partner_len : (int)sizeof s_partner - 1;
    if (partner && n > 0) memcpy(s_partner, partner, (size_t)n);
    s_partner[n > 0 ? n : 0] = 0;
    if (s_role == WIDE_LEAD && !s_partner[0]) s_role = WIDE_OFF;
}

bool wide_following(void) { return s_role == WIDE_FOLLOW && s_started && now_ms() - s_last_ms < FOLLOW_MS; }

static void send_to(uint32_t ip, const void *d, int n) {          /* caller holds the lwIP lock (or is lwIP) */
    if (!s_pcb || !ip) return;
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)n, PBUF_RAM);
    if (!p) return;
    memcpy(p->payload, d, (size_t)n);
    ip_addr_t to; ip4_addr_set_u32(ip_2_ip4(&to), ip);
    udp_sendto(s_pcb, p, &to, WIDE_PORT);
    pbuf_free(p);
}

static void recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {   /* lwIP */
    (void)arg; (void)pcb; (void)port;
    uint32_t from = addr ? ip4_addr_get_u32(ip_2_ip4(addr)) : 0, now = now_ms();
    uint8_t h[5];
    if (pbuf_copy_partial(p, h, 5, 0) != 5) { pbuf_free(p); return; }
    if (s_role == WIDE_FOLLOW && !memcmp(h, "C64WF", 5) && p->tot_len == FRAME_LEN) {
        if (s_qw - s_qr < QN) {                                       /* full: this one goes (only display_show moves s_qr) */
            pbuf_copy_partial(p, s_q[s_qw % QN], FRAME_LEN, 0);
            s_qw++;
        }
        s_last_ms = now;
        if (now - s_reply_ms >= 500) { s_reply_ms = now; send_to(from, "C64WA", 5); }   /* "still here" */
    } else if (s_role == WIDE_FOLLOW && !memcmp(h, "C64WQ", 5)) {
        send_to(from, "C64WA", 5);
    } else if (s_role == WIDE_LEAD && !memcmp(h, "C64WA", 5) && from == s_partner_ip) {
        s_alive_ms = now ? now : 1;
    }
    pbuf_free(p);
}

void wide_start(void) {
    if (s_role == WIDE_OFF || s_pcb) return;
    cyw43_arch_lwip_begin();
    s_pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (s_pcb && udp_bind(s_pcb, IP_ANY_TYPE, WIDE_PORT) == ERR_OK) udp_recv(s_pcb, recv_cb, NULL);
    else if (s_pcb) { udp_remove(s_pcb); s_pcb = NULL; }
    cyw43_arch_lwip_end();
    DIAG("zwei Matrizen: %s%s%s", s_role == WIDE_LEAD ? "dieser Pico rechnet, Partner " : "zeigt die Hälfte eines anderen Pico",
         s_role == WIDE_LEAD ? s_partner : "", s_role == WIDE_LEAD ? (s_right ? " (diese Matrix rechts)" : " (diese Matrix links)") : "");
}

void wide_task(void) {
    if (s_role != WIDE_LEAD || !s_pcb) return;
    s_partner_ip = net_resolve_wide(s_partner);
    uint32_t now = now_ms();
    bool alive = s_alive_ms && now - s_alive_ms < ALIVE_MS;
    if (!alive && s_partner_ip && now - s_probe_ms >= 1000) {         /* is the partner there? */
        s_probe_ms = now;
        cyw43_arch_lwip_begin(); send_to(s_partner_ip, "C64WQ", 5); cyw43_arch_lwip_end();
    }
}

int wide_json(char *o, int n) {
    static const char *names[] = { "", "lead", "follow" };
    bool ok = s_role == WIDE_LEAD ? g_panel_w > PANEL_PHYS_W : wide_following();
    return snprintf(o, (size_t)n, "\"wide\":\"%s\",\"wpart\":\"%s\",\"wside\":\"%s\",\"wok\":%s",
                    names[s_role], s_role == WIDE_LEAD ? s_partner : "", s_right ? "right" : "left", ok ? "true" : "false");
}

/* ------------------------------------------------------------ the frame */
static uint32_t s_pic[PANEL_W_MAX * PANEL_H];

static void columns(uint32_t *dst, const uint32_t *pic, int w, int x0) {   /* PANEL_PHYS_W columns from x0 */
    for (int y = 0; y < PANEL_H; y++) memcpy(dst + y * PANEL_PHYS_W, pic + y * w + x0, PANEL_PHYS_W * sizeof(uint32_t));
}

static void lead_frame(const uint32_t *pic) {
    static uint8_t pkt[FRAME_LEN];
    static uint32_t other[HALF_PX];
    columns(other, pic, g_panel_w, s_right ? 0 : PANEL_PHYS_W);
    memcpy(pkt, "C64WF", 5);
    uint32_t q = ++s_seq;
    pkt[5] = (uint8_t)q; pkt[6] = (uint8_t)(q >> 8); pkt[7] = (uint8_t)(q >> 16); pkt[8] = (uint8_t)(q >> 24);
    pkt[9] = (uint8_t)display_get_brightness();
    pkt[10] = (uint8_t)(display_get_light() * 255.0f + 0.5f);
    pkt[11] = display_get_dither() ? 1 : 0;
    for (int i = 0; i < HALF_PX; i++) {
        pkt[12 + 3 * i] = (uint8_t)(other[i] >> 16); pkt[13 + 3 * i] = (uint8_t)(other[i] >> 8); pkt[14 + 3 * i] = (uint8_t)other[i];
    }
    cyw43_arch_lwip_begin(); send_to(s_partner_ip, pkt, FRAME_LEN); cyw43_arch_lwip_end();
    /* own half: DELAY frames late, so both halves change together (the partner keeps about as many in reserve) */
    if (!s_wide_shown) for (int i = 0; i <= DELAY; i++) columns(s_ring[i], pic, g_panel_w, s_right ? PANEL_PHYS_W : 0);
    s_wide_shown = true;
    columns(s_ring[s_ri], pic, g_panel_w, s_right ? PANEL_PHYS_W : 0);
    s_ri = (s_ri + 1) % (DELAY + 1);
    display_show_phys(s_ring[s_ri]);
}

static void follow_frame(void) {
    uint32_t avail = s_qw - s_qr;
    while (avail > DELAY + 1) { s_qr++; avail--; }                    /* too far behind (the queue is full): catch up */
    if (avail) {
        const uint8_t *f = s_q[s_qr % QN];
        for (int i = 0; i < HALF_PX; i++) s_last_half[i] = (uint32_t)f[12 + 3 * i] << 16 | (uint32_t)f[13 + 3 * i] << 8 | f[14 + 3 * i];
        display_set_override(true, f[9], f[10] / 255.0f, f[11] & 1);
        s_ovr = true;
        s_qr++;
    }
    display_show_phys(s_last_half);                                   /* nothing new: the last frame stays */
}

void display_show(const uint32_t *fb) {
    int w = g_panel_w;
    memcpy(s_pic, fb, (size_t)(w * PANEL_H) * sizeof(uint32_t));
    if (s_overlay) s_overlay(s_pic);                                  /* drive LED, progress: on the whole picture */
    if (s_role == WIDE_FOLLOW) {
        uint32_t now = now_ms();
        bool fresh = s_qw && now - s_last_ms < FOLLOW_MS;
        if (s_started && !fresh) { s_started = false; s_qr = s_qw; DIAG("zwei Matrizen: keine Bilder mehr - eigenes Bild"); }
        if (!s_started && fresh && s_qw - s_qr >= DELAY) { s_started = true; DIAG("zwei Matrizen: Bilder vom Partner kommen"); }
        if (s_started) { follow_frame(); return; }
        if (s_ovr) { display_set_override(false, 0, 1.0f, true); s_ovr = false; }
        display_show_phys(s_pic);
        return;
    }
    if (s_role == WIDE_LEAD && w > PANEL_PHYS_W) lead_frame(s_pic);
    else { s_wide_shown = false; display_show_phys(s_pic); }
    if (s_role == WIDE_LEAD) {                                        /* the next picture: wide while the partner answers */
        bool alive = s_alive_ms && now_ms() - s_alive_ms < ALIVE_MS;
        int nw = alive ? 2 * PANEL_PHYS_W : PANEL_PHYS_W;
        if (nw != g_panel_w) {
            g_panel_w = nw;
            DIAG("zwei Matrizen: %s", alive ? "Partner antwortet - ein Bild über beide" : "Partner antwortet nicht - nur diese Matrix");
        }
    }
}
