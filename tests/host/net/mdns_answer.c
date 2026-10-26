/* own mDNS responder: A + NSEC for c64viz.local, NSEC for AAAA (no 5 s wait on macOS), legacy queries, bad input */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "mdns_lite.h"
static int fails = 0;
static void check(int ok, const char *what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }
static const uint8_t IP[4] = { 192, 168, 1, 231 };

/* query with up to two questions; second name may be a compression pointer to the first */
static int query(uint8_t *q, uint16_t id, const char *host, int t1, int t2, bool ptr2) {
    int k = 0; q[k++] = id >> 8; q[k++] = id & 255; q[k++] = 0; q[k++] = 0; q[k++] = 0; q[k++] = t2 ? 2 : 1;
    memset(q + k, 0, 6); k += 6;
    int name = k; const char *s = host;
    while (*s) { const char *d = strchr(s, '.'); int l = d ? (int)(d - s) : (int)strlen(s); q[k++] = (uint8_t)l; memcpy(q + k, s, (size_t)l); k += l; s += l + (d ? 1 : 0); }
    q[k++] = 0; q[k++] = 0; q[k++] = (uint8_t)t1; q[k++] = 0; q[k++] = 1;
    if (t2) {
        if (ptr2) { q[k++] = 0xC0; q[k++] = (uint8_t)name; } else { memcpy(q + k, q + name, (size_t)(k - 4 - name)); k += k - 4 - name; }
        q[k++] = 0; q[k++] = (uint8_t)t2; q[k++] = 0; q[k++] = 1;
    }
    return k;
}
/* independent reader: skips names, collects record types; returns false if the packet is malformed */
typedef struct { int type, cls, ttl, rdlen, rd; } rec_t;
static int skip_name(const uint8_t *m, int n, int p) {
    while (p < n) { if ((m[p] & 0xC0) == 0xC0) return p + 2; if (!m[p]) return p + 1; p += 1 + m[p]; }
    return -1;
}
static bool parse(const uint8_t *m, int n, int *qd, int *an, int *ar, rec_t *r, int *nr) {
    if (n < 12) return false;
    *qd = m[4] << 8 | m[5]; *an = m[6] << 8 | m[7]; *ar = m[10] << 8 | m[11];
    int p = 12; *nr = 0;
    for (int i = 0; i < *qd; i++) { p = skip_name(m, n, p); if (p < 0) return false; p += 4; }
    for (int i = 0; i < *an + *ar; i++) {
        p = skip_name(m, n, p); if (p < 0 || p + 10 > n) return false;
        rec_t *x = &r[(*nr)++];
        x->type = m[p] << 8 | m[p + 1]; x->cls = m[p + 2] << 8 | m[p + 3];
        x->ttl = m[p + 4] << 24 | m[p + 5] << 16 | m[p + 6] << 8 | m[p + 7]; x->rdlen = m[p + 8] << 8 | m[p + 9];
        x->rd = p + 10; p += 10 + x->rdlen; if (p > n) return false;
    }
    return p == n;
}

int main(void) {
    uint8_t q[256], r[256]; rec_t rr[8]; int qd, an, ar, nr, n, k;

    printf("== A question (macOS asks from port 5353)\n");
    n = query(q, 0, "c64viz.local", 1, 0, false); k = mdnsl_answer(q, n, IP, false, r, sizeof r);
    check(k > 0 && parse(r, k, &qd, &an, &ar, rr, &nr), "answer is a well-formed DNS message");
    check(an == 1 && ar == 1 && rr[0].type == 1 && rr[1].type == 47, "A record + NSEC as additional record");
    check(rr[0].rdlen == 4 && !memcmp(r + rr[0].rd, IP, 4), "A record carries 192.168.1.231");
    check(rr[0].cls == 0x8001 && rr[0].ttl == 120, "cache-flush class, TTL 120 s");
    check(r[rr[1].rd + rr[1].rdlen - 3] == 0 && r[rr[1].rd + rr[1].rdlen - 2] == 1 && r[rr[1].rd + rr[1].rdlen - 1] == 0x40, "NSEC bitmap: only type A exists");

    printf("== AAAA question (the one macOS waited 5 s for)\n");
    n = query(q, 0, "c64viz.local", 28, 0, false); k = mdnsl_answer(q, n, IP, false, r, sizeof r);
    check(k > 0 && parse(r, k, &qd, &an, &ar, rr, &nr) && an == 1 && ar == 0 && rr[0].type == 47, "answered with NSEC (no IPv6 address)");

    printf("== A and AAAA in one packet, second name compressed\n");
    n = query(q, 0, "c64viz.local", 1, 28, true); k = mdnsl_answer(q, n, IP, false, r, sizeof r);
    check(k > 0 && parse(r, k, &qd, &an, &ar, rr, &nr) && an == 2 && ar == 0 && rr[0].type == 1 && rr[1].type == 47, "A + NSEC as answers");

    printf("== other cases\n");
    n = query(q, 0, "C64VIZ.Local", 1, 0, false);
    check(mdnsl_answer(q, n, IP, false, r, sizeof r) > 0, "name compared case-insensitively");
    n = query(q, 0, "other.local", 1, 28, false);
    check(mdnsl_answer(q, n, IP, false, r, sizeof r) == 0, "other names ignored");
    n = query(q, 0, "c64viz.local.example", 1, 0, false);
    check(mdnsl_answer(q, n, IP, false, r, sizeof r) == 0, "longer name with the same start ignored");
    n = query(q, 0, "c64viz.local", 1, 0, false); q[2] = 0x84;
    check(mdnsl_answer(q, n, IP, false, r, sizeof r) == 0, "responses from others ignored");

    printf("== legacy one-shot query (e.g. dig -p 5353), not from port 5353\n");
    n = query(q, 0x1234, "c64viz.local", 1, 0, false); k = mdnsl_answer(q, n, IP, true, r, sizeof r);
    check(k > 0 && parse(r, k, &qd, &an, &ar, rr, &nr) && r[0] == 0x12 && r[1] == 0x34 && qd == 1, "id and question echoed");
    check(rr[0].cls == 1 && rr[0].ttl == 10, "no cache-flush bit, TTL 10 s");

    printf("== broken input\n");
    n = query(q, 0, "c64viz.local", 1, 0, false);
    check(mdnsl_answer(q, n - 6, IP, false, r, sizeof r) == 0, "truncated question");
    n = query(q, 0, "c64viz.local", 1, 0, false); q[12] = 0xC0; q[13] = 12;
    check(mdnsl_answer(q, n, IP, false, r, sizeof r) == 0, "compression pointer loop");
    check(mdnsl_answer(q, 5, IP, false, r, sizeof r) == 0, "shorter than a header");

    printf("== device name (several visualizers in one network)\n");
    check(mdnsl_name_valid("wohnzimmer") && mdnsl_name_valid("c64-2") && mdnsl_name_valid("a"), "valid names");
    check(!mdnsl_name_valid("") && !mdnsl_name_valid("-x") && !mdnsl_name_valid("x-") && !mdnsl_name_valid("Wohnzimmer")
          && !mdnsl_name_valid("a b") && !mdnsl_name_valid("wohnzimmer.local") && !mdnsl_name_valid("abcdefghijklmnopq"), "invalid: empty, -, upper case, space, dot, 17 chars");
    mdnsl_set_name("wohnzimmer");
    check(!strcmp(mdnsl_name(), "wohnzimmer"), "name set");
    n = query(q, 0, "wohnzimmer.local", 1, 0, false); k = mdnsl_answer(q, n, IP, false, r, sizeof r);
    check(k > 0 && parse(r, k, &qd, &an, &ar, rr, &nr) && rr[0].type == 1, "answers for wohnzimmer.local");
    n = query(q, 0, "c64viz.local", 1, 0, false);
    check(mdnsl_answer(q, n, IP, false, r, sizeof r) == 0, "no longer for c64viz.local");
    mdnsl_set_name("Ungueltig!");
    check(!strcmp(mdnsl_name(), "c64viz"), "invalid name -> c64viz");

    printf("== conflict: another device answers for our name\n");
    static const uint8_t OTHER[4] = { 192, 168, 1, 224 };
    n = query(q, 0, "c64viz.local", 1, 0, false); k = mdnsl_answer(q, n, OTHER, false, r, sizeof r);   /* its answer */
    check(mdnsl_conflict(r, k, IP), "A record of c64viz.local with another address: conflict");
    check(!mdnsl_conflict(r, k, OTHER), "our own answer (same address, multicast loop): no conflict");
    check(!mdnsl_conflict(q, n, IP), "a question is no conflict");
    mdnsl_set_name("test");
    check(!mdnsl_conflict(r, k, IP), "answer for another name: no conflict");
    mdnsl_set_name("c64viz");
    n = query(q, 0, "c64viz.local", 28, 0, false); k = mdnsl_answer(q, n, OTHER, false, r, sizeof r);
    check(!mdnsl_conflict(r, k, IP), "only NSEC (no address): no conflict");
    n = query(q, 0, "c64viz.local", 1, 0, false); k = mdnsl_answer(q, n, OTHER, false, r, sizeof r);
    check(!mdnsl_conflict(r, 20, IP) && !mdnsl_conflict(r, 38, IP) && mdnsl_conflict(r, 40, IP), "answer cut off before the address: no conflict, no crash");
    printf("%s\n", fails ? "SOME MDNS CHECKS FAILED" : "all mDNS checks passed");
    return fails ? 1 : 0;
}
