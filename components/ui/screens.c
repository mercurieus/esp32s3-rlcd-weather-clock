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
#include "dither.h"

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
            // clock_date
            /* The date moved off the shared top bar and onto the face, into
               the one hole the big digits leave: the colon column. It sits in
               the gap between the colon's two dots, not below them - below
               would have run into the horizontal rule at y187 and overlapped
               the lower dot, which blinks every second.

               Decoded from ui_font_saira200's glyph bitmap for ':' (4bpp,
               uncompressed, 23x102 at bitmap_index 51520): the ink rows are
               0..21 and 80..101, so with the glyph box starting at y68 the
               upper dot is y68..89, the lower dot y148..169, and the gap
               between them is y90..147 - 58px tall.

               Stacking the weekday over the day number trades the horizontal
               problem for a vertical one, and the vertical budget is the
               looser of the two. Set on one line the date was fighting the
               neighbouring digits: the widest glyph in
               ui_font_saira_condensed_bold200 ('8', adv_w 1574, box_w 84)
               overflows its own 90px column and leaves ink free only from
               about x172 to x221, a 49px channel, while the widest date
               ("We 24") is 45px at montserrat_14 and 52px at 16pt. Split over
               two lines the widest line is just "24", so the font can go back
               up to 16pt and still clear the digits with room to spare.

               Two montserrat_16 lines are 2 x 18 = 36px (line_space defaults
               to 0), leaving 11px above and below inside the 58px gap.

               The one-line version wanted lifting off the lower dot, but a
               36px block behaves differently: matching that same optical
               centre left only 6px above the block and 15px below, which read
               as sitting high. Stacked, plain geometric centring is right -
               90 + (58 - 36) / 2 = y101, 11px clear each side. There is very
               little room left to tune: the block can only travel y90..y111
               before it touches a dot.

               Horizontally the box is centred on the dots' own ink: the ':'
               glyph is 23px wide starting at x188, so its centre is x199.5,
               and a 48px box at x176 centres on x200. Stacking made this
               sensitive - the ink is only ~18px wide sitting directly under a
               23px dot, so an offset reads as a misalignment against the dot
               rather than against the column. It also made it cheap: the
               widest line is now just "24", so the 49px channel between the
               digits no longer binds the way it did on one line. */
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.clock_date = obj;
            lv_obj_set_pos(obj, 176, 101);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_min_width(obj, 48, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_max_width(obj, 48, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "We\n24");
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

#define CLOCK_SIZE 166
#define CLOCK_CENTER 83
#define CLOCK_RADIUS 73

/* Dial's screen position on the Cal+Clock screen (x 222..388, y 38..204). */
#define CAL_CLOCK_X       222
#define CAL_CLOCK_Y       38

#define MINOR_DOT_RADIUS (CLOCK_RADIUS - 4)  /* near the rim, scales with the face */
#define MINOR_DOT_SIZE   3      /* 3x3 px block - axis-aligned, no diagonal speckle */
#define MINOR_DOT_GREY   128    /* dithered mid-grey: lighter than the solid hour ticks */

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

    /* Only the 12 hour marks: verified to survive the 1 bit threshold at
       this spacing. The 60 minor ticks are dithered dots painted separately
       (draw_minor_dots) - as 2 px radial lines they broke into speckle at
       diagonal angles under the threshold. */
    line_dsc.width = 3;
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

/* The 60 minor ticks, as 3x3 dithered-grey dots near the rim. Painted with
   lv_canvas_set_px straight on the canvas, which must run outside the
   lv_canvas_init_layer / finish_layer pair. Screen-space coords are fed to
   Dither_Threshold so the pattern tiles consistently with the rest of the UI. */
static void draw_minor_dots(lv_obj_t *canvas)
{
    const int half = MINOR_DOT_SIZE / 2;
    for (int i = 0; i < 60; i++) {
        if (i % 5 == 0) continue;                 /* skip the 12 hour positions */
        double a = (i * 6 - 90) * M_PI / 180.0;
        int cx = CLOCK_CENTER + (int)(MINOR_DOT_RADIUS * cos(a));
        int cy = CLOCK_CENTER + (int)(MINOR_DOT_RADIUS * sin(a));
        for (int dy = -half; dy <= half; dy++) {
            for (int dx = -half; dx <= half; dx++) {
                int px = cx + dx, py = cy + dy;
                if (px < 0 || py < 0 || px >= CLOCK_SIZE || py >= CLOCK_SIZE) continue;
                if (Dither_Threshold(CAL_CLOCK_X + px, CAL_CLOCK_Y + py, MINOR_DOT_GREY)) {
                    lv_canvas_set_px(canvas, px, py, lv_color_hex(0x000000), LV_OPA_COVER);
                }
            }
        }
    }
}

/* The dithered dial bezel: a ring just outside CLOCK_RADIUS. Redrawn every
   update_clock_hands call (it is only ~2000 set_px, trivial next to the
   per-second full-panel SPI send) because update_clock_hands full-clears
   the canvas each time. Screen-space coords feed Dither_Threshold so the
   ring tiles with the header rail.

   The ring band is r 75..81 (CLOCK_RADIUS+2 .. CLOCK_RADIUS+8), whose
   outer edge (81) stays inside the 83 px half-canvas (166 px canvas), so
   the ring renders complete - no clipping at the four cardinal points.

   The ring carries a radial grey gradient (bright in the middle of its
   band, dark at the inner and outer edges), thresholded through the Bayer
   matrix, so it reads as a rounded metal rim rather than a flat tint -
   the "real per-pixel grey gradient" the spec asks for. */
static void draw_clock_bezel(lv_obj_t *canvas)
{
    for (int y = 0; y < CLOCK_SIZE; y++) {
        for (int x = 0; x < CLOCK_SIZE; x++) {
            double dx = x - CLOCK_CENTER, dy = y - CLOCK_CENTER;
            double r = sqrt(dx * dx + dy * dy);
            if (r < CLOCK_RADIUS + 2 || r > CLOCK_RADIUS + 8) continue;
            double band_mid = CLOCK_RADIUS + 5;
            double t = 1.0 - fabs(r - band_mid) / 3.0;   /* 1 at mid, 0 at edges */
            if (t < 0.0) t = 0.0;
            uint8_t g = (uint8_t)(80.0 + 150.0 * t);      /* 80..230 */
            if (Dither_Threshold(CAL_CLOCK_X + x, CAL_CLOCK_Y + y, g)) {
                lv_canvas_set_px(canvas, x, y, lv_color_hex(0x000000), LV_OPA_COVER);
            }
        }
    }
}

void update_clock_hands(int hour, int minute, int second)
{
    lv_obj_t *canvas = objects.clock_canvas;
    if (!canvas) return;

    lv_canvas_fill_bg(canvas, lv_color_hex(0xFFFFFF), LV_OPA_COVER);

    draw_minor_dots(canvas);

    draw_clock_bezel(canvas);

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    draw_clock_face(&layer);

    /* Digital HH:MM window on the lower face - drawn into the layer BEFORE
       the hands, so they sweep over it. Recessed look: white fill, thin
       black frame, and a dark inner top-left edge for the "sunk in" shadow. */
    char hhmm[6];
    lv_snprintf(hhmm, sizeof(hhmm), "%02d:%02d", hour, minute);

    lv_area_t win = {
        CLOCK_CENTER - 25, CLOCK_CENTER + 20,
        CLOCK_CENTER + 25, CLOCK_CENTER + 42,
    };
    lv_draw_rect_dsc_t win_dsc;
    lv_draw_rect_dsc_init(&win_dsc);
    win_dsc.bg_color = lv_color_hex(0xffffff);
    win_dsc.bg_opa = LV_OPA_COVER;
    win_dsc.border_color = lv_color_hex(0x000000);
    win_dsc.border_opa = LV_OPA_COVER;
    win_dsc.border_width = 1;
    lv_draw_rect(&layer, &win_dsc, &win);

    /* inner shadow: a 1px black line inset 2px along the top and left only */
    lv_draw_rect_dsc_t win_sh;
    lv_draw_rect_dsc_init(&win_sh);
    win_sh.bg_opa = LV_OPA_TRANSP;
    win_sh.border_color = lv_color_hex(0x000000);
    win_sh.border_opa = LV_OPA_COVER;
    win_sh.border_width = 1;
    win_sh.border_side = LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT;
    lv_area_t win_inner = { win.x1 + 2, win.y1 + 2, win.x2 - 2, win.y2 - 2 };
    lv_draw_rect(&layer, &win_sh, &win_inner);

    lv_draw_label_dsc_t hhmm_dsc;
    lv_draw_label_dsc_init(&hhmm_dsc);
    hhmm_dsc.text = hhmm;
    hhmm_dsc.color = lv_color_hex(0x000000);
    hhmm_dsc.font = &lv_font_montserrat_14;
    hhmm_dsc.align = LV_TEXT_ALIGN_CENTER;
    lv_area_t hhmm_area = { win.x1, win.y1 + 4, win.x2, win.y2 };
    lv_draw_label(&layer, &hhmm_dsc, &hhmm_area);

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(0x000000);
    line_dsc.opa = LV_OPA_COVER;
    line_dsc.p1.x = CLOCK_CENTER;
    line_dsc.p1.y = CLOCK_CENTER;

    double h_angle = ((hour % 12) * 30 + minute * 0.5 - 90) * M_PI / 180.0;
    line_dsc.width = 5;
    line_dsc.p2.x = CLOCK_CENTER + (int)(37 * cos(h_angle));
    line_dsc.p2.y = CLOCK_CENTER + (int)(37 * sin(h_angle));
    lv_draw_line(&layer, &line_dsc);

    double m_angle = (minute * 6 - 90) * M_PI / 180.0;
    line_dsc.width = 3;
    line_dsc.p2.x = CLOCK_CENTER + (int)(55 * cos(m_angle));
    line_dsc.p2.y = CLOCK_CENTER + (int)(55 * sin(m_angle));
    lv_draw_line(&layer, &line_dsc);

    double s_angle = (second * 6 - 90) * M_PI / 180.0;
    line_dsc.width = 2;
    line_dsc.p2.x = CLOCK_CENTER + (int)(62 * cos(s_angle));
    line_dsc.p2.y = CLOCK_CENTER + (int)(62 * sin(s_angle));
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
//   40..58   month title, flanked by the < / > browse arrows
//            (x 6..25 and 196..215 at y 40)
//   62..82   weekday header row: 7 fixed-width centred labels, "Mo".."Su"
//   82..202  day grid: a 6x7 array of individual day-cell labels, one per
//            cell (leading/trailing days render in a smaller font, so they
//            cannot share a multi-line label with the current month)
//   38..204  analog clock canvas (right side), digital HH:MM on the face
//   210..244 four day weather row
//   254..296 quote of the day
//
#define CAL_TITLE_Y       40
#define CAL_COL_X0        6
#define CAL_COL_W         30
#define CAL_ARROW_W       20
#define CAL_ROW_H         20    /* montserrat_14 line_height (16) + 4 */
#define CAL_ROWS          6     /* Monday-first months never need a 7th */
#define CAL_GRID_Y        62
#define CAL_GRID_W        (CAL_COL_W * 7)
#define CAL_GRID_BOTTOM   (CAL_GRID_Y + (CAL_ROWS + 1) * CAL_ROW_H)

/* CAL_CLOCK_X / CAL_CLOCK_Y are defined up with the CLOCK_* block, since the
   dial's dither helper needs them and it sits next to draw_clock_face(). */

#define CAL_FC_Y          210
#define CAL_FC_W          100
#define CAL_FC_TEXT_X     38
#define CAL_FC_ICON_SCALE 91    /* 90 px source -> ~32 px */

#define CAL_QUOTE_Y       254

static lv_obj_t *s_cal_cols[7];              /* header row: Mo..Su */
static lv_obj_t *s_cal_days[CAL_ROWS][7];    /* one label per day cell */
static lv_obj_t *s_cal_arrow_prev;
static lv_obj_t *s_cal_arrow_next;
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

/* A round month-browse button, shaded as a hemisphere bulging toward the
   viewer, lit from the upper-left, then thresholded through the Bayer
   matrix in screen space so it tiles with the rail. The glyph rides on a
   centred label child so the dither never sits behind text. Clears its
   whole canvas first - pixels outside the circle would otherwise stay
   uninitialised RGB565. */
static void draw_arrow_button(lv_obj_t *canvas, int screen_x, int screen_y, int size, const char *glyph)
{
    lv_canvas_fill_bg(canvas, lv_color_hex(0xffffff), LV_OPA_COVER);

    const int r = size / 2;
    const double lx = -0.5, ly = -0.5, lz = 0.7;   /* light from upper-left */
    const double llen = sqrt(lx * lx + ly * ly + lz * lz);

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            double dx = (x - r) / (double)r, dy = (y - r) / (double)r;
            double d2 = dx * dx + dy * dy;
            if (d2 > 1.0) continue;   /* outside the circle */
            double dz = sqrt(1.0 - d2);   /* hemisphere bulging toward the viewer */
            double dot = (dx * lx + dy * ly + dz * lz) / llen;
            uint8_t grey = (uint8_t)((dot * 0.5 + 0.5) * 255.0);
            lv_color_t c = Dither_Threshold(screen_x + x, screen_y + y, grey)
                ? lv_color_hex(0x000000) : lv_color_hex(0xffffff);
            lv_canvas_set_px(canvas, x, y, c, LV_OPA_COVER);
        }
    }

    lv_obj_t *l = lv_label_create(canvas);
    lv_obj_center(l);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text_static(l, glyph);
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

    static uint8_t s_arrow_prev_buf[LV_CANVAS_BUF_SIZE(CAL_ARROW_W, CAL_ARROW_W, 16, LV_DRAW_BUF_STRIDE_ALIGN)];
    static uint8_t s_arrow_next_buf[LV_CANVAS_BUF_SIZE(CAL_ARROW_W, CAL_ARROW_W, 16, LV_DRAW_BUF_STRIDE_ALIGN)];

    s_cal_arrow_prev = lv_canvas_create(obj);
    lv_canvas_set_buffer(s_cal_arrow_prev, s_arrow_prev_buf, CAL_ARROW_W, CAL_ARROW_W, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_cal_arrow_prev, CAL_COL_X0, CAL_TITLE_Y);
    draw_arrow_button(s_cal_arrow_prev, CAL_COL_X0, CAL_TITLE_Y, CAL_ARROW_W, "<");

    objects.cal_title = cal_add_label(obj, CAL_COL_X0 + CAL_ARROW_W, CAL_TITLE_Y, &lv_font_montserrat_16,
                                      CAL_GRID_W - 2 * CAL_ARROW_W, LV_TEXT_ALIGN_CENTER, "Calendar");

    s_cal_arrow_next = lv_canvas_create(obj);
    lv_canvas_set_buffer(s_cal_arrow_next, s_arrow_next_buf, CAL_ARROW_W, CAL_ARROW_W, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_cal_arrow_next, CAL_COL_X0 + CAL_GRID_W - CAL_ARROW_W, CAL_TITLE_Y);
    draw_arrow_button(s_cal_arrow_next, CAL_COL_X0 + CAL_GRID_W - CAL_ARROW_W, CAL_TITLE_Y, CAL_ARROW_W, ">");

    /* One label per weekday column for the header row; header text never
       mixes fonts, so it stays a single-line centred label per column. */
    static const char *DOW[7] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };
    for (int c = 0; c < 7; c++) {
        lv_obj_t *col = lv_label_create(obj);
        s_cal_cols[c] = col;
        lv_obj_set_pos(col, CAL_COL_X0 + c * CAL_COL_W, CAL_GRID_Y);
        lv_obj_set_size(col, CAL_COL_W, CAL_ROW_H);
        lv_obj_set_style_text_font(col, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(col, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(col, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text_static(col, DOW[c]);
    }

    /* One label per day cell, not one multi-line label per column: leading
       and trailing days (from the adjacent month) render in montserrat_12
       against the current month's montserrat_14, and a single lv_label
       cannot mix fonts within itself. */
    for (int r = 0; r < CAL_ROWS; r++) {
        for (int c = 0; c < 7; c++) {
            lv_obj_t *cell = lv_label_create(obj);
            s_cal_days[r][c] = cell;
            lv_obj_set_pos(cell, CAL_COL_X0 + c * CAL_COL_W, CAL_GRID_Y + (r + 1) * CAL_ROW_H);
            lv_obj_set_size(cell, CAL_COL_W, CAL_ROW_H);
            lv_obj_set_style_text_color(cell, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(cell, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(cell, "");
        }
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
            update_clock_hands(0, 0, 0);
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

void calendar_update_forecast(int idx, const char *date, const lv_image_dsc_t *icon, const char *temp)
{
    if (idx < 0 || idx >= 4 || !s_cal_fc_icon[idx]) return;
    lv_label_set_text(s_cal_fc_date[idx], date);
    lv_image_set_src(s_cal_fc_icon[idx], icon);
    lv_label_set_text(s_cal_fc_temp[idx], temp);
}

void update_calendar_display(int disp_year, int disp_month, const struct tm *today)
{
    static const char *MONTHS[] = { "January","February","March","April","May","June",
                                    "July","August","September","October","November","December" };

    if (!objects.calendar) return;

    lv_label_set_text_fmt(objects.cal_title, "%s %d", MONTHS[disp_month], disp_year);

    struct tm first = { .tm_year = disp_year - 1900, .tm_mon = disp_month, .tm_mday = 1, .tm_isdst = -1 };
    mktime(&first);
    int start_dow = (first.tm_wday + 6) % 7;    /* 0 = Monday */

    /* day 0 of the next month normalises to the last day of this one */
    struct tm last = { .tm_year = disp_year - 1900, .tm_mon = disp_month + 1, .tm_mday = 0, .tm_isdst = -1 };
    mktime(&last);
    int days_in_month = last.tm_mday;

    /* day 0 of *this* month normalises to the last day of the *previous*
       one - the same trick, one month earlier, for numbering leading days. */
    struct tm prev = { .tm_year = disp_year - 1900, .tm_mon = disp_month, .tm_mday = 0, .tm_isdst = -1 };
    mktime(&prev);
    int prev_days_in_month = prev.tm_mday;

    for (int r = 0; r < CAL_ROWS; r++) {
        for (int c = 0; c < 7; c++) {
            int day_offset = r * 7 + c - start_dow + 1;
            int shown_day;
            const lv_font_t *font;

            if (day_offset < 1) {
                shown_day = prev_days_in_month + day_offset;
                font = &lv_font_montserrat_12;
            } else if (day_offset > days_in_month) {
                shown_day = day_offset - days_in_month;
                font = &lv_font_montserrat_12;
            } else {
                shown_day = day_offset;
                font = &lv_font_montserrat_14;
            }

            lv_obj_set_style_text_font(s_cal_days[r][c], font, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_fmt(s_cal_days[r][c], "%d", shown_day);
        }
    }

    /* "Today" only makes sense - and is only shown - when the displayed
       month is the real current one. */
    if (disp_year == today->tm_year + 1900 && disp_month == today->tm_mon) {
        int idx = start_dow + today->tm_mday - 1;
        lv_obj_set_pos(objects.cal_today,
                       CAL_COL_X0 + (idx % 7) * CAL_COL_W + 2,
                       CAL_GRID_Y + (1 + idx / 7) * CAL_ROW_H - 1);
        lv_obj_remove_flag(objects.cal_today, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(objects.cal_today, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text_static(objects.quote, QUOTES[today->tm_yday % NUM_QUOTES]);

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