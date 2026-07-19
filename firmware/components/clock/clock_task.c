#include "clock_task.h"
#include "clock_config.h"
#include "pcf85063.h"
#include "shtc3.h"
#include "battery.h"
#include "weather.h"
#include "wifi_sync.h"
#include "lvgl_bsp.h"
#include "screens.h"
#include "ui.h"
#include "user_config.h"
#include "images.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_timer.h"
#define SYNC_HOUR_1 5
#define SYNC_HOUR_2 15
#define BATTERY_WARNING_MV  3200
#define BATTERY_CRITICAL_MV 3100
static const char *TAG = "ClockTask";
static const char *WEEKDAY_NAMES[7] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };
static WeatherDay s_weather[4];
static bool s_battery_warning_active = false;

typedef enum { WICON_SUNNY, WICON_PARTLY_CLOUDY, WICON_CLOUDY, WICON_RAINY, WICON_SNOWY, WICON_STORM } WeatherIcon;

static void show_battery_warning(void)
{
    if (Lvgl_lock(-1)) {
        loadScreen(SCREEN_ID_INIT);
        lv_label_set_text(objects.info, "Battery low.\nPlease charge the device.");
        Lvgl_Refresh();
        Lvgl_unlock();
    }
}

static void enter_battery_protection_shutdown(void)
{
    ESP_LOGE(TAG, "Battery critically low (<= %d mV) - putting device into permanent sleep.", BATTERY_CRITICAL_MV);

    if (Lvgl_lock(-1)) {
        loadScreen(SCREEN_ID_INIT);
        lv_label_set_text(objects.info, "Battery critically low.\nCharge, then reset device.");
        Lvgl_Refresh();
        Lvgl_unlock();
    }

    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_deep_sleep_start();
}

static void set_status(const char *text)
{
    if (Lvgl_lock(-1)) {
        lv_label_set_text(objects.info, text);
        Lvgl_Refresh();
        Lvgl_unlock();
    }
}

static void update_sync_label(const struct tm *t)
{
    if (Lvgl_lock(-1)) {
        int64_t uptime_sec = esp_timer_get_time() / 1000000LL;
        int uptime_days = (int)(uptime_sec / 86400);
        int uptime_hours = (int)((uptime_sec % 86400) / 3600);

        lv_label_set_text_fmt(objects.sync, "Last weather sync: %02d.%02d.%04d %02d:%02d  Uptime: %dd %dh",
                               t->tm_mday, t->tm_mon + 1, t->tm_year + 1900,
                               t->tm_hour, t->tm_min,
                               uptime_days, uptime_hours);
        Lvgl_Refresh();
        Lvgl_unlock();
    }
}

static WeatherIcon classify_weathercode(int code)
{
    if (code == 95 || code == 96 || code == 99) return WICON_STORM;
    if (code == 0) return WICON_SUNNY;
    if (code == 1 || code == 2) return WICON_PARTLY_CLOUDY;
    if (code == 3 || (code >= 45 && code <= 48)) return WICON_CLOUDY;
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return WICON_RAINY;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return WICON_SNOWY;
    return WICON_CLOUDY;
}

static void set_day_icon(lv_obj_t *icon_obj, WeatherIcon icon)
{
    switch (icon) {
        case WICON_SUNNY:         lv_image_set_src(icon_obj, &img_icon_sun);            break;
        case WICON_PARTLY_CLOUDY: lv_image_set_src(icon_obj, &img_icon_partly_cloudy);   break;
        case WICON_CLOUDY:        lv_image_set_src(icon_obj, &img_icon_cloud);           break;
        case WICON_RAINY:         lv_image_set_src(icon_obj, &img_icon_rain);            break;
        case WICON_SNOWY:         lv_image_set_src(icon_obj, &img_icon_snow);            break;
        case WICON_STORM:         lv_image_set_src(icon_obj, &img_icon_storm);           break;
    }
}

static void wifi_connected_cb(void *ctx)
{
    WeatherDay *days = (WeatherDay *)ctx;
    set_status("Fetching weather forecast...");
    if (Weather_Fetch(days)) {
        set_status("Weather updated.");
    } else {
        set_status("Failed to fetch weather.");
        ESP_LOGW(TAG, "Failed to fetch weather forecast.");
    }
}

static void update_labels(const struct tm *t, float temperature, float humidity, int battery_mv)
{
    if (Lvgl_lock(-1)) {
        char c[2] = { '0', '\0' };

        c[0] = (char)('0' + (t->tm_hour / 10) % 10);
        lv_label_set_text(objects.clock_hh1, c);
        c[0] = (char)('0' + t->tm_hour % 10);
        lv_label_set_text(objects.clock_hh2, c);
        c[0] = (char)('0' + (t->tm_min / 10) % 10);
        lv_label_set_text(objects.clock_mm1, c);
        c[0] = (char)('0' + t->tm_min % 10);
        lv_label_set_text(objects.clock_mm2, c);

        lv_label_set_text_fmt(objects.hum, "%d%%", (int)(humidity + 0.5f));
        lv_label_set_text_fmt(objects.date, "%02d.%02d.%04d", t->tm_mday, t->tm_mon + 1, t->tm_year + 1900);

        int volt_whole = battery_mv / 1000;
        int volt_frac = (battery_mv % 1000) / 10;
        lv_label_set_text_fmt(objects.battery, "%d.%02d", volt_whole, volt_frac);

        bool temp_negative = temperature < 0.0f;
        float temp_abs = temp_negative ? -temperature : temperature;
        int temp_whole = (int)temp_abs;
        int temp_frac = (int)((temp_abs - temp_whole) * 10.0f + 0.5f);
        if (temp_frac >= 10) { temp_frac = 0; temp_whole += 1; }
        lv_label_set_text_fmt(objects.temp, "%s%d.%d°C", temp_negative ? "-" : "", temp_whole, temp_frac);

        Lvgl_Refresh();
        Lvgl_unlock();
    }
}

static void update_forecast_labels(const WeatherDay days[4])
{
    if (Lvgl_lock(-1)) {
        lv_obj_t *date_objs[4] = { objects.day1_date, objects.day2_date, objects.day3_date, objects.day4_date };
        lv_obj_t *icon_objs[4] = { objects.day1_icon, objects.day2_icon, objects.day3_icon, objects.day4_icon };
        lv_obj_t *temp_objs[4] = { objects.day1_temp, objects.day2_temp, objects.day3_temp, objects.day4_temp };

        for (int i = 0; i < 4; i++) {
            lv_label_set_text_fmt(date_objs[i], "%d/%d %s", days[i].day, days[i].month, WEEKDAY_NAMES[days[i].weekday]);
            set_day_icon(icon_objs[i], classify_weathercode(days[i].weathercode));
            lv_label_set_text_fmt(temp_objs[i], "%d/%d°C", days[i].temp_max, days[i].temp_min);
        }

        Lvgl_Refresh();
        Lvgl_unlock();
    }
}

static void configure_key_button_wakeup(void)
{
    gpio_config_t key_cfg = {
        .pin_bit_mask = 1ULL << KEY_BUTTON_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&key_cfg);
    gpio_wakeup_enable(KEY_BUTTON_PIN, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
}

static void clock_task(void *arg)
{
    set_status("Initializing...");

    WifiSync_Init();
    Pcf85063_Init((gpio_num_t)ESP32_I2C_SDA_PIN, (gpio_num_t)ESP32_I2C_SCL_PIN);
    Shtc3_Init(Pcf85063_GetBusHandle());
    Battery_Init();
    configure_key_button_wakeup();
    int startup_battery_mv = 0;
    Battery_ReadVoltageMv(&startup_battery_mv);
    if (startup_battery_mv > 0 && startup_battery_mv <= BATTERY_CRITICAL_MV) {
        enter_battery_protection_shutdown();
    }

    struct tm t = {0};

    set_status("Connecting to WiFi...");
    ESP_LOGI(TAG, "First time and weather sync over WiFi...");
    if (!WifiSync_SyncTimeOnce(&t, CLOCK_WIFI_SSID, CLOCK_WIFI_PASS, 15000, wifi_connected_cb, s_weather)) {
        ESP_LOGE(TAG, "No WiFi connection at startup - halting device.");
        set_status("Failed to connect to WiFi. Please reset the device.");

        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    Pcf85063_SetTime(&t);
    set_status("Time synced.");
    update_sync_label(&t);
    ESP_LOGI(TAG, "RTC set from NTP.");

    float temperature = 0.0f, humidity = 0.0f;
    int battery_mv = 0;
    Shtc3_Read(&temperature, &humidity);
    Battery_ReadVoltageMv(&battery_mv);

    update_labels(&t, temperature, humidity, battery_mv);
    update_forecast_labels(s_weather);

    set_status("Starting...");
    vTaskDelay(pdMS_TO_TICKS(800));

    if (Lvgl_lock(-1)) {
        loadScreen(SCREEN_ID_MAIN);
        Lvgl_Refresh();
        Lvgl_unlock();
    }

    int last_sync_hour = -1;
    for (;;) {
        struct tm pre_sleep;
        if (Pcf85063_GetTime(&pre_sleep) != ESP_OK) {
            pre_sleep.tm_sec = 0;
        }
        int seconds_to_next_minute = 60 - pre_sleep.tm_sec;
        if (seconds_to_next_minute <= 0 || seconds_to_next_minute > 60) {
            seconds_to_next_minute = 60;
        }

        esp_sleep_enable_timer_wakeup((uint64_t)seconds_to_next_minute * 1000000ULL);
        esp_light_sleep_start();

        bool button_pressed = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO);

        if (button_pressed) {
            ESP_LOGI(TAG, "KEY button pressed - forcing sync...");

            while (gpio_get_level(KEY_BUTTON_PIN) == 0) {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            vTaskDelay(pdMS_TO_TICKS(50));

            struct tm ntp_t;
            if (WifiSync_SyncTimeOnce(&ntp_t, CLOCK_WIFI_SSID, CLOCK_WIFI_PASS, 15000, wifi_connected_cb, s_weather)) {
                Pcf85063_SetTime(&ntp_t);
                update_forecast_labels(s_weather);
                update_sync_label(&ntp_t);
                last_sync_hour = ntp_t.tm_hour;
            }
        }

        struct tm now;
        if (Pcf85063_GetTime(&now) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read RTC, skipping this cycle.");
            continue;
        }

        bool is_sync_hour = (now.tm_hour == SYNC_HOUR_1 || now.tm_hour == SYNC_HOUR_2);

        if (is_sync_hour && now.tm_min == 0 && last_sync_hour != now.tm_hour) {
            ESP_LOGI(TAG, "Scheduled sync at %02d:00 (time + weather)...", now.tm_hour);
            struct tm ntp_t;
            if (WifiSync_SyncTimeOnce(&ntp_t, CLOCK_WIFI_SSID, CLOCK_WIFI_PASS, 15000, wifi_connected_cb, s_weather)) {
                Pcf85063_SetTime(&ntp_t);
                now = ntp_t;
                update_sync_label(&now);
            }
            update_forecast_labels(s_weather);
            last_sync_hour = now.tm_hour;
        }

        if (Shtc3_Read(&temperature, &humidity) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read SHTC3, keeping previous values.");
        }
        Battery_ReadVoltageMv(&battery_mv);

        if (battery_mv > 0 && battery_mv <= BATTERY_CRITICAL_MV) {
            enter_battery_protection_shutdown();
        }

        if (battery_mv > 0 && battery_mv <= BATTERY_WARNING_MV) {
            show_battery_warning();
            s_battery_warning_active = true;
        } else {
            if (s_battery_warning_active) {
                if (Lvgl_lock(-1)) {
                    loadScreen(SCREEN_ID_MAIN);
                    Lvgl_unlock();
                }
                s_battery_warning_active = false;
            }
            update_labels(&now, temperature, humidity, battery_mv);
        }
    }
}

void ClockTask_Start(void)
{
    xTaskCreatePinnedToCore(clock_task, "ClockTask", 12 * 1024, NULL, 3, NULL, 1);
}