#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "analysis.h"
#include "visuals.h"
int main(void){ static uint32_t fb[PANEL_W*PANEL_H];
  for(int gate=1; gate>=0; gate--){ analysis_init(); visuals_init(); visuals_set_gate(gate); srand(1);
    double sum=0; int mn=99,mx=0,frames=0;
    for(int f=0;f<600;f++){ memmove(g_samples,g_samples+HOP_SIZE,(BLOCK_SIZE-HOP_SIZE)*2);
      for(int i=0;i<HOP_SIZE;i++){ double t=(f*HOP_SIZE+i)/16000.0; /* noise: 50 Hz hum + LED-PWM ripple + random, ~40 LSB rms */
        g_samples[BLOCK_SIZE-HOP_SIZE+i]=(uint16_t)(2048+35*sin(2*M_PI*50*t)+20*sin(2*M_PI*980*t)*(0.6+0.4*sin(t*3))+((rand()%41)-20)); }
      visuals_frame(VIS_VU,fb); int c=0; for(int x=0;x<PANEL_W;x++) if(fb[4*PANEL_W+x] && fb[4*PANEL_W+x]!=COL_PEAK) c++;
      if(f>60){ sum+=c; if(c<mn)mn=c; if(c>mx)mx=c; frames++; } }
    printf("noise only, gate %s: VU bar %d..%d of 32 columns (mean %.1f)\n", gate?"OPEN  (old behaviour)":"CLOSED (new)", mn, mx, sum/frames); }
}
