/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x DMIC capture driver helpers.
 */

#ifndef __DRV_DMIC_H__
#define __DRV_DMIC_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Number of DMA periods handed to the record pipe since capture last
 * started.  The utest uses it to prove that the DMIC decimator, the RX
 * DRQ and the cyclic DMA path really run; it says nothing about the
 * acoustic signal.
 */
rt_uint32_t aic_dmic_period_count(void);

/* True while the capture stream is running. */
rt_bool_t aic_dmic_is_capturing(void);

/*
 * Stop the capture stream if it is running.  Playback calls this before it
 * reprograms the shared codec clock; a no-op otherwise.
 */
void aic_dmic_stop_stream(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_DMIC_H__ */
