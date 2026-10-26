#include <stdio.h>
#include <string.h>
#include "net.h"
#include "pico/cyw43_arch.h"
extern int64_t FAKE_NOW; extern int FAKE_LINK; extern unsigned char FAKE_FLASH[];
void test_udp(unsigned char *p, int n);
static unsigned int fnv(const void *d, unsigned long n){ const unsigned char*p=d; unsigned int h=2166136261u; for(unsigned long i=0;i<n;i++){h^=p[i];h*=16777619u;} return h; }
typedef struct { unsigned magic; char ssid[33], pass[65], u64[64], u64pw[33], webpw[33]; unsigned char stream, pad[2]; unsigned check; } cfg_t;
int main(int argc,char**argv){ int on=argc>1;
  cfg_t c; memset(&c,0,sizeof c); c.magic=0xC64E7C02u; strcpy(c.ssid,"Fritz"); strcpy(c.u64,"192.168.1.64"); c.stream=on; c.check=fnv(&c,__builtin_offsetof(cfg_t,check));
  memcpy(FAKE_FLASH+4*1024*1024-2*4096,&c,sizeof c);
  net_init("t"); FAKE_LINK=CYW43_LINK_UP; FAKE_NOW+=100000; net_task();
  unsigned char pkt[770]={0}; for(int i=2;i<770;i+=2){pkt[i]=0x10;pkt[i+1]=0x20;}
  FAKE_NOW+=10000; test_udp(pkt,770);
  printf("stream %s in settings: packet arrives -> used: %s\n", on?"ON ":"OFF", net_audio_active()?"yes":"no");
  if (on) {   /* left and right are kept apart: left +3200, right -1600 -> 2048+3200/16, 2048-1600/16 */
    net_audio_skip(net_audio_avail());
    for (int i = 0; i < 192; i++) { short l = 3200, r = -1600; pkt[2+4*i]=l&255; pkt[3+4*i]=(l>>8)&255; pkt[4+4*i]=r&255; pkt[5+4*i]=(r>>8)&255; }
    FAKE_NOW += 4000; test_udp(pkt, 770);
    unsigned short L[100], R[100]; int n = net_audio_read2(L, R, 100);
    printf("stereo packet: %d samples, L=%d R=%d (expected 64, %d, %d)\n", n, L[0], R[0], 2048 + 3200 / 16, 2048 - 1600 / 16);
    if (n != 64 || L[0] != 2048 + 3200 / 16 || R[0] != 2048 - 1600 / 16) { printf("!!! channels not kept apart\n"); return 1; }
  }
  return 0; }
