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

#include "mod_littlefs_log.h"
#include "mod_nvs.h"


#define LED_PIN 22
#define MODE_PIN 12

#define RECORD_SIZE = 10; // 4 + 2 + 2 + 2 = 10 bytes

static const char *TAG = "[APP]";
static uint8_t s_led_state = 0;

// why: use mutex to prevent simultaneous access to sd card from logging and http requests
SemaphoreHandle_t SD_MUTEX = NULL;

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

esp_err_t send_http_file(httpd_req_t *req, const char *path) {
	int mutex_taken = 1;

	FILE* file = fopen(path, "rb");
	if (file == NULL) {
		ESP_LOGE_SD(TAG_HTTP, "Err opening file: %s", path);
		xSemaphoreGive(SD_MUTEX);  // Release mutex before returning

        // Send error response
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
		return ESP_OK;
	}

	// Stream file content in chunks
	char buffer[1024];
	size_t bytes_read;
	size_t total_bytes = 0;

	while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
		// Release mutex while sending network data
		xSemaphoreGive(SD_MUTEX);
		mutex_taken = 0;

		esp_err_t err = httpd_resp_send_chunk(req, buffer, bytes_read);
		if (err != ESP_OK) {
			ESP_LOGE_SD(TAG_HTTP, "Err sending chunk: %d", err);
			fclose(file);
			return err;
		}
		total_bytes += bytes_read;

		// Re-acquire mutex for next fread
		if (xSemaphoreTake(SD_MUTEX, pdMS_TO_TICKS(100)) != pdTRUE) {
			ESP_LOGE_SD(TAG_HTTP, "Err SD mutex timeout");
			fclose(file);
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card busy");
			return ESP_OK;
		}
		mutex_taken = 1;
	}
	fclose(file);

	// Release mutex if it was taken
	if (mutex_taken) xSemaphoreGive(SD_MUTEX);
	ESP_LOGW_SD(TAG_HTTP, "path %s: %d Bytes", path, total_bytes);

	// End chunked response
	return httpd_resp_send_chunk(req, NULL, 0);
}

esp_err_t HTTP_GET_LOG_HANDLER(httpd_req_t *req) {
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	httpd_resp_set_type(req, "text/plain");

	char query[128];
	char type_str[4] = {0};
	char size_str[8] = {0};
	char pa_str[16] = {0};
	char pb_str[16] = {0};
	char pc_str[16] = {0};

	size_t query_len = httpd_req_get_url_query_len(req) + 1;
	if (query_len > sizeof(query)) query_len = sizeof(query);

	if (httpd_req_get_url_query_str(req, query, query_len) == ESP_OK) {
		httpd_query_key_value(query, "type", type_str, sizeof(type_str));
		httpd_query_key_value(query, "size", size_str, sizeof(size_str));
		httpd_query_key_value(query, "pa", pa_str, sizeof(pa_str));
		httpd_query_key_value(query, "pb", pb_str, sizeof(pb_str));
		httpd_query_key_value(query, "pc", pc_str, sizeof(pc_str));
	}

	int type = atoi(type_str);
	int size = atoi(size_str);

	char full_path[64];
	snprintf(full_path, sizeof(full_path), SD_POINT"/log/%s/%s/%s", pa_str, pb_str, pc_str);

	return send_http_file(req, full_path);
}

// STEP1: /log/<uuid>/2025/latest.bin - 1 hour of record every second (3600 records)
// STEP2: /log/<uuid>/2025/1230.bin - 24 hours records every minute (1440 records - 60 per hour)
// STEP3: /log/<uuid>/2025/12.bin - 30 days records every 10 minutes (4320 records - 144 per day)

esp_err_t HTTP_DATA_HANDLER(httpd_req_t *req) {
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");    
	httpd_resp_set_type(req, "text/plain");
	
	char query[128];
	char device_id[9] = {0};
	char year[5] = {0};
	char month[3] = {0};
	char day[3] = {0};
	char win[8] = {0};
	int timeWindow = 0;

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
    ESP_LOGW_SD(TAG_HTTP, "Request dev: %s, yr: %s, mth: %s, day: %s, window: %d", 
		device_id, year, month, day, timeWindow);

	// Check if device is valid
	uint32_t uuid = hex_to_uint32_unrolled(device_id);
	int found_and_validated = 0;

	for (int i=0; i < LOG_RECORD_COUNT; i++) {
		if (RECORD_AGGREGATE[i].uuid == uuid && RECORD_AGGREGATE[i].last_log_rotation_sec > 0) {
			found_and_validated = 1;
			break;
		}
	}

	if (!found_and_validated) {
		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing Data");
		return ESP_OK;
	}

	//# Fetch data from SD card
	char path_str[64];
	if (timeWindow > 59 && strlen(month) > 0 && strlen(day) > 0) {
		snprintf(path_str, sizeof(path_str), SD_POINT"/log/%s/%s/%s%s.bin", device_id, year, month, day);
	} else {
		snprintf(path_str, sizeof(path_str), SD_POINT"/log/%s/%s/latest.bin", device_id, year);
	}

	return send_http_file(req, path_str);
}

esp_err_t HTTP_GET_FILES_HANDLER(httpd_req_t *req) {
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");    
	httpd_resp_set_type(req, "application/json");

	char query[128];
	char entry_str[64] = {0};
	char txt_str[4] = {0};
	char bin_str[4] = {0};
	char output[1024] = {0};

	size_t query_len = httpd_req_get_url_query_len(req) + 1;
	if (query_len > sizeof(query)) query_len = sizeof(query);

	if (httpd_req_get_url_query_str(req, query, query_len) == ESP_OK) {
		httpd_query_key_value(query, "sub", entry_str, sizeof(entry_str));
		httpd_query_key_value(query, "txt", txt_str, sizeof(txt_str));
		httpd_query_key_value(query, "bin", bin_str, sizeof(bin_str));
	}

	if (memcmp(entry_str, "nvs", 3) == 0) {
		int len = mod_nvs_listKeys_json(NULL, output, sizeof(output));
		return httpd_resp_send(req, output, len);
	}
	else if (memcmp(entry_str, "*sd", 3) == 0 || memcmp(entry_str, "*litt", 5) == 0) {
		// continue
	}
	else {
		httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized Access");
		return ESP_FAIL;
	}

	// replace ',' with '/'
	for (char *p = entry_str; *p; p++) {
		if (*p == '*') *p = '/';
	}

	int is_txt = atoi(txt_str);
	int is_bin = atoi(bin_str);

	int len = 0;
	if (is_txt || is_bin) {
		httpd_resp_set_type(req, "text/plain");
		len = sd_read_tail(entry_str, output, sizeof(output));
	} else {
		len = sd_entries_to_json(entry_str, output, sizeof(output));
	}
	ESP_LOGW_SD(TAG_HTTP, "path %s", entry_str);

	return httpd_resp_send(req, output, len);
}

void app_main(void) {
	esp_err_t ret;
	SD_MUTEX = xSemaphoreCreateMutex();
	ESP_LOGI(TAG, "APP START");
	
	//! nvs_flash required for WiFi, ESP-NOW, and other stuff.
	mod_nvs_setup();

	ret = mod_nvs_open("test2");
	if (ret == ESP_OK) {
		uint8_t val0;
		uint16_t val1;
		uint32_t val2;
		char str0[32];

		nvs_get_u8(NVS_HANDLER, "val0", &val0);
		nvs_get_u16(NVS_HANDLER, "val1", &val1);
		nvs_get_u32(NVS_HANDLER, "val2", &val2);
		size_t len = sizeof(str0);
		nvs_get_str(NVS_HANDLER, "val3", str0, &len);
		printf("old val0=%d, val1=%d, val2=%ld, val3=%s\n", val0, val1, val2, str0);

		///////////////////////

		char output[32];
		snprintf(output, sizeof(output), "hello %d", val0 + 10);
		nvs_set_u8(NVS_HANDLER, "val0", val0 + 10);
		nvs_set_u16(NVS_HANDLER, "val1", val1 + 11);
		nvs_set_u32(NVS_HANDLER, "val2", val2 + 12);
		nvs_set_str(NVS_HANDLER, "val3", output);
		nvs_commit(NVS_HANDLER);
		nvs_close(NVS_HANDLER);
		
		mod_nvs_listKeys("test1");
		mod_nvs_listKeys(NULL);
	}

	littleFS_init();

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

	//# Set Logs level
	// esp_log_level_set(TAG_LOG_SD, ESP_LOG_NONE);
	// esp_log_level_set(TAG_HTTP, ESP_LOG_NONE);
	esp_log_level_set(TAG, ESP_LOG_NONE);

	if (ret == ESP_OK) {
		//! NOTE: for MMC D3 or CS needs to be pullup if not used otherwise it will go into SPI mode
		ret = sd_spi_config(spi_conf0.host, spi_conf0.cs);
		
		if (ret == ESP_OK) {
			// sd_test();
			
			//# Delete log folder
			int mode = gpio_get_level(MODE_PIN);
			if (mode == 0) {
				sd_remove_dir(SD_POINT"/log2");
			}

			if (!sd_ensure_dir(SD_POINT"/log")) {
				ESP_LOGE(TAG_SD, "Err create /log");
			}
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

				// Take mutex ONCE for the entire operation
				if (xSemaphoreTake(SD_MUTEX, pdMS_TO_TICKS(100)) != pdTRUE) {
					ESP_LOGE_SD(TAG_SD, "Err SD mutex timeout");
					return;
				}
				// sd_bin_record_all(uuid, now, &timeinfo, &records);

				rotate_timeLog_write(uuid, now, &timeinfo, &records);
				xSemaphoreGive(SD_MUTEX);  // Release mutex
			}
		}

		wifi_poll();
		vTaskDelay(1000/portTICK_PERIOD_MS);
	}
}