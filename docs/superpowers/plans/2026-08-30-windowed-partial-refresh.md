# Windowed Partial Refresh Implementation Plan

> **Outcome (2026-08-30):** implemented, hardware-tested across three policy
> variants, and reverted - windowing never reliably reduced the roll this
> plan was written to fix. The actual root cause (a settling delay removed
> by an earlier, unrelated fix) was found and corrected separately; see the
> correction note at the top of this plan's spec
> (`docs/superpowers/specs/2026-08-30-windowed-partial-refresh-design.md`)
> and commit `04e0c4e`. Kept as a historical record only.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the ST7305 panel's unconditional full-frame SPI send in the LVGL flush path with a windowed send sized to the actual dirty area, so small/localized UI changes settle visibly faster than the ~50px/sec full-panel roll.

**Architecture:** A pure, unit-testable `RLCD_ComputeWindow()` function converts a screen-space rectangle into hardware-valid CASET/RASET bounds and a matching payload length. `RLCD_DisplayWindow()` extracts the corresponding bytes out of the existing `DispBuffer` into a new persistent `WindowBuffer` and sends them windowed. `RLCD_DisplayAuto()` picks between the windowed path and the existing full-panel `RLCD_Display()` based on how much of the panel a given update covers. `Lvgl_FlushCallback` tracks the union of everything painted in one refresh pass and hands it to `RLCD_DisplayAuto()` exactly once per batch, replacing its current unconditional `RLCD_Display()` call.

**Tech Stack:** ESP-IDF 5.5.5, LVGL 9.5, C++ (DisplayPort class), this project's existing `selftest`-based test harness (device-boot-time assertions logged over UART, no host-side test runner).

**Spec:** `docs/superpowers/specs/2026-08-30-windowed-partial-refresh-design.md`

## Global Constraints

- Panel is 400×300, 1-bit-per-pixel. `DisplayLen` is 15000 bytes for the full panel.
- CASET (0x2A) = vertical axis, 12px/unit, valid range 18-42 (25 units), **decreases as screen-Y decreases** (CASET 18-20 = top of screen, CASET 40-42 = bottom) - hardware-confirmed, do not re-derive.
- RASET (0x2B) = horizontal axis, 2px/unit, valid range 0-199 (200 units) - hardware-confirmed.
- Payload length for any window = `(caset_xe - caset_xs + 1) * (raset_ye - raset_ys + 1) * 3` bytes. The ×3 factor is not optional - an undersized RAMWR payload does not fail gracefully, it leaves visible garbage on the panel (hardware-confirmed during the spike).
- `DispBuffer` layout (from `InitLandscapeLUT()`, unchanged by this plan): `byte_x`-major (200 groups of 2 physical columns), `block_y`-minor (75 groups of 4 rows, Y-inverted via `inv_y = height_-1-y`) - for one fixed `byte_x`, a `block_y` sub-range is a contiguous run.
- Any buffer handed to `RLCD_Sendbuffera()` must stay alive until the transfer it queues completes - it queues an async DMA send and returns immediately. Never free (or let a caller believe it's safe to reuse) a buffer right after calling it without an intervening `RLCD_WaitTransferDone()`.
- This project's test suites (`Nav_RunTests()`, `ClockTime_RunTests()`, and this plan's new `DisplayBsp_RunTests()`) are plain functions gated by `#if CONFIG_CLOCK_SELFTEST`, called unconditionally from `main.cpp`'s `app_main()` between `Selftest_Begin()`/`Selftest_End()`. `CONFIG_CLOCK_SELFTEST` is always enabled in this project's checked-in `sdkconfig` - treat it as always-on, matching every existing suite.

---

### Task 1: `RLCD_ComputeWindow()` and its unit test

**Files:**
- Modify: `components/port_bsp/display_bsp.h`
- Modify: `components/port_bsp/display_bsp.cpp`
- Create: `components/port_bsp/display_bsp_test.cpp`
- Modify: `components/port_bsp/CMakeLists.txt`
- Modify: `main/main.cpp`

**Interfaces:**
- Produces: `struct RlcdWindow { uint8_t caset_xs, caset_xe, raset_ys, raset_ye; int len; };` and `RlcdWindow RLCD_ComputeWindow(int width, int height, int x1, int y1, int x2, int y2);` - a free function (not a `DisplayPort` member), so it needs no hardware instance to unit-test. `void DisplayBsp_RunTests(void);`.
- Consumes: nothing from earlier tasks (this is the first task).

`display_bsp.h` currently declares no free functions - only the `DisplayPort` class (see the file's full current content: it's a single `class DisplayPort { ... };` after the includes and the `ColorSelection` enum). `RlcdWindow` and `RLCD_ComputeWindow()` go **before** the class, so later tasks' class methods can call it.

- [ ] **Step 1: Add `RlcdWindow` and `RLCD_ComputeWindow()`'s declaration to `display_bsp.h`**

In `components/port_bsp/display_bsp.h`, right after the existing `enum ColorSelection { ... };` block and before `class DisplayPort {`, insert:

```cpp
/* A windowed CASET/RASET/payload-length triple, computed by
   RLCD_ComputeWindow() from a screen-space rectangle. Both the SPI command
   bytes and the payload-extraction loop that use this are driven from one
   RlcdWindow value so they can never compute mismatched sizes - an
   undersized RAMWR payload does not fail gracefully on this panel, it
   leaves visible garbage (hardware-confirmed). See
   docs/superpowers/specs/2026-08-30-windowed-partial-refresh-design.md. */
struct RlcdWindow {
    uint8_t caset_xs;
    uint8_t caset_xe;
    uint8_t raset_ys;
    uint8_t raset_ye;
    int     len;
};

/* Pure function (no DisplayPort instance needed) so it's directly unit
   testable - see display_bsp_test.cpp. width/height are the panel's pixel
   dimensions (400/300 on this hardware). x1/y1/x2/y2 is an inclusive
   screen-space rectangle; out-of-range values are clamped internally to
   [0,width-1]x[0,height-1]. Always expands the rect to the nearest valid
   CASET/RASET-addressable window (12px vertical granularity, 2px
   horizontal), never shrinks it - so a computed window may cover a few
   extra pixels beyond what was asked for, never fewer. */
RlcdWindow RLCD_ComputeWindow(int width, int height, int x1, int y1, int x2, int y2);

void DisplayBsp_RunTests(void);
```

- [ ] **Step 2: Implement `RLCD_ComputeWindow()` in `display_bsp.cpp`**

In `components/port_bsp/display_bsp.cpp`, right after the `#include` block (after `#include "display_bsp.h"`, before `DisplayPort::DisplayPort(...)`), insert:

```cpp
RlcdWindow RLCD_ComputeWindow(int width, int height, int x1, int y1, int x2, int y2)
{
    /* Clamp to panel bounds first - defensive against any caller passing
       out-of-range LVGL coordinates. */
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > width - 1)  x2 = width - 1;
    if (y2 > height - 1) y2 = height - 1;

    /* X (RASET): 2px/unit, no inversion. */
    uint8_t rs = (uint8_t)(x1 >> 1);
    uint8_t re = (uint8_t)(x2 >> 1);

    /* Y (CASET): 12px/unit (3 InitLandscapeLUT() block_y groups of 4 rows
       each), Y-inverted via inv_y = height-1-y, and CASET *decreases* as
       screen-Y increases (hardware-confirmed: CASET's low end measured at
       the top of the screen, where inv_y/block_y is largest). */
    int inv_y1 = height - 1 - y1;
    int inv_y2 = height - 1 - y2;
    int by_a = inv_y1 >> 2;
    int by_b = inv_y2 >> 2;
    int by_lo = (by_a < by_b) ? by_a : by_b;
    int by_hi = (by_a > by_b) ? by_a : by_b;
    int g_lo = by_lo / 3;   /* integer division already expands to the
                                enclosing 12px-aligned band */
    int g_hi = by_hi / 3;

    RlcdWindow w;
    w.caset_xs = (uint8_t)(42 - g_hi);   /* larger block_y group -> smaller CASET */
    w.caset_xe = (uint8_t)(42 - g_lo);   /* smaller block_y group -> larger CASET */
    w.raset_ys = rs;
    w.raset_ye = re;
    w.len = (int)(w.caset_xe - w.caset_xs + 1) * (int)(w.raset_ye - w.raset_ys + 1) * 3;
    return w;
}
```

- [ ] **Step 3: Write `display_bsp_test.cpp`**

Create `components/port_bsp/display_bsp_test.cpp`:

```cpp
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

#endif
```

- [ ] **Step 4: Register the new test file and its `selftest` dependency**

In `components/port_bsp/CMakeLists.txt`, replace the whole file with:

```cmake
idf_component_register(
    SRCS 
    "display_bsp.cpp" 
    "display_bsp_test.cpp"
    PRIV_REQUIRES 
    driver
    esp_timer 
    selftest
    REQUIRES
    esp_lcd
    INCLUDE_DIRS 
    "./")
```

(Adds `"display_bsp_test.cpp"` to `SRCS` and `selftest` to `PRIV_REQUIRES` - both required for the new test file to build and link against `SELFTEST_CHECK_INT`.)

- [ ] **Step 5: Wire `DisplayBsp_RunTests()` into the boot self-test sequence**

In `main/main.cpp`, find:

```cpp
    Selftest_Begin();
    ClockTime_RunTests();
    Nav_RunTests();
    Selftest_End();
```

Replace with:

```cpp
    Selftest_Begin();
    ClockTime_RunTests();
    Nav_RunTests();
    DisplayBsp_RunTests();
    Selftest_End();
```

(`main.cpp` already `#include`s `"display_bsp.h"` - no new include needed.)

- [ ] **Step 6: Build**

Run: `idf.py build` (from the `firmware/` directory, with the ESP-IDF environment active - see this project's established build prelude).
Expected: builds with no errors or warnings from the new code. This is the task's primary gate - the actual `SELFTEST_CHECK_INT` assertions only execute on real hardware at boot, which happens as part of Task 4's hardware verification (bundling every test suite's results into one flash-and-read-the-log cycle, matching this project's established pattern for `Nav_RunTests()`/`ClockTime_RunTests()`).

- [ ] **Step 7: Commit**

```bash
git add components/port_bsp/display_bsp.h components/port_bsp/display_bsp.cpp components/port_bsp/display_bsp_test.cpp components/port_bsp/CMakeLists.txt main/main.cpp
git commit -m "feat: add RLCD_ComputeWindow() for windowed-refresh addressing math"
```

---

### Task 2: `RLCD_DisplayWindow()`

**Files:**
- Modify: `components/port_bsp/display_bsp.h`
- Modify: `components/port_bsp/display_bsp.cpp`

**Interfaces:**
- Consumes: `RlcdWindow RLCD_ComputeWindow(int width, int height, int x1, int y1, int x2, int y2)` (Task 1).
- Produces: `void DisplayPort::RLCD_DisplayWindow(int x1, int y1, int x2, int y2);`.

- [ ] **Step 1: Add a persistent `WindowBuffer` member and the method declaration**

In `components/port_bsp/display_bsp.h`, in the `private:` section, right after the existing `uint8_t *DispBuffer = NULL;` line, add:

```cpp
    uint8_t            *WindowBuffer = NULL;
```

In the `public:` section, right after the existing `void RLCD_Display();` line, add:

```cpp
    void RLCD_DisplayWindow(int x1, int y1, int x2, int y2);
```

- [ ] **Step 2: Allocate `WindowBuffer` in the constructor**

In `components/port_bsp/display_bsp.cpp`, find the constructor's existing `DispBuffer` allocation:

```cpp
    DisplayLen                = transfer >> 3;
    DispBuffer                = (uint8_t *) heap_caps_malloc(DisplayLen, MALLOC_CAP_SPIRAM);
    assert(DispBuffer);
```

Right after `assert(DispBuffer);`, add:

```cpp
    /* Persistent scratch buffer for RLCD_DisplayWindow(), sized to the
       full-panel worst case. Reused across calls rather than malloc/free
       per send - RLCD_Sendbuffera() queues an async DMA transfer and
       returns immediately, so freeing a buffer right after queuing it
       would race the still-in-flight send (this is the exact bug the
       windowed-refresh spike's own diagnostic code hit). Safe to reuse
       for the same reason DispBuffer itself already is: the next batch's
       RLCD_WaitTransferDone() (called before that batch's first pixel
       write) guarantees any previous transfer has completed before either
       buffer is touched again. */
    WindowBuffer               = (uint8_t *) heap_caps_malloc(DisplayLen, MALLOC_CAP_SPIRAM);
    assert(WindowBuffer);
```

- [ ] **Step 3: Implement `RLCD_DisplayWindow()`**

In `components/port_bsp/display_bsp.cpp`, right after the existing `DisplayPort::RLCD_Display()` method (the one that sends the full `DispBuffer` with the hardcoded `0x12`-`0x2A`/`0x00`-`0xC7` window), add:

```cpp
/* Sends a windowed sub-rectangle instead of the full panel. x1/y1/x2/y2 are
   inclusive screen-space coordinates; RLCD_ComputeWindow() rounds them to a
   valid CASET/RASET window and the matching payload length - see that
   function for the addressing math. Writes into the persistent
   WindowBuffer (not a fresh allocation - see the constructor) so it needs
   no wait of its own: like RLCD_Display(), it queues an async send and
   relies on the caller's existing wait/send discipline (Task 1's
   RLCD_WaitTransferDone(), called at the start of the next batch) before
   either buffer is touched again. */
void DisplayPort::RLCD_DisplayWindow(int x1, int y1, int x2, int y2) {
    RlcdWindow w = RLCD_ComputeWindow(width_, height_, x1, y1, x2, y2);

    int H4 = height_ >> 2;
    int by_start = (42 - w.caset_xe) * 3;
    int run_len  = (w.caset_xe - w.caset_xs + 1) * 3;
    int cursor = 0;
    for (int bx = w.raset_ys; bx <= w.raset_ye; bx++) {
        memcpy(WindowBuffer + cursor, &DispBuffer[bx * H4 + by_start], run_len);
        cursor += run_len;
    }

    RLCD_SendCommand(0x2A);
    RLCD_SendData(w.caset_xs);
    RLCD_SendData(w.caset_xe);

    RLCD_SendCommand(0x2B);
    RLCD_SendData(w.raset_ys);
    RLCD_SendData(w.raset_ye);

    RLCD_SendCommand(0x2c);
    RLCD_Sendbuffera(WindowBuffer, w.len);
}
```

- [ ] **Step 4: Build**

Run: `idf.py build`.
Expected: builds cleanly. `RLCD_DisplayWindow()` is not called from anywhere yet (that's Task 3/4) - this task's gate is that it compiles correctly against `RLCD_ComputeWindow()`'s real signature and `DispBuffer`'s real layout.

- [ ] **Step 5: Commit**

```bash
git add components/port_bsp/display_bsp.h components/port_bsp/display_bsp.cpp
git commit -m "feat: add RLCD_DisplayWindow() for sending a windowed sub-rectangle"
```

---

### Task 3: `RLCD_DisplayAuto()` and the fallback threshold

**Files:**
- Modify: `components/port_bsp/display_bsp.h`
- Modify: `components/port_bsp/display_bsp.cpp`

**Interfaces:**
- Consumes: `void DisplayPort::RLCD_DisplayWindow(int x1, int y1, int x2, int y2)` (Task 2), the existing `void DisplayPort::RLCD_Display()`, and `RlcdWindow RLCD_ComputeWindow(...)` (Task 1).
- Produces: `void DisplayPort::RLCD_DisplayAuto(int x1, int y1, int x2, int y2);` - the single entry point Task 4's `Lvgl_FlushCallback` will call once per batch.

- [ ] **Step 1: Declare `RLCD_DisplayAuto()`**

In `components/port_bsp/display_bsp.h`, in the `public:` section, right after the `RLCD_DisplayWindow` declaration added in Task 2, add:

```cpp
    void RLCD_DisplayAuto(int x1, int y1, int x2, int y2);
```

- [ ] **Step 2: Implement `RLCD_DisplayAuto()`**

In `components/port_bsp/display_bsp.cpp`, right after `RLCD_DisplayWindow()` (added in Task 2), add:

```cpp
/* Picks between a windowed send and the existing full-panel RLCD_Display(),
   based on how much of the panel's height the rounded window would cover.
   Windowing costs a small extraction loop and a slightly more complex
   command sequence than a plain full send; that's only worth paying when
   it actually shrinks the area the panel has to physically settle. Above
   ~80% height coverage (20 of the 25 total CASET units), the two paths
   would settle at roughly the same visible speed, so this falls back to
   the simpler, already-proven full send rather than window a change that's
   nearly the whole screen anyway. */
void DisplayPort::RLCD_DisplayAuto(int x1, int y1, int x2, int y2) {
    RlcdWindow w = RLCD_ComputeWindow(width_, height_, x1, y1, x2, y2);

    int caset_units = (int)(w.caset_xe - w.caset_xs + 1);
    if (caset_units >= 20) {
        RLCD_Display();
    } else {
        RLCD_DisplayWindow(x1, y1, x2, y2);
    }
}
```

(`RLCD_DisplayAuto()` calls `RLCD_ComputeWindow()` once to decide, and `RLCD_DisplayWindow()` calls it again internally to actually build the window - a second, cheap pure-function call, not a source of drift, since both calls use the identical `x1,y1,x2,y2` and always produce the identical result.)

- [ ] **Step 3: Build**

Run: `idf.py build`.
Expected: builds cleanly. `RLCD_DisplayAuto()` is not called from anywhere yet (Task 4 wires it up) - this task's gate is that it compiles correctly and the threshold logic reads as intended on inspection: e.g. the full-panel case (`caset_xs=18, caset_xe=42` from Task 1's test) gives `caset_units = 25`, which is `>= 20`, so a full screen update correctly falls back to `RLCD_Display()`.

- [ ] **Step 4: Commit**

```bash
git add components/port_bsp/display_bsp.h components/port_bsp/display_bsp.cpp
git commit -m "feat: add RLCD_DisplayAuto() to pick windowed vs full-panel sends"
```

---

### Task 4: Batch integration and hardware verification

**Files:**
- Modify: `main/main.cpp`

**Interfaces:**
- Consumes: `void DisplayPort::RLCD_DisplayAuto(int x1, int y1, int x2, int y2)` (Task 3).
- Produces: nothing further - this is the plan's last task, wiring the new path into the live LVGL flush callback.

- [ ] **Step 1: Track a per-batch union rectangle and call `RLCD_DisplayAuto()`**

In `main/main.cpp`, find the current `Lvgl_FlushCallback()` (added/modified by Task 1 of the earlier Cal+Clock Vista redraw plan):

```cpp
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
```

Replace it with:

```cpp
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
    /* RLCD_DisplayAuto() sends only the union rect's window when that's
       smaller than the ~80%-height fallback threshold, and the existing
       full-panel RLCD_Display() otherwise - see its own comment. Only the
       last flush of a refresh pass needs to trigger the actual transfer:
       sending after every dirty rect produced N redundant sends in a row
       (see the history of this function for the batching fix this
       superseded). */
    if (lv_display_flush_is_last(drv))
    {
        RlcdPort.RLCD_DisplayAuto(s_batch_x1, s_batch_y1, s_batch_x2, s_batch_y2);
        s_batch_in_progress = false;
    }
    lv_disp_flush_ready(drv);
}
```

- [ ] **Step 2: Build**

Run: `idf.py build`.
Expected: builds with no errors or warnings.

- [ ] **Step 3: Flash and read the boot self-test log**

Flash to the device (this project's established download-mode + `idf.py -p COM8 flash` pattern) and monitor UART output during boot (`idf.py -p COM8 monitor`, or read the log through whatever the controller's existing flashing workflow already uses).
Expected: `SELFTEST COMPLETE: N check(s), 0 failure(s)` - the `0 failure(s)` covers every suite including this plan's new `DisplayBsp_RunTests()` (Task 1), not just this task's own change. Any `FAIL` line printed before that summary names the exact check, file, and line that failed - if `display_bsp_test.cpp` shows a failure, stop and re-derive the affected case's numbers by hand against `RLCD_ComputeWindow()`'s formulas before touching the hardware-facing code further.

- [ ] **Step 4: Hardware-verify the actual visual fix**

With the self-test confirmed passing, exercise the device normally and confirm:
- Switching between Main and Cal+Clock screens still looks correct - no corruption, no missing content, no stray garbage outside the changed area.
- Showing and dismissing the hint overlay (short/long press) still looks correct.
- Both of the above settle visibly faster than the ~50px/sec full-screen roll measured before this plan - the whole point of this work. A screen switch may still fall back to a full-panel send (both screens differ across nearly their whole height, likely exceeding the 80% threshold - see the spec's Roadmap note) and look unchanged from before; the hint overlay and other bounded-height changes should visibly speed up.
- The once-a-second colon blink and the focus frame still look exactly as before (unaffected by this change - same single small dirty rect either way).

If anything looks corrupted or wrong, this is a hardware-verification failure: do not mark this task complete. Re-check the affected screen region's actual pixel coordinates against `RLCD_ComputeWindow()`'s rounding by hand (the same technique used to catch this plan's own two spec-writing bugs - a CASET-direction sign error and a use-after-free - before any code was written) before attempting a fix.

- [ ] **Step 5: Commit**

```bash
git add main/main.cpp
git commit -m "feat: window the LVGL flush send to the batch's dirty-rect union"
```

## Definition of Done

- [ ] `RLCD_ComputeWindow()` exists, is a pure function independent of any `DisplayPort` instance, and its unit test (`DisplayBsp_RunTests()`) covers the full-panel case, a top-band case (verifying the CASET direction), a combined-axis case, and a RASET-only case.
- [ ] `RLCD_DisplayWindow()` sends a windowed sub-rectangle using a persistent scratch buffer, not a per-call allocation.
- [ ] `RLCD_DisplayAuto()` falls back to the existing full-panel `RLCD_Display()` above the 80%-height threshold.
- [ ] `Lvgl_FlushCallback()` tracks the union of every dirty rect in a refresh pass and calls `RLCD_DisplayAuto()` exactly once per pass, on the last flush - never more than one send per pass, matching the existing Task 1 batching discipline from the Cal+Clock Vista redraw plan.
- [ ] Device boots with `SELFTEST COMPLETE: N check(s), 0 failure(s)`.
- [ ] Hardware-verified: Main/Cal+Clock switching and the hint overlay show no corruption; bounded-height changes (hint overlay) visibly settle faster than the pre-existing ~50px/sec roll; colon blink and focus frame are unaffected.
