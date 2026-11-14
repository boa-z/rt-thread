/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x CLINT timer access for the RT-Thread RISC-V common
 * port.
 *
 * The D21x CLINT exposes mtimecmp at +0x4000 and keeps the free-running
 * counter in the rdtime CSR instead of a memory-mapped mtime register.
 * The common timer core is reused; only the two weak MMIO accessors are
 * overridden here.
 */

#include <rtthread.h>
#include <riscv_timer.h>
#include <stdint.h>

#include "board.h"

#define AIC_CLINT_MTIMECMP  (AIC_CLINT_BASE + 0x4000UL)

rt_uint64_t riscv_timer_rdtime(void)
{
    rt_uint64_t value;

    __asm__ volatile("rdtime %0" : "=r"(value));

    return value;
}

void riscv_timer_set_compare(rt_uint64_t value)
{
    /*
     * The D21x CLINT exposes MTIMECMP as separate low/high 32-bit
     * registers.  A single 64-bit store is not guaranteed to update the
     * comparator on this bus, which leaves the timer interrupt silent.
     */
    volatile rt_uint32_t *cmp =
        (volatile rt_uint32_t *)(uintptr_t)AIC_CLINT_MTIMECMP;

    cmp[0] = (rt_uint32_t)value;
    cmp[1] = (rt_uint32_t)(value >> 32);
}

rt_uint64_t rt_hw_get_clock_timer_freq(void)
{
    return BSP_CLINT_FREQ;
}
