#include <stdio.h>
#include <string.h>
#include "jukebox.h"
extern unsigned char FAKE_FLASH[];
void net_task(void);
int main(void){ const char *o,*t; int n;
  memset(FAKE_FLASH+0x100000-4096,0xFF,4096); jb_init();
  jb_route(true,"/jb/prefs?src=0&hvsc=%2FUsb0%2FC64Music%2FDOCUMENTS%2FSonglengths.md5&user=%2FUsb0%2Fuser.txt",NULL,0,&o,&n,&t);
  extern void jb_task(void); jb_task();                       /* saves to flash */
  jb_init();                                                  /* "restart" */
  jb_route(false,"/jb/state",NULL,0,&o,&n,&t); printf("after restart: %.*s\n", (int)(strstr(o,"}")-strstr(o,"\"prefs\"")+1), strstr(o,"\"prefs\""));
  jb_route(true,"/jb/prefs?src=1",NULL,0,&o,&n,&t); jb_task(); jb_init();
  jb_route(false,"/jb/state",NULL,0,&o,&n,&t); printf("source PC kept paths: %.*s\n", (int)(strstr(o,"}")-strstr(o,"\"prefs\"")+1), strstr(o,"\"prefs\""));
  /* settings saved by an older firmware (layout v1, magic JBP1): taken over, then stored in the new layout */
  { struct { unsigned magic; unsigned char src, jbsrc, pad[2]; char hvsc[100], user[100], dir[160]; unsigned check; } v1;
    memset(&v1,0,sizeof v1); v1.magic=0x4A425031u; v1.src=1; v1.jbsrc=0; strcpy(v1.hvsc,"/USB2/HVSC/Songlengths.md5"); strcpy(v1.user,"/USB2/own.txt"); strcpy(v1.dir,"/USB2/SID-Tunes/2026");
    unsigned h=2166136261u; const unsigned char *b=(const unsigned char*)&v1; for(size_t i=0;i<__builtin_offsetof(__typeof__(v1),check);i++){h^=b[i];h*=16777619u;} v1.check=h;
    memset(FAKE_FLASH+0x100000-4096,0xFF,4096); memcpy(FAKE_FLASH+0x100000-4096,&v1,sizeof v1);
    jb_init(); jb_task(); jb_init();                            /* migrated, saved, restarted */
    jb_route(false,"/jb/state",NULL,0,&o,&n,&t); const char *pp=strstr(o,"\"prefs\"");
    printf("old settings taken over: %.*s\n", (int)(strstr(pp,"}")-pp+1), pp);
    if (!strstr(pp,"\"dir\":\"/USB2/SID-Tunes/2026\"") || !strstr(pp,"\"hvsc\":\"/USB2/HVSC/Songlengths.md5\"") || !strstr(pp,"\"src\":1") || !strstr(pp,"\"sidauto\":0")
        || *(unsigned*)(FAKE_FLASH+0x100000-4096) != 0x4A425032u) { printf("!!! old jukebox settings lost\n"); return 1; } }
  return 0; }
