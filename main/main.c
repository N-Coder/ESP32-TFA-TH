#include <freertos/FreeRTOS.h>
#include <esp_log.h>

#include "wifi.h"
#include "sntp.h"
#include "sd_card.h"
#include "manchester.h"
#include "tfa.h"
#include "tasks.h"
#include "task_influx_sender.h"
#include "task_sd_writer.h"
#include "webserver.h"


static const char *TAG = "ESP32-TFA-TH/Main";

static ManchesterConfig config;
static TFATaskState tasks[2];
static TFATaskManagerState taskManager;

void app_main() {
    ESP_LOGI(TAG, "Start!");

    init_wifi();
    ESP_ERROR_CHECK(ensure_wifi(portMAX_DELAY));

    init_sntp();
    ESP_ERROR_CHECK(await_sntp_sync(60));

    init_sd_card();
    ESP_ERROR_CHECK(ensure_sd_available(portMAX_DELAY));

    config.gpio_pin = GPIO_NUM_0;
    config.clock2T = 976;
    config.rmt_channel = RMT_CHANNEL_0;
    config.rmt_mem_block_num = 8; // all 512/64 blocks
    config.buffer_size = 1024 * 4 + 16;
    esp_log_level_set("ESP32-TFA-TH/RF-PE", ESP_LOG_DEBUG); // also disabled by #define in manchester.c
    esp_log_level_set("ESP32-TFA-TH/RF-TFA", ESP_LOG_DEBUG); // also disabled by #define in tfa.c
    ManchesterState *pe_state = manchester_start_receive(&config);

    tasks[0] = taskInfluxSender;
    tasks[1] = taskSDWriter;
    taskManager.manchesterState = pe_state;
    taskManager.runningTasks = tasks;
    taskManager.runningTaskCount = 2;
    start_loops(&taskManager);

    start_webserver(&taskManager);

    ESP_LOGI(TAG, "Done.");
}
