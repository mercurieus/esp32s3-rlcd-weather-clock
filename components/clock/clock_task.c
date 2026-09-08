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
#include "overlay.h"
#include "buttons.h"
#include "nav.h"
#include "user_config.h"
#include "images.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
/* The RTC is disciplined twice a day - it drifts slowly and there is no
   reason to spend WiFi on it more often. Weather is separate: a "current"
   reading that is half a day old is not a current reading, so it refreshes
   on its own, shorter cycle. Both share one WiFi bring-up when they happen
   to fall together. */
#define SYNC_HOUR_1 5
#define SYNC_HOUR_2 15
#define WEATHER_REFRESH_MIN 30
/* Three missed refresh cycles before the reading is called stale - one
   failure is a blip, three in a row is an outage worth showing. */
#define WEATHER_STALE_US ((int64_t)WEATHER_REFRESH_MIN * 3 * 60 * 1000000LL)
#define BATTERY_WARNING_MV  3200
#define BATTERY_CRITICAL_MV 3100
static const char *TAG = "ClockTask";
static const char *WEEKDAY_NAMES[7] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };
static WeatherDay s_weather[4];
/* Cleared until the first Weather_Fetch succeeds. The top bar's forecast
   slot stays blank until then rather than showing a default cloud icon
   against a temperature that was never fetched. */
static bool s_weather_valid = false;
static WeatherNow s_weather_now = { .valid = false };
/* esp_timer keeps running across light sleep, so this stays honest through
   the sleep loop. Negative until the first successful fetch, which reads as
   stale - the bar should not claim fresh data it has never had. */
static int64_t s_last_fetch_us = -1;
static bool s_battery_warning_active = false;
static bool s_colon_visible = true;
static int s_last_cal_day = -1;
static int s_last_cal_mon = -1;
static int s_last_cal_sec = -1;
static int s_cal_disp_year = -1;
static int s_cal_disp_month = -1;

#define HINT_BOOT_MS   5000
#define HINT_EVENT_MS  2500

static nav_state_t s_nav;

/* 0xFF means "unknown", which forces the next apply_nav_visuals() to load. */
static uint8_t s_loaded_screen = 0xFF;

typedef enum { WICON_SUNNY, WICON_PARTLY_CLOUDY, WICON_CLOUDY, WICON_RAINY, WICON_SNOWY, WICON_STORM } WeatherIcon;

static void show_battery_warning(void)
{
    if (Lvgl_lock(-1)) {
        loadScreen(SCREEN_ID_INIT);
        lv_label_set_text(objects.info, "Battery low.\nPlease charge the device.");
        Lvgl_Refresh();
        s_loaded_screen = 0xFF;
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

/* DIAGNOSTIC: why the device last restarted. Shown on the panel rather than
   only logged, because the serial link to this board is unreliable and the
   restart being investigated happens on a WiFi refresh, hours apart. Panic
   (SW_CPU_RESET) and brownout are the two candidates for the reboot seen on
   weather refresh, and they need completely different fixes, so telling them
   apart is the whole point. */
static const char *reset_reason_str(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "power-on";
    case ESP_RST_EXT:      return "ext-pin";
    case ESP_RST_SW:       return "sw-restart";
    case ESP_RST_PANIC:    return "PANIC";
    case ESP_RST_INT_WDT:  return "INT-WDT";
    case ESP_RST_TASK_WDT: return "TASK-WDT";
    case ESP_RST_WDT:      return "WDT";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO:     return "sdio";
    default:               return "unknown";
    }
}

/* The sync runs inline on this task and blocks it for seconds at a time, so
   the indicator has to be painted before the call and left for the next tick
   to clear - nothing else gets to run in between. */
static void set_sync_busy(bool busy)
{
    if (Lvgl_lock(-1)) {
        Overlay_SetSyncBusy(busy);
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

        lv_label_set_text_fmt(objects.sync,
                               "Last weather sync: %02d.%02d.%04d %02d:%02d  Uptime: %dd %dh\n"
                               "Last restart: %s",
                               t->tm_mday, t->tm_mon + 1, t->tm_year + 1900,
                               t->tm_hour, t->tm_min,
                               uptime_days, uptime_hours,
                               reset_reason_str());
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
    if (Weather_Fetch(days, &s_weather_now)) {
        s_weather_valid = true;
        s_last_fetch_us = esp_timer_get_time();
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
        char temp_buf[32], hum_buf[16], date_buf[40], out_buf[16], batt_buf[16];

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

        /* The indoor reading drops the "C": that unit costs 15px at
           montserrat_20, which the trend arrow now needs, and the "%" on the
           humidity slot immediately to its right is enough to tell the two
           apart without it. */
        snprintf(temp_buf, sizeof(temp_buf), "%s%d.%d°", temp_negative ? "-" : "", temp_whole, temp_frac);
        snprintf(hum_buf, sizeof(hum_buf), "%d%%", (int)(humidity + 0.5f));
        snprintf(batt_buf, sizeof(batt_buf), "%d.%02d", battery_mv / 1000, (battery_mv % 1000) / 10);

        /* strftime's "%a %-d" is not portable in newlib - the "-" flag is a
           glibc extension - so the short date is assembled by hand. The
           newline is load-bearing: the face date is stacked, weekday over
           day number, because the colon column is far tighter horizontally
           than vertically. */
        snprintf(date_buf, sizeof(date_buf), "%s\n%d", WEEKDAY_NAMES[t->tm_wday], t->tm_mday);
        lv_label_set_text(objects.clock_date, date_buf);

        /* Prefer the current conditions; fall back to today's forecast high
           when the response carried no current_weather block, so the slot
           still says something rather than going blank. */
        const lv_image_dsc_t *out_icon = NULL;
        out_buf[0] = '\0';
        if (s_weather_now.valid) {
            out_icon = weather_icon_src(classify_weathercode(s_weather_now.weathercode));
            snprintf(out_buf, sizeof(out_buf), "%d°", s_weather_now.temp);
        } else if (s_weather_valid) {
            out_icon = weather_icon_src(classify_weathercode(s_weather[0].weathercode));
            snprintf(out_buf, sizeof(out_buf), "%d°", s_weather[0].temp_max);
        }

        const bool data_stale =
            (s_last_fetch_us < 0) ||
            (esp_timer_get_time() - s_last_fetch_us > WEATHER_STALE_US);

        Overlay_SetTopBar(temp_buf, hum_buf, out_icon, out_buf, data_stale,
                          batt_buf, Battery_PercentFromMv(battery_mv));

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

/* Main has five focusables; Cal+Clock has two: the < and > month arrows. */
static uint8_t nav_focus_count(uint8_t screen)
{
    return screen == NAV_SCREEN_MAIN ? 5 : 2;
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* One short verb per physical button - "Key" and "Boot" are the board's own
   silkscreen names, shown in the overlay's inverted badges, not the
   abstract Select/OK role names - with the long-press action parenthesised
   only where it differs from the short-press one. Either field may be
   empty when that button does nothing in the current mode. */
typedef struct {
    const char *key;
    const char *boot;
} hint_text_t;

static hint_text_t hint_for_mode(const nav_state_t *st)
{
    switch (st->mode) {
    case NAV_SCREEN:
        return st->screen == NAV_SCREEN_MAIN
            ? (hint_text_t){ "Calendar", "Open (hold: Settings)" }
            : (hint_text_t){ "Clock", "Open (hold: Settings)" };
    case NAV_FOCUS:
        /* OK_LONG reaches Settings from every mode but Settings itself
           (see nav_handle's top check) - Focus is no exception. */
        return (hint_text_t){ "Next (hold: Back)", "Details (hold: Settings)" };
    case NAV_DETAIL:
        /* Key has no short-press action here - only its hold, which lands
           on the same place as Boot's short press. */
        return (hint_text_t){ "(hold: Back)", "Back (hold: Settings)" };
    case NAV_SETTINGS:
        /* Key's hold is the actual exit; OK_LONG is a no-op while already
           in Settings (nav_handle's top check bails out), so Boot gets no
           hold note here. */
        return (hint_text_t){ "Next (hold: Exit)", "Change" };
    }
    return (hint_text_t){ "", "" };
}

/* Displayed-month state is normally seeded by apply_nav_visuals() on screen
   entry; that path is skipped if the RTC read there fails. Seed from a
   known-good `now` at the point of use so a transient RTC hiccup on entry
   can never render MONTHS[-1]. */
static void calclock_ensure_disp_month(const struct tm *now)
{
    if (s_cal_disp_month < 0) {
        s_cal_disp_year  = now->tm_year + 1900;
        s_cal_disp_month = now->tm_mon;
    }
}

/* Absolute screen rectangles for Main's focusable elements. Index 0 is the
   indoor sensor in the shared top bar; 1..4 are the forecast days. */
static void main_focus_area(uint8_t index, lv_area_t *out)
{
    if (index == 0) {
        out->x1 = 0;   out->y1 = 0;   out->x2 = 158; out->y2 = 33;
        return;
    }
    const int col = (index - 1) * 100;
    out->x1 = col;       out->y1 = 190;
    out->x2 = col + 99;  out->y2 = 283;
}

/* Absolute screen rectangles for Cal+Clock's focusable elements: 0 is the
   previous-month arrow, 1 is next. Matches CAL_COL_X0/CAL_ARROW_W/
   CAL_TITLE_Y in screens.c. */
static void calclock_focus_area(uint8_t index, lv_area_t *out)
{
    const int y0 = 40, y1 = 58;    /* CAL_TITLE_Y .. CAL_TITLE_Y + 18 */
    if (index == 0) {
        out->x1 = 6;  out->y1 = y0;  out->x2 = 25;  out->y2 = y1;
    } else {
        out->x1 = 196; out->y1 = y0; out->x2 = 215; out->y2 = y1;
    }
}

static void apply_nav_visuals(void)
{
    /* Only load on an actual screen change. Focus moves must not reload the
       screen, or every Select press would rebuild and flicker it. */
    if (s_loaded_screen != s_nav.screen) {
        loadScreen(s_nav.screen == NAV_SCREEN_MAIN
                       ? SCREEN_ID_MAIN
                       : SCREEN_ID_CALENDAR);
        s_loaded_screen = s_nav.screen;

        if (s_nav.screen == NAV_SCREEN_CALCLOCK) {
            /* Always reset to the real current month on entry - browsing
               state never persists across leaving and returning to the
               screen. */
            struct tm now_utc, now;
            if (Pcf85063_GetTime(&now_utc) == ESP_OK) {
                ClockTime_UtcToLocal(&now_utc, &now);
                s_cal_disp_year = now.tm_year + 1900;
                s_cal_disp_month = now.tm_mon;
                update_calendar_display(s_cal_disp_year, s_cal_disp_month, &now);
            }
        }
    }

    if (s_nav.mode == NAV_FOCUS) {
        lv_area_t a;
        if (s_nav.screen == NAV_SCREEN_MAIN) {
            main_focus_area(s_nav.focus, &a);
        } else {
            calclock_focus_area(s_nav.focus, &a);
        }
        Overlay_ShowFocus(&a);
    } else {
        Overlay_ShowFocus(NULL);
    }
}

static void clock_task(void *arg)
{
    set_status("Initializing...");

    ClockTime_SetTimezone(CLOCK_TIMEZONE);
    WifiSync_Init();
    Pcf85063_Init((gpio_num_t)ESP32_I2C_SDA_PIN, (gpio_num_t)ESP32_I2C_SCL_PIN);
    Shtc3_Init(Pcf85063_GetBusHandle());
    Battery_Init();

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

    ESP_LOGW(TAG, "Boot: last reset reason = %s", reset_reason_str());

    set_status("Starting...");
    vTaskDelay(pdMS_TO_TICKS(800));

    if (Lvgl_lock(-1)) {
        loadScreen(SCREEN_ID_MAIN);
        Lvgl_Refresh();
        Lvgl_unlock();
    }

    Buttons_Init(BTN_SELECT_PIN, BTN_OK_PIN);
    Buttons_EnableWakeup();
    nav_init(&s_nav, nav_focus_count, 1);

    if (Lvgl_lock(-1)) {
        hint_text_t h = hint_for_mode(&s_nav);
        Overlay_ShowHint(h.key, h.boot, now_ms(), HINT_BOOT_MS);
        Lvgl_Refresh();
        Lvgl_unlock();
    }

    int last_sync_hour = -1;
    int last_minute = -1;
    /* Seeded from the boot sync, which already fetched, so the first minute
       tick does not immediately fetch again. */
    int last_weather_slot = (t.tm_hour * 60 + t.tm_min) / WEATHER_REFRESH_MIN;
    int64_t prev_work_us = 0;
    for (;;) {
        /* Hold a real 1s cadence: the per-second Cal+Clock dial redraw adds
           ~150-200ms of render+SPI work per iteration, and a fixed 1s sleep
           on top of that made the RTC read skip a second in a regular beat.
           Sleep only the remainder of the second after last iteration's work. */
        uint64_t sleep_us = prev_work_us >= 1000000
                                ? 1000
                                : (1000000ULL - (uint64_t)prev_work_us);
        esp_sleep_enable_timer_wakeup(sleep_us);
        esp_light_sleep_start();

        int64_t cycle_start_us = esp_timer_get_time();

        struct tm now_utc;
        struct tm now;
        if (Pcf85063_GetTime(&now_utc) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read RTC, skipping this cycle.");
            continue;
        }
        ClockTime_UtcToLocal(&now_utc, &now);

        bool button_pressed = Buttons_PressPending();

        if (button_pressed) {
            /* Always drain the press even while the battery notice owns
               the screen: Buttons_ReadEvent() is what re-enables that
               pin's interrupt, so skipping it here would leave the pin
               permanently disabled after the first touch during the
               notice. Only the nav/UI reaction below is gated. */
            const btn_event_t ev = Buttons_ReadEvent();
            if (ev != BTN_EV_NONE && !s_battery_warning_active) {
                const bool changed = nav_handle(&s_nav, ev);
                if (Lvgl_lock(-1)) {
                    if (changed) {
                        if (s_nav.mode == NAV_DETAIL && s_nav.screen == NAV_SCREEN_CALCLOCK) {
                            /* Month arrows act immediately and bounce back to
                               FOCUS - see "Month browsing" in the design spec.
                               nav.c has no notion of "act, don't open a view",
                               so this reuses the same escape hatch Settings
                               rows already rely on. */
                            if (s_nav.focus == 0) {
                                s_cal_disp_month--;
                                if (s_cal_disp_month < 0) { s_cal_disp_month = 11; s_cal_disp_year--; }
                            } else {
                                s_cal_disp_month++;
                                if (s_cal_disp_month > 11) { s_cal_disp_month = 0; s_cal_disp_year++; }
                            }
                            update_calendar_display(s_cal_disp_year, s_cal_disp_month, &now);
                            s_nav.mode = NAV_FOCUS;
                            apply_nav_visuals();
                        } else {
                            apply_nav_visuals();
                            if (s_nav.mode == NAV_SCREEN &&
                                s_nav.screen == NAV_SCREEN_CALCLOCK) {
                                calclock_ensure_disp_month(&now);
                                update_calendar_display(s_cal_disp_year, s_cal_disp_month, &now);
                                update_clock_hands(now.tm_hour, now.tm_min, now.tm_sec);
                                s_last_cal_day = now.tm_mday;
                                s_last_cal_mon = now.tm_mon;
                                s_last_cal_sec = now.tm_sec;
                            }
                        }
                    }
                    hint_text_t h = hint_for_mode(&s_nav);
                    Overlay_ShowHint(h.key, h.boot, now_ms(), HINT_EVENT_MS);
                    Lvgl_Refresh();
                    Lvgl_unlock();
                }
            }
        }

        if (now.tm_min != last_minute) {
            last_minute = now.tm_min;

            const bool is_sync_hour =
                (now.tm_hour == SYNC_HOUR_1 || now.tm_hour == SYNC_HOUR_2);
            const bool time_due =
                is_sync_hour && now.tm_min == 0 && last_sync_hour != now.tm_hour;

            const int weather_slot =
                (now.tm_hour * 60 + now.tm_min) / WEATHER_REFRESH_MIN;
            const bool weather_due = (weather_slot != last_weather_slot);

            if (time_due || weather_due) {
                ESP_LOGI(TAG, "Scheduled sync at %02d:%02d (%s)...",
                         now.tm_hour, now.tm_min,
                         time_due ? "time + weather" : "weather");

                /* One WiFi bring-up serves both. SNTP runs either way because
                   that is what the API does, but the RTC is only written on a
                   time_due tick - a weather refresh should not be able to jump
                   the clock. */
                struct tm ntp_utc;
                set_sync_busy(true);
                const bool ok = WifiSync_SyncTimeOnce(&ntp_utc, CLOCK_WIFI_SSID,
                                                      CLOCK_WIFI_PASS, 15000,
                                                      wifi_connected_cb, s_weather);
                set_sync_busy(false);
                if (ok && time_due) {
                    Pcf85063_SetTime(&ntp_utc);
                    ClockTime_UtcToLocal(&ntp_utc, &now);
                }
                /* The label says "last weather sync", so it has to follow the
                   weather cycle, not the twice-daily time sync it used to
                   share a bring-up with. */
                update_sync_label(&now);
                update_forecast_labels(s_weather);

                if (time_due) {
                    last_sync_hour = now.tm_hour;
                }
                /* Advance the slot even on failure, so a persistent outage
                   retries on the next cycle rather than every minute. */
                last_weather_slot = weather_slot;
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
                        apply_nav_visuals();
                        Lvgl_unlock();
                    }
                }
                update_labels(&now, temperature, humidity, battery_mv);
            }
        }

        /* Only touch the panel when something actually changed: every refresh
           is a full 400x300 render plus a full SPI write (RENDER_MODE_FULL). */
        if (Lvgl_lock(-1)) {
            bool dirty = Overlay_TickHint(now_ms());

            if (s_battery_warning_active) {
                /* the notice is static - nothing to redraw */
            } else if (s_nav.screen == NAV_SCREEN_CALCLOCK) {
                if (now.tm_mday != s_last_cal_day || now.tm_mon != s_last_cal_mon) {
                    calclock_ensure_disp_month(&now);
                    update_calendar_display(s_cal_disp_year, s_cal_disp_month, &now);
                    s_last_cal_day = now.tm_mday;
                    s_last_cal_mon = now.tm_mon;
                    dirty = true;
                }
                if (now.tm_sec != s_last_cal_sec) {
                    update_clock_hands(now.tm_hour, now.tm_min, now.tm_sec);
                    s_last_cal_sec = now.tm_sec;
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

        prev_work_us = esp_timer_get_time() - cycle_start_us;
    }
}

void ClockTask_Start(void)
{
    xTaskCreatePinnedToCore(clock_task, "ClockTask", 12 * 1024, NULL, 3, NULL, 1);
}