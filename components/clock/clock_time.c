#include "clock_time.h"

#include <stdint.h>
#include <stdlib.h>

void ClockTime_SetTimezone(const char *tz)
{
    setenv("TZ", tz, 1);
    tzset();
}

/* Days since 1970-01-01 for a proleptic-Gregorian date, by Howard Hinnant's
   days_from_civil. Pure arithmetic, no libc state and no allocation.

   This replaced a setenv("TZ", "UTC0") / mktime / setenv-back dance, which is
   the obvious way to get a UTC epoch out of newlib and which leaked 32 bytes
   every time it ran. newlib reallocates the environment entry when the new
   value is longer than the one it replaces, and alternating "UTC0" with a real
   zone string does that on every other call. The main loop converts once a
   second, so it cost about 2160 bytes a minute - roughly 130 KB an hour, which
   emptied the heap in about an hour and then failed a 60 byte allocation
   inside phy_track_pll_init on the next Wi-Fi bring-up, where IDF's own
   ESP_ERROR_CHECK aborts and reboots the board. */
static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - (int)(era * 400));               /* [0, 399] */
    const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;       /* [0, 146096] */
    return era * 146097 + (int64_t)doe - 719468;
}

time_t ClockTime_UtcToEpoch(const struct tm *utc)
{
    const int64_t days = days_from_civil(utc->tm_year + 1900,
                                         (unsigned)(utc->tm_mon + 1),
                                         (unsigned)utc->tm_mday);
    return (time_t)(days * 86400
                    + (int64_t)utc->tm_hour * 3600
                    + (int64_t)utc->tm_min * 60
                    + utc->tm_sec);
}

void ClockTime_UtcToLocal(const struct tm *utc, struct tm *local_out)
{
    time_t epoch = ClockTime_UtcToEpoch(utc);
    localtime_r(&epoch, local_out);
}
