#include "nav.h"

void nav_init(nav_state_t *st, nav_focus_count_fn count_fn, uint8_t settings_rows)
{
    st->mode          = NAV_SCREEN;
    st->screen        = NAV_SCREEN_MAIN;
    st->focus         = 0;
    st->settings_row  = 0;
    st->settings_rows = settings_rows;
    st->focus_wrapped = false;
    st->focus_count   = count_fn;
}

bool nav_handle(nav_state_t *st, btn_event_t ev)
{
    if (ev == BTN_EV_NONE) {
        return false;
    }

    st->focus_wrapped = false;

    /* Long OK opens settings from anywhere but settings itself. */
    if (ev == BTN_EV_OK_LONG) {
        if (st->mode == NAV_SETTINGS) {
            return false;
        }
        st->mode         = NAV_SETTINGS;
        st->settings_row = 0;
        return true;
    }

    switch (st->mode) {
    case NAV_SCREEN:
        if (ev == BTN_EV_SELECT_SHORT) {
            st->screen = (uint8_t)((st->screen + 1) % NAV_SCREEN_COUNT);
            st->focus  = 0;
            return true;
        }
        if (ev == BTN_EV_OK_SHORT) {
            if (st->focus_count(st->screen) == 0) {
                return false;
            }
            st->mode  = NAV_FOCUS;
            st->focus = 0;
            return true;
        }
        return false;

    case NAV_FOCUS: {
        const uint8_t n = st->focus_count(st->screen);
        if (ev == BTN_EV_SELECT_SHORT) {
            if (n == 0) {
                return false;
            }
            const uint8_t next = (uint8_t)((st->focus + 1) % n);
            st->focus_wrapped = (next < st->focus);
            st->focus         = next;
            return true;
        }
        if (ev == BTN_EV_SELECT_LONG) {
            st->mode = NAV_SCREEN;
            return true;
        }
        if (ev == BTN_EV_OK_SHORT) {
            st->mode = NAV_DETAIL;
            return true;
        }
        return false;
    }

    case NAV_DETAIL:
        if (ev == BTN_EV_SELECT_LONG || ev == BTN_EV_OK_SHORT) {
            st->mode = NAV_FOCUS;
            return true;
        }
        return false;

    case NAV_SETTINGS:
        if (ev == BTN_EV_SELECT_SHORT) {
            if (st->settings_rows == 0) {
                return false;
            }
            st->settings_row =
                (uint8_t)((st->settings_row + 1) % st->settings_rows);
            return true;
        }
        if (ev == BTN_EV_SELECT_LONG) {
            st->mode = NAV_SCREEN;
            return true;
        }
        if (ev == BTN_EV_OK_SHORT) {
            return true;   /* caller acts on the row, then redraws */
        }
        return false;
    }

    return false;
}
