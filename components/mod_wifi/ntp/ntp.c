
#include "ntp.h"

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"

#include "esp_netif_sntp.h"
#include "lwip/ip_addr.h"
#include "esp_sntp.h"

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 48
#endif

static const char *TAG = "NTP_SERVER";
static bool is_time_retrieved = false;

static time_t now;
static struct tm timeinfo;
static uint64_t last_request_time;
static uint8_t did_setup = 0;

void sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "Notification of a time synchronization event");
    time(&now);

    // Set timezone to Eastern Standard Time and print local time
    setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0", 1);
    tzset();
    localtime_r(&now, &timeinfo);

    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time in New York is: %s", strftime_buf);
}

void ntp_setup(void) {
    time(&now);
    localtime_r(&now, &timeinfo);

    #if LWIP_DHCP_GET_NTP_SRV
        /**
         * NTP server address could be acquired via DHCP,
         * see following menuconfig options:
         * 'LWIP_DHCP_GET_NTP_SRV' - enable STNP over DHCP
         * 'LWIP_SNTP_DEBUG' - enable debugging messages
         *
         * NOTE: This call should be made BEFORE esp acquires IP address from DHCP,
         * otherwise NTP option would be rejected by default.
         */
        ESP_LOGI(TAG, "Initializing SNTP");
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SNTP_TIME_SERVER);
        config.start = false;                       // start SNTP service explicitly (after connecting)
        config.server_from_dhcp = true;             // accept NTP offers from DHCP server, if any (need to enable *before* connecting)
        config.renew_servers_after_new_IP = true;   // let esp-netif update configured SNTP server(s) after receiving DHCP lease
        config.index_of_first_server = 1;           // updates from server num 1, leaving server 0 (from DHCP) intact

        // configure the event on which we renew servers
        #ifdef CONFIG_EXAMPLE_CONNECT_WIFI
            config.ip_event_to_renew = IP_EVENT_STA_GOT_IP;
        #else
            config.ip_event_to_renew = IP_EVENT_ETH_GOT_IP;
        #endif

        config.sync_cb = sync_notification_cb; // only if we need the notification function
        esp_netif_sntp_init(&config);
        ESP_LOGI(TAG, "Starting SNTP");
        esp_netif_sntp_start();

        #if LWIP_IPV6 && SNTP_MAX_SERVERS > 2
            /* This demonstrates using IPv6 address as an additional SNTP server
            * (statically assigned IPv6 address is also possible)
            */
            ip_addr_t ip6;
            if (ipaddr_aton("2a01:3f7::1", &ip6)) {    // ipv6 ntp source "ntp.netnod.se"
                esp_sntp_setserver(2, &ip6);
            }
        #endif  /* LWIP_IPV6 */

    #else
        ESP_LOGI(TAG, "Initializing and starting SNTP");
        #if CONFIG_LWIP_SNTP_MAX_SERVERS > 1
            /* This demonstrates configuring more than one server
            */
            esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2,
                                    ESP_SNTP_SERVER_LIST(CONFIG_SNTP_TIME_SERVER, "pool.ntp.org" ) );
        #else
            /*
            * This is the basic default config with one server and starting the service
            */
            esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        #endif

        #ifdef CONFIG_SNTP_TIME_SYNC_METHOD_SMOOTH
            config.smooth_sync = true;
        #endif

        config.sync_cb = sync_notification_cb;     // Note: This is only needed if we want
        esp_netif_sntp_init(&config);
    #endif

    ESP_LOGI(TAG, "List of configured NTP servers:");

    for (uint8_t i = 0; i < SNTP_MAX_SERVERS; ++i){
        char *servername = esp_sntp_getservername(i);

        if (servername) {
            ESP_LOGI(TAG, "server %d: %s", i, servername);
        } else {
            // we have either IPv4 or IPv6 address, let's print it
            char buff[INET6_ADDRSTRLEN];
            ip_addr_t const *ip = esp_sntp_getserver(i);

            if (ipaddr_ntoa_r(ip, buff, INET6_ADDRSTRLEN) != NULL)
                ESP_LOGI(TAG, "server %d: %s", i, buff);
        }
    }

    did_setup = 1;
}

static sntp_sync_status_t current_status = SNTP_SYNC_STATUS_RESET;

ntp_status_t ntp_task(uint64_t current_time) {
    if (did_setup) return current_status;
    if (current_status == SNTP_SYNC_STATUS_COMPLETED) return current_status;

    // Is time set? If not, tm_year will be (1970 - 1900).
    if (timeinfo.tm_year >= (2016 - 1900)) return current_status;
    ESP_LOGI(TAG, "Connecting to WiFi and getting time over NTP.");
    current_status = sntp_get_sync_status();

    if (current_status != SNTP_SYNC_STATUS_COMPLETED) {
        ntp_setup();
    }

    return current_status;
}
