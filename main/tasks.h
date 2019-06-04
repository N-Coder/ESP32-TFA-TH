#ifndef ESP32_TFA_TEMP_HUM_TASKS_H
#define ESP32_TFA_TEMP_HUM_TASKS_H

#include "tfa.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#define TFA_TASK_LOG_SIZE 10

typedef struct TFATaskState TFATaskState;

typedef esp_err_t (*setup_function_t)(TFATaskState *state);

typedef esp_err_t (*loop_function_t)(TFATaskState *state);

typedef esp_err_t (*shutdown_function_t)(TFATaskState *state);

typedef struct TFATaskState {
    char *name;
    setup_function_t setupFunction;
    loop_function_t loopFunction;
    shutdown_function_t shutdownFunction;
    void *userData;

    size_t stackSize;
    size_t queueSize;

    TaskHandle_t task;
    QueueHandle_t queue;

    esp_err_t lastResults[TFA_TASK_LOG_SIZE];
    time_t lastStartTimes[TFA_TASK_LOG_SIZE];
    int64_t lastDurations[TFA_TASK_LOG_SIZE];
    size_t logOffset;
} TFATaskState;

typedef struct TFATaskManagerState {
    ManchesterState *manchesterState;
    TaskHandle_t readerTask;

    TFATaskState *runningTasks;
    size_t runningTaskCount;

    THPayload lastReadings[MAX_CHANNELS];
} TFATaskManagerState;

void start_loops(TFATaskManagerState *state);

#endif //ESP32_TFA_TEMP_HUM_TASKS_H
