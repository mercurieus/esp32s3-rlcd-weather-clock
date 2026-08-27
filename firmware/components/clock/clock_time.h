#pragma once

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Applies a POSIX TZ string to every later local-time conversion. */
void ClockTime_SetTimezone(const char *tz);

/* Converts UTC calendar time to a Unix epoch. Leaves *utc untouched. */
time_t ClockTime_UtcToEpoch(const struct tm *utc);

/* Converts UTC calendar time to local time in the active timezone. */
void ClockTime_UtcToLocal(const struct tm *utc, struct tm *local_out);

#ifdef __cplusplus
}
#endif
