#ifndef ESP32_TFA_TH_TFA_H
#define ESP32_TFA_TH_TFA_H

//https://github.com/zwobbl/froggit-read/blob/master/froggitread.c
//https://forum.pilight.org/showthread.php?tid=3225
//https://forum.fhem.de/index.php/topic,65680.90.html

#include "manchester.h"
#include <time.h>

#define MAX_CHANNELS 8
#define DATA_BYTES 6

typedef struct {
    int sensor_type;
    int session_id;
    int battery;
    int channel;
    int temp_raw;
    float temp_fahrenheit;
    float temp_celsius;
    int humidity;
    char check_byte;
    char checksum;
    time_t timestamp;
} THPayload;

#define THPAYLOAD_FMT "Sensor type: 0x%.2X  Session ID: 0x%.2X  Low battery: %c  Channel: %d  Temperature: %f °C / %f °F  Humidity: %d%%"
#define THPAYLOAD_FMT_ARGS(data) data.sensor_type, data.session_id, data.battery & 0x01 ? '1' : '0', data.channel, data.temp_celsius, data.temp_fahrenheit, data.humidity

char checksum(size_t length, char *buff);

bool skip_header_bytes();

THPayload decode_payload(char *dataBuff);

#endif //ESP32_TFA_TH_TFA_H
