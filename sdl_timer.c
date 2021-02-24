/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pico/time.h"
#include "SDL_timer.h"

SDL_TimerID hardware_alarm_timers[NUM_TIMERS];
hardware_alarm_callback_t hardware_alarm_callbacks[NUM_TIMERS];

uint64_t time_us_64() {
    return SDL_GetTicks()*1000ul;
}

uint32_t time_us_32() {
    return time_us_64();
}

void hardware_alarm_set_callback(uint alarm_num, hardware_alarm_callback_t callback) {
    hardware_alarm_callbacks[alarm_num] = callback;
    if (!callback) {
        SDL_RemoveTimer(hardware_alarm_timers[alarm_num]);
    }
}

static Uint32 alarm_callback(Uint32 interval, void *param) {
    uint32_t alarm_num = (uint)(intptr_t)param;
    assert(alarm_num < NUM_TIMERS);
    assert(hardware_alarm_callbacks[alarm_num]);
    hardware_alarm_callbacks[alarm_num](alarm_num);
    return 0;
}

// note this is a simple wrapper for the hardware timer... which only provides a 32 bit alarm,
// which takes care of possible races around setting a time concurrently with when the timer should fire,
// and compares the hi bits of the timer for correctness before calling the callback
bool hardware_alarm_set_target(uint alarm_num, absolute_time_t target) {
    hardware_alarm_cancel(alarm_num);
    int64_t delay = to_us_since_boot(target) - time_us_64();
    if (delay < 1000) delay = 1000;
    assert(delay <= UINT32_MAX);
    SDL_AddTimer((uint32_t)(delay / 1000), alarm_callback, (void *)(intptr_t)alarm_num);
    return false; // we don't miss
}

void hardware_alarm_cancel(uint alarm_num) {
    if (hardware_alarm_timers[alarm_num]) {
        SDL_RemoveTimer(hardware_alarm_timers[alarm_num]);
        hardware_alarm_timers[alarm_num] = 0;
    }
}