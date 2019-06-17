#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG // also disabled at runtime in main.c

#include <esp_log.h>
#include "tfa.h"

static const char *TAG = "ESP32-TFA-TH/RF-TFA";


// checksum code from BaronVonSchnowzer at
// http://forum.arduino.cc/index.php?topic=214436.15
char checksum(size_t length, char *buff) {
    char mask = 0x7C;
    char checksum = 0x64;
    char data;
    int byteCnt;

    for (byteCnt = 0; byteCnt < length; byteCnt++) {
        int bitCnt;
        data = buff[byteCnt];
        for (bitCnt = 7; bitCnt >= 0; bitCnt--) {
            char bit = (char) (mask & 1); // Rotate mask right
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

inline bool skip_header_bytes(ManchesterState *state) {
    int bitCount = 0;
    bit_t bit;

    ESP_LOGV(TAG, "Decoding Header...");
    while (true) {
        bit = read_bit(state);
        bitCount++;

        if (bit == 1) {
            continue; // consume a series of "1"s
        } else if (bit == 0) {
            if (bitCount > 7) {
                // after at least 7 "1"s we read a "0" and now expect the final "1":
                // 111111101 or 00001111111111101
                bit = read_bit(state);
                if (bit == 1) {
                    ESP_LOGD(TAG, "Header complete");
                    return true;
                } else {
                    ESP_LOGV(TAG, "Missing header trailing 01");
                    return false;
                }
            } else {
                ESP_LOGV(TAG, "Too few 1s in header");
                return false;
            }
        } else {
            ESP_LOGV(TAG, "Timeout/Desync (%d) after %d header bits", bit, bitCount);
            return false;
        }

    }
}

THPayload decode_payload(char *dataBuff) {
    THPayload data;

    data.sensor_type = dataBuff[0] & 0xFF;
    data.session_id = dataBuff[1] & 0xFF;
    data.battery = (dataBuff[2] & 0b10000000) > 0;
    data.channel = ((dataBuff[2] & 0b01110000) >> 4) + 1;
    data.temp_raw = ((dataBuff[2] & 0b00001111) << 8) + (dataBuff[3] & 0xFF);
    data.temp_fahrenheit = (data.temp_raw - 400) / 10;
    data.temp_celsius = (float) ((data.temp_raw - 720) * 0.0556);
    data.humidity = dataBuff[4] & 0xFF;
    data.check_byte = (char) (dataBuff[5] & 0xFF);
    data.checksum = checksum(5, dataBuff);
    time(&data.timestamp); // this might be a few microseconds back
    data.valid = true;

    if (!(data.sensor_type == 0x45 || data.sensor_type == 0x46)) {
        ESP_LOGW(TAG, "unknown sensor type 0x%.2X received", data.sensor_type);
        data.valid = false;
    }

    if (1 > data.channel || data.channel > MAX_CHANNELS) {
        ESP_LOGW(TAG, "illegal channel id %d received", data.channel);
        data.valid = false;
    }

    if (0 > data.humidity || data.humidity > 100) {
        ESP_LOGW(TAG, "invalid humidity value %d received", data.humidity);
        data.valid = false;
    }

    if (data.checksum != data.check_byte) {
        ESP_LOGE(TAG, "got checksum 0x%.2X, but expected 0x%.2X in packet "
                      "0x%.2X 0x%.2X 0x%.2X 0x%.2X 0x%.2X 0x%.2X",
                 data.check_byte, data.checksum,
                 dataBuff[0], dataBuff[1], dataBuff[2], dataBuff[3], dataBuff[4], dataBuff[5]);
        data.valid = false;
    }

    return data;
}
