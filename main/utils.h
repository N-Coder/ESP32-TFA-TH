#ifndef ESP32_TFA_TEMP_HUM_UTILS_H
#define ESP32_TFA_TEMP_HUM_UTILS_H

#include <esp_err.h>

#define ESP_PDCHECK(ret, err) do {ESP_ERROR_CHECK((ret) != pdTRUE ? err : ESP_OK)} while(0);

#endif //ESP32_TFA_TEMP_HUM_UTILS_H
