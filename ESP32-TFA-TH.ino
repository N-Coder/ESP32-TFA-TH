#include <WiFi.h>
#include "wifi_credentials.h"
#include <NTPClient.h>
#include <WebServer.h>
#include "manchester.h"
#include "tfa.h"

#define PIN 0

byte dataBuff[DATA_BYTES];
THPayload lastReadings[MAX_CHANNELS];
unsigned long lastReadingsTime[MAX_CHANNELS];


WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);


WebServer server(80);


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

    server.on("/", handleRoot);
    server.begin();

    receive(PIN);
    set_clock(976);
}


void loop() {
    timeClient.update();

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

            Serial.print(timeClient.getFormattedTime());
            Serial.print(": ");
            print_payload(data);
        }
    }

    server.handleClient();
}