#include "esp_littlefs.h"
#include "esp_log.h"

static const char *TAG_LITFS = "[LitFS]";

void littlefs_test() {
	esp_vfs_littlefs_conf_t conf = {
		.base_path = "/littlefs",
		.partition_label = "storage",
		.format_if_mount_failed = true,
		.dont_mount = false,
	};

	// Use settings defined above to initialize and mount LittleFS filesystem.
	// Note: esp_vfs_littlefs_register is an all-in-one convenience function.
	esp_err_t ret = esp_vfs_littlefs_register(&conf);
	if (ret != ESP_OK) {
		if (ret == ESP_FAIL) {
			ESP_LOGE(TAG_LITFS, "Err: LittleFS mount failed");
		} else if (ret == ESP_ERR_NOT_FOUND) {
			ESP_LOGE(TAG_LITFS, "Not Found: LittleFS partition");
		} else {
			ESP_LOGE(TAG_LITFS, "Init Err: LittleFS (%s)", esp_err_to_name(ret));
		}
		return;
	}

	size_t total = 0, used = 0;
	ret = esp_littlefs_info(conf.partition_label, &total, &used);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG_LITFS, "Err: LittleFS info (%s)", esp_err_to_name(ret));
		esp_littlefs_format(conf.partition_label);
	} else {
		ESP_LOGW(TAG_LITFS, "LittleFS partition total: %d, used: %d", total, used);
	}

	ESP_LOGW(TAG_LITFS, "Opening file");
	FILE *f = fopen("/littlefs/hello.txt", "w");
	if (f == NULL) {
		ESP_LOGE(TAG_LITFS, "Failed to open file for writing");
		return;
	}
	fprintf(f, "Hello World!\n");
	fclose(f);
	ESP_LOGW(TAG_LITFS, "File written");

	// Check if destination file exists before renaming
	struct stat st;
	if (stat("/littlefs/foo.txt", &st) == 0) {
		// Delete it if it exists
		unlink("/littlefs/foo.txt");
	}

	// Rename original file
	ESP_LOGW(TAG_LITFS, "Renaming file");
	if (rename("/littlefs/hello.txt", "/littlefs/foo.txt") != 0) {
		ESP_LOGE(TAG_LITFS, "Rename failed");
		return;
	}

	// Open renamed file for reading
	ESP_LOGW(TAG_LITFS, "Reading file");
	f = fopen("/littlefs/foo.txt", "r");
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
	f = fopen("/littlefs/example.txt", "r");
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
	esp_vfs_littlefs_unregister(conf.partition_label);
	ESP_LOGW(TAG_LITFS, "LittleFS unmounted");
}