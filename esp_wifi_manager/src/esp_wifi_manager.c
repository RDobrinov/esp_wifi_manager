#include "esp_wifi_manager.h"
#include "esp_log.h"

#define WIFIMGR_CONNECTED_BIT  BIT0
#define WIFIMGR_CONNECTING_BIT BIT1
#define WIFIMGR_SCAN_BIT       BIT2
/*
#define ESP_IPADDR_TYPE_V4                0U
#define ESP_IPADDR_TYPE_V6                6U
#define ESP_IPADDR_TYPE_ANY               46U
*/

typedef struct wm_airband_rank {
    uint8_t channel[13];
    int8_t rssi[13];
} wm_airband_rank_t;

typedef struct wm_wifi_iface {
    esp_netif_t *iface;
    wifi_config_t *driver_config;
} wm_wifi_iface_t;

typedef struct wm_wifi_mgr_config {
    wm_wifi_connection_data_t radio;
    wm_wifi_iface_t ap;
    wm_wifi_iface_t sta;
    struct {
        EventGroupHandle_t group;
        uint8_t sta_connect_retry;
        TaskHandle_t apTask_handle;
        TaskHandle_t scanTask_handle;
    } event;
    wifi_ap_record_t found_known_ap;
    wifi_event_sta_disconnected_t blacklistedAPs[WIFIMGR_MAX_KNOWN_AP]; //Not used yet
} wm_wifi_mgr_config_t;

static wm_wifi_mgr_config_t *run_conf;

static void vScanTask(void *pvParameters);
static void vConnectTask(void *pvParameters);
static void wm_wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void wm_ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void wm_apply_ap_driver_config();
static void wm_apply_netif_dns(esp_netif_t *iface, esp_ip4_addr_t *dns_server_ip, esp_netif_dns_type_t type );

static void vScanTask(void *pvParameters)
{
    wifi_mode_t wifi_run_mode = WIFI_MODE_MAX;
    TickType_t xDelayTicks = (2500 / portTICK_PERIOD_MS);
    while(true) {
        if(esp_wifi_get_mode(&wifi_run_mode) == ESP_OK) {
            if((wifi_run_mode == WIFI_MODE_APSTA) || ((wifi_run_mode == WIFI_MODE_STA) && (!(xEventGroupGetBits(run_conf->event.group) & WIFIMGR_CONNECTED_BIT)))) {    
                if ( !(xEventGroupGetBits(run_conf->event.group) & WIFIMGR_CONNECTING_BIT) && (xEventGroupGetBits(run_conf->event.group) & WIFIMGR_SCAN_BIT) && (strlen(run_conf->radio.known_networks[0].wifi_ssid) != 0)) {
                    xEventGroupClearBits(run_conf->event.group, WIFIMGR_SCAN_BIT);
                    esp_wifi_scan_start(NULL, false);
                    xDelayTicks = (2500 / portTICK_PERIOD_MS);
                }
            } else { xDelayTicks = (1000 / portTICK_PERIOD_MS); }
        } else { xDelayTicks = (500 / portTICK_PERIOD_MS); }
        vTaskDelay(xDelayTicks);
    }
}

static void vConnectTask(void *pvParameters) {

    esp_err_t err;

    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if(!(xEventGroupGetBits(run_conf->event.group) & WIFIMGR_CONNECTING_BIT)) {
            int i=0;
            while(!(xEventGroupGetBits(run_conf->event.group) & WIFIMGR_CONNECTING_BIT) && ( i < WIFIMGR_MAX_KNOWN_AP)) { 
                if(strcmp((char *)run_conf->found_known_ap.ssid, run_conf->radio.known_networks[i].wifi_ssid) == 0) {
                    strcpy((char *)run_conf->sta.driver_config->sta.ssid, run_conf->radio.known_networks[i].wifi_ssid);
                    strcpy((char *)run_conf->sta.driver_config->sta.password, run_conf->radio.known_networks[i].wifi_password);
                    run_conf->sta.driver_config->sta.channel = run_conf->found_known_ap.primary;
                    err = esp_wifi_set_config(WIFI_IF_STA, run_conf->sta.driver_config);
                    if( err == ESP_OK) {
                        xEventGroupSetBits(run_conf->event.group, WIFIMGR_CONNECTING_BIT);
                        run_conf->event.sta_connect_retry = 0;
                        esp_wifi_connect();
                    }
                }
                i++;
            }
        }
    }
}

static void wm_wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {

    const char *ftag = "wifimgr:evt";

    if (event_base == WIFI_EVENT) {
        if(event_id == WIFI_EVENT_SCAN_DONE) {
            //ESP_LOGW(ftag, "WIFI_EVENT_SCAN_DONE");
            if(((wifi_event_sta_scan_done_t *)event_data)->status == 0) {
                uint16_t found_ap_count = 0;
                wm_airband_rank_t airband;
                esp_wifi_scan_get_ap_num(&found_ap_count);
                wifi_ap_record_t *found_ap_info = (wifi_ap_record_t *)calloc(found_ap_count, sizeof(wifi_ap_record_t));
                esp_wifi_scan_get_ap_records(&found_ap_count, found_ap_info);
                memset(&(run_conf->found_known_ap), 0, sizeof(wifi_ap_record_t));
                memset(&airband.channel, 0, sizeof(airband.channel));
                memset(&airband.rssi, 0b10011111, sizeof(airband.rssi)); /* set min RSSI */
                bool bAPNotFound = true;
                for(int i=0; ( i<found_ap_count ); i++) {
                    if(bAPNotFound) {
                        int iKnownIndex = 0;
                        while( iKnownIndex < WIFIMGR_MAX_KNOWN_AP) {
                            if(strlen(run_conf->radio.known_networks[iKnownIndex].wifi_ssid) > 0) {
                                if(strcmp(run_conf->radio.known_networks[iKnownIndex].wifi_ssid, (char *)found_ap_info[i].ssid) == 0) {
                                    int iBLIndex = 0;
                                    bool bAPNotBL = true;
                                    while( (iBLIndex < WIFIMGR_MAX_KNOWN_AP) && bAPNotBL ) {
                                        if( (strcmp((char *)found_ap_info[i].ssid, (char *)run_conf->blacklistedAPs[iBLIndex].ssid) == 0) && 
                                                (memcmp((char *)found_ap_info[i].bssid, (char *)run_conf->blacklistedAPs[iBLIndex].bssid, 6 * sizeof(uint8_t)) == 0) ) {
                                            bAPNotBL = false;
                                        }
                                        iBLIndex++;
                                    }
                                    if( bAPNotBL ) { 
                                        run_conf->found_known_ap = found_ap_info[i];
                                        bAPNotFound = false;
                                        iKnownIndex = WIFIMGR_MAX_KNOWN_AP;
                                    }
                                }
                                iKnownIndex++;
                            } else { iKnownIndex = WIFIMGR_MAX_KNOWN_AP; }
                        }
                    }
                    if(run_conf->radio.wifi_ap_channel == 0) {
                        airband.channel[found_ap_info[i].primary-1]++;
                        if(airband.rssi[found_ap_info[i].primary-1] < found_ap_info[i].rssi) {airband.rssi[found_ap_info[i].primary-1] = found_ap_info[i].rssi;}
                        if(found_ap_info[i].primary-2 > 0) {
                            airband.channel[found_ap_info[i].primary-2]++;
                            if(airband.rssi[found_ap_info[i].primary-2] < found_ap_info[i].rssi) {airband.rssi[found_ap_info[i].primary-2] = found_ap_info[i].rssi;}
                        }
                        if(found_ap_info[i].primary < 13 ) {
                            airband.channel[found_ap_info[i].primary]++;
                            if(airband.rssi[found_ap_info[i].primary] < found_ap_info[i].rssi) {airband.rssi[found_ap_info[i].primary] = found_ap_info[i].rssi;}
                        }
                        if(found_ap_info[i].second != WIFI_SECOND_CHAN_NONE ) {
                            for(int b=1; b<5; b++) {
                                if( (found_ap_info[i].second == WIFI_SECOND_CHAN_ABOVE) ) { 
                                    if((found_ap_info[i].primary+b) < 13 ) {
                                        airband.channel[found_ap_info[i].primary+b]++;
                                        if(airband.rssi[found_ap_info[i].primary+b] < found_ap_info[i].rssi) {airband.rssi[found_ap_info[i].primary+b] = found_ap_info[i].rssi;}
                                    }
                                } else {
                                    if((found_ap_info[i].primary-b-1) > 0 ) {
                                        airband.channel[found_ap_info[i].primary-b-1]++;
                                        if(airband.rssi[found_ap_info[i].primary-b-1] < found_ap_info[i].rssi) {airband.rssi[found_ap_info[i].primary-b-1] = found_ap_info[i].rssi;}
                                    }
                                }
                            }
                        }
                    }
                }
                if(run_conf->radio.wifi_ap_channel == 0) {
                    int iRatedChannel = CONFIG_WIFIMGR_DEFAULT_AP_CHANNEL;
                    float fRatedRSSI = 0.0f, fCalcRSSI = 0.0f;
                    for(int i=0; i<13; i++) {
                        if(i==0) { fCalcRSSI = (float)(airband.channel[i] + airband.rssi[i]*10 + airband.channel[i+1] + airband.rssi[i+1]*10)/2; }
                        else if (i==12) { fCalcRSSI = (float)(airband.channel[i] + airband.rssi[i]*10 + airband.channel[i-1] + airband.rssi[i-1]*10)/2; }
                        else { fCalcRSSI = (float)(airband.channel[i] + airband.rssi[i]*10 + airband.channel[i-1] + airband.rssi[i-1]*10+ airband.channel[i+1] + airband.rssi[i+1]*10)/3;}
                        if( fRatedRSSI>fCalcRSSI ) {
                            fRatedRSSI = fCalcRSSI;
                            iRatedChannel = i+1;
                        }
                    }
                    if( run_conf->ap.driver_config->ap.channel != iRatedChannel ) {
                        run_conf->ap.driver_config->ap.channel = iRatedChannel;
                    };
                }
                free(found_ap_info);
                xEventGroupSetBits(run_conf->event.group, WIFIMGR_SCAN_BIT);
                if(!(xEventGroupGetBits(run_conf->event.group) & WIFIMGR_CONNECTED_BIT) && strlen((char *)run_conf->found_known_ap.ssid) > 0 ) {
                    xTaskNotifyGive(run_conf->event.apTask_handle);
                } else {
                    wifi_mode_t *wifi_run_mode = (wifi_mode_t *)calloc(1, sizeof(wifi_mode_t));
                    esp_wifi_get_mode(wifi_run_mode);
                    if(*wifi_run_mode != WIFI_MODE_APSTA) {
                        if(esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) {
                            ESP_LOGE(ftag, "APSTA failed");   
                        } else { 
                            esp_wifi_set_channel(run_conf->ap.driver_config->ap.channel, WIFI_SECOND_CHAN_NONE);
                        }
                    }
                }
            }
            return;
        }

        if ( event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
            return;
        }

        if ( event_id == WIFI_EVENT_STA_CONNECTED ) {
            //ESP_LOGE(ftag, "WIFI_EVENT_STA_CONNECTED");
            int i=0;
            while( (strlen((char *)(run_conf->radio.known_networks[i].wifi_ssid)) > 0) && (i < WIFIMGR_MAX_KNOWN_AP) ) {
                if(strcmp((char *)run_conf->radio.known_networks[i].wifi_ssid, (char *)((wifi_event_sta_connected_t *)(event_data))->ssid) == 0) {
                    //if(run_conf->radio.known_networks[i].static_ip.ip.addr != IPADDR_ANY ) {
                        wm_set_interface_ip(WIFI_IF_STA, &run_conf->radio.known_networks[i]);
                    //} else {
                    //    wm_set_interface_ip(WIFI_IF_STA, NULL);
                    //}
                    i = WIFIMGR_MAX_KNOWN_AP;
                } else { i++; }
            }
            return;
        }

        if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            if (run_conf->event.sta_connect_retry < CONFIG_WIFIMGR_MAX_STA_RETRY) {
                esp_wifi_connect();
                run_conf->event.sta_connect_retry++;
            } else {
                xEventGroupClearBits(run_conf->event.group, WIFIMGR_CONNECTING_BIT | WIFIMGR_CONNECTED_BIT);
            }
        }
    }
}

static void wm_ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    static const char *ftag = "wifimgr:ipevt";
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        run_conf->event.sta_connect_retry = 0;
        xEventGroupSetBits(run_conf->event.group, WIFIMGR_CONNECTED_BIT);
        xEventGroupClearBits(run_conf->event.group, WIFIMGR_CONNECTING_BIT);
        if(esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
            ESP_LOGE(ftag, "ip_event_handler STA failed");   
        }
    }
}

static void wm_apply_ap_driver_config() {
    strcpy((char *)run_conf->ap.driver_config->ap.ssid, run_conf->radio.ap_mode.wifi_ssid);
    run_conf->ap.driver_config->ap.channel = (run_conf->radio.wifi_ap_channel != 0) ? run_conf->radio.wifi_ap_channel : CONFIG_WIFIMGR_DEFAULT_AP_CHANNEL;
    run_conf->ap.driver_config->ap.max_connection = 1;
    run_conf->ap.driver_config->ap.authmode = 
        (strlen(strcpy((char *)run_conf->ap.driver_config->ap.password, run_conf->radio.ap_mode.wifi_password)) != 0) ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    run_conf->ap.driver_config->ap.pairwise_cipher = WIFI_CIPHER_TYPE_TKIP;
    run_conf->ap.driver_config->ap.pmf_cfg = (wifi_pmf_config_t) { .required = true };
}

static void wm_apply_netif_dns(esp_netif_t *iface, esp_ip4_addr_t *dns_server_ip, esp_netif_dns_type_t type ) {
    esp_netif_dns_info_t *dns = (esp_netif_dns_info_t *)calloc(1, sizeof(esp_netif_dns_info_t));
    *dns = (esp_netif_dns_info_t) { .ip = (esp_ip_addr_t){.u_addr.ip4 = *dns_server_ip, .type = ESP_IPADDR_TYPE_V4 }};
    esp_err_t err = esp_netif_set_dns_info(iface, type, dns);
    free(dns);
    if(ESP_OK != err) {ESP_LOGE("wm:dns", "wm_apply_netif_dns (%s)", esp_err_to_name(err));}
    return;
}

esp_err_t wm_set_interface_ip( wifi_interface_t iface, wm_wifi_base_config_t *ip_info)
{
    bool _ok = true;
    esp_netif_dhcp_status_t dhcp_status;
    esp_err_t err;

    if(iface != WIFI_IF_STA && iface != WIFI_IF_AP) return ESP_FAIL;
    esp_netif_ip_info_t *new_ip_info = (esp_netif_ip_info_t *)calloc(1, sizeof(esp_netif_ip_info_t));
    if( ip_info == NULL ) {
       *new_ip_info = (WIFI_IF_AP == iface) ? (esp_netif_ip_info_t) {
            .ip = { ((u32_t)0x0104A8C0UL) }, 
            .gw = { ((u32_t)0x0104A8C0UL) },
            .netmask = { ((u32_t)0x00FFFFFFUL) }
        } : (esp_netif_ip_info_t) {
            .ip = { ((u32_t)0x00000000UL) }, 
            .gw = { ((u32_t)0x00000000UL) },
            .netmask = { ((u32_t)0x00000000UL) }
        };
    } else { memcpy(new_ip_info, &ip_info->static_ip, sizeof(esp_netif_ip_info_t)); }

    if( iface == WIFI_IF_AP ) {
        esp_netif_dhcps_get_status(run_conf->ap.iface, &dhcp_status);
        if(ESP_NETIF_DHCP_STOPPED != dhcp_status) {
            err = esp_netif_dhcps_stop(run_conf->ap.iface);
            if(ESP_OK != err && ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED != err) {
                _ok = false;
            } else { 
                err = esp_netif_set_ip_info(run_conf->ap.iface, new_ip_info); 
                _ok &= ( ESP_OK == err );
                //ESP_LOGE("ap"," " IPSTR "/" IPSTR "/" IPSTR "/" IPSTR, IP2STR(&ip_info->static_ip.ip), IP2STR(&ip_info->static_ip.netmask), IP2STR(&ip_info->static_ip.gw), IP2STR(&ip_info->pri_dns_server));
                if( ip_info != NULL ) {
                    if(ip_info->pri_dns_server.addr != IPADDR_ANY) {
                        wm_apply_netif_dns(run_conf->ap.iface, &ip_info->pri_dns_server, ESP_NETIF_DNS_MAIN);
                    }
                } else {
                    wm_apply_netif_dns(run_conf->ap.iface, &new_ip_info->ip, ESP_NETIF_DNS_MAIN);
                }
                _ok &= ( err == ESP_OK );
                err = esp_netif_dhcps_start(run_conf->ap.iface);
                _ok &= (( err == ESP_OK || err == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED));
            }
        }    
    } else {
        esp_netif_dhcpc_get_status(run_conf->sta.iface, &dhcp_status);
        if(ESP_NETIF_DHCP_STOPPED != dhcp_status) {
            err = esp_netif_dhcpc_stop(run_conf->sta.iface);
            if(ESP_OK != err && ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED != err) {
                _ok = false;
            }
        }
        if( _ok ) {
            err = esp_netif_set_ip_info(run_conf->sta.iface, new_ip_info); 
            _ok &= ( ESP_OK == err );
            if(ip_info != NULL) {
                if(ip_info->pri_dns_server.addr != IPADDR_ANY) {
                    wm_apply_netif_dns(run_conf->sta.iface, &ip_info->pri_dns_server, (new_ip_info->ip.addr == IPADDR_ANY) ? ESP_NETIF_DNS_FALLBACK : ESP_NETIF_DNS_MAIN );
                }
            }
            if(new_ip_info->ip.addr == IPADDR_ANY )
            {
                err = esp_netif_dhcpc_start(run_conf->sta.iface);
                _ok &= (( err == ESP_OK || err == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED));
            } else {
                if(run_conf->radio.sec_dns_server.addr != IPADDR_ANY) {
                    wm_apply_netif_dns(run_conf->sta.iface, &run_conf->radio.sec_dns_server, ESP_NETIF_DNS_BACKUP);
                }
            }
        }
    }
    free(new_ip_info);
    return _ok ? ESP_OK : ESP_FAIL;
}

void wm_init_wifi_connection_data( wm_wifi_connection_data_t *pWifiConn) {
    *pWifiConn = (wm_wifi_connection_data_t) {
        .ap_mode = (wm_wifi_base_config_t) {
            .wifi_ssid = CONFIG_WIFIMGR_AP_SSID,
            .wifi_password = CONFIG_WIFIMGR_AP_PWD,
            .static_ip.ip = { IPADDR_ANY },
            .static_ip.netmask = { IPADDR_ANY }, 
            .static_ip.gw = { IPADDR_ANY },
            .pri_dns_server = { IPADDR_ANY }
        },
        .wifi_ap_channel = (((0 == strcmp(CONFIG_WIFIMGR_COUNTRY_CODE, "US")) || (0 == strcmp(CONFIG_WIFIMGR_COUNTRY_CODE, "01"))) && 
                            (CONFIG_WIFIMGR_DEFAULT_AP_CHANNEL >11 )) ? 11 : CONFIG_WIFIMGR_DEFAULT_AP_CHANNEL,
        .wifi_max_sta_retry = CONFIG_WIFIMGR_MAX_STA_RETRY,
        .country = (wifi_country_t ){ 
            .cc = CONFIG_WIFIMGR_COUNTRY_CODE,
            .schan = 1,
            .nchan = ((0 == strcmp(CONFIG_WIFIMGR_COUNTRY_CODE, "US")) || (0 == strcmp(CONFIG_WIFIMGR_COUNTRY_CODE, "01")) )? 11 : 13,
            .policy=WIFI_COUNTRY_POLICY_AUTO
        }
    };

    for( uint8_t i=0; i<WIFIMGR_MAX_KNOWN_AP; i++) { 
        pWifiConn->known_networks[i] = (wm_wifi_base_config_t){
            .wifi_ssid = "", .wifi_password = "",
            .static_ip.ip = { IPADDR_ANY },
            .static_ip.netmask = { IPADDR_ANY }, 
            .static_ip.gw = { IPADDR_ANY },
            .pri_dns_server = { IPADDR_ANY }
        };
    }
    return;
}

void wm_init_base_config( wm_wifi_base_config_t *base_conf) {
    *base_conf = (wm_wifi_base_config_t) {
        .wifi_ssid = "",
        .wifi_password = "",
        .static_ip.ip = { IPADDR_ANY },
        .static_ip.netmask = { IPADDR_ANY }, 
        .static_ip.gw = { IPADDR_ANY },
        .pri_dns_server = { IPADDR_ANY }
    };
}

void wm_change_ap_mode_config( wm_wifi_base_config_t *pWifiConn ) {
    memcpy(&run_conf->radio.ap_mode, pWifiConn, sizeof(wm_wifi_base_config_t));
    if(pWifiConn->static_ip.ip.addr != IPADDR_ANY ) {
        wm_set_interface_ip(WIFI_IF_AP, pWifiConn);
    } else { wm_set_interface_ip(WIFI_IF_AP, NULL); }

    wm_apply_ap_driver_config();
    esp_wifi_set_config(WIFI_IF_AP, run_conf->ap.driver_config);
}

void wm_set_ap_primary_dns(esp_ip4_addr_t dns_ip) {
    run_conf->radio.ap_mode.pri_dns_server = dns_ip;
}

void wm_set_sta_primary_dns(esp_ip4_addr_t dns_ip, char *ssid) {
    int iKnownIndex = 0;
    while( iKnownIndex < WIFIMGR_MAX_KNOWN_AP) {
        if(strlen(run_conf->radio.known_networks[iKnownIndex].wifi_ssid) > 0) {
            if(strcmp(run_conf->radio.known_networks[iKnownIndex].wifi_ssid, ssid) == 0) {
                run_conf->radio.known_networks[iKnownIndex].pri_dns_server = dns_ip;
            } else iKnownIndex++;
        } else { iKnownIndex = WIFIMGR_MAX_KNOWN_AP; }
    }
}

void wm_set_secondary_dns(esp_ip4_addr_t dns_ip) {
    run_conf->radio.sec_dns_server = dns_ip;
}

esp_err_t wm_init_wifi_manager(wm_wifi_connection_data_t *pInitConfig) {

    static const char *ftag = "wifimgr:init";

    static bool wifi_init_done = false;
    if(wifi_init_done) { return ESP_OK; }

    run_conf = (wm_wifi_mgr_config_t *)calloc(1, sizeof(wm_wifi_mgr_config_t));
    if(pInitConfig == NULL) { wm_init_wifi_connection_data(&run_conf->radio); } 
    else {memcpy(&run_conf->radio, pInitConfig, sizeof(wm_wifi_connection_data_t));}
    /* Disable wifi log info*/
    esp_log_level_set("wifi", ESP_LOG_WARN);
    /* Init NVS*/
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if( err != ESP_OK ) { 
            ESP_LOGE(ftag, "nvs_flash_erase (%s)", esp_err_to_name(err));
            return err; 
        }
        err = nvs_flash_init();
    }
    if( err != ESP_OK ) { 
        ESP_LOGE(ftag, "nvs_flash_init (%s)", esp_err_to_name(err));
        return err; 
    }

    /* Init netif */
    if( esp_netif_init() != ESP_OK ) {
        ESP_LOGE(ftag, "esp_netif_init failed");
        return ESP_FAIL;
    };

    /* Create default event loop */
    err = esp_event_loop_create_default();
    if( err != ESP_OK ) {
        ESP_LOGE(ftag, "esp_event_loop_create_default (%s)", esp_err_to_name(err));
        return err; 
    }
    run_conf->event.group = xEventGroupCreate();
    xEventGroupClearBits(run_conf->event.group, WIFIMGR_CONNECTING_BIT | WIFIMGR_CONNECTED_BIT);

    /* Init wifi interfaces */
    run_conf->ap.iface = esp_netif_create_default_wifi_ap();
	run_conf->sta.iface = esp_netif_create_default_wifi_sta();

    //ESP_LOGW("ap"," " IPSTR "/" IPSTR "/" IPSTR, IP2STR(&ap_ip.ip), IP2STR(&ap_ip.netmask), IP2STR(&ap_ip.gw));

    /* Setup initial driver configuration */
    run_conf->ap.driver_config = (wifi_config_t *)calloc(1, sizeof(wifi_config_t));
    run_conf->sta.driver_config = (wifi_config_t *)calloc(1, sizeof(wifi_config_t));
    if( run_conf->radio.ap_mode.static_ip.ip.addr != IPADDR_ANY ) {
        //if( wm_set_interface_ip(WIFI_IF_AP, &run_conf->radio.ap_mode.static_ip) != ESP_OK ) {
        if( wm_set_interface_ip(WIFI_IF_AP, &run_conf->radio.ap_mode) != ESP_OK ) {
            ESP_LOGE(ftag, "IP Address not changed");
        }
    }
    
    /* Init default WIFI configuration*/
    wifi_init_config_t *wifi_initconf = (wifi_init_config_t *)calloc(1, sizeof(wifi_init_config_t));
    *wifi_initconf = (wifi_init_config_t)WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(wifi_initconf);
    if( ESP_OK != err) {
        ESP_LOGE(ftag, "esp_wifi_init (%s)", esp_err_to_name(err));
        return err;
    }
    free(wifi_initconf);

    if( esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ) {
        ESP_LOGE(ftag, "esp_wifi_set_storage");
        return ESP_FAIL;
    }

    if( esp_wifi_set_country(&(run_conf->radio.country))) {
        ESP_LOGE(ftag, "esp_wifi_set_country");
        return ESP_FAIL;  
    }

    /* Event handlers registation */ /* TODO Do not use default event loop */
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wm_wifi_event_handler, NULL, NULL);
    if( ESP_OK != err) {
        ESP_LOGE(ftag, "wifi_event_handler (%s)", esp_err_to_name(err));
        return err;
    }
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wm_ip_event_handler, NULL, NULL);
    if( ESP_OK != err) {
        ESP_LOGE(ftag, "ip_event_handler (%s)", esp_err_to_name(err));
        return err;
    }

    /* Setup AP mode configuration */
    wm_apply_ap_driver_config();    

    /* Apply initial configuration */
    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if(err != ESP_OK) {
        ESP_LOGE(ftag, "esp_wifi_set_mode (%s)", esp_err_to_name(err));
        return err;    
    };
    err = esp_wifi_set_config(WIFI_IF_AP, run_conf->ap.driver_config);
    if(err != ESP_OK) {
        ESP_LOGE(ftag, "esp_wifi_set_config(WIFI_IF_AP) (%s)", esp_err_to_name(err));
        return err;
    };
    err = esp_wifi_set_config(WIFI_IF_STA, run_conf->sta.driver_config);
    if(err != ESP_OK) {
        ESP_LOGE(ftag, "esp_wifi_set_config(WIFI_IF_STA) (%s)", esp_err_to_name(err));
        return err;
    };
    esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
    err = esp_wifi_start();
    if( err != ESP_OK ) {
        ESP_LOGE(ftag, "esp_wifi_start (%s)", esp_err_to_name(err));
        return err;
    };

    /* TODO: Remove in release
    ESP_LOGI(ftag,"iface %s / %s created", run_conf->ap.iface->if_desc, run_conf->ap.iface->if_key);
    ESP_LOGI(ftag,"iface %s / %s created", run_conf->ap.iface->if_desc, run_conf->ap.iface->if_key);
    */
    memset(run_conf->blacklistedAPs, 0, sizeof(run_conf->blacklistedAPs));
    xEventGroupSetBits(run_conf->event.group, WIFIMGR_SCAN_BIT );
    xTaskCreate(vConnectTask, "wconn", 2048, NULL, 16, &run_conf->event.apTask_handle);
    xTaskCreate(vScanTask, "wscan", 2048, NULL, 15, &run_conf->event.scanTask_handle);
    wifi_init_done = true;

    return ESP_OK;
}

esp_err_t wm_add_known_network_config( wm_wifi_base_config_t *known_network) {
    int i;
    for(i=0; i<WIFIMGR_MAX_KNOWN_AP; i++) {
        if(strlen(run_conf->radio.known_networks[i].wifi_ssid) == 0 ) {
            memcpy(&run_conf->radio.known_networks[i], known_network, sizeof(wm_wifi_base_config_t));
            break; //???
        }
    }
    return ( i == WIFIMGR_MAX_KNOWN_AP ) ? ESP_FAIL : ESP_OK;
}

esp_err_t wm_add_known_network( char *ssid, char *pwd) {
    wm_wifi_base_config_t *new_network = calloc(1, sizeof(wm_wifi_base_config_t));
    wm_init_base_config(new_network);
    if(strlen(ssid)>31) {memcpy( new_network->wifi_ssid, ssid, 31);
    } else strcpy(new_network->wifi_ssid, ssid);
    if(strlen(pwd)>63) {memcpy( new_network->wifi_password, pwd, 63);
    } else strcpy(new_network->wifi_password, pwd);

    esp_err_t err = wm_add_known_network_config(new_network);
    free(new_network);
    return err;
}

esp_err_t wm_set_country(char *cc) {
    wifi_country_t *new_country = (wifi_country_t *)calloc(1, sizeof(wifi_country_t));
    *new_country = (wifi_country_t ){
        .cc = "",
        .schan = 1,
        .nchan = ((0 == strcmp(cc, "US")) || (0 == strcmp(cc, "01")) )? 11 : 13,
        .policy=WIFI_COUNTRY_POLICY_AUTO
    };
    new_country->cc[0] = cc[0];
    new_country->cc[1] = cc[1];
    memcpy(&run_conf->radio.country, new_country, sizeof(wifi_country_t));
    free(new_country);
    return (esp_wifi_set_country(&(run_conf->radio.country)) == ESP_OK ) ? ESP_OK : ESP_FAIL;
}
