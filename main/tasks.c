#include "tasks.h"
#include "wifi.h"
#include <esp_log.h>
#include <string.h>
#include <esp_http_client.h>
#include <unistd.h>

THPayload lastReadings[MAX_CHANNELS];
QueueHandle_t readingsWriteQueue;
QueueHandle_t influxdbSendQueue;

static const char *TAGR = "ESP32-TFA-TH/Reader";
static const char *TAGW = "ESP32-TFA-TH/Writer";
static const char *TAGI = "ESP32-TFA-TH/InfluxDB";

#define INFLUX_BUFFER_FILE "/sdcard/INFLXBUF.txt"
#define POST_DATA_SIZE 1024 + 128
#define POST_DATA_FREE(len) POST_DATA_SIZE - len

#define ESP_PDCHECK(ret, err) do {ESP_ERROR_CHECK((ret) != pdTRUE ? err : ESP_OK)} while(0);
// TODO use generic PD_ERRORCHECK macro

void loop_file_writer(void *arg) {
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
            // TODO peek before open, handle SD card removal
            ESP_PDCHECK(xQueueReceive(readingsWriteQueue, &data, portMAX_DELAY), ESP_ERR_TIMEOUT);

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
    vTaskDelete(NULL);
}

void loop_tfa(void *arg) {
    ManchesterState *state = arg;

    char dataBuff[DATA_BYTES];
    while (true) {
        if (!skip_header_bytes(state)) {
            continue;
        }
        // esp_log_level_set("ESP32-TFA-TH/RF-PE", ESP_LOG_VERBOSE);
        size_t bytes_read = read_bytes(state, DATA_BYTES, dataBuff);
        // esp_log_level_set("ESP32-TFA-TH/RF-PE", ESP_LOG_DEBUG);
        if (bytes_read != DATA_BYTES * 8) {
            continue;
        }

        THPayload data = decode_payload(dataBuff);
        if (data.checksum == data.check_byte) {
            if (data.timestamp - lastReadings[data.channel - 1].timestamp > 10) {
                lastReadings[data.channel - 1] = data;
                if (xQueueSend(readingsWriteQueue, &data, 0) != pdTRUE) {
                    ESP_LOGE(TAGR, "!!! TFA received reading / file write buffer overrun !!!");
                }
                if (xQueueSend(influxdbSendQueue, &data, 0) != pdTRUE) {
                    ESP_LOGE(TAGR, "!!! TFA received reading / InfluxDB send buffer overrun !!!");
                }
                ESP_LOGI(TAGR, THPAYLOAD_FMT, THPAYLOAD_FMT_ARGS(data));
                ESP_ERROR_CHECK(ESP_OK);
            }
        }
    }
    vTaskDelete(NULL);
}


size_t fill_influx_write_buffer(char *post_data, size_t len) {
    THPayload sample;
    while (POST_DATA_FREE(len) > 128) {
        if (xQueuePeek(influxdbSendQueue, &sample, len > 0 ? 10 * 1000 / portTICK_PERIOD_MS : portMAX_DELAY) !=
            pdTRUE) {
            break;
        }
        int written = snprintf(
                post_data + len, POST_DATA_FREE(len),
                CONFIG_ESP_INFLUXDB_MEASUREMENT ","
                "channel=%d,session=0x%.2X,sensor=0x%.2X "
                "humidity=%di,temp_celsius=%f,low_battery=%s "
                "%ld\n",
                sample.channel, sample.session_id, sample.sensor_type,
                sample.humidity, sample.temp_celsius,
                sample.battery & 0x01 ? "TRUE" : "FALSE",
                sample.timestamp);
        if (written >= POST_DATA_FREE(len)) {
            break;
        } else {
            ESP_PDCHECK(xQueueReceive(influxdbSendQueue, &sample, 0), ESP_ERR_INVALID_STATE);
            len += written;
        }
    }
    return len;
}

bool send_influx_write(esp_http_client_handle_t client, char *post_data, size_t len) {
    esp_http_client_set_header(client, "Content-Type", "text/plain");
    esp_http_client_set_post_field(client, post_data, len);
    if (esp_http_client_perform(client) == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status >= 200 && status < 300) {
            ESP_LOGD(TAGI, "Sent %d bytes of data to InfluxDB", len);
            return true;
        } else {
            ESP_LOGE(TAGI, "Got status code %d in response to %d bytes of data sent to InfluxDB", status, len);
        }
    } else {
        ESP_LOGE(TAGI, "HTTP Client failed to perfom");
    }
    return false;
}

#include "influx_offline_buffer.h"

void loop_influx_sender(void *arg) {
    char post_data[POST_DATA_SIZE];
    size_t len = 0;

    esp_http_client_config_t config = {
            .method = HTTP_METHOD_POST,
            .url = CONFIG_ESP_INFLUXDB_ENDPOINT,
            //.username = "user",
            //.password = "passwd",
            //.auth_type = HTTP_AUTH_TYPE_BASIC,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_INFO);

    while (true) {
        len = fill_influx_write_buffer(post_data, len);
        ESP_ERROR_CHECK(len > 0 ? ESP_OK : ESP_ERR_INVALID_STATE);

        bool send_success = false;
        esp_err_t wifi_state = ensure_wifi(10 * 1000 / portTICK_PERIOD_MS);
        if (wifi_state == ESP_OK) {
            send_success = send_influx_write(client, post_data, len);
        } else {
            ESP_LOGI(TAGI, "InfluxDB Writer HTTP Client timed out waiting for WiFi: %s (%d)",
                     esp_err_to_name(wifi_state), wifi_state);
        }

        if (send_success) {
            send_influx_offline_buffer(client, post_data);
            len = 0;
        } else if (POST_DATA_FREE(len) < 128 && uxQueueSpacesAvailable(influxdbSendQueue) < 24) {
            // the queue is close to full, we collected a lot of lines in our POST buffer, but we couldn't send them via HTTP
            // so store the data on SD card until network is available again
            store_influx_offline_buffer(post_data, len);
            len = 0;
        }
    }

    ESP_ERROR_CHECK(esp_http_client_cleanup(client));
    vTaskDelete(NULL);
}


void start_loops(ManchesterState *state) {
    readingsWriteQueue = xQueueCreate(128, sizeof(THPayload));
    influxdbSendQueue = xQueueCreate(128, sizeof(THPayload));

    TaskHandle_t xHandle = NULL;
#define ESP_CHECK_TASKCREATE(ret) ESP_ERROR_CHECK(ret == pdTRUE && xHandle != NULL ? ESP_OK : ESP_ERR_NO_MEM)
    ESP_CHECK_TASKCREATE(xTaskCreate(
            loop_tfa, "loop_tfa", 1024 * 4, (void *) state, tskIDLE_PRIORITY, &xHandle));
    ESP_CHECK_TASKCREATE(xTaskCreate(
            loop_file_writer, "loop_file_writer", 1024 * 4, NULL, tskIDLE_PRIORITY, &xHandle));
    ESP_CHECK_TASKCREATE(xTaskCreate(
            loop_influx_sender, "loop_influx_sender", 1024 * 4, NULL, tskIDLE_PRIORITY, &xHandle));
}