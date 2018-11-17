#include <esp_err.h>

#ifndef ESP32_TFA_TEMP_HUM_WIFI_H
#define ESP32_TFA_TEMP_HUM_WIFI_H

void connect_wifi();

esp_err_t await_wifi(TickType_t xTicksToWait);

#endif //ESP32_TFA_TEMP_HUM_WIFI_H
