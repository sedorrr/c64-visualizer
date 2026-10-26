/* Calendar helpers for the clock (Pico 2 W): Unix time -> local time with EU summer time. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#define TZ_EUROPE_CODE (-32768)              /* same value as TZ_EUROPE in textfx.h */
int  tz_offset_min(int64_t unix_s, int tz_min);                     /* offset to UTC in minutes */
void unix_to_local(int64_t unix_s, int tz_min, int *hour, int *minute, int *wday);
