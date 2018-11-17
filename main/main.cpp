#include <WiFi.h>
#include "wifi_credentials.h"
#include <NTPClient.h>
#include <WebServer.h>
#include "manchester.h"
#include "tfa.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"

#define PIN 0

byte dataBuff[DATA_BYTES];
THPayload lastReadings[MAX_CHANNELS];
unsigned long lastReadingsTime[MAX_CHANNELS];

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
WebServer server(80);

File file;
int file_open_day = -1;


void handleRoot() {
    String output = "<html><head></head><body>"
                    "<table><tr>"
                    "<th>timestamp</th>"
                    "<th>sensor type</th>"
                    "<th>session ID</th>"
                    "<th>low battery</th>"
                    "<th>channel</th>"
                    "<th>Temperature (&deg;C)</th>"
                    "<th>Humidity (%)</th>"
                    "</tr>\n";

    for (int i = 0; i < MAX_CHANNELS; i++) {
        output += "<tr><td>";
        output += String(lastReadingsTime[i]);
        output += "</td><td>";
        output += String(lastReadings[i].sensor_type, HEX);
        output += "</td><td>";
        output += String(lastReadings[i].session_id, HEX);
        output += "</td><td>";
        output += String(lastReadings[i].battery, BIN);
        output += "</td><td>";
        output += String(lastReadings[i].channel);
        output += "</td><td>";
        output += String(lastReadings[i].temp_celsius);
        output += "</td><td>";
        output += String(lastReadings[i].temp_fahrenheit);
        output += "</td><td>";
        output += String(lastReadings[i].humidity);
        output += "</td></tr>\n";
    }

    output += "</table></body></html>";
    server.send(200, "text/html", output);
}

void setup() {
    Serial.begin(115200);
    Serial.println("Start!");

    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    timeClient.begin();

    if (!SD.begin()) {
        Serial.println("Card Mount Failed");
        return;
    }
    Serial.print("SD Card Type: ");
    switch (SD.cardType()) {
        case CARD_NONE:
            Serial.println("No SD card attached");
            break;
        case CARD_MMC:
            Serial.println("MMC");
            break;
        case CARD_SD:
            Serial.println("SDSC");
            break;
        case CARD_SDHC:
            Serial.println("SDHC");
            break;
        default:
            Serial.println("UNKNOWN");
    }
    Serial.printf("SD Card Size: %lluMB\n", SD.cardSize() / (1024 * 1024));
    Serial.printf("Total space: %lluMB\n", SD.totalBytes() / (1024 * 1024));
    Serial.printf("Used space: %lluMB\n", SD.usedBytes() / (1024 * 1024));

    server.on("/", handleRoot);
    server.begin();

    receive(PIN);
    set_clock(976);
}


void loop() {
    timeClient.update();

    if (file_open_day != timeClient.getDay()) {
        if (file) {
            file.close();
        }
        String path = "/TH-Log." + timeClient.getFormattedDate().substring(0, 10) + ".csv";
        Serial.print("Day changed, now writing to ");
        Serial.println(path);
        file = SD.open(path, FILE_APPEND);
        file_open_day = timeClient.getDay();
        if (!file) {
            Serial.println("Failed to open file for appending");
        }
    }

    if (!skip_header_bytes()) {
        return;
    }
    if (read_bytes(DATA_BYTES, dataBuff) != DATA_BYTES * 8) {
        return;
    }
    // TODO consume trailing 0s

    THPayload data = decode_payload(dataBuff);
    if (data.checksum == data.check_byte) {
        unsigned long t = timeClient.getEpochTime();
        if (t - lastReadingsTime[data.channel - 1] > 10) {
            lastReadings[data.channel - 1] = data;
            lastReadingsTime[data.channel - 1] = t;

            Serial.print(timeClient.getFormattedTime(t));
            Serial.print(": ");
            print_payload(data);

            size_t res = file.println(
                    timeClient.getFormattedTime(t)
                    + "," + String(data.sensor_type, HEX)
                    + "," + String(data.session_id, HEX)
                    + "," + String(data.battery, BIN)
                    + "," + String(data.channel)
                    + "," + String(data.temp_celsius)
                    + "," + String(data.humidity));
            Serial.println(res);
            file.flush(); // https://github.com/espressif/arduino-esp32/issues/1293#issuecomment-386128453
        }
    }

    server.handleClient();
}