#include "mod_wifi_nan.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_nan.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ping/ping_sock.h"

#define EXAMPLE_NAN_SERV_NAME           "NAN Service Name"
#define EXAMPLE_NAN_MATCHING_FILTER     ""


static const char *TAG = "ESP-NAN";
static uint8_t g_peer_inst_id;
static esp_event_handler_instance_t instance_any_id;
static uint64_t last_update_time = 0;
static uint8_t sender_id;

static void pub_receive_event_handler(void *arg, esp_event_base_t event_base,
                                      int32_t event_id, void *event_data)
{
    wifi_event_nan_receive_t *evt = (wifi_event_nan_receive_t *)event_data;
    g_peer_inst_id = evt->peer_inst_id;

    wifi_nan_followup_params_t fup = {
        .inst_id = sender_id,
        .peer_inst_id = g_peer_inst_id,
        .svc_info = "Welcome",
    };

    /* Reply to the message from a subscriber */
    esp_wifi_nan_send_message(&fup);
}

static void pub_indication_event_handler(void *arg, esp_event_base_t event_base,
                                             int32_t event_id, void *event_data)
{
    if (event_data == NULL) return;
    wifi_event_ndp_indication_t *evt = (wifi_event_ndp_indication_t *)event_data;

    wifi_nan_datapath_resp_t ndp_resp = {0};
    ndp_resp.accept = true; /* Accept incoming datapath request */
    ndp_resp.ndp_id = evt->ndp_id;
    memcpy(ndp_resp.peer_mac, evt->peer_nmi, 6);
    esp_wifi_nan_datapath_resp(&ndp_resp);
}

static void nan_setup() {
    /* Start NAN Discovery */
    wifi_nan_config_t nan_cfg = WIFI_NAN_CONFIG_DEFAULT();
    esp_netif_create_default_wifi_nan();
    esp_wifi_nan_start(&nan_cfg);
}

void wifi_nan_publish(void)
{
    nan_setup();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    WIFI_EVENT_NAN_RECEIVE,
                    &pub_receive_event_handler,
                    NULL, &instance_any_id));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    WIFI_EVENT_NDP_INDICATION,
                    &pub_indication_event_handler,
                    NULL, &instance_any_id));

    wifi_nan_publish_cfg_t publish_cfg = {
        .service_name = EXAMPLE_NAN_SERV_NAME,
        .type = NAN_PUBLISH_UNSOLICITED,
        // .type = NAN_PUBLISH_SOLICITED,
        .matching_filter = EXAMPLE_NAN_MATCHING_FILTER,
        .single_replied_event = 0,          // reply everytime
    };

    /* Publish a service */
    bool ndp_require_consent = true;
    sender_id = esp_wifi_nan_publish_service(&publish_cfg, ndp_require_consent);
}

void wifi_nan_sendData2(uint64_t current_time) {
    if (current_time - last_update_time < 2000000) return;
    last_update_time = current_time;
    printf("IM HERE\n");

    wifi_nan_followup_params_t fup = {
        .inst_id = sender_id,
        .peer_inst_id = g_peer_inst_id,
        .svc_info = "Welcome",
    };

    /* Reply to the message from a subscriber */
    esp_wifi_nan_send_message(&fup);
}

//! Minimum wifi code requirement for NAN
// void initialise_wifi(void)
// {
//     esp_err_t ret = nvs_flash_init();
//     if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
//         ESP_ERROR_CHECK(nvs_flash_erase());
//         ret = nvs_flash_init();
//     }
//     ESP_ERROR_CHECK(ret);

//     ESP_ERROR_CHECK(esp_netif_init());
//     ESP_ERROR_CHECK(esp_event_loop_create_default());
//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     ESP_ERROR_CHECK(esp_wifi_init(&cfg));
//     ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM) );
// }


static wifi_event_nan_svc_match_t sub_match_event;

static void svc_match_handler(void* arg, esp_event_base_t event_base,
                                        int32_t event_id, void* event_data)
{
    wifi_event_nan_svc_match_t *evt = (wifi_event_nan_svc_match_t *)event_data;
    ESP_LOGI(TAG, "NAN Publisher found for Serv ID %d", evt->subscribe_id);
    memcpy(&sub_match_event, evt, sizeof(wifi_event_nan_svc_match_t));
}

void wifi_nan_subscribe(void)
{
    nan_setup();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_NAN_SVC_MATCH,
                                                        &svc_match_handler,
                                                        NULL, &instance_any_id));

    /* Subscribe a service */
    wifi_nan_subscribe_cfg_t subscribe_cfg = {
        .service_name = EXAMPLE_NAN_SERV_NAME,
        .type = NAN_SUBSCRIBE_PASSIVE,
        // .type = NAN_SUBSCRIBE_ACTIVE,
        .matching_filter = EXAMPLE_NAN_MATCHING_FILTER,
        .single_match_event = 1,
    };

    sender_id = esp_wifi_nan_subscribe_service(&subscribe_cfg);
}

void wifi_nan_sendData(uint64_t current_time) {
    if (current_time - last_update_time < 2000000) return;
    last_update_time = current_time;

    wifi_nan_followup_params_t fup = {
        .inst_id = sender_id,
        .peer_inst_id = sub_match_event.publish_id,
        .svc_info = "Hello",
    };
    memcpy(fup.peer_mac, sub_match_event.pub_if_mac, sizeof(fup.peer_mac));

    if (esp_wifi_nan_send_message(&fup) == ESP_OK)
        ESP_LOGI(TAG, "Sending message '%s' to Publisher "MACSTR" ...",
                    "Hello", MAC2STR(sub_match_event.pub_if_mac));
}

bool wifi_nan_checkPeers(uint64_t current_time) {
    if (current_time - last_update_time < 2000000) return false;
    last_update_time = current_time;

    int num_peers = 5;
    struct nan_peer_record peer_records[num_peers];
    esp_err_t ret;

    ret = esp_wifi_nan_get_peer_records(&num_peers, sender_id, peer_records);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get peer records: %s", esp_err_to_name(ret));
        return false;
    }

    if (num_peers > 0) {
        ESP_LOGI(TAG, "Found %d peers for service", num_peers);
        // You can process the peer information here if needed
        return true;
    } else {
        ESP_LOGI(TAG, "No peers found for service");
        return false;
    }
}