#ifndef MOD_SPI_H
#define MOD_SPI_H

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"

#include "driver/spi_master.h"



typedef struct {
    spi_device_handle_t spi_handle;
    uint8_t host;
    int8_t mosi;
    int8_t miso;
    int8_t clk;
    int8_t cs;      //! the main cs pin

    //! dc and rst are not part of SPI interface
    //! They are used for some display modules
    int8_t dc;          // set to -1 when not use
    int8_t rst;         // set to -1 when not use
    esp_err_t err;
} M_Spi_Conf;

esp_err_t mod_spi_init(M_Spi_Conf *conf, int frequency);

esp_err_t mod_spi_cmd(uint8_t cmd, M_Spi_Conf *conf);
esp_err_t mod_spi_data(uint8_t *data, uint16_t len, M_Spi_Conf *conf);

esp_err_t mod_spi_write_command(uint8_t cmd, const uint8_t *data, size_t len, M_Spi_Conf *conf);

void mod_spi_setup_rst(int8_t rst_pin);
void mod_spi_setup_cs(int8_t pin);
void mod_spi_switch_cs(int8_t from_pin, int8_t to_pin);


#endif