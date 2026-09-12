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

/* Updates the top bar's live readings. Strings are copied.
   weather_icon may be NULL (no forecast yet), in which case the icon is
   hidden and the outdoor reading blanked. */
void Overlay_SetTopBar(const char *temp, const char *hum,
                       const lv_image_dsc_t *weather_icon, const char *outdoor,
                       bool data_stale,
                       const char *battery_volts, int battery_pct,
                       bool charging);

/* Draws the focus frame around an absolute screen rectangle.
   Pass NULL to hide it. */
/* What the status slot shows, and why it is not simply an aerial glyph.

   The radio on this board is powered up only for a sync, so it is down for all
   but a few seconds a day. A permanent aerial would therefore be claiming a
   connection that almost never exists. These four states each mean something
   different to someone looking at the panel:

     IDLE, data fresh  -> a tick:     last sync worked, radio resting
     IDLE, data stale  -> a warning:  the data is old, something is wrong
     CONNECTING        -> a refresh:  radio up, associating, no address yet
     CONNECTED         -> an aerial:  associated and holding an IP, right now

   Overlay_SetTopBar owns the slot only in IDLE; the live states are driven by
   the Wi-Fi event loop and must not be overwritten by a routine tick. */
typedef enum {
    OVERLAY_LINK_IDLE = 0,
    OVERLAY_LINK_CONNECTING,
    OVERLAY_LINK_CONNECTED,
} OverlayLinkState;

void Overlay_SetLinkState(OverlayLinkState state);

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
