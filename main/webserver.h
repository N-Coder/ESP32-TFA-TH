#ifndef ESP32_TFA_TEMP_HUM_WEBSERVER_H
#define ESP32_TFA_TEMP_HUM_WEBSERVER_H

#include <esp_http_server.h>
#include "tasks.h"

httpd_handle_t start_webserver(TFATaskManagerState *state);

#endif //ESP32_TFA_TEMP_HUM_WEBSERVER_H
