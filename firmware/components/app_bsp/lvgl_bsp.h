#pragma once

#include "lvgl.h"

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