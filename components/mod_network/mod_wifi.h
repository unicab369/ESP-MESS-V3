#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "mod_http.h"
#include "mod_ntp.h"
#include "mdns.h"

#include "../../main/WIFI_CRED.h"

#define EXAMPLE_ESP_MAXIMUM_RETRY  5

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static const char *TAG_WIFI = "#WIFI";

typedef struct {
    int8_t channel;
    uint8_t bssid[6];
    char ssid[33];
	char password[65];
} wifi_info_t;

static wifi_info_t saved_wifi = {0};

void wifi_save_conn_info(int8_t channel, const uint8_t *bssid, const uint8_t *ssid) {
	const char method_name[] = "wifi_save_conn_info";
	nvs_handle_t handle;
	esp_err_t err = nvs_open("wifi", NVS_READWRITE, &handle);

	if (err == ESP_OK) {
		nvs_set_i8(handle, "chan", channel);
		nvs_set_blob(handle, "bssid", bssid, 6);
		nvs_set_str(handle, "ssid", (char*)ssid);

		nvs_commit(handle);
		nvs_close(handle);
		ESP_LOGW(TAG_WIFI, "%s WIFI-SAVED to NVS", method_name);
		printf("- Saved: channel=%d, SSID=%s\n", channel, ssid);
	} else {
		ESP_LOGE(TAG_WIFI, "WIFI-SAVED err: %s", esp_err_to_name(err));
	}
}

void wifi_save_password(char *password) {
	const char method_name[] = "wifi_save_password";
	nvs_handle_t handle;
	esp_err_t err = nvs_open("wifi", NVS_READWRITE, &handle);

	nvs_set_str(handle, "pasw", password);
	nvs_commit(handle);
	nvs_close(handle);

	ESP_LOGW(TAG_WIFI, "%s WIFI-SAVED to NVS", method_name);
}

int wifi_load_conn_info(void) {
	const char method_name[] = "wifi_load_conn_info";
	nvs_handle_t handle;

	esp_err_t ret = nvs_open("wifi", NVS_READONLY, &handle);
	if (ret != ESP_OK) {
		ESP_LOGW(TAG_WIFI, "%s NOT-FOUND nvs 'wifi'", method_name);
		return 0;
	}

	//# Load channel
	ret = nvs_get_i8(handle, "chan", &saved_wifi.channel);
	if (ret != ESP_OK) {
		ESP_LOGW(TAG_WIFI, "%s NOT-FOUND value 'chan'", method_name);
		nvs_close(handle);
		return 0;
	}

	//# Load BSSID
	size_t bssid_size = 6;
	ret = nvs_get_blob(handle, "bssid", saved_wifi.bssid, &bssid_size);
	if (ret != ESP_OK || bssid_size != 6) {
		ESP_LOGW(TAG_WIFI, "%s NOT-FOUND value 'bssid'", method_name);
		saved_wifi.channel = 0; // Reset channel if BSSID not found
	}

	//# Load SSID
	size_t ssid_size = sizeof(saved_wifi.ssid);
	ret = nvs_get_str(handle, "ssid", saved_wifi.ssid, &ssid_size);
	if (ret != ESP_OK) {
		// SSID not saved, use the one from config
		strncpy(saved_wifi.ssid, EXAMPLE_ESP_WIFI_SSID, sizeof(saved_wifi.ssid) - 1);
		saved_wifi.ssid[sizeof(saved_wifi.ssid) - 1] = '\0';
	}

	//# Load password
	size_t password_size = sizeof(saved_wifi.password);
	ret = nvs_get_str(handle, "passw", saved_wifi.password, &password_size);
	if (ret != ESP_OK) {
		// Password not saved, use the one from config
		strncpy(saved_wifi.password, EXAMPLE_ESP_WIFI_PASSWORD, sizeof(saved_wifi.password) - 1);
		saved_wifi.password[sizeof(saved_wifi.password) - 1] = '\0';
	}

	nvs_close(handle);
	ESP_LOGW(TAG_WIFI, "%s WIFI-LOADED channel=%d, SSID=%s", method_name,
				saved_wifi.channel, saved_wifi.ssid);
	return 1;
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
	if (event_base == WIFI_EVENT) {
		if (event_id == WIFI_EVENT_STA_START) {
			esp_wifi_connect();
		}
		else if (event_id == WIFI_EVENT_STA_CONNECTED) {
			// save channel
			// save bssid
		}
		else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
			if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
				esp_wifi_connect();
				s_retry_num++;
				ESP_LOGW(TAG_WIFI, "retry to connect to the AP");
			} else {
				// xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
			}
			ESP_LOGW(TAG_WIFI,"connect to the AP fail");
		}
	}
	else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		if (event_id == IP_EVENT_STA_GOT_IP) {
			ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
			ESP_LOGI(TAG_WIFI, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
			s_retry_num = 0;
			// xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
		}
	}
}

bool is_wifi_connected() {
	wifi_ap_record_t ap;
	return (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);
}

void wifi_print_details(wifi_ap_record_t *ap_info) {
	if (esp_wifi_sta_get_ap_info(ap_info) == ESP_OK) {
		printf("- SSID: %s\n", ap_info->ssid);
		printf("- Channel: %d\n", ap_info->primary);
		printf("- RSSI: %d dBm\n", ap_info->rssi);

		// Print BSSID
		char bssid_str[18];
		snprintf(bssid_str, sizeof(bssid_str), "%02x:%02x:%02x:%02x:%02x:%02x",
					ap_info->bssid[0], ap_info->bssid[1], ap_info->bssid[2],
					ap_info->bssid[3], ap_info->bssid[4], ap_info->bssid[5]);
		printf("- BSSID: %s\n", bssid_str);
	}
}


static httpd_handle_t web_server = NULL;

void wifi_poll() {
	const char method_name[] = "wifi_poll";

	static bool connected = false;
	static uint32_t last_attempt = 0;
	bool current = is_wifi_connected();
	static sntp_sync_status_t ntp_status = SNTP_SYNC_STATUS_RESET;

	if (current != connected) {
		if (current) {
			ESP_LOGW(TAG_WIFI, "WIFI-CONNECTED");

			wifi_ap_record_t ap_info;
			wifi_print_details(&ap_info);

			//# Save connection info
			wifi_save_conn_info(ap_info.primary, ap_info.bssid, ap_info.ssid);

			// Start HTTP server when WiFi connects
			if (web_server == NULL) {
				uint8_t mac[6];
				esp_read_mac(mac, ESP_MAC_WIFI_STA);
				ESP_LOGW(TAG_WIFI, "%s MAC-ADDR: %02X:%02X:%02X:%02X:%02X:%02X",
						method_name,
						mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

				//# START NTP
				ntp_init();

				//# START WEB SERVER
				web_server = start_webserver();

				//# START MDNS
				esp_err_t err = mdns_init();
				if (err != ESP_OK) {
					ESP_LOGE(TAG_WIFI, "%s MDNS-FAILED: %d", method_name, err);
				} else {
					char hostname[32];
					snprintf(hostname, sizeof(hostname), "esp32-%02X%02X%02X%02X",
							mac[2], mac[3], mac[4], mac[5]);
					// Set hostname: hostname.local
					mdns_hostname_set(hostname);

					// Friendly instance name
					mdns_instance_name_set("ESP32 HTTP Server");

					// Advertise an HTTP service on port 80
					mdns_service_add("ESP32-Web", "_http", "_tcp", 80, NULL, 0);
					ESP_LOGW(TAG_WIFI, "MDNS-STARTED %s", hostname);
				}
			}
		} else {
			ESP_LOGE(TAG_WIFI, "%s WIFI-DISCONNECTED", method_name);

			// Stop HTTP server when WiFi disconnects
			if (web_server != NULL) {
				httpd_stop(web_server);
				web_server = NULL;
				ESP_LOGI(TAG_WIFI, "%s SERVER-STOPPED", method_name);
			}
		}
		connected = current;

	} else if (ntp_status != SNTP_SYNC_STATUS_COMPLETED) {
		ntp_status = sntp_get_sync_status();

		if (ntp_status == SNTP_SYNC_STATUS_COMPLETED) {
			ntp_load_time();
			ESP_LOGW(TAG_WIFI, "%s NTP-SYNCED", method_name);
		} else {
			ESP_LOGW(TAG_WIFI, "%s NTP-SYNCING...", method_name);
		}

	}

	// Try to connect if not connected (every 10 seconds)
	uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

	if (!connected && (now - last_attempt > 10000)) {
		ESP_LOGW(TAG_WIFI, "%s WIFI-STATUS: connecting...", method_name);

		if (strlen(saved_wifi.ssid) > 0) {
			// Set specific channel in scan configuration
			wifi_scan_config_t scan_config = {
				.ssid = (uint8_t*)saved_wifi.ssid,
				.bssid = saved_wifi.bssid,
				.channel = saved_wifi.channel,  // Start with saved channel
				.scan_type = WIFI_SCAN_TYPE_ACTIVE,
				.scan_time = {
					.active = {
						.min = 100,  // Shorter scan on known channel
						.max = 200
					}
				}
			};

			memcpy(scan_config.bssid, saved_wifi.bssid, 6);
			esp_wifi_scan_start(&scan_config, true);
		}

		//! start connection
		esp_err_t err = esp_wifi_connect();
		if (err == ESP_OK) {
			last_attempt = now;
		} else {
			ESP_LOGE(TAG_WIFI, "%s WIFI-FAILED: %s", method_name, esp_err_to_name(err));
		}
	}
}

void wifi_init_sta() {
	const char method_name[] = "wifi_init_sta";
	esp_netif_init();
	esp_event_loop_create_default();
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	cfg.nvs_enable = 1;  // Enable NVS for credential storage
	esp_wifi_init(&cfg);

	// turn off powersaving mode for fast connection
	esp_wifi_set_ps(WIFI_PS_NONE);
	esp_wifi_set_mode(WIFI_MODE_STA);

	esp_event_handler_instance_t instance_any_id;
	esp_event_handler_instance_t instance_got_ip;
	esp_event_handler_instance_register(WIFI_EVENT,
						ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id);
	esp_event_handler_instance_register(IP_EVENT,
						IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip);

	wifi_config_t wifi_config = {
		.sta = {
			.scan_method = WIFI_FAST_SCAN,		// use fast scan
			.sort_method = WIFI_CONNECT_AP_BY_SIGNAL,  // Connect to strongest
			.threshold.authmode = WIFI_AUTH_WPA2_PSK,
		},
	};

	int has_saved_wifi = wifi_load_conn_info();

	if (has_saved_wifi && strlen(saved_wifi.ssid) > 0 && strlen(saved_wifi.password) > 0) {
		//# USE SAVED WIFI
		ESP_LOGW(TAG_WIFI, "%s USE-SAVED-INFO channel=%d", method_name, saved_wifi.channel);

		// Copy SSID
		size_t ssid_len = strlen(saved_wifi.ssid);
		memcpy(wifi_config.sta.ssid, saved_wifi.ssid, ssid_len);
		wifi_config.sta.ssid[ssid_len] = '\0';

		// Copy password
		size_t pass_len = strlen(saved_wifi.password);
		memcpy(wifi_config.sta.password, saved_wifi.password, pass_len);
		wifi_config.sta.password[pass_len] = '\0';

		// Use saved BSSID if available
		if (saved_wifi.bssid[0] != 0) {
			memcpy(wifi_config.sta.bssid, saved_wifi.bssid, 6);
			wifi_config.sta.bssid_set = 1;
		}
	}
	else {
		//# USE DEFAULT CONFIG
		ESP_LOGW(TAG_WIFI, "%s NO-SAVED-INFO use default credentials", method_name);

		// Copy the SSID and password into the configuration structure
		wifi_save_password(EXAMPLE_ESP_WIFI_PASSWORD);
		strncpy((char*)wifi_config.sta.ssid, EXAMPLE_ESP_WIFI_SSID, sizeof(wifi_config.sta.ssid));
		strncpy((char*)wifi_config.sta.password, EXAMPLE_ESP_WIFI_PASSWORD, sizeof(wifi_config.sta.password));
	}

	esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
	esp_wifi_start();

	ESP_LOGW(TAG_WIFI, "%s FINISHED-INIT", method_name);
}
