idf_component_register(
    SRCS "src/button_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES gpio_driver esp_event
)
