/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * T-Head C906 D-Cache maintenance helpers.
 *
 * The D21x has no MMU in this BSP, so the CPU address passed to the
 * display/GE DMA engine is also the physical address.  These helpers are
 * used at DMA ownership boundaries only; they do not disable the cache.
 */

#ifndef __AIC_CACHE_H__
#define __AIC_CACHE_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void aic_dcache_clean_range(uintptr_t addr, size_t size);
void aic_dcache_invalidate_range(uintptr_t addr, size_t size);
void aic_dcache_clean_invalidate_range(uintptr_t addr, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* __AIC_CACHE_H__ */
