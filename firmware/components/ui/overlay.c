#include "overlay.h"

#include "fonts.h"

static lv_obj_t *s_temp;
static lv_obj_t *s_hum;
static lv_obj_t *s_date;
static lv_obj_t *s_battery;
static lv_obj_t *s_focus;
static lv_obj_t *s_hint_box;
static lv_obj_t *s_hint_label;
static uint32_t  s_hint_until_ms;
static bool      s_hint_visible;

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

    /* Focus frame. 3 px so it survives the 1 bit threshold, and hidden
       until a focusable element is selected. */
    s_focus = overlay_frame(top, 0, 0, 10, 10);
    lv_obj_set_style_border_width(s_focus, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(s_focus, LV_OBJ_FLAG_HIDDEN);

    /* Hint overlay: white box with a black outline, bottom centre. */
    s_hint_box = lv_obj_create(top);
    lv_obj_remove_flag(s_hint_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_hint_box, 40, 252);
    lv_obj_set_size(s_hint_box, 320, 40);
    lv_obj_set_style_pad_all(s_hint_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(s_hint_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_hint_box, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_hint_box, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(s_hint_box, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(s_hint_box, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_hint_box, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(s_hint_box, LV_OBJ_FLAG_HIDDEN);

    s_hint_label = lv_label_create(s_hint_box);
    lv_obj_set_pos(s_hint_label, 4, 4);
    lv_obj_set_size(s_hint_label, 308, 32);
    lv_label_set_long_mode(s_hint_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_hint_label, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_hint_label, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(s_hint_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text_static(s_hint_label, "");

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

void Overlay_ShowFocus(const lv_area_t *area)
{
    if (!s_focus) {
        return;
    }
    if (area == NULL) {
        lv_obj_add_flag(s_focus, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_set_pos(s_focus, area->x1, area->y1);
    lv_obj_set_size(s_focus,
                    area->x2 - area->x1 + 1,
                    area->y2 - area->y1 + 1);
    lv_obj_remove_flag(s_focus, LV_OBJ_FLAG_HIDDEN);
}

void Overlay_ShowHint(const char *text, uint32_t now_ms, uint32_t duration_ms)
{
    if (!s_hint_box) {
        return;
    }
    lv_label_set_text(s_hint_label, text);
    lv_obj_remove_flag(s_hint_box, LV_OBJ_FLAG_HIDDEN);
    s_hint_visible  = true;
    s_hint_until_ms = now_ms + duration_ms;
}

bool Overlay_TickHint(uint32_t now_ms)
{
    if (!s_hint_visible) {
        return false;
    }
    /* Unsigned difference, so this stays correct across the 49-day wrap. */
    if ((int32_t)(now_ms - s_hint_until_ms) < 0) {
        return false;
    }
    lv_obj_add_flag(s_hint_box, LV_OBJ_FLAG_HIDDEN);
    s_hint_visible = false;
    return true;
}
