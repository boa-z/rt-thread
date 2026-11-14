/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x audio codec driver helpers.
 */

#ifndef __DRV_AUDIO_H__
#define __DRV_AUDIO_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Number of playback periods handed to the DMAC since the device was
 * last started.  The utest suite uses it to prove that the codec clock,
 * the FIFO DRQ and the cyclic DMA path really run; it says nothing about
 * acoustic output.
 */
rt_uint32_t aic_audio_period_count(void);

/* Current codec sample rate / channel count, 0 when not configured. */
void aic_audio_get_config(rt_uint32_t *samplerate, rt_uint32_t *channels);

/* True while the replay stream is running. */
rt_bool_t aic_audio_is_playing(void);

/*
 * Stop the replay stream if it is running.  Capture calls this before it
 * reprograms the shared codec clock; a no-op otherwise.
 */
void aic_audio_stop_stream(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_AUDIO_H__ */
