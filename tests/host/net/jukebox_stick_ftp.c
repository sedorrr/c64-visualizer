#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lwip/tcp.h"
#include "net.h"
#include "md5.h"
#include "jukebox.h"
#include "pico/cyw43_arch.h"
extern int64_t FAKE_NOW; extern int FAKE_LINK; extern unsigned char FAKE_FLASH[]; extern struct tcp_pcb *LAST_REST, *PCBS[256]; extern int NPCBS;
void test_rest_reply(const char *r);
static unsigned fnv(const void *d, unsigned long n){ const unsigned char*p=d; unsigned h=2166136261u; for(unsigned long i=0;i<n;i++){h^=p[i];h*=16777619u;} return h; }
typedef struct { unsigned magic; char ssid[33], pass[65], u64[64], u64pw[33], webpw[33]; unsigned char stream, smode, pad; unsigned check; } cfg_t;

/* ---- fake file system of the Ultimate */
typedef struct { const char *path; char *data; int len; } ffile;
static ffile FS[16]; static int NFS;
static void addf(const char *p, char *d, int n){ FS[NFS].path=p; FS[NFS].data=d; FS[NFS].len=n; NFS++; }
static char *mksid(const char *title, const char *author, int songs, int start, int pad, int *len){
  int n=0x7C+pad; char *s=calloc(1,n); memcpy(s,"PSID",4); s[0x0F]=songs; s[0x11]=start;
  strcpy(s+0x16,title); strcpy(s+0x36,author); for(int i=0x7C;i<n;i++) s[i]=(char)(i*7+songs); *len=n; return s; }
static void md5of(const char *d,int n,char hex[33]){ md5_hex(d,n,hex); }

static void reply(struct tcp_pcb *p, const char *s){ struct pbuf b={0,(void*)s,(uint16_t)strlen(s),(uint16_t)strlen(s)};
  err_t (*rc)(void*, struct tcp_pcb*, struct pbuf*, err_t)=(void*)p->recv; if(rc) rc(p->arg,p,&b,0); }
static void data_send(struct tcp_pcb *p, const char *d, int n){
  for(int o=0;o<n;){ int k=n-o<1460?n-o:1460; struct pbuf b={0,(void*)(d+o),(uint16_t)k,(uint16_t)k};
    err_t (*rc)(void*, struct tcp_pcb*, struct pbuf*, err_t)=(void*)p->recv; if(!rc) return;
    err_t e=rc(p->arg,p,&b,0); if(e==0) o+=k; else { net_task(); } }
  err_t (*rc)(void*, struct tcp_pcb*, struct pbuf*, err_t)=(void*)p->recv; if(rc) rc(p->arg,p,NULL,0); }
static char LISTS[8][2][600]; static int NLIST;     /* path -> listing */
static char *BIGLIST;                               /* oversized listing served at /Big */
static char *DEEPLIST;                              /* listing with many sub-folders served at /Deep */
static const char *listing(const char *path){ if(BIGLIST&&!strcmp(path,"/Big")) return BIGLIST;
  if(DEEPLIST&&!strcmp(path,"/Deep")) return DEEPLIST;
  for(int i=0;i<NLIST;i++) if(!strcmp(LISTS[i][0],path)) return LISTS[i][1]; return NULL; }
static int retr_count=0; int SILENT_LIST=0;
/* the fake FTP server: answers the commands the Pico has written to its control connections */
static void serve(void){
  for(int i=0;i<NPCBS;i++){ struct tcp_pcb *p=PCBS[i]; if(p->port!=21) continue;
    if(p->seen==0){ p->seen=-1; reply(p,"220 Ultimate FTP server\r\n"); }
    if(p->seen<0) p->seen=0;
    while(1){ char *s=p->out+p->seen; char *e=memchr(s,'\n',p->outlen-p->seen); if(!e) break; *e=0; p->seen=(int)(e-p->out)+1;
      if(!strncmp(s,"USER",4)) reply(p,"331 Password please\r\n");
      else if(!strncmp(s,"PASS",4)) reply(p,"230 Logged in\r\n");
      else if(!strncmp(s,"TYPE",4)) reply(p,"200 Type set\r\n");
      else if(!strncmp(s,"PASV",4)) { reply(p,"227 Entering Passive Mode (10,0,0,99,4,1).\r\n"); }
      else if(!strncmp(s,"LIST ",5)||!strncmp(s,"RETR ",5)){
        char path[300]; strcpy(path,s+5); char *r=strchr(path,'\r'); if(r)*r=0;
        struct tcp_pcb *d=PCBS[NPCBS-1];      /* the data connection opened after PASV */
        if(s[0]=='L' && SILENT_LIST) continue;
        if(s[0]=='L'){ const char *l=listing(path); if(!l){ reply(p,"550 Not found\r\n"); continue; } reply(p,"150 Here it comes\r\n"); data_send(d,l,strlen(l)); }
        else { int f=-1; for(int k=0;k<NFS;k++) if(!strcmp(FS[k].path,path)) f=k; if(f<0){ reply(p,"550 Not found\r\n"); continue; }
               retr_count++; reply(p,"150 Sending\r\n"); data_send(d,FS[f].data,FS[f].len); }
        reply(p,"226 Done\r\n"); }
      else if(!strncmp(s,"QUIT",4)) reply(p,"221 Bye\r\n"); } } }
static void run(int frames){ for(int i=0;i<frames;i++){ net_task(); serve(); FAKE_NOW+=16000; } }
static const char *state(void){ const char *o,*t; int n; jb_route(false,"/jb/state",NULL,0,&o,&n,&t); static char b[3000]; memcpy(b,o,n); b[n]=0; return b; }
static void post(const char *path){ const char *o,*t; int n; int c=jb_route(true,path,NULL,0,&o,&n,&t); printf("POST %-60s -> %d %.*s\n",path,c,n,o); }
static void postb(const char *path, const char *body){ const char *o,*t; int n; int c=jb_route(true,path,body,(int)strlen(body),&o,&n,&t); printf("POST %-60s -> %d %.*s\n",path,c,n,o); }

int count_flash(void); void detail(void); void seq(void);
int main(void){
  cfg_t c; memset(&c,0,sizeof c); c.magic=0xC64E7C02u; strcpy(c.ssid,"Fritz"); strcpy(c.u64,"192.168.1.64"); c.check=fnv(&c,__builtin_offsetof(cfg_t,check));
  memcpy(FAKE_FLASH+4*1024*1024-2*4096,&c,sizeof c); memset(FAKE_FLASH+0x100000,0xFF,0x200000);
  net_init("t"); FAKE_LINK=CYW43_LINK_UP; FAKE_NOW+=100000; net_task(); char m[160]; net_take_message(m,sizeof m);
  /* files */
  int l1,l2,l3; char *s1=mksid("Commando","Rob Hubbard",3,1,3000,&l1), *s2=mksid("Monty on the Run","Rob Hubbard",1,1,5000,&l2), *s3=mksid("Unknown Tune","Nobody",1,1,800,&l3);
  addf("/Usb0/C64Music/Commando.sid",s1,l1); addf("/Usb0/C64Music/Monty_on_the_Run.sid",s2,l2); addf("/Usb0/C64Music/Sub/Unknown.sid",s3,l3);
  char h1[33],h2[33],h3[33]; md5of(s1,l1,h1); md5of(s2,l2,h2); md5of(s3,l3,h3);
  static char hv[400000]; int hn=sprintf(hv,"[Database]\r\n; /MUSICIANS/H/Hubbard_Rob/Commando.sid\r\n%s=4:36 1:02.600 0:30(G)\r\n; /x\r\n%s=3:50\r\n",h1,h2);
  for(int i=0;i<4000;i++) hn+=sprintf(hv+hn,"; /filler/%d.sid\r\n%032x=%d:%02d %d:%02d\r\n",i,i*2654435761u,i%9,i%60,(i+3)%9,(i+7)%60);   /* bulk like the real file */
  addf("/Usb0/DOCUMENTS/Songlengths.md5",hv,hn);
  static char us[300]; int un=sprintf(us,"# Ultimate64 SID Player - user-defined song lengths\n# Format: <md5>=<seconds> (one entry per line)\n%s=185\n",h2);
  addf("/Usb0/user_songlengths_export.txt",us,un);
  NLIST=0;
  strcpy(LISTS[NLIST][0],"/"); strcpy(LISTS[NLIST++][1],"drwxrwxrwx   1 user     ftp            0 Jan 01  1980 Usb0\r\ndrwxrwxrwx   1 user     ftp            0 Jan 01  1980 Flash\r\n");
  strcpy(LISTS[NLIST][0],"/Usb0/C64Music"); strcpy(LISTS[NLIST++][1],"-rw-rw-rw-   1 user     ftp         3124 Jan 01  1980 Commando.sid\r\n-rw-rw-rw-   1 user     ftp         5124 Jan 01  1980 Monty_on_the_Run.sid\r\n-rw-rw-rw-   1 user     ftp          100 Jan 01  1980 readme.txt\r\ndrwxrwxrwx   1 user     ftp            0 Jan 01  1980 Sub\r\n");
  strcpy(LISTS[NLIST][0],"/Usb0/C64Music/Sub"); strcpy(LISTS[NLIST++][1],"-rw-rw-rw-   1 user     ftp          924 Jan 01  1980 Unknown.sid\r\n");

  printf("== browse\n"); post("/jb/ls?path=%2F"); run(20);
  { struct tcp_pcb *d=PCBS[NPCBS-1]; printf("data connection went to %u.%u.%u.%u port %d (PASV said 10.0.0.99)\n", d->remote_ip.addr&255,(d->remote_ip.addr>>8)&255,(d->remote_ip.addr>>16)&255,d->remote_ip.addr>>24, d->port); }
  { const char *o,*t; int n; jb_route(false,"/jb/ls",NULL,0,&o,&n,&t); printf("listing: %.*s\n",n,o); }
  { extern int SILENT_LIST; SILENT_LIST=1; post("/jb/ls?path=%2FSlow"); run(5);
    const char *o,*tp; int n; jb_route(false,"/jb/ls",NULL,0,&o,&n,&tp); printf("while waiting: %.*s\n",n,o);
    FAKE_NOW+=16000000; run(3); jb_route(false,"/jb/ls",NULL,0,&o,&n,&tp); printf("after 16 s: %.*s\n",n,o); SILENT_LIST=0; }
  printf("== browse big folders (HVSC DEMOS/UNKNOWN has ~700 entries, 51 KB)\n");
  for(int t=0;t<2;t++){ static char big[80000]; int bn=0, be=0, want=t?999999:700;
    while(be<want && bn<(int)(t?70000:sizeof big)-80){ bn+=sprintf(big+bn,"-rw-rw-rw-   1 user     ftp         4096 Jan 01  1980 Big_Tune_%03d.sid\r\n",be); be++; }
    BIGLIST=big; printf("fake listing: %d entries, %d bytes\n",be,bn);
    post("/jb/ls?path=%2FBig"); run(60);
    const char *o,*tp; int n; jb_route(false,"/jb/ls",NULL,0,&o,&n,&tp);
    int cnt=0; for(const char *p=o;(p=strstr(p,"{\"d\":"));p+=5) cnt++;
    const char *last=strstr(o,"Big_Tune_699.sid");
    printf("answer: %d items, %d bytes JSON, trunc=%s%s\n",cnt,n,strstr(o,"\"trunc\":true")?"true":strstr(o,"\"trunc\":false")?"false":"MISSING",
           t==0?(last?", last entry Big_Tune_699.sid present":", last entry MISSING"):"");
    BIGLIST=NULL; }
  printf("== the import shares its buffer with the folder listing: never both at once\n");
  { const char *o,*tp; int n;
    post("/jb/ls?path=%2F"); run(20);                          /* a listing is shown (names point into the buffer) */
    jb_route(false,"/jb/ls",NULL,0,&o,&n,&tp);
    if(!strstr(o,"\"state\":2")||!strstr(o,"Usb0")){ printf("!!! listing not shown before the import\n"); return 1; }
    SILENT_LIST=1; post("/jb/ls?path=%2FSlow"); run(3);       /* a listing that is still being read */
    int c=jb_route(true,"/sldb/stick?user=%2FUsb0%2Fx.txt",NULL,0,&o,&n,&tp);
    printf("import while a listing is being read -> %d\n", c);
    if(c!=409){ printf("!!! import started while the listing buffer was in use\n"); return 1; }
    FAKE_NOW+=16000000; run(3); SILENT_LIST=0;                /* that listing times out */
    post("/jb/ls?path=%2F"); run(20); }
  printf("== song lengths from the stick (user file + HVSC, %d KB)\n", hn/1024);
  post("/sldb/stick?user=%2FUsb0%2Fuser_songlengths_export.txt&hvsc=%2FUsb0%2FDOCUMENTS%2FSonglengths.md5"); run(200);
  { const char *o,*tp; int n; jb_route(false,"/jb/ls",NULL,0,&o,&n,&tp);
    printf("listing after the import: %.40s\n", o);
    if(!strstr(o,"\"state\":0")){ printf("!!! old listing still offered after the import overwrote its buffer\n"); return 1; } }
  const char *st=state(); printf("sldb: %s\n", strstr(st,"\"sldb\"")); void seq(void); printf("check right after import:\n"); seq();
  printf("== play folder with sub-folders\n"); post("/jb/play?path=%2FUsb0%2FC64Music&sub=1&shuffle=0&min=3&silence=1"); run(30);
  printf("REST to the Ultimate: %.*s\n",(int)(strstr(LAST_REST->out," HTTP")-LAST_REST->out),LAST_REST->out); test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}");
  if(net_take_message(m,sizeof m)) printf("matrix: %s\n",m);
  st=state(); printf("state: %.*s\n",(int)(strstr(st,",\"sldb\"")-st),st);
  printf("== after the song length (Commando song 1 = 4:36 -> 276 s + 2 s) ...\n"); FAKE_NOW+=279LL*1000000; run(30);
  printf("REST: %.*s\n",(int)(strstr(LAST_REST->out," HTTP")-LAST_REST->out),LAST_REST->out); test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}");
  st=state(); printf("state: %.*s\n",(int)(strstr(st,",\"sldb\"")-st),st);
  post("/jb/next"); run(30); test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}");
  st=state(); printf("after next: %.*s\n",(int)(strstr(st,",\"sldb\"")-st),st);
  { const char *o,*tp; int n; jb_route(false,"/jb/list",NULL,0,&o,&n,&tp); printf("playlist: %.*s\n",n,o);
    if(!strstr(o,"Commando.sid")||!strstr(o,"\"pos\":2")){ printf("!!! /jb/list wrong\n"); return 1; } }   /* auto-next + /jb/next: third title plays */
  post("/jb/goto?i=0"); run(30); test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}");
  printf("goto 0 -> REST: %.*s\n",(int)(strstr(LAST_REST->out," HTTP")-LAST_REST->out),LAST_REST->out);
  if(!strstr(LAST_REST->out,"Commando.sid")){ printf("!!! goto did not play title 1\n"); return 1; }
  { const char *o,*tp; int n; jb_route(false,"/jb/state",NULL,0,&o,&n,&tp);
    if(!strstr(o,"\"songs\":3")){ printf("!!! state misses songs count: %.120s\n",o); return 1; } }
  post("/jb/song?n=2"); run(30); test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}");
  printf("song 2 -> REST: %.*s\n",(int)(strstr(LAST_REST->out," HTTP")-LAST_REST->out),LAST_REST->out);
  if(!strstr(LAST_REST->out,"songnr=2")){ printf("!!! sub-tune request misses songnr\n"); return 1; }
  post("/jb/stop"); run(10); printf("stop -> REST: %.*s\n",(int)(strstr(LAST_REST->out," HTTP")-LAST_REST->out),LAST_REST->out);
  test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}");            /* finish the stop's machine:reset */
  printf("== shared playlist import (match by file name, misses dropped, order kept)\n");
  postb("/jb/plimport?path=%2FUsb0%2FC64Music&sub=1&shuffle=0&min=3&silence=1","UNKNOWN.sid\r\nCommando.sid\nFehlt_am_Stick.sid\n");
  run(60); test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}");
  printf("import -> REST: %.*s\n",(int)(strstr(LAST_REST->out," HTTP")-LAST_REST->out),LAST_REST->out);
  if(!strstr(LAST_REST->out,"/Usb0/C64Music/Sub/Unknown.sid")){ printf("!!! import did not play the first playlist title\n"); return 1; }
  st=state();
  if(!strstr(st,"2 von 3 Titeln gefunden")||!strstr(st,"\"count\":2")){ printf("!!! import result wrong: %.300s\n",st); return 1; }
  { const char *o,*tp; int n; jb_route(false,"/jb/list",NULL,0,&o,&n,&tp); printf("import playlist: %.*s\n",n,o);
    if(!strstr(o,"[\"Unknown.sid\",\"Commando.sid\"]")){ printf("!!! import order wrong\n"); return 1; } }
  printf("== scan queue overflow: 60 sub-folders, report it honestly\n");
  { static char deep[8000]; int dn=0;
    int l4; char *s4=mksid("Lonely Tune","Nobody",1,1,900,&l4); addf("/Deep/Lonely.sid",s4,l4);
    for(int i=0;i<60;i++) dn+=sprintf(deep+dn,"drw-rw-rw-   1 user     ftp            0 Jan 01  1980 Dir_%02d\r\n",i);
    dn+=sprintf(deep+dn,"-rw-rw-rw-   1 user     ftp         3124 Jan 01  1980 Lonely.sid\r\n");
    (void)dn; DEEPLIST=deep;
    post("/jb/stop"); run(10); test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}");
    post("/jb/play?path=%2FDeep&sub=1&shuffle=0&min=3&silence=1"); run(800); test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}");
    st=state();
    if(!strstr(st,"nicht alles durchsucht")||!strstr(st,"\"count\":1")){ printf("!!! queue overflow not reported: %.300s\n",st); return 1; }
    printf("overflow reported, 1 title found\n"); DEEPLIST=NULL; }
  printf("== single title that cannot be loaded: stop instead of retrying forever\n");
  { post("/jb/stop"); run(10); test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}");
    int before=retr_count;
    post("/jb/play?path=%2FUsb0%2FC64Music%2FGibtEsNicht.sid&sub=0&shuffle=0&min=3&silence=1"); run(300);
    st=state(); int tries=0;
    for(int i=1;i<NPCBS;i++) if(PCBS[i]->port==21) tries++;
    printf("state: %.90s\n",st);
    if(!strstr(st,"\"state\":\"error\"")||!strstr(st,"kein Titel ladbar")){ printf("!!! missing title not reported as error\n"); return 1; }
    int n0=NPCBS; run(300);
    if(NPCBS!=n0){ printf("!!! jukebox keeps retrying (%d new connections)\n",NPCBS-n0); return 1; }
    printf("stopped after one try, no further FTP connections (retr before: %d)\n",before); }
  printf("== SID addresses: only the sockets, switched while the tune plays, restored afterwards\n");
  { const char *CFG="HTTP/1.1 200 OK\r\n\r\n{ \"SID Addressing\" : { \"SID Socket 1 Address\" : \"$D400\", \"SID Socket 2 Address\" : \"$D420\", "
                    "\"UltiSID 1 Address\" : \"$D400\", \"UltiSID 2 Address\" : \"$D500\" }, \"errors\" : [] }";
    const unsigned char *pf = FAKE_FLASH + 0x100000 - 4096;          /* jukebox prefs v2: [6] sidauto, [7] socket, [8..9] original */
    #define REST_IS(s) (strncmp(LAST_REST->out, s, strlen(s)) == 0)
    #define EXPECT(cond, what) do { if (!(cond)) { printf("!!! %s: %.120s\n", what, LAST_REST->out); return 1; } } while (0)
    int l5; char *s5 = mksid("Two SIDs", "Somebody", 1, 1, 900, &l5); s5[5] = 3; s5[0x7A] = 0x50;   /* PSID v3, 2nd SID at $D500 */
    addf("/Usb0/C64Music/TwoSid.sid", s5, l5);
    post("/jb/stop"); run(10); test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}");
    post("/jb/prefs?sidauto=1"); run(5);
    post("/jb/play?path=%2FUsb0%2FC64Music%2FTwoSid.sid&sub=0&shuffle=0&min=3&silence=1"); run(40);
    printf("1. request: %.60s\n", LAST_REST->out);
    EXPECT(REST_IS("GET /v1/configs/SID%20Addressing "), "the configuration is read first");
    test_rest_reply(CFG); run(10);
    printf("2. request: %.90s\n", LAST_REST->out);
    EXPECT(REST_IS("PUT /v1/configs/SID%20Addressing/SID%20Socket%202%20Address?value=%24D500 "), "socket 2 switched to $D500 (the UltiSIDs are not counted)");
    printf("   remembered in flash: socket %d, original $%04X\n", pf[7], pf[8] | pf[9] << 8);
    EXPECT(pf[7] == 2 && (pf[8] | pf[9] << 8) == 0xD420, "original address remembered before switching");
    test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}"); run(10);
    printf("3. request: %.70s\n", LAST_REST->out);
    EXPECT(REST_IS("PUT /v1/runners:sidplay?file=/Usb0/C64Music/TwoSid.sid"), "then the tune is played");
    { extern int LAST_HINT; printf("   stereo hint: %d SIDs\n", LAST_HINT); EXPECT(LAST_HINT == 2, "stereo display told: 2 SIDs"); }
    test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}");
    { const char *o,*tp; int n; jb_route(false,"/jb/state",NULL,0,&o,&n,&tp); printf("   state: %.60s\n", strstr(o,"\"sidaddr\"")); }
    post("/jb/stop"); run(5);
    EXPECT(REST_IS("PUT /v1/machine:reset"), "stop resets the C64");
    { extern int LAST_HINT; EXPECT(LAST_HINT == 0, "stop: the stereo display measures again"); }
    test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}"); run(5);
    printf("4. after stop: %.90s\n", LAST_REST->out);
    EXPECT(REST_IS("PUT /v1/configs/SID%20Addressing/SID%20Socket%202%20Address?value=%24D420 "), "socket 2 restored to $D420");
    test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}"); run(5);
    EXPECT(pf[7] == 0, "nothing left to restore in flash");

    int l6; char *s6 = mksid("One SID", "Somebody", 1, 1, 900, &l6); addf("/Usb0/C64Music/OneSid.sid", s6, l6);
    post("/jb/play?path=%2FUsb0%2FC64Music%2FOneSid.sid&sub=0&shuffle=0&min=3&silence=1"); run(40);
    EXPECT(REST_IS("GET /v1/configs/SID%20Addressing "), "single SID: configuration read");
    test_rest_reply(CFG); run(10);
    printf("5. single SID: %.60s\n", LAST_REST->out);
    EXPECT(REST_IS("PUT /v1/runners:sidplay?file=/Usb0/C64Music/OneSid.sid"), "single SID at $D400: nothing switched, played at once");
    { extern int LAST_HINT; EXPECT(LAST_HINT == 1, "stereo display told: 1 SID"); }
    test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}");

    post("/jb/play?path=%2FUsb0%2FC64Music%2FTwoSid.sid&sub=0&shuffle=0&min=3&silence=1"); run(40);
    test_rest_reply(CFG); run(10); test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}"); run(10);
    EXPECT(pf[7] == 2, "switched again for the restart test");
    test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}");               /* sidplay answered: the tune plays */
    jb_init(); run(5);                                                /* the Pico restarts in the middle of the tune */
    printf("6. after a restart: %.90s\n", LAST_REST->out);
    EXPECT(REST_IS("PUT /v1/configs/SID%20Addressing/SID%20Socket%202%20Address?value=%24D420 "), "restart: socket 2 restored");
    test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}"); run(5);
    EXPECT(pf[7] == 0, "restart: nothing left to restore");
    printf("== showing the addresses on the matrix (option sidshow)\n");
    { char msg[200]; while (net_take_message(msg, sizeof msg)) ;    /* drop older messages */
      post("/jb/prefs?sidshow=1"); run(5);
      EXPECT(REST_IS("GET /v1/configs/SID%20Addressing "), "switched on: the addresses are read");
      test_rest_reply(CFG); run(3);
      int got = net_take_message(msg, sizeof msg); printf("   matrix: %s\n", got ? msg : "-");
      EXPECT(got && strstr(msg, "SID 1: D400  SID 2: D420"), "a) the current addresses are shown");
      FAKE_NOW += 61000000; run(3);                                 /* a minute later: read again */
      EXPECT(REST_IS("GET /v1/configs/SID%20Addressing "), "read again after a minute");
      test_rest_reply(CFG); run(3);
      EXPECT(!net_take_message(msg, sizeof msg), "unchanged: nothing shown");
      FAKE_NOW += 61000000; run(3);
      const char *CFG2 = "HTTP/1.1 200 OK\r\n\r\n{ \"SID Addressing\" : { \"SID Socket 1 Address\" : \"$D400\", \"SID Socket 2 Address\" : \"$D500\" } }";
      test_rest_reply(CFG2); run(3);
      got = net_take_message(msg, sizeof msg); printf("   changed elsewhere: %s\n", got ? msg : "-");
      EXPECT(got && strstr(msg, "SID 2: D500"), "changed elsewhere: the new addresses are shown");
      post("/jb/play?path=%2FUsb0%2FC64Music%2FOneSid.sid&sub=0&shuffle=0&min=3&silence=1"); run(40);   /* back to a known state */
      test_rest_reply(CFG); run(10); test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}"); run(3);
      while (net_take_message(msg, sizeof msg)) ;
      post("/jb/play?path=%2FUsb0%2FC64Music%2FTwoSid.sid&sub=0&shuffle=0&min=3&silence=1"); run(40);
      test_rest_reply(CFG); run(10); test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}"); run(10);
      got = net_take_message(msg, sizeof msg); printf("   title: %s\n", got ? msg : "-");
      EXPECT(got && strstr(msg, "Two SIDs") && strstr(msg, "SID 1: D400  SID 2: D500"), "b) the switched addresses come with the title");
      test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{}"); }
    printf("SID address checks passed\n"); }
  uint8_t d[16]; for(int i=0;i<16;i++){ unsigned v; sscanf(h1+2*i,"%2x",&v); d[i]=v; }
  printf("lookup Commando songs 1..3: %d %d %d   Monty (user file wins over HVSC 3:50): %d\n", sldb_lookup(d,1,NULL), sldb_lookup(d,2,NULL), sldb_lookup(d,3,NULL),
         ({ uint8_t e[16]; for(int i=0;i<16;i++){ unsigned v; sscanf(h2+2*i,"%2x",&v); e[i]=v; } sldb_lookup(e,1,NULL); }));
  printf("FTP RETR count: %d, records in flash: %d\n", retr_count, count_flash());
  detail(); seq(); return 0; }
int count_flash(void){ const unsigned char *p=FAKE_FLASH+0x100000+4096; unsigned len=*(unsigned*)(FAKE_FLASH+0x100000+8); const unsigned char *e=p+len; int c=0; while(p+9<=e){c++; p+=9+2*p[8];} return c; }
void detail(void){ const unsigned char *p=FAKE_FLASH+0x100000+4096; unsigned len=*(unsigned*)(FAKE_FLASH+0x100000+8); const unsigned char *e=p+len; int z=0,o=0; while(p+9<=e){ static const unsigned char zero[8]; if(!memcmp(p,zero,8)) z++; else o++; p+=9+2*p[8];} printf("zero-prefix (filler) %d, others %d\n",z,o); }
void others(void){ const unsigned char *p=FAKE_FLASH+0x100000+4096; unsigned len=*(unsigned*)(FAKE_FLASH+0x100000+8); const unsigned char *e=p+len; int idx=0; while(p+9<=e){ static const unsigned char zero[8]; if(memcmp(p,zero,8)){ printf("rec %d: ",idx); for(int i=0;i<8;i++)printf("%02x",p[i]); printf(" n=%d first=%d (offset %ld)\n",p[8],p[9]|p[10]<<8,(long)(p-(FAKE_FLASH+0x100000+4096))); } idx++; p+=9+2*p[8];} }
void seq(void){ const unsigned char *p=FAKE_FLASH+0x100000+4096; unsigned len=*(unsigned*)(FAKE_FLASH+0x100000+8); const unsigned char *e=p+len; int i=0, idx=0; static const unsigned char zero[8];
  while(p+9<=e){ if(!memcmp(p,zero,8)){ int a=p[9]|p[10]<<8, b=p[11]|p[12]<<8; int ea=(i%9)*60+i%60, eb=((i+3)%9)*60+(i+7)%60;
      if(a!=ea||b!=eb){ printf("filler mismatch at rec %d: got %d/%d expected i=%d %d/%d\n",idx,a,b,i,ea,eb); int j; for(j=i;j<i+5;j++) if(a==(j%9)*60+j%60 && b==((j+3)%9)*60+(j+7)%60) break; if(j<i+5){ printf("  -> it is filler %d, skipped %d\n",j,j-i); i=j; } }
      i++; } idx++; p+=9+2*p[8]; } }
