#include "nav.h"
#include "selftest.h"
#include "sdkconfig.h"

#if CONFIG_CLOCK_SELFTEST

/* Main has 5 focusable elements, Cal+Clock has none until milestone 4. */
static uint8_t stub_focus_count(uint8_t screen)
{
    return screen == NAV_SCREEN_MAIN ? 5 : 0;
}

void Nav_RunTests(void)
{
    nav_state_t st;

    /* Select cycles screens and wraps */
    nav_init(&st, stub_focus_count, 4);
    SELFTEST_CHECK_INT(st.mode, NAV_SCREEN);
    SELFTEST_CHECK_INT(nav_handle(&st, BTN_EV_SELECT_SHORT), 1);
    SELFTEST_CHECK_INT(st.screen, NAV_SCREEN_CALCLOCK);
    nav_handle(&st, BTN_EV_SELECT_SHORT);
    SELFTEST_CHECK_INT(st.screen, NAV_SCREEN_MAIN);

    /* OK enters focus mode on a screen that has focusables */
    nav_init(&st, stub_focus_count, 4);
    SELFTEST_CHECK_INT(nav_handle(&st, BTN_EV_OK_SHORT), 1);
    SELFTEST_CHECK_INT(st.mode, NAV_FOCUS);
    SELFTEST_CHECK_INT(st.focus, 0);

    /* OK does nothing on a screen with no focusables */
    nav_init(&st, stub_focus_count, 4);
    nav_handle(&st, BTN_EV_SELECT_SHORT);              /* to Cal+Clock */
    SELFTEST_CHECK_INT(nav_handle(&st, BTN_EV_OK_SHORT), 0);
    SELFTEST_CHECK_INT(st.mode, NAV_SCREEN);

    /* Focus advances, wraps, and reports the wrap */
    nav_init(&st, stub_focus_count, 4);
    nav_handle(&st, BTN_EV_OK_SHORT);
    for (int i = 0; i < 4; i++) {
        nav_handle(&st, BTN_EV_SELECT_SHORT);
    }
    SELFTEST_CHECK_INT(st.focus, 4);
    SELFTEST_CHECK_INT(st.focus_wrapped, 0);
    nav_handle(&st, BTN_EV_SELECT_SHORT);
    SELFTEST_CHECK_INT(st.focus, 0);
    SELFTEST_CHECK_INT(st.focus_wrapped, 1);

    /* Long Select backs out of focus mode */
    SELFTEST_CHECK_INT(nav_handle(&st, BTN_EV_SELECT_LONG), 1);
    SELFTEST_CHECK_INT(st.mode, NAV_SCREEN);

    /* OK opens detail, both back gestures return to focus */
    nav_init(&st, stub_focus_count, 4);
    nav_handle(&st, BTN_EV_OK_SHORT);
    nav_handle(&st, BTN_EV_OK_SHORT);
    SELFTEST_CHECK_INT(st.mode, NAV_DETAIL);
    nav_handle(&st, BTN_EV_OK_SHORT);
    SELFTEST_CHECK_INT(st.mode, NAV_FOCUS);
    nav_handle(&st, BTN_EV_OK_SHORT);
    nav_handle(&st, BTN_EV_SELECT_LONG);
    SELFTEST_CHECK_INT(st.mode, NAV_FOCUS);

    /* Long OK reaches settings from every mode */
    const nav_mode_t from[] = { NAV_SCREEN, NAV_FOCUS, NAV_DETAIL };
    for (unsigned i = 0; i < sizeof(from) / sizeof(from[0]); i++) {
        nav_init(&st, stub_focus_count, 4);
        st.mode = from[i];
        SELFTEST_CHECK_INT(nav_handle(&st, BTN_EV_OK_LONG), 1);
        SELFTEST_CHECK_INT(st.mode, NAV_SETTINGS);
        SELFTEST_CHECK_INT(st.settings_row, 0);
    }

    /* Long OK inside settings is a no-op, not a re-entry */
    SELFTEST_CHECK_INT(nav_handle(&st, BTN_EV_OK_LONG), 0);
    SELFTEST_CHECK_INT(st.mode, NAV_SETTINGS);

    /* Settings rows wrap, and long Select always escapes */
    nav_init(&st, stub_focus_count, 3);
    nav_handle(&st, BTN_EV_OK_LONG);
    nav_handle(&st, BTN_EV_SELECT_SHORT);
    nav_handle(&st, BTN_EV_SELECT_SHORT);
    SELFTEST_CHECK_INT(st.settings_row, 2);
    nav_handle(&st, BTN_EV_SELECT_SHORT);
    SELFTEST_CHECK_INT(st.settings_row, 0);
    SELFTEST_CHECK_INT(nav_handle(&st, BTN_EV_SELECT_LONG), 1);
    SELFTEST_CHECK_INT(st.mode, NAV_SCREEN);

    /* BTN_EV_NONE never changes anything */
    nav_init(&st, stub_focus_count, 4);
    SELFTEST_CHECK_INT(nav_handle(&st, BTN_EV_NONE), 0);
}

#else
void Nav_RunTests(void) {}
#endif
