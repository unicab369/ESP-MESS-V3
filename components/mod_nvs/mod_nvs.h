#include <stddef.h>
#include <string.h>
#include "nvs_flash.h"
#include <errno.h>

#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h"


typedef struct {
    nvs_type_t type;
    const char *str;
} type_str_pair_t;

static const type_str_pair_t type_str_pair[] = {
    { NVS_TYPE_I8, "i8" },
    { NVS_TYPE_U8, "u8" },
    { NVS_TYPE_U16, "u16" },
    { NVS_TYPE_I16, "i16" },
    { NVS_TYPE_U32, "u32" },
    { NVS_TYPE_I32, "i32" },
    { NVS_TYPE_U64, "u64" },
    { NVS_TYPE_I64, "i64" },
    { NVS_TYPE_STR, "str" },
    { NVS_TYPE_BLOB, "blob" },
    { NVS_TYPE_ANY, "any" },
};


void mod_nvs_setup(void);
int mod_nvs_set_namespace(const char* target);

esp_err_t mod_nvs_set_value(const char *key, const void* value, nvs_type_t type);
esp_err_t mod_nvs_get_value(const char *key, void* value, nvs_type_t type);
esp_err_t mod_nvs_set_blob(const char *key, void* value, size_t len);
uint8_t* mod_nvs_get_blob(const char *key, size_t* len);

esp_err_t mod_nvs_erase(const char *key);
esp_err_t mod_nvs_erase_all(void);
int mod_nvs_list(const char *partition, const char *name, nvs_type_t type);