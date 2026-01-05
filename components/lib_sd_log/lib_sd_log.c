#include "lib_sd_log.h"
#include "esp_timer.h"

#define ROTATE_LOG_FILE_COUNT 2
#define LOG_LINE_LENGTH 128

//###################################################
//# SD Card Setup
//###################################################

static sdmmc_card_t *card;

static esp_err_t check_sd_card(esp_err_t ret) {
	if (ret == ESP_OK) {
		//# Card has been initialized, print its properties
		ESP_LOGI(TAG_SD, "Filesystem mounted");
		sdmmc_card_print_info(stdout, card);
		return ret;
	}

	if (ret == ESP_FAIL) {
		ESP_LOGE(TAG_SD, "Err: mount filesystem. "
				"Format the card if needed before use.");
	} else {
		ESP_LOGE(TAG_SD, "Err: initialize the card (%s). "
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
	esp_err_t ret = esp_vfs_fat_sdspi_mount(SD_POINT, &host, &slot_config, &mount_config, &card);
    printf("card name: %s", card->cid.name);
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
	//		 ESP_LOGE(TAG, "Failed to create a new on-chip LDO power control driver");
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

	// ESP_LOGI(TAG, "Mounting filesystem");
	// esp_err_t ret;
	// esp_vfs_fat_sdmmc_mount_config_t mount_config = {
	//	 .format_if_mount_failed = false,
	//	 .max_files = 5,
	//	 .allocation_unit_size = 16 * 1024
	// };

	// ret = esp_vfs_fat_sdmmc_mount(SD_POINT, &host, &slot_config, &mount_config, &card);
	// check_sd_card(ret);
}


//# Format Card
void storage_sd_format_card() {
	esp_err_t ret = esp_vfs_fat_sdcard_format(SD_POINT, card);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG_SD, "Err: format FATFS (%s)", esp_err_to_name(ret));
		return;
	}
}

//# Unmount card
void mod_sd_deinit(spi_host_device_t slot) {
	// unmount partition and disable SPI peripheral
	esp_vfs_fat_sdcard_unmount(SD_POINT, card);
	ESP_LOGI(TAG_SD, "Card unmounted");

	// deinitialize the bus after all devices are removed
	spi_bus_free(slot);
}

size_t sd_read_file(const char *path, char *buff, size_t len) {
	FILE *f = fopen(path, "r");
    if (!f) {
		ESP_LOGE_SD(TAG_SD, "Err sd_read_file: %s", path);
		return 0;
	}
	
	// Read directly with manual tracking
	size_t count = 0;
	int c;
	
	// -1 to leave room for null terminator
	while (count < len - 1 && (c = fgetc(f)) != EOF) {
		buff[count++] = c;
	}
	
	buff[count] = '\0';
	fclose(f);
	return count;
}

size_t sd_read_tail(const char *path, char *out, size_t max) {
    FILE *f = fopen(path, "rb");
    if (!f) {
		ESP_LOGE_SD(TAG_SD, "Err sd_read_tail: %s", path);
		return 0;
	}
    
    // All in local registers where possible
    long sz;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    
    if (sz <= 0) {
        fclose(f);
        out[0] = '\0';
        return 0;
    }
    
    // Single conditional
    size_t n = (sz < max) ? sz : max;
    fseek(f, -n, SEEK_END);
    n = fread(out, 1, n, f);  // Reuse n for actual read count
    
    fclose(f);
    out[n] = '\0';
    return n;
}

//###################################################
//# ROTATE LOG
//###################################################

rotate_log_t system_log = {
	.file = NULL,
	.file_num = 0,
	.lines = 0,
	.prefix = "sys",
	.CLOSE_LINES = 20,
	.MAX_LINES = 10000
};

rotate_log_t error_log = {
	.file = NULL,
	.file_num = 0,
	.lines = 0,
	.prefix = "err",
	.CLOSE_LINES = 5,
	.MAX_LINES = 10000
};

// why: rotate log between 2 files, prevent opening file too many times because it is slow
// fclose is required for the log to be written properly, so here we close the log file every x lines
// and rotate to the next file when we reach the maximum specified number of lines

void rotate_log_close(rotate_log_t *log) {
	if (log->file) fclose(log->file);
	log->file = NULL;
}

void rotate_log_write(rotate_log_t *log, const char *msg) {
	// Close every x lines
	if (log->lines % log->CLOSE_LINES == 0) {
		if (log->file) fclose(log->file);
		
		// Rotate to next file every x lines
		if (log->lines >= log->MAX_LINES) {
			log->file_num = (log->file_num + 1) % ROTATE_LOG_FILE_COUNT;
			log->lines = 0;
		}
		
		// Open current file
		char path[64];
		snprintf(path, sizeof(path), SD_POINT"/log/%s_%d.txt", log->prefix, log->file_num);
		log->file = fopen(path, log->lines == 0 ? "w" : "a");

		if (!log->file) {
			ESP_LOGE(TAG_SD, "open err rotate_log: %s", path);
			return;
		}
	}

	// Write
	if (log->file) {
		fputs(msg, log->file);
		fputc('\n', log->file);
		log->lines++;
	}
}

// read logs from the last 2 files up to the maximum specified size

size_t rotate_log_get_latest(rotate_log_t *log, char *buffer, size_t buffer_size) {
    // Input validation
    if (!buffer || buffer_size < 2) {
        if (buffer && buffer_size > 0) buffer[0] = '\0';
        return 0;
    }

	buffer[0] = '\0';	// Clear buffer
	size_t total = 0;

	//# File 1:  Read from current file
	char path[64];
	snprintf(path, sizeof(path), SD_POINT"/log/%s_%d.txt", log->prefix, log->file_num);

	FILE *f = fopen(path, "r");
	if (f) {
		// Get file size first
		fseek(f, 0, SEEK_END);
		long size = ftell(f);
		
		// Safe seek (don't seek before file start)
		size_t to_read = (size < buffer_size - 1) ? size : buffer_size - 1;

		if (to_read > 0) {
			fseek(f, -to_read, SEEK_END);
			total = fread(buffer, 1, to_read, f);
		}
		fclose(f);
	}

	//# File 2: Previous 
	if (total < buffer_size && ROTATE_LOG_FILE_COUNT > 1) {
		int prev = (log->file_num - 1 + ROTATE_LOG_FILE_COUNT) % ROTATE_LOG_FILE_COUNT;

		snprintf(path, sizeof(path), SD_POINT"/log/%s_%d.txt", log->prefix, prev);
		f = fopen(path, "r");

		if (f) {
			fseek(f, 0, SEEK_END);
			long size = ftell(f);
			
			size_t need = buffer_size - total - 1;  // -1 for null terminator
			size_t to_read = (size < need) ? size : need;
			
			if (to_read > 0) {
				fseek(f, -to_read, SEEK_END);
				total += fread(buffer + total, 1, to_read, f);
			}
			fclose(f);
		}
	}

	buffer[total] = '\0';
	return total;
}

void log_to_sd(rotate_log_t *log, const char *tag, const char *format, ...) {
	char log_line[LOG_LINE_LENGTH];
	int64_t runtime_ms = esp_timer_get_time() / 1000;
	int written = snprintf(log_line, sizeof(log_line), "[%lld] %s: ", runtime_ms, tag);

	if (written > 0 && written < sizeof(log_line)) {
		va_list args;
		va_start(args, format);
		vsnprintf(log_line + written, sizeof(log_line) - written, format, args);
		va_end(args);
	}

	rotate_log_write(log, log_line);
}


//###################################################
//# Remove directory - Recursively
//###################################################

#include <string.h>         // For strcmp
#include <dirent.h>         // For dir
#include <errno.h>          // For errno
#include <sys/stat.h>       // For stat()
#include <unistd.h>         // For rmdir()

// remove directory recursively
int sd_remove_dir(const char* path) {
	DIR* dir = opendir(path);
	if (!dir) {
		ESP_LOGE(TAG_SD, "Err open directory: %s", path);
		return 0;
	}

	struct dirent* entry;
	char full_path[512];
	int success = 1;

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
			if (!sd_remove_dir(full_path)) {
				success = 0;
			}
		} else {
			// Remove file
			if (remove(full_path) != 0) {
				ESP_LOGE(TAG_SD, "Err remove file %s, error: %d", 
						full_path, errno);
				success = 0;
			} else {
				ESP_LOGI(TAG_SD, "Removed file: %s", full_path);
			}
		}
	}

	closedir(dir);

	// Now remove the (hopefully) empty directory
	if (rmdir(path) != 0) {
		ESP_LOGE(TAG_SD, "remove dir err %s, error: %d", 
				path, errno);
		return 0;
	}

	ESP_LOGI(TAG_SD, "Removed dir: %s", path);
	return success;
}

int sd_ensure_dir(const char *path) {
	struct stat st;
	if (stat(path, &st) == 0) {
		return S_ISDIR(st.st_mode);   // Exists and is a dir
	}
	// Not existing -> try to create
	if (mkdir(path, 0775) != 0 && errno != EEXIST) {
		ESP_LOGE_SD(TAG_SD, "mkdir(%s) failed: errno=%d", path, errno);
		return 0;
	}
	return 1;
}

int sd_overwrite_bin(const char *path, void *data, int data_len) {
	FILE *f = fopen(path, "wb");	 // "wb" for overwrite binary - create if doesn't exit
	if (f == NULL) {
		ESP_LOGE_SD(TAG_SD, "Err overwrite: %s", path);
		return 0;
	}
	fwrite(data, data_len, 1, f);
	fclose(f);
	ESP_LOGI_SD(TAG_SD, "overwritten: %s", path);
	return 1;
}

int sd_append_bin(const char *path, void *data, int data_len) {
	FILE *f = fopen(path, "ab");	 // "ab" for append binary - create if doesn't exit
	if (f == NULL) {
		ESP_LOGE_SD(TAG_SD, "Err write: %s", path);
		return 0;
	}
	fwrite(data, data_len, 1, f);
	fclose(f);
	ESP_LOGI_SD(TAG_SD, "written: %s", path);
	return 1;
}


//###################################################
//# Get entries and Folders
//###################################################

void sd_list_dirs(const char *base_path, int depth) {
	DIR *dir;
	if (!(dir = opendir(base_path))) {
		ESP_LOGE(TAG_SD, "Could not open directory: %s", base_path);
		return;
	}

	struct dirent *entry;
	struct stat file_stat;
	char path[512];

	while ((entry = readdir(dir)) != NULL) {
		// Skip current and parent directory entries
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
		// Create full path
		snprintf(path, sizeof(path), "%s/%s", base_path, entry->d_name);
		
		// Get file/directory information
		if (stat(path, &file_stat) == -1) {
			ESP_LOGW(TAG_SD, "Failed to stat %s", path);
			continue;
		}
		
		// Create indentation for hierarchy
		char indent[16];
		memset(indent, ' ', depth * 2);
		indent[depth * 2] = '\0';
		
		if (S_ISDIR(file_stat.st_mode)) {
			// It's a directory
			ESP_LOGW(TAG_SD, "%s📁 %s/", indent, entry->d_name);
			
			// Recursively list subdirectory
			sd_list_dirs(path, depth + 1);
		} else {
			// It's a file
			ESP_LOGW(TAG_SD, "%s📄 %s (Size: %ld bytes)", indent, entry->d_name, file_stat.st_size);
		}
	}

	closedir(dir);
}


int sd_entries_to_json(const char *base_path, char *json, int size) {
	if (size < 3) return 0;
	
	DIR *dir = opendir(base_path);
	if (!dir) {
		json[0] = '['; json[1] = ']'; json[2] = '\0';
		return 2;
	}
	
	struct dirent *entry;
	char *ptr = json;
	*ptr++ = '[';
	int first = 1;
	
	// NO stat() calls at all!
	while ((entry = readdir(dir)) != NULL && ptr < json + size - 4) {
		// printf("Entry: %s\n", entry->d_name);
		if (!first) *ptr++ = ',';
		first = 0;
		*ptr++ = '"';
		const char *s = entry->d_name;
		while (*s && ptr < json + size - 2) *ptr++ = *s++;
		*ptr++ = '"';
	}
	
	closedir(dir);
	*ptr++ = ']';
	*ptr = '\0';

	return ptr - json;
}