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
#include "mod_record_file.h"

#define ROTATION_LOG_PATH_LEN 64

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
	ESP_LOGI(TAG_SF, "Opening file %s", path);

	FILE *f = fopen(path, "w");
	if (f == NULL) {
		ESP_LOGE(TAG_SF, "Failed to open file for writing");
		return ESP_FAIL;
	}
	fprintf(f, buff);
	fclose(f);
	ESP_LOGI(TAG_SF, "File written");

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
	ESP_LOGI(TAG_SF, "Renaming file %s to %s", file_hello, file_foo);
	if (rename(file_hello, file_foo) != 0) {
		ESP_LOGE(TAG_SF, "Rename failed");
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
	//		 ESP_LOGE(TAG_SF, "Failed to delete the on-chip LDO power control driver");
	//		 return;
	//	 }
	// #endif
}


//###################################################
#define LOG_NODE_COUNT 10
#define LOG_RECORD_COUNT 300		// 300 seconds of 10 bytes
#define LOG_WRITE_INTERVAL_SEC 300		// 5 minutes

typedef struct {
	uint32_t timestamp;
	int16_t value1;
	int16_t value2;
	int16_t value3;
} __attribute__((packed)) record_t;

typedef struct {
	uint32_t uuid;
	uint32_t last_5minute_update_sec;		// track when the last 5 minute update time
	uint32_t last_log_rotation_sec;
	uint32_t last_minute_update_sec;
	
	uint32_t config;
	uint8_t rotation;
	uint16_t current_idx;
	record_t records[LOG_RECORD_COUNT];
} record_aggregate_t;

typedef struct {
	uint32_t uuid;
	uint32_t timestamp;
} device_cache_t;

static record_aggregate_t RECORD_AGGREGATE[LOG_NODE_COUNT] = {0};
static device_cache_t DEVICE_CACHE[LOG_NODE_COUNT] = {0};

static void cache_device(uint32_t uuid, uint32_t time_ref) {
	for (int i = 0; i < LOG_NODE_COUNT; i++) {
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


static int cache_write_to_file(
	uint32_t uuid, struct tm *tm, record_t *records, int count
) {
	char file_path[ROTATION_LOG_PATH_LEN];
	int year = tm->tm_year + 1900;
	int month = tm->tm_mon + 1;
	int day = tm->tm_mday;

	//# Create UUID directory: /log/<uuid>
	snprintf(file_path, ROTATION_LOG_PATH_LEN, SD_POINT"/log/%08lX", uuid);
	if (!sd_ensure_dir(file_path)) {
		ESP_LOGE(TAG_SF, "Err cache_write_to_file create /<uuid>");
		return 0;
	}

	//# Create year directory: /log/<uuid>/2025
	snprintf(file_path, ROTATION_LOG_PATH_LEN, SD_POINT"/log/%08lX/%d", uuid, year);
	if (!sd_ensure_dir(file_path)) {
		ESP_LOGE(TAG_SF, "Err cache_write_to_file create /<uuid>/year");
		return 0;
	}

	//# Create complete path: /log/<uuid>/2025/MMDD-X.bin
	snprintf(file_path, ROTATION_LOG_PATH_LEN, SD_POINT"/log/%08lX/%d/%02d%02d.bin", 
		uuid, year, month, day
	);
}


static void cache_and_write_record(
	uint32_t uuid, struct tm *tm, record_t *record
) {
	for (int i = 0; i < LOG_NODE_COUNT; i++) {
		record_aggregate_t *target = &RECORD_AGGREGATE[i];
		if (target->uuid != uuid) continue;
		uint32_t timestamp = record->timestamp;
		uint16_t old_idx = target->current_idx;

		// Found existing UUID
		target->records[old_idx].timestamp = timestamp;
		target->records[old_idx].value1 = record->value1;
		target->records[old_idx].value2 = record->value2;
		target->records[old_idx].value3 = record->value3;
		target->current_idx = (old_idx + 1) % LOG_RECORD_COUNT;

		//# First time: reference for the 5 minute update time 
		if (target->last_5minute_update_sec == 0) {
			target->last_5minute_update_sec = timestamp;
			break;
		}

		//# Check if 5 minutes have passed
		if (timestamp - target->last_5minute_update_sec < LOG_WRITE_INTERVAL_SEC) {
			break;
		}

		// On 5 minute interval, aggregate the data into 5 equal parts and get 
		// the average of their values. If there are 300 records, this translates to
		// 60 records per minute OR aggregate of every 1 minute
		uint64_t sum_timestamp = 0;
		uint64_t sum_value1 = 0;
		uint64_t sum_value2 = 0;
		uint64_t sum_value3 = 0;

		int records_per_minute = LOG_RECORD_COUNT/5;
		int aggregate_count = 0;
		record_t rec_to_write[5] = {0};
		int rec_idx = 0;

		//# Phase 1: from old_idx to end
		for (int i = old_idx; i < LOG_RECORD_COUNT; i++) {
			uint32_t tstamp = target->records[i].timestamp;
			if (tstamp < 1) continue;

			// Add to sum
			sum_timestamp += tstamp;
			sum_value1 += target->records[i].value1;
			sum_value2 += target->records[i].value2;
			sum_value3 += target->records[i].value3;
			aggregate_count++;

			// Aggregate the data
			if (aggregate_count >= records_per_minute) {
				rec_to_write[rec_idx].timestamp = sum_timestamp/aggregate_count;
				rec_to_write[rec_idx].value1 = sum_value1/aggregate_count;
				rec_to_write[rec_idx].value2 = sum_value2/aggregate_count;
				rec_to_write[rec_idx].value3 = sum_value3/aggregate_count;
				rec_idx++;
				aggregate_count = 0;

				// Reset for next aggregate
				sum_timestamp = 0;
				sum_value1 = 0;
				sum_value2 = 0;
				sum_value3 = 0;
			}
		}

		//# Phase 2: from 0 to old_idx
		for (int i = 0; i < old_idx; i++) {
			uint32_t tstamp = target->records[i].timestamp;
			if (tstamp < 1) continue;

			// Add to sum
			sum_timestamp += tstamp;
			sum_value1 += target->records[i].value1;
			sum_value2 += target->records[i].value2;
			sum_value3 += target->records[i].value3;
			aggregate_count++;

			// Aggregate the data
			if (aggregate_count >= records_per_minute) {
				rec_to_write[rec_idx].timestamp = sum_timestamp/aggregate_count;
				rec_to_write[rec_idx].value1 = sum_value1/aggregate_count;
				rec_to_write[rec_idx].value2 = sum_value2/aggregate_count;
				rec_to_write[rec_idx].value3 = sum_value3/aggregate_count;
				rec_idx++;
				aggregate_count = 0;

				// Reset for next aggregate
				sum_timestamp = 0;
				sum_value1 = 0;
				sum_value2 = 0;
				sum_value3 = 0;
			}
		}

		//# Handle any leftover partial aggregate
		if (aggregate_count > 0 && rec_idx < 5) {
			rec_to_write[rec_idx].timestamp = sum_timestamp / aggregate_count;
			rec_to_write[rec_idx].value1 = sum_value1 / aggregate_count;
			rec_to_write[rec_idx].value2 = sum_value2 / aggregate_count;
			rec_to_write[rec_idx].value3 = sum_value3 / aggregate_count;
		}

		//# Write to file
		cache_write_to_file(uuid, tm, rec_to_write, rec_idx);

		//# Track the last 5 minute update time
		target->last_5minute_update_sec = timestamp;

		break;  // Done
	}
}



// /log/<uuid>/new_0.bin - 1 second records with 30 minutes rotation A (1Hz = 1800 points)
// /log/<uuid>/new_1.bin - 1 second records with 30 minutes rotation B (1Hz = 1800 points)
// /log/<uuid>/2025/1230.bin - 1 minute records of 24 hours (1/min = 1440 points OR 60 per hour)
// /log/<uuid>/2025/12.bin - 10 minutes records of 30 days(1/10min = 4320 records OR 144 per day)

#define BUFFER_DURATION_SEC 1800  // 30 minutes


#define FILE_SIZE (10*1800)		// 1800 records 10 bytes each

static void rotation_get_filePath(uint32_t device_id, int target, char *file_path) {
	snprintf(file_path, ROTATION_LOG_PATH_LEN, SD_POINT"/log/%08lX/new_%d.bin", 
			device_id, target);
}

void append_to_circular_buffer(const char* file_path, const void* data, size_t size) {
	static size_t write_pos = 0;
	
	FILE* f = fopen(file_path, "rb+");
	if (!f) {
		// Create fixed-size file on first run
		printf("*** Creating file %s\n", file_path);
		f = fopen(file_path, "wb");
		if (f) {
			// Pre-allocate file size
			uint8_t zero_buffer[512] = {0};
			size_t remaining = FILE_SIZE;
			
			// fill with zeros
			while (remaining > 0) {
				size_t to_write = (remaining > sizeof(zero_buffer)) ? 
								sizeof(zero_buffer) : remaining;
				fwrite(zero_buffer, 1, to_write, f);
				remaining -= to_write;
			}
			fclose(f);
			f = fopen(file_path, "rb+");
		}
	}
	
	if (f) {
		// printf("*** writing file %s at pos %d\n", file_path, write_pos);
		fseek(f, write_pos, SEEK_SET);
		fwrite(data, 1, size, f);
		write_pos = (write_pos + size) % FILE_SIZE;
		fclose(f);
	}
}

// static void rotationLog_write(
// 	uint32_t uuid, uint32_t time_ref, struct tm *tm, record_t* record
// ) {
// 	char file_path[ROTATION_LOG_PATH_LEN];
// 	int year = tm->tm_year + 1900;
// 	int month = tm->tm_mon + 1;
// 	int day = tm->tm_mday;

// 	for (int i = 0; i < LOG_NODE_COUNT; i++) {
// 		//! filter for valid uuid and config
// 		record_aggregate_t *target = &RECORD_AGGREGATE[i];
// 		if (target->uuid != uuid || target->config == 0) continue;

// 		//# 1. Create UUID directory: /log/<uuid>
// 		snprintf(file_path, ROTATION_LOG_PATH_LEN, SD_POINT"/log/%08lX", uuid);
// 		if (!sd_ensure_dir(file_path)) {
// 			ESP_LOGE(TAG_SF, "Err rotationLog_write create /<uuid>");
// 			continue;
// 		}

// 		uint64_t run_time;
// 		runtime_start(&run_time);

// 		rotation_get_filePath(uuid, 1, file_path);
// 		append_to_circular_buffer(file_path, record, sizeof(record_t));
	
// 		//# 2. Check rotation for latest_<0|1>.bin files - takes about 15ms
// 		if (target->last_log_rotation_sec == 0 || 
// 			time_ref - target->last_log_rotation_sec >= BUFFER_DURATION_SEC
// 		) {
// 			printf("*** rotationLog_write rotate\n");
// 			// Time to rotate - switch and write to the OTHER file
// 			int new_rotation = (target->rotation == 0) ? 1 : 0;
// 			rotation_get_filePath(uuid, new_rotation, file_path);
// 			sd_overwrite_bin(file_path, record, sizeof(record_t));
			
// 			// Update rotation index to point to the new_rotation
// 			target->rotation = new_rotation;
// 			target->last_log_rotation_sec = time_ref;
// 		}
// 		else {
// 			// Append to the current active file
// 			rotation_get_filePath(uuid, target->rotation, file_path);
// 			sd_append_bin(file_path, record, sizeof(record_t));
// 		}

// 		runtime_print("*** rotationLog_write", &run_time);

// 		// Update aggregation (ALWAYS)
// 		target->count++;
// 		target->sum1 += record->value1;
// 		target->sum2 += record->value2;
// 		target->sum3 += record->value3;

// 		//# 3. Create daily file: /log/<uuid>/2025/1230.bin, stop every 60 seconds
// 		if (target->last_minute_update_sec == 0 || 
// 			time_ref - target->last_minute_update_sec >= 60
// 		) {
// 			//#Create year directory: /log/<uuid>/2025
// 			snprintf(file_path, ROTATION_LOG_PATH_LEN, SD_POINT"/log/%08lX/%d", uuid, year);

// 			if (!sd_ensure_dir(file_path)) {
// 				ESP_LOGE(TAG_SF, "Err rotationLog_write create /<uuid>/year");
// 				continue;
// 			}

// 			snprintf(file_path, ROTATION_LOG_PATH_LEN, SD_POINT"/log/%08lX/%d/%02d%02d.bin", 
// 				uuid, year, month, day
// 			);

// 			record_t minute_avg = {
// 				.timestamp = record->timestamp,
// 				.value1 = target->sum1 / target->count,
// 				.value2 = target->sum2 / target->count,
// 				.value3 = target->sum3 / target->count
// 			};

// 			sd_append_bin(file_path, &minute_avg, sizeof(record_t));
// 			target->last_minute_update_sec = time_ref;

//     		// RESET aggregates
// 			target->count = 0;
// 			target->sum1 = 0;
// 			target->sum2 = 0;
// 			target->sum3 = 0;
// 		}
// 	}
// }


// Config: /log/config.bin
static esp_err_t sd_save_config(uint32_t uuid, uint32_t config) {
	// need 2 passes
	for (int i = 0; i < LOG_NODE_COUNT; i++) {
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
		ESP_LOGE(TAG_SF, "Err sd_save_config %s", file_path);
		return ESP_FAIL;
	}

	for (int i = 0; i < LOG_NODE_COUNT; i++) {
		record_aggregate_t *target = &RECORD_AGGREGATE[i];
		if (target->uuid == 0) continue;

		fprintf(f, "%08lX %ld\n", target->uuid, target->config);
		printf("saved: %08lX %ld\n", target->uuid, target->config);
	}

	fclose(f);
	ESP_LOGW(TAG_SF, "written %s", file_path);
	return ESP_OK;
}

static esp_err_t sd_load_config() {
	const char *file_path = SD_POINT"/log/config.txt";
	FILE *f = fopen(file_path, "r");
	if (!f) {
		ESP_LOGE(TAG_SF, "Err sd_load_config %s", file_path);
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
		memset(target->records, 0, sizeof(record_t) * LOG_RECORD_COUNT);
		loaded++;
		printf("Load Config uuid: %08lX, Config: %ld\n", uuid, config);
	}

	fclose(f);
	ESP_LOGI(TAG_SF, "Loaded %d configs", loaded);
	return ESP_OK;
}


//###################################################

static int make_device_configs_str(char *buffer, size_t buffer_size) {
	if (buffer == NULL || buffer_size < 3) return 0;
	char *ptr = buffer;
	int count = 0;
	*ptr++ = '[';
	
	for (int i = 0; i < LOG_NODE_COUNT; i++) {
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

	*ptr++ = ']';
	*ptr = '\0';

	ESP_LOGW(TAG_SF, "make_device_configs_str count %d", count);
	return ptr - buffer; // Return length
}

static int make_device_caches_str(char *buffer, size_t buffer_size) {
	if (buffer == NULL || buffer_size < 3) return 0;
	int count = 0;
	char *ptr = buffer;
	*ptr++ = '[';
	
	for (int i = 0; i < LOG_NODE_COUNT; i++) {
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
	
	*ptr++ = ']';
	*ptr = '\0';

	ESP_LOGW(TAG_SF, "make_device_caches_str count %d\n", count);
	return ptr - buffer; // Return length
}

// Function to read and verify binary data
size_t sd_bin_read(const char *uuid, const char *dateStr, record_t *buffer, size_t max_records) {
	char file_path[64];
	snprintf(file_path, sizeof(file_path), SD_POINT"/log/%s/%s.bin", uuid, dateStr);

	FILE *f = fopen(file_path, "rb");
	if (f == NULL) {
		ESP_LOGE(TAG_SF, "Failed to open file for reading: %s", file_path);
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
		ESP_LOGW(TAG_SF, "Partial read: %d of %d records", records_read, record_count);
	}

	ESP_LOGI(TAG_SF, "Read %d records from %s", records_read, file_path);

	return records_read;
}

//#######################################



//! NOTE
// Pre-allocate cluster chain to avoid FAT updates
void create_preallocated_file(const char* filename) {
	// Create file with 1 cluster (4KB) allocated
	FILE* f = fopen(filename, "wb");
	
	// Write dummy data to allocate cluster
	uint8_t dummy[4096] = {0};
	fwrite(dummy, 1, sizeof(dummy), f);
	fclose(f);
	
	// Now FAT entry exists, future writes don't update FAT!
}

void append_to_preallocated(const char* filename, const void* data, size_t size) {
	// Open for update (cluster already allocated)
	FILE* f = fopen(filename, "rb+");
	
	// Overwrite existing data (same cluster)
	fseek(f, 0, SEEK_SET);
	fwrite(data, 1, size, f);
	
	// Only writes data block, no FAT updates!
	fclose(f);
}