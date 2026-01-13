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

#define FILE_PATH_LEN 64

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
#define ACTIVE_RECORDS_COUNT 10
#define RECORD_BUFFER_LEN 300					// 300 seconds of 12 bytes
// #define AGGREGATE_INTERVAL_SEC 300			// 5 minutes
#define AGGREGATE_INTERVAL_SEC 8				// 5 minutes
#define AGGREGATE_SAMPLE_COUNT 5
#define AGGREGATE_MAX_FILE_COUNT 20
#define AGGREGATE_CACHE_COUNT 5

typedef struct {
	uint32_t timestamp;
	int16_t value1;
	int16_t value2;
	int16_t value3;
	int16_t value4;
} record_t;

typedef struct {
	uint32_t uuid;
	uint32_t config;
	uint32_t last_aggregate_sec;	// track when the last aggregate happened
	uint32_t last_timestamp;		// last timestamp recorded
	file_header_t header;
	
	uint8_t file_index;				// the file index for aggregated data
	uint8_t curr_year;
	uint8_t curr_month;
	uint8_t curr_day;

	uint16_t last_aggregate_count;
	uint16_t record_idx;
	record_t records[RECORD_BUFFER_LEN];
} active_records_t;

typedef struct {
	uint32_t uuid;
	uint32_t timestamp;
} device_cache_t;


typedef struct {
	uint32_t uuid;
	uint32_t start_timestamp;
	uint32_t end_timestamp;
	uint32_t circular_index;
	record_t records[AGGREGATE_SAMPLE_COUNT];
} aggregate_cache_t;

static device_cache_t DEVICE_CACHE[ACTIVE_RECORDS_COUNT] = {0};
static active_records_t ACTIVE_RECORDS[ACTIVE_RECORDS_COUNT] = {0};
static uint32_t ACTIVE_UUIDS[ACTIVE_RECORDS_COUNT] = {0};
static aggregate_cache_t AGGREGATE_CACHE[AGGREGATE_CACHE_COUNT] = {0};
static uint32_t AGGREGATE_CACHE_UUIDS[AGGREGATE_CACHE_COUNT] = {0};

static int find_uuid_index(uint32_t uuid) {
	for (int i = 0; i < ACTIVE_RECORDS_COUNT; i++) {
		if (ACTIVE_UUIDS[i] == uuid) return i;
	}
	return -1;
}

static active_records_t *find_records_store(uint32_t uuid) {
    for (int i = 0; i < ACTIVE_RECORDS_COUNT; i++) {
        if (ACTIVE_UUIDS[i] == uuid) return &ACTIVE_RECORDS[i];
    }
    return NULL;
}

static aggregate_cache_t *find_aggregate_cache(uint32_t uuid) {
	for (int i = 0; i < AGGREGATE_CACHE_COUNT; i++) {
		if (AGGREGATE_CACHE_UUIDS[i] == uuid) return &AGGREGATE_CACHE[i];
	}
	return NULL;
}

static void cache_device(uint32_t uuid, uint32_t time_ref) {
	for (int i = 0; i < ACTIVE_RECORDS_COUNT; i++) {
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

static void test_print_last2_records(record_t *records) {
	for (int i = 0; i < 2; i++) {
		printf("[%d] timestamp: %ld, value1: %d\n",
			i, records[i].timestamp, records[i].value1);
	}
}

static void aggregate_records(
	active_records_t *target, record_t *rec_to_write, int sample_count
) {
	// empty records
	memset(rec_to_write, 0, sizeof(rec_to_write[0]) * sample_count);

	// ESP_LOGE(TAG_SF, "Saving Records");
	// for (int i = 0; i < target->last_aggregate_count; i++) {
	// 	record_t *rec = &target->records[i];
	// 	printf("[%d] %ld, %d\n", i, rec->timestamp, rec->value1);
	// }

	// aggregate the records from the current record_idx backward by the aggregated count
	// start_idx + aggregate_count = current record_idx
	const int total = target->last_aggregate_count;
	const int records_per_sample = total / sample_count;
	const int extra = total % sample_count;

	// start index wrap-around
	int start_idx = target->record_idx - total;
	if (start_idx < 0) start_idx += RECORD_BUFFER_LEN;

	uint64_t sum_timestamp = 0, sum_value1 = 0, sum_value2 = 0, sum_value3 = 0, sum_value4 = 0;
	int count = 0, sample_idx = 0;

	// Distribute extra samples: If there are 8 records over 5 sample_count
	// distribute the extra 3 records accross the first 3 samples
	int target_sample_size = records_per_sample;
	if (sample_idx < extra) target_sample_size++;

	for (int i = 0; i < total; i++) {
		int idx = start_idx + i;
		// handle wrap-around
		if (idx >= RECORD_BUFFER_LEN) idx -= RECORD_BUFFER_LEN;
		
		sum_timestamp += target->records[idx].timestamp;
		sum_value1 += target->records[idx].value1;
		sum_value2 += target->records[idx].value2;
		sum_value3 += target->records[idx].value3;
		count++;
		
		if (count >= target_sample_size) {
			rec_to_write[sample_idx].timestamp = sum_timestamp / count;
			rec_to_write[sample_idx].value1 = sum_value1 / count;
			rec_to_write[sample_idx].value2 = sum_value2 / count;
			rec_to_write[sample_idx].value3 = sum_value3 / count;
			sample_idx++;

			// Update for next sample
			sum_timestamp = sum_value1 = sum_value2 = sum_value3 = count = 0;

			if (sample_idx < sample_count) {
				target_sample_size = records_per_sample;
				// Distribute extra samples accross the first samples
				if (sample_idx < extra) target_sample_size++;
			}
		}
	}
}


static void make_aggregate_filePath(
	char *file_path, uint32_t uuid, int year, int month, int day, int file_idx
) {
	snprintf(file_path, FILE_PATH_LEN, SD_POINT"/log/%08lX/%02d/%02d%02d-%d.bin", 
			uuid, year, month, day, file_idx);
}


static int prepare_aggregate_file(
	char *file_path, active_records_t *target,uint32_t uuid, int year, int month, int day
) {
	static const char method_name[] = "prepare_aggregate_file";

	if (target->curr_year != year) {
		ESP_LOGW(TAG_SF, "%s VALIDATE-PATH: for year %d", method_name, year);

		//# Get or Create UUID directory: /log/<uuid>
		snprintf(file_path, FILE_PATH_LEN, SD_POINT"/log/%08lX", uuid);
		if (!sd_ensure_dir(file_path)) {
			ESP_LOGE(TAG_SF, "%s CREATE-PATH %s", method_name, file_path);
			return 0;
		}

		//# Get or Create year directory: /log/<uuid>/YY
		snprintf(file_path, FILE_PATH_LEN, SD_POINT"/log/%08lX/%02d", uuid, year);
		if (!sd_ensure_dir(file_path)) {
			ESP_LOGE(TAG_SF, "%s CREATE-PATH %s", method_name, file_path);
			return 0;
		}

		// update current year
		target->curr_year = year;
		// force month and day update
		target->curr_month = 0;
		target->curr_day = 0;
	}

	//# Create month and day path - Note: day and month can change between runs
	if (target->curr_month != month && target->curr_day != day) {
		ESP_LOGW(TAG_SF, "%s UPDATE-PATH: for %d/%d/%d", method_name, year, month, day);
		int target_file_idx = 0;
		uint64_t time_ref;

		for (int i = 0; i < AGGREGATE_MAX_FILE_COUNT; i++) {
			struct stat st;
			// find the first available file
			make_aggregate_filePath(file_path, uuid, year, month, day, target_file_idx);

			// stat ~6.5ms
			if (stat(file_path, &st) != 0) break;	// File does not exist
			target_file_idx = i;
		}

		if (target_file_idx >= AGGREGATE_MAX_FILE_COUNT) {
			ESP_LOGE(TAG_SF, "%s MAX-FILES reached: %d", method_name, target_file_idx);
			return 0;
		}

		// check if file_index has changed since last run
		if (target_file_idx < target->file_index) {
			target_file_idx = target->file_index;
		}

		// update target
		target->curr_month = month;
		target->curr_day = day;
		target->file_index = target_file_idx;
	}

	//# build the complete path: /log/<uuid>/YY/MMDD-X.bin
	// <uuid>/YY/MMDD-n.bin (max 6 hours - 360 records OR minutes)
	// <uuid>/YY/MMDD-n+i.bin (Max 4 files per day - total 24 hours)
	// max 1 minute a record: 10rec, 30rec, 60rec, 3hr=180rec, 6hr=360rec
	make_aggregate_filePath(file_path, uuid, year, month, day, target->file_index);
	ESP_LOGW(TAG_SF, "%s PREPARING-FILE", method_name);
	printf("- Start file: %s\n", file_path);

	return 1;
}

#define BUFFER_SIZE sizeof(record_t) * AGGREGATE_SAMPLE_COUNT
static uint8_t read_buffer[BUFFER_SIZE];
static uint8_t test_data[BUFFER_SIZE];

static nvs_handle_t my_handle;
static record_t recs_to_read[AGGREGATE_SAMPLE_COUNT];

static void reload_aggregate_specs(
	active_records_t *active, record_t *record, uint32_t timestamp
) {
	if (record == NULL) {
		// reset last aggregate values
		active->last_aggregate_count = 0;
		active->last_aggregate_sec = timestamp;
	}
	else {
		// update records values
		const int idx = active->record_idx;
		active->records[idx].timestamp = timestamp;
		active->records[idx].value1 = record->value1;
		active->records[idx].value2 = record->value2;
		active->records[idx].value3 = record->value3;

		active->last_aggregate_count++;
		active->last_timestamp = timestamp;
		active->record_idx = (active->record_idx + 1) % RECORD_BUFFER_LEN;
	}
}

static void cache_n_write_record(
	uint32_t uuid, record_t *record, int year, int month, int day
) {
	char method_name[] = "cache_n_write_record";
	active_records_t *active = find_records_store(uuid);
	if (!active) return;

	char file_path[FILE_PATH_LEN];
	uint16_t idx = active->record_idx;
	uint32_t timestamp = record->timestamp;
	record_t recs_to_write[AGGREGATE_SAMPLE_COUNT];
	uint64_t time_ref;

	//# Found existing UUID - update record
	reload_aggregate_specs(active, record, timestamp);

	//# First time: reference for the 5 minute update time 
	if (active->last_aggregate_sec == 0) {
		active->last_aggregate_sec = timestamp;
		return;
	}
	//# Check if 5 minutes have passed
	else if (timestamp - active->last_aggregate_sec < AGGREGATE_INTERVAL_SEC) {
		return;
	}

	//# Prepare file
	int check = prepare_aggregate_file(file_path, active, uuid, year, month, day);
	if (!check) return;

	//# Aggregate records
	elapse_start(&time_ref);
	aggregate_records(active, recs_to_write, AGGREGATE_SAMPLE_COUNT);		// ~200us for 5 samples
	elapse_print("\n*** AGGREGATE", &time_ref);

	test_print_last2_records(recs_to_write);
	
	ESP_LOGW(TAG_SF, "%s LOG-AGGREGATE", method_name);
	printf("- Writting %d records to: %s\n", AGGREGATE_SAMPLE_COUNT, file_path);

	//# Write to the file
	file_header_t header;

	//######################################
	printf("\n");
	ESP_LOGE(TAG_SF, "=== SD TEST === ");

	// ~20ms
	elapse_start(&time_ref);
	int next_offset = record_batch_insert(file_path, &active->header, recs_to_write,
										sizeof(record_t), AGGREGATE_SAMPLE_COUNT);
	elapse_print("\n*** WRITE1", &time_ref);
	if (!next_offset) {
		// rerun to prepare the file again for the next cycle
		active->curr_month = 0;
		active->curr_day = 0;
		active->file_index = 0;
		return;
	}
	else if (next_offset > RECORD_FILE_BLOCK_SIZE) {
		// increase the file_index an reset dates to make a new record_file next time
		active->curr_month = 0;
		active->curr_day = 0;
		active->file_index++;
	}

	// ~10ms
	elapse_start(&time_ref);
	int len = record_file_read(&header, file_path, recs_to_read, 
								sizeof(record_t), AGGREGATE_SAMPLE_COUNT);	// ~10ms
	elapse_print("\n*** READ1", &time_ref);

	//######################################

	// printf("\n");
	// ESP_LOGI(TAG_SF, "=== LITTLEFS TEST === ");
	// memset(recs_to_read, 0, sizeof(recs_to_read));

	// // ~18ms
	// elapse_start(&time_ref);
	// FILE* f = fopen("/littlefs/test.bin", "wb");
	// fwrite(recs_to_write, sizeof(record_t), AGGREGATE_SAMPLE_COUNT, f);
	// fclose(f);
	// elapse_print("*** WRITE2", &time_ref);

	// // ~7ms
	// elapse_start(&time_ref);
	// f = fopen("/littlefs/test.bin", "rb");
	// fread(recs_to_read, sizeof(record_t), AGGREGATE_SAMPLE_COUNT, f);
	// fclose(f);
	// elapse_print("*** READ2", &time_ref);

	// for (int i = 0; i < 2; i++) {
	// 	printf("[%d] timestamp: %ld, value1: %d\n",
	// 		i, recs_to_read[i].timestamp, recs_to_read[i].value1);
	// }

	//######################################
	// printf("\n");
	// ESP_LOGI(TAG_SF, "=== NVS TEST ===");
	// memset(recs_to_read, 0, sizeof(recs_to_read));
	// 	// memcpy(test_data, recs_to_write, sizeof(recs_to_write));

	// // ~20ms
	// elapse_start(&time_ref);
	// nvs_open("storage1", NVS_READWRITE, &my_handle);		// ~250us
	// nvs_set_blob(my_handle, "rec", recs_to_write, sizeof(recs_to_write));
	// // nvs_set_blob(my_handle, "records", test_data, sizeof(test_data));
	// nvs_commit(my_handle);
	// nvs_close(my_handle);
	// elapse_print("*** WRITE3", &time_ref);

	// // ~1.5ms for 1 record. ~2.7ms to read 3 records.
	// size_t required_size = sizeof(record_t) * AGGREGATE_SAMPLE_COUNT;
	// elapse_start(&time_ref);
	// nvs_open("storage1", NVS_READONLY, &my_handle);
	// nvs_get_blob(my_handle, "rec", recs_to_read, &required_size);
	// nvs_close(my_handle);
	// elapse_print("*** READ3", &time_ref);

	// for (int i = 0; i < 2; i++) {
	// 	printf("[%d] timestamp: %ld, value1: %d\n",
	// 		i, recs_to_read[i].timestamp, recs_to_read[i].value1);
	// }


	//######################################

	// printf("*** total aggregate count: %d\n", active->last_aggregate_count);
	// for (int i = 0; i < AGGREGATE_SAMPLE_COUNT; i++) {
	// 	printf("[%d] timestamp: %ld, value1: %d\n", i, rec_to_write[i].timestamp, rec_to_write[i].value1);
	// }

	// static record_t read_back[400] = {0};
	// file_header_t header;
	// int read_count = record_file_read(&header, file_path, read_back, sizeof(record_t), 400);

	// printf("*** %s Read %d records\n", method_name, read_count);
	// for (int i = 0; i < read_count; i++) {
	// 	ESP_LOGE(TAG_SF, "Read: %ld %d %d",
	// 		read_back[i].timestamp, read_back[i].value1, read_back[i].value2);
	// }

	//# Track the last 5 minute update time
	reload_aggregate_specs(active, NULL, timestamp);
}


// /log/<uuid>/new_0.bin - 1 second records with 30 minutes rotation A (1Hz = 1800 points)
// /log/<uuid>/new_1.bin - 1 second records with 30 minutes rotation B (1Hz = 1800 points)
// /log/<uuid>/2025/1230.bin - 1 minute records of 24 hours (1/min = 1440 points OR 60 per hour)
// /log/<uuid>/2025/12.bin - 10 minutes records of 30 days(1/10min = 4320 records OR 144 per day)

#define BUFFER_DURATION_SEC 1800  // 30 minutes
#define FILE_SIZE (10*1800)		// 1800 records 10 bytes each

static void rotation_get_filePath(uint32_t device_id, int target, char *file_path) {
	snprintf(file_path, FILE_PATH_LEN, SD_POINT"/log/%08lX/new_%d.bin", 
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


// Config: /log/config.bin
static esp_err_t sd_save_config(uint32_t uuid, uint32_t config) {
	const char method_name[] = "sd_save_config";

	// need 2 passes
	for (int i = 0; i < ACTIVE_RECORDS_COUNT; i++) {
		active_records_t *target = &ACTIVE_RECORDS[i];

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

			// update UUID_ARRAY
			ACTIVE_UUIDS[i] = uuid;
			break;
		}
	}

	const char *file_path = SD_POINT"/log/config.txt";
	FILE *f = fopen(file_path, "w");	 // overwrite - create if doesn't exit

	if (!f) {
		ESP_LOGE(TAG_SF, "Err %s %s", method_name, file_path);
		return ESP_FAIL;
	}

	for (int i = 0; i < ACTIVE_RECORDS_COUNT; i++) {
		active_records_t *target = &ACTIVE_RECORDS[i];
		if (target->uuid == 0) continue;
		fprintf(f, "%08lX %ld\n", target->uuid, target->config);
	}

	fclose(f);
	ESP_LOGW(TAG_SF, "%s CONFIG-SAVE", method_name);	
	printf("Saved at: %s uuid %08lX config %ld\n", file_path, uuid, config);
	return ESP_OK;
}

static esp_err_t sd_load_config() {
	const char method_name[] = "sd_load_config";
	const char *file_path = SD_POINT"/log/config.txt";
	FILE *f = fopen(file_path, "r");
	if (!f) {
		ESP_LOGE(TAG_SF, "Err sd_load_config %s", file_path);
		return ESP_FAIL;
	}

	char line[64];
	int target_idx = 0;

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

		// update UUID_ARRAY
		ACTIVE_UUIDS[target_idx] = uuid;

		// update RECORDS_STORE
		active_records_t *target = &ACTIVE_RECORDS[target_idx];
		target->uuid = uuid;
		target->config = config;
		memset(target->records, 0, sizeof(target->records));
		target_idx++;
		printf("Load Config uuid: %08lX, Config: %ld\n", uuid, config);
	}

	fclose(f);
	ESP_LOGI(TAG_SF, "%s CONFIG-LOADED: %d configs", method_name, target_idx);
	return ESP_OK;
}


//###################################################

static int make_device_configs_str(char *buffer, size_t buffer_size) {
	const char method_name[] = "make_device_configs_str";

	if (buffer == NULL || buffer_size < 3) return 0;
	char *ptr = buffer;
	int count = 0;
	*ptr++ = '[';
	
	for (int i = 0; i < ACTIVE_RECORDS_COUNT; i++) {
		active_records_t *target = &ACTIVE_RECORDS[i];

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

	ESP_LOGW(TAG_SF, "%s CONFIG-FOUND: %d", method_name, count);
	return ptr - buffer; // Return length
}

static int make_device_caches_str(char *buffer, size_t buffer_size) {
	const char method_name[] = "make_device_caches_str";

	if (buffer == NULL || buffer_size < 3) return 0;
	int count = 0;
	char *ptr = buffer;
	*ptr++ = '[';
	
	for (int i = 0; i < ACTIVE_RECORDS_COUNT; i++) {
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

	ESP_LOGW(TAG_SF, "%s CACHE-FOUND: %d", method_name, count);
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