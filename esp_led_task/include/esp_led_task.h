#ifndef _ESP_LED_TASK_H_
#define _ESP_LED_TASK_H_

#include "esp_system.h"
#include "driver/ledc.h"

//void pointer(wm_wifi_base_config_t *ptr) {
//    ptr = (wm_wifi_base_config_t *)realloc(ptr, 5);
//}

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_HIGH_SPEED_MODE
#define LEDC_OUTPUT_IO          (GPIO_NUM_19)
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY               (1024) // Set duty to 50%. ((2 ** 13) - 1) * 50% = 4095
#define LEDC_FREQUENCY          (5000) // Frequency in Hertz. Set frequency at 5 kHz

typedef struct lm_ledc_config {
    ledc_timer_config_t ledc_timer;
    ledc_channel_config_t ledc_channel;
} lm_ledc_config_t;

typedef struct lm_led_state {
    short intensity;
    short fade_time;
    short time;
} lm_led_state_t;

esp_err_t lm_init(lm_ledc_config_t *ledc_config);
void lm_apply_pgm(lm_led_state_t *led_state, size_t led_pgm_size);

#endif

