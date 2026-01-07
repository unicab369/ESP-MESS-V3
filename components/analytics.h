#include <stdatomic.h>
#include "esp_log.h"

static const char *TAG_ATOMIC = "#ATOM";
static const char *TAG_CYCLE = "#CYC";

typedef struct {
	atomic_uint current;
	atomic_uint peak;
	atomic_uint total;
} atomic_stats_t;

void atomic_tracker_start(atomic_stats_t *stats) {
	// Get new count after increment
	uint32_t new_count = atomic_fetch_add(&stats->current, 1) + 1;
	atomic_fetch_add(&stats->total, 1);
	
	// Update peak using compare_exchange
	uint32_t current_peak;
	do {
		current_peak = atomic_load(&stats->peak);
		if (new_count <= current_peak) break;
	} while (!atomic_compare_exchange_weak(&stats->peak, &current_peak, new_count));
}

void atomic_tracker_end(atomic_stats_t *stats) {
    uint32_t current = atomic_load(&stats->current);
    if (current > 0) {
        atomic_fetch_sub(&stats->current, 1);
    } else {
        ESP_LOGW(TAG_ATOMIC, "Attempt to end non-existent request!");
    }
}

void atomic_tracker_reset(atomic_stats_t *stats) {
	atomic_store(&stats->current, 0);
	atomic_store(&stats->peak, 0);
	atomic_store(&stats->total, 0);
}

void atomic_tracker_get(atomic_stats_t *stats, int *current, int *peak, int *total) {
	*current = atomic_load(&stats->current);
	*peak = atomic_load(&stats->peak);
	*total = atomic_load(&stats->total);
}

void atomic_tracker_print(atomic_stats_t *stats) {
	int current, peak, total;
	atomic_tracker_get(stats, &current, &peak, &total);
	ESP_LOGI(TAG_ATOMIC, "Active: %u, Total: %u, Peak: %u", current, total, peak);
}


#include "esp_timer.h"

typedef struct {
	uint64_t time_min_us;	// none zero minimum
	uint64_t time_max0_us;	// the first max
	uint64_t time_max1_us;	// the second max
	uint32_t count;
} cycle_t;

void cycle_tick(cycle_t *cycle, uint64_t timestamp_us) {
	uint64_t elapsed_us = esp_timer_get_time() - timestamp_us;
	cycle->count++;
	// exclude 0 on first cycle
	if (elapsed_us < cycle->time_min_us && elapsed_us > 0) {
		cycle->time_min_us = elapsed_us;
	}

	if (elapsed_us > cycle->time_max0_us) {
		cycle->time_max0_us = elapsed_us;
	}
	// exclude the max tracking on first cycle
	else if (elapsed_us > cycle->time_max1_us) {
		cycle->time_max1_us = elapsed_us;
	}
}

void cycle_reset(cycle_t *cycle) {
	cycle->time_min_us = 0xFFFFFFFFFFFFFFFF;
	cycle->time_max0_us = 0;
	cycle->time_max1_us = 0;
	cycle->count = 0;
}

void cycle_print(cycle_t *cycle) {
	ESP_LOGI(TAG_CYCLE, "%ld, min: %lldus, max: %lld:%lld us",
		cycle->count, cycle->time_min_us, cycle->time_max0_us, cycle->time_max1_us);
}