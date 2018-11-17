#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <nvs_flash.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event_loop.h>


static const char *TAG = "ESP32-TFA-TH/WiFi";

static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;
const int GOT_IP_BIT = BIT1;

esp_err_t event_handler(void *ctx, system_event_t *event) {
    if (event->event_id == SYSTEM_EVENT_STA_START) {
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT | GOT_IP_BIT);
        esp_wifi_connect();
    } else if (event->event_id == SYSTEM_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "Connected to WiFi network %s", event->event_info.connected.ssid);
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
    } else if (event->event_id == SYSTEM_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Got IP address %s", ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        xEventGroupSetBits(wifi_event_group, GOT_IP_BIT);
        esp_log_level_set("phy_init", ESP_LOG_INFO);
    } else if (event->event_id == SYSTEM_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Lost WiFi connection, reconnecting...");
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT | GOT_IP_BIT);
        esp_wifi_connect(); // This is a workaround as ESP32 WiFi libs don't currently auto-reassociate.
    }
    return ESP_OK;
}

void connect_wifi() {
    nvs_flash_init();
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
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT | GOT_IP_BIT, false, true, portMAX_DELAY);
    if ((bits & CONNECTED_BIT) > 0 && (bits & GOT_IP_BIT) > 0) {
        ESP_LOGI(TAG, "WiFi Ready!");
        return ESP_OK;
    } else {
        return ESP_ERR_TIMEOUT;
    }
}
