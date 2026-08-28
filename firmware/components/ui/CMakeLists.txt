file(GLOB_RECURSE ui_srcs "*.c")

idf_component_register(
    SRCS ${ui_srcs}
    INCLUDE_DIRS "."
    REQUIRES lvgl
    PRIV_REQUIRES selftest
)