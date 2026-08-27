#include "selftest.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include <string.h>

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

static void test_harness_runs(void)
{
    SELFTEST_CHECK_INT(1 + 1, 2);
}

void Selftest_Run(void)
{
    s_checks = 0;
    s_failures = 0;

    ESP_LOGI(TAG, "--- self-test start ---");
    test_harness_runs();
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
