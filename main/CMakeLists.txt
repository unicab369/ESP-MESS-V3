idf_component_register(
    SRCS "main.c" 
    # SRCS "test_network.c"
    
    INCLUDE_DIRS

    PRIV_REQUIRES
        driver
        nvs_flash
        spi_flash
        esp_wifi
        mod_spi
        mod_sd
        mod_network
)


littlefs_create_partition_image(storage ../flash_data FLASH_IN_PROJECT)