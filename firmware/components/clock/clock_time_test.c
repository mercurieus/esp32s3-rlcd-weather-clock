#include "clock_time.h"
#include "selftest.h"

#include "sdkconfig.h"
#include <string.h>

#if CONFIG_CLOCK_SELFTEST

#define TZ_KYIV "EET-2EEST,M3.5.0/3,M10.5.0/4"

static struct tm make_utc(int year, int mon, int mday, int hour, int min)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = year - 1900;
    t.tm_mon  = mon - 1;
    t.tm_mday = mday;
    t.tm_hour = hour;
    t.tm_min  = min;
    return t;
}

static void check_local(const char *tz, struct tm utc,
                        int want_hour, int want_min, int want_mday)
{
    struct tm local;
    ClockTime_SetTimezone(tz);
    ClockTime_UtcToLocal(&utc, &local);
    SELFTEST_CHECK_INT(local.tm_hour, want_hour);
    SELFTEST_CHECK_INT(local.tm_min,  want_min);
    SELFTEST_CHECK_INT(local.tm_mday, want_mday);
}

void ClockTime_RunTests(void)
{
    /* Winter: EET, UTC+2 */
    check_local(TZ_KYIV, make_utc(2026, 1, 15, 12, 0), 14, 0, 15);

    /* Summer: EEST, UTC+3 */
    check_local(TZ_KYIV, make_utc(2026, 7, 15, 12, 0), 15, 0, 15);

    /* One minute before the spring transition: still EET */
    check_local(TZ_KYIV, make_utc(2026, 3, 29, 0, 59), 2, 59, 29);

    /* At the spring transition: local jumps 03:00 -> 04:00 */
    check_local(TZ_KYIV, make_utc(2026, 3, 29, 1, 0), 4, 0, 29);

    /* One minute before the autumn transition: still EEST */
    check_local(TZ_KYIV, make_utc(2026, 10, 25, 0, 59), 3, 59, 25);

    /* At the autumn transition: local falls back 04:00 -> 03:00 */
    check_local(TZ_KYIV, make_utc(2026, 10, 25, 1, 0), 3, 0, 25);

    /* UTC passes through unchanged */
    check_local("UTC0", make_utc(2026, 8, 26, 19, 3), 19, 3, 26);

    /* Conversion must not modify its input */
    struct tm utc = make_utc(2026, 8, 26, 19, 3);
    struct tm before = utc;
    (void)ClockTime_UtcToEpoch(&utc);
    SELFTEST_CHECK_INT(memcmp(&utc, &before, sizeof(utc)), 0);

    /* Epoch of a known instant: 2026-08-26 19:03:00 UTC */
    SELFTEST_CHECK_INT(ClockTime_UtcToEpoch(&before), 1787770980L);
}

#else
void ClockTime_RunTests(void) {}
#endif
