#include "tasks.h"
#include <esp_log.h>

static const char *TAG = "ESP32-TFA-TH/Reader";

void loop_task_reader(void *arg) {
    TFATaskManagerState *state = arg;

    char dataBuff[DATA_BYTES];
    while (true) {
        if (!skip_header_bytes(state->manchesterState)) {
            continue;
        }
        // esp_log_level_set("ESP32-TFA-TH/RF-PE", ESP_LOG_VERBOSE);
        size_t bytes_read = read_bytes(state->manchesterState, DATA_BYTES, dataBuff);
        // esp_log_level_set("ESP32-TFA-TH/RF-PE", ESP_LOG_DEBUG);
        if (bytes_read != DATA_BYTES * 8) {
            continue;
        }

        THPayload data = decode_payload(dataBuff);
        if (data.valid && (data.timestamp - state->lastReadings[data.channel - 1].timestamp) > 10) {
            state->lastReadings[data.channel - 1] = data;
            for (int i = 0; i < state->runningTaskCount; i++) {
                if (xQueueSend(state->runningTasks[i].queue, &data, 0) != pdTRUE) {
                    ESP_LOGE(TAG, "!!! TFA received reading / %s buffer overrun !!!", state->runningTasks[i].name);
                }
            }
            ESP_LOGI(TAG, THPAYLOAD_FMT, THPAYLOAD_FMT_ARGS(data));
            ESP_ERROR_CHECK(ESP_OK);
        }
    }
}

void loop_task_wrapper(void *arg) {
    TFATaskState *taskState = arg;
    if (taskState->setupFunction != NULL) {
        ESP_ERROR_CHECK(taskState->setupFunction(taskState));
    }
    while (true) {
        time(&taskState->lastStartTimes[taskState->logOffset]);
        taskState->lastDurations[taskState->logOffset] = esp_timer_get_time();
        taskState->lastResults[taskState->logOffset] = ESP_OK;

        taskState->lastResults[taskState->logOffset] = taskState->loopFunction(taskState);

        taskState->lastDurations[taskState->logOffset] =
                esp_timer_get_time() - taskState->lastDurations[taskState->logOffset];
        taskState->logOffset = (taskState->logOffset + 1) % TFA_TASK_LOG_SIZE;
        ESP_ERROR_CHECK(ESP_OK);
    }
}

void start_loops(TFATaskManagerState *state) {
    BaseType_t ret;
    for (int i = 0; i < state->runningTaskCount; i++) {
        TFATaskState *taskState = &state->runningTasks[i];
        if (taskState->queueSize == 0) {
            taskState->queueSize = 128;
        }
        if (taskState->stackSize == 0) {
            taskState->stackSize = 1024 * 4;
        }
        if (taskState->queue == NULL) {
            taskState->queue = xQueueCreate(taskState->queueSize, sizeof(THPayload));
        }
        ret = xTaskCreatePinnedToCore(loop_task_wrapper, taskState->name, taskState->stackSize, taskState,
                                      tskIDLE_PRIORITY + 10, &taskState->task, APP_CPU_NUM);
        ESP_ERROR_CHECK(ret == pdTRUE && taskState->task != NULL ? ESP_OK : ESP_ERR_NO_MEM)
    }
    ret = xTaskCreatePinnedToCore(loop_task_reader, "loop_tfa_reader", 1024 * 4, state, tskIDLE_PRIORITY + 20,
                                  &state->readerTask, APP_CPU_NUM);
    ESP_ERROR_CHECK(ret == pdTRUE && state->readerTask != NULL ? ESP_OK : ESP_ERR_NO_MEM)
}

//void stop_loops(TFATaskManagerState *state) {
//    for (int i = 0; i < state->runningTaskCount; i++) {
//        if (state->runningTasks[i].shutdownFunction != NULL) {
//            state->runningTasks[i].shutdownFunction(&state->runningTasks[i]);
//        }
//        vTaskDelete(state->runningTasks[i].task);
//    }
//    vTaskDelete(state->readerTask);
//}