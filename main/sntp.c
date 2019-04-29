#include <lwip/apps/sntp.h>
#include <esp_log.h>

#include "sntp.h"

#define TM_YEAR_NO_SYNC 2016 - 1900
static const char *TAG = "ESP32-TFA-TH/SNTP";

void init_sntp() {
    ESP_LOGI(TAG, "Initializing SNTP...");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
}

esp_err_t await_sntp_sync(const int timeout_seconds) {
    time_t now = 0;
    struct tm timeinfo = {0};
    for (int retry = 0; timeinfo.tm_year < TM_YEAR_NO_SYNC && retry < timeout_seconds; retry += 1) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, timeout_seconds);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    char strftime_buf[64];
    //setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0", 1);
    //tzset();
    //localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time is: %s", strftime_buf);

    if (timeinfo.tm_year < TM_YEAR_NO_SYNC) {
        return ESP_ERR_TIMEOUT;
    } else {
        return ESP_OK;
    }
}
