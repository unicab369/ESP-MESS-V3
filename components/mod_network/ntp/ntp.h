#include <stdint.h>

typedef enum __attribute__((packed)) {
    NTP_STATUS_INITIATED = 0x01,
    NTP_STATUS_SETUP = 0x04,
    NTP_STATUS_FAILED = 0x08,
} ntp_status_t;

void ntp_setup(void);
ntp_status_t ntp_task(uint64_t current_time);