#include <stdint.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>

#include <dirent.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "sd_test_io.h"

#include <time.h>

// link: https://github.com/espressif/esp-idf/tree/master/examples/storage/sd_card

#if SOC_SDMMC_IO_POWER_EXTERNAL
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#endif

#define MAX_CHAR_SIZE	255
#define MOUNT_POINT "/sdcard"
#define LOG_FILE_UPDATE_INTERVAL 1800			// 1800 seconds OR 30 minutes

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

static const char *TAG_SD = "[SD]";

static FILE *file;

//# Open File
esp_err_t sd_fopen(const char *path) {
	ESP_LOGI(TAG_SD, "Open file %s", path);

	file = fopen(path, "r");
	if (file == NULL) {
		ESP_LOGE(TAG_SD, "Failed to open file for reading");
		return ESP_FAIL;
	}

	return ESP_OK;
}

//# Read File
size_t sd_fread(char *buff, size_t len) {
	// Reads a specified number of bytes (or records) from a file.
	return fread(buff, 1, len, file);
}

int sd_fclose() {
	if (file == NULL) return 0;
	ESP_LOGI(TAG_SD, "sd_fclose");
	return fclose(file);
}

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

esp_err_t sd_get(const char *path, char *buff, size_t len) {
	ESP_LOGI(TAG_SD, "Reading file %s", path);
	file = fopen(path, "r");
	if (file == NULL) {
		ESP_LOGE(TAG_SD, "Failed to open file for reading");
		return ESP_FAIL;
	}

	// Reads a line of text from a file (up to a specified number of characters 
	// or until a newline is encountered).
	fgets(buff, len, file);
	fclose(file);

	// strip newline
	char *pos = strchr(buff, '\n');
	if (pos) *pos = '\0';
	ESP_LOGI(TAG_SD, "Read from file: '%s'", buff);

	return ESP_OK;
}

static sdmmc_card_t *card;
static esp_err_t ret;

static esp_err_t check_sd_card(esp_err_t ret) {
	if (ret == ESP_OK) {
		//# Card has been initialized, print its properties
		ESP_LOGI(TAG_SD, "Filesystem mounted");
		sdmmc_card_print_info(stdout, card);
		return ret;
	}

	if (ret == ESP_FAIL) {
		ESP_LOGE(TAG_SD, "Failed to mount filesystem. "
				"Format the card if needed before use.");
	} else {
		ESP_LOGE(TAG_SD, "Failed to initialize the card (%s). "
				"Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
		#ifdef CONFIG_EXAMPLE_DEBUG_PIN_CONNECTIONS
			check_sd_card_pins(&config, pin_count);
		#endif
	}

	return ret;
}

esp_err_t sd_spi_config(uint8_t spi_host, uint8_t cs_pin) {
	ESP_LOGI(TAG_SD, "Initializing SD card. Using SPI peripheral");

	// For SoCs where the SD power can be supplied both via an internal or external (e.g. on-board LDO) power supply.
	// When using specific IO pins (which can be used for ultra high-speed SDMMC) to connect to the SD card
	// and the internal LDO power supply, we need to initialize the power supply first.
	#if CONFIG_EXAMPLE_SD_PWR_CTRL_LDO_INTERNAL_IO
		sd_pwr_ctrl_ldo_config_t ldo_config = {
			.ldo_chan_id = CONFIG_EXAMPLE_SD_PWR_CTRL_LDO_IO_ID,
		};
		sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;

		ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG_SD, "Failed to create a new on-chip LDO power control driver");
			return;
		}
		host.pwr_ctrl_handle = pwr_ctrl_handle;
	#endif

	
	// This initializes the slot without card detect (CD) and write protect (WP) signals.
	// Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
	sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
	slot_config.gpio_cs = cs_pin;
	slot_config.host_id = spi_host;

	static sdmmc_host_t host = SDSPI_HOST_DEFAULT();
	// host.slot = spi_host;

	ESP_LOGI(TAG_SD, "Mounting filesystem");
	esp_vfs_fat_sdmmc_mount_config_t mount_config = {
		.format_if_mount_failed = false,
		.max_files = 5,
		.allocation_unit_size = 16 * 1024
	};

	//# Mounting SD card
	ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
	return check_sd_card(ret);
}



#define EXAMPLE_IS_UHS1	(CONFIG_EXAMPLE_SDMMC_SPEED_UHS_I_SDR50 || CONFIG_EXAMPLE_SDMMC_SPEED_UHS_I_DDR50)

#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

void sd_mmc_config(int8_t clk, int8_t cmd, int8_t d0, int8_t d1, int8_t d2, int8_t d3) {
	// // By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT (20MHz)
	// // For setting a specific frequency, use host.max_freq_khz (range 400kHz - 40MHz for SDMMC)
	// // Example: for fixed frequency of 10MHz, use host.max_freq_khz = 10000;
	// sdmmc_host_t host = SDMMC_HOST_DEFAULT();
	// #if CONFIG_EXAMPLE_SDMMC_SPEED_HS
	//	 host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
	// #elif CONFIG_EXAMPLE_SDMMC_SPEED_UHS_I_SDR50
	//	 host.slot = SDMMC_HOST_SLOT_0;
	//	 host.max_freq_khz = SDMMC_FREQ_SDR50;
	//	 host.flags &= ~SDMMC_HOST_FLAG_DDR;
	// #elif CONFIG_EXAMPLE_SDMMC_SPEED_UHS_I_DDR50
	//	 host.slot = SDMMC_HOST_SLOT_0;
	//	 host.max_freq_khz = SDMMC_FREQ_DDR50;
	// #endif

	// // For SoCs where the SD power can be supplied both via an internal or external (e.g. on-board LDO) power supply.
	// // When using specific IO pins (which can be used for ultra high-speed SDMMC) to connect to the SD card
	// // and the internal LDO power supply, we need to initialize the power supply first.
	// #if CONFIG_EXAMPLE_SD_PWR_CTRL_LDO_INTERNAL_IO
	//	 sd_pwr_ctrl_ldo_config_t ldo_config = {
	//		 .ldo_chan_id = CONFIG_EXAMPLE_SD_PWR_CTRL_LDO_IO_ID,
	//	 };
	//	 sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;

	//	 ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
	//	 if (ret != ESP_OK) {
	//		 ESP_LOGE(TAG_SD, "Failed to create a new on-chip LDO power control driver");
	//		 return;
	//	 }
	//	 host.pwr_ctrl_handle = pwr_ctrl_handle;
	// #endif

	// // This initializes the slot without card detect (CD) and write protect (WP) signals.
	// // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
	// sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
	// #if EXAMPLE_IS_UHS1
	//	 slot_config.flags |= SDMMC_SLOT_FLAG_UHS1;
	// #endif

	// //! Note that even if card's D3 line is not connected to the ESP chip, 
	// //! it still has to be pulled up, otherwise the card will go into SPI protocol mode.
	// slot_config.clk = clk;
	// slot_config.cmd = cmd;
	// slot_config.d0 = d0;
	// slot_config.width = 1;

	// // if (config->mmc.enable_width4) {
	// //	 slot_config.d1 = config->mmc.d1;
	// //	 slot_config.d2 = config->mmc.d2;
	// //	 slot_config.d3 = config->mmc.d3;
	// //	 slot_config.width = 4;
	// // }

	// // Enable internal pullups on enabled pins. The internal pullups
	// // are insufficient however, please make sure 10k external pullups are
	// // connected on the bus. This is for debug / example purpose only.
	// slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

	// ESP_LOGI(TAG_SD, "Mounting filesystem");
	// esp_err_t ret;
	// esp_vfs_fat_sdmmc_mount_config_t mount_config = {
	//	 .format_if_mount_failed = false,
	//	 .max_files = 5,
	//	 .allocation_unit_size = 16 * 1024
	// };

	// ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
	// check_sd_card(ret);
}


//# Format Card
void storage_sd_format_card() {
	ret = esp_vfs_fat_sdcard_format(MOUNT_POINT, card);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG_SD, "Failed to format FATFS (%s)", esp_err_to_name(ret));
		return;
	}

	struct stat st;
	const char *file_foo = MOUNT_POINT"/foo.txt";

	if (stat(file_foo, &st) == 0) {
		ESP_LOGI(TAG_SD, "file still exists");
		return;
	} else {
		ESP_LOGI(TAG_SD, "file doesn't exist, formatting done");
	}
}

//# Unmount card
void mod_sd_deinit(spi_host_device_t slot) {
	//! unmount partition and disable SPI peripheral
	esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
	ESP_LOGI(TAG_SD, "Card unmounted");

	//! deinitialize the bus after all devices are removed
	spi_bus_free(slot);
}

void sd_test(void) {
	if (ret != ESP_OK) return;
	
	//# First create a file.
	const char *file_hello = MOUNT_POINT"/hello.txt";
	char data[MAX_CHAR_SIZE];
	printf("card name: %s", card->cid.name);

	snprintf(data, MAX_CHAR_SIZE, "%s %s!\n", "Hello ... Hello ... ", card->cid.name);
	ret = sd_write(file_hello, data);
	if (ret != ESP_OK) return;


	//# Check if destination file exists before renaming
	const char *file_foo = MOUNT_POINT"/foo2.txt";
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
	ret = sd_get(file_foo, line, sizeof(line));
	if (ret != ESP_OK) return;

	const char *file_nihao = MOUNT_POINT"/nihao.txt";
	memset(data, 0, MAX_CHAR_SIZE);
	snprintf(data, MAX_CHAR_SIZE, "%s %s!\n", "Nihao", card->cid.name);
	ret = sd_write(file_nihao, data);
	if (ret != ESP_OK) return;

	//# Open file for reading
	ret = sd_get(file_foo, line, sizeof(line));
	if (ret != ESP_OK) return;

	//	 // Deinitialize the power control driver if it was used
	// #if CONFIG_EXAMPLE_SD_PWR_CTRL_LDO_INTERNAL_IO
	//	 ret = sd_pwr_ctrl_del_on_chip_ldo(pwr_ctrl_handle);
	//	 if (ret != ESP_OK) {
	//		 ESP_LOGE(TAG_SD, "Failed to delete the on-chip LDO power control driver");
	//		 return;
	//	 }
	// #endif
}





#include <errno.h>

static bool sd_ensure_dir(const char *path) {
	struct stat st;
	if (stat(path, &st) == 0) {
		return S_ISDIR(st.st_mode);   // Exists and is a dir
	}
	// Not existing -> try to create
	if (mkdir(path, 0775) != 0 && errno != EEXIST) {
		ESP_LOGE("FS", "mkdir(%s) failed: errno=%d", path, errno);
		return false;
	}
	return true;
}

static bool sd_append_data(const char *path, char *data) {
	FILE *f = fopen(path, "a");	 // "a" for append - create if doesn't exit
	if (f == NULL) {
		ESP_LOGE(TAG_SD, "Failed to open file for writing: %s", path);
		return false;
	}
	fprintf(f, data);
	fclose(f);
	ESP_LOGI(TAG_SD, "write to file: %s", path);
	return true;
}

static void sd_csv_write(const char *uuid, const char *dateStr, const char *data) {
	char path[128];
	snprintf(path, sizeof(path), MOUNT_POINT"/log/%s/%s.csv", uuid, dateStr);
	if (sd_append_data(path, data)) return;

	if (!sd_ensure_dir(MOUNT_POINT)) {
		ESP_LOGE(TAG_SD, "Failed to create directory");
		return;
	}

	snprintf(path, sizeof(path), MOUNT_POINT"/log/%s", uuid);
	if (!sd_ensure_dir(path)) {
		ESP_LOGE(TAG_SD, "Failed to create directory");
		return;
	}

	// Now open /sdcard/Log1/<uuid>/sec/Date.txt
	snprintf(path, sizeof(path), MOUNT_POINT"/log/%s/%s.csv", uuid, dateStr);
	if (!sd_append_data(path, data)) {
		ESP_LOGE(TAG_SD, "Failed to write data");
	}
}




static bool sd_remove_dir_recursive(const char* path) {
    DIR* dir = opendir(path);
    if (!dir) {
        ESP_LOGE(TAG_SD, "Cannot open directory: %s", path);
        return false;
    }
    
    struct dirent* entry;
    char full_path[512];
    bool success = true;
    
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Build full path
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        
        struct stat statbuf;
        if (stat(full_path, &statbuf) != 0) {
            ESP_LOGW(TAG_SD, "Cannot stat: %s", full_path);
            continue;
        }
        
        if (S_ISDIR(statbuf.st_mode)) {
            // Recursively remove subdirectory
            if (!sd_remove_dir_recursive(full_path)) {
                success = false;
            }
        } else {
            // Remove file
            if (remove(full_path) != 0) {
                ESP_LOGE(TAG_SD, "Failed to remove file %s, error: %d", 
                        full_path, errno);
                success = false;
            } else {
                ESP_LOGI(TAG_SD, "Removed file: %s", full_path);
            }
        }
    }
    
    closedir(dir);
    
    // Now remove the (hopefully) empty directory
    if (rmdir(path) != 0) {
        ESP_LOGE(TAG_SD, "Failed to remove directory %s, error: %d", 
                path, errno);
        return false;
    }
    
    ESP_LOGI(TAG_SD, "Removed directory: %s", path);
    return success;
}

static bool sd_overwrite_bin(const char *path, void *data, int data_len) {
	FILE *f = fopen(path, "wb");	 // "wb" for overwrite binary - create if doesn't exit
	if (f == NULL) {
		ESP_LOGE(TAG_SD, "Failed to overwrite: %s", path);
		return false;
	}
	fwrite(data, data_len, 1, f);
	fclose(f);
	// ESP_LOGI(TAG_SD, "overwrite file: %s", path);
	return true;
}

static bool sd_append_bin(const char *path, void *data, int data_len) {
	FILE *f = fopen(path, "ab");	 // "ab" for append binary - create if doesn't exit
	if (f == NULL) {
		ESP_LOGE(TAG_SD, "Failed to write: %s", path);
		return false;
	}
	fwrite(data, data_len, 1, f);
	fclose(f);
	ESP_LOGI(TAG_SD, "write file: %s", path);
	return true;
}

typedef struct {
	uint32_t timestamp;
	int16_t value1;
	int16_t value2;
	int16_t value3;
} __attribute__((packed)) record_t;

typedef struct {
	uint32_t uuid;
	uint32_t timeRef;
	uint32_t config;
	uint8_t count;			// aggregated count
	int16_t sum1;
	int16_t sum2;
	int16_t sum3;
} record_aggregate_t;

static void sd_bin_write(const char *uuid, const char *dateStr, record_t* records) {
	char path[64];
	snprintf(path, sizeof(path), MOUNT_POINT"/log/%s/%s.bin", uuid, dateStr);

	if (sd_append_bin(path, records, sizeof(record_t))) {
		static time_t last_replace = 0;
		time_t now = time(NULL);
		snprintf(path, sizeof(path), MOUNT_POINT"/log/%s/latest.bin", uuid);

		//# Replace every 1800 seconds OR 30 minutes
		if (last_replace == 0 || now - last_replace > LOG_FILE_UPDATE_INTERVAL) {
			last_replace = now;
			sd_overwrite_bin(path, records, sizeof(record_t));
		} else {
			sd_append_bin(path, records, sizeof(record_t));
		}
		return;
	}

	snprintf(path, sizeof(path), MOUNT_POINT"/log/%s", uuid);
	if (!sd_ensure_dir(path)) {
		ESP_LOGE(TAG_SD, "Failed to create directory");
		return;
	}

	//# Open and write to /sdcard/Log/<uuid>/<dateStr>.bin
	snprintf(path, sizeof(path), MOUNT_POINT"/log/%s/%s.bin", uuid, dateStr);
	if (!sd_append_bin(path, records, sizeof(record_t))) {
		ESP_LOGE(TAG_SD, "Failed to write log data");
	}

	//# Open and write to /sdcard/Log/<uuid>/latest.bin
	snprintf(path, sizeof(path), MOUNT_POINT"/log/%s/latest.bin", uuid);
	if (!sd_append_bin(path, records, sizeof(record_t))) {
		ESP_LOGE(TAG_SD, "Failed to write latest data");
	}
}


#define LOG_RECORD_COUNT 10
record_aggregate_t RECORD_AGGREGATE[LOG_RECORD_COUNT] = {0};

// STEP1: /log/<uuid>/2025/latest.bin - 1 hour of record every second (3600 records)
// STEP2: /log/<uuid>/2025/1230.bin - 24 hours records every minute (1440 records - 60 per hour)
// STEP3: /log/<uuid>/2025/12.bin - 30 days records every 10 minutes (4320 records - 144 per day)

static void sd_bin_record_all(uint32_t uuid, struct tm *tm, record_t* record) {
	//# STEP1: `/log/<uuid>/2025/latest.bin`
	char file_path[64];
	int year = tm->tm_year + 1900;
	int month = tm->tm_mon + 1;
	int day = tm->tm_mday;

	time_t now = time(NULL);

	for (int i = 0; i < LOG_RECORD_COUNT; i++) {
		record_aggregate_t *recs = &RECORD_AGGREGATE[i];
		if (recs->uuid != uuid) continue;

		//# /log/<uuid>
		snprintf(file_path, sizeof(file_path), MOUNT_POINT"/log/%08lX", uuid);
		if (!sd_ensure_dir(file_path)) {
			ESP_LOGE(TAG_SD, "Failed to create /<uuid>");
			continue;
		}

		//# /log/<uuid>/2025
		snprintf(file_path, sizeof(file_path), MOUNT_POINT"/log/%08lX/%d", uuid, year);
		if (!sd_ensure_dir(file_path)) {
			ESP_LOGE(TAG_SD, "Failed to create /<uuid>/year");
			continue;
		}

		uint32_t time_dif = now - recs->timeRef;
		snprintf(file_path, sizeof(file_path), MOUNT_POINT"/log/%08lX/%d/latest.bin", uuid, year);

		//# replace 1 hour of records for every second (3600 records)
		if (recs->timeRef == 0 || time_dif > 3600) {
			recs->timeRef = now;
			sd_overwrite_bin(file_path, record, sizeof(record_t));
		}
		else {
			sd_append_bin(file_path, record, sizeof(record_t));
		}

		recs->count++;
		recs->sum1 += record->value1;
		recs->sum2 += record->value2;
		recs->sum3 += record->value3;

		if (recs->count >= 60) {
			record_t minute_avg = {
				.timestamp = record->timestamp,
				.value1 = recs->sum1 / recs->count,
				.value2 = recs->sum2 / recs->count,
				.value3 = recs->sum3 / recs->count
			};

			//# STEP2: `/log/<uuid>/2025/1230.bin`
			snprintf(file_path, sizeof(file_path), MOUNT_POINT"/log/%08lX/%d/%d%d.bin", 
				uuid, year, month, day
			);
			sd_append_bin(file_path, record, sizeof(record_t));

			recs->count = 0;
			recs->sum1 = 0;
			recs->sum2 = 0;
			recs->sum3 = 0;
		}
	}
}

// Config: /log/config.bin
static esp_err_t sd_save_config(uint32_t uuid, uint32_t config) {
	const char *file_path = MOUNT_POINT"/log/config.txt";

	FILE *f = fopen(file_path, "w");	 // overwrite - create if doesn't exit
	if (!f) {
		ESP_LOGE(TAG_SD, "Failed to write	: %s", file_path);
		return ESP_FAIL;
	}

	for (int i = 0; i < LOG_RECORD_COUNT; i++) {
		record_aggregate_t *recs = &RECORD_AGGREGATE[i];
		if (recs->uuid == 0) break;
		if (recs->uuid == uuid) recs->config = config;
		fprintf(f, "%08lX %ld\n", recs->uuid, recs->config);
	}

	fclose(f);
	ESP_LOGW(TAG_SD, "write to file: %s", file_path);
	return ESP_OK;
}

static esp_err_t sd_load_config() {
    const char *file_path = MOUNT_POINT"/log/config.txt";
    FILE *f = fopen(file_path, "r");
    if (!f) {
		ESP_LOGE(TAG_SD, "Failed to read: %s", file_path);
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
		
		if (uuid == 0 || config == 0) continue;
		record_aggregate_t *recs = &RECORD_AGGREGATE[loaded];
		recs->uuid = uuid;
		recs->config = config;
		loaded++;
		printf("Load Config uuid: %08lX, Config: %ld\n", uuid, config);
    }
    
    fclose(f);
    ESP_LOGI(TAG_SD, "Loaded %d configs", loaded);
    return ESP_OK;
}

// Function to read and verify binary data
size_t sd_bin_read(const char *uuid, const char *dateStr, record_t *buffer, size_t max_records) {
	char file_path[64];
	snprintf(file_path, sizeof(file_path), MOUNT_POINT"/log/%s/%s.bin", uuid, dateStr);

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