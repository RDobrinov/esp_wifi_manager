#include "esp_wifi_manager.h"
#include "esp_log.h"

/*
#define WIFIMGR_AP_CHANNEL 0
#define WIFIMGR_AP_START_CHANNEL 13
#define WIFIMGR_MAX_STA_RETRY 3
#define WIFIMGR_AP_SSID "WIFIMGR_AP_SSID"
#define WIFIMGR_AP_PWD ""
*/
#define WIFIMGR_CONNECTED_BIT  BIT0
#define WIFIMGR_CONNECTING_BIT BIT1
#define WIFIMGR_SCAN_BIT       BIT2


typedef struct airband_rank {
    uint8_t channel[13];
    int8_t rssi[13];
} airband_rank_t;

typedef struct wifi_iface {
    esp_netif_t *iface;
    wifi_config_t *driver_config;
} wifi_iface_t;

typedef struct wifi_mgr_config {
    wifi_connection_data_t radio;
    wifi_iface_t ap;
    wifi_iface_t sta;
    struct {
        EventGroupHandle_t group; /* FreeRTOS event group to signal when we are connected*/
        uint8_t sta_connect_retry;
        TaskHandle_t apTask_handle;
        TaskHandle_t scanTask_handle;
    } event;
    wifi_ap_record_t found_known_ap;
    wifi_event_sta_disconnected_t blacklistedAPs[WIFIMGR_MAX_KNOWN_AP]; //Not used yet
} wifi_mgr_config_t;

static wifi_mgr_config_t *run_conf;

static void vScanTask(void *pvParameters);
static void vConnectTask(void *pvParameters);
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static esp_err_t netif_stop_dhcp();

static void vScanTask(void *pvParameters)
{
    wifi_mode_t wifi_run_mode = WIFI_MODE_MAX;
    TickType_t xDelayTicks = (2500 / portTICK_PERIOD_MS);
    while(true) {
        if(esp_wifi_get_mode(&wifi_run_mode) == ESP_OK) {
            if(wifi_run_mode == WIFI_MODE_APSTA) {    
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
    const char *taskTag = "vConnectTask";
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
                        //ESP_LOGI(taskTag, "Connecting to %s, rssi %d on channel %d", run_conf->found_known_ap.ssid, run_conf->found_known_ap.rssi, run_conf->found_known_ap.primary);
                    } //else { ESP_LOGE(taskTag, "esp_wifi_set_config(WIFI_IF_STA, running_config->wifi.sta.driver_config) (%s)", esp_err_to_name(err)); }
                }
                i++;
            }
        }
    }
}

void init_wifi_connection_data( wifi_connection_data_t *pWifiConn) {
    *pWifiConn = (wifi_connection_data_t) {
        .ap_mode = (wifi_base_config_t) {
            .wifi_ssid = CONFIG_WIFIMGR_AP_SSID,
            .wifi_password = CONFIG_WIFIMGR_AP_PWD,
            .static_ip.ip = { IPADDR_NONE },
            .static_ip.netmask = { IPADDR_NONE }, 
            .static_ip.gw = { IPADDR_NONE },
            .dns_server = { IPADDR_NONE } 
        },
        //.wifi_ap_channel = CONFIG_WIFIMGR_AP_START_CHANNEL,
        .wifi_ap_channel = (((0 == strcmp(CONFIG_WIFIMGR_COUNTRY_CODE, "US")) || (0 == strcmp(CONFIG_WIFIMGR_COUNTRY_CODE, "01"))) && 
                            (CONFIG_WIFIMGR_DEFAULT_AP_CHANNEL >11 )) ? 11 : CONFIG_WIFIMGR_DEFAULT_AP_CHANNEL,
        .wifi_max_sta_retry = CONFIG_WIFIMGR_MAX_STA_RETRY,
        .country = (wifi_country_t ){
            //.cc="BG", 
            .cc = CONFIG_WIFIMGR_COUNTRY_CODE,
            .schan = 1,
            //.nchan = 13,
            .nchan = ((0 == strcmp(CONFIG_WIFIMGR_COUNTRY_CODE, "US")) || (0 == strcmp(CONFIG_WIFIMGR_COUNTRY_CODE, "01")) )? 11 : 13,
            .policy=WIFI_COUNTRY_POLICY_AUTO
        }
    };

    for( uint8_t i=0; i<WIFIMGR_MAX_KNOWN_AP; i++) { 
        pWifiConn->known_networks[i] = (wifi_base_config_t){
            .wifi_ssid = "", .wifi_password = "",
            .static_ip.ip = { IPADDR_NONE },
            .static_ip.netmask = { IPADDR_NONE }, 
            .static_ip.gw = { IPADDR_NONE },
            .dns_server = { IPADDR_NONE } 
        };
    }
    return;
}

esp_err_t init_wifi_manager(wifi_connection_data_t *pInitConfig) {

    static const char *ftag = "wifimgr:init";

    static bool wifi_init_done = false;
    if(wifi_init_done) { return ESP_OK; }

    run_conf = (wifi_mgr_config_t *)calloc(1, sizeof(wifi_mgr_config_t));
    if(pInitConfig == NULL) { init_wifi_connection_data(&run_conf->radio); } 
    else {memcpy(&run_conf->radio, pInitConfig, sizeof(wifi_connection_data_t));}
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

    /* Setup initial driver configuration */
    run_conf->ap.driver_config = (wifi_config_t *)calloc(1, sizeof(wifi_config_t));
    run_conf->sta.driver_config = (wifi_config_t *)calloc(1, sizeof(wifi_config_t));

    if( run_conf->radio.ap_mode.static_ip.ip.addr != IPADDR_NONE ) {
        err = netif_stop_dhcp();
        if( err != ESP_OK ) {
            ESP_LOGE(ftag, "netif_stop_dhcp (%s)", esp_err_to_name(err));
        } else {
            esp_netif_set_ip_info(run_conf->ap.iface, &run_conf->radio.ap_mode.static_ip);
            esp_netif_dhcps_start(run_conf->ap.iface);
            esp_netif_dhcpc_start(run_conf->sta.iface);
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
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    if( ESP_OK != err) {
        ESP_LOGE(ftag, "wifi_event_handler (%s)", esp_err_to_name(err));
        return err;
    }
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL, NULL);
    if( ESP_OK != err) {
        ESP_LOGE(ftag, "ip_event_handler (%s)", esp_err_to_name(err));
        return err;
    }

    /* Setup AP mode configuration */
    strcpy((char *)run_conf->ap.driver_config->ap.ssid, run_conf->radio.ap_mode.wifi_ssid);
    run_conf->ap.driver_config->ap.channel = (run_conf->radio.wifi_ap_channel != 0) ? run_conf->radio.wifi_ap_channel : CONFIG_WIFIMGR_DEFAULT_AP_CHANNEL;
    run_conf->ap.driver_config->ap.max_connection = 1;
    run_conf->ap.driver_config->ap.authmode = 
        (strlen(strcpy((char *)run_conf->ap.driver_config->ap.password, run_conf->radio.ap_mode.wifi_password)) != 0) ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    run_conf->ap.driver_config->ap.pairwise_cipher = WIFI_CIPHER_TYPE_TKIP;
    run_conf->ap.driver_config->ap.pmf_cfg = (wifi_pmf_config_t) { .required = true };

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

static esp_err_t netif_stop_dhcp() {

    static const char *ftag = "wifimgr:dhcp";

    bool dhcp_stopped = true;
    esp_netif_dhcp_status_t dhcp_status;
    esp_err_t err;

    esp_netif_dhcps_get_status(run_conf->ap.iface, &dhcp_status);
    if(ESP_NETIF_DHCP_STOPPED != dhcp_status) {
        err = esp_netif_dhcps_stop(run_conf->ap.iface);
        if(ESP_OK != err && ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED != err) {
            ESP_LOGE(ftag, "esp_netif_dhcps_stop (%s)", esp_err_to_name(err));
            dhcp_stopped = false;
        }
    }

    esp_netif_dhcpc_get_status(run_conf->sta.iface, &dhcp_status);
    if(ESP_NETIF_DHCP_STOPPED != dhcp_status) {
        err = esp_netif_dhcpc_stop(run_conf->sta.iface);
        if(ESP_OK != err && ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED != err) {
            ESP_LOGE(ftag, "esp_netif_dhcpc_get_status (%s)", esp_err_to_name(err));
            dhcp_stopped = false;
        }
    }

    return dhcp_stopped ? ESP_OK : ESP_FAIL;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {

    const char *ftag = "wifimgr:evt";
    if (event_base == WIFI_EVENT) {
        if ( event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        }
        if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            if (run_conf->event.sta_connect_retry < CONFIG_WIFIMGR_MAX_STA_RETRY) {
                esp_wifi_connect();
                run_conf->event.sta_connect_retry++;
            } else {
                xEventGroupClearBits(run_conf->event.group, WIFIMGR_CONNECTING_BIT | WIFIMGR_CONNECTED_BIT);

                /* Set new channel */
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
        if(event_id == WIFI_EVENT_SCAN_DONE) {
            if(((wifi_event_sta_scan_done_t *)event_data)->status == 0) {
                uint16_t found_ap_count = 0;
                airband_rank_t airband;
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
                }
            }
        }
    }
}

static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
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