/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Cache-coherent DMA buffer helpers.  See drv_dma.h for the contract.
 *
 * The D21x BSP runs without an MMU, so the CPU address is the address
 * the DMA engine sees.  The only two things that matter are alignment
 * and cache maintenance; both are handled here so that SDMC, USB, audio
 * and other future DMA users share one implementation.
 */

#include <rtthread.h>

#include "aic_cache.h"
#include "drv_dma.h"

/*
 * Keep the raw rt_malloc() pointer immediately before the aligned
 * buffer.  The extra line at the front absorbs the alignment padding,
 * and the extra line at the back keeps cache-line rounding of the tail
 * inside the allocation.
 */
#define AIC_DMA_OVERHEAD (AIC_DMA_CACHE_LINE * 2 + sizeof(rt_ubase_t))

void *aic_dma_alloc(rt_size_t size)
{
    rt_ubase_t raw;
    rt_ubase_t aligned;
    rt_ubase_t *slot;

    if (size == 0)
    {
        return RT_NULL;
    }

    if (size > (rt_size_t)-1 - AIC_DMA_OVERHEAD)
    {
        return RT_NULL;
    }

    raw = (rt_ubase_t)rt_malloc(size + AIC_DMA_OVERHEAD);
    if (raw == 0)
    {
        return RT_NULL;
    }

    aligned = RT_ALIGN(raw + sizeof(rt_ubase_t), AIC_DMA_CACHE_LINE);
    slot = (rt_ubase_t *)(aligned - sizeof(rt_ubase_t));
    *slot = raw;

    /* The caller may hand the buffer to a device without writing it. */
    rt_memset((void *)aligned, 0, size);

    return (void *)aligned;
}

void aic_dma_free(void *ptr)
{
    rt_ubase_t *slot;

    if (!ptr)
    {
        return;
    }

    slot = (rt_ubase_t *)((rt_ubase_t)ptr - sizeof(rt_ubase_t));
    rt_free((void *)(*slot));
}

rt_bool_t aic_dma_range_is_aligned(const void *addr, rt_size_t size)
{
    return (((rt_ubase_t)addr & (AIC_DMA_CACHE_LINE - 1)) == 0) &&
           ((size & (AIC_DMA_CACHE_LINE - 1)) == 0);
}

void aic_dma_cache_clean(const void *addr, rt_size_t size)
{
    aic_dcache_clean_range((uintptr_t)addr, (size_t)size);
}

void aic_dma_cache_invalidate(const void *addr, rt_size_t size)
{
    aic_dcache_invalidate_range((uintptr_t)addr, (size_t)size);
}

void aic_dma_cache_clean_invalidate(const void *addr, rt_size_t size)
{
    aic_dcache_clean_invalidate_range((uintptr_t)addr, (size_t)size);
}
