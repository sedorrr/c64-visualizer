#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lwip/tcp.h"
#include "net.h"
#include "jukebox.h"
#include "pico/cyw43_arch.h"
extern int64_t FAKE_NOW; extern int FAKE_LINK; extern unsigned char FAKE_FLASH[];
static unsigned fnv(const void *d, unsigned long n){ const unsigned char*p=d; unsigned h=2166136261u; for(unsigned long i=0;i<n;i++){h^=p[i];h*=16777619u;} return h; }
typedef struct { unsigned magic; char ssid[33], pass[65], u64[64], u64pw[33], webpw[33]; unsigned char stream, smode, pad; unsigned check; } cfg_t;
static unsigned long long rnd=88172645463325252ULL; static unsigned rn(void){ rnd^=rnd<<13; rnd^=rnd>>7; rnd^=rnd<<17; return (unsigned)(rnd>>16); }
int main(void){
  cfg_t c; memset(&c,0,sizeof c); c.magic=0xC64E7C02u; strcpy(c.ssid,"Fritz"); strcpy(c.u64,"192.168.1.64"); c.check=fnv(&c,__builtin_offsetof(cfg_t,check));
  memcpy(FAKE_FLASH+4*1024*1024-2*4096,&c,sizeof c); memset(FAKE_FLASH+0x100000,0xFF,0x200000);
  net_init("t"); FAKE_LINK=CYW43_LINK_UP; FAKE_NOW+=100000; net_task();
  /* 60000 HVSC-like records with random md5 and 1..12 sub-tunes (like the browser sends them) */
  static unsigned char all[2000000]; int len=0; static unsigned char keys[60000][16]; static int first[60000];
  for(int i=0;i<60000;i++){ for(int k=0;k<16;k++) keys[i][k]=rn()>>8; int n=1+(rn()>>8)%((i%10)?2:12);
    memcpy(all+len,keys[i],8); all[len+8]=n; for(int s=0;s<n;s++){ int v=30+(rn()>>8)%600; if(!s) first[i]=v; all[len+9+2*s]=v&255; all[len+10+2*s]=v>>8; } len+=9+2*n; }
  const char *o,*t; int on;
  jb_route(true,"/sldb/begin?hvsc=PC%3A+Songlengths.md5",NULL,0,&o,&on,&t); net_task();
  int chunks=0;
  for(int p=0;p<len;p+=32768){ int n=len-p<32768?len-p:32768; int code=jb_route(true,"/sldb/chunk",(const char*)all+p,n,&o,&on,&t); if(code!=200){printf("chunk rejected\n");} net_task(); chunks++; }
  jb_route(true,"/sldb/end",NULL,0,&o,&on,&t); net_task();
  jb_route(false,"/jb/state",NULL,0,&o,&on,&t); printf("%d chunks, %d KB -> %s\n", chunks, len/1024, strstr(o,"\"sldb\""));
  int ok=0,shown=0; for(int i=0;i<60000;i+=97){ int r=sldb_lookup(keys[i],1,NULL); if(r==first[i]) ok++; else if(shown++<4) printf("index %d: got %d expected %d\n",i,r,first[i]); } printf("lookups correct: %d of %d\n", ok, (60000+96)/97);
  return 0; }
