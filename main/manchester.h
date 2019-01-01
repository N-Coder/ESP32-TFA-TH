#ifndef ESP32_TFA_TH_MANCHESTER_H
#define ESP32_TFA_TH_MANCHESTER_H

//https://github.com/isuruceanu/phedruino
//https://forum.arduino.cc/index.php?topic=257985.0
//https://forum.arduino.cc/index.php?topic=181452.0
//https://mchr3k.github.io/arduino-libs-manchester/
//http://ww1.microchip.com/downloads/en/AppNotes/Atmel-9164-Manchester-Coding-Basics_Application-Note.pdf

//https://www.mikrocontroller.net/topic/320249#3768074
//http://www.mantech.co.za/ProductInfo.aspx?Item=15M0436

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <driver/timer.h>
#include <driver/rmt.h>
#include <esp_timer.h>

typedef char bit_t;
typedef uint16_t pulsevalue_t;

#define RMT_CLK_DIV            100 // RMT counter clock divider
#define RMT_US_TO_TICKS(us)    ((us) * APB_CLK_FREQ / RMT_CLK_DIV / 1000000)
#define RMT_TICKS_TO_US(ticks) ((ticks) * RMT_CLK_DIV * 1000000 / APB_CLK_FREQ)

#define PULSE_INPUT_LEVEL_FLAG (1ULL << (sizeof(pulsevalue_t) * CHAR_BIT - 1))

#define GET_PULSE_INPUT_LEVEL(val) (((val) & PULSE_INPUT_LEVEL_FLAG) != 0)
#define GET_PULSE_LENGTH(val) (RMT_TICKS_TO_US((val) & ~(PULSE_INPUT_LEVEL_FLAG)))

#define PULSE_SHORT_MIN(clock2T) ((int64_t) ((clock2T) * 0.25))
#define PULSE_SHORT_MAX(clock2T) ((int64_t) ((clock2T) * 0.75))
#define PULSE_LONG_MIN(clock2T)  ((int64_t) ((clock2T) * 0.75))
#define PULSE_LONG_MAX(clock2T)  ((int64_t) ((clock2T) * 1.25))

#define IS_PULSE_SHORT(length, clock2T) ((GET_PULSE_LENGTH(length) > PULSE_SHORT_MIN(clock2T)) && (GET_PULSE_LENGTH(length) <= PULSE_SHORT_MAX(clock2T)))
#define IS_PULSE_LONG(length, clock2T)  ((GET_PULSE_LENGTH(length) > PULSE_LONG_MIN(clock2T))  && (GET_PULSE_LENGTH(length) <= PULSE_LONG_MAX(clock2T)))

#define INVALID_BIT_LIMITS   ((bit_t) 2)
#define INVALID_BIT_UNPAIRED ((bit_t) 3)
#define INVALID_BIT_DESYNC   ((bit_t) 4)
#define IS_BIT_VALID(bit) ((bit) == ((bit) & 0x01))


typedef struct {
    gpio_num_t gpio_pin;
    int64_t clock2T;
    rmt_channel_t rmt_channel;
    uint8_t rmt_mem_block_num;
    size_t buffer_size;
    void *user_data;

} ManchesterConfig;

typedef struct {
    ManchesterConfig config;

    RingbufHandle_t buffer;
    esp_timer_handle_t timer;
    portMUX_TYPE mux;

    pulsevalue_t last_value;
    rmt_item32_t *rx_items;
    size_t rx_size;
    size_t rx_offset;
    bool rx_value0_read;
} ManchesterState;

ManchesterState *manchester_start_receive(ManchesterConfig *config);

void manchester_stop_receive(ManchesterState *state);

pulsevalue_t read_pulsevalue(ManchesterState *state);

bit_t read_bit(ManchesterState *state);

size_t read_bytes(ManchesterState *state, size_t maxLength, bit_t *dataBuff);

int64_t sync_clock(ManchesterState *state, int samplesCount, int min_pulse_length, int max_pulse_length);

#endif //ESP32_TFA_TH_MANCHESTER_H
