#include "wifi_stress.h"

/* sdkconfig.h explicitly, and before the #if: without it CONFIG_CLOCK_WIFI_STRESS
   is simply undefined here, the preprocessor reads that as 0, and the whole file
   compiles to the empty stub with no warning. The build succeeds, the flash
   succeeds, and the test silently does not exist - which is exactly what
   happened the first time this ran. */
#include "sdkconfig.h"

#if CONFIG_CLOCK_WIFI_STRESS

#include "wifi_sync.h"
#include "weather.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <time.h>

static const char *TAG = "WifiStress";

static WeatherDay s_days[4];
static WeatherNow s_now;

static void fetch_weather_cb(void *ctx)
{
    (void)ctx;
    Weather_Fetch(s_days, &s_now);
}

/* One pass of `cycles` bring-ups. with_fetch selects whether the weather
   request runs inside each cycle, which is what separates a leak in the radio
   path from one in HTTP/TLS. Returns the net change in free internal DRAM
   across the whole pass. */
static long run_pass(const char *label, int cycles, bool with_fetch)
{
    /* Settle first: the previous pass may have left the radio tearing down,
       and a figure taken mid-teardown reads as a leak that is not there. */
    vTaskDelay(pdMS_TO_TICKS(1000));

    const size_t start = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGW(TAG, "== pass '%s': %d cycles, weather fetch %s, start %u B free ==",
             label, cycles, with_fetch ? "ON" : "OFF", (unsigned)start);

    for (int i = 1; i <= cycles; i++) {
        const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

        struct tm utc;
        const bool ok = WifiSync_SyncTimeOnce(&utc, CONFIG_CLOCK_WIFI_SSID,
                                              CONFIG_CLOCK_WIFI_PASS, 15000,
                                              with_fetch ? fetch_weather_cb : NULL,
                                              NULL);

        vTaskDelay(pdMS_TO_TICKS(500));   /* let teardown finish before measuring */

        const size_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        ESP_LOGW(TAG,
                 "%s %2d/%d ok=%d  free %u  cycle %+ld  cumulative %+ld  largest %u",
                 label, i, cycles, (int)ok, (unsigned)after,
                 (long)after - (long)before,
                 (long)after - (long)start,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    }

    const size_t end = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const long net = (long)end - (long)start;
    ESP_LOGW(TAG, "== pass '%s' done: net %+ld B over %d cycles (%+ld B/cycle) ==",
             label, net, cycles, cycles ? net / cycles : 0);
    return net;
}

/* Repeated fetches inside ONE Wi-Fi session. The per-cycle variant cannot
   isolate the HTTP/TLS path: it pays the PHY bring-up each time, and the USB
   link has been observed dropping during a TLS handshake, which ends the run
   before enough cycles accumulate to see a trend. Holding the radio up
   removes both. */
static int s_fetch_count;

static void fetch_many_cb(void *ctx)
{
    (void)ctx;
    const size_t start = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGW(TAG, "== pass 'fetch': %d fetches in one session, start %u B free ==",
             s_fetch_count, (unsigned)start);

    for (int i = 1; i <= s_fetch_count; i++) {
        const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        const bool ok = Weather_Fetch(s_days, &s_now);
        const size_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

        ESP_LOGW(TAG,
                 "fetch %2d/%d ok=%d  free %u  cycle %+ld  cumulative %+ld  largest %u",
                 i, s_fetch_count, (int)ok, (unsigned)after,
                 (long)after - (long)before,
                 (long)after - (long)start,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

        vTaskDelay(pdMS_TO_TICKS(300));
    }

    const size_t end = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGW(TAG, "== pass 'fetch' done: net %+ld B over %d fetches (%+ld B/fetch) ==",
             (long)end - (long)start, s_fetch_count,
             s_fetch_count ? ((long)end - (long)start) / s_fetch_count : 0);
}

void WifiStress_Run(void)
{
    const int cycles = CONFIG_CLOCK_WIFI_STRESS_CYCLES;

    ESP_LOGW(TAG, "starting - this holds boot for a while, and is only ever "
                  "enabled deliberately");

    /* Fetch pass first. It is the one under suspicion, and running it while
       the link is still healthy means its numbers survive even if a later
       pass loses USB. */
    s_fetch_count = cycles;
    struct tm utc;
    const size_t before_fetch = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    WifiSync_SyncTimeOnce(&utc, CONFIG_CLOCK_WIFI_SSID, CONFIG_CLOCK_WIFI_PASS,
                          15000, fetch_many_cb, NULL);
    vTaskDelay(pdMS_TO_TICKS(1000));
    const long net_fetch = (long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) -
                           (long)before_fetch;

    const long net_radio = run_pass("radio", cycles, false);

    ESP_LOGW(TAG, "RESULT: %d fetches in one session %+ld B (incl. one Wi-Fi "
                  "session), %d radio-only cycles %+ld B",
             cycles, net_fetch, cycles, net_radio);
    ESP_LOGW(TAG, "min-ever free internal since boot: %u B",
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
}

#else  /* !CONFIG_CLOCK_WIFI_STRESS */

void WifiStress_Run(void) {}

#endif
