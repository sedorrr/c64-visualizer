/* saving texts.txt from the web page: CRs dropped, magic in front, NUL at the end, then a restart.
   Access point mode (no WLAN settings): no password needed. */
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "net.h"
struct tcp_pcb; struct tcp_pcb *test_connect(void); void test_send(struct tcp_pcb *p, const char *d, int split);
extern jmp_buf REBOOT_JMP; extern int REBOOTED; extern unsigned char FAKE_FLASH[];
static int save(const char *body, int split) {
    static char req[70000];
    int n = snprintf(req, sizeof req, "POST /texts HTTP/1.1\r\nContent-Length: %d\r\n\r\n%s", (int)strlen(body), body);
    (void)n; REBOOTED = 0;
    memset(FAKE_FLASH + 0x300000, 0xAA, 0x10000);
    struct tcp_pcb *c = test_connect();
    if (!setjmp(REBOOT_JMP)) { test_send(c, req, split); net_task(); }
    return REBOOTED;
}
/* each save ends in a restart, so every case runs in its own process: no argument = small, "big" = 60 KB */
int main(int argc, char **argv) {
    net_init("@silence 8000\nHello\n");
    int fails = 0, r;
    if (argc < 2) {
    r = save("@silence 5000\r\nNew text\r\n{color:red}Zweite Zeile\r\n", 40);
    const char *want = "C64VIZTXT1\n@silence 5000\nNew text\n{color:red}Zweite Zeile\n";
    printf("restart: %d, flash: [%s]\n", r, (char *)FAKE_FLASH + 0x300000);
    if (!r || strcmp((char *)FAKE_FLASH + 0x300000, want)) { printf("!!! small texts file wrong\n"); fails++; }
    } else {
    /* a large file (60 KB, the limit is 64 KB minus 32): nothing cut, nothing beyond the NUL written */
    static char big[61500]; int k = 0;
    for (int i = 0; k < 61000; i++) k += snprintf(big + k, sizeof big - k, "Zeile %05d mit etwas Text\r\n", i);
    r = save(big, 1460);
    int len = (int)strlen((char *)FAKE_FLASH + 0x300000), crs = 0;
    for (int i = 0; i < len; i++) if (FAKE_FLASH[0x300000 + i] == '\r') crs++;
    int lines = 0; for (int i = 0; i < k; i++) if (big[i] == '\n') lines++;
    printf("large file: restart %d, %d bytes sent, %d bytes in flash (expected %d), CRs left %d\n", r, k, len, 11 + k - lines, crs);
    if (!r || len != 11 + k - lines || crs || memcmp(FAKE_FLASH + 0x300000, "C64VIZTXT1\nZeile 00000", 22)) { printf("!!! large texts file wrong\n"); fails++; }
    }
    printf("%s\n", fails ? "SOME TEXTS SAVE CHECKS FAILED" : "all texts save checks passed");
    return fails;
}
