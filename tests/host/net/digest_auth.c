#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "net.h"
#include "pico/cyw43_arch.h"
struct tcp_pcb; struct tcp_pcb *test_connect(void); void test_send(struct tcp_pcb *p, const char *d, int split);
extern int64_t FAKE_NOW; extern int FAKE_LINK;
typedef struct { void *arg; int closed; char out[131072]; int outlen; } fakepcb;
static char NONCE[64];
static void md5sh(const char *s, char out[33]){ char cmd[700]; snprintf(cmd,sizeof cmd,"printf '%%s' '%s' | md5sum",s); FILE*p=popen(cmd,"r"); if(fscanf(p,"%32s",out)!=1) out[0]=0; pclose(p); }
/* an independent client: computes the Digest answer with the system's md5sum */
static void req(const char *what, const char *method, const char *uri, const char *pw, const char *nc, const char *nonce, const char *hdr_uri){
  char ha1[33],ha2[33],r[33],buf[1200]; snprintf(buf,sizeof buf,"admin:C64 Visualizer:%s",pw); md5sh(buf,ha1);
  snprintf(buf,sizeof buf,"%s:%s",method,hdr_uri?hdr_uri:uri); md5sh(buf,ha2);
  snprintf(buf,sizeof buf,"%s:%s:%s:abc123:auth:%s",ha1,nonce,nc,ha2); md5sh(buf,r);
  char rq[3000]; snprintf(rq,sizeof rq,"%s %s HTTP/1.1\r\nAuthorization: Digest username=\"admin\", realm=\"C64 Visualizer\", nonce=\"%s\", uri=\"%s\", algorithm=MD5, response=\"%s\", qop=auth, nc=%s, cnonce=\"abc123\"\r\nContent-Length: 0\r\n\r\n",
    method, uri, nonce, hdr_uri?hdr_uri:uri, r, nc);
  struct tcp_pcb *c=test_connect(); test_send(c,rq,40); fakepcb *f=(fakepcb*)c;
  char *st=strstr(f->out,"stale=true");
  printf("%-46s -> %.*s%s\n", what, (int)(strchr(f->out,'\r')-f->out), f->out, st?"  (stale=true: browser retries silently)":"");
}
int main(void){
  { typedef struct { unsigned magic; char ssid[33], pass[65], u64[64], u64pw[33], webpw[33]; unsigned char stream, smode, pad; unsigned check; } cfg_t;
    extern unsigned char FAKE_FLASH[]; cfg_t c; memset(&c,0,sizeof c); c.magic=0xC64E7C02u; strcpy(c.ssid,"Fritz"); strcpy(c.webpw,"Geheim64");
    const unsigned char *p=(const unsigned char*)&c; unsigned h=2166136261u; for(unsigned long i=0;i<__builtin_offsetof(cfg_t,check);i++){h^=p[i];h*=16777619u;} c.check=h;
    memcpy(FAKE_FLASH+4*1024*1024-2*4096,&c,sizeof c); }
  net_init("t"); FAKE_LINK=CYW43_LINK_UP; FAKE_NOW+=100000; net_task(); char m[80]; net_take_message(m,sizeof m);
  struct tcp_pcb *c=test_connect(); test_send(c,"GET / HTTP/1.1\r\n\r\n",5); fakepcb *f=(fakepcb*)c;
  char *w=strstr(f->out,"WWW-Authenticate: "); printf("401 header: %.*s\n", (int)(strchr(w,'\r')-w), w);
  char *n=strstr(w,"nonce=\"")+7; int k=0; while(n[k]!='"'){NONCE[k]=n[k];k++;} NONCE[k]=0;
  req("GET / with the right password","GET","/","Geheim64","00000001",NONCE,NULL);
  req("POST long jukebox URL","POST","/jb/play?path=%2FUsb0%2FC64Music%2FMUSICIANS%2FH%2FHubbard_Rob%2FCommando&sub=1&shuffle=1&min=3&silence=1","Geheim64","00000008",NONCE,NULL);
  req("GET /status, next counter","GET","/status","Geheim64","00000002",NONCE,NULL);
  req("POST /cmd?c=next, counter 4 (3 skipped)","POST","/cmd?c=next","Geheim64","00000004",NONCE,NULL);
  req("late request with counter 3 (out of order)","GET","/texts","Geheim64","00000003",NONCE,NULL);
  req("replay: counter 2 again","GET","/status","Geheim64","00000002",NONCE,NULL);
  req("wrong password","GET","/","geheim","00000005",NONCE,NULL);
  req("hash for another URL (/) used for /texts","GET","/texts","Geheim64","00000006",NONCE,"/");
  req("unknown nonce (e.g. after a restart)","GET","/","Geheim64","00000001","0123456789abcdef0123456789abcdef",NULL);
  FAKE_NOW+=3601000000LL;
  req("same nonce 1 hour later","GET","/","Geheim64","00000007",NONCE,NULL);
  return 0; }
