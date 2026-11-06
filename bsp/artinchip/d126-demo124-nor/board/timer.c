/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D12p CORET timer access for the RT-Thread RISC-V common
 * port.
 *
 * CORET is CLINT-like but uses MTIMECMP at +0x0000 and MTIME at
 * +0x7ffc (low) / +0x8000 (high), not the SiFive CLINT offsets.  Only
 * the two weak accessors and the frequency hook are overridden.
 */

#include <rtthread.h>
#include <riscv_timer.h>
#include <stdint.h>

#include "board.h"

#define CORET_MTIMECMP_LO   (AIC_CORET_BASE + 0x0000UL)
#define CORET_MTIMECMP_HI   (AIC_CORET_BASE + 0x0004UL)
#define CORET_MTIME_LO      (AIC_CORET_BASE + 0x7ffcUL)
#define CORET_MTIME_HI      (AIC_CORET_BASE + 0x8000UL)

rt_uint64_t riscv_timer_rdtime(void)
{
    rt_uint32_t hi;
    rt_uint32_t lo;
    rt_uint32_t check;

    do
    {
        hi = *(volatile rt_uint32_t *)(uintptr_t)CORET_MTIME_HI;
        lo = *(volatile rt_uint32_t *)(uintptr_t)CORET_MTIME_LO;
        check = *(volatile rt_uint32_t *)(uintptr_t)CORET_MTIME_HI;
    } while (hi != check);

    return ((rt_uint64_t)hi << 32) | lo;
}

void riscv_timer_set_compare(rt_uint64_t value)
{
    /* High word first: writing the low word arms the compare. */
    *(volatile rt_uint32_t *)(uintptr_t)CORET_MTIMECMP_HI =
        (rt_uint32_t)(value >> 32);
    *(volatile rt_uint32_t *)(uintptr_t)CORET_MTIMECMP_LO =
        (rt_uint32_t)value;
}

rt_uint64_t rt_hw_get_clock_timer_freq(void)
{
    return aic_cpu_clock();
}
