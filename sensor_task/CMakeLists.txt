idf_component_register(
    SRCS "src/sensor_task.c" "src/virtual_sensor.c"
    INCLUDE_DIRS "include"
    REQUIRES driver
)