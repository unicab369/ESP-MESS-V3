idf_component_register(
	SRCS "../lib_sd_log/lib_sd_log.c"
	INCLUDE_DIRS "."
	REQUIRES
		fatfs
		sd_card
		esp_timer
		nvs_flash
)