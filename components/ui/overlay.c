#include "overlay.h"

#include "esp_heap_caps.h"

#include "fonts.h"
#include "images.h"
#include "dither.h"

static lv_obj_t *s_temp;
static lv_obj_t *s_temp_trend;
static lv_obj_t *s_hum;
static lv_obj_t *s_hum_trend;
static lv_obj_t *s_weather_icon;
static lv_obj_t *s_outdoor;
static lv_obj_t *s_sync;
static bool      s_sync_busy;
static lv_obj_t *s_battery_volts;
static lv_obj_t *s_battery_fill;
static lv_obj_t *s_focus;
static lv_obj_t *s_hint_box;
static lv_obj_t *s_hint_key_label;
static lv_obj_t *s_hint_boot_label;
static uint32_t  s_hint_until_ms;
static bool      s_hint_visible;

/* The battery slot is a voltage reading with a small gauge beside it. The
   gauge shrank to 28x20 once it stopped being the only thing in the slot -
   it is a glanceable level, and the number carries the precision. Body
   x362..389 with a 2px border, so the fill well is x364..387 / y7..22; one
   more pixel of inset keeps the fill clear of the frame. */
#define BATT_FILL_X 365
#define BATT_FILL_Y 8
#define BATT_FILL_W 22
#define BATT_FILL_H 14

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

/* The readings ride directly on this rail (no plaques), so the band
   under them must stay legibly light - a sparse Bayer stipple, mostly
   white. A gentle gradient (a brighter ridge about a third of the way
   down, a touch darker toward the band edges) keeps it reading as brushed
   metal rather than flat, and the top/bottom 3 rows drop darker for a
   bevelled edge. */
static uint8_t rail_grey(int y)
{
    if (y <= 2)  return (uint8_t)(120 + y * 30);          /* 120,150,180  top bevel  */
    if (y >= 27) return (uint8_t)(160 - (y - 27) * 25);   /* 160,135,110  bottom bevel */
    int d = y - 11; if (d < 0) d = -d;                    /* ridge ~1/3 down */
    int g = 234 - d * 3;                                  /* 234 at ridge ... ~200 */
    return (uint8_t)(g < 198 ? 198 : g);
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

        for (int gy = 0; gy < 30; gy++) {
            canvas_dither_fill(band, 0, gy, 0, gy, 400, 1, rail_grey(gy));
        }
        /* No hard white ridge line - it cut straight through the reading
           glyphs. rail_grey's gradient already peaks bright around y11. */
        for (int x = 0; x < 400; x++) {
            lv_canvas_set_px(band, x, 29, lv_color_hex(0x000000), LV_OPA_COVER);   /* dark lower lip */
            lv_canvas_set_px(band, x, 30, lv_color_hex(0x000000), LV_OPA_COVER);
        }
    }
    lv_obj_set_pos(band, 0, 0);
    lv_obj_set_size(band, 400, 34);

    /* Kindle-style: the readings ride straight on the metal rail, no
       plaques. rail_grey keeps the text band a light, sparse stipple so
       20pt black text stays legible on it. Five slots, each wide enough
       for its widest possible value measured off montserrat_20's real
       adv_w table, so nothing can ever collide as values change:

         1 indoor    x6    w80   "-88.8" plus a trend arrow
         2 humidity  x106  w71   "100%" plus a trend arrow
         3 weather   x197  w68   24px forecast icon plus "-25"
         4 sync      x285  w25   one symbol glyph
         5 battery   x330  w64   boxed reading plus nub

       Two of the five are reserved placeholders that nothing writes to
       yet - the trend arrows and the sync glyph. They are created here
       anyway so the slot budget is fixed now and the row cannot shift
       around later when the data behind them lands.

       Every reading shares one y so they read as a single row, sitting
       high enough to clear the dark lip at y29/30. */
    #define RAIL_TEXT_Y 3
    s_temp       = overlay_label(top, 6,   RAIL_TEXT_Y, 0, LV_TEXT_ALIGN_LEFT, "0.0°");
    /* Placeholder: sized and positioned for the rising/falling arrow, but
       nothing sets it yet - there is no temperature history to trend on. */
    s_temp_trend = overlay_label(top, 68,  RAIL_TEXT_Y, 0, LV_TEXT_ALIGN_LEFT, "");
    s_hum        = overlay_label(top, 106, RAIL_TEXT_Y, 0, LV_TEXT_ALIGN_LEFT, "0%");
    /* Placeholder, same reasoning as the temperature arrow above. */
    s_hum_trend  = overlay_label(top, 159, RAIL_TEXT_Y, 0, LV_TEXT_ALIGN_LEFT, "");

    /* Outdoor forecast: the source icons are 90x90, so a 68/256 scale
       lands on ~24px. lv_image_set_scale zooms about the object's centre
       and leaves the layout box at the source size, so the box is pinned
       to 24x24 and the inner alignment set to CENTER - that keeps the
       scaled result inside the 24px slot at x197 instead of spilling 33px
       either side of it. Hidden until the first forecast arrives. */
    s_weather_icon = lv_image_create(top);
    lv_obj_set_pos(s_weather_icon, 197, 5);
    lv_obj_set_size(s_weather_icon, 24, 24);
    lv_image_set_src(s_weather_icon, &img_icon_cloud);
    lv_image_set_inner_align(s_weather_icon, LV_IMAGE_ALIGN_CENTER);
    lv_image_set_scale(s_weather_icon, 68);
    lv_obj_add_flag(s_weather_icon, LV_OBJ_FLAG_HIDDEN);

    s_outdoor = overlay_label(top, 225, RAIL_TEXT_Y, 0, LV_TEXT_ALIGN_LEFT, "");

    /* Sync status: a WiFi glyph while the data on screen is still worth
       believing, a warning once it has gone stale, and a refresh glyph while a
       sync is actually running. The slot is never empty - a blank reads as "no
       feature here" rather than as "all is well", and on a panel with no colour
       an absent glyph is indistinguishable from a bug.

       It tracks data freshness rather than live link state, because the radio
       is down between refreshes by design and an indicator that spent 29 of
       every 30 minutes showing "disconnected" would be worse than none. */
    s_sync = overlay_label(top, 285, RAIL_TEXT_Y, 0, LV_TEXT_ALIGN_LEFT, LV_SYMBOL_WIFI);

    /* Battery: the voltage, then a small hollow gauge and its nub. The
       reading is montserrat_16 rather than the row's 20pt - it is the least
       urgent number on the bar, and the smaller size buys the room the gauge
       needs beside it. Its y is set so it shares the 20pt row's baseline:
       that row sits at RAIL_TEXT_Y with line_height 22 and base_line 4, so
       the baseline is y21, and montserrat_16 (line_height 18, base_line 3)
       meets it at y6. The widest reading is 36px, so the 38px box never
       clips. */
    s_battery_volts = overlay_label(top, 320, 6, 38, LV_TEXT_ALIGN_RIGHT, "0.00");
    lv_obj_set_style_text_font(s_battery_volts, &lv_font_montserrat_16,
                               LV_PART_MAIN | LV_STATE_DEFAULT);

    overlay_frame(top, 362, 5, 28, 20);        /* gauge body - hollow outline */
    overlay_frame(top, 390, 10, 5, 10);        /* nub */

    /* Kindle-style: the box is a fuel gauge, not a number. Solid black
       rather than dithered - a stipple would read as a partial charge at a
       glance, and the whole point of the glyph is that its fill level is the
       reading. */
    s_battery_fill = lv_obj_create(top);
    lv_obj_remove_flag(s_battery_fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_battery_fill, BATT_FILL_X, BATT_FILL_Y);
    lv_obj_set_size(s_battery_fill, 0, BATT_FILL_H);
    lv_obj_set_style_pad_all(s_battery_fill, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(s_battery_fill, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_battery_fill, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_battery_fill, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_battery_fill, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

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
                       const lv_image_dsc_t *weather_icon, const char *outdoor,
                       bool data_stale,
                       const char *battery_volts, int battery_pct)
{
    if (!s_temp) {
        return;
    }
    lv_label_set_text(s_temp, temp);
    lv_label_set_text(s_hum, hum);
    /* A sync in progress owns the slot: it is the more useful thing to say,
       and the reading is about to be replaced anyway. */
    if (!s_sync_busy) {
        lv_label_set_text(s_sync, data_stale ? LV_SYMBOL_WARNING : LV_SYMBOL_WIFI);
    }
    lv_label_set_text(s_battery_volts, battery_volts);

    /* Round to the nearest pixel, but never round a battery that still has
       charge down to an empty-looking gauge. */
    if (battery_pct < 0)   battery_pct = 0;
    if (battery_pct > 100) battery_pct = 100;
    int fill_w = (BATT_FILL_W * battery_pct + 50) / 100;
    if (fill_w == 0 && battery_pct > 0) {
        fill_w = 1;
    }
    lv_obj_set_width(s_battery_fill, fill_w);

    /* Before the first successful fetch there is no forecast to show, and
       a stale icon would be worse than an empty slot - hide both halves of
       the weather slot rather than leaving last boot's cloud sitting there. */
    if (weather_icon == NULL) {
        lv_obj_add_flag(s_weather_icon, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_outdoor, "");
    } else {
        lv_image_set_src(s_weather_icon, weather_icon);
        lv_obj_remove_flag(s_weather_icon, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_outdoor, outdoor);
    }
}

void Overlay_SetSyncBusy(bool busy)
{
    if (!s_sync) {
        return;
    }
    s_sync_busy = busy;
    if (busy) {
        lv_label_set_text(s_sync, LV_SYMBOL_REFRESH);
    }
    /* Leaving busy does not clear the slot here - the next Overlay_SetTopBar
       decides between the stale warning and nothing, which is the same
       decision it makes at every other tick. */
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
