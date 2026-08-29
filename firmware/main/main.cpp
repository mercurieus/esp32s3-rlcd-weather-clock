#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <esp_timer.h>
#include <esp_log.h>

#include "display_bsp.h"
#include "lvgl_bsp.h"
#include "user_config.h"
#include "ui.h"
#include "clock_task.h"
#include "clock_time.h"
#include "nav.h"
#include "selftest.h"

DisplayPort RlcdPort(12, 11, 5, 40, 41, LCD_WIDTH, LCD_HEIGHT);

static void Lvgl_FlushCallback(lv_display_t *drv, const lv_area_t *area, uint8_t *color_map)
{
    /* A refresh pass can flush several disjoint dirty rects in a row
       (e.g. a screen switch or the hint overlay showing/hiding touches
       multiple widgets at once). RLCD_WaitTransferDone()'s semaphore is
       only ever re-given by RLCD_Display()'s completion callback below,
       which now fires just once per pass - so the wait must also happen
       just once per pass, before the pass's first pixel write, not on
       every flush call. Waiting unconditionally here would consume the
       semaphore's token on the first (non-last) flush and never get it
       back, deadlocking the very next flush of the same pass. */
    static bool s_batch_in_progress = false;
    if (!s_batch_in_progress)
    {
        RlcdPort.RLCD_WaitTransferDone();
        s_batch_in_progress = true;
    }

    uint16_t *buffer = (uint16_t *)color_map;
    for (int y = area->y1; y <= area->y2; y++)
    {
        for (int x = area->x1; x <= area->x2; x++)
        {
            uint8_t color = (*buffer < 0x7fff) ? ColorBlack : ColorWhite;
            RlcdPort.RLCD_SetPixel(x, y, color);
            buffer++;
        }
    }
    /* RLCD_Display() always ships the entire packed panel buffer over SPI
       (its column/row addressing is fixed-window - see the comment on
       RLCD_Display() itself), so there is nothing to gain by sending after
       every dirty rect: only the last flush of a refresh pass needs to
       trigger the actual transfer. Sending after each of N dirty rects
       produced N redundant full-panel sends in a row, each one now
       visible as its own top-to-bottom scan since RLCD_WaitTransferDone()
       properly serializes them - a compound UI change (screen switch,
       hint show/hide) looked like a slow, gradual multi-step redraw. */
    if (lv_display_flush_is_last(drv))
    {
        RlcdPort.RLCD_Display();
        s_batch_in_progress = false;
    }
    lv_disp_flush_ready(drv);
}

extern "C" void app_main(void)
{
    /* main.cpp orchestrates every *_RunTests() suite directly: it is the
       one place already required by everything, which keeps components
       under test depending on selftest rather than the reverse. See the
       comment on Selftest_End() in selftest.h. */
    Selftest_Begin();
    ClockTime_RunTests();
    Nav_RunTests();
    Selftest_End();

    RlcdPort.RLCD_Init();
    gpio_sleep_sel_dis(GPIO_NUM_12);
    gpio_sleep_sel_dis(GPIO_NUM_11);
    gpio_sleep_sel_dis(GPIO_NUM_5);
    gpio_sleep_sel_dis(GPIO_NUM_40);
    gpio_sleep_sel_dis(GPIO_NUM_41);
    Lvgl_PortInit(400, 300, Lvgl_FlushCallback);

    if (Lvgl_lock(-1))
    {
        ui_init();
        loadScreen(SCREEN_ID_INIT);
        Lvgl_Refresh();
        Lvgl_unlock();
    }

    ClockTask_Start();
}