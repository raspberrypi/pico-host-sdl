/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pico.h"
#include "pico/audio.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "pico/sem.h"
#include "hardware/sync.h"
#include "pico/audio_i2s.h"
#include "pico/audio_pwm.h"

#ifdef NATIVE_AUDIO_ALSA
#  include <alsa/asoundlib.h>
#endif
#ifdef NATIVE_AUDIO_SDL2
#include "SDL_image.h"
#endif

const struct audio_pwm_channel_config default_left_channel_config;
const struct audio_pwm_channel_config default_right_channel_config;
const struct audio_pwm_channel_config default_mono_channel_config;

const struct audio_format *native_audio_setup(const struct audio_format *intended_audio_format);
void native_audio_enable(bool enable);
bool native_audio_connect(struct audio_buffer_pool *producer_pool);

static struct audio_format consumer_format;
static struct audio_buffer_format consumer_buffer_format = {
    .format = &consumer_format
};

#ifdef NATIVE_AUDIO_ALSA
static int alsa_first_time = 1;
static snd_pcm_t *pcm = NULL;
static char pcmname[64];

static void close_alsa_output(void) {
    if (!pcm) return;
//    printf("Shutting down sound output\n");
    snd_pcm_drain(pcm);
    snd_pcm_close(pcm);
    pcm = NULL;
}

void native_audio_enable(bool enable) {
    snd_pcm_pause(pcm, !enable);
}

const struct audio_format *native_audio_setup(const struct audio_format *intended_audio_format)
{
    snd_pcm_hw_params_t *hw;
    snd_pcm_sw_params_t *sw;
    int err;
    unsigned int alsa_buffer_time;
    unsigned int alsa_period_time;
    unsigned int r;

    if (!pcmname[0]) {
        strcpy(pcmname, "default");
    }

    if ((err = snd_pcm_open(&pcm, pcmname, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        printf("Error: audio open error: %s\n", snd_strerror(err));
        return NULL;
    }

    snd_pcm_hw_params_alloca(&hw);

    if ((err = snd_pcm_hw_params_any(pcm, hw)) < 0) {
        printf("ERROR: No configuration available for playback: %s\n",
               snd_strerror(err));
        goto fail;
    }

    if ((err = snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        printf("Cannot set access mode: %s.\n", snd_strerror(err));
        goto fail;
    }

    // todo fix
    assert(intended_audio_format->format == AUDIO_BUFFER_FORMAT_PCM_S16 || intended_audio_format->format == AUDIO_BUFFER_FORMAT_PCM_S8);
    if (snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16) < 0) {
        printf("ALSA does not support 16bit signed audio for your soundcard\n");
        goto fail;
    }

    if (snd_pcm_hw_params_set_channels(pcm, hw, intended_audio_format->channel_count) < 0) {
        printf("ALSA does not support %d channels for your soundcard\n", intended_audio_format->channel_count);
        goto fail;
    }

    unsigned int rate = intended_audio_format->sample_freq;

    r = rate;
    if (snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, 0) < 0) {
        printf("ALSA does not support %uHz for your soundcard\n", rate);
        goto fail;
    }
    if (r != rate) {
        printf("ALSA: sample rate set to %uHz instead of %u\n", rate, r);
    }

    alsa_buffer_time = 500000;
    alsa_period_time = 50000;

    if ((err = snd_pcm_hw_params_set_buffer_time_near(pcm, hw, &alsa_buffer_time, 0)) < 0) {
        printf("Set buffer time failed: %s.\n", snd_strerror(err));
        goto fail;
    }

    if ((err = snd_pcm_hw_params_set_period_time_near(pcm, hw, &alsa_period_time, 0)) < 0) {
        printf("Set period time failed: %s.\n", snd_strerror(err));
        goto fail;
    }

    if (snd_pcm_hw_params(pcm, hw) < 0) {
        printf("Unable to install hw params\n");
        goto fail;
    }

    snd_pcm_sw_params_alloca(&sw);
    snd_pcm_sw_params_current(pcm, sw);
    if (snd_pcm_sw_params(pcm, sw) < 0) {
        printf("Unable to install sw params\n");
        goto fail;
    }

    consumer_format = *intended_audio_format;
    return intended_audio_format;

    fail:   close_alsa_output();
    return NULL;
}

void alsa_producer_pool_blocking_give_s16(struct audio_connection *connection, struct audio_buffer *buffer)
{
    // todo this is wrong for setting a single channel of stereo via non interleave
    uint8_t *output_data = buffer->buffer->bytes;
    int sample_count = buffer->sample_count;
    int err;
    while (sample_count > 0) {
        if ((err = snd_pcm_writei(pcm, output_data, sample_count)) < 0) {
            if (snd_pcm_state(pcm) == SND_PCM_STATE_XRUN) {
                if ((err = snd_pcm_prepare(pcm)) < 0)
                    printf("\nsnd_pcm_prepare() failed.\n");
                alsa_first_time = 1;
                continue;
            }
            panic("failed to send sound data");
        }
        sample_count -= err;
        output_data += snd_pcm_frames_to_bytes(pcm, err);
        if (alsa_first_time) {
            alsa_first_time = 0;
            snd_pcm_start(pcm);
        }
    }
    queue_free_audio_buffer(connection->producer_pool, buffer);
}

void alsa_producer_pool_blocking_give_upsample_s16(struct audio_connection *connection, struct audio_buffer *buffer)
{
    static int16_t sample_buffer[16384];
    // todo this is wrong for setting a single channel of stereo via non interleave
    uint8_t *output_data = buffer->buffer->bytes;
    assert(connection->producer_pool->format->sample_freq < connection->consumer_pool->format->sample_freq);
    int sample_count = (buffer->sample_count * connection->consumer_pool->format->sample_freq)/connection->producer_pool->format->sample_freq;
    int channel_sample_count = sample_count * buffer->format->format->channel_count;
    assert(channel_sample_count < count_of(sample_buffer));
    uint j = 0;
    uint frac = (65536 * connection->producer_pool->format->sample_freq) / connection->consumer_pool->format->sample_freq;
    for(uint i=0;i<channel_sample_count;i++) {
        sample_buffer[i] = ((int16_t *)buffer->buffer->bytes)[j>>16];
        j += frac;
    }
    int err;
    while (sample_count > 0) {
        if ((err = snd_pcm_writei(pcm, sample_buffer, sample_count)) < 0) {
            if (snd_pcm_state(pcm) == SND_PCM_STATE_XRUN) {
                if ((err = snd_pcm_prepare(pcm)) < 0)
                    printf("\nsnd_pcm_prepare() failed.\n");
                alsa_first_time = 1;
                continue;
            }
            panic("failed to send sound data");
        }
        sample_count -= err;
        output_data += snd_pcm_frames_to_bytes(pcm, err);
        if (alsa_first_time) {
            alsa_first_time = 0;
            snd_pcm_start(pcm);
        }
    }
    queue_free_audio_buffer(connection->producer_pool, buffer);
}

void alsa_producer_pool_blocking_give_s8(struct audio_connection *connection, struct audio_buffer *buffer)
{
    static int16_t sample_buffer[16384];
    // todo this is wrong for setting a single channel of stereo via non interleave
    uint8_t *output_data = buffer->buffer->bytes;
    int sample_count = buffer->sample_count;
    int channel_sample_count = sample_count * buffer->format->format->channel_count;
    assert(channel_sample_count < count_of(sample_buffer));
    for(uint i=0;i<channel_sample_count;i++) {
        sample_buffer[i] = buffer->buffer->bytes[i] << 8u;
    }
    int err;
    while (sample_count > 0) {
        if ((err = snd_pcm_writei(pcm, sample_buffer, sample_count)) < 0) {
            if (snd_pcm_state(pcm) == SND_PCM_STATE_XRUN) {
                if ((err = snd_pcm_prepare(pcm)) < 0)
                    printf("\nsnd_pcm_prepare() failed.\n");
                alsa_first_time = 1;
                continue;
            }
            panic("failed to send sound data");
        }
        sample_count -= err;
        output_data += snd_pcm_frames_to_bytes(pcm, err);
        if (alsa_first_time) {
            alsa_first_time = 0;
            snd_pcm_start(pcm);
        }
    }
    queue_free_audio_buffer(connection->producer_pool, buffer);
}

void alsa_producer_pool_blocking_give_upsample_s8(struct audio_connection *connection, struct audio_buffer *buffer)
{
    static int16_t sample_buffer[16384];
    // todo this is wrong for setting a single channel of stereo via non interleave
    uint8_t *output_data = buffer->buffer->bytes;
    assert(connection->producer_pool->format->sample_freq < connection->consumer_pool->format->sample_freq);
    int sample_count = (buffer->sample_count * connection->consumer_pool->format->sample_freq)/connection->producer_pool->format->sample_freq;
    int channel_sample_count = sample_count * buffer->format->format->channel_count;
    assert(channel_sample_count < count_of(sample_buffer));
    uint j = 0;
    uint frac = (65536 * connection->producer_pool->format->sample_freq) / connection->consumer_pool->format->sample_freq;
    for(uint i=0;i<channel_sample_count;i++) {
        sample_buffer[i] = buffer->buffer->bytes[j>>16] << 8u;
        j += frac;
    }
    int err;
    while (sample_count > 0) {
        if ((err = snd_pcm_writei(pcm, sample_buffer, sample_count)) < 0) {
            if (snd_pcm_state(pcm) == SND_PCM_STATE_XRUN) {
                if ((err = snd_pcm_prepare(pcm)) < 0)
                    printf("\nsnd_pcm_prepare() failed.\n");
                alsa_first_time = 1;
                continue;
            }
            panic("failed to send sound data");
        }
        sample_count -= err;
        output_data += snd_pcm_frames_to_bytes(pcm, err);
        if (alsa_first_time) {
            alsa_first_time = 0;
            snd_pcm_start(pcm);
        }
    }
    queue_free_audio_buffer(connection->producer_pool, buffer);
}



static struct audio_connection alsa_blocking_give_audio_connection_s16 = {
        .consumer_pool_take = consumer_pool_take_buffer_default,
        .consumer_pool_give = consumer_pool_give_buffer_default,
        .producer_pool_take = producer_pool_take_buffer_default,
        .producer_pool_give = alsa_producer_pool_blocking_give_s16
};

static struct audio_connection alsa_blocking_give_upsample_audio_connection_s16 = {
        .consumer_pool_take = consumer_pool_take_buffer_default,
        .consumer_pool_give = consumer_pool_give_buffer_default,
        .producer_pool_take = producer_pool_take_buffer_default,
        .producer_pool_give = alsa_producer_pool_blocking_give_upsample_s16
};


static struct audio_connection alsa_blocking_give_audio_connection_s8 = {
        .consumer_pool_take = consumer_pool_take_buffer_default,
        .consumer_pool_give = consumer_pool_give_buffer_default,
        .producer_pool_take = producer_pool_take_buffer_default,
        .producer_pool_give = alsa_producer_pool_blocking_give_s8
};

static struct audio_connection alsa_blocking_give_upsample_audio_connection_s8 = {
        .consumer_pool_take = consumer_pool_take_buffer_default,
        .consumer_pool_give = consumer_pool_give_buffer_default,
        .producer_pool_take = producer_pool_take_buffer_default,
        .producer_pool_give = alsa_producer_pool_blocking_give_upsample_s8
};


bool native_audio_connect(struct audio_buffer_pool *producer)
{
    printf("Connecting ALSA audio\n");

    // todo we need to pick a connection based on the frequency - e.g. 22050 can be more simply upsampled to 44100


    consumer_buffer_format.sample_stride = consumer_format.channel_count * 2;

    // todo don't need a consumer pool, but have to specify one in current api
    struct audio_buffer_pool *consumer = audio_new_consumer_pool(&consumer_buffer_format, 0, 0);

    if (producer->format->format == AUDIO_BUFFER_FORMAT_PCM_S16) {
        if (consumer->format->sample_freq == producer->format->sample_freq) {
            audio_complete_connection(&alsa_blocking_give_audio_connection_s16, producer, consumer);
        } else {
            audio_complete_connection(&alsa_blocking_give_upsample_audio_connection_s16, producer, consumer);
        }
    } else if (producer->format->format == AUDIO_BUFFER_FORMAT_PCM_S8) {
        if (consumer->format->sample_freq == producer->format->sample_freq) {
            audio_complete_connection(&alsa_blocking_give_audio_connection_s8, producer, consumer);
        } else {
            audio_complete_connection(&alsa_blocking_give_upsample_audio_connection_s8, producer, consumer);
        }
    } else {
        return false;
    }
    return true;
}

#endif
#ifdef NATIVE_AUDIO_SDL2
SDL_AudioDeviceID sdl_audio_device_id;
SDL_AudioSpec* sdl_audio_spec = NULL;
int bytes_per_frame;
int max_latency_bytes;

const struct audio_format *native_audio_setup(const struct audio_format *intended_audio_format)
{
    SDL_AudioSpec *desired;
    desired = (SDL_AudioSpec *)calloc(1,sizeof(SDL_AudioSpec));
    desired->freq = intended_audio_format->sample_freq;
    assert(intended_audio_format->format == AUDIO_BUFFER_FORMAT_PCM_S16 || intended_audio_format->format == AUDIO_BUFFER_FORMAT_PCM_S8);
    desired->format = AUDIO_S16SYS;
    desired->channels = intended_audio_format->channel_count;
    desired->samples = 1024; // todo random
    desired->callback = NULL;//audio_callback;
    desired->userdata = NULL;

    if (SDL_OpenAudio(desired, NULL) != 0) {
        return NULL;
    }
    bytes_per_frame = desired->channels * 2;
    uint max_latency_ms = 100;
    max_latency_bytes = (bytes_per_frame * desired->freq * max_latency_ms) / 1000;
    printf("Max latency bytes %d\n", max_latency_bytes);
    sdl_audio_device_id = 1;
    sdl_audio_spec = desired;
    consumer_format = *intended_audio_format;

    return intended_audio_format;
}

void sdl_producer_pool_blocking_give(struct audio_connection *connection, struct audio_buffer *buffer)
{
    // todo yuck hack!!!! use the SDL callback now we have buffer pools
    while (SDL_GetQueuedAudioSize(sdl_audio_device_id) > max_latency_bytes) {
        tight_loop_contents();
    }
    assert(buffer->format->sample_stride == bytes_per_frame);
    int __unused rc = SDL_QueueAudio(sdl_audio_device_id, buffer->buffer->bytes, buffer->sample_count * bytes_per_frame);
    assert(!rc);
    queue_free_audio_buffer(connection->producer_pool, buffer);
}

void sdl_producer_pool_blocking_give_s8(struct audio_connection *connection, struct audio_buffer *buffer)
{
    // todo yuck hack!!!! use the SDL callback now we have buffer pools
    while (SDL_GetQueuedAudioSize(sdl_audio_device_id) > max_latency_bytes) {
        tight_loop_contents();
    }
    static int16_t sample_buffer[16384];
    // todo this is wrong for setting a single channel of stereo via non interleave
    int sample_count = buffer->sample_count;
    int channel_sample_count = sample_count * buffer->format->format->channel_count;
    assert(channel_sample_count < count_of(sample_buffer));
    for(uint i=0;i<channel_sample_count;i++) {
        sample_buffer[i] = buffer->buffer->bytes[i] << 8u;
    }
    assert(buffer->format->sample_stride * 2 == bytes_per_frame);
    int __unused rc = SDL_QueueAudio(sdl_audio_device_id, sample_buffer, buffer->sample_count * bytes_per_frame);
    assert(!rc);
    queue_free_audio_buffer(connection->producer_pool, buffer);
}


static struct audio_connection sdl_blocking_give_audio_connection_s16 = {
        .consumer_pool_take = consumer_pool_take_buffer_default,
        .consumer_pool_give = consumer_pool_give_buffer_default,
        .producer_pool_take = producer_pool_take_buffer_default,
        .producer_pool_give = sdl_producer_pool_blocking_give
};

static struct audio_connection sdl_blocking_give_audio_connection_s8 = {
    .consumer_pool_take = consumer_pool_take_buffer_default,
    .consumer_pool_give = consumer_pool_give_buffer_default,
    .producer_pool_take = producer_pool_take_buffer_default,
    .producer_pool_give = sdl_producer_pool_blocking_give_s8
};


bool native_audio_connect(struct audio_buffer_pool *producer)
{
    printf("Connecting SDL2 audio\n");

    // todo we need to pick a connection based on the frequency - e.g. 22050 can be more simply upsampled to 44100
    consumer_buffer_format.sample_stride = consumer_format.channel_count * 2;

    // todo don't need a consumer pool, but have to specify one in current api
    struct audio_buffer_pool *consumer = audio_new_consumer_pool(&consumer_buffer_format, 0, 0);

    if (producer->format->format == AUDIO_BUFFER_FORMAT_PCM_S16) {
        audio_complete_connection(&sdl_blocking_give_audio_connection_s16, producer, consumer);
    } else if (producer->format->format == AUDIO_BUFFER_FORMAT_PCM_S8) {
        audio_complete_connection(&sdl_blocking_give_audio_connection_s8, producer, consumer);
    } else {
        return false;
    }
    return true;
}

// max_latency_ms may be -1
// buffer_sample_count may be -1 and is per channel (it is the expected number of samples in buffered passed to audio_queue_samples (if known)
extern void native_audio_enable(bool enable) {
    SDL_PauseAudio(!enable);
}

//extern uint32_t audio_get_time_ms(int channel);
//extern uint32_t audio_set_time_ms(int channel, uint32_t time_ms);
//uint32_t audio_buffered_ms(uint channel) {
//    assert(false);
//    return 0;
//}
//

uint32_t audio_get_optimal_buffer_sample_count() {
    return sdl_audio_spec->size / bytes_per_frame;
}
#endif

extern const struct audio_format *audio_pwm_setup(const struct audio_format *intended_audio_format, int32_t max_latency_ms,
                    const struct audio_pwm_channel_config *channel_config0, ...)
{
    return native_audio_setup(intended_audio_format);
}

bool audio_pwm_default_connect(struct audio_buffer_pool *producer_pool, bool dedicate_core_1) {
    return native_audio_connect(producer_pool);
}

void audio_pwm_set_enabled(bool enabled) {
    return native_audio_enable(enabled);
}

const struct audio_format *audio_i2s_setup(const struct audio_format *intended_audio_format,
                                               const struct audio_i2s_config *config) {
    return native_audio_setup(intended_audio_format);
}

bool audio_i2s_connect(struct audio_buffer_pool *producer_pool) {
    return native_audio_connect(producer_pool);
}

bool audio_i2s_connect_s8(struct audio_buffer_pool *producer_pool) {
    return native_audio_connect(producer_pool);
}

bool audio_i2s_connect_extra(audio_buffer_pool_t *producer_pool, __unused bool buffer_on_give, __unused uint buffer_count,
                             __unused uint samples_per_buffer, __unused audio_connection_t *connection) {
    return native_audio_connect(producer_pool);
}

void audio_i2s_set_enabled(bool enable) {
    return native_audio_enable(enable);
}

bool audio_pwm_set_correction_mode(enum audio_correction_mode mode) {
    return mode == acm_none;
}

enum audio_correction_mode audio_pwm_get_correction_mode() {
    return acm_none;
}

#define AUDIO_UPSAMPLE_FRACTION_BITS 12u

void audio_upsample(int16_t *input, int16_t *output, uint output_count, uint32_t step) {
    uint32_t pos = 0;
    for (int i = 0; i < output_count; i++) {
        uint32_t offset = (pos >> AUDIO_UPSAMPLE_FRACTION_BITS);
        int16_t a = input[offset];
        int16_t b = input[offset + 1];
        *output++ = a + (((b - a) * ((pos >> (AUDIO_UPSAMPLE_FRACTION_BITS - 8)) & 0xff)) >> 8);
        pos += step;
    }
}

void audio_upsample_words(int16_t *input, int16_t *output_aligned, uint output_word_count, uint32_t step) {
    audio_upsample(input, output_aligned, output_word_count * 2, step);
}
