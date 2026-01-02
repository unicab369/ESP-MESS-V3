#ifndef LIB_SD_LOG_H
#define LIB_SD_LOG_H

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "esp_log.h"

typedef struct {
	FILE *file;
	int file_num;
	int lines;
	char prefix[8];
	int CLOSE_LINES;
	int MAX_LINES;
} rotate_log_t;

extern rotate_log_t system_log;
extern rotate_log_t error_log;

void log_to_sd(rotate_log_t *log, const char *tag, const char *format, ...);

#define ESP_LOGI_SD(tag, format, ...) do { \
	ESP_LOGI(tag, format, ##__VA_ARGS__); \
	log_to_sd(&system_log, tag, format, ##__VA_ARGS__); \
} while(0)

#define ESP_LOGE_SD(tag, format, ...) do { \
	ESP_LOGE(tag, format, ##__VA_ARGS__); \
	log_to_sd(&error_log, tag, format, ##__VA_ARGS__); \
} while(0)

#endif /* LIB_SD_LOG_H */