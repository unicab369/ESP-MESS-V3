/* Async Request Handlers HTTP Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "http.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <esp_wifi.h>

#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <sys/param.h>
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_timer.h"

#include "esp_tls_crypto.h"
#include <esp_http_server.h>

#include "esp_mac.h"
#include "lwip/inet.h"

#define ASYNC_WORKER_TASK_PRIORITY      5
#define ASYNC_WORKER_TASK_STACK_SIZE    2048
#define CONFIG_EXAMPLE_MAX_ASYNC_REQUESTS 2
#define CONFIG_EXAMPLE_CONNECT_WIFI true

static const char *TAG = "APP_HTTP";
static http_interface_t* interface;

// Async requests are queued here while they wait to
// be processed by the workers
static QueueHandle_t request_queue;

// Track the number of free workers at any given time
static SemaphoreHandle_t worker_ready_count;

// Each worker has its own thread
// static TaskHandle_t worker_handles[CONFIG_EXAMPLE_MAX_ASYNC_REQUESTS];

typedef esp_err_t (*httpd_req_handler_t)(httpd_req_t *req);

typedef struct {
    httpd_req_t* req;
    httpd_req_handler_t handler;
} httpd_async_req_t;

// queue an HTTP req to the worker queue
static esp_err_t queue_request(httpd_req_t *req, httpd_req_handler_t handler)
{
    // must create a copy of the request that we own
    httpd_req_t* copy = NULL;
    esp_err_t err = httpd_req_async_handler_begin(req, &copy);
    if (err != ESP_OK) {
        return err;
    }

    int ticks = 0;

    // counting semaphore: if success, we know 1 or
    // more asyncReqTaskWorkers are available.
    if (xSemaphoreTake(worker_ready_count, ticks) == false) {
        ESP_LOGE(TAG, "No workers are available");
        httpd_req_async_handler_complete(copy); // cleanup
        return ESP_FAIL;
    }

    httpd_async_req_t async_req = {
        .req = copy,
        .handler = handler,
    };
    
    // Since worker_ready_count > 0 the queue should already have space.
    // But lets wait up to 100ms just to be safe.
    if (xQueueSend(request_queue, &async_req, pdMS_TO_TICKS(100)) == false) {
        ESP_LOGE(TAG, "worker queue is full");
        httpd_req_async_handler_complete(copy); // cleanup
        return ESP_FAIL;
    }

    return ESP_OK;
}

/* handle long request (on async thread) */
static esp_err_t long_async(httpd_req_t *req)
{
    ESP_LOGI(TAG, "running: /long");

    // track the number of long requests
    static uint8_t req_count = 0;
    req_count++;

    // send a request count
    char s[100];
    snprintf(s, sizeof(s), "<div>req: %u</div>\n", req_count);
    httpd_resp_sendstr_chunk(req, s);

    // then every second, send a "tick"
    // for (int i = 0; i < 60; i++) {

    //     // This delay makes this a "long running task".
    //     // In a real application, this may be a long calculation,
    //     // or some IO dependent code for instance.
    //     vTaskDelay(pdMS_TO_TICKS(1000));

    //     // send a tick
    //     snprintf(s, sizeof(s), "<div>%u</div>\n", i);
    //     httpd_resp_sendstr_chunk(req, s);
    // }

    // send "complete"
    httpd_resp_sendstr_chunk(req, NULL);

    return ESP_OK;
}

// each worker thread loops forever, processing requests
static void worker_task(void *p)
{
    ESP_LOGI(TAG, "starting async req task worker");

    while (true) {

        // counting semaphore - this signals that a worker
        // is ready to accept work
        xSemaphoreGive(worker_ready_count);

        // wait for a request
        httpd_async_req_t async_req;
        if (xQueueReceive(request_queue, &async_req, portMAX_DELAY)) {

            ESP_LOGI(TAG, "invoking %s", async_req.req->uri);

            // call the handler
            async_req.handler(async_req.req);

            // Inform the server that it can purge the socket used for
            // this request, if needed.
            if (httpd_req_async_handler_complete(async_req.req) != ESP_OK) {
                ESP_LOGE(TAG, "failed to complete async req");
            }
        }
    }

    ESP_LOGW(TAG, "worker stopped");
    // vTaskDelete(NULL);
}

// start worker threads
static void start_workers(void)
{

    // counting semaphore keeps track of available workers
    worker_ready_count = xSemaphoreCreateCounting(
        CONFIG_EXAMPLE_MAX_ASYNC_REQUESTS,  // Max Count
        0); // Initial Count
    if (worker_ready_count == NULL) {
        ESP_LOGE(TAG, "Failed to create workers counting Semaphore");
        return;
    }

    // create queue
    request_queue = xQueueCreate(1, sizeof(httpd_async_req_t));
    if (request_queue == NULL){
        ESP_LOGE(TAG, "Failed to create request_queue");
        vSemaphoreDelete(worker_ready_count);
        return;
    }

    // // start worker tasks
    // for (int i = 0; i < CONFIG_EXAMPLE_MAX_ASYNC_REQUESTS; i++) {

    //     bool success = xTaskCreate(worker_task, "async_req_worker",
    //                                 ASYNC_WORKER_TASK_STACK_SIZE, // stack size
    //                                 (void *)0, // argument
    //                                 ASYNC_WORKER_TASK_PRIORITY, // priority
    //                                 &worker_handles[i]);

    //     if (!success) {
    //         ESP_LOGE(TAG, "Failed to start asyncReqWorker");
    //         continue;
    //     }
    // }
}

/* adds /long request to the request queue */
static esp_err_t long_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "uri: /long");

    // add to the async request queue
    if (queue_request(req, long_async) == ESP_OK) {
        return ESP_OK;
    } else {
        httpd_resp_set_status(req, "503 Busy");
        httpd_resp_sendstr(req, "<div> no workers available. server busy.</div>");
        return ESP_OK;
    }
}

/* A quick HTTP GET handler, which does not
   use any asynchronous features */
static esp_err_t quick_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "uri: /quick");
    char s[100];
    snprintf(s, sizeof(s), "random: %u\n", rand());
    httpd_resp_sendstr(req, s);
    return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "uri: /");
    const char* html = "<div><a href=\"/long\">long</a></div>"
        "<div><a href=\"/quick\">quick</a></div>";
    httpd_resp_sendstr(req, html);
    return ESP_OK;
}

// HTTP Error (404) Handler - Redirects all requests to the root page
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    // Set status
    httpd_resp_set_status(req, "302 Temporary Redirect");
    // Redirect to the "/" root directory
    httpd_resp_set_hdr(req, "Location", "/");
    // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
    httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "Redirecting to root");
    return ESP_OK;
}

// HTML for the captive portal page
static const char *CAPTIVE_PORTAL_HTML = "<html><body><h1>Welcome to the Captive Portal!</h1></body></html>";

// HTTP GET handler for the captive portal
static esp_err_t captive_portal_get_handler(httpd_req_t *req) {
    httpd_resp_send(req, CAPTIVE_PORTAL_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// HTTP GET handler for serving the file
static esp_err_t file_get_handler(httpd_req_t *req) {
    char chunk_buffer2[800];
    memset(chunk_buffer2, 0x41, 800);

    char chunk_buffer[255];
    char chunk_len = sizeof(chunk_buffer);

    uint64_t time_ref = esp_timer_get_time();
    size_t bytes_read;
    esp_err_t err = ESP_FAIL;

    err = interface->on_file_fopen_cb("/foo.txt");
    uint64_t bytes = 0;

    if (err == ESP_OK) {
        for (int i = 0; i<70; i++) {
            bytes += sizeof(chunk_buffer2);
            err = httpd_resp_send_chunk(req, chunk_buffer2, sizeof(chunk_buffer2));
        }
        

        // // Read and send the file in chunks
        // while ((bytes_read = interface->on_file_fread_cb(chunk_buffer, chunk_len)) > 0) {
        //     bytes += bytes_read;
        //     // printf(">>> bytes: %u\n", bytes_read);

        //     // Send the chunk
        //     err = httpd_resp_send_chunk(req, chunk_buffer2, sizeof(chunk_buffer2));
        //     if (err != ESP_OK) {
        //         httpd_resp_send_500(req);
        //         break;
        //     }
        // }
    }

    printf("total bytes: %llu\n", bytes);
    interface->on_file_fclose_cb();

    uint64_t time_diff = esp_timer_get_time() - time_ref;
    char str[255];
    snprintf(str, sizeof(str), "total bytes: %llu, request_time = %llu\n", bytes, time_diff);
    printf(str);

    snprintf(str, sizeof(str), "bytes: %llu, ms: %llu", bytes, time_diff);
    interface->on_display_print(str, 0);
    esp_err_t ret = httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}

static esp_err_t device_request_handler(httpd_req_t *req) {
    // Set CORS headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");

    // Handle OPTIONS request (preflight request)
    if (req->method == HTTP_OPTIONS) {
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    
    char buffer[100];
    int ret, remaining = req->content_len;

    // Read the request body
    if ((ret = httpd_req_recv(req, buffer, MIN(remaining, sizeof(buffer)))) <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }


    
    buffer[ret] = '\0';                             // Null-terminate the buffer
    // ESP_LOGI(TAG, "Received data: %s", buffer);     // Log the received data

    // Parse the string (e.g., "name=John&value=42")
    char *name = strstr(buffer, "name=");
    char *value = strstr(buffer, "value=");

    if (name && value) {
        name += 5; // Move past "name="
        value += 6; // Move past "value="

        // Extract name and value
        char *name_end = strchr(name, '&');
        if (name_end) *name_end = '\0'; // Terminate name string

        // ESP_LOGI(TAG, "Name: %s, Value: %s", name, value);
    }

    //! Example array of values
    // Create a JSON string manually, including the array of values
    char json_response[400]; // Adjust the size as needed


    size_t arr_size;
    uint16_t *arr_data;
    interface->on_request_data(&arr_data, &arr_size);

    // Start the JSON string
    snprintf(json_response, sizeof(json_response),
    "{\"status\":\"ok\",\"len\":%hu,\"data\":[", arr_size);

    for (uint16_t i = 0; i < arr_size; i++) {
        char temp[10]; // Temporary buffer for each value
        snprintf(temp, sizeof(temp), "%hu", arr_data[i]); // Format the value
        strcat(json_response, temp); // Append the value to the JSON string

        // Add a comma after each value (except the last one)
        if (i < arr_size - 1) {
            strcat(json_response, ",");
        }
    }

    // Close the JSON array and object
    strcat(json_response, "]}");

    // Send a response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    // It is advisable that httpd_config_t->max_open_sockets > MAX_ASYNC_REQUESTS
    // Why? This leaves at least one socket still available to handle
    // quick synchronous requests. Otherwise, all the sockets will
    // get taken by the long async handlers, and your server will no
    // longer be responsive.
    config.max_open_sockets = CONFIG_EXAMPLE_MAX_ASYNC_REQUESTS + 1;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGI(TAG, "Error starting server!");
        return NULL;
    }

    // Set URI handlers
    httpd_register_uri_handler(server, &(const httpd_uri_t) {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = captive_portal_get_handler,
        .user_ctx  = NULL
    });

    // httpd_register_uri_handler(server, &(const httpd_uri_t) {
    //     .uri       = "/",
    //     .method    = HTTP_GET,
    //     .handler   = index_handler,
    // });
    
    httpd_register_uri_handler(server, &(const httpd_uri_t) {
        .uri       = "/long",
        .method    = HTTP_GET,
        .handler   = long_handler,
    });

    httpd_register_uri_handler(server, &(const httpd_uri_t) {
        .uri       = "/quick",
        .method    = HTTP_GET,
        .handler   = quick_handler,
    });

    httpd_register_uri_handler(server, &(const httpd_uri_t) {
        .uri       = "/file/",
        .method    = HTTP_GET,
        .handler   = file_get_handler,
    });

    httpd_register_uri_handler(server, &(const httpd_uri_t) {
        .uri       = "/device",
        .method    = HTTP_POST,
        .handler   = device_request_handler,
    });

    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);

    return server;
}

static esp_err_t stop_webserver(httpd_handle_t server) {
    // Stop the httpd server
    return httpd_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data) {
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        if (stop_webserver(*server) == ESP_OK) {
            *server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data) {
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
}


static void dhcp_set_captiveportal_url(void) {
    // get the IP of the access point to redirect to
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);

    char ip_addr[16];
    inet_ntoa_r(ip_info.ip.addr, ip_addr, 16);
    ESP_LOGI(TAG, "Set up softAP with IP: %s", ip_addr);

    // turn the IP into a URI
    char* captiveportal_uri = (char*) malloc(32 * sizeof(char));
    assert(captiveportal_uri && "Failed to allocate captiveportal_uri");
    strcpy(captiveportal_uri, "http://");
    strcat(captiveportal_uri, ip_addr);

    // get a handle to configure DHCP with
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");

    // set the DHCP option 114
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(netif));
    ESP_ERROR_CHECK(esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, captiveportal_uri, strlen(captiveportal_uri)));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(netif));
}


void http_setup(http_interface_t* intf) {
    interface = intf;
    static httpd_handle_t server = NULL;

#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));
#endif // CONFIG_EXAMPLE_CONNECT_WIFI
#ifdef CONFIG_EXAMPLE_CONNECT_ETHERNET
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, &disconnect_handler, &server));
#endif // CONFIG_EXAMPLE_CONNECT_ETHERNET

    // start workers
    start_workers();

    dhcp_set_captiveportal_url();

    /* Start the server for the first time */
    server = start_webserver();
}

#include "esp_http_client.h"

// Event handler for HTTP client events
esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // Print the response data
                printf("%.*s", evt->data_len, (char *)evt->data);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}


// Function to perform an HTTP GET request
void http_get_request() {
    esp_http_client_config_t config = {
        .url = "http://httpbin.org/get",  // Example URL for testing
        .event_handler = http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Perform the GET request
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET Status =");
        // ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
        //         esp_http_client_get_status_code(client),
        //         esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    // Clean up
    esp_http_client_cleanup(client);
}

// Function to perform an HTTP POST request
void http_post_request() {
    esp_http_client_config_t config = {
        .url = "http://httpbin.org/post",  // Example URL for testing
        .event_handler = http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Set POST data
    const char *post_data = "field1=value1&field2=value2";
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    // Perform the POST request
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET Status =");
        // ESP_LOGI(TAG, "HTTP POST Status = %d, content_length = %d",
        //         esp_http_client_get_status_code(client),
        //         esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }

    // Clean up
    esp_http_client_cleanup(client);
}


// // HTTP GET handler to send file in chunks
// static esp_err_t file_get_handler(httpd_req_t *req) {
//     const char *file_path = "/sdcard/foo.txt";  // Path to the file
//     FILE *file = fopen(file_path, "r");
//     if (file == NULL) {
//         ESP_LOGE(TAG, "Failed to open file: %s", file_path);
//         httpd_resp_send_500(req);
//         return ESP_FAIL;
//     }

//     // Set the response content type
//     httpd_resp_set_type(req, "text/plain");

//     // Buffer to hold each chunk of data
//     char chunk_buffer[1024];  // 1 KB chunk size
//     size_t bytes_read;

//     // Read and send the file in chunks
//     while ((bytes_read = fread(chunk_buffer, 1, sizeof(chunk_buffer), file)) > 0) {
//         // Send the chunk
//         if (httpd_resp_send_chunk(req, chunk_buffer, bytes_read) != ESP_OK) {
//             ESP_LOGE(TAG, "Failed to send chunk");
//             fclose(file);
//             return ESP_FAIL;
//         }
//     }

//     // Close the file
//     fclose(file);

//     // End the response
//     httpd_resp_send_chunk(req, NULL, 0);
//     return ESP_OK;
// }
