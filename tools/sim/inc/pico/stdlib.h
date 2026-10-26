#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef uint64_t absolute_time_t;
static inline absolute_time_t get_absolute_time(void) { return 0; }
static inline absolute_time_t delayed_by_us(absolute_time_t t, uint64_t us) { return t + us; }
static inline void sleep_until(absolute_time_t t) { (void)t; }
