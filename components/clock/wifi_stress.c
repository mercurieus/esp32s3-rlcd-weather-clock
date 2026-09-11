#include "wifi_stress.h"

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

void WifiStress_Run(void)
{
    const int cycles = CONFIG_CLOCK_WIFI_STRESS_CYCLES;

    ESP_LOGW(TAG, "starting - this holds boot for a while, and is only ever "
                  "enabled deliberately");

    const long net_radio = run_pass("radio", cycles, false);
    const long net_full  = run_pass("full",  cycles, true);

    ESP_LOGW(TAG, "RESULT: radio-only %+ld B, with-fetch %+ld B, "
                  "attributable to the fetch %+ld B",
             net_radio, net_full, net_full - net_radio);
    ESP_LOGW(TAG, "min-ever free internal since boot: %u B",
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
}

#else  /* !CONFIG_CLOCK_WIFI_STRESS */

void WifiStress_Run(void) {}

#endif
