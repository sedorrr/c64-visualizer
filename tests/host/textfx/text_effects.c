#include "textfx.c"
#include <stdio.h>
static int fr=0; static int want[8]; static int nw=0; static uint32_t snaps[8][PANEL_W*PANEL_H];
void display_init(void){} void display_set_brightness(int s){(void)s;}
void display_show(const uint32_t *fb){ for(int i=0;i<nw;i++) if(fr==want[i]) memcpy(snaps[i],fb,sizeof snaps[i]); }
static bool w(void){ fr++; return false; }
static void dump(int i){ for(int y=0;y<PANEL_H;y++){ for(int x=0;x<PANEL_W;x++){ uint32_t p=snaps[i][y*PANEL_W+x]; putchar(!p?'.':(p==0xFFFFFF?'*':'#')); } putchar('\n'); } }
static void run(const char *line, int a, int b, int c){
  fr=0; nw=0; textfx_play_str(line,w,16); int total=fr; fr=0; want[0]=a; want[1]=b; want[2]=c; nw=3; textfx_play_str(line,w,16);
  printf("== %s  (%.1f s)\n", line, total*0.016); for(int i=0;i<3;i++){ printf("frame %d:\n", want[i]); dump(i);} }
int main(void){
  run("{color:00C0FF}{typewriter}READY{cursor}{hold:1500}", 10, 40, 75);
  run("{color:white}{wave}Wave", 30, 34, 38);
  run("{color:red}{sparkle}SID", 30, 31, 32);
  run("{hold:500}12:34{hold:1000}", 60, 60, 60);
  return 0; }
