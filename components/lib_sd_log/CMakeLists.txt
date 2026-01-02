idf_component_register(
	SRCS
	INCLUDE_DIRS "."
	REQUIRES
		fatfs
		sd_card
		esp_timer
	PRIV_REQUIRES esp_timer
)