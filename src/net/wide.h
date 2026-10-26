/*
 * Two matrices as one picture (option, Pico 2 W): one Pico ("lead") draws a 64 x 8 picture, shows its half and sends
 * the other half every frame to the partner ("follow"), which shows it with the lead's brightness. The halves can never
 * drift apart (random effects included), the partner needs no audio of its own. While the partner does not answer,
 * the lead draws for its own matrix only; while no frames come, the partner shows its own picture.
 * Protocol (UDP port WIDE_PORT): docs/HANDBUCH.md, Anhang "Netzwerk-Protokolle".
 */
#pragma once
#include <stdbool.h>

#define WIDE_PORT 11065
enum { WIDE_OFF = 0, WIDE_LEAD, WIDE_FOLLOW };

void wide_config(int role, bool right, const char *partner, int partner_len);   /* after texts.txt was read */
void wide_start(void);                  /* in the home network (net.c) */
void wide_task(void);                   /* main loop (net.c): find the partner, ask for it while it does not answer */
int  wide_json(char *out, int n);       /* "wide":"lead","wpart":"..","wside":"right","wok":true  (no braces) */
bool wide_following(void);              /* frames of a lead are being shown right now */
