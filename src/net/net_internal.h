/* Functions of net.c used by the jukebox module. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
const char *net_u64_ip(void);
const char *net_u64_pw(void);
uint32_t    net_silent_ms(void);
void        net_post_message(const char *m);      /* scroll once on the matrix */
bool        net_rest_idle(void);
uint32_t    net_rest_put(const char *path_query);  /* PUT request to the Ultimate's REST API; returns a ticket */
uint32_t    net_rest_get(const char *path_query);  /* GET request; returns a ticket */
bool        net_rest_done(uint32_t ticket);        /* the request with this ticket has finished */
int         net_rest_result(const char **body);    /* of the last finished request: HTTP status (0 = none), body */
char       *net_body_buffer(void);              /* the shared request body buffer (64 KB) */
void        net_relay_np(const char *query);    /* a title (np query) on to the next visualizer of the stream chain */
uint32_t    net_resolve_wide(const char *host);  /* address of the two-matrix partner (0 = not known yet); main loop */
