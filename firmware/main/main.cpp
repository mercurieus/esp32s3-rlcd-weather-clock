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

/* Hardware testing (2026-08-30) found the panel's visible settle artifact
   does not track a window's pixel area the way windowed refresh assumed -
   it tracks how much of the window's OWN content actually changes. The
   hint overlay's window is smaller than a clock digit's (3 CASET units vs
   ~12) but its border+background flip nearly all at once, and that reads
   as worse than a digit swap on an otherwise-white background, even
   though the digit's window is geometrically larger. DisplayPort has no
   way to see "how much of this window's content changed" - it only sees
   a rectangle - so that can't be the gate.
   What IS confirmed acceptable on hardware is windowing specifically for
   the Main screen's digital clock column (the clock_hh1/hh2/mm1/mm2
   digits and the ":" separator - see screens.c) - so that one screen
   region is hardcoded here as the sole windowing allow-list. Everything
   else (hint overlay, screen switches, anything outside this rect) goes
   through the unwindowed RLCD_Display() full-panel send instead.
   This rectangle is measured from screens.c/create_screen_main(): the
   digit labels sit at y=39 using ui_font_saira_condensed_bold200
   (line_height 142, so bottom edge is 39+142=181) and span the full
   digit row (clock_hh1 at x=-2 through clock_mm2 ending at x=398); the
   colon (obj2) sits slightly higher at y=30 using ui_font_saira200 (same
   142 line_height). Union of both: x:[0,399], y:[30,181]. */
#define CLOCK_DIGIT_AREA_X1 0
#define CLOCK_DIGIT_AREA_Y1 30
#define CLOCK_DIGIT_AREA_X2 399
#define CLOCK_DIGIT_AREA_Y2 181

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
    /* Only the last flush of a refresh pass needs to trigger the actual
       transfer: sending after every dirty rect produced N redundant sends
       in a row (see the history of this function for the batching fix
       this superseded). Whether that send windows or goes full-panel is
       gated on the clock-digit-area allow-list above this function - see
       its comment for why. */
    if (lv_display_flush_is_last(drv))
    {
        bool in_clock_digit_area =
            s_batch_x1 >= CLOCK_DIGIT_AREA_X1 && s_batch_x2 <= CLOCK_DIGIT_AREA_X2 &&
            s_batch_y1 >= CLOCK_DIGIT_AREA_Y1 && s_batch_y2 <= CLOCK_DIGIT_AREA_Y2;
        if (in_clock_digit_area)
        {
            RlcdPort.RLCD_DisplayAuto(s_batch_x1, s_batch_y1, s_batch_x2, s_batch_y2);
        }
        else
        {
            RlcdPort.RLCD_Display();
        }
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