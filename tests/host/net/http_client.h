/* tiny web client for host tests: Digest login with the default password (needed in the home WLAN) */
#pragma once
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "md5.h"
#include "net.h"
struct tcp_pcb; struct tcp_pcb *test_connect(void); void test_send(struct tcp_pcb *p, const char *d, int split);
extern jmp_buf REBOOT_JMP; extern int REBOOTED;
typedef struct { void *arg; int closed; char out[131072]; int outlen; } hc_pcb;
static void hc_md5(const char *in, char out[33]) {
    md5_ctx_t m; uint8_t d[16]; md5_init(&m); md5_update(&m, in, strlen(in)); md5_final(&m, d);
    for (int i = 0; i < 16; i++) sprintf(out + 2 * i, "%02x", d[i]);
}
static const char *hc_auth(const char *method, const char *uri) {
    static char nonce[64], line[600]; static int nc = 0;
    if (!nonce[0]) {
        struct tcp_pcb *c = test_connect(); test_send(c, "GET / HTTP/1.1\r\n\r\n", 9);
        char *n = strstr(((hc_pcb *)c)->out, "nonce=\"");
        if (!n) return "";                                   /* own access point: no login */
        n += 7; int k = 0; while (n[k] != '"' && k < 63) { nonce[k] = n[k]; k++; } nonce[k] = 0;
    }
    char ha1[33], ha2[33], r[33], b[600], ncs[9]; snprintf(ncs, sizeof ncs, "%08x", ++nc);
    hc_md5("admin:C64 Visualizer:commodore64", ha1); snprintf(b, sizeof b, "%s:%s", method, uri); hc_md5(b, ha2);
    snprintf(b, sizeof b, "%s:%s:%s:abc:auth:%s", ha1, nonce, ncs, ha2); hc_md5(b, r);
    snprintf(line, sizeof line, "Authorization: Digest username=\"admin\", realm=\"C64 Visualizer\", nonce=\"%s\", uri=\"%s\", "
             "algorithm=MD5, response=\"%s\", qop=auth, nc=%s, cnonce=\"abc\"\r\n", nonce, uri, r, ncs);
    return line;
}
/* returns the whole answer (status line + headers + body) */
static const char *hc_req(const char *method, const char *path, const char *body) {
    static char rq[70000];
    struct tcp_pcb *c = test_connect();
    snprintf(rq, sizeof rq, "%s %s HTTP/1.1\r\n%sContent-Length: %d\r\n\r\n%s", method, path, hc_auth(method, path), body ? (int)strlen(body) : 0, body ? body : "");
    REBOOTED = 0;
    if (!setjmp(REBOOT_JMP)) { test_send(c, rq, 40); if (!strcmp(method, "POST") && !strcmp(path, "/config")) net_task(); }   /* saving happens in the main loop */
    return ((hc_pcb *)c)->out;
}
static const char *hc_body(const char *answer) { const char *b = strstr(answer, "\r\n\r\n"); return b ? b + 4 : ""; }
