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

/* Call after waking with ESP_SLEEP_WAKEUP_GPIO. Debounces, classifies the
   press, then blocks until release so one push cannot emit twice.
   Returns BTN_EV_NONE if no button was actually down. */
btn_event_t Buttons_ReadEvent(void);

#ifdef __cplusplus
}
#endif
