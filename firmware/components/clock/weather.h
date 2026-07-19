#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int day;
    int month;
    int weekday;
    int temp_max;
    int temp_min;
    int weathercode;
} WeatherDay;

bool Weather_Fetch(WeatherDay days_out[4]);

#ifdef __cplusplus
}
#endif