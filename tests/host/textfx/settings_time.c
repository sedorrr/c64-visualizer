#include "textfx.c"
#include <stdio.h>
void display_init(void){} void display_set_brightness(int s){(void)s;} void display_show(const uint32_t *fb){(void)fb;}
int main(void){ textfx_settings_t st;
  textfx_load("@sleep 30\n@night 23:00-07:00\n@clock off\n@timezone europe\nx\n",&st); printf("sleep=%u night=%d..%d clock=%d tz=%d\n",st.sleep_min,st.night_from,st.night_to,st.clock,st.tz_min);
  textfx_load("@night off\n@timezone +2\nx\n",&st); printf("night=%d tz=%d clock(default)=%d sleep(default)=%u\n",st.night_from,st.tz_min,st.clock,st.sleep_min);
  textfx_load("@timezone -5:30\n@timezone utc\nx\n",&st); printf("tz utc=%d\n",st.tz_min);
  textfx_load("@timezone -5:30\nx\n",&st); printf("tz -5:30=%d\n",st.tz_min); }
