idf_component_register(
    SRCS
        "images.c"
        "nav.c"
        "nav_test.c"
        "overlay.c"
        "screens.c"
        "styles.c"
        "ui.c"
        "ui_font_saira200.c"
        "ui_font_saira_condensed_bold200.c"
        "ui_image_icon_cloud.c"
        "ui_image_icon_partly_cloudy.c"
        "ui_image_icon_rain.c"
        "ui_image_icon_snow.c"
        "ui_image_icon_storm.c"
        "ui_image_icon_sun.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
    PRIV_REQUIRES selftest
)
