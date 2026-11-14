/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * UART DMA helpers shared by the MSH command and the utest regression.
 */

#ifndef __DRV_UART_H__
#define __DRV_UART_H__

#include <rtthread.h>

#if defined(BSP_USING_UART1) && defined(BSP_USING_UART_DMA)
/*
 * Send len bytes through the DMAC on uart1 with the controller in MCR
 * internal-loopback mode, read them back through the normal serial RX
 * path and compare every byte.  No external wiring is required.
 *
 * Returns RT_EOK on success, -RT_EINVAL for an unsupported length,
 * -RT_ENOSYS when uart1 is absent, -RT_EBUSY when the device cannot be
 * opened, -RT_ENOMEM when the buffers cannot be allocated, -RT_ETIMEOUT
 * when the transfer or the loopback does not complete, and -RT_EIO on a
 * data mismatch.  kbps (may be NULL) receives the measured throughput.
 */
rt_err_t aic_uart1_dma_loopback_test(rt_uint32_t len, rt_uint32_t *kbps);

/*
 * Queue total bytes as chunks of chunk bytes without waiting (a writer
 * thread does the rt_device_write calls) while the RX FIFO is drained
 * here.  Exercises the back-to-back completion path, unaligned chunk
 * starts (use an odd chunk size) and, with hold_channels set, the polled
 * fallback when no DMAC channel is free.  At most eight chunks are
 * accepted so the writer never blocks.  Same return codes as above.
 */
rt_err_t aic_uart1_dma_loopback_stream(rt_uint32_t total, rt_uint32_t chunk,
                                       rt_bool_t hold_channels,
                                       rt_uint32_t *kbps);

/*
 * Run a TX DMA transfer while the device is also open with INT_RX and
 * the receive side goes through the standard serial RX ring (the 1 ms
 * poll thread), as an application would use it.
 */
rt_err_t aic_uart1_dma_mixed_rx_test(rt_uint32_t len, rt_uint32_t *kbps);

/*
 * Close uart1 while a DMA transfer is in flight, then verify that a new
 * transfer on the reopened device still works.  Returns RT_EOK when the
 * abort released the channel and a 64-byte loopback passes.
 */
rt_err_t aic_uart1_dma_close_test(void);
#endif

#endif /* __DRV_UART_H__ */
