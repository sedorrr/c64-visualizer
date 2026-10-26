/* Retro news from the Raspberry Pi jukebox (see news.h). */
#include "news.h"
#include <string.h>
#include "pico/stdlib.h"
#include "diag.h"

static char s_in[NEWS_MAX * (NEWS_LEN + 1) + 16];     /* filled in the lwIP context ... */
static volatile int s_in_len = -1;                     /* ... -1 = empty, else waiting for news_task */
static char s_news[NEWS_MAX][NEWS_LEN + 1];
static int s_n = 0;
static uint32_t s_at = 0;
static bool s_any = false;

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

bool news_put(const char *text, int len) {
    if (s_in_len >= 0) return false;
    if (len < 0) len = 0;
    if (len > (int)sizeof s_in) len = (int)sizeof s_in;
    memcpy(s_in, text, (size_t)len);
    s_in_len = len;
    return true;
}

/* the characters the matrix font has; German UTF-8 letters spelt out, everything else a space ({ } are codes) */
static int clean(const char *s, int n, char *out, int len) {
    static const char keep[] = " !.-:,?'\"/&()+#%";
    int o = 0;
    for (int i = 0; i < n && o < len - 2; i++) {
        unsigned char c = (unsigned char)s[i];
        const char *sub = NULL;
        if (c == 0xC3 && i + 1 < n) {
            switch ((unsigned char)s[++i]) {
                case 0xA4: sub = "ae"; break; case 0xB6: sub = "oe"; break; case 0xBC: sub = "ue"; break;
                case 0x84: sub = "Ae"; break; case 0x96: sub = "Oe"; break; case 0x9C: sub = "Ue"; break;
                case 0x9F: sub = "ss"; break; default: sub = " "; break;
            }
        } else if (c >= 0x80) {                                   /* other UTF-8: skip the whole character */
            while (i + 1 < n && ((unsigned char)s[i + 1] & 0xC0) == 0x80) i++;
            sub = " ";
        }
        if (sub) {
            for (; *sub && o < len - 1; sub++) out[o++] = *sub;
        } else if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c && strchr(keep, c))) {
            out[o++] = (char)c;
        } else {
            out[o++] = ' ';
        }
        if (o >= 2 && out[o - 1] == ' ' && out[o - 2] == ' ') o--;    /* no runs of spaces */
    }
    while (o > 0 && out[o - 1] == ' ') o--;
    int b = 0;
    while (b < o && out[b] == ' ') b++;
    memmove(out, out + b, (size_t)(o - b));
    out[o - b] = 0;
    return o - b;
}

void news_task(void) {
    int len = s_in_len;
    if (len < 0) return;
    s_n = 0;
    for (int i = 0; i < len && s_n < NEWS_MAX; ) {
        int e = i;
        while (e < len && s_in[e] != '\n') e++;
        if (clean(s_in + i, e - i, s_news[s_n], NEWS_LEN + 1) > 0) s_n++;
        i = e + 1;
    }
    s_at = now_ms();
    s_any = true;
    s_in_len = -1;
    diag_printf("news: %d headlines\n", s_n);
}

int news_count(void) {                                         /* no writes: /status reads it in the lwIP context */
    return s_n && now_ms() - s_at <= NEWS_KEEP_MS ? s_n : 0;
}

bool news_get(int i, char *out, int len) {
    if (i < 0 || i >= news_count() || len < 1) return false;
    strncpy(out, s_news[i], (size_t)len - 1);
    out[len - 1] = 0;
    return true;
}

int news_age_ms(void) {
    return s_any ? (int)(now_ms() - s_at) : -1;
}
