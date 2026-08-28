#include "overlay.h"

#include "fonts.h"

static lv_obj_t *s_temp;
static lv_obj_t *s_hum;
static lv_obj_t *s_date;
static lv_obj_t *s_battery;

/* A solid 2 px block, not an lv_line: hairlines dither away on a 1 bit
   panel. */
static void overlay_rule(lv_obj_t *parent, int x, int y, int w)
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

static lv_obj_t *overlay_frame(lv_obj_t *parent, int x, int y, int w, int h)
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

static lv_obj_t *overlay_label(lv_obj_t *parent, int x, int y, int width,
                              lv_text_align_t align, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (width > 0) {
        lv_obj_set_style_min_width(l, width, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_max_width(l, width, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(l, align, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lv_label_set_text_static(l, text);
    return l;
}

void Overlay_Create(void)
{
    lv_obj_t *top = lv_layer_top();

    /* The layer itself must not paint, or it would hide every screen. */
    lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(top, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(top, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    /* An opaque white band so screen content cannot show through the bar. */
    lv_obj_t *band = lv_obj_create(top);
    lv_obj_remove_flag(band, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(band, 0, 0);
    lv_obj_set_size(band, 400, 34);
    lv_obj_set_style_pad_all(band, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(band, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(band, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(band, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(band, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    overlay_frame(top, 329, 5, 64, 26);   /* battery body */
    overlay_frame(top, 391, 10, 7, 16);   /* battery nub  */

    s_temp    = overlay_label(top, 2, 3, 0, LV_TEXT_ALIGN_LEFT, "0.0");
    s_hum     = overlay_label(top, 89, 3, 0, LV_TEXT_ALIGN_LEFT, "0%");
    s_date    = overlay_label(top, 163, 3, 0, LV_TEXT_ALIGN_LEFT, "01.01.2000");
    s_battery = overlay_label(top, 332, 3, 58, LV_TEXT_ALIGN_CENTER, "0.00");

    overlay_rule(top, 0, 34, 400);

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    LV_LOG_USER("LVGL heap: %u free of %u", (unsigned)mon.free_size, (unsigned)mon.total_size);
}

void Overlay_SetTopBar(const char *temp, const char *hum,
                      const char *date, const char *battery)
{
    if (!s_temp) {
        return;
    }
    lv_label_set_text(s_temp, temp);
    lv_label_set_text(s_hum, hum);
    lv_label_set_text(s_date, date);
    lv_label_set_text(s_battery, battery);
}
