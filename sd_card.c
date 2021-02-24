/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#if PICO_EXTRAS
#include <stdio.h>
#include "pico/sd_card.h"
#include "SDL_timer.h"

static FILE *sd_file_in;
#if 1
static double seek_time_ms = 1;
static double sector_ms = 1000 * (512 + 8 + 20)/12000000.0;
#else
static double seek_time_ms = 0;
static double sector_ms = 0;
#endif
static uint32_t *sd_next_control_word;
static uint sd_sector;
static uint sd_read_sector_count;
static uint32_t sd_sector_tick_base;
static double sector_next_tick_ms;

int sd_init(bool allow_four_data_pins) {
    if (!sd_file_in)
    {
        const char *filename = "sd.card";
        sd_file_in = fopen(filename, "rb");
        if (!sd_file_in)
        {
            printf("Can't open file '%s'\n", filename);
        }
    }
    return sd_file_in ? 0 : -1;
}

int sd_readblocks_scatter_async(uint32_t *control_words, uint32_t block, uint block_count) {
    sd_next_control_word = control_words;
    sd_sector = block;
    sd_read_sector_count = block_count;
    sd_sector_tick_base = SDL_GetTicks();
    sector_next_tick_ms = seek_time_ms + sector_ms;
    return SD_OK;
}

#define MAX_BLOCK_COUNT 32
static uint32_t crcs[MAX_BLOCK_COUNT * 2];
uint32_t ctrl_words[(MAX_BLOCK_COUNT + 1) * 4];

int sd_readblocks_async(uint32_t *buf, uint32_t block, uint block_count) {
    assert(block_count <= MAX_BLOCK_COUNT);

    uint32_t *p = ctrl_words;
    for(int i = 0; i < block_count; i++)
    {
        *p++ = host_safe_hw_ptr (buf + i * 128);
        *p++ = 128;
        // for now we read the CRCs also
        *p++ = host_safe_hw_ptr (crcs + i * 2);
        *p++ = 2;
    }
    *p++ = 0;
    *p++ = 0;
    return sd_readblocks_scatter_async(ctrl_words, block, block_count);
}

bool sd_scatter_read_complete(int *status) {
    while (sd_read_sector_count && sector_next_tick_ms < (int32_t)((SDL_GetTicks() - sd_sector_tick_base))) {
        uint8_t buf[512];
        fseek(sd_file_in, sd_sector * 512, SEEK_SET);
        uint read = fread(buf, 1, 512, sd_file_in);
        assert(read == 512);
        uint pos = 0;
        while (pos < 512) {
            void *ptr = decode_host_safe_hw_ptr(sd_next_control_word[0]);
            assert(ptr);
            assert(sd_next_control_word[1]);
            assert(sd_next_control_word[1] <= 128);
            memcpy(ptr, buf + pos, sd_next_control_word[1] * 4);
            pos += sd_next_control_word[1] * 4;
            sd_next_control_word += 2;
        }
        void *ptr = decode_host_safe_hw_ptr(sd_next_control_word[0]);
        assert(ptr);
        assert(sd_next_control_word[1] == 2);
        sd_next_control_word += 2; // skip CRC
        sd_sector++;
        sd_read_sector_count--;
        sector_next_tick_ms += sector_ms;
    }
    if (!sd_read_sector_count) {
        assert(!sd_next_control_word || !*sd_next_control_word);
    }
    return !sd_read_sector_count;
}
#endif