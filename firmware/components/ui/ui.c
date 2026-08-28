#include "ui.h"
#include "screens.h"
#include "images.h"
#include "actions.h"
#include "vars.h"
#include "overlay.h"

#include <string.h>

static int16_t currentScreen = -1;

static lv_obj_t *getLvglObjectFromIndex(int32_t index) {
    if (index == -1) {
        return 0;
    }
    return ((lv_obj_t **)&objects)[index];
}

void loadScreen(enum ScreensEnum screenId) {
    currentScreen = screenId - 1;
    lv_obj_t *screen = getLvglObjectFromIndex(currentScreen);
    /* No fade: the panel is 1 bit, so every intermediate opacity frame
       thresholds to a solid flash, and the manual refresh loop only runs
       lv_timer_handler once per second so the animation stalls half way. */
    lv_screen_load(screen);
}

void ui_init() {
    create_screens();
    Overlay_Create();
    loadScreen(SCREEN_ID_INIT);

}

void ui_tick() {
    tick_screen(currentScreen);
}
