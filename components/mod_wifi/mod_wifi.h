#ifndef WIFI_H
#define WIFI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"

typedef struct {
    uint8_t max_retries;
    void(*on_display_print)(const char *str, uint8_t line);
} app_wifi_interface_t;


void wifi_init_sta(const char* sta_ssid, const char* sta_passwd);
void wifi_setup(app_wifi_interface_t *config);
void wifi_softAp_begin(const char* ap_ssid, const char* ap_passwd, uint8_t channel);
void wifi_sta_begin(const char* sta_ssid, const char* sta_passwd);

void wifi_connect(void);
void wifi_scan(void);
wifi_event_t wifi_get_status(void);

void wifi_wps_begin(void);

#endif