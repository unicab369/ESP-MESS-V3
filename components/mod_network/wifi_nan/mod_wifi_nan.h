#ifndef WIFI_NAN_H
#define WIFI_NAN_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// NAN (Neighbor Awareness Networking)
// Function prototypesw
void wifi_nan_publish(void);
void wifi_nan_subscribe(void);
void wifi_nan_sendData(uint64_t current_time);
bool wifi_nan_checkPeers(uint64_t current_time);
void wifi_nan_sendData2(uint64_t current_time);

#endif