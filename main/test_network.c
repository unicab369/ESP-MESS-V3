#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "WIFI_CRED.h"

#define EXAMPLE_ESP_MAXIMUM_RETRY  5
#define BLINK_GPIO 22

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "[ESP-MESS]";

static int s_retry_num = 0;


static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "retry to connect to the AP");
        } else {
            // xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGW(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
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
    static uint32_t last_connect_attempt = 0;
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    if (is_wifi_connected()) {
        if (!connected) {
            printf("WiFi connected!\n");
            connected = true;
            
            // Get IP address
            esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            esp_netif_ip_info_t ip_info;
            esp_netif_get_ip_info(sta_netif, &ip_info);
            printf("IP: " IPSTR "\n", IP2STR(&ip_info.ip));
        }
    } else {
        if (connected) {
            printf("WiFi disconnected\n");
            connected = false;
        }
        
        // Only try to connect once every 1 seconds
        if (now - last_connect_attempt > 1000) {
            printf("Connecting...\n");
            esp_err_t err = esp_wifi_connect();
            if (err == ESP_OK) {
                last_connect_attempt = now;
            } else {
                printf("Connect error: %s\n", esp_err_to_name(err));
            }
        }
    }
}

void wifi_init_sta(void) {
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
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
            .sae_h2e_identifier = "",
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");
}

void app_main(void) {
	//# nvs_flash required for WiFi, ESP-NOW, and other stuff.
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "APP START");

    wifi_init_sta();

    //# Setup Blinking
    static uint8_t s_led_state = 0;
	gpio_reset_pin(BLINK_GPIO);
	gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

    while(1) {
        wifi_poll();
        gpio_set_level(BLINK_GPIO, s_led_state);
        s_led_state = !s_led_state;
        
        vTaskDelay(1000 / portTICK_PERIOD_MS); 
    }
}
