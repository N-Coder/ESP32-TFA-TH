#ifndef ESP32_TFA_TH_TFA_H
#define ESP32_TFA_TH_TFA_H

#include <Arduino.h>


//https://github.com/zwobbl/froggit-read/blob/master/froggitread.c
//https://forum.pilight.org/showthread.php?tid=3225
//https://forum.fhem.de/index.php/topic,65680.90.html


#define MAX_CHANNELS 8
#define DATA_BYTES 6

struct THPayload {
    int sensor_type;
    int session_id;
    int battery;
    int channel;
    int temp_raw;
    float temp_fahrenheit;
    float temp_celsius;
    int humidity;
    byte check_byte;
    byte checksum;
};


byte checksum(int length, byte *buff);

bool skip_header_bytes();

THPayload decode_payload(byte *dataBuff);

THPayload print_payload(THPayload data);

#endif //ESP32_TFA_TH_TFA_H
