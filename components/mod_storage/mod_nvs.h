#include <stddef.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"

#include "nvs_flash.h"
#include "nvs.h"

static nvs_handle_t NVS_HANDLER;
static const char *TAG_NVS = "[NVS]";

esp_err_t mod_nvs_setup(void) {
	esp_err_t ret = nvs_flash_init();
	if (ret != ESP_OK) {
		ESP_LOGE(TAG_NVS, "Err mod_nvs_setup: %s", esp_err_to_name(ret));
		return ret;
	}

	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	return ret;
}

esp_err_t mod_nvs_open(const char* namespace) {
	esp_err_t ret = nvs_open(namespace, NVS_READWRITE, &NVS_HANDLER);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG_NVS, "Err mod_nvs_openNamespace: %s", esp_err_to_name(ret));
		return ret;
	}

	return ESP_OK;
}

// List all keys in a namespace
void mod_nvs_listKeys(const char *namespace) {
	if (namespace == NULL) {
		printf("\n***Listing all namespaces and keys:\n");
	}
	nvs_iterator_t it = NULL;
	esp_err_t result = nvs_entry_find(NVS_DEFAULT_PART_NAME, namespace, NVS_TYPE_ANY, &it);

	while (result == ESP_OK) {
		nvs_entry_info_t info;
		nvs_entry_info(it, &info);
		result = nvs_entry_next(&it);

		// skip the phy namespace
		if (memcmp(info.namespace_name, "phy", 3) == 0) continue;
		// skip the nvs.net namespace
		if (memcmp(info.namespace_name, "nvs.net", 7) == 0) continue;

		printf("Namespace: '%s', Key: '%s', Type: %d\n", info.namespace_name, info.key, info.type);
	}

	nvs_release_iterator(it);
}

static int mod_nvs_listKeys_json(const char *namespace, char *buffer, size_t buffer_size) {
	nvs_iterator_t it = NULL;
	esp_err_t result = nvs_entry_find(NVS_DEFAULT_PART_NAME, namespace, NVS_TYPE_ANY, &it);

	int count = 0;
	char *ptr = buffer;
	*ptr++ = '[';

	while (result == ESP_OK) {
		nvs_entry_info_t info;
		nvs_entry_info(it, &info);
		result = nvs_entry_next(&it);

		// skip the phy namespace
		if (memcmp(info.namespace_name, "phy", 3) == 0) continue;
		// skip the nvs.net namespace
		if (memcmp(info.namespace_name, "nvs.net", 7) == 0) continue;

		if (count > 0) *ptr++ = ',';
		int written = snprintf(ptr, buffer_size - (ptr - buffer),
							"[\"%s\",\"%s\",%d]", info.namespace_name, info.key, info.type);
		if (written < 0 || written >= buffer_size - (ptr - buffer)) break; // Buffer full
		ptr += written;
		count++;

		// Safety check
		if (ptr - buffer >= buffer_size - 64) break;
		// printf("Namespace: '%s', Key: '%s', Type: %d\n", info.namespace_name, info.key, info.type);
	}

	nvs_release_iterator(it);
	*ptr++ = ']';
	*ptr = '\0';
	return ptr - buffer;
}