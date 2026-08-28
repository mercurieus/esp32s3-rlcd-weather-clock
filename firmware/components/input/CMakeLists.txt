idf_component_register(
    SRCS "buttons.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES driver esp_hw_support
    REQUIRES ui
)
