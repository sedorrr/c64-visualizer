#include "textfx.c"
#include <stdio.h>
void display_init(void){} void display_set_brightness(int s){(void)s;} void display_show(const uint32_t *fb){(void)fb;}
int main(void){ textfx_settings_t st;
  textfx_load("@sleepcolor FF4000\n@sleepstyle breathe\n@sleep 20\nx\n",&st); printf("color %06X style %d sleep %u\n",st.sleep_color,st.sleep_style,st.sleep_min);
  textfx_load("@sleepcolor auto\nx\n",&st); printf("auto -> %06X\n",st.sleep_color);
  textfx_load("@sleepcolor green\nx\n",&st); printf("green -> %06X\n",st.sleep_color); }
