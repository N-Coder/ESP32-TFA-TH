#ifndef ESP32_TFA_TH_MANCHESTER_H
#define ESP32_TFA_TH_MANCHESTER_H

//https://github.com/isuruceanu/phedruino
//https://forum.arduino.cc/index.php?topic=257985.0
//http://forum.arduino.cc/index.php?topic=181452.0
//https://mchr3k.github.io/arduino-libs-manchester/

#include <driver/gpio.h>

#define BUFFER_SIZE 1024
#define MAX_EDGE_LENGTH 5000

typedef char bit_t;
typedef int64_t timespan_t;

void receive(gpio_num_t pin);

timespan_t poll_edge_time();

void set_clock(timespan_t val);

bool sync_clock(int samplesCount);

bit_t decode_bit(bit_t previous);

bit_t get_previous_bit();

size_t read_bytes(size_t maxLength, bit_t *dataBuff);

#endif //ESP32_TFA_TH_MANCHESTER_H
