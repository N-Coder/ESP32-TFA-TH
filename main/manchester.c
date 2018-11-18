#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/portmacro.h>
#include <freertos/projdefs.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <limits.h>

#include "manchester.h"

QueueHandle_t buffer;
volatile int64_t lastEdgeTime = 0;
bit_t previousBit = 0;

timespan_t clock2T, limitShortMin = 0, limitShortMax = 0, limitLongMin = 0, limitLongMax = 0;

static const char *TAG = "ESP32-TFA-TH/RF-PE";
static const char *TAG_BIT = "ESP32-TFA-TH/RF-PE/Bits";


static void gpio_isr_handler(void *arg) {
    int64_t t = esp_timer_get_time();
    timespan_t val = t - lastEdgeTime;
    if (xQueueSendFromISR(buffer, &val, NULL) != pdTRUE) {
        // FIXME can't be called from ISR
        ESP_LOGE(TAG, "!!! manchester receive buffer overrun !!!");
    }
    lastEdgeTime = t;
}

void receive(gpio_num_t pin) {
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.pin_bit_mask = (1ULL << pin);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    buffer = xQueueCreate(BUFFER_SIZE, sizeof(timespan_t));
    ESP_ERROR_CHECK(buffer == NULL ? ESP_ERR_NO_MEM : ESP_OK);
    lastEdgeTime = esp_timer_get_time();
    previousBit = (bit_t) ((gpio_get_level(pin) + 1) % 2);

    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_EDGE | ESP_INTR_FLAG_LEVEL1));
    ESP_ERROR_CHECK(gpio_isr_handler_add(pin, gpio_isr_handler, NULL));
}

timespan_t poll_edge_time() {
    timespan_t val;
    BaseType_t ret = xQueueReceive(buffer, &val, portMAX_DELAY);
    ESP_ERROR_CHECK(ret != pdTRUE ? ESP_ERR_TIMEOUT : ESP_OK);
    previousBit = (bit_t) ((previousBit + 1) % 2);
    return val;
}

bit_t get_previous_bit() {
    return previousBit;
}


void set_clock(timespan_t val) {
    clock2T = val;
    limitShortMin = (timespan_t) (clock2T * 0.25); // Compute low T limit
    limitShortMax = (timespan_t) (clock2T * 0.75); // Compute high T limit
    limitLongMin = (timespan_t) (clock2T * 0.75); // Compute low 2T limit
    limitLongMax = (timespan_t) (clock2T * 1.25); // Compute high 2T limit
    ESP_LOGD(TAG, "Set clock to clock2T = %lld, %lld < short < %lld, %lld < long < %lld",
             clock2T, limitShortMin, limitShortMax, limitLongMin, limitLongMax);
}


bool sync_clock(int samplesCount) {
    timespan_t clockT = 0, tmp = 0, average = 0, count = 0;

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


bit_t decode_bit(bit_t previous) {
    timespan_t tmp = poll_edge_time();
    if (tmp < MAX_EDGE_LENGTH) {
        if ((tmp > limitLongMin) && (tmp < limitLongMax)) { // long
            previous = (bit_t) (previous ^ 0x01); // invert previous for logical change
            assert(previous == get_previous_bit());
            ESP_LOGV(TAG_BIT, "%d (%lld %d)", previous, tmp, get_previous_bit());
            return previous;

        } else if ((tmp > limitShortMin) && (tmp < limitShortMax)) { // short
            timespan_t tmp2 = poll_edge_time();

            if ((tmp2 > limitShortMin) && (tmp2 < limitShortMax)) {
                assert(previous == get_previous_bit());
                ESP_LOGV(TAG_BIT, "%d (%lld %d) (%lld %d)", previous, tmp, ((get_previous_bit() + 1) % 2),
                         tmp2, get_previous_bit());
                return previous; // bit stays the same

            } else { // else un-paired short time
                ESP_LOGV(TAG_BIT, "2 (%lld %d) (%lld %d)", tmp, ((get_previous_bit() + 1) % 2),
                         tmp2, get_previous_bit());
                return 2;
            }
        }  // else edge time outside limits
    }
    ESP_LOGV(TAG_BIT, "3 (%lld %d)", tmp, get_previous_bit());
    return 3;
}

size_t read_bytes(size_t maxLength, bit_t *dataBuff) {
    ESP_LOGV(TAG, "Decoding %d bytes of data...", maxLength);
    size_t bitCount = 0, byteCount = 0;
    bit_t bit = get_previous_bit();

    while (byteCount < maxLength) {
        if (bitCount == 0) {
            dataBuff[byteCount] = 0;
        }

        bit = decode_bit(bit);
        bitCount++;

        if (bit == 1) {
            dataBuff[byteCount] <<= 1;
            dataBuff[byteCount] |= 0x01;
        } else if (bit == 0) {
            dataBuff[byteCount] <<= 1;
        } else {
            ESP_LOGV(TAG, "Timeout/Desync");
            break;
        }

        if (bitCount == 8) {
            bitCount = 0;
            byteCount++;
        }
    }
    if (byteCount == maxLength) {
        ESP_LOGV(TAG, "Done");
    }
    return byteCount * 8 + bitCount;
}
