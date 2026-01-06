#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include "esp_log.h"
#include "sdkconfig.h"
#include "driver/gpio.h"

#include "main-services.h"


#define LED_PIN 22
#define MODE_PIN 12

#define RECORD_SIZE = 10; // 4 + 2 + 2 + 2 = 10 bytes

static const char *TAG = "#APP";
static uint8_t s_led_state = 0;

int random_int(int min, int max) {
	return min + rand() % (max - min + 1);
}

void toggle_led() {
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

	nvs_get_u8(NVS_HANDLER, "SD", &log_sd);
	nvs_get_u8(NVS_HANDLER, "HTTP", &log_http);
	nvs_get_u8(NVS_HANDLER, "APP", &log_app);
	nvs_get_u8(NVS_HANDLER, "DIAG1", &log_diag1);
	nvs_get_u8(NVS_HANDLER, "DIAG2", &log_diag2);
	nvs_get_u8(NVS_HANDLER, "DIAG3", &log_diag3);
	nvs_close(NVS_HANDLER);

	ESP_LOGW(TAG, "Update Logs \nSD:%d, HTTP:%d, APP:%d \nDIAG1:%d, DIAG2:%d, DIAG3:%d",
		log_sd, log_http, log_app,
		log_diag1, log_diag2, log_diag3
	);

	//# Set Logs level
	esp_log_level_set(TAG_SD, log_sd);
	esp_log_level_set(TAG_HTTP, log_http);
	esp_log_level_set(TAG, log_app);
	esp_log_level_set("#DIAG1", log_diag1);
	esp_log_level_set("#DIAG2", log_diag2);
	esp_log_level_set("#DIAG3", log_diag3);
}

void filtered_log_handler() {
	char output[256] = {0};

	if (esp_log_level_get("#DIAG1") > 1) {
		ESP_LOGW("#DIAG1", "Partition Table");
		make_partition_tableStr(output);
		printf("%s", output);
	}
	if (esp_log_level_get("#DIAG2") > 1) {
		ESP_LOGW("#DIAG2", "SRAM Breakdown");
		make_detailed_sramStr(output);
		printf("%s", output);
	}
	if (esp_log_level_get("#DIAG3") > 1) {
		ESP_LOGW("#DIAG3", "Task Watermarks");
		make_tasks_watermarksStr(output);
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

	while (1) {
		toggle_led();

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

		filtered_log_handler();

		vTaskDelay(1000/portTICK_PERIOD_MS);
	}
}