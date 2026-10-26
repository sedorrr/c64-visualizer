#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lwip/tcp.h"
#include "net.h"
#include "md5.h"
#include "jukebox.h"
#include "pico/cyw43_arch.h"
extern int64_t FAKE_NOW; extern int FAKE_LINK; extern unsigned char FAKE_FLASH[]; extern struct tcp_pcb *LAST_REST;
struct tcp_pcb *test_connect(void); void test_rest_reply(const char *r);
static unsigned fnv(const void *d, unsigned long n){ const unsigned char*p=d; unsigned h=2166136261u; for(unsigned long i=0;i<n;i++){h^=p[i];h*=16777619u;} return h; }
typedef struct { unsigned magic; char ssid[33], pass[65], u64[64], u64pw[33], webpw[33]; unsigned char stream, smode, pad; unsigned check; } cfg_t;
static char NONCE[40]; static int NC=1;
static void get_nonce(void){ struct tcp_pcb *c=test_connect(); struct pbuf b={0,"GET / HTTP/1.1\r\n\r\n",18,18}; err_t (*rc)(void*, struct tcp_pcb*, struct pbuf*, err_t)=(void*)c->recv; rc(c->arg,c,&b,0);
  { err_t (*s)(void*, struct tcp_pcb*, u16_t)=(void*)c->sent; for(int i=0;i<20&&s&&!c->closed;i++) s(c->arg,c,100); }
  char *n=strstr(c->out,"nonce=\"")+7; int k=0; while(n[k]!='"'){NONCE[k]=n[k];k++;} NONCE[k]=0; }
static struct tcp_pcb *req(const char *method, const char *uri, const unsigned char *body, int n){
  char ha1[33],ha2[33],r[33],buf[300]; snprintf(buf,sizeof buf,"admin:C64 Visualizer:commodore64"); md5_hex(buf,strlen(buf),ha1);
  snprintf(buf,sizeof buf,"%s:%s",method,uri); md5_hex(buf,strlen(buf),ha2); char nc[12]; sprintf(nc,"%08x",NC++);
  snprintf(buf,sizeof buf,"%s:%s:%s:xyz:auth:%s",ha1,NONCE,nc,ha2); md5_hex(buf,strlen(buf),r);
  static char all[80000]; int h=sprintf(all,"%s %s HTTP/1.1\r\nAuthorization: Digest username=\"admin\", realm=\"C64 Visualizer\", nonce=\"%s\", uri=\"%s\", response=\"%s\", qop=auth, nc=%s, cnonce=\"xyz\"\r\nContent-Length: %d\r\n\r\n",method,uri,NONCE,uri,r,nc,n);
  memcpy(all+h,body,n); struct tcp_pcb *c=test_connect(); struct pbuf b={0,all,(uint16_t)(h+n),(uint16_t)(h+n)};
  err_t (*rc)(void*, struct tcp_pcb*, struct pbuf*, err_t)=(void*)c->recv; rc(c->arg,c,&b,0);
  err_t (*s)(void*, struct tcp_pcb*, u16_t)=(void*)c->sent; for(int i=0;i<20&&s&&!c->closed;i++) s(c->arg,c,100); return c; }
int main(void){
  cfg_t c; memset(&c,0,sizeof c); c.magic=0xC64E7C02u; strcpy(c.ssid,"Fritz"); strcpy(c.u64,"192.168.1.64"); c.stream=0; c.check=fnv(&c,__builtin_offsetof(cfg_t,check));
  memcpy(FAKE_FLASH+4*1024*1024-2*4096,&c,sizeof c);
  net_init("t"); FAKE_LINK=CYW43_LINK_UP; FAKE_NOW+=100000; net_task(); char m[160]; net_take_message(m,sizeof m);
  get_nonce();
  /* a fake PSID file: header 0x7C bytes + 3000 bytes of code */
  static unsigned char sid[0x7C+3000]; memset(sid,0,sizeof sid); memcpy(sid,"PSID",4); sid[15]=3; sid[17]=1;
  strcpy((char*)sid+0x16,"Last Ninja (Wilderness)"); strcpy((char*)sid+0x36,"Ben Daglish & Anthony Lees"); for(int i=0x7C;i<(int)sizeof sid;i++) sid[i]=(unsigned char)i;
  { /* song length database with this tune: song 1 = 2:15, song 2 = 3:07 */
    uint8_t d[16]; md5_ctx_t m5; md5_init(&m5); md5_update(&m5,sid,sizeof sid); md5_final(&m5,d);
    uint8_t rec[13]; memcpy(rec,d,8); rec[8]=2; rec[9]=135; rec[10]=0; rec[11]=187; rec[12]=0;
    const char *o,*tp; int on; memset(FAKE_FLASH+0x100000,0xFF,0x200000);
    jb_route(true,"/sldb/begin?user=x",NULL,0,&o,&on,&tp); net_task();
    jb_route(true,"/sldb/chunk",(const char*)rec,13,&o,&on,&tp); net_task();
    jb_route(true,"/sldb/end",NULL,0,&o,&on,&tp); net_task(); }
  struct tcp_pcb *r=req("POST","/sid?song=2",sid,sizeof sid);
  { char *b=strstr(r->out,"\r\n\r\n"); printf("answer to the browser: %s\n", b?b+4:"?"); } printf("POST /sid -> %.*s\n",(int)(strchr(r->out,'\r')-r->out),r->out);
  struct tcp_pcb *bad=req("POST","/sid",(const unsigned char*)"hello world, no sid file here at all.......................................................................................................................",140);
  printf("POST /sid (no SID) -> %.*s | %s\n",(int)(strchr(bad->out,'\r')-bad->out),bad->out,strstr(bad->out,"\r\n\r\n")+4);
  FAKE_NOW+=20000; net_task();
  if (net_take_message(m,sizeof m)) printf("matrix message: %s\n", m);
  struct tcp_pcb *u=LAST_REST; err_t (*sent)(void*, struct tcp_pcb*, u16_t)=(void*)u->sent;
  for(int i=0;i<10 && sent;i++) sent(NULL,u,1460);
  char *hdr_end=strstr(u->out,"\r\n\r\n"); printf("request to the Ultimate:\n%.*s\n", (int)(hdr_end-u->out), u->out);
  int cl=atoi(strstr(u->out,"Content-Length: ")+16); int body=u->outlen-(int)(hdr_end+4-u->out);
  printf("Content-Length %d, body bytes sent %d -> %s\n", cl, body, cl==body?"match":"MISMATCH");
  char *data=strstr(hdr_end+4,"\r\n\r\n")+4; printf("SID data intact: %s\n", memcmp(data,sid,sizeof sid)==0?"yes":"NO");
  printf("multipart end: %s\n", strstr(u->out+u->outlen-40,"------c64vizSIDupload--")?"ok":"missing");
  r=req("GET","/status",NULL,0); printf("status while uploading: %s\n", strstr(r->out,"\"sidbusy\""));
  test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{\"errors\":[]}");
  r=req("GET","/status",NULL,0); printf("status after reply: %s\n", strstr(r->out,"\"u64state\""));
  r=req("POST","/cmd?c=u64reset",NULL,0); FAKE_NOW+=20000; net_task(); u=LAST_REST; printf("stop button -> %.*s\n",(int)(strstr(u->out," HTTP")-u->out),u->out);
  return 0; }
