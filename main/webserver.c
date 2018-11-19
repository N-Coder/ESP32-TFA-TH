#include <esp_http_server.h>
#include <esp_log.h>
#include <tcpip_adapter.h>
#include "webserver.h"
#include "tfa.h"
#include "tasks.h"

static const char *TAG = "ESP32-TFA-TH/HTTP";

char http_resp[2048];

esp_err_t current_html_get_handler(httpd_req_t *req) {
    strcpy(http_resp, "<html><head></head><body>"
                      "<table><tr>"
                      "<th>timestamp</th>"
                      "<th>sensor type</th>"
                      "<th>session ID</th>"
                      "<th>low battery</th>"
                      "<th>channel</th>"
                      "<th>Temperature (&deg;C)</th>"
                      "<th>Humidity (%)</th>"
                      "</tr>\n");

    struct tm timeinfo;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        strcat(http_resp, "<tr><td>");
        localtime_r(&lastReadings[i].timestamp, &timeinfo);
        strftime(http_resp + strlen(http_resp), sizeof(http_resp), "%Y-%m-%d %H:%M:%S", &timeinfo);
        sprintf(http_resp + strlen(http_resp), "</td>"
                                               "<td>0x%.2X</td>"
                                               "<td>0x%.2X</td>"
                                               "<td>%c</td>"
                                               "<td>%d</td>"
                                               "<td>%f</td>"
                                               "<!-- %f -->"
                                               "<td>%d</td>"
                                               "</tr>\n", THPAYLOAD_FMT_ARGS(lastReadings[i]));
    }
    strcat(http_resp, "</table></body></html>");

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, http_resp, strlen(http_resp));
    return ESP_OK;
}

httpd_uri_t current_html = {
        .uri       = "/current.html",
        .method    = HTTP_GET,
        .handler   = current_html_get_handler
};

esp_err_t current_json_get_handler(httpd_req_t *req) {
    strcpy(http_resp, "[");

    struct tm timeinfo;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        strcat(http_resp, "{\"time\":\"");
        localtime_r(&lastReadings[i].timestamp, &timeinfo);
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
                lastReadings[i].timestamp, THPAYLOAD_FMT_ARGS(lastReadings[i]));
    }
    http_resp[strlen(http_resp) - 1] = ']';

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, http_resp, strlen(http_resp));
    return ESP_OK;
}

httpd_uri_t current_json = {
        .uri       = "/current.json",
        .method    = HTTP_GET,
        .handler   = current_json_get_handler
};


httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();


    tcpip_adapter_ip_info_t ipInfo;
    tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ipInfo);

    ESP_LOGI(TAG, "Starting server on "
            IPSTR
            ":%d", IP2STR(&(ipInfo.ip)), config.server_port);
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    ESP_LOGD(TAG, "Registering URI handlers");
    httpd_register_uri_handler(server, &current_html);
    httpd_register_uri_handler(server, &current_json);
    return server;
}
