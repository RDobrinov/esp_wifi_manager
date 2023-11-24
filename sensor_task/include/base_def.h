
typedef enum {
    NONE = 0,
    TEMPERATURE,
    HUMIDITY,
    PRESSURE,
    MAX
} sensor_magnitude_t;

typedef enum {
    INIT,
    PROCESS,
    STATUS,
    GET_DESC,
    GET_MAG,
    GET_COUNT
} sensor_drv_tsk_t;