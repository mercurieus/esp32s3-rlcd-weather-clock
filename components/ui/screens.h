#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <time.h>

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_INIT = 1,
    SCREEN_ID_MAIN = 2,
    SCREEN_ID_CALENDAR = 3,
    _SCREEN_ID_LAST = 3
};

typedef struct _objects_t {
    lv_obj_t *init;
    lv_obj_t *main;
    lv_obj_t *calendar;
    lv_obj_t *cal_title;
    lv_obj_t *cal_today;
    lv_obj_t *clock_canvas;
    lv_obj_t *quote;
    lv_obj_t *info;
    lv_obj_t *clock_hh1;
    lv_obj_t *obj2;
    lv_obj_t *obj4;
    lv_obj_t *clock_hh2;
    lv_obj_t *clock_mm1;
    lv_obj_t *clock_mm2;
    lv_obj_t *day1_icon;
    lv_obj_t *day2_icon;
    lv_obj_t *day3_icon;
    lv_obj_t *day4_icon;
    lv_obj_t *day1_date;
    lv_obj_t *day1_temp;
    lv_obj_t *day2_temp;
    lv_obj_t *day3_temp;
    lv_obj_t *day4_temp;
    lv_obj_t *day2_date;
    lv_obj_t *day3_date;
    lv_obj_t *day4_date;
    lv_obj_t *sync;
} objects_t;

extern objects_t objects;

void create_screen_init();
void tick_screen_init();

void create_screen_main();
void tick_screen_main();

void create_screen_calendar();
void tick_screen_calendar();

void update_clock_hands(int hour, int minute, int second);
void update_calendar_display(int disp_year, int disp_month, const struct tm *today);
void calendar_update_forecast(int idx, const char *date, const lv_image_dsc_t *icon, const char *temp);

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/