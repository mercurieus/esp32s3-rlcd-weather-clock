#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t Pcf85063_Init(gpio_num_t sda_pin, gpio_num_t scl_pin);
esp_err_t Pcf85063_SetTime(const struct tm *time);
esp_err_t Pcf85063_GetTime(struct tm *time);

/* Whether the time the chip is holding can be believed.

   Bit 7 of the seconds register is the oscillator-stop flag: the PCF85063
   raises it whenever the oscillator has stopped since the last write, which is
   what happens when the chip loses power. With a backup cell fitted that means
   a flat cell; with the cell absent - as on this unit - it means any power
   cycle at all. Writing the time clears it.

   Without this the chip returns a perfectly well-formed wrong time after every
   unplug, and a clock that states the wrong time confidently is worse than one
   that admits it does not know. */
esp_err_t Pcf85063_TimeIsValid(bool *valid);
i2c_master_bus_handle_t Pcf85063_GetBusHandle(void);

#ifdef __cplusplus
}
#endif