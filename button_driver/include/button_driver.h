/**
 * 
*/
#ifndef _BUTTON_DRIVER_H_
#define _BUTTON_DRIVER_H_

#include "esp_event.h"
#include "gpio_driver.h"

/**
 * @brief Type of single button configuration
*/
typedef union {
    struct {
        uint32_t btn_gpio_num:8;    /*!< GPIO_NUM_XX for button */
        uint32_t button_id:16;      /*!< Button ID              */
        uint32_t reserved24_27:4;   /*!< Reserved not used      */
        uint32_t def_state:1;       /*!< Default button state   */
        uint32_t btn_gpio_mode:3;   /*!< GPIO mode              */
    };
    uint32_t val;                   /*!< Container value        */
} btn_drv_init_config_t;

/**
 * @brief Type of button control and notification event IDs
*/
typedef enum {
    BTNDRV_EVENT_TASK_CREATED,          /*!< [out] Send when driver task created          */
    BTNDRV_EVENT_BUTTON_REGISTRED,      /*!< [out] Send when driver registred a button    */
    BTNDRV_EVENT_BUTTON_DEREGISTRED,    /*!< [out] Send when driver de-registred a button */
    BTNDRV_EVENT_BUTTON_PRESS,          /*!< [out] Send when button is pressed            */
    BTNDRV_EVENT_BUTTON_RELEASE,        /*!< [out] Send when button is released           */
    BTNDRV_EVENT_REG_BUTTON,            /*!< [in] Request a button registration           */
    BTNDRV_EVENT_DEREG_BUTTON,          /*!< [in] Request a button de-registration        */
    BTNDRV_EVENT_REG_FAILED,            /*!< [out] Send when button registration failed   */
    BTNDRV_EVENT_DEREG_FAILED,          /*!< [out] Send when button de-registration failed*/
    BTN_CLICK_EVENT,                    /*!< [out] Button clicked once                    */
    BTN_DBL_CLICK_EVENT,                /*!< [out] Button clicked twice                   */
    BTN_TRPL_CLICK_EVENT,               /*!< [out] Triple click                           */
    BTN_LONG_CLICK_EVENT,               /*!< [out] Long button click                      */
    BTN_LLONG_CLICK_EVENT,              /*!< [out] Very long button click                 */
    BTN_MULTI_CLICK_EVENT,              /*!< [out] Monkey onboard - multiple clicks       */
    BTN_HOLD_EVENT                      /*!< [out] Holding button pressed for long time   */
} btn_drv_task_event_t;

ESP_EVENT_DECLARE_BASE(BTNDRV_EVENT);

/**
 * @brief Create and init driver task
 * 
 * @param[in] btndrv_evt_loop Pointer to event loop handler for communication
 *                            if NULL registred handler and send events to default loop
 * @return 
 *      - NONE
*/
void btn_drv_init(esp_event_loop_handle_t *btndrv_evt_loop);

#endif /* _BUTTON_DRIVER_H_ */