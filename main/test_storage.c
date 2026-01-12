#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_littlefs.h"
#include "esp_timer.h"
#include "esp_log.h"

//! WARNING !!!: writing to NVS and LittleFS cause wear on the flash chip
//! Make sure to not run these tests too often (4+ seconds loop cycle)

#define BUFFER_SIZE 128
static const char* TAG = "READ_TEST";

static uint8_t test_data[BUFFER_SIZE];
static uint8_t read_buffer[BUFFER_SIZE];

void benchmark_read_test() {	
	memset(test_data, 0x55, sizeof(test_data));

	// === WRITE PHASE ===	
	//# 1. NVS Write
	int64_t start = esp_timer_get_time();
	nvs_handle_t handle;
	nvs_open("bench", NVS_READWRITE, &handle);
	nvs_set_blob(handle, "test", test_data, sizeof(test_data));
	nvs_commit(handle);
	nvs_close(handle);
	int64_t nvs_write_time = esp_timer_get_time() - start;
	
	//# 2. LittleFS Write
	start = esp_timer_get_time();
	FILE* f = fopen("/littlefs/test.bin", "wb");
	fwrite(test_data, 1, sizeof(test_data), f);
	fclose(f);
	int64_t littlefs_write_time = esp_timer_get_time() - start;
	// Small delay to ensure writes are complete
	vTaskDelay(pdMS_TO_TICKS(10));
	
	// === READ PHASE ===
	//# 1. NVSS Read Test
	start = esp_timer_get_time();
	nvs_open("bench", NVS_READONLY, &handle);
	size_t size = sizeof(read_buffer);
	esp_err_t ret = nvs_get_blob(handle, "test", read_buffer, &size);
	nvs_close(handle);
	int64_t nvs_read_time = esp_timer_get_time() - start;
	if (ret != ESP_OK) {
		printf("NVSS read failed: %s\n", esp_err_to_name(ret));
	}
	
	//# 2. LittleFS Read Test
	int64_t littlefs_read_time = 0;
	start = esp_timer_get_time();
	f = fopen("/littlefs/test.bin", "rb");

	if (f == NULL) {
		printf("Failed to open LittleFS file for reading\n");
	}
	else {
		size_t bytes_read = fread(read_buffer, 1, sizeof(read_buffer), f);
		fclose(f);
		littlefs_read_time = esp_timer_get_time() - start;
		
		if (bytes_read != sizeof(read_buffer)) {
			printf("LittleFS read incomplete: %d bytes\n", bytes_read);
		}
	}
	
	// === SUMMARY ===
	int littlefs_write_win = littlefs_write_time < nvs_write_time ? 1 : 0;
	int littlefs_read_win = littlefs_read_time < nvs_read_time ? 1 : 0;
	// printf("\nOperation        | NVSS      | LittleFS  | Winner\n");
	// printf("-----------------|-----------|-----------|--------\n");
	// printf("Write        	 |  %5lld us |  %5lld us | %s\n", 
	// 	nvs_write_time, littlefs_write_time, littlefs_write_win ? "LittleFS" : "NVSS");
	// printf("Read         	 |  %5lld us |  %5lld us | %s\n", 
	// 	nvs_read_time, littlefs_read_time, littlefs_read_win ? "LittleFS" : "NVSS");

	printf("\nWRITE LittleFS is %s: %.1fx\n",
			littlefs_write_win ? "FASTER" : "SLOWER",
			(float)littlefs_write_time / nvs_write_time);
	printf("READ LittleFS is %s: %.1fx\n",
			littlefs_read_win ? "FASTER" : "SLOWER",
			(float)littlefs_read_time /nvs_read_time);
}

void app_main() {
	// Initialize storage first
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || 
		ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		nvs_flash_erase();
		nvs_flash_init();
	}
	
	// Initialize LittleFS
	esp_vfs_littlefs_conf_t conf = {
		.base_path = "/littlefs",
		.partition_label = "storage",
		.format_if_mount_failed = true,
	};
	esp_vfs_littlefs_register(&conf);
	
	printf("\n\n=======================================\n");
	printf("Storage Read/Write Benchmark Test\n");
	printf("=======================================\n");
	printf("Buffer size: %dB\n", BUFFER_SIZE);

	// Run the benchmark

	for(;;) {
		benchmark_read_test();
		vTaskDelay(pdMS_TO_TICKS(4000));
	}
}

//# Buffer size: 128B
// Operation        | NVSS      | LittleFS  | Winner
// -----------------|-----------|-----------|--------
// Write            |   1377 us |  12243 us | NVSS
// Read             |   1462 us |   2729 us | NVSS
//
// WRITE LittleFS is SLOWER: 10.0x
// READ LittleFS is SLOWER: 1.9x to 6.7x

//# Buffer size: 256B
// Operation        | NVSS      | LittleFS  | Winner
// -----------------|-----------|-----------|--------
// Write            |   1712 us |  12409 us | NVSS
// Read             |   1810 us |   3836 us | NVSS
//
// WRITE LittleFS is SLOWER: 8.7x
// READ LittleFS is SLOWER: 1.9x to 5.0x

//# Buffer size: 512B
// Operation        | NVSS      | LittleFS  | Winner
// -----------------|-----------|-----------|--------
// Write            |   2339 us |  13477 us | NVSS
// Read             |   2493 us |   4527 us | NVSS
//
// WRITE LittleFS is SLOWER: 6.5x
// READ LittleFS is SLOWER: 1.8x to 3.3x

//# Buffer size: 1024B
// Operation        | NVSS      | LittleFS  | Winner
// -----------------|-----------|-----------|--------
// Write            |   3774 us |  62289 us | NVSS
// Read             |   3963 us |   2904 us | LittleFS
//
// WRITE LittleFS is SLOWER: 13.7x
// READ LittleFS is FASTER: 0.8x

//# Buffer size: 2048B
// Operation        | NVSS      | LittleFS  | Winner
// -----------------|-----------|-----------|--------
// Write            |   6843 us |  62741 us | NVSS
// Read             |   7087 us |   3253 us | LittleFS
//
// WRITE LittleFS is 9.2x SLOWER
// READ LittleFS is 0.5x FASTER
