#include <esp_http_server.h>
#include <esp_log.h>
#include <tcpip_adapter.h>
#include <dirent.h>
#include "webserver.h"
#include "tfa.h"
#include "tasks.h"

static const char *TAG = "ESP32-TFA-TH/HTTP";
static time_t start_timestamp;
char http_resp[2048];

esp_err_t current_html_get_handler(httpd_req_t *req) {
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
    strcat(http_resp, "</table>Running since: ");
    localtime_r(&start_timestamp, &timeinfo);
    strftime(http_resp + strlen(http_resp), sizeof(http_resp), "%Y-%m-%d %H:%M:%S", &timeinfo);
    strcat(http_resp, "</body></html>");

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


esp_err_t sd_file_get_handler(httpd_req_t *req) {
    esp_err_t ret;
    char filename[24] = "/sdcard/";
    char length_str[12];
    FILE *f;
    struct stat st;


    ret = httpd_req_get_url_query_str(req, http_resp, sizeof(http_resp));
    if (ret == ESP_OK) {
        ret = httpd_query_key_value(http_resp, "file", filename + strlen(filename),
                                    sizeof(filename) - strlen(filename));
    }
    if (ret == ESP_ERR_NOT_FOUND) {
        strcpy(http_resp, "<html><head></head><body>"
                          "<ul>\n");

        struct dirent *pDirent;
        DIR *pDir = opendir("/sdcard/");
        if (pDir == NULL) {
            ESP_ERROR_CHECK(httpd_resp_send_404(req));
            return ESP_OK;
        }
        while ((pDirent = readdir(pDir)) != NULL) {
            sprintf(http_resp + strlen(http_resp), "<li>%s</li>\n", pDirent->d_name);
        }
        closedir(pDir);
        strcat(http_resp, "</ul></body></html>");

        ESP_ERROR_CHECK(httpd_resp_send(req, http_resp, strlen(http_resp)));
        return ESP_OK;
    } else if (ret == ESP_ERR_HTTPD_RESULT_TRUNC) {
        ESP_ERROR_CHECK(httpd_resp_send_404(req));
        return ESP_OK;
    }
    ESP_ERROR_CHECK(ret);

    if (stat(filename, &st) == 0 && S_ISREG(st.st_mode)) {
        ESP_LOGV(TAG, "Delivering file %s containing %ld bytes", filename, st.st_size);
        if (strcmp(filename + 8, ".CSV") == 0) {
            httpd_resp_set_type(req, "text/csv");
        } else {
            httpd_resp_set_type(req, "text/plain");
        }
        sprintf(length_str, "%ld", st.st_size);
        httpd_resp_set_hdr(req, "Content-Length", length_str);
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    } else {
        ESP_ERROR_CHECK(httpd_resp_send_404(req));
        return ESP_OK;
    }

    if (req->method == HTTP_HEAD) {
        ESP_ERROR_CHECK(httpd_resp_send_chunk(req, NULL, 0));
        ESP_LOGV(TAG, "Sent only headers after HEAD request.");
        return ESP_OK;
    }

    f = fopen(filename, "r");
    if (f == NULL) {
        ESP_ERROR_CHECK(httpd_resp_send_404(req));
        return ESP_OK;
    }

    // TODO Content-Range
    size_t sent = 0;
    while (sent < st.st_size) {
        http_resp[0] = 0;
        size_t len = 0;
        while (len < 0.8 * sizeof(http_resp) && sent + len < st.st_size) {
            if (fgets(http_resp + len, sizeof(http_resp) - len, f) == NULL) {
                fclose(f);
                ESP_ERROR_CHECK(httpd_resp_send_404(req));
                return ESP_OK;
            }
            size_t newlen = strlen(http_resp);
            ESP_LOGV(TAG,
                     "Got %u new bytes of data. Buffer already contains %u of %u bytes. Already sent %u of %ld bytes.",
                     newlen - len, newlen, sizeof(http_resp), sent, st.st_size);
            len = newlen;
        }
        ESP_ERROR_CHECK(httpd_resp_send_chunk(req, http_resp, len));
        sent += len;
    }
    ESP_ERROR_CHECK(httpd_resp_send_chunk(req, NULL, 0));
    fclose(f);
    ESP_LOGV(TAG, "Finished sending %u of %ld bytes.", sent, st.st_size);
    return ESP_OK;
}

httpd_uri_t sd_file = {
        .uri       = "/sd_file",
        .method    = HTTP_GET,
        .handler   = sd_file_get_handler
};
httpd_uri_t sd_file_head = {
        .uri       = "/sd_file",
        .method    = HTTP_HEAD,
        .handler   = sd_file_get_handler
};

esp_err_t cors_options_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Allow", req->user_ctx);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", req->user_ctx);
    char headers[128] = {0};
    esp_err_t ret = httpd_req_get_hdr_value_str(req, headers, "Access-Control-Request-Headers", sizeof(headers));
    ESP_LOGV(TAG, "Access-Control-Request-Headers %X %s", ret, headers);
    if (ret == ESP_OK) {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", headers);
    } else {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "if-none-match,range");
    }
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

httpd_uri_t sd_file_options = {
        .uri       = "/sd_file",
        .method    = HTTP_OPTIONS,
        .handler   = cors_options_handler,
        .user_ctx = "GET, OPTIONS, HEAD"
};

httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    time(&start_timestamp);

    tcpip_adapter_ip_info_t ipInfo;
    tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ipInfo);

    ESP_LOGI(TAG, "Starting server on http://"
            IPSTR
            ":%d", IP2STR(&(ipInfo.ip)), config.server_port);
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    ESP_LOGD(TAG, "Registering URI handlers");
    httpd_register_uri_handler(server, &current_html);
    httpd_register_uri_handler(server, &current_json);
    httpd_register_uri_handler(server, &sd_file);
    httpd_register_uri_handler(server, &sd_file_head);
    httpd_register_uri_handler(server, &sd_file_options);
    return server;
}
