#ifndef ESP32_TFA_TEMP_HUM_SNTP_H
#define ESP32_TFA_TEMP_HUM_SNTP_H

#include <esp_err.h>

void init_sntp();

esp_err_t await_sntp_sync(const int timeout_seconds);

#endif //ESP32_TFA_TEMP_HUM_SNTP_H
