/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _PICO_AUDIO_I2S_H
#define _PICO_AUDIO_I2S_H

#include "pico/audio.h"
#include "pico/pico_host_sdl.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PICO_AUDIO_I2S_DATA_PIN
#define PICO_AUDIO_I2S_DATA_PIN 0
#endif

#ifndef PICO_AUDIO_I2S_CLOCK_PIN_BASE
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 0
#endif

typedef struct audio_i2s_config {
    uint8_t data_pin;
    uint8_t clock_pin_base;
    uint8_t dma_channel;
    uint8_t pio_sm;
} audio_i2s_config_t;

// todo i2s used here, some of it is common
const struct audio_format *audio_i2s_setup(const struct audio_format *intended_audio_format,
                                               const struct audio_i2s_config *config);

// todo make a common version (or a macro) .. we don't want to pull in unnecessary code by default
bool audio_i2s_connect(struct audio_buffer_pool *producer);
bool audio_i2s_connect_s8(struct audio_buffer_pool *producer);
bool audio_i2s_connect_extra(struct audio_buffer_pool *producer, bool buffer_on_give, uint buffer_count, uint samples_per_buffer, audio_connection_t *connection);

void audio_i2s_set_enabled(bool enabled);

#ifdef __cplusplus
}
#endif

#endif //_AUDIO_I2S_H
