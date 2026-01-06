#include <stdint.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>

#include <dirent.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "sd_test_io.h"

#include <time.h>
#include "esp_timer.h"

#include "../lib_sd_log/lib_sd_log.h"

// link: https://github.com/espressif/esp-idf/tree/master/examples/storage/sd_card


#define MAX_CHAR_SIZE	255

uint32_t hex_to_uint32_unrolled(const char *hex_str) {
    uint32_t result = 0;
    
    // Process each character individually (no loop overhead)
    result = ((hex_str[0] <= '9' ? hex_str[0] - '0' : (hex_str[0] & 0x0F) + 9) << 28) |
			((hex_str[1] <= '9' ? hex_str[1] - '0' : (hex_str[1] & 0x0F) + 9) << 24) |
			((hex_str[2] <= '9' ? hex_str[2] - '0' : (hex_str[2] & 0x0F) + 9) << 20) |
			((hex_str[3] <= '9' ? hex_str[3] - '0' : (hex_str[3] & 0x0F) + 9) << 16) |
			((hex_str[4] <= '9' ? hex_str[4] - '0' : (hex_str[4] & 0x0F) + 9) << 12) |
			((hex_str[5] <= '9' ? hex_str[5] - '0' : (hex_str[5] & 0x0F) + 9) << 8) |
			((hex_str[6] <= '9' ? hex_str[6] - '0' : (hex_str[6] & 0x0F) + 9) << 4) |
			((hex_str[7] <= '9' ? hex_str[7] - '0' : (hex_str[7] & 0x0F) + 9));
    
    return result;
}

static FILE *file;

//# Write File
esp_err_t sd_write(const char *path, char *buff) {
	ESP_LOGI(TAG_SD, "Opening file %s", path);

	FILE *f = fopen(path, "w");
	if (f == NULL) {
		ESP_LOGE(TAG_SD, "Failed to open file for writing");
		return ESP_FAIL;
	}
	fprintf(f, buff);
	fclose(f);
	ESP_LOGI(TAG_SD, "File written");

	return ESP_OK;
}

void sd_test(void) {	
	//# First create a file.
	const char *file_hello = SD_POINT"/hello.txt";
	char data[MAX_CHAR_SIZE];

	snprintf(data, MAX_CHAR_SIZE, "%s!\n", "Hello ... Hello ... ");
	esp_err_t ret = sd_write(file_hello, data);
	if (ret != ESP_OK) return;

	//# Check if destination file exists before renaming
	const char *file_foo = SD_POINT"/foo2.txt";
	struct stat st;
	if (stat(file_foo, &st) == 0) {
		// Delete it if it exists
		unlink(file_foo);
	}

	//# Rename original file
	ESP_LOGI(TAG_SD, "Renaming file %s to %s", file_hello, file_foo);
	if (rename(file_hello, file_foo) != 0) {
		ESP_LOGE(TAG_SD, "Rename failed");
		return;
	}

	char line[MAX_CHAR_SIZE];
	size_t len = sd_read_file(file_foo, line, sizeof(line));
	if (len < 1) return;

	const char *file_nihao = SD_POINT"/nihao.txt";
	memset(data, 0, MAX_CHAR_SIZE);
	snprintf(data, MAX_CHAR_SIZE, "%s!\n", "Nihao");
	ret = sd_write(file_nihao, data);
	if (ret != ESP_OK) return;

	//# Open file for reading
	len = sd_read_file(file_foo, line, sizeof(line));

	//	 // Deinitialize the power control driver if it was used
	// #if CONFIG_EXAMPLE_SD_PWR_CTRL_LDO_INTERNAL_IO
	//	 ret = sd_pwr_ctrl_del_on_chip_ldo(pwr_ctrl_handle);
	//	 if (ret != ESP_OK) {
	//		 ESP_LOGE(TAG_SD, "Failed to delete the on-chip LDO power control driver");
	//		 return;
	//	 }
	// #endif
}


//###################################################


typedef struct {
	uint32_t timestamp;
	int16_t value1;
	int16_t value2;
	int16_t value3;
} __attribute__((packed)) record_t;

typedef struct {
	uint32_t uuid;
	uint32_t last_log_rotation_sec;
	uint32_t last_minute_update_sec;
	uint32_t config;
	uint8_t count;			// aggregated count
	uint8_t rotation;
	int32_t sum1;
	int32_t sum2;
	int32_t sum3;
} record_aggregate_t;

typedef struct {
	uint32_t uuid;
	uint32_t timestamp;
} device_cache_t;


#define LOG_RECORD_COUNT 10
record_aggregate_t RECORD_AGGREGATE[LOG_RECORD_COUNT] = {0};
device_cache_t DEVICE_CACHE[LOG_RECORD_COUNT] = {0};

static void cache_device(uint32_t uuid, uint32_t time_ref) {
	for (int i = 0; i < LOG_RECORD_COUNT; i++) {
		device_cache_t *target = &DEVICE_CACHE[i];
		
		if (target->uuid == uuid) {
			// Found existing UUID
			target->timestamp = time_ref;
			break;  // Done!
		}
		
		//! Assumption: First empty slot, the rest are uuid = 0
		// UUID is new - add to empty slot and break loop
		if (target->uuid == 0) { 
			target->uuid = uuid;
			target->timestamp = time_ref;
			break;
		}
	}
}

// 1. /log/<uuid>/2025/latest_0.bin → Current 0-30 minutes (1Hz = 1800 points)
// 2. /log/<uuid>/2025/latest_1.bin → Current 30-60 minutes (1Hz = 1800 points)
// 3. /log/<uuid>/2025/1230.bin → Daily aggregate (1/min = 1440 points OR 60 per hour)
// 4. /log/<uuid>/2025/12.bin → Monthly aggregate (1/10min = 4320 records OR 144 per day)

// #define BUFFER_DURATION_SEC 1800  // 30 minutes
#define BUFFER_DURATION_SEC 120  // 30 minutes

static void rotate_timeLog_write(
	uint32_t uuid, uint32_t time_ref, struct tm *tm, record_t* record
) {
	char file_path[64];
	int year = tm->tm_year + 1900;
	int month = tm->tm_mon + 1;
	int day = tm->tm_mday;

	for (int i = 0; i < LOG_RECORD_COUNT; i++) {
		//! filter for valid uuid and config
		record_aggregate_t *target = &RECORD_AGGREGATE[i];
		if (target->uuid != uuid || target->config == 0) continue;

		//# 1. Create UUID directory: /log/<uuid>
		snprintf(file_path, sizeof(file_path), SD_POINT"/log/%08lX", uuid);
		if (!sd_ensure_dir(file_path)) {
			ESP_LOGE_SD(TAG_SD, "Err: create /<uuid>");
			continue;
		}

		//# 2. Create year directory: /log/<uuid>/2025
		snprintf(file_path, sizeof(file_path), SD_POINT"/log/%08lX/%d", uuid, year);
		if (!sd_ensure_dir(file_path)) {
			ESP_LOGE_SD(TAG_SD, "Err: create /<uuid>/year");
			continue;
		}

		//# 3. Create rotation file: /log/<uuid>/2025/new_#.bin
		snprintf(file_path, sizeof(file_path), SD_POINT"/log/%08lX/%d/new_%d.bin", 
				uuid, year, target->rotation);

        if (target->last_log_rotation_sec == 0 || 
			time_ref - target->last_log_rotation_sec >= BUFFER_DURATION_SEC
		) {
			target->rotation = (target->rotation == 0) ? 1 : 0;
			target->last_log_rotation_sec = time_ref;

			// Clear the new buffer file (start fresh)
			sd_overwrite_bin(file_path, record, sizeof(record_t));
		} else {
			// Append to current buffer
			sd_append_bin(file_path, record, sizeof(record_t));
		}

		// Update aggregation (ALWAYS)
		target->count++;
		target->sum1 += record->value1;
		target->sum2 += record->value2;
		target->sum3 += record->value3;

		//# 4. Create daily file: /log/<uuid>/2025/1230.bin, stop every 60 seconds
		if (target->last_minute_update_sec == 0 || 
			time_ref - target->last_minute_update_sec >= 60
		) {
			snprintf(file_path, sizeof(file_path), SD_POINT"/log/%08lX/%d/%02d%02d.bin", 
				uuid, year, month, day
			);

			record_t minute_avg = {
				.timestamp = record->timestamp,
				.value1 = target->sum1 / target->count,
				.value2 = target->sum2 / target->count,
				.value3 = target->sum3 / target->count
			};

			sd_append_bin(file_path, &minute_avg, sizeof(record_t));
			target->last_minute_update_sec = time_ref;

    		// RESET aggregates
			target->count = 0;
			target->sum1 = 0;
			target->sum2 = 0;
			target->sum3 = 0;
		}
	}
}

// STEP1: /log/<uuid>/2025/latest.bin - 1 hour of record every second (3600 records)
// STEP2: /log/<uuid>/2025/1230.bin - 24 hours records every minute (1440 records - 60 per hour)
// STEP3: /log/<uuid>/2025/12.bin - 30 days records every 10 minutes (4320 records - 144 per day)

static void sd_bin_record_all(uint32_t uuid, uint32_t time_ref, struct tm *tm, record_t* record) {
	//# STEP1: `/log/<uuid>/2025/latest.bin`
	char file_path[64];
	int year = tm->tm_year + 1900;
	int month = tm->tm_mon + 1;
	int day = tm->tm_mday;

	for (int i = 0; i < LOG_RECORD_COUNT; i++) {
		//! filter for valid uuid and config
		record_aggregate_t *target = &RECORD_AGGREGATE[i];
		if (target->uuid != uuid || target->config == 0) continue;

		//# /log/<uuid>
		snprintf(file_path, sizeof(file_path), SD_POINT"/log/%08lX", uuid);
		if (!sd_ensure_dir(file_path)) {
			ESP_LOGE_SD(TAG_SD, "Err: create /<uuid>");
			continue;
		}

		//# /log/<uuid>/2025
		snprintf(file_path, sizeof(file_path), SD_POINT"/log/%08lX/%d", uuid, year);
		if (!sd_ensure_dir(file_path)) {
			ESP_LOGE_SD(TAG_SD, "Err: create /<uuid>/year");
			continue;
		}

		uint32_t time_dif = time_ref - target->last_log_rotation_sec;
		snprintf(file_path, sizeof(file_path), SD_POINT"/log/%08lX/%d/latest.bin", uuid, year);

		//# replace the 1 second records for every hour (3600 records)
		if (target->last_log_rotation_sec == 0 || time_dif > 3600) {
			target->last_log_rotation_sec = time_ref;
			sd_overwrite_bin(file_path, record, sizeof(record_t));
		}
		else {
			sd_append_bin(file_path, record, sizeof(record_t));
		}

		target->count++;
		target->sum1 += record->value1;
		target->sum2 += record->value2;
		target->sum3 += record->value3;

		if (target->count >= 60) {
			record_t minute_avg = {
				.timestamp = record->timestamp,
				.value1 = target->sum1 / target->count,
				.value2 = target->sum2 / target->count,
				.value3 = target->sum3 / target->count
			};

			//# STEP2: `/log/<uuid>/2025/1230.bin`
			snprintf(file_path, sizeof(file_path), SD_POINT"/log/%08lX/%d/%02d%02d.bin", 
				uuid, year, month, day
			);
			sd_append_bin(file_path, record, sizeof(record_t));

			target->count = 0;
			target->sum1 = 0;
			target->sum2 = 0;
			target->sum3 = 0;
		}
	}
}

// Config: /log/config.bin
static esp_err_t sd_save_config(uint32_t uuid, uint32_t config) {
	// need 2 passes
	for (int i = 0; i < LOG_RECORD_COUNT; i++) {
		record_aggregate_t *target = &RECORD_AGGREGATE[i];
		//# overwrite existing uuid's config
		if (target->uuid == uuid) {
			target->config = config;
			break;
		}
		if (target->uuid == 0) {
			//! Assumption: First empty slot, the rest are uuid = 0
			// UUID is new - add to empty slot and break loop
			target->uuid = uuid;
			target->config = config;		// default config
			break;
		}
	}

	const char *file_path = SD_POINT"/log/config.txt";
	FILE *f = fopen(file_path, "w");	 // overwrite - create if doesn't exit
	if (!f) {
		ESP_LOGE_SD(TAG_SD, "Err write: %s", file_path);
		return ESP_FAIL;
	}

	for (int i = 0; i < LOG_RECORD_COUNT; i++) {
		record_aggregate_t *target = &RECORD_AGGREGATE[i];
		if (target->uuid == 0) continue;

		fprintf(f, "%08lX %ld\n", target->uuid, target->config);
		printf("saved: %08lX %ld\n", target->uuid, target->config);
	}

	fclose(f);
	ESP_LOGW(TAG_SD, "written: %s", file_path);
	return ESP_OK;
}

static esp_err_t sd_load_config() {
	const char *file_path = SD_POINT"/log/config.txt";
	FILE *f = fopen(file_path, "r");
	if (!f) {
		ESP_LOGE_SD(TAG_SD, "Err read: %s", file_path);
		return ESP_FAIL;
	}

	char line[64];
	int loaded = 0;

	while (fgets(line, sizeof(line), f)) {
		// Quick validation
		if (line[8] != ' ') continue;
		
		// Parse with strtoul
		char *endptr;
		uint32_t uuid = strtoul(line, &endptr, 16);		// base 16
		if (*endptr != ' ') continue;
		
		uint32_t config = strtoul(endptr + 1, &endptr, 10);		// base 10
		if (*endptr != '\n' && *endptr != '\0') continue;
		
		//! filter for valid uuid and config
		if (uuid == 0 || config == 0) continue;
		record_aggregate_t *target = &RECORD_AGGREGATE[loaded];
		target->uuid = uuid;
		target->config = config;
		loaded++;
		printf("Load Config uuid: %08lX, Config: %ld\n", uuid, config);
	}

	fclose(f);
	ESP_LOGI(TAG_SD, "Loaded %d configs", loaded);
	return ESP_OK;
}


//###################################################

static int make_device_configs_str(char *buffer, size_t buffer_size) {
	if (buffer == NULL || buffer_size < 3) return 0;
	char *ptr = buffer;
	int count = 0;
	*ptr++ = '[';
	
	for (int i = 0; i < LOG_RECORD_COUNT; i++) {
		record_aggregate_t *target = &RECORD_AGGREGATE[i];

		//! filter for valid uuid and config
		if (target->uuid == 0 || target->config == 0) continue;
		if (count > 0) *ptr++ = ',';
		
		int written = snprintf(ptr, buffer_size - (ptr - buffer),
							"[\"%08lX\",%ld]", target->uuid, target->config);
		if (written < 0 || written >= buffer_size - (ptr - buffer)) break; // Buffer full
		ptr += written;
		count++;
		
		// Safety check
		if (ptr - buffer >= buffer_size - 64) break;
	}
	
	ESP_LOGW(TAG_SD, "configs count: %d\n", count);
	*ptr++ = ']';
	*ptr = '\0';
	return ptr - buffer; // Return length
}

static int make_device_caches_str(char *buffer, size_t buffer_size) {
	if (buffer == NULL || buffer_size < 3) return 0;
	int count = 0;
	char *ptr = buffer;
	*ptr++ = '[';
	
	for (int i = 0; i < LOG_RECORD_COUNT; i++) {
        device_cache_t *target = &DEVICE_CACHE[i];
        if (target->uuid == 0) break;
		
        if (count > 0) *ptr++ = ',';
		int written = snprintf(ptr, buffer_size - (ptr - buffer),
							"[\"%08lX\",%ld]", target->uuid, target->timestamp);
		if (written < 0 || written >= buffer_size - (ptr - buffer)) break; // Buffer full
		ptr += written;
		count++;
		
		// Safety check
		if (ptr - buffer >= buffer_size - 64) break;
	}
	
	ESP_LOGW(TAG_SD, "caches count: %d\n", count);
	*ptr++ = ']';
	*ptr = '\0';
	return ptr - buffer; // Return length
}

// Function to read and verify binary data
size_t sd_bin_read(const char *uuid, const char *dateStr, record_t *buffer, size_t max_records) {
	char file_path[64];
	snprintf(file_path, sizeof(file_path), SD_POINT"/log/%s/%s.bin", uuid, dateStr);

	FILE *f = fopen(file_path, "rb");
	if (f == NULL) {
		ESP_LOGE(TAG_SD, "Failed to open file for reading: %s", file_path);
		return false;
	}

	// Get file size
	fseek(f, 0, SEEK_END);
	long file_size = ftell(f);
	fseek(f, 0, SEEK_SET);

	// Calculate number of records
	size_t record_count = file_size / sizeof(record_t);
	if (record_count > max_records) record_count = max_records;

	// Read records
	size_t records_read = fread(buffer, sizeof(record_t), record_count, f);
	fclose(f);

	if (records_read != record_count) {
		ESP_LOGW(TAG_SD, "Partial read: %d of %d records", records_read, record_count);
	}

	ESP_LOGI(TAG_SD, "Read %d records from %s", records_read, file_path);

	return records_read;
}
