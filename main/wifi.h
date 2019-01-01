#ifndef ESP32_TFA_TEMP_HUM_WIFI_H
#define ESP32_TFA_TEMP_HUM_WIFI_H

#include <freertos/FreeRTOS.h>
#include <esp_err.h>

void connect_wifi();

esp_err_t await_wifi(TickType_t xTicksToWait);

esp_err_t ensure_wifi(TickType_t xTicksToWait);

#endif //ESP32_TFA_TEMP_HUM_WIFI_H
