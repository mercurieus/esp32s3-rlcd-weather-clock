#pragma once

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
i2c_master_bus_handle_t Pcf85063_GetBusHandle(void);

#ifdef __cplusplus
}
#endif