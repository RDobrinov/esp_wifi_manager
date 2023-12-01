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
    TaskHandle_t sensor_task_handle;
    SemaphoreHandle_t x_LL_Semaphore;
} snstsk_config_t;

static snstsk_config_t *tsk_conf = NULL;

static void vSensorTask(void *pvParameters);

static void vSensorTask(void *pvParameters) {
    ESP_LOGI("snstsk", "vSensorTask running");
    while(true) {
        if()
        vTaskDelay(2);
    };
}

esp_err_t sns_task_init(void) {
    if(tsk_conf) return ESP_OK;
    tsk_conf = (snstsk_config_t *)calloc(1, sizeof(snstsk_config_t));
    if(!tsk_conf) return ESP_ERR_NO_MEM;
    tsk_conf->head_node = NULL;
    tsk_conf->tail_node = NULL;
    tsk_conf->x_LL_Semaphore = xSemaphoreCreateBinary();
    xSemaphoreGive(tsk_conf->x_LL_Semaphore);
    xTaskCreate(vSensorTask, "snstsk", 2048, NULL, 15, &tsk_conf->sensor_task_handle);
    return ESP_OK;
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