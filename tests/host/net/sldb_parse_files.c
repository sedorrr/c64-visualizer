#include "jukebox.c"
#include <stdio.h>
int main(int argc,char**argv){ FILE*o=fopen("../../web/fixtures/c_records.bin","wb");
  for(int a=1;a<argc;a++){ FILE*f=fopen(argv[a],"r"); char line[700];
    while(fgets(line,sizeof line,f)){ uint8_t m[8]; uint16_t s[64]; int n=parse_line(line,m,s); if(!n) continue;
      uint8_t r[140]; memcpy(r,m,8); r[8]=n; for(int i=0;i<n;i++){r[9+2*i]=s[i]&255;r[10+2*i]=s[i]>>8;} fwrite(r,1,9+2*n,o);} fclose(f);} fclose(o); }
