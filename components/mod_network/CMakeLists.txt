idf_component_register(
	INCLUDE_DIRS "."

	REQUIRES
		esp_wifi
		esp-tls
		esp_eth
		esp_http_server
		esp_http_client
		wpa_supplicant
		driver
		esp_timer
)