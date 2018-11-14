#ifndef ESP32_TFA_TH_MANCHESTER_H
#define ESP32_TFA_TH_MANCHESTER_H

#include <Arduino.h>


//https://github.com/isuruceanu/phedruino
//https://forum.arduino.cc/index.php?topic=257985.0
//http://forum.arduino.cc/index.php?topic=181452.0
//https://mchr3k.github.io/arduino-libs-manchester/


#define BUFFER_SIZE 512
#define MAX_EDGE_LENGTH 5000

//#define DEBUG_POLL
//#define DEBUG_READ
//#define MANCHESTER_YIELD Serial.println("yield");


#ifdef DEBUG_READ
#define dbg_read(s) Serial.print(s)
#else
#define dbg_read(s)
#endif

#ifndef MANCHESTER_YIELD
#define MANCHESTER_YIELD
#endif


void isr();

void receive(int pin);

int poll_edge_time();

void set_clock(int val);

bool sync_clock(int samplesCount);

void calibrate_clock();

byte decode_bit(byte previous);

byte get_previous_bit();

int read_bytes(int maxLength, byte *dataBuff);

#endif //ESP32_TFA_TH_MANCHESTER_H
