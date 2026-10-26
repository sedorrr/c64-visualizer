/* firmware update over the web page: begin / chunks / end (check) / apply, refusals, flash boundaries */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lwip/tcp.h"
#include "net.h"
#include "ota.h"
extern int64_t FAKE_NOW; extern unsigned char FAKE_FLASH[];
extern uint32_t OTA_APPLIED_OFF, OTA_APPLIED_SIZE;
#define STAGE 0x310000u
#define STAGE_SIZE (0x400000u - 0x2000u - STAGE)
static int fails = 0;
static void check(int ok, const char *what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }
static int post(const char *path, const char *body, int len) {
    const char *o, *t; int n; int c = ota_route(true, path, body, len, &o, &n, &t); return c;
}
static const char *state(void) { static char b[300]; const char *o, *t; int n; ota_route(false, "/fw/state", NULL, 0, &o, &n, &t); memcpy(b, o, n); b[n] = 0; return b; }
static uint32_t fnv(const uint8_t *p, uint32_t n) { uint32_t h = 2166136261u; while (n--) { h ^= *p++; h *= 16777619u; } return h; }
static uint8_t *image(uint32_t n, int marker, int ident) {
    uint8_t *d = malloc(n); for (uint32_t i = 0; i < n; i++) d[i] = (uint8_t)(i * 7 + 3);
    if (marker) { d[0x138] = 0xd3; d[0x139] = 0xde; d[0x13a] = 0xff; d[0x13b] = 0xff; }
    if (ident) memcpy(d + n / 2, "c64viz_wifi", 11);
    return d;
}
/* sends the image like the web page does; returns the HTTP status of /fw/end (or the first failure) */
static int upload(const uint8_t *d, uint32_t n, uint32_t hash) {
    char p[80]; snprintf(p, sizeof p, "/fw/begin?size=%u", n);
    int c = post(p, NULL, 0); if (c != 200) return c;
    for (uint32_t off = 0; off < n; off += 32768) {
        uint32_t k = n - off < 32768 ? n - off : 32768;
        snprintf(p, sizeof p, "/fw/chunk?off=%u", off);
        if ((c = post(p, (const char *)d + off, (int)k)) != 200) return c;
        ota_task();
    }
    snprintf(p, sizeof p, "/fw/end?hash=%08x", hash);
    c = post(p, NULL, 0); ota_task(); return c;
}
int main(void) {
    memset(FAKE_FLASH, 0xFF, 4 * 1024 * 1024);
    memcpy(FAKE_FLASH + 0x30FFF0, "TEXTS-END-GUARD", 16);           /* last bytes of the texts area */
    memcpy(FAKE_FLASH + 0x3FE000, "NETCFG-GUARD", 13);               /* WLAN settings sector */

    printf("== good image, 10000 bytes (last page padded)\n");
    uint8_t *img = image(10000, 1, 1);
    check(upload(img, 10000, fnv(img, 10000)) == 200, "upload accepted");
    printf("state: %s\n", state());
    check(strstr(state(), "\"state\":\"ready\"") != NULL, "checked and ready");
    check(!memcmp(FAKE_FLASH + STAGE, img, 10000), "staged bytes equal the image");
    check(FAKE_FLASH[STAGE + 10000] == 0xFF && FAKE_FLASH[STAGE + 12287] == 0xFF, "rest of the last sector erased");

    printf("== refusals\n");
    check(upload(img, 10000, fnv(img, 10000) ^ 1) == 200 && strstr(state(), "Prüfsumme") != NULL, "wrong hash -> error");
    uint8_t *nomark = image(10000, 0, 1);
    check(upload(nomark, 10000, fnv(nomark, 10000)) == 200 && strstr(state(), "keine gültige RP2350") != NULL, "no IMAGE_DEF -> error");
    uint8_t *pico2 = image(10000, 1, 0);
    check(upload(pico2, 10000, fnv(pico2, 10000)) == 200 && strstr(state(), "keine WLAN-Firmware") != NULL, "Pico 2 firmware without WLAN -> error");
    check(post("/fw/apply", NULL, 0) == 409, "apply refused after a failed check");
    char p[64]; snprintf(p, sizeof p, "/fw/begin?size=%u", STAGE_SIZE + 4096);
    check(post(p, NULL, 0) == 400, "image larger than the staging area refused");
    check(post("/fw/begin?size=20000", NULL, 0) == 200 && post("/fw/chunk?off=8192", (const char *)img, 4096) == 400, "chunk out of order refused");

    printf("== largest possible image: texts and WLAN settings stay untouched\n");
    uint8_t *big = image(STAGE_SIZE, 1, 1);
    check(upload(big, STAGE_SIZE, fnv(big, STAGE_SIZE)) == 200 && strstr(state(), "ready") != NULL, "952 KB image staged and checked");
    check(!memcmp(FAKE_FLASH + 0x30FFF0, "TEXTS-END-GUARD", 16), "texts area untouched");
    check(!memcmp(FAKE_FLASH + 0x3FE000, "NETCFG-GUARD", 13), "WLAN settings untouched");
    printf("== apply (last: the real copy never returns)\n");
    check(upload(img, 10000, fnv(img, 10000)) == 200 && strstr(state(), "ready") != NULL, "good image staged again");
    check(post("/fw/apply", NULL, 0) == 200, "apply accepted");
    ota_task(); check(OTA_APPLIED_SIZE == 0, "apply waits for the answer to reach the browser");
    FAKE_NOW += 1000000; ota_task();
    check(OTA_APPLIED_OFF == STAGE && OTA_APPLIED_SIZE == 10000, "copy started with offset 0x310000 and the right size");
    printf("%s\n", fails ? "SOME OTA CHECKS FAILED" : "all OTA checks passed");
    return fails ? 1 : 0;
}
