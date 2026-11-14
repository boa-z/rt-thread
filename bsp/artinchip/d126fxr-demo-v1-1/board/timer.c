/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D12p CORET timer access for the RT-Thread RISC-V common
 * port.
 *
 * CORET is CLINT-like but uses MTIMECMP at +0x0000 and MTIME at
 * +0x7ff8 (low) / +0x7ffc (high), not the SiFive CLINT offsets.  The
 * vendor header comments MTIME as "+0x7ffc", but that is the high word:
 * the 64-bit member is 8-byte aligned, so its low word sits at +0x7ff8.
 * Both the vendor SPL and the Luban-Lite RT-Thread image access the
 * pair at +0x7ff8/+0x7ffc.  Only the two weak accessors and the
 * frequency hook are overridden.
 */

#include <rtthread.h>
#include <riscv_timer.h>
#include <stdint.h>

#include "board.h"

#define CORET_MTIMECMP_LO   (AIC_CORET_BASE + 0x0000UL)
#define CORET_MTIMECMP_HI   (AIC_CORET_BASE + 0x0004UL)
#define CORET_MTIME_LO      (AIC_CORET_BASE + 0x7ff8UL)
#define CORET_MTIME_HI      (AIC_CORET_BASE + 0x7ffcUL)

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
    return BSP_CLINT_FREQ;
}
