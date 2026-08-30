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
       only ever re-given by the completion callback behind whichever send
       RLCD_DisplayAuto() below picks, which now fires just once per pass -
       so the wait must also happen just once per pass, before the pass's
       first pixel write, not on every flush call. Waiting unconditionally
       here would consume the semaphore's token on the first (non-last)
       flush and never get it back, deadlocking the very next flush of the
       same pass. s_batch_x1/y1/x2/y2 track the union of every dirty rect
       in the pass, so a compound change (several widgets at once) still
       windows to one rectangle covering everything touched, sent once. */
    static bool s_batch_in_progress = false;
    static int s_batch_x1, s_batch_y1, s_batch_x2, s_batch_y2;
    if (!s_batch_in_progress)
    {
        RlcdPort.RLCD_WaitTransferDone();
        s_batch_in_progress = true;
        s_batch_x1 = area->x1; s_batch_y1 = area->y1;
        s_batch_x2 = area->x2; s_batch_y2 = area->y2;
    }
    else
    {
        if (area->x1 < s_batch_x1) s_batch_x1 = area->x1;
        if (area->y1 < s_batch_y1) s_batch_y1 = area->y1;
        if (area->x2 > s_batch_x2) s_batch_x2 = area->x2;
        if (area->y2 > s_batch_y2) s_batch_y2 = area->y2;
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
    /* RLCD_DisplayAuto() windows the union rect's send on this hardware -
       see its own comment. Only the last flush of a refresh pass needs to
       trigger the actual transfer: sending after every dirty rect produced
       N redundant sends in a row (see the history of this function for the
       batching fix this superseded). */
    if (lv_display_flush_is_last(drv))
    {
        RlcdPort.RLCD_DisplayAuto(s_batch_x1, s_batch_y1, s_batch_x2, s_batch_y2);
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
    DisplayBsp_RunTests();
    Selftest_End();

    RlcdPort.RLCD_Init();
    /* The windowed-refresh design depends on an implicit invariant: the
       physical panel and DispBuffer must already agree on everything
       outside whatever window a later windowed send covers, since a
       windowed send only writes ITS OWN rectangle - it never touches the
       rest of the panel. RLCD_Init() only memsets DispBuffer to white
       (RLCD_ColorClear) and never sends anything, so without this, the
       panel's actual contents are whatever they were left in before this
       boot (stale, from before flashing, or garbage) until something
       happens to send a full frame. Make that explicit instead of relying
       on LVGL's first screen-load happening to invalidate enough of the
       display to cover for it.
       Order matters: WaitTransferDone() first consumes the semaphore's
       initial free token (given in the constructor, nothing in flight
       yet), so RLCD_Display() right after it queues a send behind a
       semaphore that's now correctly "taken" - the next WaitTransferDone()
       call (the first real LVGL flush) will genuinely block until this
       transfer completes rather than falling through on a token nobody
       consumed yet. */
    RlcdPort.RLCD_WaitTransferDone();
    RlcdPort.RLCD_Display();
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