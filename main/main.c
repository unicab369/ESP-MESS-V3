#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "driver/gpio.h"

#include "mod_spi.h"
#include "mod_sd.h"

#include "mod_wifi.h"
#include "WIFI_CRED.h"

#define LED_PIN 22
#define MODE_PIN 12
static const char *TAG = "[ESP-MESS]";


int random_int(int min, int max) {
	return min + rand() % (max - min + 1);
}

esp_err_t HTTP_SD_DATA_STREAM(httpd_req_t *req, const char* device, 
	const char *year, const char *month, const char* day
) {
	char path[64];
	if (strlen(month) > 0 && strlen(day) > 0) {
		snprintf(path, sizeof(path), MOUNT_POINT"/log/%s/%s/%s%s.bin", device, year, month, day);
	} else {
		snprintf(path, sizeof(path), MOUNT_POINT"/log/%s/%s/latest.bin", device, year);
	}

	FILE* _file = fopen(path, "rb");
	if (_file == NULL) {
		ESP_LOGE(TAG, "Failed to open file for reading");
        // Send error response
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Data file not found");
		return ESP_OK;
	}

    // Set response headers
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");    

	// Stream file content in chunks
	char buffer[1024];
	size_t bytes_read;
	size_t total_bytes = 0;

	while ((bytes_read = fread(buffer, 1, sizeof(buffer), _file)) > 0) {
        esp_err_t err = httpd_resp_send_chunk(req, buffer, bytes_read);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error sending chunk: %d", err);
            fclose(_file);
            return err;
        }
		total_bytes += bytes_read;
	}
	fclose(_file);
	ESP_LOGW(TAG, "path %s: %d Bytes", path, total_bytes);
	
	// End chunked response
	httpd_resp_send_chunk(req, NULL, 0);

	return ESP_OK;
}


void app_main(void) {
	//! nvs_flash required for WiFi, ESP-NOW, and other stuff.
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);
	ESP_LOGI(TAG, "APP START");

	//# Setup Blinking
	static uint8_t s_led_state = 0;
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
	uint8_t uuid[4] = {0xAA, 0xBB, 0xCC, 0xDD};

	if (ret == ESP_OK) {
		//! NOTE: for MMC D3 or CS needs to be pullup if not used otherwise it will go into SPI mode
		sd_spi_config(spi_conf0.host, spi_conf0.cs);
		// sd_test();
		
		//# Delete log folder
		int mode = gpio_get_level(MODE_PIN);
		if (mode == 0) {
			sd_remove_dir_recursive(MOUNT_POINT"/log");
		}

		if (!sd_ensure_dir(MOUNT_POINT"/log")) {
			ESP_LOGE(TAG_SD, "Failed to create /log");
		}

		memcpy(ref[0].uuid, uuid, sizeof(uuid));
		// write_file_test();
	}

	while (1) {
		// ESP_LOGI(TAG, "Turning the LED %s!", s_led_state == true ? "ON" : "OFF");
		gpio_set_level(LED_PIN, s_led_state);
		s_led_state = !s_led_state;

		struct tm timeinfo = timeinfo_now();
		if (timeinfo.tm_year > 70) {
			// year number starts at 1900, epoch year is 1970
			ESP_LOGI(TAG_WIFI, "Time: %s", GET_TIME_STR);

			record_t records = {
				.timestamp = (u32_t)time_now(),  // Fixed timestamp
				// .value1 = 10,
				// .value2 = 20,
				// .value3 = 30
				.value1 = random_int(20, 30),
				.value2 = random_int(70, 80),
				.value3 = random_int(0, 100),
			};

			sd_bin_record_all(uuid, &timeinfo, &records);
		}

		wifi_poll();
		vTaskDelay(1000/portTICK_PERIOD_MS);
	}
}

void write_file_test() {
	int counter = 0;
	uint8_t uuid[4] = {0xAA, 0xBB, 0xCC, 0xDD};
	memcpy(ref[0].uuid, uuid, 6);
	ref[0].timeRef = 0;

	struct tm timeinfo1 = {
		.tm_year = 2025-1900,
		.tm_mon = 12-1,
		.tm_mday = 30,
		.tm_hour = 0,
		.tm_min = 0,
		.tm_sec = 0,
		.tm_isdst = -1,
	};

	record_t record1 = {
		.timestamp = 1767122783,  // Fixed timestamp
		.value1 = 10,
		.value2 = 20,
		.value3 = 30
	};

	while(1) {
		counter++;
		record1.timestamp += counter;
		sd_bin_record_all(uuid, &timeinfo1, &record1);
		vTaskDelay(1000/portTICK_PERIOD_MS);
	}
}