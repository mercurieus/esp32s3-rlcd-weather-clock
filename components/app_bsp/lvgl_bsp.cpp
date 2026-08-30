#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include "lvgl_bsp.h"

static SemaphoreHandle_t lvgl_mux = NULL;
#define BYTES_PER_PIXEL (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565))

static const char *TAG = "LvglPort";
static int64_t s_last_tick_us = 0;

bool Lvgl_lock(int timeout_ms)
{
    const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mux, timeout_ticks) == pdTRUE;
}

void Lvgl_unlock(void)
{
    assert(lvgl_mux && "Lvgl_PortInit must be called first");
    xSemaphoreGive(lvgl_mux);
}

void Lvgl_Refresh(void)
{
    int64_t now_us = esp_timer_get_time();
    uint32_t elapsed_ms = (uint32_t)((now_us - s_last_tick_us) / 1000);
    s_last_tick_us = now_us;

    lv_tick_inc(elapsed_ms);
    lv_timer_handler();
    lv_refr_now(NULL);

    /* See RLCD_PANEL_SETTLE_DELAY_MS's own comment (lvgl_bsp.h) for why
       this exists and how it was confirmed necessary - short version: not
       optional, removing it brings back a multi-second visible roll on
       every large redraw. */
    vTaskDelay(pdMS_TO_TICKS(RLCD_PANEL_SETTLE_DELAY_MS));
}

void Lvgl_PortInit(int width, int height, DispFlushCb flush_cb)
{
    lvgl_mux = xSemaphoreCreateMutex();
    lv_init();
    lv_display_t * disp = lv_display_create(width, height);
    lv_display_set_flush_cb(disp, flush_cb);

    size_t buffer_size = width * height * BYTES_PER_PIXEL;
    uint8_t *buffer_1 = (uint8_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
    uint8_t *buffer_2 = (uint8_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
    assert(buffer_1);
    assert(buffer_2);

    /* PARTIAL: LVGL re-renders only the invalidated rectangle each refresh,
       not the whole tree - the colon blink alone used to force a full
       400x300 composite every second. This used to also mean a tick with
       multiple dirty rects cost one full SPI send per rect; Lvgl_FlushCallback
       now batches every dirty rect in one refresh pass into a single send
       (see its own comment). The send itself still ships the whole panel
       (RLCD_Display()) - windowed partial-refresh was tried twice on real
       hardware and didn't help either time, see Lvgl_FlushCallback's
       comment in main.cpp. */
    lv_display_set_buffers(disp, buffer_1, buffer_2, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    s_last_tick_us = esp_timer_get_time();

    ESP_LOGI(TAG, "LVGL initialized (manual mode - no background task)");
}