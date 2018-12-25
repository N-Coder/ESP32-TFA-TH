#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG // also disabled at runtime in main.c

#include "manchester.h"

#include <esp_log.h>

static const char *TAG = "ESP32-TFA-TH/RF-PE";

static size_t rmt_get_mem_len(rmt_channel_t channel) {
    int block_num = RMT.conf_ch[channel].conf0.mem_size;
    int item_block_len = block_num * RMT_MEM_ITEM_NUM;
    volatile rmt_item32_t *data = RMTMEM.chan[channel].data32;
    size_t idx;
    for (idx = 0; idx < item_block_len; idx++) {
        if (data[idx].duration0 == 0) {
            return idx;
        } else if (data[idx].duration1 == 0) {
            return idx + 1;
        }
    }
    return idx;
}

static void fetch_rmt_data(void *args) {
    ManchesterState *state = args;
    rmt_channel_t channel = state->config.rmt_channel;

    portENTER_CRITICAL(&state->mux);
    RMT.conf_ch[channel].conf1.rx_en = 0;
    RMT.conf_ch[channel].conf1.mem_owner = RMT_MEM_OWNER_TX; //change memory owner to protect data.

    size_t item_len = rmt_get_mem_len(channel);
    BaseType_t res = xRingbufferSend(state->buffer, (void *) RMTMEM.chan[channel].data32, item_len * 4, 0);
    if (res == pdFALSE) {
        ESP_LOGE(TAG, "RMT RX BUFFER FULL (got %d, but only %d free)", item_len * 4,
                 xRingbufferGetCurFreeSize(state->buffer));
    }
    RMT.conf_ch[channel].conf1.mem_wr_rst = 1;

    RMT.conf_ch[channel].conf1.mem_owner = RMT_MEM_OWNER_RX;
    RMT.conf_ch[channel].conf1.rx_en = 1;
    portEXIT_CRITICAL(&state->mux);
}

ManchesterState *manchester_start_receive(ManchesterConfig *config) {
    ManchesterState *state = malloc(sizeof(ManchesterState));
    state->config = *config;

    rmt_config_t rmt_rx;
    rmt_rx.channel = config->rmt_channel;
    rmt_rx.gpio_num = config->gpio_pin;
    rmt_rx.clk_div = RMT_CLK_DIV;
    rmt_rx.mem_block_num = 8; // all 512/64 blocks
    rmt_rx.rmt_mode = RMT_MODE_RX;
    rmt_rx.rx_config.filter_en = true;
    rmt_rx.rx_config.filter_ticks_thresh = 0xFF; // counted in source clock, not divided counter clock: 255 * 1/80 MHz ~= 3µs
    rmt_rx.rx_config.idle_threshold = (uint16_t) RMT_US_TO_TICKS(PULSE_LONG_MAX(config->clock2T));
    ESP_ERROR_CHECK(rmt_config(&rmt_rx));
    ESP_ERROR_CHECK(rmt_driver_install(rmt_rx.channel, config->buffer_size, 0));

    ESP_ERROR_CHECK(rmt_get_ringbuf_handle(config->rmt_channel, &state->buffer));

    esp_timer_create_args_t timer_args;
    timer_args.callback = fetch_rmt_data;
    timer_args.arg = state;
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = "fetch_rmt_data_timer";
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &state->timer));
    uint64_t src_ticks_until_full = ((uint64_t) (RMT.conf_ch[rmt_rx.channel].conf0.mem_size * RMT_MEM_ITEM_NUM * 2))
                                    * (rmt_rx.rx_config.filter_ticks_thresh * 100); //avg pulse length is 300 µs
    ESP_ERROR_CHECK(esp_timer_start_periodic(state->timer, RMT_TICKS_TO_US(src_ticks_until_full)));

    state->mux = (portMUX_TYPE) portMUX_INITIALIZER_UNLOCKED;
    state->last_value = 0;
    state->rx_items = NULL;
    state->rx_size = 0;
    state->rx_offset = 0;
    state->rx_value0_read = false;

    ESP_ERROR_CHECK(rmt_rx_start(rmt_rx.channel, true));
    return state;
}

void manchester_stop_receive(ManchesterState *state) {
    ESP_ERROR_CHECK(rmt_rx_stop(state->config.rmt_channel));
    ESP_ERROR_CHECK(esp_timer_stop(state->timer));
    ESP_ERROR_CHECK(esp_timer_delete(state->timer));
    ESP_ERROR_CHECK(rmt_driver_uninstall(state->config.rmt_channel));
    free(state);
}


inline pulsevalue_t read_pulsevalue(ManchesterState *state) {
    if (state->rx_size - state->rx_offset <= 0) {
        if (state->rx_items != NULL) {
            vRingbufferReturnItem(state->buffer, (void *) state->rx_items);
        }
        ESP_LOGV(TAG, "Consumed %d pulses, buffer has space for %d", state->rx_offset,
                 xRingbufferGetCurFreeSize(state->buffer));
        state->rx_items = (rmt_item32_t *) xRingbufferReceive(state->buffer, &state->rx_size, portMAX_DELAY);
        ESP_LOGV(TAG, "Got %d new pulses from buffer, which now has space for %d", state->rx_size,
                 xRingbufferGetCurFreeSize(state->buffer));
        state->rx_offset = 0;
        state->rx_value0_read = false;
    }
    pulsevalue_t value;
    if (!state->rx_value0_read) {
        state->rx_value0_read = true;
        value = (pulsevalue_t) (((state->rx_items + state->rx_offset)->val >> 16) & 0xFFFF);
    } else {
        state->rx_value0_read = false;
        state->rx_offset += 1;
        value = (pulsevalue_t) ((state->rx_items + state->rx_offset)->val & 0xFFFF);
    }
    ESP_LOGV(TAG, "Pulse %d %s: %lld µs", GET_PULSE_INPUT_LEVEL(value),
             IS_PULSE_SHORT(value, state->config.clock2T) ? "short" :
             (IS_PULSE_LONG(value, state->config.clock2T) ? "long" : "invalid"),
             GET_PULSE_LENGTH(value));

    // length = 0 -> incomplete packet or packet end marker: INVALID_BIT_LIMITS
    // consecutive same level -> pulse in between filtered out: INVALID_BIT_DESYNC // TODO merge or drop?
    // length out of range: INVALID_BIT_LIMITS
    // (unpaired short interval: INVALID_BIT_UNPAIRED)

    state->last_value = value;
    return value;
}

inline bit_t read_bit(ManchesterState *state) {
    pulsevalue_t pulse_value = read_pulsevalue(state);

    if (IS_PULSE_LONG(pulse_value, state->config.clock2T)) {
        return GET_PULSE_INPUT_LEVEL(pulse_value);

    } else if (IS_PULSE_SHORT(pulse_value, state->config.clock2T)) {
        pulsevalue_t pulse2_value = read_pulsevalue(state);

        if (IS_PULSE_SHORT(pulse2_value, state->config.clock2T)) {
            return GET_PULSE_INPUT_LEVEL(pulse2_value);
        } else if (IS_PULSE_LONG(pulse2_value, state->config.clock2T)) {
            return INVALID_BIT_UNPAIRED; // unpaired second short time, probably missed the first one
        }
    }

    return INVALID_BIT_LIMITS; // else edge time outside limits
}

size_t read_bytes(ManchesterState *state, size_t maxLength, bit_t *dataBuff) {
    ESP_LOGD(TAG, "Decoding %d bytes of data...", maxLength);
    size_t bitCount = 0, byteCount = 0;
    bit_t bit;

    while (byteCount < maxLength) {
        if (bitCount == 0) {
            dataBuff[byteCount] = 0;
        }

        bit = read_bit(state);
        bitCount++;

        if (bit == 1) {
            dataBuff[byteCount] <<= 1;
            dataBuff[byteCount] |= 0x01;
        } else if (bit == 0) {
            dataBuff[byteCount] <<= 1;
        } else {
            ESP_LOGD(TAG, "Timeout/Desync (%d) after %d bytes, %d bits", bit, byteCount, bitCount);
            break;
        }

        if (bitCount == CHAR_BIT) {
            bitCount = 0;
            byteCount++;
        }
    }
    if (byteCount == maxLength) {
        ESP_LOGD(TAG, "Done");
    }
    return byteCount * CHAR_BIT + bitCount;
}

int64_t sync_clock(ManchesterState *state, int samplesCount, int min_pulse_length, int max_pulse_length) {
    int64_t clockT = 0, tmp = 0, average = 0, count = 0;

    clockT = GET_PULSE_LENGTH(read_pulsevalue(state));
    while (count < samplesCount) {
        tmp = GET_PULSE_LENGTH(read_pulsevalue(state));
        if (min_pulse_length < tmp && tmp < max_pulse_length) {
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
            }
        }
    }

    return clockT;
}
