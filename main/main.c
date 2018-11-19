#include <freertos/FreeRTOS.h>
#include <esp_log.h>

#include "wifi.h"
#include "sntp.h"
#include "sd_card.h"
#include "tfa.h"
#include "manchester.h"
#include "tasks.h"

#define PIN 0
static const char *TAG = "ESP32-TFA-TH/Main";

void app_main() {
    ESP_LOGI(TAG, "Start!");

    connect_wifi();
    ESP_ERROR_CHECK(await_wifi(portMAX_DELAY));

    init_sntp();
    ESP_ERROR_CHECK(await_sntp_sync(60));

    init_sd_card();

    receive(PIN);
    // sync_clock(10);
    set_clock(976);

    start_loops();
    esp_log_level_set("ESP32-TFA-TH/RF-TFA", ESP_LOG_INFO);
    esp_log_level_set("ESP32-TFA-TH/RF-PE/Bits", ESP_LOG_INFO);

    ESP_LOGI(TAG, "Done.");
}
