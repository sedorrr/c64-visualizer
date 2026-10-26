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
static void post(const char *path, const char *body, int len){ const char *o,*t; int n; int c=jb_route(true,path,body,len,&o,&n,&t); printf("POST %-40s -> %d %.*s\n",path,c,n,o); }
int main(void){
  cfg_t c; memset(&c,0,sizeof c); c.magic=0xC64E7C02u; strcpy(c.ssid,"Fritz"); strcpy(c.u64,"192.168.1.64"); c.check=fnv(&c,__builtin_offsetof(cfg_t,check));
  memcpy(FAKE_FLASH+4*1024*1024-2*4096,&c,sizeof c); memset(FAKE_FLASH+0x100000,0xFF,0x200000);
  net_init("t"); FAKE_LINK=CYW43_LINK_UP; FAKE_NOW+=100000; net_task();
  FILE*f=fopen("../../web/fixtures/c_records.bin","rb"); static char recs[4096]; int n=fread(recs,1,sizeof recs,f); fclose(f);
  post("/sldb/begin?user=PC%3A+user.txt&hvsc=",NULL,0); net_task();
  post("/sldb/chunk",recs,100); post("/sldb/chunk",recs+100,10);   /* second one while the first is not written yet */
  net_task(); post("/sldb/chunk",recs+100,n-100); net_task();
  post("/sldb/end",NULL,0); net_task();
  const char *o,*t; int k; jb_route(false,"/jb/state",NULL,0,&o,&k,&t); printf("%s\n", strstr(o,"\"sldb\""));
  uint8_t d[16]; const char *h="14ffd691d263fc470edd24a2e3286131"; for(int i=0;i<16;i++){ unsigned v; sscanf(h+2*i,"%2x",&v); d[i]=v; }
  printf("lookup 14ffd691... (user file 185 s): %d, song 2 of the same tune: %d\n", sldb_lookup(d,1,NULL), sldb_lookup(d,2,NULL));
  const char *h2="d35a9bc7666fdcb4a1b2c3d4e5f60718"; for(int i=0;i<16;i++){ unsigned v; sscanf(h2+2*i,"%2x",&v); d[i]=v; }
  printf("HVSC tune songs 1..4: %d %d %d %d\n", sldb_lookup(d,1,NULL), sldb_lookup(d,2,NULL), sldb_lookup(d,3,NULL), sldb_lookup(d,4,NULL));
  const char *h3="79fa12a55943f8856482d82922ce75a3"; for(int i=0;i<16;i++){ unsigned v; sscanf(h3+2*i,"%2x",&v); d[i]=v; }
  printf("3:50.5 / 0:12.499 -> %d %d\n", sldb_lookup(d,1,NULL), sldb_lookup(d,2,NULL));
  /* erase the database from the web page: /sldb/clear, executed in jb_task */
  post("/sldb/clear",NULL,0); net_task();
  jb_route(false,"/jb/state",NULL,0,&o,&k,&t); printf("after clear: %s\n", strstr(o,"\"sldb\""));
  int gone = sldb_lookup(d,1,NULL);
  printf("lookup after clear: %d\n", gone);
  if (gone != -1 || !strstr(o,"\"count\":0")) { printf("!!! clear did not empty the database\n"); return 1; }
  return 0; }
