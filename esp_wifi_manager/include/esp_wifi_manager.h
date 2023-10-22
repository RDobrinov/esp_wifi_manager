#ifndef _WIFI_MANAGER_H_
#define _WIFI_MANAGER_H_

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

#include "esp_netif.h"
#include "lwip/ip4_addr.h"
#include "esp_netif_types.h"
#include "../lwip/esp_netif_lwip_internal.h"

/* WiFi */
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "freertos/event_groups.h"
/**/

#define WIFIMGR_MAX_KNOWN_AP 5

typedef struct wifi_base_config {
    char wifi_ssid[32];
    char wifi_password[64];
    esp_netif_ip_info_t static_ip;
    esp_ip4_addr_t dns_server;
} wifi_base_config_t;

typedef struct wifi_connection_data {
    wifi_base_config_t known_networks[WIFIMGR_MAX_KNOWN_AP];
    wifi_base_config_t ap_mode;
    wifi_country_t country;
    uint8_t wifi_ap_channel;
    uint8_t wifi_max_sta_retry;
} wifi_connection_data_t;

void init_wifi_connection_data( wifi_connection_data_t *pWifiConn );
void init_base_config( wifi_base_config_t *base_conf );
void wm_change_ap_mode_config( wifi_base_config_t *pWifiConn );

esp_err_t init_wifi_manager( wifi_connection_data_t *pInitConfig );
esp_err_t add_known_network_config( wifi_base_config_t *known_netork );
esp_err_t add_known_network( char *ssid, char *pwd );
esp_err_t wm_set_country(char *cc); //or char (*cc)[2]

#endif /* _WIFI_MANAGER_H_ */