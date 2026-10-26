#pragma once
#include "lwip/ip4_addr.h"
#include "lwip/tcp.h"
#define ERR_INPROGRESS (-5)
typedef void (*dns_found_callback)(const char *name, const ip_addr_t *ipaddr, void *callback_arg);
/* fake: names are answered later by test_dns_answer() (like a real mDNS lookup) */
err_t dns_gethostbyname(const char *name, ip_addr_t *addr, dns_found_callback cb, void *arg);
