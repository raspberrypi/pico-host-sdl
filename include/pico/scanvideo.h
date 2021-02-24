/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _PICO_SCANVIDEO_H_
#define _PICO_SCANVIDEO_H_

// note that defining to false will force non-inclusion also
#if !defined(PICO_SCANVIDEO_DPI)
#define PICO_SCANVIDEO_DPI 1

#ifndef PARAM_ASSERTIONS_ENABLED_SCANVIDEO_DPI
#define PARAM_ASSERTIONS_ENABLED_SCANVIDEO_DPI 0
#endif

#include "pico/scanvideo/scanvideo_base.h"
#include "pico/pico_host_sdl.h"

typedef void (*simulate_scanvideo_pio_fn)(const uint32_t *dma_data, uint32_t dma_data_size,
                                      uint16_t *pixel_buffer, int32_t max_pixels, int32_t expected_width, bool overlay);

extern void scanvideo_set_simulate_scanvideo_pio_fn(const char *id, simulate_scanvideo_pio_fn fn);

typedef void (*simulate_composable_cmd_fn)(const uint16_t **dma_data, uint16_t **pixels, int32_t max_pixels, bool overlay);

extern void scanvideo_set_simulate_composable_cmd(uint cmd, simulate_composable_cmd_fn fn);


// todo move these to a host specific header
// todo until we have an abstraction
// These are or'ed with SDL_SCANCODE_* constants in last_key_scancode.
enum key_modifiers {
    WITH_SHIFT = 0x8000,
    WITH_CTRL  = 0x4000,
    WITH_ALT   = 0x2000,
};
extern void (*platform_key_down)(int scancode, int keysym, int modifiers);
extern void (*platform_key_up)(int scancode, int keysym, int modifiers);
extern void (*platform_mouse_move)(int dx, int dy);
extern void (*platform_mouse_button_down)(int button);
extern void (*platform_mouse_button_up)(int button);
extern void (*platform_quit)();

#define PICO_SCANVIDEO_ALPHA_PIN 5u
#define PICO_SCANVIDEO_PIXEL_RSHIFT 0u
#define PICO_SCANVIDEO_PIXEL_GSHIFT 6u
#define PICO_SCANVIDEO_PIXEL_BSHIFT 11u

#endif
#endif
