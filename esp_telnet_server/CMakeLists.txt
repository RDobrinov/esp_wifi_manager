idf_component_register(
    SRCS "src/esp_telnet_server.c" "src/libtelnet.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_event
)