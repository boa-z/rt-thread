/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * External DMA controller (DMAC) helpers shared by the MSH command and
 * the utest regression.
 */

#ifndef __DRV_DMAC_H__
#define __DRV_DMAC_H__

#include <rtthread.h>

/* True after the clock, reset and IRQ setup completed. */
rt_bool_t aic_dmac_ready(void);

/*
 * Run one memory-to-memory transfer of len bytes through the controller,
 * wait for the completion interrupt, invalidate the destination and
 * compare every byte.
 *
 * Returns RT_EOK on a verified copy, -RT_EBUSY when the controller is
 * not initialized, -RT_EINVAL for an unsupported length, -RT_ENOMEM when
 * no channel or buffer is available, -RT_ETIMEOUT on a missing
 * completion interrupt and -RT_EIO when the copied data differs.
 * kbps and ch (either may be NULL) receive the measured throughput and
 * the channel number that served the transfer.
 */
rt_err_t aic_dmac_memcpy_test(rt_uint32_t len, rt_uint32_t *kbps, int *ch);

#endif /* __DRV_DMAC_H__ */
