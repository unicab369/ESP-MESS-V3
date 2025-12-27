#include "udp_socket.h"

#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_net_stack.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "regex.h"
#include "esp_nan.h"
#include "esp_log.h"
#include "esp_err.h"

#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"


#define PORT 3333

static const char *TAG = "UDP_STUFF";
static int server_sock;
static uint8_t retry_count = 5;

struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
socklen_t socklen = sizeof(source_addr);
struct sockaddr_in6 dest_addr;

udp_status_t current_status = UDP_STATUS_INITIATED;

void udp_close() {
    // Close the socket
    shutdown(server_sock, 0);
    close(server_sock);
}

udp_status_t udp_server_socket_setup(uint64_t current_time) {
    if (current_status == UDP_STATUS_SETUP) return current_status;

    int addr_family = AF_INET;
    int ip_protocol = 0;
    ESP_LOGE(TAG, "IM HERE zzzz 2222");

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(PORT);
        ip_protocol = IPPROTO_IP;
    } else if (addr_family == AF_INET6) {
        bzero(&dest_addr.sin6_addr.un, sizeof(dest_addr.sin6_addr.un));
        dest_addr.sin6_family = AF_INET6;
        dest_addr.sin6_port = htons(PORT);
        ip_protocol = IPPROTO_IPV6;

        #if defined(CONFIG_EXAMPLE_IPV4) && defined(CONFIG_EXAMPLE_IPV6)
            // Note that by default IPV6 binds to both protocols, it is must be disabled
            // if both protocols used at the same time (used in CI)
            int opt = 1;
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
        #endif
    }

    server_sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
    if (server_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        current_status = UDP_STATUS_SOCKET_FAILED;
        return current_status;
    }
    ESP_LOGI(TAG, "Socket created");

    #if defined(CONFIG_LWIP_NETBUF_RECVINFO) && !defined(CONFIG_EXAMPLE_IPV6)
        int enable = 1;
        lwip_setsockopt(sock, IPPROTO_IP, IP_PKTINFO, &enable, sizeof(enable));
    #endif

    #if defined(CONFIG_EXAMPLE_IPV4) && defined(CONFIG_EXAMPLE_IPV6)
        if (addr_family == AF_INET6) {
            // Note that by default IPV6 binds to both protocols, it is must be disabled
            // if both protocols used at the same time (used in CI)
            int opt = 1;
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
        }
    #endif

    //! Set the socket to non-blocking mode
    int flags = fcntl(server_sock, F_GETFL, 0);
    fcntl(server_sock, F_SETFL, flags | O_NONBLOCK);

    int err = bind(server_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        current_status = UDP_STATUS_BINDING_FAILED;
        return current_status;
    }

    ESP_LOGI(TAG, "Socket bound, port %d", PORT);
    current_status = UDP_STATUS_SETUP;
    return current_status;
}

static char rx_buffer[128];
static char addr_str[128];

void udp_server_socket_task() {
    if (current_status != UDP_STATUS_SETUP) return;

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(server_sock, &read_fds);

    struct timeval timeout;
    timeout.tv_sec = 0;  // 1-second timeout
    timeout.tv_usec = 1;

    int ret = select(server_sock + 1, &read_fds, NULL, NULL, &timeout);
    if (ret > 0) {
        if (FD_ISSET(server_sock, &read_fds)) {
            char rx_buffer[128];
            char addr_str[128];

            int len = recvfrom(server_sock, rx_buffer, sizeof(rx_buffer) - 1, 0,
                              (struct sockaddr *)&source_addr, &socklen);
            if (len > 0) {
                rx_buffer[len] = 0; // Null-terminate the received data

                // Get the sender's IP address as a string
                if (source_addr.ss_family == AF_INET) {
                    inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
                } else if (source_addr.ss_family == AF_INET6) {
                    inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
                }

                ESP_LOGI(TAG, "Received %d bytes from %s:", len, addr_str);
                ESP_LOGI(TAG, "%s", rx_buffer);

                // Send a response back to the client
                int err = sendto(server_sock, rx_buffer, len, 0,
                                 (struct sockaddr *)&source_addr, sizeof(source_addr));
                if (err < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                }
            }
        }
    } else if (ret == 0) {
        // Timeout: No data received
        // ESP_LOGI(TAG, "No data received within the timeout period");
    } else {
        ESP_LOGE(TAG, "select failed: errno %d", errno);
    }
}

#define MESSAGE_UDP "Hello, UDP!"
#define BROADCAST_IP "10.0.0.255" 

uint64_t last_sent_time = 0;

void udp_client_socket_send(uint64_t current_time) {
    if (current_status != UDP_STATUS_SETUP) return;
    
    // printf("last_sent_time: %"PRIu64", curr_time: %"PRIu64"\n", last_sent_time, current_time);
    if (current_time - last_sent_time < 1000000) return;
    last_sent_time = current_time;

    // Create UDP socket
    int client_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (client_sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
        return;
    }

    // Configure destination address
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, BROADCAST_IP, &dest_addr.sin_addr);

    // Send UDP message
    int err = sendto(client_sock, MESSAGE_UDP, strlen(MESSAGE_UDP), 0, 
                        (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Failed to send UDP message: errno %d", errno);
    } else {
        ESP_LOGI(TAG, "UDP message sent: %s", MESSAGE_UDP);
    }

    close(client_sock);
}