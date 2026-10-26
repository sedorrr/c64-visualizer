#pragma once
#define SNTP_OPMODE_POLL 0
static inline void sntp_setoperatingmode(int m){(void)m;} static inline void sntp_setservername(int i,const char*s){(void)i;(void)s;} static inline void sntp_init(void){}
