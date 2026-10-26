#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "pico/cyw43_arch.h"
int64_t FAKE_NOW=0; unsigned char FAKE_FLASH[4*1024*1024]; int REBOOTED=0; jmp_buf REBOOT_JMP; int FAKE_LINK=0; cyw43_t cyw43_state; ip_addr_t IP_ANY_FAKE;
static err_t (*ACCEPT)(void*, struct tcp_pcb*, err_t); void (*UDPRECV)(void*, struct udp_pcb*, struct pbuf*, const ip_addr_t*, u16_t);
u16_t pbuf_copy_partial(const struct pbuf *p, void *d, u16_t len, u16_t off){ u16_t n=0; for(;p&&n<len;p=p->next){ u16_t c=p->len; if(off>=c){off-=c;continue;} u16_t k=c-off; if(k>len-n)k=len-n; memcpy((char*)d+n,(char*)p->payload+off,k); n+=k; off=0;} return n; }
void pbuf_free(struct pbuf *p){(void)p;}
struct tcp_pcb *tcp_new_ip_type(int t){(void)t; return calloc(1,sizeof(struct tcp_pcb));}
err_t tcp_bind(struct tcp_pcb *p, const ip_addr_t *a, u16_t port){(void)p;(void)a;(void)port;return 0;}
struct tcp_pcb *tcp_listen_with_backlog(struct tcp_pcb *p, int b){(void)b;return p;}
void tcp_accept(struct tcp_pcb *p, err_t (*f)(void*, struct tcp_pcb*, err_t)){(void)p;ACCEPT=f;}
void tcp_arg(struct tcp_pcb *p, void *a){p->arg=a;}
void tcp_recv(struct tcp_pcb *p, err_t (*f)(void*, struct tcp_pcb*, struct pbuf*, err_t)){p->recv=(void*)f;}
void tcp_sent(struct tcp_pcb *p, err_t (*f)(void*, struct tcp_pcb*, u16_t)){p->sent=(void*)f;}
void tcp_err(struct tcp_pcb *p, void (*f)(void*, err_t)){p->err=(void*)f;}
void tcp_poll(struct tcp_pcb *p, err_t (*f)(void*, struct tcp_pcb*), u8_t iv){(void)p;(void)f;(void)iv;}
err_t tcp_write(struct tcp_pcb *p, const void *d, u16_t n, u8_t f){(void)f; if(p->outlen+n>(int)sizeof p->out){fprintf(stderr,"fake tcp_write: answer larger than the test buffer (%d bytes) - enlarge out[] in lwip/tcp.h and the tests\n",(int)sizeof p->out); abort();} memcpy(p->out+p->outlen,d,n); p->outlen+=n; return 0;}
err_t tcp_output(struct tcp_pcb *p){(void)p;return 0;}
int tcp_sndbuf(struct tcp_pcb *p){(void)p;return 5840;}
int tcp_sndqueuelen(struct tcp_pcb *p){(void)p;return 0;}
void tcp_recved(struct tcp_pcb *p, u16_t n){(void)p;(void)n;}
err_t tcp_close(struct tcp_pcb *p){p->closed=1;return 0;}
void tcp_abort(struct tcp_pcb *p){p->closed=1;}
struct tcp_pcb *LAST_REST; char JOINED[32]; char FAKE_AP_SSID[40]; int FAKE_AP_ON, FAKE_AP_STARTS, FAKE_CONNECTS, FAKE_LEAVES;
int ip4addr_aton(const char *s, ip4_addr_t *a){ return ipaddr_aton(s,a); }
struct tcp_pcb *PCBS[1024]; int NPCBS;
/* FAKE_HOST_UP: set by a test to decide which addresses answer (network scan); NULL = every address answers */
int (*FAKE_HOST_UP)(uint32_t ip); int FAKE_CONNECT_COUNT;
err_t tcp_connect(struct tcp_pcb *p, const ip_addr_t *a, u16_t port, err_t (*f)(void*, struct tcp_pcb*, err_t)){ p->remote_ip=*a; p->port=port; FAKE_CONNECT_COUNT++;
  if(NPCBS<1024) PCBS[NPCBS++]=p;
  if(FAKE_HOST_UP){ if(FAKE_HOST_UP(a->addr)) f(p->arg,p,0); return 0; }
  if(port==80) LAST_REST=p; f(p->arg,p,0); return 0;}
void test_rest_reply(const char *r){ struct tcp_pcb *p=LAST_REST; err_t (*rc)(void*, struct tcp_pcb*, struct pbuf*, err_t)=(void*)p->recv; struct pbuf b={0,(void*)r,(uint16_t)strlen(r),(uint16_t)strlen(r)}; rc(NULL,p,&b,0); rc(NULL,p,NULL,0); }
const char *test_rest_sent(void){ static char s[400]; struct tcp_pcb *p=LAST_REST; int n=p->outlen<399?p->outlen:399; memcpy(s,p->out,n); s[n]=0; char *e=strstr(s," HTTP"); if(e)*e=0; return s; }
struct udp_pcb *udp_new_ip_type(int t){(void)t; return calloc(1,sizeof(struct udp_pcb));}
err_t udp_bind(struct udp_pcb *p, const ip_addr_t *a, u16_t port){(void)a;p->port=port;return 0;}
/* the audio stream port goes to test_udp(), mDNS to test_mdns() */
void (*MDNSRECV)(void*, struct udp_pcb*, struct pbuf*, const ip_addr_t*, u16_t);
/* other ports (device beacons, two matrices): a receiver per port */
static struct { u16_t port; void (*f)(void*, struct udp_pcb*, struct pbuf*, const ip_addr_t*, u16_t); struct udp_pcb *pcb; } PORTRECV[8]; static int PORTRECV_N;
void udp_recv(struct udp_pcb *p, void (*f)(void*, struct udp_pcb*, struct pbuf*, const ip_addr_t*, u16_t), void *arg){(void)arg;
  if(p->port==5353) MDNSRECV=f; else if(p->port==11001) UDPRECV=f;
  else if(PORTRECV_N<8){ PORTRECV[PORTRECV_N].port=p->port; PORTRECV[PORTRECV_N].f=f; PORTRECV[PORTRECV_N].pcb=p; PORTRECV_N++; } }
void udp_remove(struct udp_pcb *p){ free(p); }
const ip_addr_t IP_BCAST_FAKE = { 0xFFFFFFFFu };
udp_log_t UDP_LOG[256]; int UDP_LOG_N;
void test_udp_port(u16_t port, const uint8_t *pkt, int n, uint32_t from){
  for(int i=0;i<PORTRECV_N;i++) if(PORTRECV[i].port==port){ struct pbuf *b=calloc(1,sizeof *b); b->payload=malloc(n?n:1); memcpy(b->payload,pkt,n); b->len=b->tot_len=(uint16_t)n;
    ip_addr_t a; a.addr=from; PORTRECV[i].f(NULL,PORTRECV[i].pcb,b,&a,port); return; } }
unsigned char UDP_SENT[1024]; int UDP_SENT_LEN; u16_t UDP_SENT_PORT; ip_addr_t UDP_SENT_TO;
struct pbuf *pbuf_alloc(int layer, u16_t len, int type){(void)layer;(void)type; struct pbuf *b=calloc(1,sizeof *b); b->payload=calloc(1,len); b->len=b->tot_len=len; return b;}
int UDP_SENT_COUNT;
err_t udp_sendto(struct udp_pcb *p, struct pbuf *b, const ip_addr_t *to, u16_t port){(void)p; UDP_SENT_COUNT++;
  { udp_log_t *l=&UDP_LOG[UDP_LOG_N++ % 256]; l->port=port; l->to=to->addr; l->len=b->len; memcpy(l->d,b->payload,b->len<800?b->len:800); }
  if(port==11064||port==11065){ UDP_SENT_COUNT--; return 0; }   /* device beacons, two matrices: only in UDP_LOG (older tests look at UDP_SENT) */
  memcpy(UDP_SENT,b->payload,b->len<1024?b->len:1024); UDP_SENT_LEN=b->len; UDP_SENT_PORT=port; UDP_SENT_TO=*to; return 0;}
static void udp_none(void*a, struct udp_pcb*b, struct pbuf*c, const ip_addr_t*d, u16_t e){(void)a;(void)b;(void)c;(void)d;(void)e;}
int ipaddr_aton(const char *s, ip_addr_t *a){ unsigned b[4]; if(sscanf(s,"%u.%u.%u.%u",&b[0],&b[1],&b[2],&b[3])!=4) return 0; a->addr=b[0]|b[1]<<8|b[2]<<16|b[3]<<24; return 1;}
char *ip4addr_ntoa(const ip4_addr_t *a){ static char s[16]; sprintf(s,"%u.%u.%u.%u",a->addr&255,(a->addr>>8)&255,(a->addr>>16)&255,a->addr>>24); return s;}
void flash_range_erase(uint32_t off, size_t n){ memset(FAKE_FLASH+off,0xFF,n);} void flash_range_program(uint32_t off, const uint8_t *d, size_t n){ memcpy(FAKE_FLASH+off,d,n);}
/* helpers for the test */
struct tcp_pcb *test_connect(void){ struct tcp_pcb *p=calloc(1,sizeof *p); ACCEPT(NULL,p,0); return p; }
void test_send(struct tcp_pcb *p, const char *data, int split){ int n=strlen(data); struct pbuf b2={0,(void*)(data+split),0,(uint16_t)(n-split)}; struct pbuf b1={split<n?&b2:NULL,(void*)data,(uint16_t)n,(uint16_t)(split<n?split:n)}; err_t (*f)(void*, struct tcp_pcb*, struct pbuf*, err_t)=(void*)p->recv; f(p->arg,p,&b1,0);
  err_t (*s)(void*, struct tcp_pcb*, u16_t)=(void*)p->sent; for(int i=0;i<20 && s && !p->closed;i++) s(p->arg,p,100); }
void test_udp(uint8_t *pkt, int n){ struct pbuf b={0,pkt,(uint16_t)n,(uint16_t)n}; (UDPRECV?UDPRECV:udp_none)(NULL,NULL,&b,NULL,0); }
/* a stream packet from a given sender (IPv4 as a.b.c.d -> a | b<<8 | ...) */
void test_udp_from(uint8_t *pkt, int n, uint32_t from){ struct pbuf b={0,pkt,(uint16_t)n,(uint16_t)n}; ip_addr_t a; a.addr=from; (UDPRECV?UDPRECV:udp_none)(NULL,NULL,&b,&a,0); }
/* name lookup: remembered until the test answers */
#include "lwip/dns.h"
char DNS_ASKED[64]; int DNS_ASK_COUNT; static dns_found_callback DNS_CB; static void *DNS_ARG;
err_t dns_gethostbyname(const char *name, ip_addr_t *addr, dns_found_callback cb, void *arg){(void)addr; snprintf(DNS_ASKED,sizeof DNS_ASKED,"%s",name); DNS_ASK_COUNT++; DNS_CB=cb; DNS_ARG=arg; return ERR_INPROGRESS;}
void test_dns_answer(uint32_t ip){ ip_addr_t a; a.addr=ip; if(DNS_CB) DNS_CB(DNS_ASKED, ip ? &a : NULL, DNS_ARG); }
/* the REST request the Pico sends to the Ultimate is captured here */
#include <stdbool.h>
volatile bool settings_frozen;   /* settings.c is not linked into the network tests */
#include "lwip/etharp.h"
struct netif *netif_default;
struct { uint32_t ip; struct eth_addr mac; } FAKE_ARP[8]; int FAKE_NARP;
ssize_t etharp_find_addr(struct netif *n, const ip4_addr_t *ip, struct eth_addr **e, const ip4_addr_t **r) {
    (void)n; (void)r;
    for (int i = 0; i < FAKE_NARP; i++) if (FAKE_ARP[i].ip == ip->addr) { *e = &FAKE_ARP[i].mac; return i; }
    return -1;
}
