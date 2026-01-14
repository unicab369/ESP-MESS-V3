#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_wifi.h"
#include <esp_http_server.h>

static const char *TAG_HTTP = "#HTTP";

static const char *HTML_PAGE = 
"<!DOCTYPE html>"
"<html>"
"<head>"
"	<title>ESP32 Web Server</title>"
"	<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
"	<style>"
"		body {"
"			font-family: 'Arial', sans-serif;"
"			text-align: center;"
"			margin: 0;"
"			padding: 20px;"
"			background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);"
"			min-height: 100vh;"
"			color: white;"
"		}"
"		.container {"
"			background: rgba(255, 255, 255, 0.1);"
"			backdrop-filter: blur(10px);"
"			border-radius: 20px;"
"			padding: 40px;"
"			margin: 50px auto;"
"			max-width: 800px;"
"			box-shadow: 0 8px 32px rgba(0, 0, 0, 0.1);"
"			border: 1px solid rgba(255, 255, 255, 0.2);"
"		}"
"		h1 {"
"			font-size: 3em;"
"			margin-bottom: 10px;"
"			text-shadow: 2px 2px 4px rgba(0, 0, 0, 0.3);"
"		}"
"		p {"
"			font-size: 1.2em;"
"			line-height: 1.6;"
"			margin: 20px 0;"
"		}"
"		.info-box {"
"			background: rgba(0, 0, 0, 0.2);"
"			border-radius: 10px;"
"			padding: 20px;"
"			margin: 30px 0;"
"			text-align: left;"
"		}"
"		.chip {"
"			display: inline-block;"
"			background: rgba(255, 255, 255, 0.2);"
"			padding: 8px 16px;"
"			border-radius: 50px;"
"			margin: 5px;"
"			font-weight: bold;"
"		}"
"		.ip-address {"
"			font-family: monospace;"
"			background: rgba(0, 0, 0, 0.3);"
"			padding: 10px 20px;"
"			border-radius: 10px;"
"			display: inline-block;"
"			margin-top: 20px;"
"			font-size: 1.5em;"
"			letter-spacing: 1px;"
"		}"
"		.footer {"
"			margin-top: 40px;"
"			font-size: 0.9em;"
"			opacity: 0.8;"
"		}"
"	</style>"
"</head>"
"<body>"
"	<div class=\"container\">"
"		<h1>🚀 ESP32 Web Server</h1>"
"		<p>Welcome to your ESP32 running an HTTP server!</p>"
"		"
"		<div class=\"info-box\">"
"			<h3>✨ Features:</h3>"
"			<div class=\"chip\">WiFi Enabled</div>"
"			<div class=\"chip\">HTTP Server</div>"
"			<div class=\"chip\">Static Web Page</div>"
"			<div class=\"chip\">ESP-IDF</div>"
"		</div>"
"		"
"		<p>This page is served directly from the ESP32's memory.</p>"
"		"
"		<div>"
"			<h3>📍 Server IP Address:</h3>"
"			<div class=\"ip-address\" id=\"ipAddress\">Connecting...</div>"
"		</div>"
"		"
"		<div class=\"info-box\">"
"			<h3>📊 System Info:</h3>"
"			<p><strong>Free Heap:</strong> <span id=\"freeHeap\">...</span> bytes</p>"
"			<p><strong>Uptime:</strong> <span id=\"uptime\">0</span> seconds</p>"
"			<p><strong>Chip Model:</strong> ESP32</p>"
"		</div>"
"		"
"		<div class=\"footer\">"
"			<p>Refresh page to update system info</p>"
"			<p>Server running on ESP-IDF HTTP Server</p>"
"		</div>"
"	</div>"
"	"
"	<script>"
"		// Update uptime every second"
"		let uptime = 0;"
"		setInterval(() => {"
"			uptime++;"
"			document.getElementById('uptime').textContent = uptime;"
"		}, 1000);"
"		"
"		// Fetch system info on page load"
"		fetch('/info')"
"			.then(response => response.json())"
"			.then(data => {"
"				document.getElementById('freeHeap').textContent = data.heap_free;"
"				document.getElementById('ipAddress').textContent = data.ip_address;"
"			});"
"	</script>"
"</body>"
"</html>";

static esp_err_t options_handler(httpd_req_t *req) {
	ESP_LOGW(TAG_HTTP, "Sending OPTIONS response for CORS preflight");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

	httpd_resp_send(req, NULL, 0);  // 200, empty body
	return ESP_OK;
}

// Handler for root URL "/" - serves HTML page
static esp_err_t root_get_handler(httpd_req_t *req) {
	ESP_LOGI(TAG_HTTP, "Serving HTML page to client");
	
	// Set content type to HTML & Send the HTML page
	httpd_resp_set_type(req, "text/html");
	httpd_resp_send(req, HTML_PAGE, strlen(HTML_PAGE));
	return ESP_OK;
}

static esp_err_t info_get_handler(httpd_req_t *req) {
	// Get IP address
	char ip_str[16] = "0.0.0.0";
	esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
	if (netif) {
		esp_netif_ip_info_t ip_info;
		if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
			esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
		}
	}

	// Create JSON response
	char buf[256];
	int len = snprintf(buf, sizeof(buf),
			"{\"heap_free\": %lu, \"ip_address\": \"%s\"}",
			esp_get_free_heap_size(), ip_str);

	httpd_resp_set_type(req, "application/json");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");

	httpd_resp_send(req, buf, len);
	return ESP_OK;
}

static esp_err_t get_data_handler(httpd_req_t *req) {
	char query[128];

	size_t qlen = httpd_req_get_url_query_len(req) + 1;
	if (qlen > sizeof(query)) qlen = sizeof(query);

	if (httpd_req_get_url_query_str(req, query, qlen) == ESP_OK) {
		char arg_value[32] = {0};

		if (httpd_query_key_value(query, "device", arg_value, sizeof(arg_value)) == ESP_OK) {
			printf("Device: %s\n", arg_value);
		}

		if (httpd_query_key_value(query, "date", arg_value, sizeof(arg_value)) == ESP_OK) {
			printf("Date: %s\n", arg_value);
		}
	}

	char buf[256];
	int len = snprintf(buf, sizeof(buf), "[[0,%.2f],[1000,%.2f],[2000,%.2f]]", 1.23f, 1.45f, 1.40f);

	httpd_resp_set_type(req, "application/json");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
	httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
	httpd_resp_send(req, buf, len);

	return ESP_OK;
}

esp_err_t HTTP_GET_RECORDS_HANDLER(httpd_req_t *req);
esp_err_t HTTP_GET_CONFIG_HANDLER(httpd_req_t *req);
esp_err_t HTTP_SAVE_CONFIG_HANDLER(httpd_req_t *req);
esp_err_t HTTP_SCAN_HANDLER(httpd_req_t *req);
esp_err_t HTTP_GET_LOG_HANDLER(httpd_req_t *req);

esp_err_t HTTP_GET_ENTRIES_HANDLER(httpd_req_t *req);
esp_err_t HTTP_UPDATE_ENTRY_HANDLER(httpd_req_t *req);
esp_err_t HTTP_UPDATE_NVS_HANDLER(httpd_req_t *req);
esp_err_t HTTP_GET_FILE_HANDLER(httpd_req_t *req);
esp_err_t HTTP_UPDATE_FILE_HANDLER(httpd_req_t *req);

// Start HTTP server
static httpd_handle_t start_webserver(void) {
	httpd_handle_t server = NULL;
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.max_open_sockets = 7;
	config.uri_match_fn = httpd_uri_match_wildcard;

	// Configure server
	config.stack_size = 4096;
	config.max_uri_handlers = 20;
	ESP_LOGI(TAG_HTTP, "START-HTTP-SERVER port %d", config.server_port);

	// Start the HTTP server
	if (httpd_start(&server, &config) == ESP_OK) {
		// Register root handler
		httpd_uri_t root = {
			.uri = "/",
			.method = HTTP_GET,
			.handler = root_get_handler,
			.user_ctx = NULL
		};
		httpd_register_uri_handler(server, &root);

		httpd_uri_t options_uri = {
			.uri	  = "/*",
			.method   = HTTP_OPTIONS,
			.handler  = options_handler,
			.user_ctx = NULL,
		};
		httpd_register_uri_handler(server, &options_uri);

		httpd_uri_t info_uri = {
			.uri	  = "/info",
			.method   = HTTP_GET,
			.handler  = info_get_handler,
			.user_ctx = NULL,
		};
		httpd_register_uri_handler(server, &info_uri);

		httpd_uri_t config_uri = {
			.uri	  = "/g_config",
			.method   = HTTP_GET,
			.handler  = HTTP_GET_CONFIG_HANDLER,
			.user_ctx = NULL,
		};
		httpd_register_uri_handler(server, &config_uri);

		httpd_uri_t save_config_uri = {
			.uri	  = "/s_config",
			.method   = HTTP_GET,
			.handler  = HTTP_SAVE_CONFIG_HANDLER,
			.user_ctx = NULL,
		};
		httpd_register_uri_handler(server, &save_config_uri);

		httpd_uri_t get_records_uri = {
			.uri	  = "/g_rec",
			.method   = HTTP_GET,
			.handler  = HTTP_GET_RECORDS_HANDLER,
			.user_ctx = NULL,
		};
		httpd_register_uri_handler(server, &get_records_uri);

		httpd_uri_t scan_uri = {
			.uri	  = "/scan",
			.method   = HTTP_GET,
			.handler  = HTTP_SCAN_HANDLER,
			.user_ctx = NULL,
		};
		httpd_register_uri_handler(server, &scan_uri);

		httpd_uri_t get_log_uri = {
			.uri	  = "/g_log",
			.method   = HTTP_GET,
			.handler  = HTTP_GET_LOG_HANDLER,
			.user_ctx = NULL,
		};
		httpd_register_uri_handler(server, &get_log_uri);

		httpd_uri_t update_nvs_uri = {
			.uri	  = "/u_nvs",
			.method   = HTTP_GET,
			.handler  = HTTP_UPDATE_NVS_HANDLER,
			.user_ctx = NULL,
		};
		httpd_register_uri_handler(server, &update_nvs_uri);

		httpd_uri_t get_entries_uri = {
			.uri	  = "/g_entry",
			.method   = HTTP_GET,
			.handler  = HTTP_GET_ENTRIES_HANDLER,
			.user_ctx = NULL,
		};
		httpd_register_uri_handler(server, &get_entries_uri);

		httpd_uri_t update_entry_uri = {
			.uri	  = "/u_entry",
			.method   = HTTP_GET,
			.handler  = HTTP_UPDATE_ENTRY_HANDLER,
			.user_ctx = NULL,
		};
		httpd_register_uri_handler(server, &update_entry_uri);

		httpd_uri_t get_file_uri = {
			.uri	  = "/g_file",
			.method   = HTTP_GET,
			.handler  = HTTP_GET_FILE_HANDLER,
			.user_ctx = NULL,
		};
		httpd_register_uri_handler(server, &get_file_uri);

		httpd_uri_t update_file_uri = {
			.uri	  = "/u_file",
			.method   = HTTP_GET,
			.handler  = HTTP_UPDATE_FILE_HANDLER,
			.user_ctx = NULL,
		};
		httpd_register_uri_handler(server, &update_file_uri);

		ESP_LOGI(TAG_HTTP, "HTTP-SERVER started");
	} else {
		ESP_LOGE(TAG_HTTP, "HTTP-SERVER failed");
	}

	return server;
}



static void url_decode_inplace(char *str) {
    char *src = str;
    char *dst = str;

    while (*src) {
        if (src[0] == '%' && src[1] && src[2]) {
            // Decode %XX hex to char
            char c1 = src[1];
            char c2 = src[2];

            uint8_t val = 0;
            if (c1 >= '0' && c1 <= '9') val = (c1 - '0') << 4;
            else if (c1 >= 'A' && c1 <= 'F') val = (c1 - 'A' + 10) << 4;
            else if (c1 >= 'a' && c1 <= 'f') val = (c1 - 'a' + 10) << 4;

            if (c2 >= '0' && c2 <= '9') val |= (c2 - '0');
            else if (c2 >= 'A' && c2 <= 'F') val |= (c2 - 'A' + 10);
            else if (c2 >= 'a' && c2 <= 'f') val |= (c2 - 'a' + 10);

            *dst++ = (char)val;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

// Decode new line only - %0A and %0D
static void url_decode_newline(char *str) {
    char *d = str;

    while (*str) {
        if (str[0] == '%' && str[1] == '0' && (str[2] == 'A' || str[2] == 'a')) {
            *d++ = '\n'; str += 3;
        } else if (str[0] == '%' && str[1] == '0' && (str[2] == 'D' || str[2] == 'd')) {
            *d++ = '\r'; str += 3;
        } else if (*str == '+') {
            *d++ = ' '; str++;
        } else {
            *d++ = *str++;
        }
    }
    *d = '\0';
}