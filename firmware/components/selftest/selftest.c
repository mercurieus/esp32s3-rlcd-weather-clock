#include "selftest.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#if CONFIG_CLOCK_SELFTEST

static const char *TAG = "Selftest";

static int s_checks;
static int s_failures;

void selftest_check(bool ok, const char *expr, const char *file, int line)
{
    s_checks++;
    if (!ok) {
        s_failures++;
        ESP_LOGE(TAG, "FAIL %s:%d  %s", file, line, expr);
    }
}

void selftest_check_int(long actual, long expected, const char *expr,
                        const char *file, int line)
{
    s_checks++;
    if (actual != expected) {
        s_failures++;
        ESP_LOGE(TAG, "FAIL %s:%d  %s was %ld, expected %ld",
                 file, line, expr, actual, expected);
    }
}

void selftest_check_str(const char *actual, const char *expected,
                        const char *expr, const char *file, int line)
{
    s_checks++;
    if (actual == NULL || expected == NULL || strcmp(actual, expected) != 0) {
        s_failures++;
        ESP_LOGE(TAG, "FAIL %s:%d  %s was \"%s\", expected \"%s\"",
                 file, line, expr,
                 actual ? actual : "(null)", expected ? expected : "(null)");
    }
}

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

static void set_tz(const char *tz) { setenv("TZ", tz, 1); tzset(); }

static time_t utc_to_epoch(const struct tm *utc)
{
    char old_tz[64];
    const char *cur = getenv("TZ");
    if (cur) {
        strncpy(old_tz, cur, sizeof(old_tz) - 1);
        old_tz[sizeof(old_tz) - 1] = '\0';
    } else {
        old_tz[0] = '\0';
    }
    set_tz("UTC0");
    struct tm copy = *utc;
    copy.tm_isdst = 0;
    time_t epoch = mktime(&copy);
    if (old_tz[0]) { setenv("TZ", old_tz, 1); } else { unsetenv("TZ"); }
    tzset();
    return epoch;
}

static void check_local(const char *tz, struct tm utc,
                        int want_hour, int want_min, int want_mday)
{
    struct tm local;
    set_tz(tz);
    time_t epoch = utc_to_epoch(&utc);
    localtime_r(&epoch, &local);
    SELFTEST_CHECK_INT(local.tm_hour, want_hour);
    SELFTEST_CHECK_INT(local.tm_min,  want_min);
    SELFTEST_CHECK_INT(local.tm_mday, want_mday);
}

static void test_harness_runs(void)
{
    SELFTEST_CHECK_INT(1 + 1, 2);
}

static void test_clock_time(void)
{
#define TZ_KYIV "EET-2EEST,M3.5.0/3,M10.5.0/4"

    /* Winter: EET, UTC+2 */
    check_local(TZ_KYIV, make_utc(2026, 1, 15, 12, 0), 14, 0, 15);

    /* Summer: EEST, UTC+3 */
    check_local(TZ_KYIV, make_utc(2026, 7, 15, 12, 0), 15, 0, 15);

    /* Spring transition: local jumps 03:00 -> 04:00 */
    check_local(TZ_KYIV, make_utc(2026, 3, 29, 1, 0), 4, 0, 29);

    /* Autumn transition: local falls back 04:00 -> 03:00 */
    check_local(TZ_KYIV, make_utc(2026, 10, 25, 1, 0), 3, 0, 25);

    /* UTC passes through unchanged */
    check_local("UTC0", make_utc(2026, 8, 26, 19, 3), 19, 3, 26);

    /* Epoch of a known instant: 2026-08-26 19:03:00 UTC */
    struct tm known = make_utc(2026, 8, 26, 19, 3);
    SELFTEST_CHECK_INT(utc_to_epoch(&known), 1787770980L);

    /* Conversion must not modify its input */
    struct tm utc = make_utc(2026, 8, 26, 19, 3);
    struct tm before = utc;
    (void)utc_to_epoch(&utc);
    SELFTEST_CHECK_INT(memcmp(&utc, &before, sizeof(utc)), 0);

#undef TZ_KYIV
}

void Selftest_Run(void)
{
    s_checks = 0;
    s_failures = 0;

    ESP_LOGI(TAG, "--- self-test start ---");
    test_harness_runs();
    test_clock_time();
    ESP_LOGI(TAG, "SELFTEST COMPLETE: %d check(s), %d failure(s)",
             s_checks, s_failures);
}

#else  /* !CONFIG_CLOCK_SELFTEST */

void Selftest_Run(void) {}
void selftest_check(bool ok, const char *e, const char *f, int l)
{ (void)ok; (void)e; (void)f; (void)l; }
void selftest_check_int(long a, long b, const char *e, const char *f, int l)
{ (void)a; (void)b; (void)e; (void)f; (void)l; }
void selftest_check_str(const char *a, const char *b, const char *e,
                        const char *f, int l)
{ (void)a; (void)b; (void)e; (void)f; (void)l; }

#endif
