/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pico/time.h"
#include "SDL_timer.h"

SDL_TimerID hardware_alarm_timers[NUM_GENERIC_TIMERS];
hardware_alarm_callback_t hardware_alarm_callbacks[NUM_GENERIC_TIMERS];

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
    assert(alarm_num < NUM_GENERIC_TIMERS);
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

#include "pico/time_adapter.h"

SDL_TimerID pool_timers[TA_NUM_TIMER_ALARMS];
void (*alarm_pool_irq_handler)(void);
uint current_hardware_alarm_num;

uint32_t pool_timer_callback(uint32_t period, void *param) {
    current_hardware_alarm_num = (uint32_t)(uintptr_t)param;
    alarm_pool_irq_handler();
    return 0;
}

void clear_pool_timer(uint hardware_alarm_num) {
    SDL_RemoveTimer(pool_timers[hardware_alarm_num]);
    pool_timers[hardware_alarm_num] = 0;
}

void ta_clear_irq(__unused alarm_pool_timer_t *timer, uint hardware_alarm_num) {
    clear_pool_timer(hardware_alarm_num);
}

void ta_clear_force_irq(__unused alarm_pool_timer_t *timer, uint hardware_alarm_num) {
    clear_pool_timer(hardware_alarm_num);
}

void reset_pool_timer(uint hardware_alarm_num, uint delay) {
    clear_pool_timer(hardware_alarm_num);
    pool_timers[hardware_alarm_num] = SDL_AddTimer(delay, pool_timer_callback, (void *)(uintptr_t)hardware_alarm_num);
}

void ta_force_irq(__unused alarm_pool_timer_t *timer, uint hardware_alarm_num) {
    reset_pool_timer(hardware_alarm_num, 0);
}

static uint8_t timer_inst;

uint ta_timer_num(alarm_pool_timer_t *timer) {
    return 0;
}

alarm_pool_timer_t *ta_default_timer_instance(void) {
    return &timer_inst;
}

alarm_pool_timer_t *ta_timer_instance(uint timer_alarm_num) {
    assert(!timer_alarm_num);
    return ta_default_timer_instance();
}

alarm_pool_timer_t *ta_from_current_irq(uint *alarm_num) {
    *alarm_num = current_hardware_alarm_num;
    return ta_timer_instance(0);
}

void ta_set_timeout(__unused alarm_pool_timer_t *timer, uint hardware_alarm_num, int64_t target) {
    int64_t delta = target - (int64_t)time_us_64();
    if (delta < 0) delta = 0;
    else delta /= 1000;
    reset_pool_timer(hardware_alarm_num, (uint32_t)delta);
}

void ta_enable_irq_handler(__unused alarm_pool_timer_t *timer, uint hardware_alarm_num, void (*irq_handler)(void)) {
    alarm_pool_irq_handler = irq_handler;
}

void ta_disable_irq_handler(__unused alarm_pool_timer_t *timer, uint hardware_alarm_num, void (*irq_handler)(void)) {
    clear_pool_timer(hardware_alarm_num);
}

void ta_hardware_alarm_claim(__unused alarm_pool_timer_t *timer, uint hardware_alarm_num) {
    hardware_alarm_claim(hardware_alarm_num);
}

int ta_hardware_alarm_claim_unused(__unused alarm_pool_timer_t *timer, bool required) {
    return hardware_alarm_claim_unused(required);
}
