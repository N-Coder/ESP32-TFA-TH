#ifndef ESP32_TFA_TEMP_HUM_TASK_SD_WRITER_H
#define ESP32_TFA_TEMP_HUM_TASK_SD_WRITER_H

#include "tasks.h"

esp_err_t loop_file_writer(TFATaskState *taskState);

extern TFATaskState taskSDWriter;

#endif //ESP32_TFA_TEMP_HUM_TASK_SD_WRITER_H
