#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <time.h>
#include <string.h>

#include "wifi.h"
#include "sntp.h"
#include "sd_card.h"
#include "tfa.h"
#include "manchester.h"

#define PIN 0
static const char *TAG = "ESP32-TFA-TH/Main";

THPayload lastReadings[MAX_CHANNELS];
QueueHandle_t readingsWriteQueue;

void loop_tfa(void *arg);

void loop_writer(void *arg);

void app_main() {
    ESP_LOGI(TAG, "Start!");
    connect_wifi();
    ESP_ERROR_CHECK(await_wifi(portMAX_DELAY));

    init_sntp();
    ESP_ERROR_CHECK(await_sntp_sync(60));

    init_sd_card();

    receive(PIN);
//    sync_clock(10);
    set_clock(976);

    readingsWriteQueue = xQueueCreate(128, sizeof(THPayload));

    TaskHandle_t xHandle = NULL;
    BaseType_t ret;

    ret = xTaskCreate(loop_tfa, "loop_tfa", 1024 * 4, NULL, tskIDLE_PRIORITY, &xHandle);
    ESP_ERROR_CHECK(ret == pdTRUE && xHandle != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ret = xTaskCreate(loop_writer, "loop_writer", 1024 * 4, NULL, tskIDLE_PRIORITY, &xHandle);
    ESP_ERROR_CHECK(ret == pdTRUE && xHandle != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_LOGI(TAG, "Done.");
}

void loop_writer(void *arg) {
    THPayload data;
    char str[64];
    struct tm timeinfo;
    FILE *file = NULL;
    while (true) {
        if (file) { // close old file if unused for some time
            BaseType_t ret = xQueueReceive(readingsWriteQueue, &data, 1000 / portTICK_PERIOD_MS);
            if (ret != pdTRUE) {
                ESP_LOGV(TAG, "Closing file");
                fclose(file);
                file = NULL;
                continue;
            } else {
                localtime_r(&data.timestamp, &timeinfo);
            }
        } else {
            BaseType_t ret = xQueueReceive(readingsWriteQueue, &data, portMAX_DELAY);
            ESP_ERROR_CHECK(ret != pdTRUE ? ESP_ERR_TIMEOUT : ESP_OK);

            localtime_r(&data.timestamp, &timeinfo);
            strcpy(str, "/sdcard/");
            strftime(str + strlen(str), sizeof(str) - strlen(str), "%Y%m%d", &timeinfo);
            strcat(str, ".csv");
            ESP_LOGD(TAG, "Opening file %s", str);
            file = fopen(str, "a");
            ESP_ERROR_CHECK(file == NULL ? ESP_ERR_INVALID_STATE : ESP_OK);
        }

        strftime(str, sizeof(str), "%Y-%m-%d %H:%M:%S", &timeinfo);
        ESP_LOGV(TAG, "Writing reading from %s: "
                THPAYLOAD_FMT, str, THPAYLOAD_FMT_ARGS(data));
        fprintf(file, "\"%s\",0x%.2X,0x%.2X,%c,%d,%f,%f,%d\n", str, THPAYLOAD_FMT_ARGS(data));
    }
}


void loop_tfa(void *arg) {
    char dataBuff[DATA_BYTES];
    while (true) {
        if (!skip_header_bytes()) {
            continue;
        }
        if (read_bytes(DATA_BYTES, dataBuff) != DATA_BYTES * 8) {
            continue;
        }
        // TODO consume trailing 0s

        THPayload data = decode_payload(dataBuff);
        if (data.checksum == data.check_byte) {
            if (data.timestamp - lastReadings[data.channel - 1].timestamp > 10) {
                lastReadings[data.channel - 1] = data;
                if (xQueueSend(readingsWriteQueue, &data, portMAX_DELAY) != pdTRUE) {
                    ESP_LOGE(TAG, "!!! TFA received reading write buffer overrun !!!");
                }
                ESP_LOGI(TAG, THPAYLOAD_FMT, THPAYLOAD_FMT_ARGS(data));
                ESP_ERROR_CHECK(ESP_OK);
            }
        }
    }
}
