#include "jukebox.c"
#include <stdio.h>
int main(void){
  static char hv[300000]; int hn=0;
  for(int i=0;i<4000;i++) hn+=sprintf(hv+hn,"; /filler/%d.sid\r\n%032x=%d:%02d %d:%02d\r\n",i,i*2654435761u,i%9,i%60,(i+3)%9,(i+7)%60);
  w_begin(); st_head=st_tail=0; lp.n=0;
  for(int o=0;o<hn;){ int k=hn-o<1460?hn-o:1460; if(!room_stage(k)){ stage_drain(); continue; } stage_feed((const uint8_t*)hv+o,k); o+=k; }
  stage_drain(); w_end();
  extern unsigned char FAKE_FLASH[];
  const unsigned char *p=FAKE_FLASH+0x100000+4096, *e=p+hdr()->data_len; int i=0,bad=0;
  while(p+9<=e){ int a=p[9]|p[10]<<8,b=p[11]|p[12]<<8; if(a!=(i%9)*60+i%60||b!=((i+3)%9)*60+(i+7)%60){ if(bad<3) printf("bad %d at off %ld\n",i,(long)(p-(FAKE_FLASH+0x100000+4096))); bad++;} i++; p+=9+2*p[8]; }
  printf("count %u records scanned %d bad %d\n", hdr()->count, i, bad); }
