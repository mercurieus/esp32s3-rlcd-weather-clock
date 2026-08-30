#include "overlay.h"

#include "esp_heap_caps.h"

#include "fonts.h"
#include "dither.h"

static lv_obj_t *s_temp;
static lv_obj_t *s_hum;
static lv_obj_t *s_date;
static lv_obj_t *s_battery;
static lv_obj_t *s_focus;
static lv_obj_t *s_hint_box;
static lv_obj_t *s_hint_key_label;
static lv_obj_t *s_hint_boot_label;
static uint32_t  s_hint_until_ms;
static bool      s_hint_visible;

#define RAIL_BUF_SIZE LV_CANVAS_BUF_SIZE(400, 34, 16, LV_DRAW_BUF_STRIDE_ALIGN)
static uint8_t *s_rail_buf = NULL;

/* Fills a canvas rectangle with a dithered mid-grey band - the "metal"
   look. grey is 0..255, same convention as Dither_Threshold. canvas_x/y
   are the fill's position within the canvas, screen_x/y its absolute
   screen position (dithering is computed in screen space so adjacent
   dithered elements tile consistently against each other). */
static void canvas_dither_fill(lv_obj_t *canvas, int canvas_x, int canvas_y,
                               int screen_x, int screen_y, int w, int h, uint8_t grey)
{
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            lv_color_t c = Dither_Threshold(screen_x + px, screen_y + py, grey)
                ? lv_color_hex(0x000000) : lv_color_hex(0xffffff);
            lv_canvas_set_px(canvas, canvas_x + px, canvas_y + py, c, LV_OPA_COVER);
        }
    }
}

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

/* A recessed white plaque: keeps text off the dithered rail (the hard
   rule - dithering behind text destroys glyphs at this panel's pitch),
   shaded to read as sunken via a dark top-left edge, light bottom-right. */
static void overlay_plaque(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(o, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(o, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(o, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(o, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static lv_obj_t *overlay_label(lv_obj_t *parent, int x, int y, int width,
                              lv_text_align_t align, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (width > 0) {
        lv_obj_set_style_min_width(l, width, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_max_width(l, width, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(l, align, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lv_label_set_text_static(l, text);
    return l;
}

/* A small inverted badge: black box, white text. Marks which physical
   button ("Key" or "Boot") a hint line refers to, so it reads at a glance
   instead of requiring the sentence to be parsed first. Sized to its
   parent row and left un-positioned - the row's flex layout places it. */
static lv_obj_t *overlay_badge(lv_obj_t *parent, int w, int h, const char *text)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *l = lv_label_create(o);
    lv_obj_center(l);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text_static(l, text);
    return o;
}

void Overlay_Create(void)
{
    lv_obj_t *top = lv_layer_top();

    /* The layer itself must not paint, or it would hide every screen. */
    lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(top, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(top, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    /* A rolled metal rail: dithered mid-grey band, a bright ridge about a
       third of the way down and a dark lower lip - not a flat white bar.
       Drawn on a canvas because a per-object dither would blow the LVGL
       heap budget. Still fully opaque so screen content cannot show
       through the bar. */
    s_rail_buf = (uint8_t *)heap_caps_malloc(RAIL_BUF_SIZE, MALLOC_CAP_SPIRAM);
    lv_obj_t *band = lv_canvas_create(top);
    if (s_rail_buf) {
        lv_canvas_set_buffer(band, s_rail_buf, 400, 34, LV_COLOR_FORMAT_RGB565);
        lv_canvas_fill_bg(band, lv_color_hex(0xffffff), LV_OPA_COVER);

        canvas_dither_fill(band, 0, 0, 0, 0, 400, 30, 160);
        for (int x = 0; x < 400; x++) {
            lv_canvas_set_px(band, x, 10, lv_color_hex(0xffffff), LV_OPA_COVER);   /* bright ridge */
            lv_canvas_set_px(band, x, 29, lv_color_hex(0x000000), LV_OPA_COVER);   /* dark lower lip */
            lv_canvas_set_px(band, x, 30, lv_color_hex(0x000000), LV_OPA_COVER);
        }
    }
    lv_obj_set_pos(band, 0, 0);
    lv_obj_set_size(band, 400, 34);

    /* Battery plaque first, so the battery outline frames draw on top of it.
       Each plaque is sized to contain its reading's widest value at
       montserrat_20 ("-88.8" / "100%" / "01.01.2000" / "4.20") with a ~3 px
       margin, and sits in the rail's clean band (y 2..27, above the dark
       lower lip at y 29..30). The label is inset ~3 px inside its plaque.
       Visible dithered-metal gaps between plaques are intentional. */
    overlay_plaque(top, 328, 2, 58, 25);
    overlay_frame(top, 329, 5, 64, 26);   /* battery body */
    overlay_frame(top, 391, 10, 7, 16);   /* battery nub  */

    overlay_plaque(top, 0, 2, 60, 25);
    s_temp    = overlay_label(top, 3, 5, 0, LV_TEXT_ALIGN_LEFT, "0.0");

    overlay_plaque(top, 64, 2, 58, 25);
    s_hum     = overlay_label(top, 67, 5, 0, LV_TEXT_ALIGN_LEFT, "0%");

    overlay_plaque(top, 132, 2, 116, 25);
    s_date    = overlay_label(top, 135, 5, 0, LV_TEXT_ALIGN_LEFT, "01.01.2000");

    s_battery = overlay_label(top, 330, 5, 52, LV_TEXT_ALIGN_CENTER, "0.00");

    overlay_rule(top, 0, 34, 400);

    /* Focus frame. 3 px so it survives the 1 bit threshold, and hidden
       until a focusable element is selected. */
    s_focus = overlay_frame(top, 0, 0, 10, 10);
    lv_obj_set_style_border_width(s_focus, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(s_focus, LV_OBJ_FLAG_HIDDEN);

    /* Hint overlay: white box with a black outline, bottom centre. One
       line, one fixed sequence - "Key" (Select) badge+text then "Boot"
       (OK) badge+text, the silkscreen names, not the abstract role names.
       Flex row layout centres each badge against its text on the same
       baseline regardless of font metrics, instead of guessed offsets. */
    s_hint_box = lv_obj_create(top);
    lv_obj_remove_flag(s_hint_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_hint_box, 8, 268);
    lv_obj_set_size(s_hint_box, 384, 28);
    lv_obj_set_style_pad_all(s_hint_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(s_hint_box, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(s_hint_box, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(s_hint_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_hint_box, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_hint_box, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(s_hint_box, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(s_hint_box, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_hint_box, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(s_hint_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_hint_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(s_hint_box, LV_OBJ_FLAG_HIDDEN);

    overlay_badge(s_hint_box, 36, 18, "Key");

    s_hint_key_label = lv_label_create(s_hint_box);
    lv_obj_set_width(s_hint_key_label, LV_SIZE_CONTENT);
    lv_obj_set_height(s_hint_key_label, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(s_hint_key_label, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_hint_key_label, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text_static(s_hint_key_label, "");

    overlay_badge(s_hint_box, 36, 18, "Boot");

    s_hint_boot_label = lv_label_create(s_hint_box);
    lv_obj_set_width(s_hint_boot_label, LV_SIZE_CONTENT);
    lv_obj_set_height(s_hint_boot_label, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(s_hint_boot_label, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_hint_boot_label, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text_static(s_hint_boot_label, "");

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

void Overlay_ShowHint(const char *key_text, const char *boot_text,
                      uint32_t now_ms, uint32_t duration_ms)
{
    if (!s_hint_box) {
        return;
    }
    lv_label_set_text(s_hint_key_label, key_text);
    lv_label_set_text(s_hint_boot_label, boot_text);
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
