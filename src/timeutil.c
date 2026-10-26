/* Calendar helpers for the clock: Unix time -> local time, EU summer time (CET/CEST). */
#include "timeutil.h"

static int64_t days_from_civil(int y, int m, int d) {          /* H. Hinnant's algorithm */
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int yoe = (int)(y - era * 400);
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
static int year_of(int64_t days) {
    int64_t z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    int doe = (int)(z - era * 146097);
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153, m = mp < 10 ? mp + 3 : mp - 9;
    return (int)(yoe + era * 400) + (m <= 2);
}
static int64_t last_sunday_utc1(int year, int month) {        /* last Sunday of the month, 01:00 UTC */
    int64_t d = days_from_civil(year, month, 31);
    int dow = (int)((d + 4) % 7);                              /* 1970-01-01 was a Thursday; 0 = Sunday */
    if (dow < 0) dow += 7;
    return (d - dow) * 86400 + 3600;
}
int tz_offset_min(int64_t unix_s, int tz_min) {
    if (tz_min != TZ_EUROPE_CODE) return tz_min;
    int y = year_of(unix_s / 86400);
    bool summer = unix_s >= last_sunday_utc1(y, 3) && unix_s < last_sunday_utc1(y, 10);
    return summer ? 120 : 60;
}
void unix_to_local(int64_t unix_s, int tz_min, int *hour, int *minute, int *wday) {
    int64_t t = unix_s + (int64_t)tz_offset_min(unix_s, tz_min) * 60;
    int64_t days = t / 86400, sec = t % 86400;
    if (sec < 0) { sec += 86400; days--; }
    *hour = (int)(sec / 3600); *minute = (int)(sec % 3600 / 60);
    if (wday) { int w = (int)((days + 4) % 7); *wday = w < 0 ? w + 7 : w; }
}
