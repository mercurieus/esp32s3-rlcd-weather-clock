#include "clock_time.h"

#include <stdlib.h>
#include <string.h>

void ClockTime_SetTimezone(const char *tz)
{
    setenv("TZ", tz, 1);
    tzset();
}

time_t ClockTime_UtcToEpoch(const struct tm *utc)
{
    char old_tz[64];
    const char *cur = getenv("TZ");
    if (cur) {
        strncpy(old_tz, cur, sizeof(old_tz) - 1);
        old_tz[sizeof(old_tz) - 1] = '\0';
    } else {
        old_tz[0] = '\0';
    }

    setenv("TZ", "UTC0", 1);
    tzset();

    struct tm copy = *utc;
    copy.tm_isdst = 0;
    time_t epoch = mktime(&copy);

    if (old_tz[0]) {
        setenv("TZ", old_tz, 1);
    } else {
        unsetenv("TZ");
    }
    tzset();

    return epoch;
}

void ClockTime_UtcToLocal(const struct tm *utc, struct tm *local_out)
{
    time_t epoch = ClockTime_UtcToEpoch(utc);
    localtime_r(&epoch, local_out);
}
