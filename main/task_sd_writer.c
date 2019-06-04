#include "task_sd_writer.h"
#include "utils.h"
#include <string.h>
#include <unistd.h>
#include <esp_log.h>

static const char *TAG = "ESP32-TFA-TH/Writer";

TFATaskState taskSDWriter = {
        .name = "loop_file_writer",
        .loopFunction = loop_file_writer
};

esp_err_t loop_file_writer(TFATaskState *taskState) {
    THPayload data;
    char str[64];
    struct tm timeinfo;

    if (taskState->userData) { // close old file if unused for some time
        BaseType_t ret = xQueueReceive(taskState->queue, &data, 1000 / portTICK_PERIOD_MS);
        if (ret != pdTRUE) {
            ESP_LOGV(TAG, "Closing file");
            fclose(taskState->userData);
            taskState->userData = NULL;
            return ESP_OK;
        } else {
            localtime_r(&data.timestamp, &timeinfo);
        }
    } else {
        // TODO peek before open, handle SD card removal
        // TODO handle interrupt
        ESP_PDCHECK(xQueueReceive(taskState->queue, &data, portMAX_DELAY), ESP_ERR_TIMEOUT);

        localtime_r(&data.timestamp, &timeinfo);
        strcpy(str, "/sdcard/");
        strftime(str + strlen(str), sizeof(str) - strlen(str), "%Y%m%d", &timeinfo);
        strcat(str, ".csv");
        ESP_LOGD(TAG, "Opening file %s", str);
        taskState->userData = fopen(str, "a");
        // TODO handle SD card not available, use ensure_sd_card_available
        ESP_ERROR_CHECK(taskState->userData == NULL ? ESP_ERR_INVALID_STATE : ESP_OK);
    }

    strftime(str, sizeof(str), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGV(TAG, "Writing reading from %s: "
            THPAYLOAD_FMT, str, THPAYLOAD_FMT_ARGS(data));
    fprintf(taskState->userData, "\"%s\",0x%.2X,0x%.2X,%c,%d,%f,%f,%d\n", str, THPAYLOAD_FMT_ARGS(data));
    return ESP_OK;
}
