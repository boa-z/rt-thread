/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * D12p CORET tick.
 *
 * CORET is a standard 64-bit machine timer at CPU_BASE + 0x4000.
 * MTIMECMP is at offset 0, MTIME at offset 0x7ffc.  RV32 accesses the
 * 64-bit registers as two 32-bit halves.
 */

#include <rthw.h>
#include <rtthread.h>
#include <stdint.h>

#include "board.h"

#define CORET_MTIMECMP_LO   (AIC_CORET_BASE + 0x0000UL)
#define CORET_MTIMECMP_HI   (AIC_CORET_BASE + 0x0004UL)
#define CORET_MTIME_LO      (AIC_CORET_BASE + 0x7ffcUL)
#define CORET_MTIME_HI      (AIC_CORET_BASE + 0x8000UL)
#define CORET_IRQ           7

static uint32_t g_tick_interval;
static uint32_t g_next_tick_lo;
static uint32_t g_next_tick_hi;

static inline uint32_t aic_readl(uint32_t addr)
{
    return *(volatile uint32_t *)(uintptr_t)addr;
}

static inline void aic_writel(uint32_t value, uint32_t addr)
{
    *(volatile uint32_t *)(uintptr_t)addr = value;
}

static void aic_coret_read(uint32_t *hi, uint32_t *lo)
{
    uint32_t h1, h2;

    do
    {
        h1 = aic_readl(CORET_MTIME_HI);
        *lo = aic_readl(CORET_MTIME_LO);
        h2 = aic_readl(CORET_MTIME_HI);
    } while (h1 != h2);

    *hi = h2;
}

static void aic_coret_set_cmp(uint32_t hi, uint32_t lo)
{
    /* High word first: writing the low word arms the compare. */
    aic_writel(hi, CORET_MTIMECMP_HI);
    aic_writel(lo, CORET_MTIMECMP_LO);
}

static void aic_systick_handler(int irq, void *param)
{
    uint32_t hi, lo;
    uint32_t next_lo;
    uint32_t next_hi;

    (void)irq;
    (void)param;

    rt_tick_increase();

    next_lo = g_next_tick_lo + g_tick_interval;
    next_hi = g_next_tick_hi;
    if (next_lo < g_next_tick_lo)
    {
        next_hi++;
    }

    aic_coret_read(&hi, &lo);
    if ((next_hi < hi) || (next_hi == hi && next_lo <= lo))
    {
        /* We fell behind; restart one interval from now. */
        next_lo = lo + g_tick_interval;
        next_hi = hi;
        if (next_lo < lo)
        {
            next_hi++;
        }
    }

    g_next_tick_lo = next_lo;
    g_next_tick_hi = next_hi;
    aic_coret_set_cmp(next_hi, next_lo);
}

void rt_hw_tick_init(void)
{
    unsigned long cpu_clk = aic_cpu_clock();
    uint32_t hi, lo;

    g_tick_interval = (uint32_t)(cpu_clk / RT_TICK_PER_SECOND);
    if (g_tick_interval == 0)
    {
        g_tick_interval = 1;
    }

    aic_coret_read(&hi, &lo);
    lo += g_tick_interval;
    if (lo < g_tick_interval)
    {
        hi++;
    }
    g_next_tick_lo = lo;
    g_next_tick_hi = hi;
    aic_coret_set_cmp(hi, lo);

    rt_hw_interrupt_install(CORET_IRQ, aic_systick_handler,
                            RT_NULL, "coret");
    rt_hw_interrupt_umask(CORET_IRQ);
}

void rt_hw_us_delay(rt_uint32_t us)
{
    uint32_t start_hi, start_lo;
    uint32_t now_hi, now_lo;
    uint64_t start;
    uint64_t now;
    uint64_t ticks;

    ticks = (uint64_t)us * (uint64_t)(aic_cpu_clock() / 1000000UL);

    aic_coret_read(&start_hi, &start_lo);
    start = ((uint64_t)start_hi << 32) | start_lo;

    do
    {
        aic_coret_read(&now_hi, &now_lo);
        now = ((uint64_t)now_hi << 32) | now_lo;
    } while ((now - start) < ticks);
}
