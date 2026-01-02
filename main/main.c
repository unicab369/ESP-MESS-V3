#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "driver/gpio.h"

#include "mod_spi.h"
#include "mod_sd.h"

#include "mod_wifi.h"
#include "WIFI_CRED.h"

#define LED_PIN 22
#define MODE_PIN 12

#define RECORD_SIZE = 10; // 4 + 2 + 2 + 2 = 10 bytes

static const char *TAG = "[ESP-MESS]";
static uint8_t s_led_state = 0;

int random_int(int min, int max) {
	return min + rand() % (max - min + 1);
}

void toggle_led() {
	gpio_set_level(LED_PIN, s_led_state);
	s_led_state = !s_led_state;	
}

esp_err_t HTTP_GET_CONFIG_HANDLER(httpd_req_t *req) {
	// Set response headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");    
	httpd_resp_set_type(req, "application/json");

    char response[1024];
	int response_len = make_device_configs_str(response, sizeof(response));
    return httpd_resp_send(req, response, response_len);
}

esp_err_t HTTP_SCAN_HANDLER(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");    
    httpd_resp_set_type(req, "application/json");
    
    char response[2048];
    char *ptr = response;
    size_t remaining = sizeof(response);
    
    // Start JSON object
    int written = snprintf(ptr, remaining, "{\"caches\":");
    ptr += written;
    remaining -= written;
    
    // Add caches array
    written = make_device_caches_str(ptr, remaining);
    if (written == 0) return ESP_FAIL;
    ptr += written;
    remaining -= written;
    
    // Add comma and configs key
    written = snprintf(ptr, remaining, ",\"cfgs\":");
    ptr += written;
    remaining -= written;
    
    // Add configs array
    written = make_device_configs_str(ptr, remaining);
    if (written == 0) return ESP_FAIL;
    ptr += written;
    remaining -= written;
    
    // Close JSON object
    written = snprintf(ptr, remaining, "}");
    ptr += written;
    
    return httpd_resp_send(req, response, ptr - response);
}

esp_err_t HTTP_SAVE_CONFIG_HANDLER(httpd_req_t *req) {
	httpd_resp_set_type(req, "text/plain");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

	char device_id[9] = {0};
	char config_str[16] = {0};

	char query[128];
	size_t query_len = httpd_req_get_url_query_len(req) + 1;
	if (query_len > sizeof(query)) query_len = sizeof(query);

	if (httpd_req_get_url_query_str(req, query, query_len) == ESP_OK) {
		httpd_query_key_value(query, "dev", device_id, sizeof(device_id));
		httpd_query_key_value(query, "cfg", config_str, sizeof(config_str));
	}

	uint32_t uuid = hex_to_uint32_unrolled(device_id);

	// Validate parameters
	if (uuid < 1) {
		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing parameters");
		return ESP_OK;
	}

	uint32_t config = (uint32_t)strtoul(config_str, NULL, 10);	// decimal base 10
	printf("Saving Config uuid: %08lX, Config: %ld\n", uuid, config);

	if (sd_save_config(uuid, config) != ESP_OK) {
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save config");
		return ESP_OK;
	}

	return httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
}

esp_err_t HTTP_GET_ESPLOG_HANDLER(httpd_req_t *req) {
	httpd_resp_set_type(req, "text/plain");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

	char type_str[4] = {0};
	char size_str[8] = {0};
	char query[128];
	size_t query_len = httpd_req_get_url_query_len(req) + 1;
	if (query_len > sizeof(query)) query_len = sizeof(query);

	if (httpd_req_get_url_query_str(req, query, query_len) == ESP_OK) {
		httpd_query_key_value(query, "type", type_str, sizeof(type_str));
		httpd_query_key_value(query, "size", size_str, sizeof(size_str));
	}

	int type = atoi(type_str);
	int size = atoi(size_str);

	return ESP_OK;
}

// STEP1: /log/<uuid>/2025/latest.bin - 1 hour of record every second (3600 records)
// STEP2: /log/<uuid>/2025/1230.bin - 24 hours records every minute (1440 records - 60 per hour)
// STEP3: /log/<uuid>/2025/12.bin - 30 days records every 10 minutes (4320 records - 144 per day)

esp_err_t HTTP_DATA_HANDLER(httpd_req_t *req) {
	char query[128];
    char device_id[9] = {0};
	char year[5] = {0};
	char month[3] = {0};
	char day[3] = {0};
	char win[8] = {0};
	int timeWindow = 0;

	// Set response headers
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");    

	size_t query_len = httpd_req_get_url_query_len(req) + 1;
	if (query_len > sizeof(query)) query_len = sizeof(query);

    if (httpd_req_get_url_query_str(req, query, query_len) == ESP_OK) {
        httpd_query_key_value(query, "dev", device_id, sizeof(device_id));        
		httpd_query_key_value(query, "yr", year, sizeof(year));
		httpd_query_key_value(query, "mth", month, sizeof(month));
		httpd_query_key_value(query, "day", day, sizeof(day));
		httpd_query_key_value(query, "win", win, sizeof(win));
		timeWindow = atoi(win);
    }

	// Validate parameters
	if (year < 0 || (month < 0 && day < 0)) {
		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing parameters");
		return ESP_OK;
	}
    ESP_LOGW(TAG_HTTP, "Request device: %s, yr: %s, mth: %s, day: %s, window: %d", 
		device_id, year, month, day, timeWindow);

	// Check if device is valid
	uint32_t uuid = hex_to_uint32_unrolled(device_id);
	int found_and_validated = 0;

	for (int i=0; i < LOG_RECORD_COUNT; i++) {
		if (RECORD_AGGREGATE[i].uuid == uuid && RECORD_AGGREGATE[i].timestamp > 0) {
			found_and_validated = 1;
			break;
		}
	}

	if (!found_and_validated) {
		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing Data");
		return ESP_OK;
	}

	// Fetch data from SD card
	char path[64];
	if (timeWindow > 59 && strlen(month) > 0 && strlen(day) > 0) {
		snprintf(path, sizeof(path), MOUNT_POINT"/log/%s/%s/%s%s.bin", device_id, year, month, day);
	} else {
		snprintf(path, sizeof(path), MOUNT_POINT"/log/%s/%s/latest.bin", device_id, year);
	}

	FILE* _file = fopen(path, "rb");
	if (_file == NULL) {
		ESP_LOGE(TAG, "Failed to open file for reading");
        // Send error response
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Data file not found");
		return ESP_OK;
	}

	// Stream file content in chunks
	char buffer[1024];
	size_t bytes_read;
	size_t total_bytes = 0;

	while ((bytes_read = fread(buffer, 1, sizeof(buffer), _file)) > 0) {
		esp_err_t err = httpd_resp_send_chunk(req, buffer, bytes_read);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "Error sending chunk: %d", err);
			fclose(_file);
			return err;
		}
		total_bytes += bytes_read;
	}
	fclose(_file);
	ESP_LOGW(TAG, "path %s: %d Bytes", path, total_bytes);

	// End chunked response
	return httpd_resp_send_chunk(req, NULL, 0);
}


void app_main(void) {
	//! nvs_flash required for WiFi, ESP-NOW, and other stuff.
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);
	ESP_LOGI(TAG, "APP START");

	//# Setup Blinking
	gpio_reset_pin(LED_PIN);
	gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

	//# Setup Mode Pin
	gpio_reset_pin(MODE_PIN);
	gpio_set_direction(MODE_PIN, GPIO_MODE_INPUT);
	gpio_set_pull_mode(MODE_PIN, GPIO_PULLUP_ONLY);

	//# Setup Wifi
	wifi_init_sta(EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASSWORD);

	//# Init SPI1 peripherals
	M_Spi_Conf spi_conf0 = {
		.host = 1,
		.mosi = CONFIG_SPI_MOSI,
		.miso = CONFIG_SPI_MISO,
		.clk = CONFIG_SPI_CLK,
		.cs = -1,
	};

	ret = mod_spi_init(&spi_conf0, 20E6);

	esp_log_level_set(TAG_LOG_SD, ESP_LOG_NONE);

	if (ret == ESP_OK) {
		//! NOTE: for MMC D3 or CS needs to be pullup if not used otherwise it will go into SPI mode
		ret = sd_spi_config(spi_conf0.host, spi_conf0.cs);
		
		if (ret == ESP_OK) {
			// sd_test();
			
			//# Delete log folder
			int mode = gpio_get_level(MODE_PIN);
			if (mode == 0) {
				sd_remove_dir(MOUNT_POINT"/log2");
			}

			if (!sd_ensure_dir(MOUNT_POINT"/log")) {
				ESP_LOGE(TAG_SD, "Err create /log");
			}

			sd_load_config();
		}
	}

	while (1) {
		toggle_led();

		struct tm timeinfo = timeinfo_now();
		if (timeinfo.tm_year > 70) {
			// year number starts at 1900, epoch year is 1970
			ESP_LOGI_SD(TAG, "T%s", GET_TIME_STR);
			uint32_t uuid = 0xAABBCCDA;

			uint32_t now = (uint32_t)time_now();

			for (int i=0; i<10; i++) {
				record_t records = {
					.timestamp = now,  // Fixed timestamp
					// .value1 = 10,
					// .value2 = 20,
					// .value3 = 30
					.value1 = random_int(20, 30),
					.value2 = random_int(70, 80),
					.value3 = random_int(0, 100),
				};

				uuid += i;
				cache_device(uuid, now);
				sd_bin_record_all(uuid, now, &timeinfo, &records);
			}
		}

		wifi_poll();
		vTaskDelay(1000/portTICK_PERIOD_MS);
	}
}