#include "manchester.h"
#include "tfa.h"

#define PIN 0

byte dataBuff[DATA_BYTES];
THPayload[MAX_CHANNELS]
last_readings;

void setup() {
    receive(PIN);
    set_clock(976);

    Serial.begin(115200);
    Serial.println("start");
}


void loop() {
    if (!skip_header_bytes()) {
        return;
    }

    if (read_bytes(DATA_BYTES, dataBuff) != DATA_BYTES * 8) {
        return;
    }

    // TODO consume trailing 0s

    THPayload data = decode_payload(dataBuff);
    print_payload(data);
    if (data.checksum == data.check_byte) {
        last_readings[data.channel] = data;
    }
}