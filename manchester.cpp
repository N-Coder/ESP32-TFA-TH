#include <assert.h>
#include <limits.h>
#include "manchester.h"

volatile uint8_t readPin = 0;
volatile int buffer[BUFFER_SIZE];
volatile int readPtr, writePtr = 0;
volatile unsigned long lastEdgeTime = 0;

volatile int clock2T, limitShortMin = 0, limitShortMax = 0, limitLongMin = 0, limitLongMax = 0;


void isr() {
    unsigned long t = micros();
    if (digitalRead(readPin) != (writePtr % 2)) { // HIGH == 1, LOW == 0
        // time until fall at even indices, time until raise at odd indices
        Serial.println("jump");
        buffer[writePtr] = 0;
        writePtr = (writePtr + 1) % BUFFER_SIZE;
    }
    if (t < lastEdgeTime) { // micros() timer overflow
        buffer[writePtr] = t + (ULONG_MAX - lastEdgeTime);
    } else {
        buffer[writePtr] = t - lastEdgeTime;
    }
    writePtr = (writePtr + 1) % BUFFER_SIZE;
    lastEdgeTime = t;
    if (writePtr == readPtr) {
        Serial.println("!!! manchester receive buffer overrun !!!");
    }
}

void receive(uint8_t pin) {
    readPin = pin;
    pinMode(pin, INPUT);
    lastEdgeTime = micros();
    attachInterrupt(pin, isr, CHANGE);
}

int poll_edge_time() {
    while (readPtr == writePtr) {
        MANCHESTER_YIELD
    }
    int val = buffer[readPtr];
#ifdef DEBUG_POLL
    Serial.print(readPtr);
    Serial.print("/");
    Serial.print(writePtr);
    Serial.print(": ");
    Serial.print(val);
    Serial.print(" ");
    Serial.print(readPtr % 2);
    Serial.println();
#endif
    readPtr += 1;
    if (readPtr == BUFFER_SIZE) {
        readPtr = 0;
    }
    return val;
}

byte get_previous_bit() {
    return readPtr % 2;
}


void set_clock(int val) {
    clock2T = val;
    limitShortMin = (int) (clock2T * 0.25); // Compute low T limit
    limitShortMax = (int) (clock2T * 0.75); // Compute high T limit
    limitLongMin = (int) (clock2T * 0.75); // Compute low 2T limit
    limitLongMax = (int) (clock2T * 1.25); // Compute high 2T limit
}


bool sync_clock(int samplesCount) {
    int clockT = 0, tmp = 0, average = 0, count = 0;

    clockT = poll_edge_time();
    do {
        tmp = poll_edge_time();
        if (tmp < MAX_EDGE_LENGTH) {

            if (tmp < (clockT * 0.5)) {
                clockT = tmp;   // Time below limit
            } else if ((tmp >= (clockT * 0.5)) && (tmp <= (clockT * 1.5))) {
                average += tmp; // Accumulate
                count++;
                clockT = (average / count);
            } else if ((tmp >= (clockT * 1.5)) && (tmp <= (clockT * 2.5))) {
                average += (tmp / 2); // Accumulate but sample/2
                count++;
                clockT = (average / count);

            } else {
                clockT = 128; // Force default to 2T = 256us
                break;
            }
        } else {
            clockT = 128; // Force default to 2T = 256us
            break;
        }
    } while (count < samplesCount);

    set_clock(clockT * 2);
    return count == samplesCount;
}


void calibrate_clock() {
    Serial.println("calibrating");
    bool calibrated = false;
    while (!calibrated) {
        int samples[5];
        int avg = 0;
        for (int i = 0; i < 5; i++) {
            bool ret = false;
            while (!ret) {
                ret = sync_clock(20);
            }
            samples[i] = clock2T;
            avg += clock2T;

            Serial.print("clock2T[");
            Serial.print(i);
            Serial.print("]: ");
            Serial.println(clock2T);
        }
        avg = avg / 5;
        Serial.print("avg: ");
        Serial.println(avg);
        calibrated = true;
        for (int i = 0; i < 5; i++) {
            if (abs(samples[i] - avg) > (int) (avg * 0.1)) {

                Serial.print(i);
                Serial.print(": deviation of ");
                Serial.print(samples[i]);
                Serial.print(" from avg ");
                Serial.print(avg);
                Serial.println(" is more than 10%. ");

                calibrated = false;
            }
        }
    }

    Serial.print("clock2T: ");
    Serial.print(clock2T);
    Serial.print("  limitShortMin: ");
    Serial.print(limitShortMin);
    Serial.print("  limitShortMax: ");
    Serial.print(limitShortMax);
    Serial.print("  limitLongMin: ");
    Serial.print(limitLongMin);
    Serial.print("  limitLongMax: ");
    Serial.print(limitLongMax);
    Serial.println();
}


byte decode_bit(byte previous) {
    int tmp = poll_edge_time();
    if (tmp < MAX_EDGE_LENGTH) {
        if ((tmp > limitLongMin) && (tmp < limitLongMax)) { // long
            previous = previous ^ 0x01; // invert previous for logical change
            assert(previous == get_previous_bit());
            dbg_read(previous);
            return previous;
        } else if ((tmp > limitShortMin) && (tmp < limitShortMax)) { // short
            tmp = poll_edge_time();
            if ((tmp > limitShortMin) && (tmp < limitShortMax)) {
                assert(previous == get_previous_bit());
                dbg_read(previous);
                return previous; // bit stays the same
            } // else un-paired short time
        }  // else edge time outside limits
    }
    return 2;
}

int read_bytes(int maxLength, byte *dataBuff) {
    dbg_read("Decoding Data...\n");
    int bitCount = 0, byteCount = 0;
    byte bit = get_previous_bit();

    while (byteCount < maxLength) {
        if (bitCount == 0) {
            dataBuff[byteCount] = 0;
            dbg_read(" ");
        }

        bit = decode_bit(bit);
        bitCount++;

        if (bit == 1) {
            dataBuff[byteCount] <<= 1;
            dataBuff[byteCount] |= 0x01;
        } else if (bit == 0) {
            dataBuff[byteCount] <<= 1;
        } else {
            dbg_read("  Timeout/Desync\n\n\n");
            break;
        }

        if (bitCount == 8) {
            bitCount = 0;
            byteCount++;
        }
    }
    if (byteCount == maxLength) {
        dbg_read("  Done\n");
    }
    return byteCount * 8 + bitCount;
}
