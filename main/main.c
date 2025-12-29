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

#define BLINK_GPIO 22
static const char *TAG = "[ESP-MESS]";


int random_int(int min, int max) {
	return min + rand() % (max - min + 1);
}

esp_err_t HTTP_SD_DATA_STREAM(httpd_req_t *req, const char* device, const char* dateStr) {
	FILE *_file = sd_log_file(device, dateStr);
	if (_file == NULL) {
		ESP_LOGE(TAG, "Failed to open file for reading");
        // Send error response
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Data file not found");
		return ESP_OK;
	}

	ESP_LOGW(TAG, "File opened successfully, starting stream...");
    // Set response headers
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");    

	// Stream file content in chunks
	char buffer[512];
	size_t bytes_read;
	size_t total_bytes = 0;

	while ((bytes_read = fread(buffer, 1, sizeof(buffer), _file)) > 0) {
        esp_err_t err = httpd_resp_send_chunk(req, buffer, bytes_read);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error sending chunk: %d", err);
            fclose(_file);
            return err;
        }

		// printf("send trunk %s\n", buffer);
		total_bytes += bytes_read;
	}
	fclose(_file);
	ESP_LOGW(TAG, "total Bytes: %d", total_bytes);
	
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
	gpio_reset_pin(BLINK_GPIO);
	gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

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
		sd_spi_config(spi_conf0.host, spi_conf0.cs);
		sd_test();
	}

	int counter = 0;

	while (1) {
		// ESP_LOGI(TAG, "Turning the LED %s!", s_led_state == true ? "ON" : "OFF");
		gpio_set_level(BLINK_GPIO, s_led_state);
		s_led_state = !s_led_state;

		struct tm timeinfo = timeinfo_now();
		if (timeinfo.tm_year > 70) {
			// year number starts at 1900, epoch year is 1970
			ESP_LOGI(TAG_WIFI, "Time: %s", GET_TIME_STR);

			char data[48];
			int temp = random_int(20, 30);			// Temperature: 20-30°C
			int humidity = random_int(40, 80);		// Humidity: 40-80%
			int light = random_int(0, 1000);		// Light: 0-1000 lux
			sprintf(data, "%lld,%d,%d,%d\n", (long long)time_now(), temp, humidity, light);

			char datestr[32];
			strftime(datestr, sizeof(datestr), "%Y%m%d", &timeinfo);
			// sd_log_data("aabbcc", datestr, data);
		}
				
		wifi_poll();
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
}
