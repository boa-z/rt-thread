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
    *(volatile rt_uint64_t *)(uintptr_t)AIC_CLINT_MTIMECMP = value;
}

rt_uint64_t rt_hw_get_clock_timer_freq(void)
{
    return BSP_CLINT_FREQ;
}
