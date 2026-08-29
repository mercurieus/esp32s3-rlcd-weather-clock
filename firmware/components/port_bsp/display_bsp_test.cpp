#include "display_bsp.h"
#include "selftest.h"
#include "sdkconfig.h"

#if CONFIG_CLOCK_SELFTEST

static void check_window(int width, int height, int x1, int y1, int x2, int y2,
                         uint8_t want_caset_xs, uint8_t want_caset_xe,
                         uint8_t want_raset_ys, uint8_t want_raset_ye,
                         int want_len)
{
    RlcdWindow w = RLCD_ComputeWindow(width, height, x1, y1, x2, y2);
    SELFTEST_CHECK_INT(w.caset_xs, want_caset_xs);
    SELFTEST_CHECK_INT(w.caset_xe, want_caset_xe);
    SELFTEST_CHECK_INT(w.raset_ys, want_raset_ys);
    SELFTEST_CHECK_INT(w.raset_ye, want_raset_ye);
    SELFTEST_CHECK_INT(w.len, want_len);
}

void DisplayBsp_RunTests(void)
{
    /* Full panel: must match RLCD_Display()'s existing hardcoded window
       exactly (CASET 0x12-0x2A, RASET 0x00-0xC7, len == the real
       DisplayLen for this hardware). */
    check_window(400, 300, 0, 0, 399, 299, 18, 42, 0, 199, 15000);

    /* Top band (y 0-33, roughly the shared top bar's height): must resolve
       to CASET's *low* end, matching the hardware-confirmed "low CASET =
       top of screen" direction - this is the case that would silently
       invert if the CASET-to-block_y sign were wrong. */
    check_window(400, 300, 0, 0, 399, 33, 18, 20, 0, 199, 1800);

    /* A single forecast-tile-sized rect (x 0-99, y 190-283, matching
       main_focus_area()'s tile geometry in clock_task.c): exercises both
       axes' rounding together on a non-full-width, non-full-height rect. */
    check_window(400, 300, 0, 190, 99, 283, 33, 41, 0, 49, 1350);

    /* RASET-only rounding: a narrow horizontal slice (x 2-5) with the full
       height, isolating the X-axis rounding from the Y-axis math above. */
    check_window(400, 300, 2, 0, 5, 299, 18, 42, 1, 2, 150);
}

#else

void DisplayBsp_RunTests(void) {}

#endif
