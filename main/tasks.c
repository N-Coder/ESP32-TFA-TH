#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <time.h>
#include <string.h>
#include "tasks.h"

THPayload lastReadings[MAX_CHANNELS];
QueueHandle_t readingsWriteQueue;

static const char *TAGW = "ESP32-TFA-TH/Writer";

void loop_writer(void *arg) {
    THPayload data;
    char str[64];
    struct tm timeinfo;
    FILE *file = NULL;
    while (true) {
        if (file) { // close old file if unused for some time
            BaseType_t ret = xQueueReceive(readingsWriteQueue, &data, 1000 / portTICK_PERIOD_MS);
            if (ret != pdTRUE) {
                ESP_LOGV(TAGW, "Closing file");
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
            ESP_LOGD(TAGW, "Opening file %s", str);
            file = fopen(str, "a");
            ESP_ERROR_CHECK(file == NULL ? ESP_ERR_INVALID_STATE : ESP_OK);
        }

        strftime(str, sizeof(str), "%Y-%m-%d %H:%M:%S", &timeinfo);
        ESP_LOGV(TAGW, "Writing reading from %s: "
                THPAYLOAD_FMT, str, THPAYLOAD_FMT_ARGS(data));
        fprintf(file, "\"%s\",0x%.2X,0x%.2X,%c,%d,%f,%f,%d\n", str, THPAYLOAD_FMT_ARGS(data));
    }
}

static const char *TAGR = "ESP32-TFA-TH/Reader";

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
                    ESP_LOGE(TAGR, "!!! TFA received reading write buffer overrun !!!");
                }
                ESP_LOGI(TAGR, THPAYLOAD_FMT, THPAYLOAD_FMT_ARGS(data));
                ESP_ERROR_CHECK(ESP_OK);
            }
        }
    }
}

void start_loops() {
    readingsWriteQueue = xQueueCreate(128, sizeof(THPayload));
    TaskHandle_t xHandle = NULL;
    BaseType_t ret;
    ret = xTaskCreate(loop_tfa, "loop_tfa", 1024 * 4, NULL, tskIDLE_PRIORITY, &xHandle);
    ESP_ERROR_CHECK(ret == pdTRUE && xHandle != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ret = xTaskCreate(loop_writer, "loop_writer", 1024 * 4, NULL, tskIDLE_PRIORITY, &xHandle);
    ESP_ERROR_CHECK(ret == pdTRUE && xHandle != NULL ? ESP_OK : ESP_ERR_NO_MEM);
}