#include <WiFi.h>
#include "wifi_credentials.h"
#include <NTPClient.h>
#include "manchester.h"
#include "tfa.h"

#define PIN 0

byte dataBuff[DATA_BYTES];
THPayload lastReadings[MAX_CHANNELS];
unsigned long lastReadingsTime[MAX_CHANNELS];


WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

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
        if (t - lastReadingsTime[data.channel] > 10) {
            lastReadings[data.channel] = data;
            lastReadingsTime[data.channel] = t;

            Serial.print(timeClient.getFormattedTime());
            Serial.print(": ");
            print_payload(data);
        }
    }
}