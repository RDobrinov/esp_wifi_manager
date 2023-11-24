#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_led_task.h"
#include "esp_log.h"
#include <string.h>

typedef struct lm_led {
    TaskHandle_t led_task_handle;
    ledc_mode_t speed_mode;
    ledc_channel_t channel;
    SemaphoreHandle_t xSemaphore;
    TickType_t cycle_start;
    TickType_t cycle_interval;
    uint8_t uIndex;
    lm_led_state_t *led_pgm;
} lm_led_t;

static lm_led_t *lm = NULL;

static void vLedTask(void *pvParameters);
static esp_err_t lm_update_state(lm_led_state_t *leds);

const char *tag ="ledtask";

static void vLedTask(void *pvParameters) {
    ESP_LOGI(tag, "vLedTask running");
    while(true)
    {
        if(xSemaphoreTake(lm->xSemaphore, (TickType_t) 1) == pdTRUE) {
            if(lm->led_pgm) {
                if(lm->led_pgm[lm->uIndex].intensity == -1 ) {
                    if((xTaskGetTickCount() - lm->cycle_start) > lm->cycle_interval) {
                        lm->uIndex = 0;
                    }
                }
                if(lm->uIndex == 0 ) {
                    lm_update_state(&lm->led_pgm[lm->uIndex]);
                    lm->uIndex++;
                } else {
                    if((xTaskGetTickCount() - lm->cycle_start) > lm->cycle_interval) {
                        lm_update_state(&lm->led_pgm[lm->uIndex]);
                        lm->uIndex++;
                    }
                }
            }
            xSemaphoreGive(lm->xSemaphore);
            vTaskDelay(1);
        }
    }
}

static esp_err_t lm_update_state(lm_led_state_t *leds) {
    lm->cycle_interval = leds->time + leds->fade_time;
    esp_err_t err;
    if(leds->fade_time == 0) {
        err = ledc_set_duty_and_update(lm->speed_mode, lm->channel, leds->intensity, 0);
    } else {
        err = ledc_set_fade_time_and_start(lm->speed_mode, lm->channel, leds->intensity, leds->fade_time, LEDC_FADE_NO_WAIT);
    }
    lm->cycle_start = xTaskGetTickCount();
    lm->cycle_interval /= portTICK_PERIOD_MS;
    return err;
}

esp_err_t lm_init(lm_ledc_config_t *ledc_config) {
    lm = (lm_led_t *)calloc(1, sizeof(lm_led_t));
    //lm->led_config = (lm_ledc_config_t *)calloc(1, sizeof(lm_ledc_config_t));
    lm->led_pgm = NULL;
    if(ledc_config) {
        lm->speed_mode = ledc_config->ledc_channel.speed_mode;
        lm->channel = ledc_config->ledc_channel.channel;
        ledc_timer_config(&ledc_config->ledc_timer);
        ledc_channel_config(&ledc_config->ledc_channel);
    } else {
        lm->speed_mode = LEDC_HIGH_SPEED_MODE;
        lm->channel = LEDC_CHANNEL_0;
        lm_ledc_config_t *defcfg = (lm_ledc_config_t *)calloc(1, sizeof(lm_ledc_config_t));
        defcfg->ledc_timer = (ledc_timer_config_t) {
            .speed_mode       = LEDC_HIGH_SPEED_MODE,
            .timer_num        = LEDC_TIMER_0,
            .duty_resolution  = LEDC_TIMER_12_BIT,
            .freq_hz          = 16000,              // Set output frequency at 16kHz
            .clk_cfg          = LEDC_AUTO_CLK    
        };
        defcfg->ledc_channel = (ledc_channel_config_t) {
            .speed_mode     = LEDC_HIGH_SPEED_MODE,
            .channel        = LEDC_CHANNEL_0,
            .timer_sel      = LEDC_TIMER_0,         
            .intr_type      = LEDC_INTR_DISABLE,    // No interupts
            .gpio_num       = GPIO_NUM_19,          // TTGO T7 v1.4 green led pin
            .duty           = 0,                    // Set duty to 0% - Led off
            .hpoint         = 0                     // High point to 0 mean latch high at counter overflow
        };
        ledc_timer_config(&defcfg->ledc_timer);
        ledc_channel_config(&defcfg->ledc_channel);
        free(defcfg);
        lm->xSemaphore = xSemaphoreCreateBinary();
        lm->uIndex = 0;
        xSemaphoreGive(lm->xSemaphore);
        xTaskCreate(vLedTask, "ledctrl", 2048, NULL, 12, &lm->led_task_handle);
    }
    ledc_fade_func_install(0);
    return ESP_OK;
}

void lm_apply_pgm(lm_led_state_t *led_state, size_t led_state_size) {
    if(!lm) {
        ESP_LOGE(tag,"not_init");
        return;
    }
    if(led_state) {
        if(xSemaphoreTake(lm->xSemaphore, (TickType_t) 2) == pdTRUE) {
            free(lm->led_pgm);
            lm->led_pgm = (lm_led_state_t *)malloc(led_state_size);
            lm->uIndex = 0;
            memcpy(lm->led_pgm, led_state, led_state_size);
            xSemaphoreGive(lm->xSemaphore);
        } else {
            ESP_LOGE(tag,"blocked");
            return;
        }
    }
    return;
}