#include "mod_wifi.h"
#include "esp_netif_net_stack.h"
#include "esp_netif.h"

#include "regex.h"
#include "esp_nan.h"
#include "esp_wps.h"

/* STA Configuration */
#define EXAMPLE_ESP_MAXIMUM_RETRY           20

/*DHCP server option*/
#define DHCPS_OFFER_DNS             0x02

static const char *TAG_AP = "WiFi SoftAP";
static const char *TAG_STA = "WiFi Sta";
static int s_retry_num = 0;

static const char *TAG = "WIFI";

static app_wifi_interface_t* interface;
static uint64_t last_update_time = 0;
static uint8_t remaining_retries = 0;

void wifi_get_status_info(wifi_event_t event, char *output, size_t len) {
    const char *status_str = NULL;

    switch (event) {
        case WIFI_EVENT_WIFI_READY:             status_str = "WIFI_READY"; break;
        case WIFI_EVENT_SCAN_DONE:              status_str = "SCAN_DONE"; break;
        case WIFI_EVENT_STA_START:              status_str = "STA_START"; break;
        case WIFI_EVENT_STA_STOP:               status_str = "STA_STOP"; break;
        case WIFI_EVENT_STA_CONNECTED:          status_str = "STA_CONNECTED"; break;
        case WIFI_EVENT_STA_DISCONNECTED:       status_str = "STA_DISCONNECTED"; break;
        case WIFI_EVENT_STA_AUTHMODE_CHANGE:    status_str = "STA_AUTHMODE_CHANGE"; break;

        case WIFI_EVENT_STA_WPS_ER_SUCCESS:     status_str = "STA_WPS_ER_SUCCESS"; break;
        case WIFI_EVENT_STA_WPS_ER_FAILED:      status_str = "STA_WPS_ER_FAILED"; break;
        case WIFI_EVENT_STA_WPS_ER_TIMEOUT:     status_str = "STA_WPS_ER_TIMEOUT"; break;
        case WIFI_EVENT_STA_WPS_ER_PIN:         status_str = "STA_WPS_ER_PIN"; break;
        case WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP: status_str = "STA_WPS_ER_PBC_OVERLAP"; break;

        case WIFI_EVENT_AP_START:               status_str = "AP_START"; break;
        case WIFI_EVENT_AP_STOP:                status_str = "AP_STOP"; break;
        case WIFI_EVENT_AP_STACONNECTED:        status_str = "AP_STA_CONNECTED"; break;
        case WIFI_EVENT_AP_STADISCONNECTED:     status_str = "AP_STA_DISCONNECTED"; break;
        case WIFI_EVENT_AP_PROBEREQRECVED:      status_str = "AP_PROBEREQRECVED"; break;
        
        case WIFI_EVENT_AP_WPS_RG_SUCCESS:      status_str = "AP_WPS_RG_SUCCESS"; break;
        case WIFI_EVENT_AP_WPS_RG_FAILED:       status_str = "AP_WPS_RG_FAILED"; break;
        case WIFI_EVENT_AP_WPS_RG_TIMEOUT:      status_str = "AP_WPS_RG_TIMEOUT"; break;
        case WIFI_EVENT_AP_WPS_RG_PIN:          status_str = "AP_WPS_RG_PIN"; break;
        case WIFI_EVENT_AP_WPS_RG_PBC_OVERLAP:  status_str = "AP_WPS_RG_PBC_OVERLAP"; break;
        default:                                status_str = "UNKNOWN"; break;
    }

    strlcpy(output, "WIFI_EVENT_", len);
    strlcat(output, status_str, len);
} 

static wifi_event_t curr_status;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
        ESP_LOGI(TAG_AP, "Station "MACSTR" joined, AID=%d",
                MAC2STR(event->mac), event->aid);

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
        ESP_LOGI(TAG_AP, "Station "MACSTR" left, AID=%d, reason:%d",
                MAC2STR(event->mac), event->aid, event->reason);
        // TODO: retry to connect

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG_STA, "Station started");
        ESP_ERROR_CHECK(esp_wifi_connect());

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG_STA, "Station disconnected");

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG_STA, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
    }

    curr_status = event_id;

    char output[32];
    wifi_get_status_info(event_id, output, sizeof(output));
    printf(">>> WIFI STATUS: %s\n", output);
    interface->on_display_print(output, 1);
}

void wifi_init_sta(const char* sta_ssid, const char* sta_passwd) {
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

        wifi_config_t conf = {
        .sta = {
            .scan_method = WIFI_FAST_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .failure_retry_cnt = EXAMPLE_ESP_MAXIMUM_RETRY,
            .threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };

    // Copy the SSID and password into the configuration structure
    strncpy((char *)conf.sta.ssid, sta_ssid, sizeof(conf.sta.ssid));
    strncpy((char *)conf.sta.password, sta_passwd, sizeof(conf.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &conf) );

    ESP_ERROR_CHECK(esp_wifi_start());
}


void softap_set_dns_addr(esp_netif_t *esp_netif_ap,esp_netif_t *esp_netif_sta) {
    esp_netif_dns_info_t dns;
    esp_netif_get_dns_info(esp_netif_sta,ESP_NETIF_DNS_MAIN,&dns);
    uint8_t dhcps_offer_option = DHCPS_OFFER_DNS;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(esp_netif_ap));
    ESP_ERROR_CHECK(esp_netif_dhcps_option(esp_netif_ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &dhcps_offer_option, sizeof(dhcps_offer_option)));
    ESP_ERROR_CHECK(esp_netif_set_dns_info(esp_netif_ap, ESP_NETIF_DNS_MAIN, &dns));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(esp_netif_ap));
}

void wifi_setup(app_wifi_interface_t *intf) {
    interface = intf;
    remaining_retries = intf->max_retries;

    //! Minimum setup for ESP-NOW
    // ESP_ERROR_CHECK(esp_netif_init());
    // ESP_ERROR_CHECK(esp_event_loop_create_default());
    // wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    // ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    // ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    // ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    // ESP_ERROR_CHECK( esp_wifi_start());
    // ESP_ERROR_CHECK( esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));

    //! NOTE: these 2 lines need to start FIRST!
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,
        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP,
        &wifi_event_handler, NULL, NULL));

    /*Initialize WiFi */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // /* Set sta as the default interface */
    // esp_netif_set_default_netif(esp_netif_sta);

}

void wifi_softAp_begin(const char* ap_ssid, const char* ap_passwd, uint8_t channel) {
    /* Initialize AP */
    ESP_LOGI(TAG_AP, "ESP_WIFI_MODE_AP");

    esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_ap_config = {
        .ap = {
            .max_connection = 1,
            .channel = channel,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    // Copy the SSID and password into the configuration structure
    size_t ssid_len = sizeof(wifi_ap_config.ap.ssid);
    size_t passw_len = sizeof(wifi_ap_config.ap.password);
    wifi_ap_config.ap.ssid_len = ssid_len;

    strncpy((char*)wifi_ap_config.ap.ssid, ap_ssid, ssid_len);
    strncpy((char*)wifi_ap_config.ap.password, ap_passwd, passw_len);
    

    if (strlen(ap_passwd) == 0) {
        wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));
    ESP_LOGI(TAG_AP, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
            ap_ssid, ap_passwd, channel);

    // /* Enable napt on the AP netif */
    // if (esp_netif_napt_enable(esp_netif_ap) != ESP_OK) {
    //     ESP_LOGE(TAG_STA, "NAPT not enabled on the netif: %p", esp_netif_ap);
    // }
}


#if CONFIG_EXAMPLE_WPS_TYPE_PBC
    #define WPS_MODE WPS_TYPE_PBC
#elif CONFIG_EXAMPLE_WPS_TYPE_PIN
    #if CONFIG_EXAMPLE_WPS_PIN_VALUE
    #define EXAMPLE_WPS_PIN_VALUE CONFIG_EXAMPLE_WPS_PIN_VALUE
    #endif
    #define WPS_MODE WPS_TYPE_PIN
#else
    #define WPS_MODE WPS_TYPE_DISABLE
#endif /*CONFIG_EXAMPLE_WPS_TYPE_PBC*/

static esp_wps_config_t wps_config = WPS_CONFIG_INIT_DEFAULT(WPS_MODE);

//! Running in AP Mode
void wifi_wps_begin(void) {
	ESP_LOGI(TAG, "start wps...");

    //! WHY? undefined reference to `esp_wifi_ap_wps_enable'

	// ESP_ERROR_CHECK(esp_wifi_ap_wps_enable(&wps_config));

    // #if EXAMPLE_WPS_PIN_VALUE
    //     ESP_LOGI(TAG, "Staring WPS registrar with user specified pin %s", pin);
    //     snprintf((char *)pin, 9, "%08d", EXAMPLE_WPS_PIN_VALUE);
    //     ESP_ERROR_CHECK(esp_wifi_ap_wps_start(pin));
    // #else
    //     if (wps_config.wps_type == WPS_TYPE_PBC) {
    //         ESP_LOGI(TAG, "Staring WPS registrar in PBC mode");
    //     } else {
    //         ESP_LOGI(TAG, "Staring WPS registrar with random generated pin");
    //     }

    //     ESP_ERROR_CHECK(esp_wifi_ap_wps_start(NULL));
    // #endif
}

void wifi_sta_begin(const char* sta_ssid, const char* sta_passwd) {
    printf("connecting to %s\n", sta_ssid);

    wifi_config_t conf = {
        .sta = {
            .scan_method = WIFI_FAST_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .failure_retry_cnt = EXAMPLE_ESP_MAXIMUM_RETRY,
            .threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };

    // Copy the SSID and password into the configuration structure
    strncpy((char *)conf.sta.ssid, sta_ssid, sizeof(conf.sta.ssid));
    strncpy((char *)conf.sta.password, sta_passwd, sizeof(conf.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &conf) );
    ESP_LOGI(TAG_STA, "wifi_init_sta finished.");
}

void wifi_disconnect() {
    esp_wifi_disconnect();

    // esp_wifi_stop();     // stop driver
    // esp_wifi_deinit();
}

void wifi_stop() {
    esp_wifi_stop();
    // esp_wifi_deinit();
}

wifi_event_t wifi_get_status() {    
    return curr_status;
}

#define DEFAULT_SCAN_LIST_SIZE 10

static void print_auth_mode(int authmode) {
    switch (authmode) {
        case WIFI_AUTH_OPEN:                    ESP_LOGI(TAG_AP, "Authmode \tWIFI_AUTH_OPEN"); break;
        case WIFI_AUTH_OWE:                     ESP_LOGI(TAG_AP, "Authmode \tWIFI_AUTH_OWE"); break;
        case WIFI_AUTH_WEP:                     ESP_LOGI(TAG_AP, "Authmode \tWIFI_AUTH_WEP"); break;
        case WIFI_AUTH_WPA_PSK:                 ESP_LOGI(TAG_AP, "Authmode \tWIFI_AUTH_WPA_PSK"); break;
        case WIFI_AUTH_WPA2_PSK:                ESP_LOGI(TAG_AP, "Authmode \tWIFI_AUTH_WPA2_PSK"); break;
        case WIFI_AUTH_WPA_WPA2_PSK:            ESP_LOGI(TAG_AP, "Authmode \tWIFI_AUTH_WPA_WPA2_PSK"); break;
        case WIFI_AUTH_ENTERPRISE:              ESP_LOGI(TAG_AP, "Authmode \tWIFI_AUTH_ENTERPRISE"); break;
        case WIFI_AUTH_WPA3_PSK:                ESP_LOGI(TAG_AP, "Authmode \tWIFI_AUTH_WPA3_PSK"); break;
        case WIFI_AUTH_WPA2_WPA3_PSK:           ESP_LOGI(TAG_AP, "Authmode \tWIFI_AUTH_WPA2_WPA3_PSK"); break;
        case WIFI_AUTH_WPA3_ENTERPRISE:         ESP_LOGI(TAG_AP, "Authmode \tWIFI_AUTH_WPA3_ENTERPRISE"); break;
        case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:    ESP_LOGI(TAG_AP, "Authmode \tWIFI_AUTH_WPA2_WPA3_ENTERPRISE"); break;
        case WIFI_AUTH_WPA3_ENT_192:            ESP_LOGI(TAG_AP, "Authmode \tWIFI_AUTH_WPA3_ENT_192"); break;
        default:                                ESP_LOGI(TAG_AP, "Authmode \tWIFI_AUTH_UNKNOWN"); break;
    }
}

void wifi_scan(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
    uint16_t ap_count = 0;
    memset(ap_info, 0, sizeof(ap_info));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_wifi_scan_start(NULL, true);

    ESP_LOGI(TAG_AP, "Max AP number ap_info can hold = %u", number);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));
    ESP_LOGI(TAG_AP, "Total APs scanned = %u, actual AP number ap_info holds = %u", ap_count, number);

    for (int i = 0; i < number; i++) {
        ESP_LOGI(TAG_AP, "SSID \t\t%s", ap_info[i].ssid);
        ESP_LOGI(TAG_AP, "RSSI \t\t%d", ap_info[i].rssi);

        print_auth_mode(ap_info[i].authmode);
        ESP_LOGI(TAG_AP, "Channel \t\t%d", ap_info[i].primary);
    }
}

