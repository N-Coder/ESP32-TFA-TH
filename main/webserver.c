#include "webserver.h"
#include <freertos/task.h>
#include <esp_log.h>
#include <tcpip_adapter.h>
#include <dirent.h>

#include "task_influx_sender.h"
#include "task_sd_writer.h"

static const char *TAG = "ESP32-TFA-TH/HTTP";
static time_t start_timestamp;
char http_resp[1024 * 10];

#include "webserver_dump_debug.c"
#include "webserver_sd_file.c"

esp_err_t debug_html_get_handler(httpd_req_t *req) {
    strcpy(http_resp, "<html><head></head><body>\n"
                      "<style>"
                      "td,th {border: 1px solid black; padding: 5px}\n"
                      "table {border-collapse: collapse}\n"
                      "dt {font-weight: bold}\n"
                      "#t1 tr td:nth-child(n+3) {text-align: right}\n"
                      "#t2 tr td:nth-child(-n+3) {vertical-align: top}\n"
                      ".t3 tr td:nth-child(2) {text-align: right}\n"
                      "</style>");
    TFATaskManagerState *tfaState = req->user_ctx;

    UBaseType_t numberOfTasks = uxTaskGetNumberOfTasks();
    TaskStatus_t *tasks = malloc(sizeof(TaskStatus_t) * numberOfTasks);
    uint32_t totalRunTime;
    UBaseType_t retrievedTasks = uxTaskGetSystemState(tasks, numberOfTasks, &totalRunTime);

    if (retrievedTasks != numberOfTasks) {
        sprintf(http_resp + strlen(http_resp),
                "<h1>Could not retrieve FreeRTOS tasks - got %d of %d tasks states!</h1>\n",
                retrievedTasks,
                numberOfTasks
        );
    } else {
        dump_tasks(numberOfTasks, tasks, totalRunTime);
        dump_tfa_tasks(tfaState, numberOfTasks, tasks);
    }
    dump_various(tfaState, numberOfTasks, totalRunTime);
    free(tasks);

    strcat(http_resp, "</body></html>");

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, http_resp, strlen(http_resp));
    return ESP_OK;
}

esp_err_t current_html_get_handler(httpd_req_t *req) {
    TFATaskManagerState *tfaState = req->user_ctx;
    strcpy(http_resp, "<html><head></head><body>"
                      "<table><tr>"
                      "<th>timestamp</th>"
                      "<th>sensor type</th>"
                      "<th>session ID</th>"
                      "<th>low battery</th>"
                      "<th>channel</th>"
                      "<th>temperature (&deg;C)</th>"
                      "<th>humidity (%)</th>"
                      "</tr>\n");

    struct tm timeinfo;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        strcat(http_resp, "<tr><td>");
        localtime_r(&tfaState->lastReadings[i].timestamp, &timeinfo);
        strftime(http_resp + strlen(http_resp), sizeof(http_resp), "%Y-%m-%d %H:%M:%S", &timeinfo);
        sprintf(http_resp + strlen(http_resp), "</td>"
                                               "<td>0x%.2X</td>"
                                               "<td>0x%.2X</td>"
                                               "<td>%c</td>"
                                               "<td>%d</td>"
                                               "<td>%f</td>"
                                               "<!-- %f -->"
                                               "<td>%d</td>"
                                               "</tr>\n", THPAYLOAD_FMT_ARGS(tfaState->lastReadings[i]));
    }
    strcat(http_resp, "</table>Running since: ");
    localtime_r(&start_timestamp, &timeinfo);
    strftime(http_resp + strlen(http_resp), sizeof(http_resp), "%Y-%m-%d %H:%M:%S", &timeinfo);
    strcat(http_resp, "</body></html>");

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, http_resp, strlen(http_resp));
    return ESP_OK;
}

esp_err_t current_json_get_handler(httpd_req_t *req) {
    TFATaskManagerState *tfaState = req->user_ctx;
    strcpy(http_resp, "[");

    struct tm timeinfo;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        strcat(http_resp, "{\"time\":\"");
        localtime_r(&tfaState->lastReadings[i].timestamp, &timeinfo);
        strftime(http_resp + strlen(http_resp), sizeof(http_resp), "%Y-%m-%d %H:%M:%S", &timeinfo);
        sprintf(http_resp + strlen(http_resp),
                "\", \"timestamp\":%ld,"
                "\"sensor\": \"0x%.2X\","
                "\"session\": \"0x%.2X\","
                "\"low_batt\": %c,"
                "\"channel\": %d,"
                "\"tempC\": %f,"
                "\"tempF\": %f,"
                "\"humidity\": %d},",
                tfaState->lastReadings[i].timestamp, THPAYLOAD_FMT_ARGS(tfaState->lastReadings[i]));
    }
    http_resp[strlen(http_resp) - 1] = ']';

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, http_resp, strlen(http_resp));
    return ESP_OK;
}


void set_httpd_log_level(esp_log_level_t level) {
    esp_log_level_set("httpd", level);
    esp_log_level_set("httpd_parse", level);
    esp_log_level_set("httpd_sess", level);
    esp_log_level_set("httpd_txrx", level);
    esp_log_level_set("httpd_uri", level);
}

httpd_handle_t start_webserver(TFATaskManagerState *state) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    tcpip_adapter_ip_info_t ipInfo;
    tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ipInfo);
    ESP_LOGI(TAG, "Starting server on http://"
            IPSTR
            ":%d", IP2STR(&(ipInfo.ip)), config.server_port);
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    time(&start_timestamp);

    httpd_uri_t current_html = {
            .uri       = "/current.html",
            .method    = HTTP_GET,
            .handler   = current_html_get_handler,
            .user_ctx  = state
    };
    httpd_uri_t current_json = {
            .uri       = "/current.json",
            .method    = HTTP_GET,
            .handler   = current_json_get_handler,
            .user_ctx  = state
    };
    httpd_uri_t debug = {
            .uri       = "/debug.html",
            .method    = HTTP_GET,
            .handler   = debug_html_get_handler,
            .user_ctx  = state
    };

    ESP_LOGD(TAG, "Registering URI handlers");
    httpd_register_uri_handler(server, &current_html);
    httpd_register_uri_handler(server, &current_json);
    httpd_register_uri_handler(server, &debug);
    httpd_register_uri_handler(server, &sd_file);
    httpd_register_uri_handler(server, &sd_file_head);
    httpd_register_uri_handler(server, &sd_file_options);
    set_httpd_log_level(ESP_LOG_INFO);
    return server;
}
