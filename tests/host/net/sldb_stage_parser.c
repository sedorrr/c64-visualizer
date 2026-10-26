#include "jukebox.c"
#include <stdio.h>
/* replace the flash writer target: count records directly from the ring */
int main(void){
  static char hv[300000]; int hn=0;
  for(int i=0;i<4000;i++) hn+=sprintf(hv+hn,"; /filler/%d.sid\r\n%032x=%d:%02d %d:%02d\r\n",i,i*2654435761u,i%9,i%60,(i+3)%9,(i+7)%60);
  st_head=st_tail=0; lp.n=0; int got=0, bad=0, i=0;
  for(int o=0;o<hn;){ int k=hn-o<1460?hn-o:1460;
    if(!room_stage(k)){ /* drain */
      while(st_tail!=st_head){ int len=s_buf.stage[st_tail%STAGE]; uint8_t r[140]; for(int j=0;j<len;j++) r[j]=s_buf.stage[(st_tail+1+j)%STAGE]; st_tail+=1+len;
        int a=r[9]|r[10]<<8, b=r[11]|r[12]<<8; if(a!=(i%9)*60+i%60||b!=((i+3)%9)*60+(i+7)%60){ if(bad<5) printf("bad record %d (len %d)\n",i,len); bad++; } i++; got++; }
      continue; }
    stage_feed((const uint8_t*)hv+o,k); o+=k; }
  while(st_tail!=st_head){ int len=s_buf.stage[st_tail%STAGE]; st_tail+=1+len; got++; i++; }
  printf("records %d, bad %d\n",got,bad); }
