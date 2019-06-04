#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

long get_influx_offline_buffer_length() {
    FILE *dump = fopen(INFLUX_BUFFER_FILE, "r");
    if (dump) {
        fseek(dump, 0, SEEK_END);
        long size = ftell(dump);
        fclose(dump);
        return size;
    } else {
        return -1;
    }
}

void store_influx_offline_buffer(InfluxSenderState *state) {
    FILE *dump = fopen(INFLUX_BUFFER_FILE, "a");
    // TODO use ensure_sd_card_available
    fputs(state->post_data, dump);
    fclose(dump);
    ESP_LOGD(TAG, "Wrote %d bytes of data destined to InfluxDB to file %s for interim storage", state->post_data_len,
             INFLUX_BUFFER_FILE);
}

void send_influx_offline_buffer(InfluxSenderState *state) {
    state->post_data_len = 0;
    FILE *dump = fopen(INFLUX_BUFFER_FILE, "r");
    esp_err_t send_success;

    if (dump) {
        ESP_LOGD(TAG, "Connectivity restored, flushing stored data from file %s to InfluxDB", INFLUX_BUFFER_FILE);
    }
    while (dump) {
        ESP_LOGV(TAG, "Filling HTTP post buffer from file.");
        while (POST_DATA_FREE(state) > 128) {
            if (fgets(POST_DATA_OFFSET(state), POST_DATA_FREE(state), dump) == NULL) {
                ESP_LOGV(TAG, "Read last line, closing file.");
                fclose(dump);
                dump = NULL;
                break;
            }
            state->post_data_len = strlen(state->post_data);
            ESP_LOGV(TAG, "Buffer contains %d bytes after reading a new line (%d still free).",
                     state->post_data_len, POST_DATA_FREE(state));
        }
        ESP_LOGD(TAG, "Finished reading, buffer contains %d bytes (%d still free).",
                 state->post_data_len, POST_DATA_FREE(state));

        char *term_nl = strrchr(state->post_data, '\n');
        if (term_nl != NULL && *(term_nl + 1) != 0) {
            // last newline is not right before terminator, so we have a dangling half-read line we need to unread
            size_t newlen = term_nl - state->post_data + 1;
            ESP_LOGV(TAG, "Buffer contains %d characters, but last newline is at %d. "
                          "Seeking back in file by %d characters, and continuing with buffer length %d.",
                     state->post_data_len, (term_nl - state->post_data), (newlen - state->post_data_len), newlen);
            fseek(dump, (long) (newlen - state->post_data_len), SEEK_CUR);
            state->post_data_len = newlen;
        }
        if (state->post_data_len > 0) {
            send_success = send_influx_write(state);
            state->post_data_len = 0;
        } else {
            send_success = ESP_OK;
        }
        if (send_success != ESP_OK) {
            ESP_LOGD(TAG, "Connection broke again, restart sending the offline buffer later.");
            break;
        } else if (dump == NULL) {
            // fgets reached the end and closed the file, so delete after successfully sending
            ESP_LOGD(TAG, "Whole offline buffer successfully sent. Deleting file.");
            unlink(INFLUX_BUFFER_FILE);
            break;
        } else {
            ESP_LOGD(TAG, "Part of offline buffer successfully sent, continuing with next part.");
            taskYIELD();
            continue;
        }
    }
    if (dump) {
        fclose(dump);
        dump = NULL;
    }
}