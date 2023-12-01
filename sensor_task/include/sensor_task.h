#include "base_def.h"

typedef struct reg_sensor_event {
    sensor_handle_t sensor_entry_h;
    union sensor_conf_t _config;
} reg_sensor_event_t;

typedef enum {
    SNSTSK_EVENT_REG_SENSOR,
    SNSTSK_EVENT_DEREG_SENSOR
} sensor_task_event_t;

esp_err_t sns_task_init(void);