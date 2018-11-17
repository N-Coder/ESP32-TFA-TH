#include <freertos/FreeRTOS.h>
#include <esp_log.h>

#include "wifi.h"


#define PIN 0
static const char *TAG = "ESP32-TFA-TH/Main";


void app_main() {
    ESP_LOGI(TAG, "Start!");
    connect_wifi();
    await_wifi();
    ESP_LOGI(TAG, "Done.");
}
