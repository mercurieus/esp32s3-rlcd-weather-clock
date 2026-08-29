# Cal+Clock Vista Redraw Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Redraw the existing Cal+Clock screen as a plain-Vista analog dial with a working second hand and month browsing, give the shared top bar and Cal+Clock's bezel/buttons a dithered metal look, and replace the SPI flush path's blind 50 ms delay with an accurate completion wait.

**Architecture:** No screens are added and no `nav.c` change is needed — Cal+Clock stays the second top-level screen it already is. The dial gets a second hand and finer ticks; the calendar grid moves from one multi-line label per column to one label per cell so leading/trailing days can use a smaller font; `<`/`>` month-browsing reuses `nav.c`'s existing FOCUS/DETAIL machinery the same way Settings already does ("caller acts, then bounces back to FOCUS"). The shared top bar and Cal+Clock's own bezel/buttons get a Bayer-dithered metal treatment, gated on an early hardware check that dithered pixels don't bleed. The SPI panel driver gets a real completion callback in place of a padded delay.

**Tech Stack:** ESP-IDF 5.5.5, LVGL 9.5, FreeRTOS, picolibc, Waveshare ESP32-S3-RLCD-4.2.

**Spec:** `docs/superpowers/specs/2026-08-29-calclock-vista-redraw-design.md`

**Covers milestone 4** of `docs/superpowers/specs/2026-08-26-widget-navigation-and-settings-design.md`, refined into its own spec the same way milestones 1-3 became `docs/superpowers/plans/2026-08-26-navigation-foundation.md`. Milestones 5-7 (weather API extension, Settings/NVS, chimes) get their own plans once this lands.

## Global Constraints

- **1-bit panel.** `Lvgl_FlushCallback` thresholds RGB565 at `0x7fff`. No grey exists. Every stroke must be pure black (`0x000000`) and at least 2 px wide.
- **`components/ui/CMakeLists.txt` uses an explicit `SRCS` list, not a glob.** Add any new file under `components/ui/` to it by hand.
- **LVGL heap is 64 KB** (`CONFIG_LV_MEM_SIZE_KILOBYTES=64`, built-in allocator). Watch object count — the grid rework alone adds 35 new label objects (42 day cells minus the 7 that already exist as column headers).
- **LVGL timers do not run on their own.** `lv_timer_handler()` is only called from `Lvgl_Refresh()`. Anything time-based must be driven from the one-second task tick.
- **Buttons:** Select = `GPIO_NUM_18`, OK = `GPIO_NUM_0`. Active low, internal pull-up.
- **All LVGL calls must hold the lock:** `Lvgl_lock(-1)` / `Lvgl_unlock()`.
- **Screen coordinates:** 400x300. Shared overlay owns y 0..36. Screens own y 38..300.
- **`nav.c` has no backward-navigation event.** `SELECT_SHORT` only ever moves focus forward with wrap. Month browsing (Task 4) works around this without touching `nav.c` — see that task.
- **`DispBuffer` (`components/port_bsp/display_bsp.cpp`) is single-buffered.** Writing to it while a previous SPI transfer is still in flight is a data race. Task 1 fixes the only place this was informally protected (a padded delay) with a real wait; every later task that touches drawing code relies on that wait already being in place and must not reintroduce a blind delay.

## Build environment

Same as `docs/superpowers/plans/2026-08-26-navigation-foundation.md`'s "Build environment" section — reuse that PowerShell prelude verbatim. From `firmware/`:

- Build: `& $py "$env:IDF_PATH\tools\idf.py" build`
- Flash: `& $py "$env:IDF_PATH\tools\idf.py" -p COM<N> flash`

There is no host C compiler for the LVGL/ESP-IDF side, so hardware verification is flash-and-look, same as every prior task in this project. The font-metrics check in Task 6 is host-side Python, like `tools/verify_nav.py`, and runs instantly.

## File structure

| File | Responsibility |
|---|---|
| `components/port_bsp/display_bsp.[h,cpp]` | Modified: real SPI transfer-done wait, replacing the blind delay |
| `components/app_bsp/lvgl_bsp.cpp` | Modified: drop the `vTaskDelay(50)` |
| `main/main.cpp` | Modified: `Lvgl_FlushCallback` waits before writing pixels |
| `components/ui/dither.[h,c]` | New: Bayer 4x4 ordered-dither helper, shared by the hardware test pattern and the real header rail/bezel/button rendering |
| `components/ui/overlay.c` | Modified: shared top bar becomes a dithered metal rail with recessed plaques |
| `components/ui/screens.c` | Modified: dial gets a second hand and 60 minor ticks; calendar grid becomes per-cell; `<`/`>` month-browsing arrows; bezel and arrow dithering |
| `components/clock/clock_task.c` | Modified: `nav_focus_count` for Cal+Clock, its focus-area geometry, the DETAIL-bounce month-shift handler, `update_clock_hands`'s new `second` parameter threaded through |
| `tools/verify_calendar_layout.py` | New: host-side check of grid/dial/rail geometry against real LVGL font metrics, parsed from the compiled font `.c` sources |

---

### Task 1: Accurate SPI transfer wait

**Files:**
- Modify: `firmware/components/port_bsp/display_bsp.h`
- Modify: `firmware/components/port_bsp/display_bsp.cpp`
- Modify: `firmware/components/app_bsp/lvgl_bsp.cpp:37`
- Modify: `firmware/main/main.cpp:17-31`

**Interfaces:**
- Consumes: nothing new.
- Produces: `void DisplayPort::RLCD_WaitTransferDone()` — blocks until any SPI color transfer already in flight has completed. Later tasks that add drawing code must not need to know this exists; it is called once, at the top of `Lvgl_FlushCallback`, and nowhere else.

- [ ] **Step 1: Add the semaphore and the wait method**

In `firmware/components/port_bsp/display_bsp.h`, add to the includes:

```cpp
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
```

Add to the private section, after `int DisplayLen;`:

```cpp
    SemaphoreHandle_t xfer_done_ = NULL;
    static bool on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t *edata,
                                    void *user_ctx);
```

Add to the public section, after `void RLCD_Display();`:

```cpp
    void RLCD_WaitTransferDone();
```

- [ ] **Step 2: Create the semaphore and register the callback**

In `firmware/components/port_bsp/display_bsp.cpp`, in the constructor, immediately after
`ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)spihost, &io_config, &io_handle));`:

```cpp
    xfer_done_ = xSemaphoreCreateBinary();
    assert(xfer_done_);
    xSemaphoreGive(xfer_done_);   /* nothing in flight yet */

    esp_lcd_panel_io_callbacks_t cbs = {};
    cbs.on_color_trans_done = &DisplayPort::on_color_trans_done;
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, this));
```

- [ ] **Step 3: Implement the callback and the wait method**

Add near `RLCD_Sendbuffera`:

```cpp
bool DisplayPort::on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                      esp_lcd_panel_io_event_data_t *edata,
                                      void *user_ctx)
{
    DisplayPort *self = static_cast<DisplayPort *>(user_ctx);
    BaseType_t high_task_awoken = pdFALSE;
    xSemaphoreGiveFromISR(self->xfer_done_, &high_task_awoken);
    return high_task_awoken == pdTRUE;
}

void DisplayPort::RLCD_WaitTransferDone()
{
    xSemaphoreTake(xfer_done_, portMAX_DELAY);
}
```

- [ ] **Step 4: Move the wait to before the pixel writes, not before the send**

In `firmware/main/main.cpp`, `Lvgl_FlushCallback` currently reads:

```cpp
static void Lvgl_FlushCallback(lv_display_t *drv, const lv_area_t *area, uint8_t *color_map)
{
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
    RlcdPort.RLCD_Display();
    lv_disp_flush_ready(drv);
}
```

Change it to wait first — this is the fix, not the send:

```cpp
static void Lvgl_FlushCallback(lv_display_t *drv, const lv_area_t *area, uint8_t *color_map)
{
    RlcdPort.RLCD_WaitTransferDone();

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
    RlcdPort.RLCD_Display();
    lv_disp_flush_ready(drv);
}
```

- [ ] **Step 5: Drop the blind delay**

In `firmware/components/app_bsp/lvgl_bsp.cpp`, `Lvgl_Refresh()` currently ends with:

```c
    lv_tick_inc(elapsed_ms);
    lv_timer_handler();
    lv_refr_now(NULL);

    vTaskDelay(pdMS_TO_TICKS(50));
}
```

Remove the `vTaskDelay` line:

```c
    lv_tick_inc(elapsed_ms);
    lv_timer_handler();
    lv_refr_now(NULL);
}
```

- [ ] **Step 6: Build and verify on hardware**

Run: `& $py "$env:IDF_PATH\tools\idf.py" build`
Expected: builds clean.

Flash and confirm: Main and Cal+Clock still render correctly (top bar, clock digits, colon blink, forecast tiles), and the focus frame / hint overlay from the navigation-foundation work still behave exactly as before. This is a timing-only change — "looks and feels identical, if anything snappier" is the bar. No visual regression is acceptable; if anything looks wrong (torn frames, missing content), the wait is in the wrong place and Step 4 needs revisiting before proceeding to any other task, since every later task's drawing code depends on this being correct.

- [ ] **Step 7: Commit**

```bash
git add firmware/components/port_bsp/display_bsp.h firmware/components/port_bsp/display_bsp.cpp \
       firmware/components/app_bsp/lvgl_bsp.cpp firmware/main/main.cpp
git commit -m "fix: wait for the actual SPI transfer instead of a padded delay

Lvgl_FlushCallback now waits on a real on_color_trans_done completion
signal before writing this frame's pixels, replacing a blind 50ms
vTaskDelay that was informally protecting DispBuffer (single-buffered,
not safe to mutate while a previous transfer is still in flight) against
a race it never actually proved safe against."
```

---

### Task 2: Dither hardware verification

The whole metal treatment (Task 5, and the header rail in Task 5) depends on this panel rendering isolated dithered pixels cleanly rather than having them bleed into a flat grey blur. This must be confirmed before writing any bezel/rail code for real — see `docs/superpowers/specs/2026-08-29-calclock-vista-redraw-design.md`'s "Metal via dithering" section.

**Files:**
- Create: `firmware/components/ui/dither.h`
- Create: `firmware/components/ui/dither.c`
- Modify: `firmware/components/ui/CMakeLists.txt`
- Modify: `firmware/components/clock/clock_task.c` (temporary test-pattern hook, see Step 3)

**Interfaces:**
- Consumes: nothing.
- Produces: `uint8_t Dither_Threshold(int x, int y, uint8_t grey)` — given a screen position and a target grey level 0..255, returns `1` (paint black) or `0` (paint white) using a tiled 4x4 Bayer matrix. Task 5 depends on this exact signature.

- [ ] **Step 1: Write the Bayer matrix and threshold function**

```c
// firmware/components/ui/dither.h
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ordered (Bayer 4x4) dithering: given a target grey level (0 = black,
   255 = white) and a screen position, decides whether this one pixel
   should be black or white. Tiling the 4x4 matrix across a region turns a
   flat grey fill into a checker-like pattern that approximates that grey
   on a 1-bit panel - a plain grey fill would just collapse to solid under
   this panel's hard RGB565-at-0x7fff threshold (see Lvgl_FlushCallback in
   main/main.cpp), so the dithering has to happen at this level, before
   LVGL or the panel driver ever sees a single flat colour. */
uint8_t Dither_Threshold(int x, int y, uint8_t grey);

#ifdef __cplusplus
}
#endif
```

```c
// firmware/components/ui/dither.c
#include "dither.h"

/* Values 0..15, tiled across the screen by (x % 4, y % 4). */
static const uint8_t BAYER_4X4[4][4] = {
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 },
};

uint8_t Dither_Threshold(int x, int y, uint8_t grey)
{
    /* Map the matrix cell to the centre of its slice of the 0..255 range,
       so grey=0 never paints anything and grey=255 always does. */
    const uint8_t cell = BAYER_4X4[y & 3][x & 3];
    const int threshold = (cell * 256 + 128) / 16;
    return (grey < threshold) ? 1 : 0;
}
```

- [ ] **Step 2: Add it to the build**

In `firmware/components/ui/CMakeLists.txt`, add `"dither.c"` to the `SRCS` list (alphabetically, after `"ui.c"`... actually before it — keep the existing alphabetical order: it goes between `"screens.c"` and `"styles.c"`? No: `dither.c` sorts before `images.c`. Insert at the top of the list):

```cmake
idf_component_register(
    SRCS
        "dither.c"
        "images.c"
        "nav.c"
        "nav_test.c"
        "overlay.c"
        "screens.c"
        "styles.c"
        "ui.c"
        "ui_font_saira200.c"
        "ui_font_saira_condensed_bold200.c"
        "ui_image_icon_cloud.c"
        "ui_image_icon_partly_cloudy.c"
        "ui_image_icon_rain.c"
        "ui_image_icon_snow.c"
        "ui_image_icon_storm.c"
        "ui_image_icon_sun.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
    PRIV_REQUIRES selftest
)
```

- [ ] **Step 3: Flash a temporary tone-ramp test pattern**

This step is deliberately throwaway - it exists only to answer one yes/no question on real hardware, per the "Spike" pattern this project has used for hardware probes before (button pin confirmation, layer persistence). `RlcdPort` (the `DisplayPort` instance) and `Lvgl_FlushCallback` both already live in `firmware/main/main.cpp:15`, which is C++ - put the test pattern there too, rather than reaching for it from `clock_task.c` (plain C, and this project's existing pattern is that `clock_task.c` never touches `RlcdPort` directly; only `main.cpp` does). Add to `firmware/main/main.cpp`, after `Lvgl_FlushCallback`:

```cpp
/* TEMPORARY dither hardware verification - see Task 2 of
   docs/superpowers/plans/2026-08-29-calclock-vista-redraw.md. Remove once
   confirmed (Step 5). */
static void DitherTestPattern(void)
{
    const int band_w = 400 / 8;
    for (int band = 0; band < 8; band++) {
        const uint8_t grey = (uint8_t)(band * 32);
        for (int y = 0; y < 300; y++) {
            for (int x = band * band_w; x < (band + 1) * band_w; x++) {
                RlcdPort.RLCD_SetPixel(x, y, Dither_Threshold(x, y, grey) ? ColorBlack : ColorWhite);
            }
        }
    }
    RlcdPort.RLCD_Display();
}
```

Add `#include "dither.h"` to `main.cpp`'s includes. Call it from `app_main()`, immediately after `RlcdPort.RLCD_Init();` and before anything else touches the panel:

```cpp
    RlcdPort.RLCD_Init();
    DitherTestPattern();
    vTaskDelay(pdMS_TO_TICKS(60000));
```

- [ ] **Step 4: Build, flash, and inspect**

Run: `& $py "$env:IDF_PATH\tools\idf.py" build`, then flash.

Look at the panel: 8 vertical bands, left to right darkest to lightest. Confirm each band shows a visible dithered texture (a checker-like pattern of black and white pixels) rather than a flat, blurred grey block. Report which of these you see:

- **Bands read as a texture, distinguishably different shades of "grey" from left to right** - dithering is viable, proceed to Step 5 and then Task 5 later builds it into the real header rail/bezel/buttons.
- **Bands look flat/blurred, hard to tell one band from its neighbour** - dithering does not survive this panel's reflective/viewing characteristics. Stop here; do not proceed to Task 5's dithering work. Report this back before continuing the plan - the header rail and bezel need a different (non-dithered) treatment, which is a design change, not an implementation detail, and needs to go back through brainstorming.

- [ ] **Step 5: Remove the temporary test pattern**

Once confirmed, delete the `DitherTestPattern` function, its call site, its `vTaskDelay(60000)`, and the `#include "dither.h"` from `main.cpp` if nothing else in it needs `dither.h` yet (Task 5 will re-add it). `dither.h`/`dither.c` themselves stay - they're the real, permanent Bayer-dithering primitive Task 5 uses.

Run: `& $py "$env:IDF_PATH\tools\idf.py" build`
Expected: builds clean, boots normally into Main as before.

- [ ] **Step 6: Commit**

```bash
git add firmware/components/ui/dither.h firmware/components/ui/dither.c firmware/components/ui/CMakeLists.txt
git commit -m "feat: add the Bayer 4x4 dither primitive, confirmed on hardware

Dither_Threshold() is the shared ordered-dithering building block for
the metal header rail and Cal+Clock's bezel/buttons (Task 5). Verified
on the actual panel with a throwaway tone-ramp test pattern before
committing to using it for real - dithered pixels render as a visible
texture, not a bled-together flat grey."
```

---

### Task 3: Dial and second hand

**Files:**
- Modify: `firmware/components/ui/screens.h`
- Modify: `firmware/components/ui/screens.c`
- Modify: `firmware/components/clock/clock_task.c`

**Interfaces:**
- Consumes: nothing new.
- Produces: `void update_clock_hands(int hour, int minute, int second)` — signature change from the current `(int hour, int minute)`. Every later task that calls this must pass all three.

- [ ] **Step 1: Reposition the dial and thread `second` through the signature**

In `firmware/components/ui/screens.c`, the dial's screen position constants currently read:

```c
#define CAL_CLOCK_X       240
#define CAL_CLOCK_Y       52
```

Change to match the spec's dial geometry (x 238..388, y 62..212 - `CLOCK_SIZE` is already 150, so `238 + 150 = 388` and `62 + 150 = 212` land exactly on those bounds):

```c
#define CAL_CLOCK_X       238
#define CAL_CLOCK_Y       62
```

In `firmware/components/ui/screens.h`, change:

```c
void update_clock_hands(int hour, int minute);
```

to:

```c
void update_clock_hands(int hour, int minute, int second);
```

- [ ] **Step 2: Add 60 minor ticks at r=58**

In `firmware/components/ui/screens.c`, `draw_clock_face()` currently draws only the 12 major ticks. Add 60 minor ticks - 2 px wide, 4 px long, at radius 58 (spacing between adjacent 60-tick marks at r=58 is ~6.3 px, wide enough for a 2 px stroke to survive the 1-bit threshold; 1 px does not, which is what produced speckle noise the first time this was tried - see the design spec's "Dial and grid" section). Skip the 12 positions that coincide with major ticks, since those are already drawn:

```c
static void draw_clock_face(lv_layer_t *layer)
{
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(0x000000);
    line_dsc.opa = LV_OPA_COVER;

    /* 60 minor ticks, skipping the 12 positions the major ticks already
       cover (every 5th one). 2 px wide, 4 px long, r=58 - 1 px does not
       survive the 1 bit threshold at this spacing. */
    line_dsc.width = 2;
    for (int i = 0; i < 60; i++) {
        if (i % 5 == 0) continue;
        double angle = (i * 6 - 90) * M_PI / 180.0;
        int inner = 58 - 4;
        int outer = 58;
        line_dsc.p1.x = CLOCK_CENTER + (int)(inner * cos(angle));
        line_dsc.p1.y = CLOCK_CENTER + (int)(inner * sin(angle));
        line_dsc.p2.x = CLOCK_CENTER + (int)(outer * cos(angle));
        line_dsc.p2.y = CLOCK_CENTER + (int)(outer * sin(angle));
        lv_draw_line(layer, &line_dsc);
    }

    /* Only the 12 hour marks: verified to survive the 1 bit threshold at
       this spacing (60 minute ticks did not, at 1 px - see above). */
    line_dsc.width = 3;
    for (int i = 0; i < 12; i++) {
        double angle = (i * 30 - 90) * M_PI / 180.0;
        int inner = CLOCK_RADIUS - 11;
        int outer = CLOCK_RADIUS - 2;
        line_dsc.p1.x = CLOCK_CENTER + (int)(inner * cos(angle));
        line_dsc.p1.y = CLOCK_CENTER + (int)(inner * sin(angle));
        line_dsc.p2.x = CLOCK_CENTER + (int)(outer * cos(angle));
        line_dsc.p2.y = CLOCK_CENTER + (int)(outer * sin(angle));
        lv_draw_line(layer, &line_dsc);
    }

    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.color = lv_color_hex(0x000000);
    arc_dsc.width = 3;
    arc_dsc.radius = CLOCK_RADIUS;
    arc_dsc.center.x = CLOCK_CENTER;
    arc_dsc.center.y = CLOCK_CENTER;
    arc_dsc.start_angle = 0;
    arc_dsc.end_angle = 360;
    arc_dsc.opa = LV_OPA_COVER;
    lv_draw_arc(layer, &arc_dsc);
}
```

- [ ] **Step 3: Add the second hand and update the function signature**

Replace `update_clock_hands` (the comment above it is now wrong - there is a second hand, remove it):

```c
void update_clock_hands(int hour, int minute, int second)
{
    lv_obj_t *canvas = objects.clock_canvas;
    if (!canvas) return;

    lv_canvas_fill_bg(canvas, lv_color_hex(0xFFFFFF), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    draw_clock_face(&layer);

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(0x000000);
    line_dsc.opa = LV_OPA_COVER;
    line_dsc.p1.x = CLOCK_CENTER;
    line_dsc.p1.y = CLOCK_CENTER;

    double h_angle = ((hour % 12) * 30 + minute * 0.5 - 90) * M_PI / 180.0;
    line_dsc.width = 5;
    line_dsc.p2.x = CLOCK_CENTER + (int)(34 * cos(h_angle));
    line_dsc.p2.y = CLOCK_CENTER + (int)(34 * sin(h_angle));
    lv_draw_line(&layer, &line_dsc);

    double m_angle = (minute * 6 - 90) * M_PI / 180.0;
    line_dsc.width = 3;
    line_dsc.p2.x = CLOCK_CENTER + (int)(52 * cos(m_angle));
    line_dsc.p2.y = CLOCK_CENTER + (int)(52 * sin(m_angle));
    lv_draw_line(&layer, &line_dsc);

    double s_angle = (second * 6 - 90) * M_PI / 180.0;
    line_dsc.width = 2;
    line_dsc.p2.x = CLOCK_CENTER + (int)(58 * cos(s_angle));
    line_dsc.p2.y = CLOCK_CENTER + (int)(58 * sin(s_angle));
    lv_draw_line(&layer, &line_dsc);

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_hex(0x000000);
    rect_dsc.bg_opa = LV_OPA_COVER;
    lv_area_t hub = { CLOCK_CENTER - 4, CLOCK_CENTER - 4, CLOCK_CENTER + 4, CLOCK_CENTER + 4 };
    lv_draw_rect(&layer, &rect_dsc, &hub);

    lv_canvas_finish_layer(canvas, &layer);
    lv_obj_invalidate(canvas);
}
```

- [ ] **Step 4: Update every call site and redraw on second changes, not just minute changes**

In `firmware/components/clock/clock_task.c`, rename the tracked field and pass seconds through. Change:

```c
static int s_last_cal_min = -1;
```

to:

```c
static int s_last_cal_sec = -1;
```

Update both call sites. The button-handler block (around line 368-372):

```c
                            update_calendar_display(&now);
                            update_clock_hands(now.tm_hour, now.tm_min);
                            s_last_cal_day = now.tm_mday;
                            s_last_cal_mon = now.tm_mon;
                            s_last_cal_min = now.tm_min;
```

becomes:

```c
                            update_calendar_display(&now);
                            update_clock_hands(now.tm_hour, now.tm_min, now.tm_sec);
                            s_last_cal_day = now.tm_mday;
                            s_last_cal_mon = now.tm_mon;
                            s_last_cal_sec = now.tm_sec;
```

The per-second dirty-check block (around line 433-441):

```c
                if (now.tm_min != s_last_cal_min) {
                    update_clock_hands(now.tm_hour, now.tm_min);
                    s_last_cal_min = now.tm_min;
                    dirty = true;
                }
```

becomes:

```c
                if (now.tm_sec != s_last_cal_sec) {
                    update_clock_hands(now.tm_hour, now.tm_min, now.tm_sec);
                    s_last_cal_sec = now.tm_sec;
                    dirty = true;
                }
```

This is the change that makes the second hand actually move: the old check only redrew on minute rollover, which is why there was never a second hand before (see the deleted comment in Step 3).

- [ ] **Step 5: Build and verify on hardware**

Run: `& $py "$env:IDF_PATH\tools\idf.py" build`
Expected: builds clean.

Flash and confirm on the Cal+Clock screen:
1. The dial sits at its new position (moved 2 px left, 10 px down from before - should be barely noticeable but confirm nothing overlaps the grid or forecast row).
2. 60 minor ticks are visible around the rim, distinct from the 12 major hour ticks, and none of them look like noise/speckle.
3. **The second hand is visible and moves once per second.** This is the headline feature of this task - the old dial never had one because it only redrew on minute changes; confirm it now sweeps continuously.
4. Hour and minute hands still point correctly (spot-check against the digital time on Main).

- [ ] **Step 6: Commit**

```bash
git add firmware/components/ui/screens.h firmware/components/ui/screens.c firmware/components/clock/clock_task.c
git commit -m "feat: add the dial's second hand and 60 minor ticks

update_clock_hands() gains a second parameter and the per-second tick
now redraws the dial on every second, not just every minute - that
minute-only check is why there was never a second hand before. Also
adds 60 minor ticks at r=58 (2px, survives the 1-bit threshold at that
spacing where 1px did not) and repositions the dial to the spec's
238..388 x 62..212 bounds."
```

---

### Task 4: Calendar grid and month browsing

**Files:**
- Modify: `firmware/components/ui/screens.h`
- Modify: `firmware/components/ui/screens.c`
- Modify: `firmware/components/clock/clock_task.c`

**Interfaces:**
- Consumes: `nav_state_t`, `nav_handle()`, `Overlay_ShowFocus()` (all from milestones 1-3, unchanged).
- Produces: `void update_calendar_display(int disp_year, int disp_month, const struct tm *today)` — signature change from the current `(const struct tm *ti)`. `disp_year` is the real year (e.g. 2026, not `tm_year`'s since-1900 form); `disp_month` is 0-based (0 = January), matching `tm_mon`.

- [ ] **Step 1: Replace the per-column grid with per-cell labels**

In `firmware/components/ui/screens.c`, the grid currently has one label per column (`s_cal_cols[7]`) holding a `\n`-joined multi-line string, which cannot mix font sizes within one label - needed for leading/trailing days to render smaller than the current month's days. Replace with one label per header column (unchanged, header text never mixes fonts) plus a new 6x7 array of individual day-number labels.

Change:

```c
static lv_obj_t *s_cal_cols[7];
```

to:

```c
static lv_obj_t *s_cal_cols[7];              /* header row: Mo..Su */
static lv_obj_t *s_cal_days[CAL_ROWS][7];    /* one label per day cell */
```

In `create_screen_calendar()`, the header-column loop currently sets multi-line text and line-space styling meant for the day numbers too - simplify it to header-only (single line), and add the new per-cell day labels after it:

```c
    static const char *DOW[7] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };
    for (int c = 0; c < 7; c++) {
        lv_obj_t *col = lv_label_create(obj);
        s_cal_cols[c] = col;
        lv_obj_set_pos(col, CAL_COL_X0 + c * CAL_COL_W, CAL_GRID_Y);
        lv_obj_set_size(col, CAL_COL_W, CAL_ROW_H);
        lv_obj_set_style_text_font(col, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(col, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(col, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text_static(col, DOW[c]);
    }

    /* One label per day cell, not one multi-line label per column: leading
       and trailing days (from the adjacent month) render in montserrat_12
       against the current month's montserrat_14, and a single lv_label
       cannot mix fonts within itself. */
    for (int r = 0; r < CAL_ROWS; r++) {
        for (int c = 0; c < 7; c++) {
            lv_obj_t *cell = lv_label_create(obj);
            s_cal_days[r][c] = cell;
            lv_obj_set_pos(cell, CAL_COL_X0 + c * CAL_COL_W, CAL_GRID_Y + (r + 1) * CAL_ROW_H);
            lv_obj_set_size(cell, CAL_COL_W, CAL_ROW_H);
            lv_obj_set_style_text_color(cell, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(cell, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(cell, "");
        }
    }
```

- [ ] **Step 2: Add the `<`/`>` month-browsing arrows and displayed-month state**

Add near the other calendar statics:

```c
static lv_obj_t *s_cal_arrow_prev;
static lv_obj_t *s_cal_arrow_next;
```

In `create_screen_calendar()`, replace the old single `objects.cal_title` line (`cal_add_label(obj, 8, CAL_TITLE_Y, ...)`) with the month header flanked by two plain-text arrows - this task gives them working focus/action logic with placeholder styling; Task 5 (gated on Task 2's hardware confirmation) replaces the look with dithered round buttons without touching this logic:

```c
    s_cal_arrow_prev = cal_add_label(obj, CAL_COL_X0, CAL_TITLE_Y, &lv_font_montserrat_16,
                                     CAL_ARROW_W, LV_TEXT_ALIGN_CENTER, "<");
    objects.cal_title = cal_add_label(obj, CAL_COL_X0 + CAL_ARROW_W, CAL_TITLE_Y, &lv_font_montserrat_16,
                                      CAL_GRID_W - 2 * CAL_ARROW_W, LV_TEXT_ALIGN_CENTER, "Calendar");
    s_cal_arrow_next = cal_add_label(obj, CAL_COL_X0 + CAL_GRID_W - CAL_ARROW_W, CAL_TITLE_Y, &lv_font_montserrat_16,
                                     CAL_ARROW_W, LV_TEXT_ALIGN_CENTER, ">");
```

Add the new width constant next to the other `CAL_*` defines:

```c
#define CAL_ARROW_W       20
```

- [ ] **Step 3: Rewrite `update_calendar_display()` for per-cell rendering, leading/trailing days, and a separately-tracked displayed month**

Replace the whole function:

```c
void update_calendar_display(int disp_year, int disp_month, const struct tm *today)
{
    static const char *MONTHS[] = { "January","February","March","April","May","June",
                                    "July","August","September","October","November","December" };

    if (!objects.calendar) return;

    lv_label_set_text_fmt(objects.cal_title, "%s %d", MONTHS[disp_month], disp_year);

    struct tm first = { .tm_year = disp_year - 1900, .tm_mon = disp_month, .tm_mday = 1, .tm_isdst = -1 };
    mktime(&first);
    int start_dow = (first.tm_wday + 6) % 7;    /* 0 = Monday */

    /* day 0 of the next month normalises to the last day of this one */
    struct tm last = { .tm_year = disp_year - 1900, .tm_mon = disp_month + 1, .tm_mday = 0, .tm_isdst = -1 };
    mktime(&last);
    int days_in_month = last.tm_mday;

    /* day 0 of *this* month normalises to the last day of the *previous*
       one - the same trick, one month earlier, for numbering leading days. */
    struct tm prev = { .tm_year = disp_year - 1900, .tm_mon = disp_month, .tm_mday = 0, .tm_isdst = -1 };
    mktime(&prev);
    int prev_days_in_month = prev.tm_mday;

    for (int r = 0; r < CAL_ROWS; r++) {
        for (int c = 0; c < 7; c++) {
            int day_offset = r * 7 + c - start_dow + 1;
            int shown_day;
            const lv_font_t *font;

            if (day_offset < 1) {
                shown_day = prev_days_in_month + day_offset;
                font = &lv_font_montserrat_12;
            } else if (day_offset > days_in_month) {
                shown_day = day_offset - days_in_month;
                font = &lv_font_montserrat_12;
            } else {
                shown_day = day_offset;
                font = &lv_font_montserrat_14;
            }

            lv_obj_set_style_text_font(s_cal_days[r][c], font, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_fmt(s_cal_days[r][c], "%d", shown_day);
        }
    }

    /* "Today" only makes sense - and is only shown - when the displayed
       month is the real current one. */
    if (disp_year == today->tm_year + 1900 && disp_month == today->tm_mon) {
        int idx = start_dow + today->tm_mday - 1;
        lv_obj_set_pos(objects.cal_today,
                       CAL_COL_X0 + (idx % 7) * CAL_COL_W + 2,
                       CAL_GRID_Y + (1 + idx / 7) * CAL_ROW_H - 1);
        lv_obj_remove_flag(objects.cal_today, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(objects.cal_today, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text_static(objects.quote, QUOTES[today->tm_yday % NUM_QUOTES]);

    lv_obj_invalidate(objects.calendar);
}
```

Note `today->tm_yday` for the quote-of-the-day: the quote stays tied to the real date, not the browsed-to month, so it doesn't change while browsing.

- [ ] **Step 4: Update `screens.h`**

```c
void update_calendar_display(int disp_year, int disp_month, const struct tm *today);
```

- [ ] **Step 5: Wire month browsing into `clock_task.c`**

Add the displayed-month state near the other calendar statics:

```c
static int s_cal_disp_year = -1;
static int s_cal_disp_month = -1;
```

Change `nav_focus_count`:

```c
static uint8_t nav_focus_count(uint8_t screen)
{
    return screen == NAV_SCREEN_MAIN ? 5 : 2;
}
```

Add a focus-area function for Cal+Clock's two arrows, alongside `main_focus_area`:

```c
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
```

Update `apply_nav_visuals()` to use the right focus-area function per screen:

```c
static void apply_nav_visuals(void)
{
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
```

Now the DETAIL-bounce month-shift handler. In the button-handling block, after `nav_handle()` and inside the existing `if (changed)` branch, add the Cal+Clock case alongside the existing Cal+Clock dial-refresh case. The block currently reads:

```c
                    if (changed) {
                        apply_nav_visuals();
                        if (s_nav.mode == NAV_SCREEN &&
                            s_nav.screen == NAV_SCREEN_CALCLOCK) {
                            update_calendar_display(&now);
                            update_clock_hands(now.tm_hour, now.tm_min, now.tm_sec);
                            s_last_cal_day = now.tm_mday;
                            s_last_cal_mon = now.tm_mon;
                            s_last_cal_sec = now.tm_sec;
                        }
                    }
```

Change to:

```c
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
                                update_calendar_display(s_cal_disp_year, s_cal_disp_month, &now);
                                update_clock_hands(now.tm_hour, now.tm_min, now.tm_sec);
                                s_last_cal_day = now.tm_mday;
                                s_last_cal_mon = now.tm_mon;
                                s_last_cal_sec = now.tm_sec;
                            }
                        }
                    }
```

Update the per-second dirty-check block's calendar-display call (still keyed on the *real* day/month rolling over, which only matters when the displayed month is the current one - `update_calendar_display` itself already handles that via the "today" highlight logic, so this call is safe to make unconditionally when the real date changes):

```c
            } else if (s_nav.screen == NAV_SCREEN_CALCLOCK) {
                if (now.tm_mday != s_last_cal_day || now.tm_mon != s_last_cal_mon) {
                    update_calendar_display(s_cal_disp_year, s_cal_disp_month, &now);
                    s_last_cal_day = now.tm_mday;
                    s_last_cal_mon = now.tm_mon;
                    dirty = true;
                }
```

- [ ] **Step 6: Build and verify on hardware**

Run: `& $py "$env:IDF_PATH\tools\idf.py" build`
Expected: builds clean.

Flash and confirm, on the Cal+Clock screen:
1. The grid shows the current month's days in the larger font, with any leading/trailing days from adjacent months visible in the smaller font (not blank, as before) - unless the current real month happens to start on a Monday and end exactly on a Sunday, in which case there may be no leading/trailing days to see this particular month; if so, use Task 4 Step 5's browsing to move to a month that does have them.
2. Short-press Boot: a focus frame appears around the `<` arrow.
3. Short-press Key: focus moves to the `>` arrow (only two elements, so it alternates).
4. Short-press Boot on the focused `>` arrow (repeatedly): the displayed month advances each time, the grid redraws, and the arrow's focus frame never disappears or flickers into a detail view - it should feel like a direct action.
5. Same for `<`: the month goes backward, including crossing a year boundary (browse from January back to December of the previous year).
6. Hold Key: focus frame disappears, back to plain screen mode; the month stays wherever browsing left it.
7. Switch to Main and back to Cal+Clock (short-press Key twice): the displayed month resets to the real current month, regardless of where browsing left off.
8. The "today" highlight is visible only when the displayed month is the real current month; browse away and confirm it disappears, browse back and confirm it reappears in the right cell.

- [ ] **Step 7: Commit**

```bash
git add firmware/components/ui/screens.h firmware/components/ui/screens.c firmware/components/clock/clock_task.c
git commit -m "feat: per-cell calendar grid with working month browsing

Replaces the one-multi-line-label-per-column grid with one label per
cell so leading/trailing days can render in a smaller font than the
current month - a single lv_label cannot mix fonts. Adds real
focus/action for the < > month arrows: two new Cal+Clock focusables,
reusing the same act-then-bounce-back-to-FOCUS pattern NAV_SETTINGS
already relies on, since nav.c has no notion of a focused element just
performing an action. No nav.c change.

Supersedes the parent spec's per-day focus and day-detail overlay -
see the 2026-08-29 spec amendment for why."
```

---

### Task 5: Dithered header rail, bezel, and month arrows

**Only proceed with this task if Task 2's hardware verification confirmed dithering survives on this panel.** If it did not, stop and get a design decision on an alternative treatment before writing any of this.

**Files:**
- Modify: `firmware/components/ui/overlay.c`
- Modify: `firmware/components/ui/screens.c`

**Interfaces:**
- Consumes: `uint8_t Dither_Threshold(int x, int y, uint8_t grey)` (Task 2).
- Produces: nothing new for later tasks - this is a leaf, purely visual task.

- [ ] **Step 1: Add a dithered-fill helper shared by both files**

This is small enough to duplicate as a `static` helper in each file rather than adding a third shared module - both call sites need it exactly once and the two files already have their own `overlay_*`/`cal_*` helper-naming conventions. In `firmware/components/ui/overlay.c`, add near the top, after the includes:

```c
#include "dither.h"

/* Fills a rectangle with a dithered mid-grey band - the "metal" look. grey
   is 0..255, same convention as Dither_Threshold. */
static void overlay_dither_fill(lv_obj_t *parent, int x, int y, int w, int h, uint8_t grey)
{
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            if (!Dither_Threshold(x + px, y + py, grey)) {
                continue;   /* white pixels are the object's own white background */
            }
            lv_obj_t *dot = lv_obj_create(parent);
            lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_pos(dot, x + px, y + py);
            lv_obj_set_size(dot, 1, 1);
            lv_obj_set_style_pad_all(dot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(dot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(dot, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
}
```

This creates one LVGL object per black dither pixel. For a 400x34 rail at a mid grey (roughly half the pixels), that's on the order of 6800 objects - see Step 2 for why this is not what actually ships.

- [ ] **Step 2: Replace the per-pixel approach with a canvas before it ships**

Step 1's per-object approach is a correctness reference, not the real implementation - it would blow well past the 64 KB LVGL heap budget (Global Constraints) for a single rail. Replace `overlay_dither_fill` with an `lv_canvas` draw instead, matching how `screens.c` already draws the clock dial (`lv_canvas_create` + `lv_canvas_init_layer` + direct pixel writes + `lv_canvas_finish_layer`, see `create_screen_calendar()`'s clock canvas setup). Rewrite:

```c
#include "dither.h"

#define RAIL_BUF_SIZE LV_CANVAS_BUF_SIZE(400, 34, 16, LV_DRAW_BUF_STRIDE_ALIGN)
static uint8_t *s_rail_buf = NULL;

/* Fills a canvas rectangle with a dithered mid-grey band - the "metal"
   look. grey is 0..255, same convention as Dither_Threshold. canvas_x/y
   are the fill's position within the canvas, x/y are its absolute screen
   position (dithering must be computed in screen space so adjacent
   dithered elements tile consistently against each other). */
static void canvas_dither_fill(lv_obj_t *canvas, int canvas_x, int canvas_y,
                               int screen_x, int screen_y, int w, int h, uint8_t grey)
{
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            lv_color_t c = Dither_Threshold(screen_x + px, screen_y + py, grey)
                ? lv_color_hex(0x000000) : lv_color_hex(0xffffff);
            lv_canvas_set_px(canvas, canvas_x + px, canvas_y + py, c, LV_OPA_COVER);
        }
    }
}
```

- [ ] **Step 3: Rebuild the top bar as a rolled metal rail**

In `Overlay_Create()`, replace the plain white `band` object with a canvas-drawn rail. The existing code:

```c
    lv_obj_t *band = lv_obj_create(top);
    lv_obj_remove_flag(band, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(band, 0, 0);
    lv_obj_set_size(band, 400, 34);
    lv_obj_set_style_pad_all(band, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(band, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(band, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(band, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(band, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
```

becomes:

```c
    s_rail_buf = (uint8_t *)heap_caps_malloc(RAIL_BUF_SIZE, MALLOC_CAP_SPIRAM);
    lv_obj_t *band = lv_canvas_create(top);
    if (s_rail_buf) {
        lv_canvas_set_buffer(band, s_rail_buf, 400, 34, LV_COLOR_FORMAT_RGB565);
        lv_canvas_fill_bg(band, lv_color_hex(0xffffff), LV_OPA_COVER);

        /* Dithered band, dark lower lip, bright ridge about a third of the
           way down, dark lower lip again - a "rolled rail" rather than a
           flat bar. Bevel pairs: light edge one side, dark the other, 2
           px total per edge. */
        canvas_dither_fill(band, 0, 0, 0, 0, 400, 30, 160);
        for (int x = 0; x < 400; x++) {
            lv_canvas_set_px(band, x, 10, lv_color_hex(0xffffff), LV_OPA_COVER);   /* bright ridge */
            lv_canvas_set_px(band, x, 29, lv_color_hex(0x000000), LV_OPA_COVER);   /* dark lower lip */
            lv_canvas_set_px(band, x, 30, lv_color_hex(0x000000), LV_OPA_COVER);
        }
    }
    lv_obj_set_pos(band, 0, 0);
    lv_obj_set_size(band, 400, 34);
```

- [ ] **Step 4: Sink the four readings into recessed plaques**

`s_temp`, `s_hum`, `s_date`, `s_battery` are created by `overlay_label()` directly on `top`, sitting on the now-dithered rail - per the hard rule (never dither behind text), each needs a plain white plaque behind it with a 1 px dark top-left edge / light bottom-right edge to read as recessed. Add a helper above `overlay_label`:

```c
/* A recessed white plaque: keeps text off the dithered rail (the hard
   rule - dithering behind text destroys glyphs at this panel's pitch),
   shaded to read as sunken via a dark top-left edge, light bottom-right. */
static void overlay_plaque(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(o, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(o, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(o, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(o, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
}
```

Call it once per reading, sized a few px larger than each label's existing bounds, before creating the labels themselves. The existing `overlay_label` calls (`Overlay_Create()`) give each reading's x position; give each plaque a 4 px margin on every side as a starting bound, refined visually in Step 6's hardware check below like every other measurement in this task:

```c
    overlay_plaque(top, 0, 1, 84, 22);
    s_temp    = overlay_label(top, 2, 3, 0, LV_TEXT_ALIGN_LEFT, "0.0");

    overlay_plaque(top, 87, 1, 60, 22);
    s_hum     = overlay_label(top, 89, 3, 0, LV_TEXT_ALIGN_LEFT, "0%");

    overlay_plaque(top, 161, 1, 100, 22);
    s_date    = overlay_label(top, 163, 3, 0, LV_TEXT_ALIGN_LEFT, "01.01.2000");

    overlay_plaque(top, 330, 1, 62, 22);
    s_battery = overlay_label(top, 332, 3, 58, LV_TEXT_ALIGN_CENTER, "0.00");
```

- [ ] **Step 5: Dither the dial's bezel and give the month arrows their real look**

In `firmware/components/ui/screens.c`, add `#include "dither.h"`. The dial already renders via `lv_canvas` (`update_clock_hands`/`draw_clock_face`) - add a bezel ring drawn once, in `create_screen_calendar()` right after the canvas is created and before the first `update_clock_hands(0, 0, 0)` call:

```c
    if (clock_buf) {
        lv_obj_t *canvas = lv_canvas_create(obj);
        objects.clock_canvas = canvas;
        lv_canvas_set_buffer(canvas, clock_buf, CLOCK_SIZE, CLOCK_SIZE, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(canvas, CAL_CLOCK_X, CAL_CLOCK_Y);
        update_clock_hands(0, 0, 0);
    }
```

The dithered bezel itself is drawn as part of `draw_clock_face()` (Task 3), as a ring just outside `CLOCK_RADIUS`, using `Dither_Threshold` against the canvas's own `lv_layer_t` via `lv_draw_rect` isn't viable for per-pixel dithering - use `lv_canvas_set_px` directly instead, which requires switching that ring's drawing to run before `lv_canvas_init_layer`/after `lv_canvas_finish_layer` in `update_clock_hands`, since `lv_draw_*` calls need the layer but `lv_canvas_set_px` does not. Add a `draw_clock_bezel(lv_obj_t *canvas)` function called once (not every `update_clock_hands`, since the bezel never changes):

```c
static void draw_clock_bezel(lv_obj_t *canvas)
{
    for (int y = 0; y < CLOCK_SIZE; y++) {
        for (int x = 0; x < CLOCK_SIZE; x++) {
            double dx = x - CLOCK_CENTER, dy = y - CLOCK_CENTER;
            double r = sqrt(dx * dx + dy * dy);
            if (r < CLOCK_RADIUS + 2 || r > CLOCK_RADIUS + 10) continue;
            /* Screen-space coordinates for consistent tiling with the rail
               and any other dithered element - the canvas sits at
               (CAL_CLOCK_X, CAL_CLOCK_Y). */
            uint8_t on = Dither_Threshold(CAL_CLOCK_X + x, CAL_CLOCK_Y + y, 150);
            if (on) {
                lv_canvas_set_px(canvas, x, y, lv_color_hex(0x000000), LV_OPA_COVER);
            }
        }
    }
}
```

Call it once, in `create_screen_calendar()`, right after the canvas is created:

```c
        update_clock_hands(0, 0, 0);
        draw_clock_bezel(canvas);
```

`update_clock_hands()` calls `lv_canvas_fill_bg()` every redraw, which erases the bezel - move the bezel draw to happen once, and change `update_clock_hands` so it no longer erases the whole canvas, only redraws the face/hands region inside the bezel. This requires care: `lv_canvas_fill_bg` covering the full `CLOCK_SIZE` square would wipe `draw_clock_bezel`'s ring on the very next per-second redraw. Change the fill to cover only the disc `draw_clock_face` and the hands actually touch (radius `CLOCK_RADIUS`, not the full canvas), by filling a centered square just larger than `CLOCK_RADIUS` instead of the whole canvas:

```c
    lv_area_t face_area = {
        CLOCK_CENTER - CLOCK_RADIUS - 1, CLOCK_CENTER - CLOCK_RADIUS - 1,
        CLOCK_CENTER + CLOCK_RADIUS + 1, CLOCK_CENTER + CLOCK_RADIUS + 1,
    };
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    lv_draw_rect_dsc_t bg_dsc;
    lv_draw_rect_dsc_init(&bg_dsc);
    bg_dsc.bg_color = lv_color_hex(0xffffff);
    bg_dsc.bg_opa = LV_OPA_COVER;
    lv_draw_rect(&layer, &bg_dsc, &face_area);
```

in place of the current `lv_canvas_fill_bg(canvas, lv_color_hex(0xFFFFFF), LV_OPA_COVER);` / `lv_canvas_init_layer(canvas, &layer);` pair at the top of `update_clock_hands` (both statements replaced by the block above).

For the month arrows: replace the plain `"<"`/`">"` text labels from Task 4 Step 2 with round dithered buttons. Keep `s_cal_arrow_prev`/`s_cal_arrow_next` as the actual focusable/labelled objects (their position and size are what `calclock_focus_area()` already targets, so leave that function alone), but change their creation from `cal_add_label` to a small canvas each, shaded via hemisphere-normal-against-light-vector rather than a flat gradient: for each pixel inside the circle, compute the surface normal as if the circle were a hemisphere bulging toward the viewer, dot it against a fixed light direction (e.g. upper-left, `(-0.5, -0.5, 0.7)` normalized), and threshold the resulting brightness through `Dither_Threshold` the same way as the rail. This is a new, self-contained function:

```c
static void draw_arrow_button(lv_obj_t *canvas, int screen_x, int screen_y, int size, const char *glyph)
{
    const int r = size / 2;
    const double lx = -0.5, ly = -0.5, lz = 0.7;   /* light from upper-left */
    const double llen = sqrt(lx * lx + ly * ly + lz * lz);

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            double dx = (x - r) / (double)r, dy = (y - r) / (double)r;
            double d2 = dx * dx + dy * dy;
            if (d2 > 1.0) continue;   /* outside the circle */
            double dz = sqrt(1.0 - d2);   /* hemisphere bulging toward the viewer */
            double dot = (dx * lx + dy * ly + dz * lz) / llen;   /* normal is (dx,dy,dz), already unit length */
            uint8_t grey = (uint8_t)((dot * 0.5 + 0.5) * 255.0);
            lv_color_t c = Dither_Threshold(screen_x + x, screen_y + y, grey)
                ? lv_color_hex(0x000000) : lv_color_hex(0xffffff);
            lv_canvas_set_px(canvas, x, y, c, LV_OPA_COVER);
        }
    }

    lv_obj_t *l = lv_label_create(canvas);
    lv_obj_center(l);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text_static(l, glyph);
}
```

In `create_screen_calendar()`, replace the `cal_add_label` calls for the two arrows with canvases of size `CAL_ARROW_W` (20 px, so `r=10`) at the same positions `calclock_focus_area()` expects (screen x 6/196, y 40):

```c
    static uint8_t s_arrow_prev_buf[LV_CANVAS_BUF_SIZE(CAL_ARROW_W, CAL_ARROW_W, 16, LV_DRAW_BUF_STRIDE_ALIGN)];
    static uint8_t s_arrow_next_buf[LV_CANVAS_BUF_SIZE(CAL_ARROW_W, CAL_ARROW_W, 16, LV_DRAW_BUF_STRIDE_ALIGN)];

    s_cal_arrow_prev = lv_canvas_create(obj);
    lv_canvas_set_buffer(s_cal_arrow_prev, s_arrow_prev_buf, CAL_ARROW_W, CAL_ARROW_W, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_cal_arrow_prev, CAL_COL_X0, CAL_TITLE_Y);
    draw_arrow_button(s_cal_arrow_prev, CAL_COL_X0, CAL_TITLE_Y, CAL_ARROW_W, "<");

    objects.cal_title = cal_add_label(obj, CAL_COL_X0 + CAL_ARROW_W, CAL_TITLE_Y, &lv_font_montserrat_16,
                                      CAL_GRID_W - 2 * CAL_ARROW_W, LV_TEXT_ALIGN_CENTER, "Calendar");

    s_cal_arrow_next = lv_canvas_create(obj);
    lv_canvas_set_buffer(s_cal_arrow_next, s_arrow_next_buf, CAL_ARROW_W, CAL_ARROW_W, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_cal_arrow_next, CAL_COL_X0 + CAL_GRID_W - CAL_ARROW_W, CAL_TITLE_Y);
    draw_arrow_button(s_cal_arrow_next, CAL_COL_X0 + CAL_GRID_W - CAL_ARROW_W, CAL_TITLE_Y, CAL_ARROW_W, ">");
```

`calclock_focus_area()` in `clock_task.c` (Task 4) already targets these exact bounds (x 6..25 / 196..215, y 40..58) - no change needed there.

- [ ] **Step 6: Build and verify on hardware**

Run: `& $py "$env:IDF_PATH\tools\idf.py" build`
Expected: builds clean.

Flash and confirm:
1. The shared top bar (visible on both Main and Cal+Clock) now shows a dithered metal band with a bright ridge and dark lower lip, not a flat white bar.
2. The four readings (temp, humidity, date, battery) are legible, sitting on plain white recessed plaques - not dithered behind, per the hard rule.
3. The dial's bezel (the ring just outside the tick marks) is dithered; the tick marks, hands, and hub are still crisp pure black, not affected by the bezel dither.
4. The `<`/`>` month buttons look like shaded round buttons (bulging toward the viewer, light from the upper-left), not flat circles or plain text - and still work exactly as before (focus frame, month browsing) since Task 4's logic was untouched.
5. Nothing from Task 3's dial redraw or Task 4's grid/browsing regressed.

- [ ] **Step 7: Commit**

```bash
git add firmware/components/ui/overlay.c firmware/components/ui/screens.c
git commit -m "feat: dithered metal header rail, dial bezel, and month buttons

Bayer-dithered (Dither_Threshold, confirmed on hardware in Task 2)
rolled rail for the shared top bar, with the four readings sunk into
recessed white plaques so their text stays off the dither per the hard
rule. Dial bezel dithered the same way. Month arrows redrawn as
hemisphere-shaded round buttons instead of plain text - their focus/
action logic from Task 4 is unchanged, only their paint."
```

---

### Task 6: Geometry test coverage

**Files:**
- Create: `firmware/tools/../../tools/verify_calendar_layout.py` (i.e. `tools/verify_calendar_layout.py` at the repo root, alongside `tools/verify_nav.py`)

**Interfaces:**
- Consumes: nothing (host-side, standalone).
- Produces: a script exit code (0 = all checks pass) and a printed summary, run manually - same convention as `tools/verify_nav.py`.

- [ ] **Step 1: Write the font-metrics parser**

LVGL's compiled font sources store each glyph's advance width in a `glyph_dsc[]` array, indexed via a `cmaps[]` range table - `.adv_w` is in 1/16 px fixed point (`(adv_w + 8) >> 4` gives the whole-pixel width, per `lv_font_fmt_txt.c`'s glyph lookup). Confirmed by inspecting `firmware/managed_components/lvgl__lvgl/src/font/lv_font_montserrat_12.c` and `_14.c`: their `cmaps[]` both start with `.range_start = 32, .range_length = 95, .glyph_id_start = 1` (the printable ASCII range), so `glyph_dsc[codepoint - 32 + 1]` is the entry for `codepoint`.

```python
#!/usr/bin/env python3
"""Verifies Cal+Clock's layout geometry against real glyph widths parsed
from LVGL's compiled font sources, instead of eyeballing it - the method
that caught the column-alignment and quote-wrapping problems fixed in
ccffbf5. See docs/superpowers/specs/2026-08-29-calclock-vista-redraw-design.md.
"""
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
FONT_DIR = REPO_ROOT / "firmware" / "managed_components" / "lvgl__lvgl" / "src" / "font"

GLYPH_RE = re.compile(r"\.adv_w\s*=\s*(-?\d+)")
CMAP_RANGE_RE = re.compile(
    r"\.range_start\s*=\s*(\d+),\s*\.range_length\s*=\s*(\d+),\s*\.glyph_id_start\s*=\s*(\d+)"
)


def load_font_widths(font_file: str) -> dict[str, int]:
    """Returns {char: pixel_width} for every printable ASCII char this
    font defines, parsed from its compiled glyph_dsc/cmaps tables."""
    text = (FONT_DIR / font_file).read_text(encoding="utf-8")

    glyph_section = re.search(
        r"glyph_dsc\[\]\s*=\s*\{(.*?)\n\};", text, re.DOTALL
    )
    if not glyph_section:
        raise ValueError(f"{font_file}: could not find glyph_dsc[] table")
    adv_widths = [int(m.group(1)) for m in GLYPH_RE.finditer(glyph_section.group(1))]

    cmap_section = re.search(r"cmaps\[\]\s*=\s*\{(.*?)\n\};", text, re.DOTALL)
    if not cmap_section:
        raise ValueError(f"{font_file}: could not find cmaps[] table")
    ranges = [
        (int(m.group(1)), int(m.group(2)), int(m.group(3)))
        for m in CMAP_RANGE_RE.finditer(cmap_section.group(1))
    ]

    widths: dict[str, int] = {}
    for range_start, range_length, glyph_id_start in ranges:
        for offset in range(range_length):
            codepoint = range_start + offset
            if codepoint > 126:   # only printable ASCII matters here
                continue
            glyph_id = glyph_id_start + offset
            if glyph_id >= len(adv_widths):
                continue
            widths[chr(codepoint)] = (adv_widths[glyph_id] + 8) >> 4
    return widths


def text_width(widths: dict[str, int], text: str) -> int:
    return sum(widths.get(ch, widths[" "]) for ch in text)
```

- [ ] **Step 2: Write the checks**

```python
def check_calendar_grid(errors: list[str]) -> None:
    """Every day-number cell must fit its column at both font sizes used -
    montserrat_14 for the current month, montserrat_12 for leading/
    trailing days (see update_calendar_display() in screens.c)."""
    w14 = load_font_widths("lv_font_montserrat_14.c")
    w12 = load_font_widths("lv_font_montserrat_12.c")

    CAL_COL_W = 30
    widest_two_digit = max(text_width(w14, f"{d:d}") for d in range(10, 32))
    if widest_two_digit > CAL_COL_W:
        errors.append(
            f"montserrat_14 two-digit day ({widest_two_digit}px) does not fit "
            f"CAL_COL_W ({CAL_COL_W}px)"
        )

    widest_header = max(text_width(w14, dow) for dow in ["Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"])
    if widest_header > CAL_COL_W:
        errors.append(
            f"widest weekday header ({widest_header}px) does not fit "
            f"CAL_COL_W ({CAL_COL_W}px)"
        )

    widest_leading_trailing = max(text_width(w12, f"{d:d}") for d in range(1, 32))
    if widest_leading_trailing > CAL_COL_W:
        errors.append(
            f"montserrat_12 leading/trailing day ({widest_leading_trailing}px) "
            f"does not fit CAL_COL_W ({CAL_COL_W}px)"
        )


def check_calendar_title(errors: list[str]) -> None:
    """The month/year header must fit between the two CAL_ARROW_W-wide
    arrows, for the longest month name at the widest plausible year."""
    w16 = load_font_widths("lv_font_montserrat_16.c") if (FONT_DIR / "lv_font_montserrat_16.c").exists() else None
    if w16 is None:
        errors.append(
            "lv_font_montserrat_16.c not found under managed_components - "
            "cannot verify the month/year header width; check CONFIG_LV_FONT_MONTSERRAT_16 "
            "is enabled and the font source is actually compiled in"
        )
        return

    CAL_COL_X0 = 6
    CAL_COL_W = 30
    CAL_ARROW_W = 20
    CAL_GRID_W = CAL_COL_W * 7
    available = CAL_GRID_W - 2 * CAL_ARROW_W

    longest_month = "September"   # longest of the twelve English month names
    widest_title = text_width(w16, f"{longest_month} 2026")
    if widest_title > available:
        errors.append(
            f"'{longest_month} 2026' at montserrat_16 ({widest_title}px) does not fit "
            f"the {available}px gap between the month arrows"
        )


def check_dial_geometry(errors: list[str]) -> None:
    """The 60 minor ticks must be spaced widely enough at their radius for
    a 2px stroke to survive the panel's 1-bit threshold - this is exactly
    the arithmetic that was done by hand to pick r=58 for Task 3; this
    check makes sure that reasoning was right and stays right if the
    radius or tick count ever changes. The empirical floor (1px does not
    survive, 2px does at ~6.3px spacing) comes from Task 3's own testing
    against the real panel - there is no general formula for this, only
    that one confirmed data point, so this check is a regression guard
    against the specific geometry already verified on hardware, not an
    independent derivation of what would survive."""
    import math

    MINOR_TICK_COUNT = 60
    MINOR_TICK_RADIUS = 58
    MINOR_TICK_WIDTH = 2
    CONFIRMED_SURVIVING_SPACING_PX = 6.3   # 60 ticks at r=58, confirmed on hardware in Task 3

    spacing = 2 * math.pi * MINOR_TICK_RADIUS / MINOR_TICK_COUNT
    if spacing < CONFIRMED_SURVIVING_SPACING_PX - 0.1:   # small tolerance for float rounding
        errors.append(
            f"minor tick spacing at r={MINOR_TICK_RADIUS} for {MINOR_TICK_COUNT} ticks "
            f"is {spacing:.2f}px, narrower than the {CONFIRMED_SURVIVING_SPACING_PX}px "
            f"already confirmed on hardware to survive a {MINOR_TICK_WIDTH}px stroke - "
            f"this needs a fresh hardware check, not just this arithmetic, before shipping"
        )


def main() -> int:
    errors: list[str] = []
    check_calendar_grid(errors)
    check_calendar_title(errors)
    check_dial_geometry(errors)

    if errors:
        for e in errors:
            print(f"FAIL: {e}")
        print(f"\n{len(errors)} geometry check(s) failed")
        return 1

    print("all calendar geometry checks pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 3: Run it**

Run: `python tools/verify_calendar_layout.py`

Expected: `all calendar geometry checks pass`, exit code 0. If it fails, the failure message names the exact constant (`CAL_COL_W`, `CAL_ARROW_W`) and font that don't fit together - fix the constant in `screens.c` (from Task 3/4/5) or the string being measured, not the check itself, unless the check's own arithmetic is wrong.

If `lv_font_montserrat_16.c` genuinely isn't present (font not compiled in), check `firmware/sdkconfig` for `CONFIG_LV_FONT_MONTSERRAT_16` - this project already uses `lv_font_montserrat_16` in `screens.c` (`cal_title`), so it must be enabled; if the file is missing anyway, LVGL may vendor 16 pt differently (e.g. only via a `_compressed` variant) - locate the actual compiled source under `FONT_DIR` and adjust the glob/path in Step 2 rather than skipping the check.

- [ ] **Step 4: Commit**

```bash
git add tools/verify_calendar_layout.py
git commit -m "test: verify Cal+Clock geometry against real font metrics

Parses glyph advance widths directly from LVGL's compiled font .c
sources (glyph_dsc[]/cmaps[], 1/16px fixed point) rather than eyeballing
column widths - the method that caught the alignment problems fixed in
ccffbf5, now automated and covering the per-cell grid and the new
month/year header from this milestone."
```

---

## Definition of done

- [ ] Tasks 1-6 committed (Task 5 only if Task 2's hardware check passed - otherwise its scope is replaced by whatever design decision follows from that finding).
- [ ] `python tools/verify_calendar_layout.py` prints `all calendar geometry checks pass`.
- [ ] `python tools/verify_nav.py` still prints `all invariants hold` (nothing here changes `nav.c`, but confirm no accidental edit crept in).
- [ ] All hardware verification steps in Tasks 1, 3, 4, and 5 pass on the actual device.
- [ ] The second hand visibly sweeps once per second on Cal+Clock.
- [ ] Month browsing works forward and backward, including across a year boundary, and resets to the current month on every fresh entry to the screen.
- [ ] `TEMP-visual-design-notes.md` no longer exists (already deleted when the design spec was written) and its content is fully represented in `docs/superpowers/specs/2026-08-29-calclock-vista-redraw-design.md`.
