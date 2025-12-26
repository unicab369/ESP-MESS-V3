#include "web_socket.h"
#include <mbedtls/sha1.h>
#include <mbedtls/base64.h>


#include "esp_log.h"
#include "esp_err.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"

#define PORT 3333
#define TAG "WEBSOCKET_SERVER"

#define MAX_CLIENTS 10
typedef struct {
    int socket;
    int8_t handshaked;
} socket_client_info_t;

static socket_client_info_t client_infos[MAX_CLIENTS] = {0};
static struct pollfd poll_arr[MAX_CLIENTS + 1];            // +1 for the server socket

static int fds_count;
static int server_sock = -1;
static uint64_t last_log_time;

void web_socket_server_cleanup(void) {
    memset(client_infos, 0, sizeof(client_infos));
    memset(poll_arr, 0, sizeof(poll_arr));
    fds_count = 1;                      // Start with only the server socket

    if (server_sock < 0) return;
    close(server_sock);
    server_sock = -1; // Reset to invalid descriptor
}

void web_socket_setup(void) {
    if (server_sock > 0) return;
    web_socket_server_cleanup();

    //! Create TCP socket
    server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (server_sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        return;
    }

    // Initialize the server socket in the pollfd array
    poll_arr[0].fd = server_sock;
    poll_arr[0].events = POLLIN;        // Monitor for incoming connections

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    //! Bind the socket to the port
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket");
        web_socket_server_cleanup();
        return;
    }

    //! Listen for incoming connections, max pending connection = 5
    if (listen(server_sock, 5) < 0) {
        ESP_LOGE(TAG, "Failed to listen on socket");
        web_socket_server_cleanup();
        return;
    }

    //! Set the socket to non-blocking mode
    int flags = fcntl(server_sock, F_GETFL, 0);
    fcntl(server_sock, F_SETFL, flags | O_NONBLOCK);
    ESP_LOGI(TAG, "WebSocket server started on port %d", PORT);
}

static void remove_client_socket(int client_sock, int client_index) {
    close(client_sock);
    client_infos[client_index].socket = -1;
    client_infos[client_index].handshaked = 0;
    printf("Client removed %d\n", client_sock);
}

static int handle_recv(int client_sock, int client_index, char *rx_buff, size_t buffer_len) {
    int len = recv(client_sock, rx_buff, buffer_len, 0);
    if (len == 0) {
        // Client disconnected
        ESP_LOGW(TAG, "Client disconnected: %d", client_sock);
        remove_client_socket(client_sock, client_index);
        return 0;

    } else if (len < 0) {
        ESP_LOGE(TAG, "Client recv failed: %d. err: %s", client_sock, strerror(errno));
        remove_client_socket(client_sock, client_index);
        return 0;
    }

    ESP_LOGI(TAG, "Received from client: %d. msg: %s", client_sock, rx_buff);
    rx_buff[len] = '\0'; // Null-terminate the received data
    return len;
}

static int handle_send(int client_sock, int client_index, char *tx_buffer, size_t buffer_len) {
    int result = send(client_sock, tx_buffer, buffer_len, 0);
    if (result < 0) {
        ESP_LOGE(TAG, "Client failed: %d. err: %s\n", client_sock, strerror(errno));
        remove_client_socket(client_sock, client_index);
    } else if (result == 0) {
        ESP_LOGI(TAG, "Client closed: %d", client_sock);
    }

    return result;
}

static uint8_t make_handshake_packet(char *rx_buff, char *tx_buff, size_t tx_len) {
    #define HANDSHAKE_MAGIC_NUMBER "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

    //! Check for handshaking request
    char *key_start = strstr(rx_buff, "Sec-WebSocket-Key: ");
    if (!key_start) return 0;

    ESP_LOGI(TAG, "Found key_start");
    key_start += 19; // Move past "Sec-WebSocket-Key: "

    char *key_end = strstr(key_start, "\r\n");
    if (!key_end) return 0;
    
    ESP_LOGI(TAG, "Found key_end");
    *key_end = 0; // Null-terminate the key

    //! Concatenate client key with WebSocket GUID
    char combined[256];
    snprintf(combined, sizeof(combined), "%s%s", key_start, HANDSHAKE_MAGIC_NUMBER);

    //! Compute SHA-1 hash
    unsigned char sha1[20];
    if (mbedtls_sha1((unsigned char *)combined, strlen(combined), sha1) != 0) {
        ESP_LOGE(TAG, "SHA-1 computation failed");
        return 0;
    }

    //! Base64 encode the hash
    size_t base64_len;
    char accept_key[64];
    if (mbedtls_base64_encode((unsigned char *)accept_key, sizeof(accept_key), &base64_len, sha1, 20) != 0) {
        ESP_LOGE(TAG, "Base64 encoding failed");
        return 0;
    }

    //! build websocket handshake response
    const char *ws_handshake_resp =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n";
    snprintf(tx_buff, tx_len, ws_handshake_resp, accept_key);

    return 1;
}

static void web_socket_server_poll(int client_sock) {
    //! Add the new client socket to the pollfd array
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_infos[i].socket > 0) continue;
        client_infos[i].socket = client_sock;
        
        //! recv: Receive data from the client. Make sure the buffer is big enough
        char rx_buff[500];
        int len = handle_recv(client_sock, i, rx_buff, sizeof(rx_buff) - 1);
        if (len < 1) continue;

        // Echo the data back to the client if already handshaked
        if (client_infos[i].handshaked) {
            handle_send(client_sock, i, rx_buff, len);
            break;
        }

        //! Perform handshake
        printf("Start Handshake: %d\n", client_sock);
        char tx_buff[256]; // Ensure buffer is large enough
        uint8_t check = make_handshake_packet(rx_buff, tx_buff, sizeof(tx_buff));
        
        if (check) {
            ESP_LOGI(TAG, "Handshake to client: %d. tx_buff:\n%s", client_sock, tx_buff);

            //! send: handshake response
            int result = handle_send(client_sock, i, tx_buff, strlen(tx_buff));
            if (result > 0) {
                ESP_LOGI(TAG, "Handshake sent successfully");
                client_infos[i].handshaked = 1;
            }
        } else {
            remove_client_socket(client_sock, i);
        }
        break;      //! REQUIRED: stop the loop
    }
}

static int cur_client_sock = -1;

// Send a WebSocket text frame to the client
void send_websocket_message(int client_sock, const void *message, size_t len) {
    uint8_t frame[len + 2]; // Frame buffer (header + payload)
    cur_client_sock = client_sock;

    // Construct the frame
    frame[0] = 0x81;                        // FIN bit set, opcode for text frame
    frame[1] = len;                         // Payload length
    memcpy(frame + 2, message, len);        // Copy the payload

    // Send the frame
    send(client_sock, frame, len + 2, 0);
}

void send_cur_websocket_message(const void *message, size_t len) {
    if (cur_client_sock < 0) return;
    send_websocket_message(cur_client_sock, message, len);
}

void web_socket_poll(uint64_t current_time) {
    //! Wait for activity on any socket
    int activity = poll(poll_arr, fds_count, 0); // -1 means no timeout
    if (activity < 0) return;

    if (poll_arr[0].revents & POLLIN) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(poll_arr[0].fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_sock < 0) {
            ESP_LOGE(TAG, "Failed connection: %s\n", strerror(errno));
            return;

        } else {
            if (fds_count >= MAX_CLIENTS + 1) {
                printf("Max clients reached, closing connection\n");
                close(client_sock);
                return;
            }

            bool found_match = false;
            for (int i = 1; i <= fds_count; i++) {
                ESP_LOGW(TAG, "compare fd: %d, client: %d, index: %d", poll_arr[i].fd, client_sock, i);
                if (poll_arr[i].fd != client_sock) continue;
                ESP_LOGW(TAG, "found match");
                found_match = true;
                break;
            }
        
            if (!found_match) {
                struct pollfd *poll_ref = &poll_arr[fds_count];
                poll_arr[fds_count].fd = client_sock;
                poll_arr[fds_count].events = POLLIN; // Monitor for incoming data
                ESP_LOGW(TAG, "newly added: %d. at: %d", poll_arr[fds_count].fd, fds_count);
                fds_count++;
            }

            ESP_LOGW(TAG, "fds_counts: %d", fds_count);
            web_socket_server_poll(client_sock);
        }
    }

    //! Check for activity on client sockets
    for (int i = 1; i < fds_count; i++) {
        struct pollfd target = poll_arr[i];
        int target_client = target.fd;

        if (target.revents & POLLERR) {
            //! Remove the client socket with an error
            printf("Client err: %d\n", target_client);
            remove_client_socket(target_client, i-1);
            poll_arr[i].fd = -1; // Mark as invalid in the pollfd array
        }

        if (target.revents & POLLIN) {
            //! Handle client data
            uint8_t frame[128];
            // uint8_t check 
            int len = recv(target_client, frame, sizeof(frame), 0);

            if (len > 0) {
                // Extract the payload length
                uint8_t payload_len = frame[1] & 0x7F; // Mask out the MASK bit
                uint8_t mask_offset = 2; // Start of masking key (if present)
        
                // Check if the payload is masked
                if (frame[1] & 0x80) { // MASK bit is set
                    // Extract the masking key
                    uint8_t masking_key[4];
                    memcpy(masking_key, frame + 2, 4);
                    mask_offset += 4; // Move past the masking key
        
                    // Unmask the payload
                    uint8_t payload[payload_len];
                    for (size_t i = 0; i < payload_len; i++) {
                        payload[i] = frame[mask_offset + i] ^ masking_key[i % 4];
                    }
        
                    // Log the unmasked payload
                    ESP_LOGI(TAG, "Received: %.*s", payload_len, payload);
                } else {
                    // Payload is not masked
                    ESP_LOGI(TAG, "Received: %.*s", payload_len, frame + mask_offset);
                }
        
                // Send a response to the client
                const char *message = "Hello from ESP32!";
                size_t len = strlen(message);
                send_websocket_message(target_client, message, len);
                
            } else if (len == 0) {
                ESP_LOGI(TAG, "Client disconnected");
            }
        }
    }
}
