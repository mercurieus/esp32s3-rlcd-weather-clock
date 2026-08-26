#include <string.h>
#include <time.h>
#include <math.h>
#include "esp_heap_caps.h"

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

#include <string.h>

objects_t objects;

//
// Event handlers
//

lv_obj_t *tick_value_change_obj;

//
// Screens
//

void create_screen_init() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.init = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 400, 300);
    {
        lv_obj_t *parent_obj = obj;
        {
            // info
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.info = obj;
            lv_obj_set_pos(obj, 0, 138);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_min_width(obj, 400, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_max_width(obj, 400, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "");
        }
    }
    
    tick_screen_init();
}

void tick_screen_init() {
}

void create_screen_main() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 400, 300);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.obj0 = obj;
            lv_obj_set_pos(obj, 329, 5);
            lv_obj_set_size(obj, 64, 26);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        {
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.obj1 = obj;
            lv_obj_set_pos(obj, 391, 10);
            lv_obj_set_size(obj, 7, 16);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        {
            // clock_hh1
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.clock_hh1 = obj;
            lv_obj_set_pos(obj, -2, 39);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_font(obj, &ui_font_saira_condensed_bold200, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_max_width(obj, 90, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_min_width(obj, 90, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "0");
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.obj2 = obj;
            lv_obj_set_pos(obj, 178, 30);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_font(obj, &ui_font_saira200, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, ":");
        }
        {
            lv_obj_t *obj = lv_line_create(parent_obj);
            objects.obj3 = obj;
            lv_obj_set_pos(obj, 0, 34);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            static lv_point_precise_t line_points[] = {
                { 400, 0 },
                { 0, 0 }
            };
            lv_line_set_points(obj, line_points, 2);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        {
            lv_obj_t *obj = lv_line_create(parent_obj);
            objects.obj4 = obj;
            lv_obj_set_pos(obj, 0, 187);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            static lv_point_precise_t line_points[] = {
                { 400, 0 },
                { 0, 0 }
            };
            lv_line_set_points(obj, line_points, 2);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        {
            // temp
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.temp = obj;
            lv_obj_set_pos(obj, 2, 3);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "00.0 C");
        }
        {
            // hum
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.hum = obj;
            lv_obj_set_pos(obj, 89, 3);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "00%");
        }
        {
            // date
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.date = obj;
            lv_obj_set_pos(obj, 163, 3);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "01.01.2000r.");
        }
        {
            // battery
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.battery = obj;
            lv_obj_set_pos(obj, 332, 3);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_max_width(obj, 58, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_min_width(obj, 58, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "0.00");
        }
        {
            // clock_hh2
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.clock_hh2 = obj;
            lv_obj_set_pos(obj, 85, 39);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_font(obj, &ui_font_saira_condensed_bold200, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_max_width(obj, 90, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_min_width(obj, 90, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "0");
        }
        {
            // clock_mm1
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.clock_mm1 = obj;
            lv_obj_set_pos(obj, 218, 39);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_font(obj, &ui_font_saira_condensed_bold200, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_max_width(obj, 90, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_min_width(obj, 90, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "0");
        }
        {
            // clock_mm2
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.clock_mm2 = obj;
            lv_obj_set_pos(obj, 308, 39);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_font(obj, &ui_font_saira_condensed_bold200, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_max_width(obj, 90, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_min_width(obj, 90, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "0");
        }
        {
            // day1_icon
            lv_obj_t *obj = lv_image_create(parent_obj);
            objects.day1_icon = obj;
            lv_obj_set_pos(obj, 0, 192);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_image_set_src(obj, &img_icon_sun);
            lv_image_set_scale(obj, 130);
        }
        {
            // day2_icon
            lv_obj_t *obj = lv_image_create(parent_obj);
            objects.day2_icon = obj;
            lv_obj_set_pos(obj, 104, 192);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_image_set_src(obj, &img_icon_sun);
            lv_image_set_scale(obj, 130);
        }
        {
            // day3_icon
            lv_obj_t *obj = lv_image_create(parent_obj);
            objects.day3_icon = obj;
            lv_obj_set_pos(obj, 207, 192);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_image_set_src(obj, &img_icon_sun);
            lv_image_set_scale(obj, 130);
        }
        {
            // day4_icon
            lv_obj_t *obj = lv_image_create(parent_obj);
            objects.day4_icon = obj;
            lv_obj_set_pos(obj, 311, 192);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_image_set_src(obj, &img_icon_sun);
            lv_image_set_scale(obj, 130);
        }
        {
            // day1_date
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.day1_date = obj;
            lv_obj_set_pos(obj, 0, 193);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_max_width(obj, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_min_width(obj, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "30/12 Mo");
        }
        {
            // day1_temp
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.day1_temp = obj;
            lv_obj_set_pos(obj, 0, 261);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_min_width(obj, 90, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_max_width(obj, 90, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "29/18 C");
        }
        {
            // day2_temp
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.day2_temp = obj;
            lv_obj_set_pos(obj, 104, 262);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_min_width(obj, 90, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_max_width(obj, 90, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "29/18 C");
        }
        {
            // day3_temp
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.day3_temp = obj;
            lv_obj_set_pos(obj, 207, 262);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_max_width(obj, 90, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_min_width(obj, 90, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "29/18 C");
        }
        {
            // day4_temp
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.day4_temp = obj;
            lv_obj_set_pos(obj, 311, 262);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_length(obj, 85, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_min_width(obj, 90, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_max_width(obj, 90, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "29/18 C");
        }
        {
            // day2_date
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.day2_date = obj;
            lv_obj_set_pos(obj, 100, 193);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_max_width(obj, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_min_width(obj, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "24/12");
        }
        {
            // day3_date
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.day3_date = obj;
            lv_obj_set_pos(obj, 201, 193);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_max_width(obj, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_min_width(obj, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "1/1 Su");
        }
        {
            // day4_date
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.day4_date = obj;
            lv_obj_set_pos(obj, 301, 193);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_max_width(obj, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_min_width(obj, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "24/12 Sa");
        }
        {
            // sync
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.sync = obj;
            lv_obj_set_pos(obj, 0, 285);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_min_width(obj, 400, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_max_width(obj, 400, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "Last weather sync: 22.22.2023r. 00:00 UpTime: 0d 0h");
        }
    }
    
    tick_screen_main();
}

void tick_screen_main() {
}

#define CLOCK_SIZE 150
#define CLOCK_CENTER 75
#define CLOCK_RADIUS 68

/* The panel is 1 bit (main.cpp thresholds RGB565 at 0x7fff), so everything
   drawn here must be pure black and >= 2 px wide - anti-aliased 1 px strokes
   land near the threshold and break up into speckle. */

#define CLOCK_BUF_SIZE LV_CANVAS_BUF_SIZE(CLOCK_SIZE, CLOCK_SIZE, 16, LV_DRAW_BUF_STRIDE_ALIGN)

static uint8_t *clock_buf = NULL;

static void draw_clock_face(lv_layer_t *layer)
{
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(0x000000);
    line_dsc.opa = LV_OPA_COVER;
    line_dsc.width = 3;

    /* Only the 12 hour marks: 60 minute ticks at 1 px do not survive the
       1 bit threshold and just turn the rim into noise. */
    for (int i = 0; i < 12; i++) {
        double angle = (i * 30 - 90) * M_PI / 180.0;
        int inner = CLOCK_RADIUS - 11;
        int outer = CLOCK_RADIUS - 2;
        line_dsc.p1.x = CLOCK_CENTER + (int)(inner * cos(angle));
        line_dsc.p1.y = CLOCK_CENTER + (int)(inner * sin(angle));
        line_dsc.p2.x = CLOCK_CENTER + (int)(outer * cos(angle));
        line_dsc.p2.y = CLOCK_CENTER + (int)(outer * sin(angle));
        lv_draw_line(layer, &line_dsc);
    }

    lv_draw_arc_dsc_t arc_dsc;
    lv_draw_arc_dsc_init(&arc_dsc);
    arc_dsc.color = lv_color_hex(0x000000);
    arc_dsc.width = 3;
    arc_dsc.radius = CLOCK_RADIUS;
    arc_dsc.center.x = CLOCK_CENTER;
    arc_dsc.center.y = CLOCK_CENTER;
    arc_dsc.start_angle = 0;
    arc_dsc.end_angle = 360;
    arc_dsc.opa = LV_OPA_COVER;
    lv_draw_arc(layer, &arc_dsc);
}

/* No second hand: it cost a full 400x300 panel refresh every second and the
   old 0x888888 stroke thresholded to white, so it was never visible anyway. */
void update_clock_hands(int hour, int minute)
{
    lv_obj_t *canvas = objects.clock_canvas;
    if (!canvas) return;

    lv_canvas_fill_bg(canvas, lv_color_hex(0xFFFFFF), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    draw_clock_face(&layer);

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(0x000000);
    line_dsc.opa = LV_OPA_COVER;
    line_dsc.p1.x = CLOCK_CENTER;
    line_dsc.p1.y = CLOCK_CENTER;

    double h_angle = ((hour % 12) * 30 + minute * 0.5 - 90) * M_PI / 180.0;
    line_dsc.width = 5;
    line_dsc.p2.x = CLOCK_CENTER + (int)(34 * cos(h_angle));
    line_dsc.p2.y = CLOCK_CENTER + (int)(34 * sin(h_angle));
    lv_draw_line(&layer, &line_dsc);

    double m_angle = (minute * 6 - 90) * M_PI / 180.0;
    line_dsc.width = 3;
    line_dsc.p2.x = CLOCK_CENTER + (int)(52 * cos(m_angle));
    line_dsc.p2.y = CLOCK_CENTER + (int)(52 * sin(m_angle));
    lv_draw_line(&layer, &line_dsc);

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_hex(0x000000);
    rect_dsc.bg_opa = LV_OPA_COVER;
    lv_area_t hub = { CLOCK_CENTER - 4, CLOCK_CENTER - 4, CLOCK_CENTER + 4, CLOCK_CENTER + 4 };
    lv_draw_rect(&layer, &rect_dsc, &hub);

    lv_canvas_finish_layer(canvas, &layer);
    lv_obj_invalidate(canvas);
}

static const char *QUOTES[] = {
    "The only way to do great work is to love what you do. - Steve Jobs",
    "In the middle of difficulty lies opportunity. - Albert Einstein",
    "Life is what happens when you are busy making other plans. - John Lennon",
    "The future belongs to those who believe in the beauty of their dreams. - Eleanor Roosevelt",
    "It is during our darkest moments that we must focus to see the light. - Aristotle",
    "The purpose of our lives is to be happy. - Dalai Lama",
    "In three words I can sum up life: it goes on. - Robert Frost",
    "Be the change that you wish to see in the world. - Mahatma Gandhi",
    "The best time to plant a tree was 20 years ago. The second best time is now. - Chinese Proverb",
    "Not all those who wander are lost. - J.R.R. Tolkien",
    "Everything you can imagine is real. - Pablo Picasso",
    "Do what you can, with what you have, where you are. - Theodore Roosevelt",
    "Simplicity is the ultimate sophistication. - Leonardo da Vinci",
    "Turn your wounds into wisdom. - Oprah Winfrey",
    "The only impossible journey is the one you never begin. - Tony Robbins",
    "Happiness is not something ready made. It comes from your own actions. - Dalai Lama",
    "Life is really simple, but we insist on making it complicated. - Confucius",
    "The mind is everything. What you think you become. - Buddha",
    "Strive not to be a success, but rather to be of value. - Albert Einstein",
    "You miss 100% of the shots you don't take. - Wayne Gretzky",
};

#define NUM_QUOTES (sizeof(QUOTES) / sizeof(QUOTES[0]))

//
// Calendar screen layout (400 x 300, 1 bit panel)
//
//   0..36    top bar: temp / humidity / date / battery, rule at y=34
//   40..58   month title
//   62..202  day columns: one label per weekday, line 0 is the "Mo..Su"
//            header, lines 1..6 are the week rows
//   52..202  analog clock canvas (right hand side)
//   210..244 four day weather row
//   254..296 quote of the day
//
#define CAL_TITLE_Y       40
#define CAL_COL_X0        6
#define CAL_COL_W         30
#define CAL_ROW_H         20    /* montserrat_14 line_height (16) + 4 */
#define CAL_ROWS          6     /* Monday-first months never need a 7th */
#define CAL_GRID_Y        62
#define CAL_GRID_W        (CAL_COL_W * 7)
#define CAL_GRID_BOTTOM   (CAL_GRID_Y + (CAL_ROWS + 1) * CAL_ROW_H)

#define CAL_CLOCK_X       240
#define CAL_CLOCK_Y       52

#define CAL_FC_Y          210
#define CAL_FC_W          100
#define CAL_FC_TEXT_X     38
#define CAL_FC_ICON_SCALE 91    /* 90 px source -> ~32 px */

#define CAL_QUOTE_Y       254

static lv_obj_t *s_cal_cols[7];
static lv_obj_t *s_cal_bar_temp;
static lv_obj_t *s_cal_bar_hum;
static lv_obj_t *s_cal_bar_date;
static lv_obj_t *s_cal_bar_batt;
static lv_obj_t *s_cal_fc_icon[4];
static lv_obj_t *s_cal_fc_date[4];
static lv_obj_t *s_cal_fc_temp[4];

/* A solid 2 px block, not an lv_line: hairlines dither away on a 1 bit panel. */
static void cal_add_rule(lv_obj_t *parent, int x, int y, int w)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, 2);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static lv_obj_t *cal_add_frame(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(o, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(o, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(o, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    return o;
}

static lv_obj_t *cal_add_label(lv_obj_t *parent, int x, int y, const lv_font_t *font,
                               int width, lv_text_align_t align, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (width > 0) {
        lv_obj_set_style_min_width(l, width, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_max_width(l, width, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(l, align, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lv_label_set_text_static(l, text);
    return l;
}

/* Mirrors the main screen top bar. LVGL objects cannot live on two screens,
   so this is a second set of widgets fed by the same values. */
static void create_calendar_top_bar(lv_obj_t *parent)
{
    cal_add_frame(parent, 329, 5, 64, 26);   /* battery body */
    cal_add_frame(parent, 391, 10, 7, 16);   /* battery nub  */

    s_cal_bar_temp = cal_add_label(parent, 2, 3, &lv_font_montserrat_26, 0, LV_TEXT_ALIGN_LEFT, "0.0");
    s_cal_bar_hum  = cal_add_label(parent, 89, 3, &lv_font_montserrat_26, 0, LV_TEXT_ALIGN_LEFT, "0%");
    s_cal_bar_date = cal_add_label(parent, 163, 3, &lv_font_montserrat_26, 0, LV_TEXT_ALIGN_LEFT, "01.01.2000");
    s_cal_bar_batt = cal_add_label(parent, 332, 3, &lv_font_montserrat_26, 58, LV_TEXT_ALIGN_CENTER, "0.00");

    cal_add_rule(parent, 0, 34, 400);
}

static void create_calendar_forecast_row(lv_obj_t *parent)
{
    for (int i = 0; i < 4; i++) {
        int x = i * CAL_FC_W;

        lv_obj_t *icon = lv_image_create(parent);
        lv_obj_set_pos(icon, x + 2, CAL_FC_Y);
        lv_obj_set_size(icon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_image_set_src(icon, &img_icon_sun);
        /* Scaling happens around the pivot, which defaults to the centre of
           the 90 px source - a shrunk icon would land ~29 px down and right
           of its own position. Anchor it top-left instead. */
        lv_image_set_pivot(icon, 0, 0);
        lv_image_set_scale(icon, CAL_FC_ICON_SCALE);
        s_cal_fc_icon[i] = icon;

        s_cal_fc_date[i] = cal_add_label(parent, x + CAL_FC_TEXT_X, CAL_FC_Y,
                                         &lv_font_montserrat_12, 0, LV_TEXT_ALIGN_LEFT, "--/--");
        s_cal_fc_temp[i] = cal_add_label(parent, x + CAL_FC_TEXT_X, CAL_FC_Y + 16,
                                         &lv_font_montserrat_12, 0, LV_TEXT_ALIGN_LEFT, "--/--");
    }
}

void create_screen_calendar() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.calendar = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 400, 300);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    create_calendar_top_bar(obj);

    objects.cal_title = cal_add_label(obj, 8, CAL_TITLE_Y, &lv_font_montserrat_16,
                                      0, LV_TEXT_ALIGN_LEFT, "Calendar");

    /* One label per weekday column instead of one space-padded text block:
       Montserrat is proportional (digit 1 is 5 px, digit 4 is 9 px), so the
       columns can only line up if each one is its own fixed-width centred
       label. Line 0 is the weekday header, so the header is always over its
       own column by construction. */
    static const char *DOW[7] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };
    for (int c = 0; c < 7; c++) {
        lv_obj_t *col = lv_label_create(obj);
        s_cal_cols[c] = col;
        lv_obj_set_pos(col, CAL_COL_X0 + c * CAL_COL_W, CAL_GRID_Y);
        lv_obj_set_size(col, CAL_COL_W, (CAL_ROWS + 1) * CAL_ROW_H);
        lv_label_set_long_mode(col, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(col, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(col, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(col, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        /* labels ignore pad_row - line pitch comes from text_line_space */
        lv_obj_set_style_text_line_space(col, CAL_ROW_H - 16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text_static(col, DOW[c]);
    }

    cal_add_rule(obj, CAL_COL_X0, CAL_GRID_Y + CAL_ROW_H - 4, CAL_GRID_W);

    {
        /* Sized and placed from the same constants as the columns, so the box
           always lands exactly on a cell. */
        lv_obj_t *hl = cal_add_frame(obj, 0, 0, CAL_COL_W - 4, CAL_ROW_H - 2);
        objects.cal_today = hl;
        lv_obj_add_flag(hl, LV_OBJ_FLAG_HIDDEN);
    }

    {
        if (!clock_buf) {
            clock_buf = (uint8_t *)heap_caps_malloc(CLOCK_BUF_SIZE, MALLOC_CAP_SPIRAM);
            if (!clock_buf) {
                clock_buf = (uint8_t *)heap_caps_malloc(CLOCK_BUF_SIZE, MALLOC_CAP_DEFAULT);
            }
        }
        if (clock_buf) {
            lv_obj_t *canvas = lv_canvas_create(obj);
            objects.clock_canvas = canvas;
            lv_canvas_set_buffer(canvas, clock_buf, CLOCK_SIZE, CLOCK_SIZE, LV_COLOR_FORMAT_RGB565);
            lv_obj_set_pos(canvas, CAL_CLOCK_X, CAL_CLOCK_Y);
            update_clock_hands(0, 0);
        }
    }

    cal_add_rule(obj, 0, CAL_GRID_BOTTOM + 4, 400);
    create_calendar_forecast_row(obj);
    cal_add_rule(obj, 0, CAL_QUOTE_Y - 4, 400);

    {
        lv_obj_t *ql = lv_label_create(obj);
        objects.quote = ql;
        lv_obj_set_pos(ql, 2, CAL_QUOTE_Y);
        lv_obj_set_size(ql, 396, 300 - CAL_QUOTE_Y - 2);
        lv_label_set_long_mode(ql, LV_LABEL_LONG_WRAP);
        /* montserrat_10 is unreadable once the 1 bit threshold eats the
           anti-aliasing; 12 is the smallest that survives. */
        lv_obj_set_style_text_font(ql, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ql, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(ql, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text_static(ql, "");
    }

    tick_screen_calendar();
}

void calendar_update_top_bar(const char *temp, const char *hum, const char *date, const char *battery)
{
    if (!s_cal_bar_temp) return;
    lv_label_set_text(s_cal_bar_temp, temp);
    lv_label_set_text(s_cal_bar_hum, hum);
    lv_label_set_text(s_cal_bar_date, date);
    lv_label_set_text(s_cal_bar_batt, battery);
}

void calendar_update_forecast(int idx, const char *date, const lv_image_dsc_t *icon, const char *temp)
{
    if (idx < 0 || idx >= 4 || !s_cal_fc_icon[idx]) return;
    lv_label_set_text(s_cal_fc_date[idx], date);
    lv_image_set_src(s_cal_fc_icon[idx], icon);
    lv_label_set_text(s_cal_fc_temp[idx], temp);
}

void update_calendar_display(const struct tm *ti) {
    static const char *MONTHS[] = { "January","February","March","April","May","June",
                                    "July","August","September","October","November","December" };
    static const char *DOW[7] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };

    if (!objects.calendar) return;

    lv_label_set_text_fmt(objects.cal_title, "%s %d", MONTHS[ti->tm_mon], ti->tm_year + 1900);

    struct tm first = { .tm_year = ti->tm_year, .tm_mon = ti->tm_mon, .tm_mday = 1, .tm_isdst = -1 };
    mktime(&first);
    int start_dow = (first.tm_wday + 6) % 7;    /* 0 = Monday */

    /* day 0 of the next month normalises to the last day of this one */
    struct tm last = { .tm_year = ti->tm_year, .tm_mon = ti->tm_mon + 1, .tm_mday = 0, .tm_isdst = -1 };
    mktime(&last);
    int days_in_month = last.tm_mday;

    for (int c = 0; c < 7; c++) {
        char col[8 + CAL_ROWS * 4];
        size_t n = (size_t)snprintf(col, sizeof(col), "%s", DOW[c]);
        for (int r = 0; r < CAL_ROWS && n < sizeof(col) - 1; r++) {
            int day = r * 7 + c - start_dow + 1;
            if (day >= 1 && day <= days_in_month) {
                n += (size_t)snprintf(col + n, sizeof(col) - n, "\n%d", day);
            } else {
                n += (size_t)snprintf(col + n, sizeof(col) - n, "\n");
            }
        }
        lv_label_set_text(s_cal_cols[c], col);
    }

    int idx = start_dow + ti->tm_mday - 1;
    lv_obj_set_pos(objects.cal_today,
                   CAL_COL_X0 + (idx % 7) * CAL_COL_W + 2,
                   CAL_GRID_Y + (1 + idx / 7) * CAL_ROW_H - 1);
    lv_obj_remove_flag(objects.cal_today, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text_static(objects.quote, QUOTES[ti->tm_yday % NUM_QUOTES]);

    lv_obj_invalidate(objects.calendar);
}

void tick_screen_calendar() {
}

typedef void (*tick_screen_func_t)();
tick_screen_func_t tick_screen_funcs[] = {
    tick_screen_init,
    tick_screen_main,
    tick_screen_calendar,
};
void tick_screen(int screen_index) {
    if (screen_index >= 0 && screen_index < 3) {
        tick_screen_funcs[screen_index]();
    }
}
void tick_screen_by_id(enum ScreensEnum screenId) {
    tick_screen(screenId - 1);
}

//
// Fonts
//

ext_font_desc_t fonts[] = {
    { "Saira200", &ui_font_saira200 },
    { "SairaCondensedBold200", &ui_font_saira_condensed_bold200 },
#if LV_FONT_MONTSERRAT_8
    { "MONTSERRAT_8", &lv_font_montserrat_8 },
#endif
#if LV_FONT_MONTSERRAT_10
    { "MONTSERRAT_10", &lv_font_montserrat_10 },
#endif
#if LV_FONT_MONTSERRAT_12
    { "MONTSERRAT_12", &lv_font_montserrat_12 },
#endif
#if LV_FONT_MONTSERRAT_14
    { "MONTSERRAT_14", &lv_font_montserrat_14 },
#endif
#if LV_FONT_MONTSERRAT_16
    { "MONTSERRAT_16", &lv_font_montserrat_16 },
#endif
#if LV_FONT_MONTSERRAT_18
    { "MONTSERRAT_18", &lv_font_montserrat_18 },
#endif
#if LV_FONT_MONTSERRAT_20
    { "MONTSERRAT_20", &lv_font_montserrat_20 },
#endif
#if LV_FONT_MONTSERRAT_22
    { "MONTSERRAT_22", &lv_font_montserrat_22 },
#endif
#if LV_FONT_MONTSERRAT_24
    { "MONTSERRAT_24", &lv_font_montserrat_24 },
#endif
#if LV_FONT_MONTSERRAT_26
    { "MONTSERRAT_26", &lv_font_montserrat_26 },
#endif
#if LV_FONT_MONTSERRAT_28
    { "MONTSERRAT_28", &lv_font_montserrat_28 },
#endif
#if LV_FONT_MONTSERRAT_30
    { "MONTSERRAT_30", &lv_font_montserrat_30 },
#endif
#if LV_FONT_MONTSERRAT_32
    { "MONTSERRAT_32", &lv_font_montserrat_32 },
#endif
#if LV_FONT_MONTSERRAT_34
    { "MONTSERRAT_34", &lv_font_montserrat_34 },
#endif
#if LV_FONT_MONTSERRAT_36
    { "MONTSERRAT_36", &lv_font_montserrat_36 },
#endif
#if LV_FONT_MONTSERRAT_38
    { "MONTSERRAT_38", &lv_font_montserrat_38 },
#endif
#if LV_FONT_MONTSERRAT_40
    { "MONTSERRAT_40", &lv_font_montserrat_40 },
#endif
#if LV_FONT_MONTSERRAT_42
    { "MONTSERRAT_42", &lv_font_montserrat_42 },
#endif
#if LV_FONT_MONTSERRAT_44
    { "MONTSERRAT_44", &lv_font_montserrat_44 },
#endif
#if LV_FONT_MONTSERRAT_46
    { "MONTSERRAT_46", &lv_font_montserrat_46 },
#endif
#if LV_FONT_MONTSERRAT_48
    { "MONTSERRAT_48", &lv_font_montserrat_48 },
#endif
};

//
// Color themes
//

uint32_t active_theme_index = 0;

//
//
//

void create_screens() {

// Set default LVGL theme
    lv_display_t *dispp = lv_display_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), false, LV_FONT_DEFAULT);
    lv_display_set_theme(dispp, theme);
    
    // Initialize screens
    // Create screens
    create_screen_init();
    create_screen_main();
    create_screen_calendar();
}