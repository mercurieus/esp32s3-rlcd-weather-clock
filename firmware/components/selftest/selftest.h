#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void Selftest_Run(void);

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
