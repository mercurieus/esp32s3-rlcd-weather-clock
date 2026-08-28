#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Builds the shared overlay on lv_layer_top(), which floats above whichever
   screen is loaded. Call once, after create_screens(). */
void Overlay_Create(void);

/* Updates the top bar. Strings are copied. */
void Overlay_SetTopBar(const char *temp, const char *hum,
                      const char *date, const char *battery);

#ifdef __cplusplus
}
#endif
