/* Retro news from the Raspberry Pi jukebox: a few headlines, scrolled between the idle texts (Pico 2 W only).
   They arrive as "C64NEWS1\n<line>\n<line>..." over UDP (stream port, also along the forwarding chain) or as the
   body of POST /news; an empty list clears them. Only in RAM - after NEWS_KEEP_MS without a new list they are gone. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define NEWS_MAX 8                      /* headlines kept */
#define NEWS_LEN 90                     /* characters per headline (a text line holds ~100) */
#define NEWS_KEEP_MS (3u * 3600u * 1000u)
#define NEWS_MAGIC "C64NEWS1\n"

/* lwIP context: copy a list (lines separated by \n) for the main program; false = the previous one is not taken yet */
bool news_put(const char *text, int len);
/* main program: take over a new list (net_task) */
void news_task(void);
/* number of headlines (0 once expired); main program: headline i, only characters the matrix font has */
int  news_count(void);
bool news_get(int i, char *out, int len);
/* for /status: headlines kept, ms since the last list (-1 = none yet) */
int  news_age_ms(void);
