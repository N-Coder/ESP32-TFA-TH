#ifndef ESP32_TFA_TEMP_HUM_TASKS_H
#define ESP32_TFA_TEMP_HUM_TASKS_H

#include "tfa.h"

extern THPayload lastReadings[MAX_CHANNELS];

void start_loops();

#endif //ESP32_TFA_TEMP_HUM_TASKS_H
