#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdkconfig.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "main-services.h"

#define MAIN_TASK_INTERVAL_US 2000000	// 2 seconds
#define LED_PIN 22
#define MODE_PIN 12

static const char *TAG = "#APP";

cycle_t main_cycle;

int random_int(int min, int max) {
	return min + rand() % (max - min + 1);
}

void toggle_led() {
	static uint8_t s_led_state = 0;
	gpio_set_level(LED_PIN, s_led_state);
	s_led_state = !s_led_state;	
}

void SERV_RELOAD_LOGS() {
	esp_err_t ret = mod_nvs_open("s_log");
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Err reload_logs: %s", esp_err_to_name(ret));
		return;
	}

	uint8_t log_sd = 0, log_http = 0, log_app = 0;
	uint8_t log_diag1 = 0, log_diag2 = 0, log_diag3 = 0;
	uint8_t log_sf = 0;

	nvs_get_u8(NVS_HANDLER, "SD", &log_sd);
	nvs_get_u8(NVS_HANDLER, "HTTP", &log_http);
	nvs_get_u8(NVS_HANDLER, "APP", &log_app);
		nvs_get_u8(NVS_HANDLER, "PART", &log_diag1);
		nvs_get_u8(NVS_HANDLER, "SRAM", &log_diag2);
		nvs_get_u8(NVS_HANDLER, "TASKS", &log_diag3);
		nvs_get_u8(NVS_HANDLER, "SF", &log_sf);
	nvs_close(NVS_HANDLER);

	ESP_LOGW(TAG, "Update Logs");
	printf("SD:%d, HTTP:%d, APP:%d\nPART:%d, SRAM:%d, TASKS:%d, SF:%d\n",
		log_sd, log_http, log_app,
		log_diag1, log_diag2, log_diag3, log_sf
	);

	//# Set Logs level
	esp_log_level_set(TAG_SD, log_sd);
	esp_log_level_set(TAG_HTTP, log_http);
	esp_log_level_set(TAG, log_app);
	esp_log_level_set("#PART", log_diag1);
	esp_log_level_set("#SRAM", log_diag2);
	esp_log_level_set("#TASKS", log_diag3);
	esp_log_level_set("#SF", log_sf);
}

void log_diagnostics_handler() {
	char output[256] = {0};

	if (esp_log_level_get("#PART") > 1) {
		ESP_LOGW("#PART", "Partition Table");
		make_partition_tableStr(output);
		printf("%s", output);
	}
	if (esp_log_level_get("#SRAM") > 1) {
		ESP_LOGW("#SRAM", "SRAM Breakdown");
		memset(output, 0, sizeof(output));
		make_detailed_sramStr(output);
		printf("%s", output);
	}
	if (esp_log_level_get("#TASKS") > 1) {
		ESP_LOGW("#TASKS", "Task Watermarks");
		memset(output, 0, sizeof(output));
		make_tasks_watermarksStr(output);
		printf("%s", output);
	}
	if (esp_log_level_get("#SF") > 1) {
		ESP_LOGW("#SF", "Storage Diagnostics");
		memset(output, 0, sizeof(output));
		sd_card_info(output);
		printf("%s", output);

		memset(output, 0, sizeof(output));
		get_littlefs_space(output);
		printf("%s", output);
	}

	// int pos = make_partition_tableStr(buffer);
	// pos += make_partition_tableStr2(buffer + pos);
	// return pos;
}


void app_main(void) {
	esp_err_t ret;
	SD_MUTEX = xSemaphoreCreateMutex();
	ESP_LOGI(TAG, "APP START");
	
	//! nvs_flash required for WiFi, ESP-NOW, and other stuff.
	mod_nvs_setup();
	SERV_RELOAD_LOGS();
	littleFS_init();

	//# Setup Blinking
	gpio_reset_pin(LED_PIN);
	gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

	//# Setup Mode Pin
	gpio_reset_pin(MODE_PIN);
	gpio_set_direction(MODE_PIN, GPIO_MODE_INPUT);
	gpio_set_pull_mode(MODE_PIN, GPIO_PULLUP_ONLY);

	//# Setup Wifi
	wifi_init_sta(EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASSWORD);

	//# Init SPI1 peripherals
	M_Spi_Conf spi_conf0 = {
		.host = 1,
		.mosi = CONFIG_SPI_MOSI,
		.miso = CONFIG_SPI_MISO,
		.clk = CONFIG_SPI_CLK,
		.cs = -1,
	};

	ret = mod_spi_init(&spi_conf0, 20E6);

	if (ret == ESP_OK) {
		//! NOTE: for MMC D3 or CS needs to be pullup if not used otherwise it will go into SPI mode
		ret = sd_spi_config(spi_conf0.host, spi_conf0.cs);
		
		if (ret == ESP_OK) {
			// sd_test();
			
			//# Delete log folder
			int mode = gpio_get_level(MODE_PIN);
			if (mode == 0) {
				sd_remove_dir_recursive(SD_POINT"/log2");
			}

			if (!sd_ensure_dir(SD_POINT"/log")) {
				ESP_LOGE(TAG_SD, "Err create /log");
			}
		}
	}

	TickType_t last_timestamp_us = xTaskGetTickCount();
	// print to reset the counts
	cycle_reset(&main_cycle);

	while (1) {
		uint64_t now_us = esp_timer_get_time();

		//# Interval check
		if (now_us - last_timestamp_us >= MAIN_TASK_INTERVAL_US) {
			struct tm timeinfo = timeinfo_now();

			if (timeinfo.tm_year > 70) {
				// year number starts at 1900, epoch year is 1970
				ESP_LOGI_SD(TAG, "T%s", GET_TIME_STR);
				uint32_t uuid = 0xAABBCCDA;
				uint32_t now = (uint32_t)time_now();

				for (int i=0; i<10; i++) {
					record_t records = {
						.timestamp = now,  // Fixed timestamp
						// .value1 = 10,
						// .value2 = 20,
						// .value3 = 30
						.value1 = random_int(20, 30),
						.value2 = random_int(70, 80),
						.value3 = random_int(0, 100),
					};

					uuid += i;
					cache_device(uuid, now);

					// $Take mutex
					if (xSemaphoreTake(SD_MUTEX, pdMS_TO_TICKS(50)) == pdTRUE) {
						// sd_bin_record_all(uuid, now, &timeinfo, &records);
						rotate_timeLog_write(uuid, now, &timeinfo, &records);
						xSemaphoreGive(SD_MUTEX);  // $Release mutex
					} else {
						ESP_LOGW(TAG_SD, "SD card busy, skipping log write");
					}
				}
			}

			wifi_poll();
			toggle_led();
			log_diagnostics_handler();

			cycle_print(&main_cycle);
			cycle_reset(&main_cycle);
			atomic_tracker_print(&http_stats);
			atomic_tracker_reset(&http_stats);
			
			last_timestamp_us = now_us;
		}

		cycle_tick(&main_cycle, now_us);
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}