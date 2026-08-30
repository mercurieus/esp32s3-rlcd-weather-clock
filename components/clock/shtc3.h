#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t Shtc3_Init(i2c_master_bus_handle_t bus);
esp_err_t Shtc3_Read(float *temperature_c, float *humidity_percent);

#ifdef __cplusplus
}
#endif