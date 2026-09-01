#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t Battery_Init(void);
esp_err_t Battery_ReadPercent(int *percent_out);
esp_err_t Battery_ReadVoltageMv(int *millivolts_out);

/* Maps a millivolt reading onto the same discharge curve Battery_ReadPercent
   uses. Exposed so a caller that already has a voltage does not have to take
   a second ADC reading, and cannot disagree with it. */
int Battery_PercentFromMv(int millivolts);

#ifdef __cplusplus
}
#endif