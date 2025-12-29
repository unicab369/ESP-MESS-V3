#include <sys/time.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"

#include "esp_netif_sntp.h"
#include "lwip/ip_addr.h"
#include "esp_sntp.h"

#include <time.h>
#include "esp_timer.h"

int EPOCH_TIME_2000 = 946702800;		// epoch time of 2000-01-01 00:00:00
static time_t base_time = 0;		  	// Set this to initial time
static int64_t base_uptime_us = 0;		// When base_time was set

// Initialize with any time you want
void time_init(time_t initial_time) {
	base_time = initial_time;
	base_uptime_us = esp_timer_get_time();
}

// Get current time (no NTP, no sync)
time_t time_now(void) {
	int64_t elapsed_us = esp_timer_get_time() - base_uptime_us;
	return base_time + (elapsed_us / 1000000LL);
}

struct tm timeinfo_now(void) {
	time_t now = time_now();
	struct tm timeinfo;
	localtime_r(&now, &timeinfo);
	return timeinfo;	
}

char* time_make_str(const char *format) {
	static char buf[64];
	struct tm timeinfo = timeinfo_now();
	strftime(buf, sizeof(buf), format, &timeinfo);  // Time only
	return buf;
}

#define GET_TIME_STR time_make_str("%H:%M:%S")
#define GET_DATE_TIME_STR time_make_str("%Y-%m-%d %H:%M:%S")
#define GET_SHORT_DATE_STR time_make_str("%Y%m%d")



void ntp_init(void) {
	// Simple config with one server
	esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
	esp_netif_sntp_init(&config);
	esp_netif_sntp_start();
}

// Wait up to 10 seconds for sync
bool ntp_wait_sync(void) {
	return esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) == ESP_OK;
}

void ntp_load_time(void) {
	// Set timezone once (EST)
	// setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
	setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0", 1);
	tzset();

	time_t ntp_now;
	time(&ntp_now);
	time_init(ntp_now);
}