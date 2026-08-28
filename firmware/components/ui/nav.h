#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "btn_event.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NAV_SCREEN = 0,
    NAV_FOCUS,
    NAV_DETAIL,
    NAV_SETTINGS,
} nav_mode_t;

#define NAV_SCREEN_MAIN     0
#define NAV_SCREEN_CALCLOCK 1
#define NAV_SCREEN_COUNT    2

/* Returns how many focusable elements a screen has. Supplied by the caller
   so this module needs no knowledge of any screen. */
typedef uint8_t (*nav_focus_count_fn)(uint8_t screen);

typedef struct {
    nav_mode_t         mode;
    uint8_t            screen;
    uint8_t            focus;
    uint8_t            settings_row;
    uint8_t            settings_rows;
    bool               focus_wrapped;  /* focus just wrapped past the end   */
    nav_focus_count_fn focus_count;
} nav_state_t;

void nav_init(nav_state_t *st, nav_focus_count_fn count_fn, uint8_t settings_rows);

/* Applies one button event. Returns true when the caller must redraw. */
bool nav_handle(nav_state_t *st, btn_event_t ev);

void Nav_RunTests(void);

#ifdef __cplusplus
}
#endif
