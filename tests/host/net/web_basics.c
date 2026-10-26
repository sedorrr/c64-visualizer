#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "net.h"
#include "help_page.h"
struct tcp_pcb; struct tcp_pcb *test_connect(void); void test_send(struct tcp_pcb *p, const char *d, int split); void test_udp(unsigned char *p, int n);
extern int64_t FAKE_NOW; extern int FAKE_LINK; extern jmp_buf REBOOT_JMP; extern int REBOOTED; extern unsigned char FAKE_FLASH[];
typedef struct { void *arg; int closed; char out[131072]; int outlen; } fakepcb;
#include "pico/cyw43_arch.h"
static void show(const char *what, struct tcp_pcb *p){ fakepcb *f=(fakepcb*)p; char *body=strstr(f->out,"\r\n\r\n"); printf("%-26s -> %.*s | %d bytes total, closed=%d | body: %.70s\n", what, (int)(strchr(f->out,'\r')-f->out), f->out, f->outlen, f->closed, body?body+4:""); }
int main(int argc, char **argv){ (void)argv;
  net_init("@silence 8000\nHello\n");                 /* no WLAN settings in flash -> access point */
  char m[80]; if (net_take_message(m,sizeof m)) printf("message: %s\n", m);
  struct tcp_pcb *c;
  c=test_connect(); test_send(c,"GET / HTTP/1.1\r\nHost: x\r\n\r\n",10); show("GET /",c);
  c=test_connect(); test_send(c,"GET /texts HTTP/1.1\r\n\r\n",30); show("GET /texts",c);
  c=test_connect(); test_send(c,"GET /status HTTP/1.1\r\n\r\n",5); show("GET /status",c);
  c=test_connect(); test_send(c,"GET /diag HTTP/1.1\r\n\r\n",5); show("GET /diag",c);
  if (!strstr(((fakepcb*)c)->out,"200 OK") || !strstr(((fakepcb*)c)->out,"net: init")) { printf("!!! /diag does not show the log\n"); return 1; }
  /* the manual for the "Hilfe" tab: gzip bytes unchanged, with Content-Encoding */
  c=test_connect(); test_send(c,"GET /hilfe HTTP/1.1\r\n\r\n",7);
  { fakepcb *f=(fakepcb*)c; char *body=strstr(f->out,"\r\n\r\n");
    if (!strstr(f->out,"200 OK") || !strstr(f->out,"Content-Encoding: gzip\r\n") || !body || f->out+f->outlen-(body+4) != HELP_GZ_LEN
        || memcmp(body+4,HELP_GZ,HELP_GZ_LEN) || (unsigned char)body[4] != 0x1f || (unsigned char)body[5] != 0x8b) { printf("!!! /hilfe wrong\n"); return 1; }
    printf("GET /hilfe -> %d bytes gzip, Content-Encoding: gzip: ok\n", HELP_GZ_LEN); }
  c=test_connect(); test_send(c,"POST /cmd?c=next HTTP/1.1\r\nContent-Length: 0\r\n\r\n",20); show("POST /cmd?c=next",c); printf("command = %d (1 = next)\n", net_take_command());
  /* brightness in percent from the web page slider */
  net_set_brightness(45);
  c=test_connect(); test_send(c,"GET /status HTTP/1.1\r\n\r\n",5);
  if (!strstr(((fakepcb*)c)->out,"\"bright\":45,")) { printf("!!! status misses the brightness\n"); return 1; }
  const char *okp[] = { "4", "77", "100" }; int okv[] = { 4, 77, 100 };
  for (int i = 0; i < 3; i++) { char rq[120]; snprintf(rq, sizeof rq, "POST /cmd?c=bright&p=%s HTTP/1.1\r\nContent-Length: 0\r\n\r\n", okp[i]);
    c=test_connect(); test_send(c,rq,20); int cmd = net_take_command();
    printf("bright&p=%s -> command %d (expected %d)\n", okp[i], cmd, NETCMD_BRIGHT_PCT + okv[i]); if (cmd != NETCMD_BRIGHT_PCT + okv[i]) { printf("!!! wrong command\n"); return 1; } }
  const char *bad[] = { "3", "101", "x", "" };
  for (int i = 0; i < 4; i++) { char rq[120]; snprintf(rq, sizeof rq, "POST /cmd?c=bright&p=%s HTTP/1.1\r\nContent-Length: 0\r\n\r\n", bad[i]);
    c=test_connect(); test_send(c,rq,20);
    if (!strstr(((fakepcb*)c)->out,"400") || net_take_command() != NETCMD_NONE) { printf("!!! bright&p=%s accepted\n", bad[i]); return 1; } }
  printf("brightness in percent from the web page: ok\n");
  c=test_connect(); test_send(c,"GET /nope HTTP/1.1\r\n\r\n",50); show("GET /nope",c);
  /* UDP audio: 192 stereo frames of a constant +3200 -> 64 samples of 2048+3200*2/... */
  unsigned char pkt[770]; pkt[0]=0;pkt[1]=0; for(int i=0;i<192;i++){ short v=(i%2)?1600:3200; pkt[2+4*i]=v&255; pkt[3+4*i]=v>>8; pkt[4+4*i]=v&255; pkt[5+4*i]=v>>8; }
  FAKE_NOW=1000000; test_udp(pkt,770); unsigned short s[100], s2[100]; int n=net_audio_read2(s,s2,100);
  printf("UDP (stream is off by default since v37 -> expect 0): active=%d, %d samples from one packet, first L=%d R=%d (expected 2048+(3200+1600+3200)/48=%d each)\n", net_audio_active(), n, s[0], s2[0], 2048+(3200+1600+3200)/48);
  FAKE_NOW+=300000; printf("300 ms later active=%d (expect 0)\n", net_audio_active());
  if (argc > 1) goto phase2;
  /* config POST, split body */
  c=test_connect();
  if (!setjmp(REBOOT_JMP)) { test_send(c,"POST /config HTTP/1.1\r\nContent-Length: 69\r\n\r\nssid=Fritz%21Box+7590&pass=geheim123&u64=192.168.1.64&u64pw=&stream=1",60); show("POST /config",c); net_task(); }
  printf("rebooted after config save: %d\n", REBOOTED);
  { FILE *f=fopen("flash.bin","wb"); fwrite(FAKE_FLASH,1,4*1024*1024,f); fclose(f); }
  return 0;
phase2:
  { FILE *f=fopen("flash.bin","rb"); fread(FAKE_FLASH,1,4*1024*1024,f); fclose(f); }
  net_init("x"); FAKE_LINK=CYW43_LINK_UP; cyw43_state.netif[0].ip.addr=57u<<24|1u<<16|168u<<8|192u; FAKE_NOW+=100000; net_task(); if (net_take_message(m,sizeof m)) printf("message: %s\n", m);
  FAKE_NOW+=600000; net_task();
  c=test_connect(); test_send(c,"GET /status HTTP/1.1\r\n\r\n",5); show("GET /status (STA, needs the password -> 401 expected)",c);
  { fakepcb *f=(fakepcb*)c; char *b=strstr(f->out,"\r\n\r\n"); printf("  status JSON: %s\n", b+4); }
  c=test_connect();
  if (!setjmp(REBOOT_JMP)) { test_send(c,"POST /texts HTTP/1.1\r\nContent-Length: 24\r\n\r\n@silence 5000\r\nNew text\r\n",40); show("POST /texts",c); net_task(); }
  printf("rebooted after texts save: %d, flash text: [%s]\n", REBOOTED, (char*)FAKE_FLASH+0x300000);
  return 0; }
