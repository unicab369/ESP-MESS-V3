#include "mod_nvs.h"


static const char *TAG = "MOD_NVS";

static char sel_namespace[32] = "storage";
static nvs_handle_t sel_handle;

static const size_t TYPE_STR_PAIR_SIZE = sizeof(type_str_pair) / sizeof(type_str_pair[0]);

static nvs_type_t str_to_type(const char *type)
{
    for (int i = 0; i < TYPE_STR_PAIR_SIZE; i++) {
        const type_str_pair_t *p = &type_str_pair[i];
        if (strcmp(type, p->str) == 0) {
            return  p->type;
        }
    }

    return NVS_TYPE_ANY;
}


void mod_nvs_setup(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

int mod_nvs_set_namespace(const char* target)
{
    strlcpy(sel_namespace, target, sizeof(sel_namespace));
    ESP_LOGI(TAG, "Namespace set to '%s'", sel_namespace);
    return 0;
}


static esp_err_t open_nvs() {
    esp_err_t ret = nvs_open(sel_namespace, NVS_READWRITE, &sel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t mod_nvs_set_value(const char *key, const void* value, nvs_type_t type) {
    esp_err_t ret = open_nvs();
    if (ret != ESP_OK) return ret;

    switch (type) {
        case NVS_TYPE_I8:
            ret = nvs_set_i8(sel_handle, key, *(int8_t*)value);
            break;
        case NVS_TYPE_U8:
            ret = nvs_set_u8(sel_handle, key, *(uint8_t*)value);
            break;
        case NVS_TYPE_I16:
            ret = nvs_set_i16(sel_handle, key, *(int16_t*)value);
            break;
        case NVS_TYPE_U16:
            ret = nvs_set_u16(sel_handle, key, *(uint16_t*)value);
            break;
        case NVS_TYPE_I32:
            ret = nvs_set_i32(sel_handle, key, *(int32_t*)value);
            break;
        case NVS_TYPE_U32:
            ret = nvs_set_u32(sel_handle, key, *(uint32_t*)value);
            break;
        case NVS_TYPE_I64:
            ret = nvs_set_i64(sel_handle, key, *(int64_t*)value);
            break;
        case NVS_TYPE_U64:
            ret = nvs_set_u64(sel_handle, key, *(uint64_t*)value);
            break;
        case NVS_TYPE_STR:
            ret = nvs_set_str(sel_handle, key, (const char*)value);
            break;
        default:
            ESP_LOGE(TAG, "Unsupported NVS type: %d", type);
            ret = ESP_ERR_NOT_SUPPORTED;
            break;
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store value for key %s: %s", key, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Stored value for key %s", key);
    }

    nvs_commit(sel_handle);
    nvs_close(sel_handle);
    return ret;
}

esp_err_t mod_nvs_get_value(const char *key, void* value, nvs_type_t type) {
    esp_err_t ret = open_nvs();
    if (ret != ESP_OK) return ret;

    switch (type) {
        case NVS_TYPE_I8:
            ret = nvs_get_i8(sel_handle, key, (int8_t*)value);
            break;
        case NVS_TYPE_U8:
            ret = nvs_get_u8(sel_handle, key, (uint8_t*)value);
            break;
        case NVS_TYPE_I16:
            ret = nvs_get_i16(sel_handle, key, (int16_t*)value);
            break;
        case NVS_TYPE_U16:
            ret = nvs_get_u16(sel_handle, key, (uint16_t*)value);
            break;
        case NVS_TYPE_I32:
            ret = nvs_get_i32(sel_handle, key, (int32_t*)value);
            break;
        case NVS_TYPE_U32:
            ret = nvs_get_u32(sel_handle, key, (uint32_t*)value);
            break;
        case NVS_TYPE_I64:
            ret = nvs_get_i64(sel_handle, key, (int64_t*)value);
            break;
        case NVS_TYPE_U64:
            ret = nvs_get_u64(sel_handle, key, (uint64_t*)value);
            break;
        case NVS_TYPE_STR: {
            // For strings, the value should be a buffer to store the string
            size_t required_size;
            ret = nvs_get_str(sel_handle, key, NULL, &required_size); // Get required size
            if (ret == ESP_OK) {
                ret = nvs_get_str(sel_handle, key, (char*)value, &required_size); // Read string
            }
            break;
        }
        default:
            ESP_LOGE(TAG, "Unsupported NVS type: %d", type);
            ret = ESP_ERR_NOT_SUPPORTED;
            break;
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read value for key %s: %s", key, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Read value for key %s", key);
    }

    nvs_close(sel_handle);
    return ret;
}

esp_err_t mod_nvs_set_blob(const char *key, void* value, size_t len) {
    if (open_nvs() != ESP_OK) return ESP_FAIL;

    esp_err_t err = nvs_set_blob(sel_handle, key, value, len);
    err = nvs_commit(sel_handle);
    nvs_close(sel_handle);
    return err;
}

uint8_t* mod_nvs_get_blob(const char *key, size_t* len) {
    if (open_nvs() != ESP_OK) return NULL;

    // First, get the required size of the blob
    esp_err_t err = nvs_get_blob(sel_handle, key, NULL, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get blob size for key %s: %s", key, esp_err_to_name(err));
        return NULL;
    }

    // Allocate memory for the blob
    uint8_t *blob = (uint8_t *)malloc(*len);
    if (blob == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for blob");
        return NULL;
    }

    // Read the blob into the allocated memory
    err = nvs_get_blob(sel_handle, key, blob, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read blob for key %s: %s", key, esp_err_to_name(err));
        free(blob); // Free the allocated memory on failure
        return NULL;
    }

    return blob; // Return the allocated blob
}

esp_err_t mod_nvs_erase(const char *key) {
    if (open_nvs() != ESP_OK) return ESP_FAIL;

    esp_err_t err = nvs_erase_key(sel_handle, key);
    if (err == ESP_OK) {
        err = nvs_commit(sel_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Value with key %s erased", key);
        }
    }

    nvs_close(sel_handle);

    return err;
}

esp_err_t mod_nvs_erase_all(void) {
    if (open_nvs() != ESP_OK) return ESP_FAIL;
    
    esp_err_t err = nvs_erase_all(sel_handle);
        if (err == ESP_OK) {
            err = nvs_commit(sel_handle);
        }

    ESP_LOGI(TAG, "Namespace %s was %s erased", sel_namespace, (err == ESP_OK) ? "" : "not");

    nvs_close(sel_handle);
    return ESP_OK;
}

static const char *type_to_str2(nvs_type_t type) {
    for (int i = 0; i < TYPE_STR_PAIR_SIZE; i++) {
        const type_str_pair_t *p = &type_str_pair[i];
        if (p->type == type) {
            return  p->str;
        }
    }

    return "Unknown";
}


int mod_nvs_list(const char *partition, const char *name, nvs_type_t type) {
    nvs_iterator_t it = NULL;
    esp_err_t result = nvs_entry_find(partition, name, type, &it);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "No such entry was found");
        return 1;
    }

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "NVS error: %s", esp_err_to_name(result));
        return 1;
    }

    do {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        result = nvs_entry_next(&it);

        printf("namespace = %s, key = %s, type = %s\n",
            info.namespace_name, info.key, type_to_str2(info.type));
    } while (result == ESP_OK);

    if (result != ESP_ERR_NVS_NOT_FOUND) { // the last iteration ran into an internal error
        ESP_LOGE(TAG, "NVS error %s at current iteration, stopping.", esp_err_to_name(result));
        return 1;
    }

    return 0;
}