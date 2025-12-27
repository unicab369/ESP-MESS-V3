#include "http2.h"



// static const char *HTML_PAGE = 
// "<!DOCTYPE html>"
// "<html>"
// "<head>"
// "	<title>ESP32 Web Server</title>"
// "	<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
// "	<style>"
// "		body {"
// "			font-family: 'Arial', sans-serif;"
// "			text-align: center;"
// "			margin: 0;"
// "			padding: 20px;"
// "			background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);"
// "			min-height: 100vh;"
// "			color: white;"
// "		}"
// "		.container {"
// "			background: rgba(255, 255, 255, 0.1);"
// "			backdrop-filter: blur(10px);"
// "			border-radius: 20px;"
// "			padding: 40px;"
// "			margin: 50px auto;"
// "			max-width: 800px;"
// "			box-shadow: 0 8px 32px rgba(0, 0, 0, 0.1);"
// "			border: 1px solid rgba(255, 255, 255, 0.2);"
// "		}"
// "		h1 {"
// "			font-size: 3em;"
// "			margin-bottom: 10px;"
// "			text-shadow: 2px 2px 4px rgba(0, 0, 0, 0.3);"
// "		}"
// "		p {"
// "			font-size: 1.2em;"
// "			line-height: 1.6;"
// "			margin: 20px 0;"
// "		}"
// "		.info-box {"
// "			background: rgba(0, 0, 0, 0.2);"
// "			border-radius: 10px;"
// "			padding: 20px;"
// "			margin: 30px 0;"
// "			text-align: left;"
// "		}"
// "		.chip {"
// "			display: inline-block;"
// "			background: rgba(255, 255, 255, 0.2);"
// "			padding: 8px 16px;"
// "			border-radius: 50px;"
// "			margin: 5px;"
// "			font-weight: bold;"
// "		}"
// "		.ip-address {"
// "			font-family: monospace;"
// "			background: rgba(0, 0, 0, 0.3);"
// "			padding: 10px 20px;"
// "			border-radius: 10px;"
// "			display: inline-block;"
// "			margin-top: 20px;"
// "			font-size: 1.5em;"
// "			letter-spacing: 1px;"
// "		}"
// "		.footer {"
// "			margin-top: 40px;"
// "			font-size: 0.9em;"
// "			opacity: 0.8;"
// "		}"
// "	</style>"
// "</head>"
// "<body>"
// "	<div class=\"container\">"
// "		<h1>🚀 ESP32 Web Server</h1>"
// "		<p>Welcome to your ESP32 running an HTTP server!</p>"
// "		"
// "		<div class=\"info-box\">"
// "			<h3>✨ Features:</h3>"
// "			<div class=\"chip\">WiFi Enabled</div>"
// "			<div class=\"chip\">HTTP Server</div>"
// "			<div class=\"chip\">Static Web Page</div>"
// "			<div class=\"chip\">ESP-IDF</div>"
// "		</div>"
// "		"
// "		<p>This page is served directly from the ESP32's memory.</p>"
// "		"
// "		<div>"
// "			<h3>📍 Server IP Address:</h3>"
// "			<div class=\"ip-address\" id=\"ipAddress\">Connecting...</div>"
// "		</div>"
// "		"
// "		<div class=\"info-box\">"
// "			<h3>📊 System Info:</h3>"
// "			<p><strong>Free Heap:</strong> <span id=\"freeHeap\">...</span> bytes</p>"
// "			<p><strong>Uptime:</strong> <span id=\"uptime\">0</span> seconds</p>"
// "			<p><strong>Chip Model:</strong> ESP32</p>"
// "		</div>"
// "		"
// "		<div class=\"footer\">"
// "			<p>Refresh page to update system info</p>"
// "			<p>Server running on ESP-IDF HTTP Server</p>"
// "		</div>"
// "	</div>"
// "	"
// "	<script>"
// "		// Update uptime every second"
// "		let uptime = 0;"
// "		setInterval(() => {"
// "			uptime++;"
// "			document.getElementById('uptime').textContent = uptime;"
// "		}, 1000);"
// "		"
// "		// Fetch system info on page load"
// "		fetch('/info')"
// "			.then(response => response.json())"
// "			.then(data => {"
// "				document.getElementById('freeHeap').textContent = data.heap_free;"
// "				document.getElementById('ipAddress').textContent = data.ip_address;"
// "			});"
// "	</script>"
// "</body>"
// "</html>";

// // Handler for root URL "/" - serves HTML page
// static esp_err_t root_get_handler(httpd_req_t *req) {
// 	ESP_LOGI(TAG, "Serving HTML page to client");
	
// 	// Set content type to HTML
// 	httpd_resp_set_type(req, "text/html");
	
// 	// Send the HTML page
// 	httpd_resp_send(req, HTML_PAGE, strlen(HTML_PAGE));
	
// 	return ESP_OK;
// }

// // Handler for "/info" endpoint - returns system info as JSON
// static esp_err_t info_get_handler(httpd_req_t *req) {
// 	ESP_LOGI(TAG, "Serving system info");
	
// 	// Get IP address
// 	char ip_str[16] = "0.0.0.0";
// 	esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
// 	if (netif) {
// 		esp_netif_ip_info_t ip_info;
// 		if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
// 			esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
// 		}
// 	}
	
// 	// Create JSON response
// 	char json_response[256];
// 	snprintf(json_response, sizeof(json_response),
// 			"{\"heap_free\": %d, \"ip_address\": \"%s\"}",
// 			esp_get_free_heap_size(), ip_str);
	
// 	// Set content type to JSON
// 	httpd_resp_set_type(req, "application/json");
	
// 	// Send JSON response
// 	httpd_resp_send(req, json_response, strlen(json_response));
	
// 	return ESP_OK;
// }

// // Start HTTP server
// static httpd_handle_t start_webserver(void) {
// 	httpd_handle_t server = NULL;
// 	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	
// 	// Configure server
// 	config.stack_size = 4096;
// 	config.max_uri_handlers = 8;
	
// 	ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);
	
// 	// Start the HTTP server
// 	if (httpd_start(&server, &config) == ESP_OK) {
// 		// Register root handler
// 		httpd_uri_t root = {
// 			.uri = "/",
// 			.method = HTTP_GET,
// 			.handler = root_get_handler,
// 			.user_ctx = NULL
// 		};
// 		httpd_register_uri_handler(server, &root);
		
// 		// Register info handler
// 		httpd_uri_t info = {
// 			.uri = "/info",
// 			.method = HTTP_GET,
// 			.handler = info_get_handler,
// 			.user_ctx = NULL
// 		};
// 		httpd_register_uri_handler(server, &info);
		
// 		ESP_LOGI(TAG, "HTTP server started successfully");
// 	} else {
// 		ESP_LOGE(TAG, "Failed to start HTTP server!");
// 	}
	
// 	return server;
// }
