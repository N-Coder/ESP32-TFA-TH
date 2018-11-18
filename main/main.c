#include <freertos/FreeRTOS.h>
#include <esp_log.h>

#include "wifi.h"
#include "sntp.h"
#include "sd_card.h"

#define PIN 0
static const char *TAG = "ESP32-TFA-TH/Main";


void app_main() {
    ESP_LOGI(TAG, "Start!");
    connect_wifi();
    ESP_ERROR_CHECK(await_wifi(portMAX_DELAY));

    init_sntp();
    ESP_ERROR_CHECK(await_sntp_sync(60));

    init_sd_card();

    ESP_LOGI(TAG, "Done.");
}
