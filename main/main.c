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
        }

        // char output[32];
        // sprintf(output, "%d,3,4,5,6\n", counter++);
        // sd_write_data(MOUNT_POINT"/test.csv", output);
        
        wifi_poll();
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
}
