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

void reload_logs() {
	esp_err_t ret = mod_nvs_open("s_log");
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Err reload_logs: %s", esp_err_to_name(ret));
		return;
	}

	uint8_t log_sd = 0, log_http = 0, log_app = 0;
	nvs_get_u8(NVS_HANDLER, "SD", &log_sd);
	nvs_get_u8(NVS_HANDLER, "HTTP", &log_http);
	nvs_get_u8(NVS_HANDLER, "APP", &log_app);
	nvs_close(NVS_HANDLER);
	ESP_LOGW(TAG, "Update Logs SD:%d, HTTP:%d, APP:%d", log_sd, log_http, log_app);

	//# Set Logs level
	esp_log_level_set(TAG_SD, log_sd);
	esp_log_level_set(TAG_HTTP, log_http);
	esp_log_level_set(TAG, log_app);
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

esp_err_t HTTP_UPDATE_NVS_HANDLER(httpd_req_t *req) {
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");    
	httpd_resp_set_type(req, "application/json");

	char query[128];
	char name_str[11] = {0};
	char new_key[11] = {0};
	char old_key[11] = {0};
	char val_str[32] = {0};
	char type_str[4] = {0};
	char output[1024] = {0};

	size_t query_len = httpd_req_get_url_query_len(req) + 1;
	if (query_len > sizeof(query)) query_len = sizeof(query);

	if (httpd_req_get_url_query_str(req, query, query_len) == ESP_OK) {
		httpd_query_key_value(query, "name", name_str, sizeof(name_str));
		httpd_query_key_value(query, "new_k", new_key, sizeof(new_key));
		httpd_query_key_value(query, "old_k", old_key, sizeof(old_key));
		httpd_query_key_value(query, "val", val_str, sizeof(val_str));
		httpd_query_key_value(query, "typ", type_str, sizeof(type_str));
	}

	int type = atoi(type_str);
	int value = atoi(val_str);
	size_t len = 0;
	esp_err_t ret = ESP_OK;
	int has_old_key = strlen(old_key) > 0;

	//# open namespace first
	if (strlen(name_str) > 0) {
		ret = mod_nvs_open(name_str);
		if (ret != ESP_OK) {
			httpd_resp_set_type(req, "text/plain");
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Err mod_nvs_open");
			return ESP_OK;
		}
	}

	// Delete if type == 0 and value == 0
	if (type == 0 && value == 0) {
		//# Delete - return list
		if (has_old_key) {
			nvs_erase_key(NVS_HANDLER, old_key);
		} else {
			nvs_erase_all(NVS_HANDLER);
		}
		
		nvs_commit(NVS_HANDLER);
		nvs_close(NVS_HANDLER);
		len = mod_nvs_listKeys_json(NULL, output, sizeof(output));
		return httpd_resp_send(req, output, len);
	}
	else if (has_old_key) {
		// if old_key != new_key then delete okd_key first
		if (
			memcmp(new_key, old_key, sizeof(old_key)) != 0
		) {
			nvs_erase_key(NVS_HANDLER, old_key);
		}

		// then set/update
		switch (type) {
			case 1: nvs_set_u8(NVS_HANDLER, new_key, (uint8_t)value); break;
			case 2: nvs_set_u16(NVS_HANDLER, new_key, (uint16_t)value); break;
			case 4: nvs_set_u32(NVS_HANDLER, new_key, (uint32_t)value); break;
			case 8: nvs_set_u64(NVS_HANDLER, new_key, (uint64_t)value); break;
			case 17: nvs_set_i8(NVS_HANDLER, new_key, (int8_t)value); break;
			case 18: nvs_set_i16(NVS_HANDLER, new_key, (int16_t)value); break;
			case 20: nvs_set_i32(NVS_HANDLER, new_key, (int32_t)value); break;
			case 24: nvs_set_i64(NVS_HANDLER, new_key, (int64_t)value); break;
			case 33: nvs_set_str(NVS_HANDLER, new_key, val_str); break;
			default: break;
		}
		nvs_commit(NVS_HANDLER);
		nvs_close(NVS_HANDLER);

		if (memcmp(name_str, "s_log", 5) == 0) {
			reload_logs();
		}

		len = mod_nvs_listKeys_json(NULL, output, sizeof(output));
		return httpd_resp_send(req, output, len);
	}
	else {
		// if no old_key then get
		uint8_t u8_val;uint16_t u16_val; uint32_t u32_val; uint64_t u64_val;
		int8_t i8_val; int16_t i16_val; int32_t i32_val; int64_t i64_val;

		//# Get - Return individual value
		switch (type) {
			case 1: {
				nvs_get_u8(NVS_HANDLER, new_key, &u8_val);
				len = snprintf(output, sizeof(output), "{\"val\":%d,\"typ\":1}", u8_val);
				ret = httpd_resp_send(req, output, len);
				break;
			}
			case 2: {
				nvs_get_u16(NVS_HANDLER, new_key, &u16_val);
				len = snprintf(output, sizeof(output), "{\"val\":%d,\"typ\":2}", u16_val);
				ret = httpd_resp_send(req, output, len);
				break;
			}
			case 4: {
				nvs_get_u32(NVS_HANDLER, new_key, &u32_val);
				len = snprintf(output, sizeof(output), "{\"val\":%ld,\"typ\":4}", u32_val);
				ret = httpd_resp_send(req, output, len);
				break;
			}
			case 8: {
				nvs_get_u64(NVS_HANDLER, new_key, &u64_val);
				len = snprintf(output, sizeof(output), "{\"val\":%lld,\"typ\":8}", u64_val);
				ret = httpd_resp_send(req, output, len);
				break;
			}
			case 17: {
				nvs_get_i8(NVS_HANDLER, new_key, &i8_val);
				len = snprintf(output, sizeof(output), "{\"val\":%d,\"typ\":17}", i8_val);
				ret = httpd_resp_send(req, output, len);
				break;
			}
			case 18: {
				nvs_get_i16(NVS_HANDLER, new_key, &i16_val);
				len = snprintf(output, sizeof(output), "{\"val\":%d,\"typ\":18}", i16_val);
				ret = httpd_resp_send(req, output, len);
				break;
			}
			case 20: {
				nvs_get_i32(NVS_HANDLER, new_key, &i32_val);
				len = snprintf(output, sizeof(output), "{\"val\":%ld,\"typ\":20}", i32_val);
				ret = httpd_resp_send(req, output, len);
				break;
			}
			case 24: {
				nvs_get_i64(NVS_HANDLER, new_key, &i64_val);
				len = snprintf(output, sizeof(output), "{\"val\":%lld,\"typ\":24}", i64_val);
				ret = httpd_resp_send(req, output, len);
				break;
			}
			case 33: {
				len = sizeof(val_str);
				nvs_get_str(NVS_HANDLER, new_key, val_str, &len);
				len = snprintf(output, sizeof(output), "{\"val\":\"%s\",\"typ\":33}", val_str);
				ret = httpd_resp_send(req, output, len);
				break;
			}
			default:
				ret = httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN); 
				break;
		}

		nvs_close(NVS_HANDLER);
		return ret;
	}
}

esp_err_t HTTP_UPDATE_ENTRY_HANDLER(httpd_req_t *req) {
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");    
	httpd_resp_set_type(req, "text/plain");

	char query[128];
	char new_path[64] = {0};
	char old_path[64] = {0};
	char isFile_str[4] = {0};

	size_t query_len = httpd_req_get_url_query_len(req) + 1;
	if (query_len > sizeof(query)) query_len = sizeof(query);

	if (httpd_req_get_url_query_str(req, query, query_len) == ESP_OK) {
		httpd_query_key_value(query, "new", new_path, sizeof(new_path));
		httpd_query_key_value(query, "old", old_path, sizeof(old_path));
		httpd_query_key_value(query, "isFile_str", old_path, sizeof(isFile_str));
	}
	// replace '*' with '/'
	for (char *p = new_path; *p; p++) if (*p == '*') *p = '/';
	for (char *p = old_path; *p; p++) if (*p == '*') *p = '/';

	esp_err_t ret;
	int new_name_len = strlen(new_path);
	int old_name_len = strlen(old_path);
	int is_file = atoi(isFile_str);

	// no old_name => Create
	if (!old_name_len) {
		ESP_LOGW(TAG, "create: %s", new_path);
		sd_ensure_dir(new_path);
	}
	// no new_name => Delete
	else if (!new_name_len) {
		ESP_LOGW(TAG, "remove: %s", old_path);

		if (is_file) {
			sd_remove_file(old_path);
		} else {
			sd_remove_dir_recursive(old_path);
		}
	}
	// otherwise => Rename
	else if (new_name_len && old_name_len) {
		ESP_LOGW(TAG, "rename: %s -> %s", old_path, new_path);
		
		ret = sd_rename(old_path, new_path);
		if (ret == ESP_OK) {
			return httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
		}
	}

	return httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
}

esp_err_t HTTP_GET_ENTRIES_HANDLER(httpd_req_t *req) {
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

	// replace '*' with '/'
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
	reload_logs();

	// ret = mod_nvs_open("test2");
	// if (ret == ESP_OK) {
	// 	uint8_t val0;
	// 	uint16_t val1;
	// 	uint32_t val2;
	// 	char str0[32];

	// 	nvs_get_u8(NVS_HANDLER, "val0", &val0);
	// 	nvs_get_u16(NVS_HANDLER, "val1", &val1);
	// 	nvs_get_u32(NVS_HANDLER, "val2", &val2);
	// 	size_t len = sizeof(str0);
	// 	nvs_get_str(NVS_HANDLER, "val3", str0, &len);
	// 	printf("old val0=%d, val1=%d, val2=%ld, val3=%s\n", val0, val1, val2, str0);

	// 	///////////////////////

	// 	char output[32];
	// 	snprintf(output, sizeof(output), "hello %d", val0 + 10);
	// 	nvs_set_u8(NVS_HANDLER, "val0", val0 + 10);
	// 	nvs_set_u16(NVS_HANDLER, "val1", val1 + 11);
	// 	nvs_set_u32(NVS_HANDLER, "val2", val2 + 12);
	// 	nvs_set_str(NVS_HANDLER, "val3", output);
	// 	nvs_commit(NVS_HANDLER);
	// 	nvs_close(NVS_HANDLER);
		
	// 	mod_nvs_listKeys("test1");
	// 	mod_nvs_listKeys(NULL);
	// }

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

	if (ret == ESP_OK) {
		//! NOTE: for MMC D3 or CS needs to be pullup if not used otherwise it will go into SPI mode
		ret = sd_spi_config(spi_conf0.host, spi_conf0.cs);
		
		if (ret == ESP_OK) {
			// sd_test();
			
			//# Delete log folder
			int mode = gpio_get_level(MODE_PIN);
			if (mode == 0) {
				sd_remove_dir_recursive(SD_POINT"/log2");
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