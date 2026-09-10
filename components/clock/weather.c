#include "weather.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "Weather";

typedef struct {
    char *buf;
    int len;
    int cap;
} HttpBuffer;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    HttpBuffer *hb = (HttpBuffer *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && hb) {
        if (hb->len + evt->data_len < hb->cap - 1) {
            memcpy(hb->buf + hb->len, evt->data, evt->data_len);
            hb->len += evt->data_len;
            hb->buf[hb->len] = '\0';
        }
    }
    return ESP_OK;
}

static int calc_weekday(int year, int month, int day)
{
    if (month < 3) { month += 12; year -= 1; }
    int k = year % 100;
    int j = year / 100;
    int h = (day + 13 * (month + 1) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    return (h + 6) % 7;
}

static int round_temp(double v)
{
    return (v >= 0.0) ? (int)(v + 0.5) : (int)(v - 0.5);
}

static bool weather_fetch_once(WeatherDay days_out[4], WeatherNow *now_out)
{
    char url[256];
    snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
        "&daily=weathercode,temperature_2m_max,temperature_2m_min"
        "&current_weather=true"
        "&timezone=auto&forecast_days=4",
        CONFIG_WEATHER_LATITUDE, CONFIG_WEATHER_LONGITUDE);

    static char response[4096];
    response[0] = '\0';
    HttpBuffer hb = { .buf = response, .len = 0, .cap = sizeof(response) };

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &hb,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "Failed to fetch weather: %s, status %d", esp_err_to_name(err), status);
        return false;
    }

    cJSON *root = cJSON_Parse(response);
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse weather JSON");
        return false;
    }

    /* Current conditions are optional: a response missing them is still a
       usable forecast, so this is parsed before the daily block is validated
       and never fails the fetch. */
    if (now_out) {
        now_out->valid = false;
        cJSON *cur = cJSON_GetObjectItem(root, "current_weather");
        cJSON *cur_temp = cur ? cJSON_GetObjectItem(cur, "temperature") : NULL;
        cJSON *cur_code = cur ? cJSON_GetObjectItem(cur, "weathercode") : NULL;
        if (cJSON_IsNumber(cur_temp) && cJSON_IsNumber(cur_code)) {
            now_out->temp        = round_temp(cur_temp->valuedouble);
            now_out->weathercode = cur_code->valueint;
            now_out->valid       = true;
        } else {
            ESP_LOGW(TAG, "Response carried no current_weather block");
        }
    }

    cJSON *daily = cJSON_GetObjectItem(root, "daily");
    cJSON *times = daily ? cJSON_GetObjectItem(daily, "time") : NULL;
    cJSON *codes = daily ? cJSON_GetObjectItem(daily, "weathercode") : NULL;
    cJSON *tmax  = daily ? cJSON_GetObjectItem(daily, "temperature_2m_max") : NULL;
    cJSON *tmin  = daily ? cJSON_GetObjectItem(daily, "temperature_2m_min") : NULL;

    if (!times || !codes || !tmax || !tmin) {
        ESP_LOGW(TAG, "Incomplete JSON response");
        cJSON_Delete(root);
        return false;
    }

    int count = cJSON_GetArraySize(times);
    if (count > 4) count = 4;

    for (int i = 0; i < count; i++) {
        const char *date_str = cJSON_GetArrayItem(times, i)->valuestring;
        int year = 0, month = 0, day = 0;
        sscanf(date_str, "%d-%d-%d", &year, &month, &day);

        days_out[i].day = day;
        days_out[i].month = month;
        days_out[i].weekday = calc_weekday(year, month, day);
        days_out[i].weathercode = cJSON_GetArrayItem(codes, i)->valueint;
        days_out[i].temp_max = round_temp(cJSON_GetArrayItem(tmax, i)->valuedouble);
        days_out[i].temp_min = round_temp(cJSON_GetArrayItem(tmin, i)->valuedouble);
    }

    cJSON_Delete(root);
    return count >= 4;
}

bool Weather_Fetch(WeatherDay days_out[4], WeatherNow *now_out)
{
    const int max_attempts = 2;

    if (now_out) {
        now_out->valid = false;
    }

    for (int attempt = 1; attempt <= max_attempts; attempt++) {
        if (weather_fetch_once(days_out, now_out)) {
            return true;
        }
        if (attempt < max_attempts) {
            ESP_LOGW(TAG, "Retrying weather fetch (attempt %d/%d)...", attempt + 1, max_attempts);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    return false;
}