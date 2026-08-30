#pragma once

#include "btn_event.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Configures both pins as inputs with internal pull-ups. Active low. */
void Buttons_Init(gpio_num_t select_pin, gpio_num_t ok_pin);

/* Arms both pins as light-sleep wake sources. Call once after Init. */
void Buttons_EnableWakeup(void);

/* True if a press edge fired since the last call, clearing it as it's
   read. Backed by a real GPIO interrupt, not sleep-wakeup cause: the task
   can be awake (e.g. mid panel-refresh) when a tap happens, and
   ESP_SLEEP_WAKEUP_GPIO only reflects presses still held at the next
   sleep entry, which drops any press that starts and ends while busy. */
bool Buttons_PressPending(void);

/* Call once Buttons_PressPending() returns true. Debounces, classifies the
   press, then blocks until release so one push cannot emit twice.
   Returns BTN_EV_NONE if no button was actually down. */
btn_event_t Buttons_ReadEvent(void);

#ifdef __cplusplus
}
#endif
