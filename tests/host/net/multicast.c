#include <stdio.h>
#include <string.h>
#include "net.h"
#include "pico/cyw43_arch.h"
extern int64_t FAKE_NOW; extern int FAKE_LINK; extern unsigned char FAKE_FLASH[]; extern char JOINED[32];
void test_rest_reply(const char *r); const char *test_rest_sent(void);
static unsigned fnv(const void *d, unsigned long n){ const unsigned char*p=d; unsigned h=2166136261u; for(unsigned long i=0;i<n;i++){h^=p[i];h*=16777619u;} return h; }
typedef struct { unsigned magic; char ssid[33], pass[65], u64[64], u64pw[33], webpw[33]; unsigned char stream, smode, pad; unsigned check; } cfg_t;
int main(int argc, char **argv){ int smode=argv[1][0]-'0'; const char *u64cfg=argc>2?argv[2]:"";
  cfg_t c; memset(&c,0,sizeof c); c.magic=0xC64E7C02u; strcpy(c.ssid,"Fritz"); strcpy(c.u64,"192.168.1.64"); c.stream=1; c.smode=smode; c.check=fnv(&c,__builtin_offsetof(cfg_t,check));
  memcpy(FAKE_FLASH+4*1024*1024-2*4096,&c,sizeof c);
  net_init("t"); FAKE_LINK=CYW43_LINK_UP; cyw43_state.netif[0].ip.addr=57u<<24|1u<<16|168u<<8|192u; FAKE_NOW+=100000; net_task();
  FAKE_NOW+=600000; net_task(); printf("  1st request: %s\n", test_rest_sent());
  if (smode) { char r[300]; snprintf(r,sizeof r,"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\n  \"Data Streams\": {\n    \"Stream VIC to\": \"239.0.1.64\",\n    \"Stream Audio to\": \"%s\",\n    \"Stream Debug to\": \"239.0.1.66\"\n  },\n  \"errors\": []\n}",u64cfg);
    test_rest_reply(r); FAKE_NOW+=3100000; net_task(); printf("  2nd request: %s   (joined group: %s)\n", test_rest_sent(), JOINED[0]?JOINED:"-"); }
  return 0; }
