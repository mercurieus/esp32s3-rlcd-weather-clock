#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BTN_EV_NONE = 0,
    BTN_EV_SELECT_SHORT,
    BTN_EV_SELECT_LONG,
    BTN_EV_OK_SHORT,
    BTN_EV_OK_LONG,
} btn_event_t;

#ifdef __cplusplus
}
#endif
