#include "esp_littlefs.h"
#include "esp_log.h"

#define LITTLEFS_POINT "/littlefs"

static const char *TAG_LITFS = "#LitFS";

void littleFS_init() {
	const char method_name[] = "littleFS_init";

	esp_vfs_littlefs_conf_t conf = {
		.base_path = LITTLEFS_POINT,
		.partition_label = "storage",
		.format_if_mount_failed = true,
		.dont_mount = false,
	};

	// Use settings defined above to initialize and mount LittleFS filesystem.
	// Note: esp_vfs_littlefs_register is an all-in-one convenience function.
	esp_err_t ret = esp_vfs_littlefs_register(&conf);
	if (ret != ESP_OK) {
		if (ret == ESP_FAIL) {
			ESP_LOGE(TAG_LITFS, "%s MOUNT-FAILED", method_name);
		} else if (ret == ESP_ERR_NOT_FOUND) {
			ESP_LOGE(TAG_LITFS, "%s NOT-FOUND", method_name);
		} else {
			ESP_LOGE(TAG_LITFS, "%s ERR-FOUND: %s", method_name, esp_err_to_name(ret));
		}
		return;
	}

	size_t total = 0, used = 0;
	ret = esp_littlefs_info(conf.partition_label, &total, &used);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG_LITFS, "%s INFO-EXCEPTION: %s", method_name, esp_err_to_name(ret));
		esp_littlefs_format(conf.partition_label);
	} else {
		ESP_LOGW(TAG_LITFS, "%s PARTITION-INFO", method_name);
		printf("Partition: %dB (used)/ %d\n", used, total);
	}
}

void littleFS_test() {
	ESP_LOGW(TAG_LITFS, "Opening file");
	FILE *f = fopen(LITTLEFS_POINT"/hello.txt", "w");
	if (f == NULL) {
		ESP_LOGE(TAG_LITFS, "Failed to open file for writing");
		return;
	}
	fprintf(f, "Hello World!\n");
	fclose(f);
	ESP_LOGW(TAG_LITFS, "File written");

	// Check if destination file exists before renaming
	struct stat st;
	if (stat(LITTLEFS_POINT"/foo.txt", &st) == 0) {
		// Delete it if it exists
		unlink(LITTLEFS_POINT"/foo.txt");
	}

	// Rename original file
	ESP_LOGW(TAG_LITFS, "Renaming file");
	if (rename(LITTLEFS_POINT"/hello.txt", LITTLEFS_POINT"/foo.txt") != 0) {
		ESP_LOGE(TAG_LITFS, "Rename failed");
		return;
	}

	// Open renamed file for reading
	ESP_LOGW(TAG_LITFS, "Reading file");
	f = fopen(LITTLEFS_POINT"/foo.txt", "r");
	if (f == NULL) {
		ESP_LOGE(TAG_LITFS, "Failed to open file for reading");
		return;
	}

	char line[128] = {0};
	fgets(line, sizeof(line), f);
	fclose(f);
	// strip newline
	char* pos = strpbrk(line, "\r\n");
	if (pos) {
		*pos = '\0';
	}
	ESP_LOGW(TAG_LITFS, "Read from file: '%s'", line);

	ESP_LOGW(TAG_LITFS, "Reading from flashed filesystem example.txt");
	f = fopen(LITTLEFS_POINT"/example.txt", "r");
	if (f == NULL) {
		ESP_LOGE(TAG_LITFS, "Failed to open file for reading");
		return;
	}
	fgets(line, sizeof(line), f);
	fclose(f);
	// strip newline
	pos = strpbrk(line, "\r\n");
	if (pos) {
		*pos = '\0';
	}
	ESP_LOGW(TAG_LITFS, "Read from file: '%s'", line);

	// All done, unmount partition and disable LittleFS
	// esp_vfs_littlefs_unregister(conf.partition_label);
	// esp_vfs_littlefs_unregister("storage");
	// ESP_LOGW(TAG_LITFS, "LittleFS unmounted");
}