/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x CLINT timer.
 *
 * The CLINT free-running counter runs at 4 MHz on D21x.  RT-Thread uses
 * machine timer interrupts (IRQ 7) through the generic RISC-V port.
 */

#include <rthw.h>
#include <rtthread.h>
#include <stdint.h>

#include "board.h"

#define CLINT_MTIMECMPL0 0x4000
#define CLINT_MTIMECMPH0 0x4004

#if (BSP_CLINT_FREQ / RT_TICK_PER_SECOND) == 0
#error "BSP_CLINT_FREQ must be at least RT_TICK_PER_SECOND"
#endif

static uint64_t aic_tick_interval;
static uint64_t aic_next_tick;

static inline uint64_t aic_read_time(void)
{
    uint64_t value;

    __asm__ volatile("rdtime %0" : "=r"(value));
    return value;
}

static void aic_set_mtimecmp(uint64_t value)
{
    volatile uint32_t *cmp_l;
    volatile uint32_t *cmp_h;

    cmp_l = (volatile uint32_t *)(AIC_CLINT_BASE + CLINT_MTIMECMPL0);
    cmp_h = (volatile uint32_t *)(AIC_CLINT_BASE + CLINT_MTIMECMPH0);

    /* Keep the low word last to avoid a spurious match. */
    *cmp_h = (uint32_t)(value >> 32);
    *cmp_l = (uint32_t)value;
}

int rt_hw_tick_init(void)
{
    aic_tick_interval = AIC_CLINT_FREQ / RT_TICK_PER_SECOND;
    if (aic_tick_interval == 0)
    {
        aic_tick_interval = 1;
    }

    aic_next_tick = aic_read_time() + aic_tick_interval;
    aic_set_mtimecmp(aic_next_tick);

    /* Enable machine timer interrupt. */
    __asm__ volatile("csrs mie, %0" : : "r"(1UL << 7));

    return 0;
}

/*
 * Called from libcpu/risc-v/rv64/trap.c for a machine timer interrupt.
 */
int rt_hw_tick_isr(void)
{
    uint64_t now;

    rt_tick_increase();

    /*
     * Advance from the previous compare value instead of from "now" so
     * that interrupt latency does not accumulate as tick drift.  If the
     * handler is already behind, restart one interval from now rather
     * than arming an immediate, repeated interrupt.
     */
    aic_next_tick += aic_tick_interval;
    now = aic_read_time();
    if (aic_next_tick <= now)
    {
        aic_next_tick = now + aic_tick_interval;
    }

    aic_set_mtimecmp(aic_next_tick);

    return 0;
}

void rt_hw_us_delay(rt_uint32_t us)
{
    uint64_t start;
    uint64_t ticks;

    start = aic_read_time();
    ticks = (uint64_t)us * (AIC_CLINT_FREQ / 1000000U);

    while ((aic_read_time() - start) < ticks);
}
