/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * C906 D-Cache maintenance for non-coherent DMA (DE/GE).
 *
 * The instruction encodings and the a5 register convention come from the
 * T-Head C906 cache operations used by the vendor core_rv64.h.  Only the
 * range operations are needed by this BSP.
 */

#include <stdint.h>
#include <stddef.h>

#include "board.h"
#include "aic_cache.h"

#define AIC_CACHE_LINE_SIZE 64UL

static inline uintptr_t aic_cache_align_down(uintptr_t addr)
{
    return addr & ~(AIC_CACHE_LINE_SIZE - 1);
}

static inline uintptr_t aic_cache_align_up(uintptr_t addr)
{
    return (addr + AIC_CACHE_LINE_SIZE - 1) &
           ~(AIC_CACHE_LINE_SIZE - 1);
}

void aic_dcache_clean_range(uintptr_t addr, size_t size)
{
    uintptr_t start;
    uintptr_t end;

    if (!size)
        return;

    start = aic_cache_align_down(addr);
    end = aic_cache_align_up(addr + size);

    __asm__ volatile("fence rw, rw" ::: "memory");
    while (start < end)
    {
        register unsigned long c_addr asm("a5") = start;
        __asm__ volatile(".long 0x0297800b" : "+r"(c_addr) :: "memory");
        start += AIC_CACHE_LINE_SIZE;
    }
    __asm__ volatile("fence rw, rw" ::: "memory");
    __asm__ volatile(".long 0x0190000b" ::: "memory"); /* sync.s */
}

void aic_dcache_invalidate_range(uintptr_t addr, size_t size)
{
    uintptr_t start;
    uintptr_t end;

    if (!size)
        return;

    start = aic_cache_align_down(addr);
    end = aic_cache_align_up(addr + size);

    __asm__ volatile("fence rw, rw" ::: "memory");
    while (start < end)
    {
        register unsigned long c_addr asm("a5") = start;
        __asm__ volatile(".long 0x02a7800b" : "+r"(c_addr) :: "memory");
        start += AIC_CACHE_LINE_SIZE;
    }
    __asm__ volatile("fence rw, rw" ::: "memory");
    __asm__ volatile(".long 0x0190000b" ::: "memory"); /* sync.s */
}

void aic_dcache_clean_invalidate_range(uintptr_t addr, size_t size)
{
    uintptr_t start;
    uintptr_t end;

    if (!size)
        return;

    start = aic_cache_align_down(addr);
    end = aic_cache_align_up(addr + size);

    __asm__ volatile("fence rw, rw" ::: "memory");
    while (start < end)
    {
        register unsigned long c_addr asm("a5") = start;
        __asm__ volatile(".long 0x02b7800b" : "+r"(c_addr) :: "memory");
        start += AIC_CACHE_LINE_SIZE;
    }
    __asm__ volatile("fence rw, rw" ::: "memory");
    __asm__ volatile(".long 0x0190000b" ::: "memory"); /* sync.s */
}
