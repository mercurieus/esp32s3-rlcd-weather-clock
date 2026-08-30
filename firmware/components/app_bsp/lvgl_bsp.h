#pragma once

#include "lvgl.h"

/* Minimum time to leave between the end of one panel send and the start of
   the next, so the ST7305 has time to finish its own internal RAM-write/
   settle cycle - on_color_trans_done (RLCD_WaitTransferDone()) only proves
   the SPI bytes are off the wire, not that the panel has caught up
   internally. Hardware-confirmed necessary via A/B test (2026-08-30): a
   build without this delay showed a multi-second visible "roll" on every
   large redraw; restoring it eliminated the roll (see Lvgl_Refresh()'s
   call site and docs/superpowers/specs/2026-08-30-windowed-partial-
   refresh-design.md for the investigation that led there).
   Independently corroborated by Waveshare's own reference examples for
   this exact board/panel (02_Example/ESP-IDF/{08_LVGL_V8_Test,
   09_LVGL_V9_Test}/components/app_bsp/lvgl_bsp.h, this project's likely
   ancestor per the build's binary name): both hardcode the identical 50ms
   as LVGL_TASK_MIN_DELAY_MS, the floor on how often their own background
   LVGL task is allowed to call lv_timer_handler() (and therefore flush/
   send) at all - the same requirement, enforced by a different mechanism.
   This project has no equivalent standing task (ClockTask calls
   esp_light_sleep_start() between updates to save power, which a
   free-running ~50ms-cadence task would prevent), so Lvgl_Refresh() pays
   this cost explicitly instead of getting it for free from a task's own
   pacing. */
#define RLCD_PANEL_SETTLE_DELAY_MS 50

typedef void (*DispFlushCb)(lv_display_t * disp, const lv_area_t * area, uint8_t * color_p);

#ifdef __cplusplus
extern "C" {
#endif

void Lvgl_PortInit(int width, int height, DispFlushCb flush_cb);
bool Lvgl_lock(int timeout_ms);
void Lvgl_unlock(void);
void Lvgl_Refresh(void);

#ifdef __cplusplus
}
#endif