#ifndef _ESP_TELNET_SERVER_H_
#define _ESP_TELNET_SERVER_H_

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

//#include "esp_netif.h"
//#include "lwip/ip4_addr.h"
//#include "esp_netif_types.h"
//#include "../lwip/esp_netif_lwip_internal.h"

/* WiFi */
//#include "esp_wifi.h"
//#include "nvs_flash.h"
#include "esp_event.h"
//#include "freertos/event_groups.h"

typedef enum {
    TL_START,
    TL_STOP,
    TL_SHUTDOWN
} tl_cmds_e;

typedef enum {
    TL_SRV_NOACT,
    TL_SRV_LISTEN,
    TL_SRV_CLIENT_CONNECTED
} tl_srv_state_t;

typedef struct tl_queue_data {
    tl_cmds_e cmd;
} tl_queue_data_t;

//void tl_server_init(esp_event_loop_handle_t *uevent_handle, esp_event_base_t event_base, int32_t event_id);
//void tl_();
esp_err_t tl_server_init();
QueueHandle_t tl_get_cmd_handle();

#endif /* _ESP_TELNET_SERVER_H_ */