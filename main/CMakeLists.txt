idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "./"
    PRIV_REQUIRES selftest esp_timer port_bsp app_bsp ui clock soc driver)
