#include "clock_task.h"
#include "clock_config.h"
#include "clock_time.h"
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
#include <stdio.h>
#define SYNC_HOUR_1 5
#define SYNC_HOUR_2 15
#define BATTERY_WARNING_MV  3200
#define BATTERY_CRITICAL_MV 3100
static const char *TAG = "ClockTask";
static const char *WEEKDAY_NAMES[7] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };
static WeatherDay s_weather[4];
static bool s_battery_warning_active = false;
static bool s_colon_visible = true;
static bool s_on_calendar_screen = false;
static int s_last_cal_day = -1;
static int s_last_cal_mon = -1;
static int s_last_cal_min = -1;

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

static const lv_image_dsc_t *weather_icon_src(WeatherIcon icon)
{
    switch (icon) {
        case WICON_SUNNY:         return &img_icon_sun;
        case WICON_PARTLY_CLOUDY: return &img_icon_partly_cloudy;
        case WICON_CLOUDY:        return &img_icon_cloud;
        case WICON_RAINY:         return &img_icon_rain;
        case WICON_SNOWY:         return &img_icon_snow;
        case WICON_STORM:         return &img_icon_storm;
    }
    return &img_icon_cloud;
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
        char temp_buf[32], hum_buf[16], date_buf[40], batt_buf[24];

        c[0] = (char)('0' + (t->tm_hour / 10) % 10);
        lv_label_set_text(objects.clock_hh1, c);
        c[0] = (char)('0' + t->tm_hour % 10);
        lv_label_set_text(objects.clock_hh2, c);
        c[0] = (char)('0' + (t->tm_min / 10) % 10);
        lv_label_set_text(objects.clock_mm1, c);
        c[0] = (char)('0' + t->tm_min % 10);
        lv_label_set_text(objects.clock_mm2, c);

        bool temp_negative = temperature < 0.0f;
        float temp_abs = temp_negative ? -temperature : temperature;
        int temp_whole = (int)temp_abs;
        int temp_frac = (int)((temp_abs - temp_whole) * 10.0f + 0.5f);
        if (temp_frac >= 10) { temp_frac = 0; temp_whole += 1; }

        snprintf(temp_buf, sizeof(temp_buf), "%s%d.%d°C", temp_negative ? "-" : "", temp_whole, temp_frac);
        snprintf(hum_buf, sizeof(hum_buf), "%d%%", (int)(humidity + 0.5f));
        snprintf(date_buf, sizeof(date_buf), "%02d.%02d.%04d", t->tm_mday, t->tm_mon + 1, t->tm_year + 1900);
        snprintf(batt_buf, sizeof(batt_buf), "%d.%02d", battery_mv / 1000, (battery_mv % 1000) / 10);

        lv_label_set_text(objects.temp, temp_buf);
        lv_label_set_text(objects.hum, hum_buf);
        lv_label_set_text(objects.date, date_buf);
        lv_label_set_text(objects.battery, batt_buf);

        /* the calendar screen carries its own copy of the top bar */
        calendar_update_top_bar(temp_buf, hum_buf, date_buf, batt_buf);

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
            char date_buf[40], temp_buf[32], cal_temp_buf[32];
            const lv_image_dsc_t *icon = weather_icon_src(classify_weathercode(days[i].weathercode));

            snprintf(date_buf, sizeof(date_buf), "%d/%d %s", days[i].day, days[i].month, WEEKDAY_NAMES[days[i].weekday]);
            snprintf(temp_buf, sizeof(temp_buf), "%d/%d°C", days[i].temp_max, days[i].temp_min);
            /* the calendar row is narrower, so it drops the unit */
            snprintf(cal_temp_buf, sizeof(cal_temp_buf), "%d/%d°", days[i].temp_max, days[i].temp_min);

            lv_label_set_text(date_objs[i], date_buf);
            lv_image_set_src(icon_objs[i], icon);
            lv_label_set_text(temp_objs[i], temp_buf);

            calendar_update_forecast(i, date_buf, icon, cal_temp_buf);
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

    ClockTime_SetTimezone(CLOCK_TIMEZONE);
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

    struct tm t_utc = {0};
    struct tm t = {0};

    set_status("Connecting to WiFi...");
    ESP_LOGI(TAG, "First time and weather sync over WiFi...");
    if (!WifiSync_SyncTimeOnce(&t_utc, CLOCK_WIFI_SSID, CLOCK_WIFI_PASS, 15000, wifi_connected_cb, s_weather)) {
        ESP_LOGE(TAG, "No WiFi connection at startup - halting device.");
        set_status("Failed to connect to WiFi. Please reset the device.");

        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    Pcf85063_SetTime(&t_utc);          /* the RTC now holds UTC */
    ClockTime_UtcToLocal(&t_utc, &t);  /* everything displayed is local */
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
    int last_minute = -1;
    for (;;) {
        esp_sleep_enable_timer_wakeup(1000000ULL);
        esp_light_sleep_start();

        struct tm now_utc;
        struct tm now;
        if (Pcf85063_GetTime(&now_utc) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read RTC, skipping this cycle.");
            continue;
        }
        ClockTime_UtcToLocal(&now_utc, &now);

        bool button_pressed = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO);

        /* While the low battery notice is up it owns the screen - let it. */
        if (button_pressed && !s_battery_warning_active) {
            ESP_LOGI(TAG, "BOOT button pressed - toggling screen...");

            while (gpio_get_level(KEY_BUTTON_PIN) == 0) {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            vTaskDelay(pdMS_TO_TICKS(50));

            if (Lvgl_lock(-1)) {
                if (s_on_calendar_screen) {
                    loadScreen(SCREEN_ID_MAIN);
                    s_on_calendar_screen = false;
                } else {
                    loadScreen(SCREEN_ID_CALENDAR);
                    update_calendar_display(&now);
                    update_clock_hands(now.tm_hour, now.tm_min);
                    s_last_cal_day = now.tm_mday;
                    s_last_cal_mon = now.tm_mon;
                    s_last_cal_min = now.tm_min;
                    s_on_calendar_screen = true;
                }
                Lvgl_Refresh();
                Lvgl_unlock();
            }
        }

        if (now.tm_min != last_minute) {
            last_minute = now.tm_min;

            bool is_sync_hour = (now.tm_hour == SYNC_HOUR_1 || now.tm_hour == SYNC_HOUR_2);

            if (is_sync_hour && now.tm_min == 0 && last_sync_hour != now.tm_hour) {
                ESP_LOGI(TAG, "Scheduled sync at %02d:00 (time + weather)...", now.tm_hour);
                struct tm ntp_utc;
                if (WifiSync_SyncTimeOnce(&ntp_utc, CLOCK_WIFI_SSID, CLOCK_WIFI_PASS, 15000, wifi_connected_cb, s_weather)) {
                    Pcf85063_SetTime(&ntp_utc);
                    ClockTime_UtcToLocal(&ntp_utc, &now);
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
                    s_battery_warning_active = false;
                    /* Go back to whichever screen the user was on, not always main. */
                    if (Lvgl_lock(-1)) {
                        loadScreen(s_on_calendar_screen ? SCREEN_ID_CALENDAR : SCREEN_ID_MAIN);
                        Lvgl_unlock();
                    }
                }
                update_labels(&now, temperature, humidity, battery_mv);
            }
        }

        /* Only touch the panel when something actually changed: every refresh
           is a full 400x300 render plus a full SPI write (RENDER_MODE_FULL). */
        if (Lvgl_lock(-1)) {
            bool dirty = false;

            if (s_battery_warning_active) {
                /* the notice is static - nothing to redraw */
            } else if (s_on_calendar_screen) {
                if (now.tm_mday != s_last_cal_day || now.tm_mon != s_last_cal_mon) {
                    update_calendar_display(&now);
                    s_last_cal_day = now.tm_mday;
                    s_last_cal_mon = now.tm_mon;
                    dirty = true;
                }
                if (now.tm_min != s_last_cal_min) {
                    update_clock_hands(now.tm_hour, now.tm_min);
                    s_last_cal_min = now.tm_min;
                    dirty = true;
                }
            } else {
                s_colon_visible = !s_colon_visible;
                lv_obj_set_style_opa(objects.obj2, s_colon_visible ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
                dirty = true;
            }

            if (dirty) {
                Lvgl_Refresh();
            }
            Lvgl_unlock();
        }
    }
}

void ClockTask_Start(void)
{
    xTaskCreatePinnedToCore(clock_task, "ClockTask", 12 * 1024, NULL, 3, NULL, 1);
}