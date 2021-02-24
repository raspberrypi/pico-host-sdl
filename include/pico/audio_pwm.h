/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _PICO_AUDIO_PWM_H
#define _PICO_AUDIO_PWM_H

#include "pico/audio.h"
#include "pico/pico_host_sdl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PICO_AUDIO_PWM_L_PIN 0
#define PICO_AUDIO_PWM_R_PIN 1

// todo we need a place to register these or just allow them to overlap, or base them on a FOURCC - this is just made up
#define AUDIO_BUFFER_FORMAT_PIO_PWM_FIRST 1000
#define AUDIO_BUFFER_FORMAT_PIO_PWM_CMD1 (AUDIO_BUFFER_FORMAT_PIO_PWM_FIRST)
#define AUDIO_BUFFER_FORMAT_PIO_PWM_CMD3 (AUDIO_BUFFER_FORMAT_PIO_PWM_FIRST+1)

struct __packed audio_pwm_channel_config {
    struct pio_audio_channel_config core;
    uint8_t pattern;
};

// can copy this to modify just the pin
extern const struct audio_pwm_channel_config default_left_channel_config;
extern const struct audio_pwm_channel_config default_right_channel_config;
extern const struct audio_pwm_channel_config default_mono_channel_config;

// max_latency_ms may be -1 (for don't care)
extern const struct audio_format *
audio_pwm_setup(const struct audio_format *intended_audio_format, int32_t max_latency_ms,
                    const struct audio_pwm_channel_config *channel_config0, ...);

// attempt a default mapping of producer buffers to pio pwm pcio_audio output
// dedicate_core_1 to have core 1 set aside entirely to do work offloading as much stuff from the producer side as possible
// todo also allow IRQ hander to do it i guess
extern bool audio_pwm_default_connect(struct audio_buffer_pool *producer_pool, bool dedicate_core_1);

extern void audio_pwm_set_enabled(bool enabled);

extern bool audio_pwm_set_correction_mode(enum audio_correction_mode mode);

extern enum audio_correction_mode audio_pwm_get_correction_mode();

#ifdef __cplusplus
}
#endif

#endif //_PIO_AUDIO_PWM_H
