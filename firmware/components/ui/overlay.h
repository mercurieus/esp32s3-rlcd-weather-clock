#pragma once

#include <stdbool.h>
#include <stdint.h>
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

/* Draws the focus frame around an absolute screen rectangle.
   Pass NULL to hide it. */
void Overlay_ShowFocus(const lv_area_t *area);

/* Shows the hint overlay for duration_ms: a single line, Key (Select)
   then Boot (OK) in that fixed sequence, each led by an inverted badge
   with that name. Either string may be empty when that button does
   nothing in the current mode. Text is copied. */
void Overlay_ShowHint(const char *key_text, const char *boot_text,
                      uint32_t now_ms, uint32_t duration_ms);

/* Call once per task tick. Hides an expired hint and returns true when it
   did, meaning the caller must refresh. LVGL timers cannot do this: they
   only run inside Lvgl_Refresh(), which only runs when something is dirty. */
bool Overlay_TickHint(uint32_t now_ms);

#ifdef __cplusplus
}
#endif
