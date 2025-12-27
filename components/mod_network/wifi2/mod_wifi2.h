#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "http2/http2.h"

#define EXAMPLE_ESP_MAXIMUM_RETRY  5

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;


static int s_retry_num = 0;

static const char *TAG_WIFI = "[WIFI]";


static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG_WIFI, "retry to connect to the AP");
        } else {
            // xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGW(TAG_WIFI,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG_WIFI, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        // xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

bool is_wifi_connected() {
    wifi_ap_record_t ap;
    return (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);
}

void wifi_poll() {
    static bool connected = false;
    static uint32_t last_attempt = 0;
    static httpd_handle_t server = NULL;
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    bool current = is_wifi_connected();
    
    if (current != connected) {
        if (current) {
            ESP_LOGI(TAG_WIFI, "WiFi connected!");
            
            // Start HTTP server when WiFi connects
            if (server == NULL) {
                server = start_webserver();
            }
        } else {
            ESP_LOGI(TAG_WIFI, "WiFi disconnected");
            
            // Stop HTTP server when WiFi disconnects
            if (server != NULL) {
                httpd_stop(server);
                server = NULL;
                ESP_LOGI(TAG_WIFI, "HTTP server stopped");
            }
        }
        connected = current;
    }
    
    // Try to connect if not connected (every 10 seconds)
    if (!connected && (now - last_attempt > 10000)) {
        ESP_LOGI(TAG_WIFI, "Attempting WiFi connection...");
        esp_err_t err = esp_wifi_connect();
        if (err == ESP_OK) {
            last_attempt = now;
        } else {
            ESP_LOGE(TAG_WIFI, "Failed to start connection: %s", esp_err_to_name(err));
        }
    }
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
                        ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                        IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
            .sae_h2e_identifier = "",
        },
    };

    // Copy the SSID and password into the configuration structure
    strncpy((char*)wifi_config.sta.ssid, sta_ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, sta_passwd, sizeof(wifi_config.sta.password));
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG_WIFI, "wifi_init_sta finished.");
}
