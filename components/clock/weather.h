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

/* The conditions right now, as opposed to a day's forecast. Open-Meteo
   returns this separately from the daily block, and it can be absent even
   when the forecast parses, so it carries its own validity flag. */
typedef struct {
    bool valid;
    int  temp;
    int  weathercode;
} WeatherNow;

/* Fills days_out with a 4-day forecast. now_out may be NULL if the caller
   only wants the forecast; when given, its .valid says whether the response
   actually carried a current-conditions block. */
bool Weather_Fetch(WeatherDay days_out[4], WeatherNow *now_out);

#ifdef __cplusplus
}
#endif