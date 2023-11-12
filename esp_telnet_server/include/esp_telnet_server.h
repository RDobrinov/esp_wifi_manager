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
/*
#define TS_LM_MODE          1
#define TS_LM_FORWARDMASK   2
#define TS_LM_SLC           3
#define TS_LM_EOF           236
#define TS_LM_SUPS          237
#define TS_LM_ABORT         238

#define TS_SLC_SYNCH        1
#define TS_SLC_BRK          2
#define TS_SLC_IP           3
#define TS_SLC_AO           4
#define TS_SLC_AYT          5
#define TS_SLC_EOR          6
#define TS_SLC_ABORT        7
#define TS_SLC_EOF          8
#define TS_SLC_SUSP         9
#define TS_SLC_EC          10
#define TS_SLC_EL          11
#define TS_SLC_EW          12
#define TS_SLC_RP          13
#define TS_SLC_LNEXT       14
#define TS_SLC_XON         15
#define TS_SLC_XOFF        16
#define TS_SLC_FORW1       17
#define TS_SLC_FORW2       18
#define TS_SLC_MCL         19
#define TS_SLC_MCR         20
#define TS_SLC_MCWL        21
#define TS_SLC_MCWR        22
#define TS_SLC_MCBOL       23
#define TS_SLC_MCEOL       24
#define TS_SLC_INSRT       25
#define TS_SLC_OVER        26
#define TS_SLC_ECR         27
#define TS_SLC_EWR         28
#define TS_SLC_EBOL        29
#define TS_SLC_EEOL        30

#define TS_SLC_DEFAULT      3
#define TS_SLC_VALUE        2
#define TS_SLC_CANTCHANGE   1
#define TS_SLC_NOSUPPORT    0
#define TS_SLC_LEVELBITS    3

#define TS_SLC_ACK        128
#define TS_SLC_FLUSHIN     64
#define TS_SLC_FLUSHOUT    32
*/
typedef enum {
    TL_CMD_START,
    TL_CMD_STOP,
    TL_CMD_SHUTDOWN
} tl_cmds_e;

typedef enum {
    TL_SRV_NOACT,
    TL_SRV_LISTEN,
    TL_SRV_CLIENT_CONNECTED
} tl_srv_state_t;

typedef enum {
    TLSRV_EVENT_SERVER_START,
    TLSRV_EVENT_SERVER_STOP,
    TLSRV_EVENT_CLIENT_CONNECT,
    TLSRV_EVENT_CLIENT_DISCONNECT,
    TLSRV_EVENT_LINE_TYPED
} tlsrv_event_t;

ESP_EVENT_DECLARE_BASE(TLSRV_EVENT);

typedef struct tl_queue_data {
    tl_cmds_e cmd;
} tl_queue_data_t;

esp_err_t tl_server_init(esp_event_loop_handle_t *p_uevent_loop);
QueueHandle_t tl_get_cmd_handle();

#endif /* _ESP_TELNET_SERVER_H_ */