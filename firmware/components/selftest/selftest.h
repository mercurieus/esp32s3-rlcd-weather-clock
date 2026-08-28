#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Resets the pass/fail counters and logs the start marker. Call this once
   at boot, before any *_RunTests() suite runs. */
void Selftest_Begin(void);

/* Logs the accumulated pass/fail summary. Call once, after every suite for
   this boot has run.

   Suites are not called from here or from anywhere in this component:
   selftest is a leaf (only requires "log"), and components under test add
   "selftest" to their own PRIV_REQUIRES to reach the CHECK macros - never
   the other way around, which would risk a build cycle wherever a component
   under test is itself required by something selftest would need (this bit
   clock, which requires main for user_config.h, while main requires
   selftest). main.cpp is already required by everything, so it is the one
   place that calls Selftest_Begin(), each component's own *_RunTests(), and
   Selftest_End(), in that order. */
void Selftest_End(void);

/* Read the accumulated counts at any point - after Selftest_End() for the
   final tally, or mid-run for a partial one. Useful wherever the result
   needs to be shown or logged again later (a Settings/Info page, for
   instance), not just once at boot. */
void Selftest_GetCounts(int *checks, int *failures);

void selftest_check(bool ok, const char *expr, const char *file, int line);
void selftest_check_int(long actual, long expected, const char *expr,
                        const char *file, int line);
void selftest_check_str(const char *actual, const char *expected,
                        const char *expr, const char *file, int line);

#define SELFTEST_CHECK(cond) \
    selftest_check((cond), #cond, __FILE__, __LINE__)
#define SELFTEST_CHECK_INT(actual, expected) \
    selftest_check_int((long)(actual), (long)(expected), #actual, __FILE__, __LINE__)
#define SELFTEST_CHECK_STR(actual, expected) \
    selftest_check_str((actual), (expected), #actual, __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif
