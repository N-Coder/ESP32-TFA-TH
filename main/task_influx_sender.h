#ifndef ESP32_TFA_TEMP_HUM_TASK_INFLUX_SENDER_H
#define ESP32_TFA_TEMP_HUM_TASK_INFLUX_SENDER_H

#include "tasks.h"
#include <esp_http_client.h>

#define POST_DATA_SIZE 1024 + 128
#define POST_DATA_FREE(state) state->post_data_size - state->post_data_len
#define POST_DATA_OFFSET(state) state->post_data + state->post_data_len

esp_err_t setup_influx_sender(TFATaskState *taskState);

esp_err_t loop_influx_sender(TFATaskState *taskState);

long get_influx_offline_buffer_length();

typedef struct InfluxSenderState {
    char *post_data;
    size_t post_data_size;
    size_t post_data_len;

    esp_http_client_handle_t http_client;
} InfluxSenderState;

extern TFATaskState taskInfluxSender;

#endif //ESP32_TFA_TEMP_HUM_TASK_INFLUX_SENDER_H
