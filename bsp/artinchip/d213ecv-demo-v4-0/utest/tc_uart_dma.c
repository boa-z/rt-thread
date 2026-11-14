/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * UART1 TX-DMA regression against the DMAC, using MCR internal loopback
 * so no external RS-232 peer is required.
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "utest.h"
#include "drv_uart.h"

#if defined(BSP_USING_UART1) && defined(BSP_USING_UART_DMA)

static void tc_uart_dma_small(void)
{
    uassert_int_equal(aic_uart1_dma_loopback_test(64, RT_NULL), RT_EOK);
}

static void tc_uart_dma_1k(void)
{
    uassert_int_equal(aic_uart1_dma_loopback_test(1024, RT_NULL), RT_EOK);
}

static void tc_uart_dma_4k(void)
{
    uassert_int_equal(aic_uart1_dma_loopback_test(4096, RT_NULL), RT_EOK);
}

static void tc_uart_dma_reject_short(void)
{
    uassert_int_equal(aic_uart1_dma_loopback_test(4, RT_NULL),
                      -RT_EINVAL);
}

/*
 * Eight chunks are queued without waiting, so the completion ISR starts
 * them back to back.  An odd chunk size makes every chunk after the
 * first start unaligned; the driver must stage them, not poll.
 */
static void tc_uart_dma_stream_misaligned(void)
{
    uassert_int_equal(aic_uart1_dma_loopback_stream(4096, 513, RT_FALSE,
                                                    RT_NULL),
                      RT_EOK);
}

static void tc_uart_dma_stream_aligned(void)
{
    uassert_int_equal(aic_uart1_dma_loopback_stream(8192, 1024, RT_FALSE,
                                                    RT_NULL),
                      RT_EOK);
}

/* Every DMAC channel is taken, so the driver must fall back to polling
 * and still deliver the complete stream. */
static void tc_uart_dma_no_channel(void)
{
    uassert_int_equal(aic_uart1_dma_loopback_stream(1024, 128, RT_TRUE,
                                                    RT_NULL),
                      RT_EOK);
}

static void tc_uart_dma_mixed_rx(void)
{
    uassert_int_equal(aic_uart1_dma_mixed_rx_test(1024, RT_NULL), RT_EOK);
}

static void tc_uart_dma_close_in_flight(void)
{
    uassert_int_equal(aic_uart1_dma_close_test(), RT_EOK);
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_uart_dma_small);
    UTEST_UNIT_RUN(tc_uart_dma_1k);
    UTEST_UNIT_RUN(tc_uart_dma_4k);
    UTEST_UNIT_RUN(tc_uart_dma_reject_short);
    UTEST_UNIT_RUN(tc_uart_dma_stream_misaligned);
    UTEST_UNIT_RUN(tc_uart_dma_stream_aligned);
    UTEST_UNIT_RUN(tc_uart_dma_no_channel);
    UTEST_UNIT_RUN(tc_uart_dma_mixed_rx);
    UTEST_UNIT_RUN(tc_uart_dma_close_in_flight);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.uart1.dma", RT_NULL, RT_NULL, 60);

#endif /* BSP_USING_UART1 && BSP_USING_UART_DMA */
