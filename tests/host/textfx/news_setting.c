/* @news on/off, and the symbols the news headlines need are in the matrix font (not the "unknown" box) */
#include "textfx.c"
#include <stdio.h>
void display_init(void){} void display_set_brightness(int s){(void)s;} void display_show(const uint32_t *fb){(void)fb;}
static int fails = 0;
static bool stop_now(void) { return true; }
#define EXPECT(c, what) do { if (c) printf("ok   %s\n", what); else { printf("FAIL %s\n", what); fails++; } } while (0)
int main(void) {
    textfx_settings_t st;
    textfx_load("x\n", &st);
    EXPECT(st.news, "@news: on without the line");
    textfx_load("@news off\nx\n", &st);
    EXPECT(!st.news, "@news off");
    textfx_load("@news on\nx\n", &st);
    EXPECT(st.news, "@news on");
    int bad = 0, w;
    for (const char *c = ",?'\"/&()+#%!.-:"; *c; c++)
        if (font_glyph_w(*c, &w) == FONT_UNKNOWN || font_glyph(*c) == FONT_UNKNOWN) { printf("   no glyph: %c\n", *c); bad++; }
    EXPECT(!bad, "all symbols of a headline have a glyph");
    EXPECT(font_glyph_w('{', &w) == FONT_UNKNOWN, "and { stays unknown (codes are removed before)");
    EXPECT(!textfx_play_str("{color:FFD000}{speed:30}+++ CSDb: X'2026 (50% done) & more? +++", stop_now, 16),
           "a headline line parses and plays (stopped at once here)");
    printf(fails ? "%d FAILED\n" : "all ok\n", fails);
    return fails ? 1 : 0;
}
