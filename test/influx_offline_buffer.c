#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

typedef void *esp_http_client_handle_t;
#define ESP_LOGD(tag, format, ...) printf ("%s - " format "\n", tag, ## __VA_ARGS__)
#define ESP_LOGV(tag, format, ...) printf ("%s - " format "\n", tag, ## __VA_ARGS__)

const size_t POST_DATA_SIZE = 1024;
#define POST_DATA_FREE(len) POST_DATA_SIZE - len
const char *INFLUX_BUFFER_FILE = "INFLXBUF.txt";
const char *TAGI = "ESP32-TFA-TH/InfluxDB";

bool (*send_influx_write_ptr)(esp_http_client_handle_t client, char *post_data, size_t len);

void set_send_influx_write(bool (*ptr)(esp_http_client_handle_t client, char *post_data, size_t len)) {
    send_influx_write_ptr = ptr;
}

bool send_influx_write(esp_http_client_handle_t client, char *post_data, size_t len) {
    printf("Calling callback at %p with %d bytes\n", send_influx_write_ptr, len);
    return (*send_influx_write_ptr)(client, post_data, len);
    //ESP_LOGV("send_influx_write", "Got %d bytes of data: \n>>>%.*s<<<", len, len, post_data);
    //return true;
}

#include "../main/influx_offline_buffer.h"