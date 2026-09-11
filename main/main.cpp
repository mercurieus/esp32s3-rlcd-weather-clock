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
#include "wifi_stress.h"

DisplayPort RlcdPort(12, 11, 5, 40, 41, LCD_WIDTH, LCD_HEIGHT);

static void Lvgl_FlushCallback(lv_display_t *drv, const lv_area_t *area, uint8_t *color_map)
{
    /* A refresh pass can flush several disjoint dirty rects in a row (e.g.
       a screen switch or the hint overlay showing/hiding touches multiple
       widgets at once). RLCD_WaitTransferDone()'s semaphore is only ever
       re-given by the completion callback behind RLCD_Display(), which
       fires just once per pass - so the wait must also happen just once
       per pass, before the pass's first pixel write, not on every flush
       call. Waiting unconditionally here would consume the semaphore's
       token on the first (non-last) flush and never get it back,
       deadlocking the very next flush of the same pass.
       Windowed partial-refresh (sending just the changed sub-rectangle
       instead of the full panel) was implemented and hardware-tested
       2026-08-30, twice: once before Lvgl_Refresh()'s post-send settling
       delay was restored (invalid test, no reliable signal either way -
       see that function's comment), then again after, as a clean test.
       Still no measurable benefit: the ST7305's visible settle cost
       appears roughly fixed per send, not proportional to the addressed
       window's area, so windowing only adds extraction/addressing
       complexity for no win. Always a full-panel send. See git history
       and docs/superpowers/specs/2026-08-30-windowed-partial-refresh-
       design.md for the full investigation. */
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
    /* Only the last flush of a refresh pass needs to trigger the actual
       transfer: sending after every dirty rect produced N redundant sends
       in a row (see the history of this function for the batching fix
       this superseded). */
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

    /* Compiled out unless CONFIG_CLOCK_WIFI_STRESS is set. Runs here, in
       the same place as the other suites, because it needs the whole
       system up and must finish before ClockTask_Start() begins its own
       Wi-Fi work. */
    WifiStress_Run();

    RlcdPort.RLCD_Init();
    /* RLCD_Init() only memsets DispBuffer to white (RLCD_ColorClear) and
       never sends anything, so without an explicit send here the panel's
       actual contents are whatever they were left in before this boot
       (stale, from before flashing, or garbage) until the first LVGL
       flush happens to cover it. Make that explicit instead of relying on
       LVGL's first screen-load happening to invalidate the whole display.
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