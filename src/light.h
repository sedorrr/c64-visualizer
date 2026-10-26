/* Optional VEML7700 light sensor on I2C0 (SDA GP4, SCL GP5, address 0x10): automatic matrix brightness.
   Without a sensor nothing changes; it may also be plugged in later (checked every 5 s). */
#pragma once
#include <stdbool.h>

void  light_init(void);
/* once per frame: reads the sensor every 250 ms (non-blocking apart from ~1 ms of I2C) and returns the
   smoothed brightness factor 0..1 for the matrix (1.0 without a sensor or with @light off) */
float light_tick(bool enabled, int min_pct, int strength, float frame_s);
bool  light_present(void);
float light_lux_now(void);                 /* last measurement, -1 = no sensor */
