#include <stdint.h>

typedef enum __attribute__((packed)) {
    UDP_STATUS_INITIATED = 0x01,
    UDP_STATUS_RETRY = 0x02,
    UDP_STATUS_SETUP = 0x04,
    UPD_STATUS_DISCONNECTED = 0x05,
    UDP_STATUS_SOCKET_FAILED = 0x06,
    UDP_STATUS_BINDING_FAILED = 0x07,
    UDP_STATUS_FAILED = 0x08,
} udp_status_t;

udp_status_t udp_server_socket_setup(uint64_t current_time);
void udp_server_socket_task(void);
void udp_client_socket_send(uint64_t current_time);