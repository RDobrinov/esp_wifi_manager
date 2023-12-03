#include "sensor_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

typedef struct sns_ll_data {
    sensor_handle_t _handler;
} sns_ll_data_t;

typedef struct sns_ll_node {
    sns_ll_data_t _data;
    struct sns_ll_node *_next;
} sns_ll_node_t;

typedef struct snstsk_config {
    sns_ll_node_t *head_node;
    sns_ll_node_t *tail_node;
    esp_event_loop_handle_t uevent_loop;
    TaskHandle_t sensor_task_handle;
    SemaphoreHandle_t x_LL_Semaphore;
} snstsk_config_t;

ESP_EVENT_DEFINE_BASE(SNSTSK_EVENT);

static snstsk_config_t *tsk_conf = NULL;

static void vSensorTask(void *pvParameters);
static void _event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static esp_err_t _event_post(int32_t event_id, const void *event_data, size_t event_data_size);

static void vSensorTask(void *pvParameters) {
    _event_post(SNSTSK_EVENT_TASK_CREATED, NULL, 0);
    while(true) {
        if(!tsk_conf->head_node) {
            
        }
        vTaskDelay(2);
    };
}

static void _event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if(SNSTSK_EVENT == event_base) {
        if(SNSTSK_EVENT_REG_SENSOR == event_id) {
            if(!tsk_conf->head_node) {
                tsk_conf->head_node = (sns_ll_node_t *)malloc(sizeof(sns_ll_node_t));
                tsk_conf->head_node->_next = NULL;
                tsk_conf->tail_node = tsk_conf->head_node;
            } else {
                tsk_conf->tail_node->_next = (sns_ll_node_t *)malloc(sizeof(sns_ll_node_t));
                tsk_conf->tail_node = tsk_conf->tail_node->_next;
                tsk_conf->tail_node = NULL;
            }
            _event_post(SNSTSK_EVENT_SENSOR_REGISTRED, NULL, 0);
        }
    }
}

static esp_err_t _event_post(int32_t event_id, const void *event_data, size_t event_data_size)
{
    return (tsk_conf->uevent_loop) ? esp_event_post_to(tsk_conf->uevent_loop, SNSTSK_EVENT, event_id, event_data, event_data_size, 1)
                                  : esp_event_post(SNSTSK_EVENT, event_id, event_data, event_data_size, 1);
}

esp_err_t sns_task_init(esp_event_loop_handle_t *uevent_loop) {
    if(tsk_conf) return ESP_OK;
    tsk_conf = (snstsk_config_t *)calloc(1, sizeof(snstsk_config_t));
    if(!tsk_conf) return ESP_ERR_NO_MEM;
    tsk_conf->head_node = NULL;
    tsk_conf->tail_node = NULL;
    if(uevent_loop) {
        tsk_conf->uevent_loop = *uevent_loop;
        esp_event_handler_instance_register_with(tsk_conf->uevent_loop, SNSTSK_EVENT, SNSTSK_EVENT_REG_SENSOR, _event_handler, NULL, NULL);
    } else {
        tsk_conf->uevent_loop = NULL;
        esp_event_handler_instance_register(SNSTSK_EVENT, SNSTSK_EVENT_REG_SENSOR, _event_handler, NULL, NULL);
    }
    tsk_conf->x_LL_Semaphore = xSemaphoreCreateBinary();
    xSemaphoreGive(tsk_conf->x_LL_Semaphore);
    xTaskCreate(vSensorTask, "snstsk", 2048, NULL, 15, &tsk_conf->sensor_task_handle);
    return ESP_OK;
}

esp_event_loop_handle_t sns_task_get_elhandler(void) {
    return tsk_conf->uevent_loop;
}

/*
    typedef struct ll_data {
        //sensor_handle_t _handler;
        int a;
        char b;
    } ll_data_t;

    typedef struct ll_node {
        ll_data_t _data;
        struct ll_node *_next;
    } ll_node_t;

    ll_node_t *first = (ll_node_t *)calloc(1, sizeof(ll_node_t));
    ll_node_t *work = first;
    first->_data.a = 1001;
    first->_next = NULL;
    ll_node_t *nextone;
    for(int i = 0; i<4; i++) {
        nextone = (ll_node_t *)calloc(1, sizeof(ll_node_t));
        work->_next = nextone;
        nextone->_data.a = 1002+i;
        nextone->_next = NULL;
        work = nextone;
    }
    work = first;
    do {
        printf("ll element %i at %p\n", work->_data.a, work->_next);
        work = work->_next;
    } while(work->_next);
*/