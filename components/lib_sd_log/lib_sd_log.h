#ifndef LIB_SD_LOG_H
#define LIB_SD_LOG_H

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "esp_log.h"

#define SD_POINT "/sdcard"
static const char *TAG_SF = "#FS";

// Color macros
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_WHITE   "\033[37m"
#define COLOR_BOLD    "\033[1m"

// Logging macros with colors
#define LOG_INFO(fmt, ...)  printf(COLOR_CYAN "I " COLOR_RESET fmt "\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  printf(COLOR_YELLOW "W " COLOR_RESET fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) printf(COLOR_RED "E " COLOR_RESET fmt "\n", ##__VA_ARGS__)
#define LOG_SUCCESS(fmt, ...) printf(COLOR_GREEN "S " COLOR_RESET fmt "\n", ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) printf(COLOR_MAGENTA "D " COLOR_RESET fmt "\n", ##__VA_ARGS__)


esp_err_t sd_spi_config(uint8_t spi_host, uint8_t cs_pin);

int sd_remove_dir_recursive(const char* path);
int sd_ensure_dir(const char *path);
int sd_overwrite_bin(const char *path, void *data, int data_len);
int sd_append_bin(const char *path, void *data, int data_len);
int sd_entries_to_json(const char *path, char *json, int size);
void sd_list_dirs(const char *base_path, int depth);
size_t sd_read_file(const char *path, char *buff, size_t len);
size_t sd_read_tail(const char *path, char *out, size_t max);
size_t sd_write_str(const char *path, const char *str);

esp_err_t sd_remove_file(const char *path);
esp_err_t sd_rename(const char *old_path, const char *new_path);
int sd_card_info(char *buffer);

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

#define ESP_LOGW_SD(tag, format, ...) do { \
	ESP_LOGW(tag, format, ##__VA_ARGS__); \
	log_to_sd(&system_log, tag, format, ##__VA_ARGS__); \
} while(0)

#define ESP_LOGE_SD(tag, format, ...) do { \
	ESP_LOGE(tag, format, ##__VA_ARGS__); \
	log_to_sd(&error_log, tag, format, ##__VA_ARGS__); \
	log_to_sd(&system_log, tag, format, ##__VA_ARGS__); \
} while(0)


#endif /* LIB_SD_LOG_H */