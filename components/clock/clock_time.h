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

/* Self-test suite, defined in clock_time_test.c. Called from main.cpp
   between Selftest_Begin()/Selftest_End() - see the comment on
   Selftest_End() for why it isn't called from within selftest itself.
   Empty unless CONFIG_CLOCK_SELFTEST. */
void ClockTime_RunTests(void);

#ifdef __cplusplus
}
#endif
