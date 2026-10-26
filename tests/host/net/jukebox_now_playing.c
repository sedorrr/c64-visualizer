/* Now playing from the PC jukebox (Ultimate64 SID Jukebox): POST /jb/np. The PC plays the tunes itself and tells
   the Pico title, length and SIDs - the Pico shows the title, drives the progress dot and the stereo display. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lwip/tcp.h"
#include "net.h"
#include "jukebox.h"
#include "net_internal.h"
#include "pico/cyw43_arch.h"
extern int64_t FAKE_NOW; extern int FAKE_LINK; extern unsigned char FAKE_FLASH[]; extern struct tcp_pcb *LAST_REST, *PCBS[256]; extern int NPCBS;
extern int LAST_HINT;
void test_rest_reply(const char *r);
static unsigned fnv(const void *d, unsigned long n){ const unsigned char*p=d; unsigned h=2166136261u; for(unsigned long i=0;i<n;i++){h^=p[i];h*=16777619u;} return h; }
typedef struct { unsigned magic; char ssid[33], pass[65], u64[64], u64pw[33], webpw[33]; unsigned char stream, smode, pad; unsigned check; } cfg_t;

static int fails = 0;
#define EXPECT(cond, what) do { if (cond) printf("ok   %s\n", what); else { printf("FAIL %s\n", what); fails++; } } while (0)
static void run(int frames){ for(int i=0;i<frames;i++){ net_task(); FAKE_NOW+=16000; } }
static int post(const char *path){ const char *o,*t; int n; int c=jb_route(true,path,NULL,0,&o,&n,&t); printf("POST %-90s -> %d %.*s\n",path,c,n,o); return c; }
static const char *np(void){ const char *o,*t; int n; jb_route(false,"/jb/np",NULL,0,&o,&n,&t); static char b[400]; memcpy(b,o,n); b[n]=0; return b; }
static const char *state(void){ const char *o,*t; int n; jb_route(false,"/jb/state",NULL,0,&o,&n,&t); static char b[3000]; memcpy(b,o,n); b[n]=0; return b; }
static int msg(char *m, int n){ int got=0; while(net_take_message(m,n)) got++; return got; }   /* last message, count */
#define REST_IS(s) (LAST_REST && strncmp(LAST_REST->out, s, strlen(s)) == 0)

int main(void){
  cfg_t c; memset(&c,0,sizeof c); c.magic=0xC64E7C02u; strcpy(c.ssid,"Fritz"); strcpy(c.u64,"192.168.1.64"); c.check=fnv(&c,__builtin_offsetof(cfg_t,check));
  memcpy(FAKE_FLASH+4*1024*1024-2*4096,&c,sizeof c); memset(FAKE_FLASH+0x100000,0xFF,0x200000);
  net_init("t"); FAKE_LINK=CYW43_LINK_UP; FAKE_NOW+=100000; run(3); char m[200]; msg(m,sizeof m);

  printf("== a tune starts on the PC\n");
  EXPECT(net_jb_progress() == -1, "nothing plays: no progress dot");
  post("/jb/np?new=1&title=Next%20Awakening&author=Jeroen%20Tel&secs=117&el=0&song=1&songs=3&sid=D400%2CD420");
  run(3);
  int got = msg(m,sizeof m); printf("   matrix: %s\n", got ? m : "-");
  EXPECT(got == 1 && strstr(m,"Next Awakening") && strstr(m,"Jeroen Tel") && strstr(m,"1:57"), "title, author and length scroll once");
  EXPECT(LAST_HINT == 2, "two SIDs: the stereo display is told");
  printf("   /jb/np: %s\n", np());
  EXPECT(strstr(np(),"\"on\":true") && strstr(np(),"\"chips\":2") && strstr(np(),"\"sids\":\"D400 D420\"") && strstr(np(),"\"songs\":3"), "state for the web page");
  EXPECT(strstr(state(),"\"np\":{\"on\":true"), "/jb/state carries it too");
  FAKE_NOW += 58500000LL; run(1);
  printf("   progress after 58.5 s: %d\n", net_jb_progress());
  EXPECT(net_jb_progress() >= 495 && net_jb_progress() <= 505, "progress dot half way");

  printf("== the PC sends the clock again (every 30 s)\n");
  post("/jb/np?title=Next%20Awakening&author=Jeroen%20Tel&secs=117&el=90&song=1&songs=3&sid=D400%2CD420"); run(3);
  EXPECT(msg(m,sizeof m) == 0, "no new title on the matrix");
  printf("   /jb/np: %s\n", np()); EXPECT(strstr(np(),"\"elapsed\":90"), "clock taken over");

  printf("== stop on the PC\n");
  post("/jb/np?stop=1"); run(3);
  EXPECT(strstr(np(),"\"on\":false") && LAST_HINT == 0 && net_jb_progress() == -1, "stop: no tune, stereo measured again, no dot");

  printf("== one SID, length unknown\n");
  post("/jb/np?new=1&title=Commando&author=Rob%20Hubbard&secs=0&sid=%24D400"); run(3);
  got = msg(m,sizeof m); printf("   matrix: %s\n", got ? m : "-");
  EXPECT(got && strstr(m,"Commando") && !strstr(m,"0:00"), "title without a length");
  EXPECT(LAST_HINT == 1 && net_jb_progress() == -1, "mono for sure, no progress dot");

  printf("== the PC goes away without stop\n");
  FAKE_NOW += 76000000LL; run(2);
  EXPECT(strstr(np(),"\"on\":false") && LAST_HINT == 0, "unknown length: ends after 75 s without news");
  post("/jb/np?new=1&title=Short&author=X&secs=200&chips=1"); run(2); msg(m,sizeof m);
  FAKE_NOW += 100000000LL; run(2);
  EXPECT(strstr(np(),"\"on\":true"), "known length: still on after 100 s (the Ultimate plays on)");
  FAKE_NOW += 106000000LL; run(2);
  EXPECT(strstr(np(),"\"on\":false"), "ended 5 s after the tune's end");

  printf("== requests the Pico refuses\n");
  EXPECT(post("/jb/np?new=1&secs=10") == 400, "without title and author");
  post("/jb/np?new=1&title=A&author=B");
  EXPECT(post("/jb/np?new=1&title=C&author=D") == 409, "a second one before the first was taken: busy (the PC retries)");
  run(2); msg(m,sizeof m);
  post("/jb/np?stop=1"); run(2);

  printf("== SID addresses on the matrix (option sidshow): switched by the PC, shown with the title\n");
  { const char *CFG ="HTTP/1.1 200 OK\r\n\r\n{ \"SID Addressing\" : { \"SID Socket 1 Address\" : \"$D400\", \"SID Socket 2 Address\" : \"$D420\" } }";
    const char *CFG2="HTTP/1.1 200 OK\r\n\r\n{ \"SID Addressing\" : { \"SID Socket 1 Address\" : \"$D400\", \"SID Socket 2 Address\" : \"$D500\" } }";
    post("/jb/prefs?sidshow=1"); run(5);
    EXPECT(REST_IS("GET /v1/configs/SID%20Addressing "), "switched on: the addresses are read");
    test_rest_reply(CFG); run(3); msg(m,sizeof m);
    post("/jb/np?new=1&title=Two%20SIDs&author=Somebody&secs=100&sid=D400,D500"); run(3);
    EXPECT(REST_IS("GET /v1/configs/SID%20Addressing ") && msg(m,sizeof m) == 0, "the title waits until the sockets are read");
    test_rest_reply(CFG2); run(3);
    got = msg(m,sizeof m); printf("   matrix (%d): %s\n", got, got ? m : "-");
    EXPECT(got == 1 && strstr(m,"Two SIDs") && strstr(m,"SID 2: D500"), "one message: title with the new addresses");
    post("/jb/np?new=1&title=Two%20SIDs%20again&author=Somebody&secs=100&sid=D400,D500"); run(3);
    test_rest_reply(CFG2); run(3);
    got = msg(m,sizeof m); printf("   matrix: %s\n", got ? m : "-");
    EXPECT(got == 1 && strstr(m,"again") && !strstr(m,"SID 2"), "unchanged sockets: title only");
    post("/jb/np?stop=1"); run(3);
    FAKE_NOW += 2100000; run(2);
    EXPECT(REST_IS("GET /v1/configs/SID%20Addressing "), "after stop the sockets are read again");
    test_rest_reply(CFG); run(3);
    got = msg(m,sizeof m); printf("   matrix: %s\n", got ? m : "-");
    EXPECT(got == 1 && strstr(m,"SID 2: D420"), "the PC put them back: shown");
    post("/jb/np?new=1&title=No%20answer&author=X&secs=100"); run(3);
    EXPECT(msg(m,sizeof m) == 0, "Ultimate does not answer: title waits ...");
    FAKE_NOW += 3100000; run(2);
    EXPECT(msg(m,sizeof m) == 1, "... at most 3 s");
    test_rest_reply("HTTP/1.1 503 x\r\n\r\n"); run(2);
    post("/jb/np?stop=1"); run(3); post("/jb/prefs?sidshow=0"); run(3); }

  printf("== the PC takes over while the Pico's own jukebox runs\n");
  post("/jb/play?path=%2FUsb0%2FGibtEsNicht.sid&sub=0&shuffle=0&min=3&silence=1"); run(5);
  printf("   own jukebox: %.60s\n", state());
  EXPECT(!strstr(state(),"\"state\":\"idle\""), "own jukebox busy");
  struct tcp_pcb *before = LAST_REST;
  post("/jb/np?new=1&title=From%20PC&author=Me&secs=60&sid=D400"); run(5);
  printf("   own jukebox: %.70s\n", state());
  EXPECT(strstr(state(),"\"state\":\"idle\"") && strstr(state(),"Jukebox am PC spielt"), "own playlist ended");
  EXPECT(LAST_REST == before, "no reset sent to the Ultimate (the PC's tune plays)");
  EXPECT(strstr(np(),"\"on\":true"), "the PC's tune is shown");
  post("/jb/play?path=%2FUsb0%2FGibtEsNicht.sid&sub=0&shuffle=0&min=3&silence=1"); run(3);
  EXPECT(strstr(np(),"\"on\":false"), "the own jukebox starts again: the PC's tune is forgotten");

  printf("== a tune after a long pause (silence counter already above a minute)\n");
  run(400);                                                   /* the own jukebox's FTP attempt times out */
  void net_set_silence(uint32_t ms);
  net_set_silence(300000);
  post("/jb/np?new=1&title=After%20a%20pause&author=X&secs=300&sid=D400,D420"); run(3);
  EXPECT(strstr(np(),"\"on\":true") && LAST_HINT == 2, "not ended before the first note arrives");
  FAKE_NOW += 30000000LL; net_set_silence(330000); run(2);
  EXPECT(strstr(np(),"\"on\":true"), "30 s of silence since the start: still on");
  FAKE_NOW += 31000000LL; net_set_silence(361000); run(2);
  EXPECT(strstr(np(),"\"on\":false") && LAST_HINT == 0, "a minute of silence since the start: ended");
  net_set_silence(0);

  printf("== REST answers are not lost when another request runs in between (review finding)\n");
  { const char *body; char m2[200]; while (net_take_message(m2, sizeof m2)) ;
    uint32_t t = net_rest_get("/v1/configs/SID%20Addressing"); run(3);
    test_rest_reply("HTTP/1.1 200 OK\r\n\r\n{ \"SID Socket 1 Address\" : \"$D400\" }"); run(1);
    EXPECT(!net_rest_idle(), "GET answer not read yet: no other request may start (it would overwrite the body)");
    EXPECT(net_rest_done(t) && net_rest_result(&body) == 200 && strstr(body, "Socket 1"), "the GET answer is still there");
    EXPECT(net_rest_idle(), "after reading: free again");
    uint32_t a = net_rest_put("/v1/machine:pause"); run(3); test_rest_reply("HTTP/1.1 200 OK\r\n\r\n"); run(2);
    uint32_t b = net_rest_put("/v1/machine:resume"); run(3); test_rest_reply("HTTP/1.1 500 Error\r\n\r\n"); run(2);
    EXPECT(net_rest_done(a) && net_rest_result(NULL) == 200, "first PUT: its own status (200) even after a second request finished");
    EXPECT(net_rest_done(b) && net_rest_result(NULL) == 500, "second PUT: 500");
    EXPECT(!net_rest_done(12345), "unknown ticket: not done"); }

  printf(fails ? "FAILED: %d\n" : "all now-playing checks passed\n", fails);
  return fails ? 1 : 0; }
