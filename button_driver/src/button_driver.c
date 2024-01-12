
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_log.h"

#include "button_driver.h"

typedef union {
    struct {
        uint32_t ready:1;
        uint32_t last_state:1;
        uint32_t clicks:3;
        uint32_t reserved5_32:28;
    };
    uint32_t val;
} btn_drv_button_status_t;

typedef struct btn_drv_ll_data {
    btn_drv_init_config_t _config;  /*!< Init configuration */
    TickType_t press;               /*!< Press tick value   */
    TickType_t release;             /*!< Release tick value */
    TickType_t last_change;         /*!< Last change value  */
    btn_drv_button_status_t status; /*!< Button status      */
} btn_drv_ll_data_t;

/**
 * @brief Type of single linked list node
*/
typedef struct btn_drv_ll_node {
    btn_drv_ll_data_t _node_data;           /*!< Single node payload                */
    struct btn_drv_ll_node *_next_node;     /*!< Pointer to next node in linked list*/
} btn_drv_ll_node_t;

/**
 * @brief Type of dirver configuration and state
*/
typedef struct btn_drv_config {
    btn_drv_ll_node_t *head_node;           /*!< Linked List head node pointer      */
    btn_drv_ll_node_t *tail_node;           /*!< Linked List tail node pointer      */
    esp_event_loop_handle_t uevent_loop;    /*!< Button driver control event loop   */
    TaskHandle_t btndrv_task_handle;        /*!< Button driver task handle          */
    SemaphoreHandle_t x_LL_Semaphore;       /*!< Linked List change semaphore       */
} btn_drv_config_t;

ESP_EVENT_DEFINE_BASE(BTNDRV_EVENT);

static btn_drv_config_t *tsk_conf = NULL;

static void vBtnDrvTask(void *pvParameters);
static esp_err_t _event_post(int32_t event_id, const void *event_data, size_t event_data_size);
static void _event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static uint32_t _get_new_button_id(uint32_t req_id);

/**
 * @brief Driver task main function
 * 
 * @param[in] pvParameters [NOT USED] Pointer to task parameters
 * @return 
 *      - None
*/
static void vBtnDrvTask(void *pvParameters) {
    _event_post(BTNDRV_EVENT_TASK_CREATED, NULL, 0);
    while(true) {
        if(tsk_conf->head_node) {
            /* Button list is not empty */
            if(xSemaphoreTake(tsk_conf->x_LL_Semaphore, (TickType_t) 1) == pdTRUE) {
                btn_drv_ll_node_t *ll_node = tsk_conf->head_node;
                while(ll_node) {
                    if(!ll_node->_node_data.status.ready) {
                        /* Not ready. Init GPIO */
                        gpio_config_t *io_conf = (gpio_config_t *)malloc(sizeof(gpio_config_t));
                        io_conf->intr_type = GPIO_INTR_DISABLE;
                        io_conf->mode = GPIO_MODE_INPUT;
                        io_conf->pin_bit_mask = BIT64(ll_node->_node_data._config.btn_gpio_num);
                        io_conf->pull_down_en = (ll_node->_node_data._config.btn_gpio_mode == GPIO_PULLDOWN_ONLY);
                        io_conf->pull_up_en = (ll_node->_node_data._config.btn_gpio_mode == GPIO_PULLUP_ONLY);
                        gpio_config(io_conf);
                        free(io_conf);
                        ll_node->_node_data.status.last_state = gpio_get_level(ll_node->_node_data._config.btn_gpio_num);
                        ll_node->_node_data.status.ready = (ll_node->_node_data.status.last_state == ll_node->_node_data._config.def_state);
                    } else {
                        /* Ready. Looking for state changes */
                        int btn_state = gpio_get_level(ll_node->_node_data._config.btn_gpio_num);
                        TickType_t current_tick = xTaskGetTickCount();
                        if((btn_state != ll_node->_node_data.status.last_state) && ((current_tick - ll_node->_node_data.last_change) > 5)) {
                            /* Button changed its last state */
                            ll_node->_node_data.status.last_state = btn_state;
                            #if (CONFIG_BTNDRV_EVENT_SEND_EXTENDED_INFO == 1)
                            _event_post((btn_state == ll_node->_node_data._config.def_state)? BTNDRV_EVENT_BUTTON_RELEASE : BTNDRV_EVENT_BUTTON_PRESS, &(ll_node->_node_data._config), sizeof(btn_drv_init_config_t));
                            #endif
                            /* Determinate change type (press/release)*/
                            if(btn_state == ll_node->_node_data._config.def_state) {
                                /* Release occur */
                                ll_node->_node_data.release = current_tick;
                                uint32_t press_release = (uint32_t)ll_node->_node_data.release - (uint32_t)ll_node->_node_data.press;
                                /* Checking for long click */
                                if(press_release > CONFIG_BTNDRV_LLONG_CLICK_HOLD_TIME) {
                                    _event_post(BTN_LLONG_CLICK_EVENT, &(ll_node->_node_data._config), sizeof(btn_drv_init_config_t));
                                } else {
                                    if(press_release > CONFIG_BTNDRV_LONG_CLICK_HOLD_TIME) {
                                        _event_post(BTN_LONG_CLICK_EVENT, &(ll_node->_node_data._config), sizeof(btn_drv_init_config_t));
                                    } else {
                                        /* Not a long click, click counter incremented 
                                         * and monkey onboard event detected and filtered
                                        */
                                        if(ll_node->_node_data.status.clicks<4) ll_node->_node_data.status.clicks++;
                                    }
                                }
                                ll_node->_node_data.press = 0UL;
                            } else {
                                /* Press occur */
                                ll_node->_node_data.press = current_tick;
                                ll_node->_node_data.release = 0UL;
                            }
                            ll_node->_node_data.last_change = current_tick;
                        } else {
                            /* No state change occur. Check and send a HOLD event if any */
                            if((btn_state != ll_node->_node_data._config.def_state) && ((current_tick - ll_node->_node_data.press) > CONFIG_BTNDRV_BUTTON_HOLD_HOLD_TIME))
                            {
                                _event_post(BTN_HOLD_EVENT, &(ll_node->_node_data._config), sizeof(btn_drv_init_config_t));
                                ll_node->_node_data.press = current_tick;
                            }
                        }
                        /* Button change processing finished, process click(s) event if any*/
                        if((current_tick - ll_node->_node_data.last_change > CONFIG_BTNDRV_EVENT_SEND_TIMEOUT) && (ll_node->_node_data.status.clicks)) {
                            _event_post((1 == ll_node->_node_data.status.clicks) ? BTN_CLICK_EVENT 
                                    : (2 == ll_node->_node_data.status.clicks) ? BTN_DBL_CLICK_EVENT 
                                    : (3 == ll_node->_node_data.status.clicks) ? BTN_TRPL_CLICK_EVENT 
                                    : BTN_MULTI_CLICK_EVENT, &(ll_node->_node_data._config), sizeof(btn_drv_init_config_t));
                            ll_node->_node_data.status.clicks = 0UL;
                        }
                    }
                    /* Node processing finished, going to next node */
                    ll_node = ll_node->_next_node;
                }
                /* Last node reached, release lock and return control */
                xSemaphoreGive(tsk_conf->x_LL_Semaphore);
                vTaskDelay(1);
            }
        } else { vTaskDelay(2); }   /* Button list is empty so do nothing */
        //vTaskDelay(1);
    }
}

/**
 * @brief Driver event post function
 * 
 * @param[in] event_id Event ID to post
 * @param[in] event_data Pointer to event data to post
 * @param[in] event_data_size Event data size 
 * @return 
 *      - None
*/
static esp_err_t _event_post(int32_t event_id, const void *event_data, size_t event_data_size) {
    return (tsk_conf->uevent_loop) ? esp_event_post_to(tsk_conf->uevent_loop, BTNDRV_EVENT, event_id, event_data, event_data_size, 1)
                                  : esp_event_post(BTNDRV_EVENT, event_id, event_data, event_data_size, 1);
}

/**
 * @brief Driver event handler
 * 
 * @param[in] arg Pointer to event argument
 * @param[in] event_base Event base
 * @param[in] event_id Event ID
 * @param[in] event_data Pointer to event data
 * @return 
 *      - None
*/
static void _event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if(BTNDRV_EVENT == event_base) {
        if(BTNDRV_EVENT_REG_BUTTON == event_id) {
            btn_drv_ll_node_t *new_node = (btn_drv_ll_node_t *)calloc(1, sizeof(btn_drv_ll_node_t));
            new_node->_node_data._config.val = ((btn_drv_init_config_t *)event_data)->val;
            if(new_node->_node_data._config.btn_gpio_mode == GPIO_PULLUP_PULLDOWN) new_node->_node_data._config.btn_gpio_mode = GPIO_PULLUP_ONLY;
            new_node->_node_data._config.button_id = 0;            
            if(gpio_drv_reserve(((btn_drv_init_config_t *)event_data)->btn_gpio_num)) {
                new_node->_node_data._config.button_id = _get_new_button_id(((btn_drv_init_config_t *)event_data)->button_id);
            } else {
                /* GPIO already claimed */
                new_node->_node_data._config.btn_gpio_num = GPIO_NUM_MAX;
            }
            if((new_node->_node_data._config.btn_gpio_num == GPIO_NUM_MAX) || (new_node->_node_data._config.button_id == 0)) {
                free(new_node);
                new_node = NULL;
                _event_post(BTNDRV_EVENT_REG_FAILED, event_data, sizeof(btn_drv_init_config_t));
                return;
            }
            new_node->_node_data.status.val = 0UL;
            /* Add new node to linked list structure */
            if(xSemaphoreTake(tsk_conf->x_LL_Semaphore, (TickType_t) 2) == pdTRUE) {
                if(!tsk_conf->head_node) {
                    tsk_conf->head_node = new_node;
                    tsk_conf->head_node->_next_node = NULL;
                    tsk_conf->tail_node = tsk_conf->head_node;
                } else {
                    tsk_conf->tail_node->_next_node = new_node;
                    tsk_conf->tail_node = tsk_conf->tail_node->_next_node;
                    tsk_conf->tail_node->_next_node = NULL;
                }
                xSemaphoreGive(tsk_conf->x_LL_Semaphore);
                ((btn_drv_init_config_t *)event_data)->button_id = new_node->_node_data._config.button_id;
                _event_post(BTNDRV_EVENT_BUTTON_REGISTRED, event_data, sizeof(btn_drv_init_config_t));
            }
        }
        if(BTNDRV_EVENT_DEREG_BUTTON == event_id) {
            if(!tsk_conf->head_node) return;
            btn_drv_ll_node_t *ll_node = tsk_conf->head_node;
            btn_drv_ll_node_t *prev_ll_node = NULL;
            bool found = false;
            while(ll_node) {
                found = (((btn_drv_init_config_t *)event_data)->button_id != 0) ? (ll_node->_node_data._config.button_id == ((btn_drv_init_config_t *)event_data)->button_id) 
                        : (((btn_drv_init_config_t *)event_data)->btn_gpio_num == ll_node->_node_data._config.btn_gpio_num);
                if(found) {
                    if(prev_ll_node) {
                        prev_ll_node->_next_node = ll_node->_next_node;
                        if(!prev_ll_node->_next_node) tsk_conf->tail_node = prev_ll_node; 
                    } else {
                        tsk_conf->head_node = ll_node->_next_node;
                    }
                    *((btn_drv_init_config_t *)event_data) = ll_node->_node_data._config;
                    free(ll_node);
                    ll_node = NULL;
                } else { 
                    prev_ll_node = ll_node;
                    ll_node = ll_node->_next_node;
                }
            }
            _event_post((found) ? BTNDRV_EVENT_BUTTON_DEREGISTRED : BTNDRV_EVENT_DEREG_FAILED, event_data, sizeof(btn_drv_init_config_t));
        }
        /*
        btn_drv_ll_node_t *ll_node = tsk_conf->head_node;
        while(ll_node) {
            ESP_LOGE("bdrv", "[HN_%p]:[TN_%p]:[NN_%p][%04X]:GPIO%02d", (tsk_conf->head_node), (tsk_conf->tail_node), ll_node->_next_node, ll_node->_node_data._config.button_id, ll_node->_node_data._config.btn_gpio_num);
            ll_node = ll_node->_next_node;
        }
        */
    }
    return;
}

/**
 * @brief Check or generate new button id
 * 
 * @param[in] req_id Requested ID. If 0 generate new one
 * @return 
 *      - New ID or 0 if requested id already exist
*/
static uint32_t _get_new_button_id(uint32_t req_id) {
    uint32_t newbtn_id = req_id;
    int i = !!(req_id);
    while( i<2 ) {
        if(newbtn_id == 0) {
            newbtn_id = (esp_random() & 0xFFFF);
        }
        btn_drv_ll_node_t *ll_node = tsk_conf->head_node;
        bool not_found = true;
        while( not_found && ll_node) {
            not_found = (ll_node->_node_data._config.button_id != newbtn_id);
            ll_node = ll_node->_next_node;
        }
        if(!not_found) {
            newbtn_id = 0;
            i++;
        } else i=2;
    }
    return newbtn_id;
}

void btn_drv_init(esp_event_loop_handle_t *btndrv_evt_loop) {
    gpio_drv_init();
    if(!tsk_conf) {
        tsk_conf = (btn_drv_config_t *)calloc(1, sizeof(btn_drv_config_t));
        if(tsk_conf) {
            tsk_conf->head_node = NULL;
            tsk_conf->tail_node = NULL;
            tsk_conf->uevent_loop = (btndrv_evt_loop) ? *btndrv_evt_loop : NULL;
            if(tsk_conf->uevent_loop) {
                /* Register internal handler to user loop */
                esp_event_handler_instance_register_with(tsk_conf->uevent_loop, BTNDRV_EVENT, BTNDRV_EVENT_REG_BUTTON, _event_handler, NULL, NULL);
                esp_event_handler_instance_register_with(tsk_conf->uevent_loop, BTNDRV_EVENT, BTNDRV_EVENT_DEREG_BUTTON, _event_handler, NULL, NULL);
            } else {
                /* Register internal handler to default loop */
                esp_event_handler_instance_register(BTNDRV_EVENT, BTNDRV_EVENT_REG_BUTTON, _event_handler, NULL, NULL);
                esp_event_handler_instance_register(BTNDRV_EVENT, BTNDRV_EVENT_DEREG_BUTTON, _event_handler, NULL, NULL);
            }
            tsk_conf->x_LL_Semaphore = xSemaphoreCreateBinary();
            xSemaphoreGive(tsk_conf->x_LL_Semaphore);
            xTaskCreate(vBtnDrvTask, "btndrvtsk", 2048, NULL, 15, &tsk_conf->btndrv_task_handle);
        }
    }
}
