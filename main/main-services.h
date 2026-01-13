#include "mod_wifi.h"
#include "WIFI_CRED.h"

void elapse_start(uint64_t *timestamp) {
	*timestamp = esp_timer_get_time();
}

uint64_t elapse_stop(uint64_t *timestamp) {
	return esp_timer_get_time() - *timestamp;
}

void elapse_print(const char *prefix, uint64_t *timestamp) {
	uint64_t elapsed = elapse_stop(timestamp);
	printf("%s elapsed: %lld us\n", prefix, elapsed);
}

#include "mod_littlefs_log.h"
#include "mod_nvs.h"
#include "mod_spi.h"
#include "mod_sd.h"

#include "../components/analytics.h"

#define RECORD_SIZE sizeof(record_t)				// 10 bytes
#define HTTP_CHUNK_SIZE 4096
static char HTTP_FILE_BUFFER[HTTP_CHUNK_SIZE];

// why: use mutex to prevent simultaneous access to sd card from logging and http requests
// design: mutex on read and queue on write to SD card
SemaphoreHandle_t FS_MUTEX = NULL;
atomic_stats_t http_stats = {0};

void SERV_RELOAD_LOGS();

esp_err_t HTTP_GET_CONFIG_HANDLER(httpd_req_t *req) {
	atomic_tracker_start(&http_stats);
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	httpd_resp_set_type(req, "application/json");

	char response[1024];
	int response_len = make_device_configs_str(response, sizeof(response));
	atomic_tracker_end(&http_stats);
	return httpd_resp_send(req, response, response_len);
}

esp_err_t HTTP_SCAN_HANDLER(httpd_req_t *req) {
	atomic_tracker_start(&http_stats);
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");    
	httpd_resp_set_type(req, "application/json");
	
	char response[1024];
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
	
	atomic_tracker_end(&http_stats);
	return httpd_resp_send(req, response, ptr - response);
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

		//# then Set/Update
		switch (type) {
			case 1: nvs_set_u8(NVS_HANDLER, new_key, (uint8_t)value); break;
			case 2: nvs_set_u16(NVS_HANDLER, new_key, (uint16_t)value); break;
			case 4: nvs_set_u32(NVS_HANDLER, new_key, (uint32_t)value); break;
			case 8: nvs_set_u64(NVS_HANDLER, new_key, (uint64_t)value); break;
			case 17: nvs_set_i8(NVS_HANDLER, new_key, (int8_t)value); break;
			case 18: nvs_set_i16(NVS_HANDLER, new_key, (int16_t)value); break;
			case 20: nvs_set_i32(NVS_HANDLER, new_key, (int32_t)value); break;
			case 24: nvs_set_i64(NVS_HANDLER, new_key, (int64_t)value); break;
			case 33: {
				//! Decode URL encoded string
				url_decode_inplace(val_str);
				nvs_set_str(NVS_HANDLER, new_key, val_str);
				break;
			}
			default: break;
		}
		nvs_commit(NVS_HANDLER);
		nvs_close(NVS_HANDLER);

		if (memcmp(name_str, "s_log", 5) == 0) {
			SERV_RELOAD_LOGS();
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

				if (memcmp(new_key, "CRED", 4) == 0) {
					len = snprintf(output, sizeof(output), "{\"val\":\"\",\"typ\":33}");
				} else {
					len = snprintf(output, sizeof(output), "{\"val\":\"%s\",\"typ\":33}", val_str);
				}

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


int FS_ACCESS_START(httpd_req_t *req) {
	atomic_tracker_start(&http_stats);

	// #Take Mutex - if FS is locked (wait max 50 seconds)
	// also prevent changes on params when multiple clients request simultaneously
	if (xSemaphoreTake(FS_MUTEX, pdMS_TO_TICKS(50)) != pdTRUE) {
		// FS is busy - tell client to wait
		atomic_tracker_end(&http_stats);
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "FS busy");
		return 0;
	}

	return 1;
}

void FS_ACCESS_RELEASE() {
	xSemaphoreGive(FS_MUTEX);
	atomic_tracker_end(&http_stats);
}

// fs_access
esp_err_t HTTP_SAVE_CONFIG_HANDLER(httpd_req_t *req) {
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	httpd_resp_set_type(req, "text/plain");

	const char method_name[] = "HTTP_SAVE_CONFIG_HANDLER";
	char device_id[9] = {0};
	char config_str[16] = {0};

	char query[128];
	size_t query_len = httpd_req_get_url_query_len(req) + 1;
	if (query_len > sizeof(query)) query_len = sizeof(query);

	if (httpd_req_get_url_query_str(req, query, query_len) == ESP_OK) {
		httpd_query_key_value(query, "dev", device_id, sizeof(device_id));
		httpd_query_key_value(query, "cfg", config_str, sizeof(config_str));
	}

	// Validate parameters
	uint32_t uuid = hex_to_uint32_unrolled(device_id);
	if (uuid < 1) {
		ESP_LOGE(TAG_HTTP, "%s INVALID-UUID: %s", method_name, device_id);
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing parameters");
	}

	uint32_t config = (uint32_t)strtoul(config_str, NULL, 10);	// decimal base 10
	ESP_LOGW(TAG_HTTP, "%s uuid: %08lX, Config: %ld", method_name, uuid, config);

	//# FS_ACCESS: start here to allow other tasks to work while this handler get to this point
	// concurrent requests will be waiting here, they all have their own stack so their variables are safe
	if (!FS_ACCESS_START(req)) return ESP_OK;

	if (sd_save_config(uuid, config) != ESP_OK) {
		FS_ACCESS_RELEASE();	//# FS RELEASE
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save config");
	}

	FS_ACCESS_RELEASE();	//# FS RELEASE
	return httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
}

// fs_access
esp_err_t http_send_file_chunks(httpd_req_t *req, void *buffer, const char *path) {
	if (!FS_ACCESS_START(req)) return ESP_OK;
	const char method_name[] = "http_send_file_chunks";
	FILE* file = fopen(path, "rb");			// ~7.0ms

	if (file == NULL) {
		ESP_LOGE(TAG_HTTP, "Err %s Not Found %s", method_name, path);
		FS_ACCESS_RELEASE();	//# FS RELEASE
		return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
	}

	// Stream file content in chunks
	size_t bytes_read;
	size_t total_bytes = 0;
	uint64_t start_time;

	elapse_start(&start_time);

	while ((bytes_read = fread(buffer, 1, HTTP_CHUNK_SIZE, file)) > 0) {
		esp_err_t ret = httpd_resp_send_chunk(req, buffer, bytes_read);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG_HTTP, "Err %s httpd_resp_send_chunk", method_name);
			fclose(file);
			FS_ACCESS_RELEASE();	//# FS RELEASE
			return ret;
		}
		total_bytes += bytes_read;
	}

	ESP_LOGW(TAG_HTTP, "%s sent: %s %dB in %lldus",
		method_name, path, total_bytes, elapse_stop(&start_time));
	fclose(file);
	FS_ACCESS_RELEASE();	//# FS RELEASE

	return ESP_OK;
}

int http_send_record_chunks(httpd_req_t *req, char *path, char *buffer) {
	if (!FS_ACCESS_START(req)) return 0;
	const char method_name[] = "http_send_record_chunks";
	FILE* file = fopen(path, "rb");		// ~7.0ms

	if (!file) {
		ESP_LOGE(TAG_HTTP, "Err %s Not Found %s", method_name, path);
		FS_ACCESS_RELEASE();
		httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, NULL);
		return 0;
	}

	size_t bytes_read;
	size_t total_bytes = 0;
	file_header_t header;
	// Skip header ~ 1.8ms fseek and fread takes about the same time
	fread(&header, 1, HEADER_SIZE, file);

	// ~3ms to read a chunk
	while ((bytes_read = fread(buffer, 1, HTTP_CHUNK_SIZE, file)) > 0) {
		// uint32_t f_timestamp = *(uint32_t*)buffer;
        // printf("*** f_timestamp: %ld\n", f_timestamp);

		// Send entire chunk
		if (httpd_resp_send_chunk(req, buffer, bytes_read) != ESP_OK) {
			ESP_LOGE(TAG_HTTP, "Err %s sending_chunk", method_name);
			fclose(file);
			FS_ACCESS_RELEASE();
			return 0;
		}
		total_bytes += bytes_read;
	}
	fclose(file);
	FS_ACCESS_RELEASE();

	return total_bytes;
}

int get_n_records(
	httpd_req_t *req, char *path, char *read_buffer, char *OUTPUT_BUFFER, size_t n_records
) {
	if (!FS_ACCESS_START(req)) return 0;

	FILE* file = fopen(path, "rb");
	if (!file) {
		ESP_LOGE(TAG_HTTP, "Err open %s", path);
		FS_ACCESS_RELEASE();
		httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, NULL);
		return 0;
	}

	// Get file size. takes about 4ms
	fseek(file, 0, SEEK_END);
	size_t file_size = ftell(file);
	fseek(file, 0, SEEK_SET);
	
	size_t total_records = file_size / RECORD_SIZE;
	
	// Calculate optimal spacing for exactly n_records
	size_t spacing;
	if (n_records <= 1) {
		spacing = 0;  // Just get first record
	} else {
		// We want n_records with maximum equal spacing
		// If we want 5 records from 100: positions 0, 25, 50, 75, 99
		spacing = (total_records - 1) / (n_records - 1);
	}
	
	size_t records_collected = 0;
	size_t custom_pos = 0;
	size_t current_record = 0;

	uint64_t start_time;
	elapse_start(&start_time);

	while (records_collected < n_records) {
		size_t bytes_read = fread(read_buffer, 1, HTTP_CHUNK_SIZE, file);
		if (!bytes_read) break;
		size_t records_in_chunk = bytes_read / RECORD_SIZE;
		
		for (size_t i = 0; i < records_in_chunk; i++) {
			// Use calculated spacing
			if (current_record % (spacing + 1) == 0) {
				memcpy(OUTPUT_BUFFER + custom_pos, read_buffer + (i * RECORD_SIZE), RECORD_SIZE);
				custom_pos += RECORD_SIZE;
				records_collected++;
				if (records_collected >= n_records) break;
			}
			current_record++;
			if (current_record >= total_records) break;
		}
	}
	
	fclose(file);
	FS_ACCESS_RELEASE();
	printf("**** records_collected %d\n", records_collected);
	
	elapse_print("*** sd readtime", &start_time);
	return records_collected * RECORD_SIZE;
}


// fs_access - internal
esp_err_t HTTP_GET_RECORDS_HANDLER(httpd_req_t *req) {
	const char method_name[] = "HTTP_GET_RECORDS_HANDLER";
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");    
	httpd_resp_set_type(req, "text/plain");
	
	char query[128];
	char device_id[9] = {0};
	char year_str[5] = {0};
	char month_str[3] = {0};
	char day_str[3] = {0};
	char window_str[8] = {0};
	int window = 0, year = 0, month = 0, day = 0;

	char minT_str[16] = {0};
	char maxT_str[16] = {0};
	uint64_t minT_s = 0, maxT_s = 0;

	size_t query_len = httpd_req_get_url_query_len(req) + 1;
	if (query_len > sizeof(query)) query_len = sizeof(query);

	if (httpd_req_get_url_query_str(req, query, query_len) == ESP_OK) {
		httpd_query_key_value(query, "dev", device_id, sizeof(device_id));        
		httpd_query_key_value(query, "yr", year_str, sizeof(year_str));
		httpd_query_key_value(query, "mth", month_str, sizeof(month_str));
		httpd_query_key_value(query, "day", day_str, sizeof(day_str));
		httpd_query_key_value(query, "win", window_str, sizeof(window_str));
		
		window = atoi(window_str);
		year = atoi(year_str);
		month = atoi(month_str);
		day = atoi(day_str);

		httpd_query_key_value(query, "minT", minT_str, sizeof(minT_str));
		httpd_query_key_value(query, "maxT", maxT_str, sizeof(maxT_str));
		minT_s = strtoull(minT_str, NULL, 10);
		maxT_s = strtoull(maxT_str, NULL, 10);
	}

	ESP_LOGI(TAG_HTTP, "%s REQUESTED-DEV", method_name);
	printf("Requested: dev=%s, date=%d/%02d/%02d, window=%dm\n", device_id, year, month, day, window);

	// Validate parameters
	if (year < 0 || (month < 0 && day < 0)) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing parameters");
	}
	
	char file_path[64];
	esp_err_t ret = ESP_OK;
	uint64_t time_ref;
	uint32_t uuid = hex_to_uint32_unrolled(device_id);
	active_records_t *target = find_records_store(uuid);

	if (target) {
		if (window > 60 && month > 0 && day > 0) {
			// if window is greater than 59 minutes, get daily log
			make_aggregate_filePath(file_path, uuid, year%100, month, day, target->file_index);
			ESP_LOGW(TAG_HTTP, "%s REQUEST-AGGREGATE", method_name); printf("at %s\n", file_path);

			file_header_t header;

			if (!FS_ACCESS_START(req)) return httpd_resp_send_chunk(req, NULL, 0);
			elapse_start(&time_ref);
			int len = record_file_read(&header, file_path, HTTP_FILE_BUFFER, RECORD_SIZE, 200);	// ~10ms
			FS_ACCESS_RELEASE();

			ret = httpd_resp_send(req, HTTP_FILE_BUFFER, len * RECORD_SIZE);					// ~5.5ms
			elapse_print("*** httpd_resp_send", &time_ref);
			printf("*** startTime: %ld, latestTime: %ld\n", header.start_time, header.latest_time);

			// elapse_start(&time_ref);
			// http_send_record_chunks(req, file_path, buffer);				// ~25ms YUCK
			// elapse_print("**** http_send_record_chunks", &time_ref);
		} else {
			// takes about 5ms for 300 records
			return httpd_resp_send(req, (const char*)target->records, sizeof(target->records));
		}
	}
	else {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Record not registered");
	}

	return httpd_resp_send_chunk(req, NULL, 0);
}

// fs_access -internal
esp_err_t HTTP_GET_FILE_HANDLER(httpd_req_t *req) {
	const char method_name[] = "HTTP_GET_FILE_HANDLER";
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	httpd_resp_set_type(req, "text/plain");

	char query[128];
	char path[64] = {0};

	size_t query_len = httpd_req_get_url_query_len(req) + 1;
	if (query_len > sizeof(query)) query_len = sizeof(query);

	if (httpd_req_get_url_query_str(req, query, query_len) == ESP_OK) {
		httpd_query_key_value(query, "path", path, sizeof(path));
	}

	for (char *p = path; *p; p++) if (*p == '*') *p = '/';
	ESP_LOGW(TAG_HTTP, "%s send path %s", method_name, path);

	http_send_file_chunks(req, HTTP_FILE_BUFFER, path);
	return httpd_resp_send_chunk(req, NULL, 0);
}

// fs_access
esp_err_t HTTP_UPDATE_FILE_HANDLER(httpd_req_t *req) {
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	httpd_resp_set_type(req, "text/plain");

	char query[128];
	char new_path[64] = {0};
	char old_path[64] = {0};
	char text_str[1024] = {0};

	size_t query_len = httpd_req_get_url_query_len(req) + 1;
	if (query_len > sizeof(query)) query_len = sizeof(query);

	if (httpd_req_get_url_query_str(req, query, query_len) == ESP_OK) {
		httpd_query_key_value(query, "new", new_path, sizeof(new_path));
		httpd_query_key_value(query, "old", old_path, sizeof(old_path));
		httpd_query_key_value(query, "txt", text_str, sizeof(text_str));
	}

	// replace '*' with '/'
	for (char *p = new_path; *p; p++) if (*p == '*') *p = '/';
	for (char *p = old_path; *p; p++) if (*p == '*') *p = '/';

	esp_err_t ret;
	int new_name_len = strlen(new_path);
	int old_name_len = strlen(old_path);

	//# FS_ACCESS: start here to allow other tasks to work while this handler get to this point
	// concurrent requests will be waiting here, they all have their own stack so their variables are safe
	if (!FS_ACCESS_START(req)) return ESP_OK;

	// no old_name => Create
	if (!old_name_len) {
		ESP_LOGW(TAG_HTTP, "create: %s", new_path);
		url_decode_newline(text_str);
		sd_write_str(new_path, text_str);
	}
	// no new_name => Delete
	else if (!new_name_len) {
		ESP_LOGW(TAG_HTTP, "remove: %s", old_path);
		sd_remove_file(old_path);
	}
	// otherwise => Rename
	else if (new_name_len && old_name_len) {
		ESP_LOGW(TAG_HTTP, "rename: %s -> %s", old_path, new_path);
		
		ret = sd_rename(old_path, new_path);
		if (ret == ESP_OK) {
			url_decode_newline(text_str);
			sd_write_str(new_path, text_str);
		}
	}

	FS_ACCESS_RELEASE();		//# FS RELEASE
	return httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
}

// fs_access
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

	//# FS_ACCESS: start here to allow other tasks to work while this handler get to this point
	// concurrent requests will be waiting here, they all have their own stack so their variables are safe
	if (!FS_ACCESS_START(req)) return ESP_OK;

	// no old_name => Create
	if (!old_name_len) {
		ESP_LOGW(TAG_HTTP, "create: %s", new_path);
		sd_ensure_dir(new_path);
	}
	// no new_name => Delete
	else if (!new_name_len) {
		ESP_LOGW(TAG_HTTP, "remove: %s", old_path);

		if (is_file) {
			sd_remove_file(old_path);
		} else {
			sd_remove_dir_recursive(old_path);
		}
	}
	// otherwise => Rename
	else if (new_name_len && old_name_len) {
		ESP_LOGW(TAG_HTTP, "rename: %s -> %s", old_path, new_path);
		ret = sd_rename(old_path, new_path);
	}

	FS_ACCESS_RELEASE();
	return httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
}

// fs_access
esp_err_t HTTP_GET_ENTRIES_HANDLER(httpd_req_t *req) {
	const char method_name[] = "HTTP_GET_ENTRIES_HANDLER";
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	httpd_resp_set_type(req, "application/json");
	ESP_LOGI(TAG_HTTP, "%s", method_name);

	char query[128];
	char entry_str[64] = {0};
	char txt_str[4] = {0};
	char bin_str[4] = {0};
	char output[512] = {0};

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

	ESP_LOGW(TAG_HTTP, "path %s", entry_str);
	int is_txt = atoi(txt_str);
	int is_bin = atoi(bin_str);
	int len = 0;

	if (!is_txt && !is_bin) {
		len = sd_entries_to_json(entry_str, output, sizeof(output));
		return httpd_resp_send(req, output, len);
	}

	//# FS_ACCESS: start here to allow other tasks to work while this handler get to this point
	// concurrent requests will be waiting here, they all have their own stack so their variables are safe
	if (!FS_ACCESS_START(req)) return ESP_OK;

	// text or binary
	httpd_resp_set_type(req, "text/plain");
	len = sd_read_tail(entry_str, output, sizeof(output));

	FS_ACCESS_RELEASE();	//# FS RELEASE
	return httpd_resp_send(req, output, len);
}

// fs_access -internal
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
	char buffer[1024];
	snprintf(full_path, sizeof(full_path), SD_POINT"/log/%s/%s/%s", pa_str, pb_str, pc_str);
	http_send_file_chunks(req, buffer, full_path);

	return httpd_resp_send_chunk(req, NULL, 0);
}


//###################################################
//# Diagnostic Helpers
//###################################################

#include "esp_partition.h"

int make_partition_tableStr(char *buffer) {
	char *ptr = buffer;  // Pointer to current position
	
	esp_partition_iterator_t it = esp_partition_find(
		ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
	
	while (it) {
		const esp_partition_t* part = esp_partition_get(it);
		int written = sprintf(ptr, "%s %dKB, ", part->label, (int)(part->size / 1024));
		ptr += written;  // Move pointer forward
		it = esp_partition_next(it);
	}
	
	esp_partition_iterator_release(it);
	*ptr++ = '\n';
	*ptr = '\0';  // Null-terminate
	return ptr - buffer; 
}

//! NOTE: Required: FreeRTOS Trace Facility
// idf.py menuconfig > Component config > FreeRTOS > Kernal
// check configUSE_TRACE_FACILITY and configUSE_STATS_FORMATTING_FUNCTIONS
#define MAX_PRINTING_TASKS 20  // Reasonable maximum

int make_detailed_sramStr(char *buffer) {
	// Get heap info for internal memory
	multi_heap_info_t heap_info;
	heap_caps_get_info(&heap_info, MALLOC_CAP_INTERNAL);

	// Calculate
	int free_sram_kb = heap_info.total_free_bytes / 1024;
	int used_sram_kb = heap_info.total_allocated_bytes / 1024;
	int total_sram_kb = free_sram_kb + used_sram_kb;
	int used_percent = (used_sram_kb * 100) / total_sram_kb;
	int free_percent = (free_sram_kb * 100) / total_sram_kb;

	char *ptr = buffer;  // Pointer to current position

	int written = sprintf(ptr, "SRAM %dK = %dK (%d%% Used) + %dK (%d%% Free)\n",
			total_sram_kb, used_sram_kb, used_percent, free_sram_kb, free_percent);
	ptr += written;
	written = sprintf(ptr, "Blocks: %d Used + %d Free\n",
			heap_info.allocated_blocks, heap_info.free_blocks);
	ptr += written;
	
	// largest_free_block is the size of the largest free block in the heap
	// minimum_free_bytes is the lifetime minimum free heap size
	written = sprintf(ptr, "Largest Free Block: %3dK\n", heap_info.largest_free_block / 1024);
	ptr += written;
	written = sprintf(ptr, "Lifetime min Free: %3dK\n", heap_info.minimum_free_bytes / 1024);
	ptr += written;
	*ptr = '\0';  // Null-terminate
	return ptr - buffer;
}

int make_tasks_watermarksStr(char *buffer) {
	static TaskStatus_t tasks[MAX_PRINTING_TASKS];  // No malloc/free!
	UBaseType_t task_count = uxTaskGetNumberOfTasks();
	if (task_count > MAX_PRINTING_TASKS) task_count = MAX_PRINTING_TASKS;
	task_count = uxTaskGetSystemState(tasks, task_count, NULL);
	char *ptr = buffer;  // Pointer to current position

	for (int i = 0; i < task_count; i++) {
		uint32_t min_free_bytes = tasks[i].usStackHighWaterMark * sizeof(StackType_t);
		int written = sprintf(ptr, "%s %lu\n", tasks[i].pcTaskName, min_free_bytes);
		ptr += written;
	}
	*ptr = '\0';  // Null-terminate
	return ptr - buffer;
}

int make_detailed_littlefsStr(char *buffer) {
	size_t total_kb = 0, used_kb = 0;
	esp_err_t ret = esp_littlefs_info(NULL, &total_kb, &used_kb);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG_SF, "littlefs_info failed: %s", esp_err_to_name(ret));
		return 0;
	}

	total_kb = total_kb / 1024;
	used_kb = used_kb / 1024;
	size_t free = total_kb - used_kb;

	char *ptr = buffer;  // Pointer to current position
	int written = sprintf(ptr, "LittleFS %dK = %dK (%d%% Used) + %dK (Free)\n",
								total_kb, used_kb, used_kb*100 / total_kb, free);
	ptr += written;
	*ptr = '\0';  // Null-terminate
	return ptr - buffer;
}

