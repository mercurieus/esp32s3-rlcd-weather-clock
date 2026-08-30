# Why `Lvgl_Refresh()` Exists, and the Reference-Example Comparison

Context: this project drives the same Waveshare ESP32-S3-RLCD-4.2 board and
ST7305 panel as Waveshare's own official examples
(`ESP32-S3-RLCD-4.2/02_Example/ESP-IDF/08_LVGL_V8_Test` and
`09_LVGL_V9_Test`) and, further afield, XiaoZhi's `lcd_display.cc`. This
compares how each handles LVGL's refresh timing, and explains why this
project's `Lvgl_Refresh()` (`components/app_bsp/lvgl_bsp.cpp`) looks
different from both - written up after a 2026-08-30 investigation that
found this project had accidentally dropped a required settling delay (see
`docs/superpowers/specs/2026-08-30-windowed-partial-refresh-design.md`'s
correction note and `RLCD_PANEL_SETTLE_DELAY_MS` in `lvgl_bsp.h` for that
story).

## The Waveshare reference examples

Both `08_LVGL_V8_Test` and `09_LVGL_V9_Test` - same board, same panel, and
this project's own binary name `09_LVGL_V9_TestRLCD` all but confirms this
project started from the V9 one - define:

```c
#define LVGL_TICK_PERIOD_MS    5
#define LVGL_TASK_MAX_DELAY_MS 500
#define LVGL_TASK_MIN_DELAY_MS 50
```

They run a standing FreeRTOS task (`Lvgl_port_task`) that loops forever
calling `lv_timer_handler()`, then sleeps for whatever LVGL recommends -
clamped to **at least 50ms**. That clamp is exactly the number this
project found necessary by hardware A/B testing, completely independently.
It's not a coincidence - it's the same underlying panel requirement, baked
into Waveshare's own example as a floor on how often `lv_timer_handler()`
(and therefore a flush/`RLCD_Display()`) is allowed to fire at all. Their
flush callback has no delay of its own; the task loop's own pacing *is*
the settling delay, just structured differently than this project's.

They also use `LV_DISPLAY_RENDER_MODE_FULL` - always re-render and resend
the whole panel when anything's dirty, no partial-rect optimization at
all. So the reference examples are strictly less sophisticated than this
project on that axis (no dirty-rect batching, no windowing attempt), but
they get the timing right by construction.

## Why `Lvgl_Refresh()` exists here instead of that standing task

This project calls `esp_light_sleep_start()` in `ClockTask`'s main loop to
save power between updates (`components/clock/clock_task.c:341-342`) - a
battery-powered clock's clock task sleeps the CPU and only wakes on a
1-second timer or a button interrupt. A free-running background task
polling every 50-500ms is fundamentally incompatible with that: the CPU
could never light-sleep with another task waking up that often. So this
project traded the standard always-on task model for calling
`Lvgl_Refresh()` explicitly, only when the sleep loop wakes and something
is actually known to have changed - a deliberate, reasonable adaptation
for the power budget, not an oversight.

The cost of that trade is that the 50ms pacing the standing task gave for
free had to be reproduced explicitly - which is exactly what now sits in
`Lvgl_Refresh()` as `RLCD_PANEL_SETTLE_DELAY_MS`.

## XiaoZhi's `lcd_display.cc`

Doesn't hand-roll any of this at all - it uses Espressif's official
`esp_lvgl_port` component, which manages task/tick/timing internally.
It's the "correct, maintained library" approach in general, but it's
driving SPI/RGB/MIPI panel classes, not this reflective ST7305, so it has
no bearing on the settle-time question specifically - no equivalent quirk
to compare against.
