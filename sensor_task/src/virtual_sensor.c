#include "esp_log.h"
#include "base_def.h"

static sensor_magnitude_t magnitude = TEMPERATURE;

void virtual_sensor(sensor_drv_tsk_t task, void *data) {
    ESP_LOGI("vst", "inside driver function");
    
}