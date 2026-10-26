/* the settings layout v5 as stored in flash (src/net/net.c netcfg_t) - for tests that read or write it directly */
#pragma once
#include <stddef.h>
typedef struct { char label[17], addr[64], pw[33]; } tu64_t;
typedef struct { unsigned magic; char ssid[33], pass[65], webpw[33], name[17]; tu64_t u64[4]; unsigned char stream, smode, src, pad; char fwd[64]; unsigned check; } v5_t;
#define V5_MAGIC 0xC64E7C05u
