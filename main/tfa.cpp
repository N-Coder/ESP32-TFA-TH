#include "tfa.h"
#include "manchester.h"


// checksum code from BaronVonSchnowzer at
// http://forum.arduino.cc/index.php?topic=214436.15
byte checksum(int length, byte *buff) {
    byte mask = 0x7C;
    byte checksum = 0x64;
    byte data;
    int byteCnt;

    for (byteCnt = 0; byteCnt < length; byteCnt++) {
        int bitCnt;
        data = buff[byteCnt];
        for (bitCnt = 7; bitCnt >= 0; bitCnt--) {
            byte bit = mask & 1; // Rotate mask right
            mask = (mask >> 1) | (mask << 7);
            if (bit) {
                mask ^= 0x18;
            }

            // XOR mask into checksum if data bit is 1
            if (data & 0x80) {
                checksum ^= mask;
            }
            data <<= 1;
        }
    }
    return checksum;
}

bool skip_header_bytes() {
    int bitCount = 0;
    byte bit = get_previous_bit();

    dbg_read("Decoding Header...\n ");
    while (true) {
        bit = decode_bit(bit);
        bitCount++;

        if (bit == 1) {
            continue; // consume a series of "1"s
        } else if (bit == 0) {
            if (bitCount > 7) {
                // after at least 7 "1"s we read a "0" and now expect the final "1":
                // 111111101 or 00001111111111101
                bit = decode_bit(bit);
                if (bit == 1) {
                    dbg_read("  Header complete\n");
                    return true;
                } else {
                    dbg_read("  Missing header trailing 01\n");
                    return false;
                }
            } else {
                dbg_read("  Too few 1s in header\n");
                return false;
            }
        } else {
            dbg_read("  Timeout/Desync\n\n\n");
            return false;
        }

    }
}

THPayload decode_payload(byte *dataBuff) {
    THPayload data{};

    data.sensor_type = dataBuff[0] & 0xFF;
    data.session_id = dataBuff[1] & 0xFF;
    data.battery = (dataBuff[2] & 0b10000000) > 0;
    data.channel = ((dataBuff[2] & 0b01110000) >> 4) + 1;
    data.temp_raw = ((dataBuff[2] & 0b00001111) << 8) + (dataBuff[3] & 0xFF);
    data.temp_fahrenheit = (data.temp_raw - 400) / 10;
    data.temp_celsius = (data.temp_raw - 720) * 0.0556;
    data.humidity = dataBuff[4] & 0xFF;
    data.check_byte = dataBuff[5] & 0xFF;
    data.checksum = checksum(5, dataBuff);

    return data;
}

THPayload print_payload(THPayload data) {
    if (!(data.sensor_type == 0x45 || data.sensor_type == 0x46)) {
        Serial.print("WARN: unknown sensor type ");
        Serial.print(data.sensor_type);
        Serial.println(" received");
    }

    if (data.channel < 1 || data.channel > MAX_CHANNELS) {
        Serial.print("WARN: illegal channel id ");
        Serial.print(data.channel);
        Serial.println(" received");
    }

    if (data.humidity > 100) {
        Serial.print("WARN: invalid humidity value ");
        Serial.print(data.humidity);
        Serial.println(" received");
    }

    if (data.checksum != data.check_byte) {
        Serial.print("ERR: got checksum ");
        Serial.print(data.check_byte, BIN);
        Serial.print(" but expected ");
        Serial.println(data.checksum, BIN);
    }

    Serial.print("Sensor type: 0x");
    Serial.print(data.sensor_type, HEX);
    Serial.print("  Session ID: 0x");
    Serial.print(data.session_id, HEX);
    Serial.print("  Low battery: ");
    Serial.print(data.battery, BIN);
    Serial.print("  Channel: ");
    Serial.print(data.channel);
    Serial.print("  Temperature: ");
    // Serial.print(temp_raw, BIN);
    // Serial.print(" RAW / ");
    Serial.print(data.temp_celsius);
    Serial.print("°C / ");
    Serial.print(data.temp_fahrenheit);
    Serial.print("°F  Humidity: ");
    Serial.print(data.humidity);
    Serial.println();
}