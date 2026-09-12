#include "wifi_sync.h"

/* Explicitly, and before anything that reads a CONFIG_ macro. An undefined
   CONFIG_ symbol is silently 0 to the preprocessor, which once compiled a
   whole diagnostic out of this project without a single warning. */
#include "sdkconfig.h"

#include "alloc_watch.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "WifiSync";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_event_group = NULL;
static bool s_initialized = false;

static bool    s_wifi_hint_valid = false;
static uint8_t s_wifi_channel = 0;
static uint8_t s_wifi_bssid[6];

/* STA_START owns the first connect attempt of every bring-up. esp_wifi_start()
   posts it asynchronously, so the caller cannot reliably connect itself before
   the driver is ready - which is why this handler exists and why the attempt
   loop below must not also connect on its first pass. */
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_err_t cerr = esp_wifi_connect();
        if (cerr != ESP_OK) {
            ESP_LOGW(TAG, "esp_wifi_connect on STA_START: %s", esp_err_to_name(cerr));
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(s_event_group, WIFI_FAIL_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_event_group, WIFI_CONNECTED_BIT);
    }
}

void WifiSync_Init(void)
{
    if (s_initialized) return;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    s_event_group = xEventGroupCreate();

    s_initialized = true;
}

bool WifiSync_SyncTimeOnce(struct tm *out_time, const char *ssid, const char *password, uint32_t timeout_ms,
                            WifiSync_ConnectedCallback on_connected, void *ctx)
{
    if (!s_initialized) {
        WifiSync_Init();
    }

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    if (s_wifi_hint_valid) {
        wifi_config.sta.channel = s_wifi_channel;
        memcpy(wifi_config.sta.bssid, s_wifi_bssid, 6);
        wifi_config.sta.bssid_set = true;
    }

    /* Deliberately NOT ESP_ERROR_CHECK. These three used to abort - which
       reboots the device - on any non-OK return. That is defensible for the
       first call at boot, where a failure means the device is unusable
       anyway, but this function runs again on every refresh, and by then a
       transient error is something to report and retry, not to die on. A
       failed refresh should cost a stale reading, not an uptime. */
    /* The last look at the heap before the radio comes up. If the restart
       happens inside esp_wifi_start(), this is what the next boot reports -
       the core dump cannot supply it, because it stores task stacks and no
       heap at all. */
    AllocWatch_Snapshot(ALLOC_WATCH_TAG_WIFI_START);

    /* A plain if, not #if: an undefined CONFIG_ symbol would make the
       preprocessor form vanish silently, while this one fails to compile. The
       comparison folds away when the floor is 0. */
    const size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (CONFIG_CLOCK_MIN_HEAP_FOR_WIFI > 0 &&
        free_internal < (size_t)CONFIG_CLOCK_MIN_HEAP_FOR_WIFI) {
        /* Refusing here is the whole point. Bring-up costs ~49 KB of internal
           DRAM, and running out part-way through lands in phy_track_pll_init(),
           where IDF's own ESP_ERROR_CHECK aborts and reboots the board over a
           ~60 byte allocation. Declining the refresh costs a stale reading. */
        ESP_LOGE(TAG, "skipping bring-up: %u B internal free, under the %d B floor "
                      "(largest block %u)",
                 (unsigned)free_internal, CONFIG_CLOCK_MIN_HEAP_FOR_WIFI,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        return false;
    }

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
        /* Already started means a previous run tore down incompletely; the
           radio is usable as-is, so carry on rather than treating it as a
           failure. */
        if (err == ESP_ERR_WIFI_CONN || err == ESP_ERR_WIFI_NOT_STOPPED) {
            ESP_LOGW(TAG, "esp_wifi_start: %s - continuing", esp_err_to_name(err));
            err = ESP_OK;
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi bring-up failed: %s", esp_err_to_name(err));
        esp_wifi_stop();
        return false;
    }

    const int max_attempts = 3;
    bool connected = false;
    EventBits_t bits = 0;

    for (int attempt = 1; attempt <= max_attempts && !connected; attempt++) {
        xEventGroupClearBits(s_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

        bool use_hint = (attempt == 1) && s_wifi_hint_valid;
        uint32_t attempt_timeout = use_hint ? 5000 : timeout_ms;

        if (attempt > 1) {
            wifi_config_t retry_config = {0};
            strncpy((char *)retry_config.sta.ssid, ssid, sizeof(retry_config.sta.ssid) - 1);
            strncpy((char *)retry_config.sta.password, password, sizeof(retry_config.sta.password) - 1);
            retry_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
            esp_wifi_set_config(WIFI_IF_STA, &retry_config);
            ESP_LOGW(TAG, "WiFi connection attempt %d/%d (full scan)...", attempt, max_attempts);
            esp_wifi_connect();
        } else if (use_hint) {
            /* No esp_wifi_connect() here: STA_START has already issued it with
               this same config. Calling it again raced the driver and logged
               "sta is connecting, return error" on every hinted bring-up. */
            ESP_LOGI(TAG, "Connecting to WiFi using known channel/BSSID...");
        }

        bits = xEventGroupWaitBits(s_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                    pdFALSE, pdFALSE, pdMS_TO_TICKS(attempt_timeout));

        if (bits & WIFI_CONNECTED_BIT) {
            connected = true;
        } else if (use_hint) {
            ESP_LOGW(TAG, "Known channel/BSSID failed, falling back to full scan.");
            s_wifi_hint_valid = false;
        } else if (attempt < max_attempts) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    if (connected) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            s_wifi_channel = ap_info.primary;
            memcpy(s_wifi_bssid, ap_info.bssid, 6);
            s_wifi_hint_valid = true;
        }
    }

    bool got_time = false;

    if (connected) {
        ESP_LOGI(TAG, "Connected to WiFi, fetching NTP time...");
        esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_CLOCK_NTP_SERVER);
        esp_netif_sntp_init(&sntp_config);

        if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms)) == ESP_OK) {
            time_t now;
            time(&now);
            gmtime_r(&now, out_time);
            got_time = true;
            ESP_LOGI(TAG, "Time fetched (UTC): %02d:%02d:%02d %02d-%02d-%04d",
                     out_time->tm_hour, out_time->tm_min, out_time->tm_sec,
                     out_time->tm_mday, out_time->tm_mon + 1, out_time->tm_year + 1900);
        } else {
            ESP_LOGW(TAG, "NTP sync timeout");
        }
        esp_netif_sntp_deinit();

        if (on_connected) {
            ESP_LOGI(TAG, "WiFi still connected - invoking additional callback (e.g. weather)...");
            on_connected(ctx);
        }
    } else {
        ESP_LOGW(TAG, "Failed to connect to WiFi after %d attempts.", max_attempts);
    }

    esp_wifi_disconnect();
    esp_wifi_stop();

    return got_time;
}