#include <stdint.h>

typedef enum {
    TCP_STATUS_INITIATED = 0x01,
    TCP_STATUS_RETRY = 0x02,
    TCP_STATUS_SETUP = 0x04,
    TCP_STATUS_DISCONNECTED = 0x05,
    TCP_STATUS_SOCKET_FAILED = 0x06,
    TCP_STATUS_BINDING_FAILED = 0x07,
    TCP_STATUS_LISENNING_FAILED = 0x09,
    TCP_STATUS_FAILED = 0x08,
} tcp_status_t;

void tcp_server_socket_setup(uint64_t current_time);
void tcp_server_socket_task(uint64_t current_time);
void tcp_client_socket_task(uint64_t current_time);