void store_influx_offline_buffer(char *post_data, size_t len) {
    FILE *dump = fopen(INFLUX_BUFFER_FILE, "a");
    fputs(post_data, dump);
    fclose(dump);
    ESP_LOGD(TAGI, "Wrote %d bytes of data destined to InfluxDB to file %s for interim storage", len,
             INFLUX_BUFFER_FILE);
}

void send_influx_offline_buffer(esp_http_client_handle_t client, char *post_data) {
    FILE *dump = fopen(INFLUX_BUFFER_FILE, "r");
    size_t len = 0;
    bool send_success;

    if (dump) {
        ESP_LOGD(TAGI, "Connectivity restored, flushing stored data from file %s to InfluxDB", INFLUX_BUFFER_FILE);
    }
    while (dump) {
        ESP_LOGV(TAGI, "Filling HTTP post buffer from file.");
        while (POST_DATA_FREE(len) > 128) {
            if (fgets(post_data + len, POST_DATA_FREE(len), dump) == NULL) {
                ESP_LOGV(TAGI, "Read last line, closing file.");
                fclose(dump);
                dump = NULL;
                break;
            }
            len = strlen(post_data);
            ESP_LOGV(TAGI, "Buffer contains %d bytes after reading a new line (%d still free).", len,
                     POST_DATA_FREE(len));
        }
        ESP_LOGD(TAGI, "Finished reading, buffer contains %d bytes (%d still free).", len, POST_DATA_FREE(len));

        char *term_nl = strrchr(post_data, '\n');
        if (term_nl != NULL && *(term_nl + 1) != 0) {
            // last newline is not right before terminator, so we have a dangling half-read line we need to unread
            size_t newlen = term_nl - post_data + 1;
            ESP_LOGV(TAGI, "Buffer contains %d characters, but last newline is at %d. "
                           "Seeking back in file by %d characters, and continuing with buffer length %d.",
                     len, (term_nl - post_data), (newlen - len), newlen);
            fseek(dump, (long) (newlen - len), SEEK_CUR);
            len = newlen;
        }

        send_success = send_influx_write(client, post_data, len);
        len = 0;
        if (!send_success) {
            ESP_LOGD(TAGI, "Connection broke again, restart sending the offline buffer later.");
            break;
        } else if (dump == NULL) {
            // fgets reached the end and closed the file, so delete after successfully sending
            ESP_LOGD(TAGI, "Whole offline buffer successfully sent. Deleting file.");
            unlink(INFLUX_BUFFER_FILE);
            break;
        } else {
            ESP_LOGD(TAGI, "Part of offline buffer successfully sent, continuing with next part.");
            continue;
        }
    }
}