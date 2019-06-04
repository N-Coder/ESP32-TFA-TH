#include "task_influx_sender.h"
#include "wifi.h"
#include "utils.h"
#include <esp_log.h>

static const char *TAG = "ESP32-TFA-TH/InfluxDB";

TFATaskState taskInfluxSender = {
        .name = "loop_influx_sender",
        .setupFunction = setup_influx_sender,
        .loopFunction = loop_influx_sender
};

void fill_influx_write_buffer(QueueHandle_t influxdbSendQueue, InfluxSenderState *state) {
    THPayload sample;
    while (POST_DATA_FREE(state) > 128) {
        // TODO handle interrupt
        if (xQueuePeek(influxdbSendQueue, &sample,
                       state->post_data_len > 0 ? 10 * 1000 / portTICK_PERIOD_MS : portMAX_DELAY) !=
            pdTRUE) {
            break;
        }
        int written = snprintf(
                POST_DATA_OFFSET(state), POST_DATA_FREE(state),
                CONFIG_ESP_INFLUXDB_MEASUREMENT ","
                "channel=%d,session=0x%.2X,sensor=0x%.2X "
                "humidity=%di,temp_celsius=%f,low_battery=%s "
                "%ld\n",
                sample.channel, sample.session_id, sample.sensor_type,
                sample.humidity, sample.temp_celsius,
                sample.battery & 0x01 ? "TRUE" : "FALSE",
                sample.timestamp);
        if (written >= POST_DATA_FREE(state)) {
            break;
        } else {
            ESP_PDCHECK(xQueueReceive(influxdbSendQueue, &sample, 0), ESP_ERR_INVALID_STATE);
            state->post_data_len += written;
        }
    }
}

esp_err_t send_influx_write(InfluxSenderState *state) {
    esp_http_client_handle_t client = state->http_client;
    esp_http_client_set_header(client, "Content-Type", "text/plain");
    esp_http_client_set_post_field(client, state->post_data, state->post_data_len);
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status >= 200 && status < 300) {
            ESP_LOGD(TAG, "Sent %d bytes of data to InfluxDB (%d)", state->post_data_len, status);
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "Got status code %d in response to %d bytes of data sent to InfluxDB",
                     status, state->post_data_len);
            return ESP_ERR_HTTP_BASE + status;
        }
    } else {
        ESP_LOGE(TAG, "HTTP client failed to perfom: %s 0x%x(%d)", esp_err_to_name(err), err, err);
        return err;
    }
}

#define INFLUX_BUFFER_FILE "/sdcard/INFLXBUF.txt"

#include "influx_offline_buffer.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

esp_err_t setup_influx_sender(TFATaskState *taskState) {
    InfluxSenderState *influxState = malloc(sizeof(InfluxSenderState));
    taskState->userData = influxState;
    influxState->post_data = malloc(POST_DATA_SIZE * sizeof(char));
    influxState->post_data_size = POST_DATA_SIZE;
    influxState->post_data_len = 0;

    esp_http_client_config_t config = {
            .method = HTTP_METHOD_POST,
            .url = CONFIG_ESP_INFLUXDB_ENDPOINT,
            //.username = "user",
            //.password = "passwd",
            //.auth_type = HTTP_AUTH_TYPE_BASIC,
    };
    influxState->http_client = esp_http_client_init(&config);
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_INFO);
    return ESP_OK;
}

esp_err_t loop_influx_sender(TFATaskState *taskState) {
    InfluxSenderState *influxState = taskState->userData;

    fill_influx_write_buffer(taskState->queue, influxState); // TODO handle interrupt
    ESP_ERROR_CHECK(influxState->post_data_len > 0 ? ESP_OK : ESP_ERR_INVALID_STATE);

    bool send_result = ESP_FAIL;
    esp_err_t wifi_state = ensure_wifi(10 * 1000 / portTICK_PERIOD_MS); // TODO handle interrupt
    if (wifi_state == ESP_OK) {
        send_result = send_influx_write(influxState);
    } else {
        ESP_LOGI(TAG, "InfluxDB Writer HTTP Client timed out waiting for WiFi: %s 0x%x(%d)",
                 esp_err_to_name(wifi_state), wifi_state, wifi_state);
    }

    if (send_result == ESP_OK) {
        send_influx_offline_buffer(influxState);
        influxState->post_data_len = 0;
    } else if (POST_DATA_FREE(influxState) < 128) {
        // the queue is close to full, we collected a lot of lines in our POST buffer, but we couldn't send them via HTTP
        // so store the data on SD card until network is available again
        store_influx_offline_buffer(influxState);
        influxState->post_data_len = 0;
    }

    if (wifi_state != ESP_OK) {
        return wifi_state;
    } else {
        return send_result;
    }
}

//void shutdown_influx_sender(void *arg) {
//    ESP_ERROR_CHECK(esp_http_client_cleanup(client));
//    free(influxState->post_data);
//    free(taskState->userData);
//}