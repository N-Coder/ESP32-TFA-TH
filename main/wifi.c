#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <nvs_flash.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event_loop.h>


static const char *TAG = "ESP32-TFA-TH/WiFi";

static EventGroupHandle_t wifi_event_group;
const int READY_BIT = BIT0;
const int CONNECTED_BIT = BIT1;
const int GOT_IP_BIT = BIT2;

esp_err_t event_handler(void *ctx, system_event_t *event) {
    if (event->event_id == SYSTEM_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi driver ready, connecting...");
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT | GOT_IP_BIT);
        xEventGroupSetBits(wifi_event_group, READY_BIT);
        ESP_ERROR_CHECK(esp_wifi_connect());
    } else if (event->event_id == SYSTEM_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "Connected to WiFi network %s", event->event_info.connected.ssid);
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
    } else if (event->event_id == SYSTEM_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Got IP address %s", ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        xEventGroupSetBits(wifi_event_group, GOT_IP_BIT);
        esp_log_level_set("phy_init", ESP_LOG_INFO);
    } else if (event->event_id == SYSTEM_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Lost WiFi connection to %s (%d), reconnecting...",
                 event->event_info.disconnected.ssid, event->event_info.disconnected.reason);
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT | GOT_IP_BIT);
        ESP_ERROR_CHECK(esp_wifi_connect());
        // this is a workaround as ESP32 WiFi libs don't currently auto-reassociate
    } else if (event->event_id == SYSTEM_EVENT_STA_STOP) {
        ESP_LOGI(TAG, "WiFi driver shut down");
        xEventGroupClearBits(wifi_event_group, READY_BIT | CONNECTED_BIT | GOT_IP_BIT);
    } else {
        ESP_LOGE(TAG, "Unknown Wifi Event %d", event->event_id);
    }
    return ESP_OK;
}

void connect_wifi() {
    ESP_ERROR_CHECK(nvs_flash_init());
    tcpip_adapter_init();

    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

    wifi_event_group = xEventGroupCreate();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_config_t sta_config = {
            .sta = {
                    .ssid = CONFIG_ESP_WIFI_SSID,
                    .password = CONFIG_ESP_WIFI_PASSWORD,
                    .bssid_set = false
            }
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Connecting WiFi...");
}

esp_err_t await_wifi(TickType_t xTicksToWait) {
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, READY_BIT | CONNECTED_BIT | GOT_IP_BIT,
                                           false, true, xTicksToWait);
    if ((bits & READY_BIT) != 0 && (bits & CONNECTED_BIT) != 0 && (bits & GOT_IP_BIT) != 0) {
        wifi_ap_record_t wifiInfo;
        ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&wifiInfo))
        ESP_LOGI(TAG, "WiFi connection to %s ready!", wifiInfo.ssid);
        return ESP_OK;
    } else {
        return ESP_ERR_TIMEOUT;
    }
}


esp_err_t ensure_wifi(TickType_t xTicksToWait) {
    EventBits_t bits;
    bits = xEventGroupGetBits(wifi_event_group);
    if ((bits & READY_BIT) != 0 && (bits & CONNECTED_BIT) == 0) {
        esp_err_t ret = esp_wifi_connect();
        if (ret != ESP_OK) {
            return ret;
        }
    }
    bits = xEventGroupWaitBits(wifi_event_group, READY_BIT | CONNECTED_BIT | GOT_IP_BIT, false, true, xTicksToWait);
    if ((bits & READY_BIT) != 0 && (bits & CONNECTED_BIT) != 0 && (bits & GOT_IP_BIT) != 0) {
        wifi_ap_record_t wifiInfo;
        return esp_wifi_sta_get_ap_info(&wifiInfo);
    } else {
        if ((bits & READY_BIT) != 0) {
            return ESP_ERR_TIMEOUT;
        } else {
            return ESP_ERR_WIFI_NOT_STARTED;
        }
    }
}

