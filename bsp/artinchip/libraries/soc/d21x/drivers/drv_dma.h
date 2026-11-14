/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Cache-coherent DMA buffer helpers for the ArtInChip D21x BSP.
 *
 * The C906 has a non-coherent D-Cache: a device that uses a CPU buffer
 * as a DMA target must be given a cache-line aligned range, and the CPU
 * must clean (write back) or invalidate that range at the ownership
 * boundaries.  This file provides the small amount of infrastructure
 * that every DMA-capable driver needs, so drivers do not grow their own
 * ad-hoc alignment and cache handling.
 */

#ifndef __DRV_DMA_H__
#define __DRV_DMA_H__

#include <rtthread.h>

/*
 * C906 L1 D-Cache line size.  board/cache.c uses the same value; keep
 * the two in sync if a different SoC is added.
 */
#define AIC_DMA_CACHE_LINE 64

/*
 * Allocate a buffer that is aligned to AIC_DMA_CACHE_LINE at both ends
 * (the allocation is rounded up to a whole number of cache lines).
 * Returns RT_NULL when size is zero or memory is exhausted.
 */
void *aic_dma_alloc(rt_size_t size);

/* Release a buffer returned by aic_dma_alloc(). */
void aic_dma_free(void *ptr);

/*
 * True when a range can be handed to a DMA engine without a bounce
 * buffer: the start address and the size are both line aligned.
 */
rt_bool_t aic_dma_range_is_aligned(const void *addr, rt_size_t size);

/* CPU wrote the buffer; make the data visible to the device. */
void aic_dma_cache_clean(const void *addr, rt_size_t size);

/* Device wrote the buffer; discard stale CPU lines before reading it. */
void aic_dma_cache_invalidate(const void *addr, rt_size_t size);

/* Both directions, for shared descriptors or bidirectional buffers. */
void aic_dma_cache_clean_invalidate(const void *addr, rt_size_t size);

#endif /* __DRV_DMA_H__ */
