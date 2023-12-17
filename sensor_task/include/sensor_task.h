#include "base_def.h"
#include "esp_event.h"

typedef struct reg_sensor_event {
    sensor_handle_t sensor_entry_h;
    sensor_conf_t _config;
} reg_sensor_event_t;

typedef enum {
    SNSTSK_EVENT_TASK_CREATED,
    SNSTSK_EVENT_SENSOR_REGISTRED,
    SNSTSK_EVENT_REG_SENSOR,
    SNSTSK_EVENT_DEREG_SENSOR
} sensor_task_event_t;

ESP_EVENT_DECLARE_BASE(SNSTSK_EVENT);

esp_err_t sns_task_init(esp_event_loop_handle_t *snsvent_loop);
esp_event_loop_handle_t sns_task_get_elhandler(void);