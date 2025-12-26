
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>


typedef enum __attribute__((packed)) {
    WEBSOCKET_DISCONNECTED = 0x00,
    WEBSOCKET_SETUP = 0x01,
    WEBSOCKET_ACCEPT_IMCOMING = 0x02,
    WEBSOCKET_HANDSHAKED = 0x03,
} web_socket_status_t;

void web_socket_setup(void);
void web_socket_poll(uint64_t current_time);
void send_cur_websocket_message(const void *message, size_t len);