#include "textfx.c"
#include <stdio.h>
static int fr=0,want=0; static uint32_t snap[PANEL_W*PANEL_H];
void display_init(void){} void display_set_brightness(int s){(void)s;}
void display_show(const uint32_t *fb){ if(fr==want) memcpy(snap,fb,sizeof snap); }
static bool w(void){ fr++; return false; }
int main(void){ const char *l="{color:FFD000}{speed:35}Commando {color:00C0FF}Rob Hubbard {color:FFFFFF}4:36";
  fr=0; textfx_play_str(l,w,16); int total=fr; want=total-40; fr=0; textfx_play_str(l,w,16);
  for(int y=0;y<PANEL_H;y++){ for(int x=0;x<PANEL_W;x++){ uint32_t p=snap[y*PANEL_W+x]; putchar(!p?'.':(p==0xFFFFFF?'#':'o')); } putchar('\n'); } }
