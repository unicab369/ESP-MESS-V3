idf_component_register(
	SRCS
		"wifi/mod_wifi.c"
		"wifi2/mod_wifi2.c"
		"ntp/ntp.c"
		"udp_socket/udp_socket.c"
		"tcp_socket/tcp_socket.c"
		"web_socket/web_socket.c"
		"http/http.c"

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