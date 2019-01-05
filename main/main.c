#include <freertos/FreeRTOS.h>
#include <esp_log.h>

#include "wifi.h"
#include "sntp.h"
#include "sd_card.h"
#include "tfa.h"
#include "manchester.h"
#include "tasks.h"
#include "webserver.h"

static const char *TAG = "ESP32-TFA-TH/Main";

void app_main() {
    ESP_LOGI(TAG, "Start!");

    init_wifi();
    ESP_ERROR_CHECK(ensure_wifi(portMAX_DELAY));

    init_sntp();
    ESP_ERROR_CHECK(await_sntp_sync(60));

    init_sd_card();
    ESP_ERROR_CHECK(ensure_sd_available(portMAX_DELAY));

    ManchesterConfig config = {
            .gpio_pin = 0,
            .clock2T = 976,
            .rmt_channel = RMT_CHANNEL_0,
            .rmt_mem_block_num = 8, // all 512/64 blocks
            .buffer_size = 1024 * 4
    };
    esp_log_level_set("ESP32-TFA-TH/RF-PE", ESP_LOG_DEBUG); // also disabled by #define in manchester.c
    esp_log_level_set("ESP32-TFA-TH/RF-TFA", ESP_LOG_DEBUG); // also disabled by #define in tfa.c
    ManchesterState *pe_state = manchester_start_receive(&config);

    start_loops(pe_state);

    start_webserver();

    ESP_LOGI(TAG, "Done.");
}
