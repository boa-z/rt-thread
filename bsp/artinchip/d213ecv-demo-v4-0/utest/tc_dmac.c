/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * External DMA controller regression (BSP_USING_DMAC only).
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "utest.h"
#include "drv_dmac.h"
#include "hal_dma.h"

#ifdef BSP_USING_DMAC

static void tc_dmac_memcpy_small(void)
{
    uassert_int_equal(aic_dmac_memcpy_test(4096, RT_NULL, RT_NULL), RT_EOK);
}

static void tc_dmac_memcpy_large(void)
{
    uassert_int_equal(aic_dmac_memcpy_test(64 * 1024, RT_NULL, RT_NULL),
                      RT_EOK);
}

static void tc_dmac_reject_short(void)
{
    uassert_int_equal(aic_dmac_memcpy_test(4, RT_NULL, RT_NULL),
                      -RT_EINVAL);
}

static void tc_dmac_repeated(void)
{
    int i;

    for (i = 0; i < 8; i++)
    {
        uassert_int_equal(aic_dmac_memcpy_test(8192, RT_NULL, RT_NULL),
                          RT_EOK);
    }
}

/*
 * All eight channels can be owned at once, the ninth request fails
 * cleanly, and releasing them makes them available again.  A leaked
 * "used" flag or descriptor would surface as a failure here.
 */
static void tc_dmac_channel_exhaustion(void)
{
    struct aic_dma_chan *held[AIC_DMA_CH_NUM];
    struct aic_dma_chan *extra;
    int i;

    for (i = 0; i < AIC_DMA_CH_NUM; i++)
    {
        held[i] = hal_request_dma_chan();
        uassert_not_null(held[i]);
    }

    extra = hal_request_dma_chan();
    uassert_null(extra);

    for (i = 0; i < AIC_DMA_CH_NUM; i++)
    {
        if (held[i])
        {
            hal_release_dma_chan(held[i]);
        }
    }

    extra = hal_request_dma_chan();
    uassert_not_null(extra);
    if (extra)
    {
        hal_release_dma_chan(extra);
    }
}

static void tc_dmac_channel_reuse(void)
{
    int i;

    for (i = 0; i < 200; i++)
    {
        struct aic_dma_chan *ch = hal_request_dma_chan();

        uassert_not_null(ch);
        if (!ch)
        {
            return;
        }
        hal_release_dma_chan(ch);
    }

    /* The 36-descriptor pool must still be intact. */
    uassert_int_equal(aic_dmac_memcpy_test(4096, RT_NULL, RT_NULL), RT_EOK);
}

/*
 * Cyclic descriptors are built one task per period.  Reject the cases
 * that would build a misaligned chain, silently drop a partial period,
 * or exhaust the descriptor pool half-way, and leave no partial list
 * behind.
 */
static void tc_dmac_cyclic_checks(void)
{
    struct aic_dma_chan *ch = hal_request_dma_chan();

    uassert_not_null(ch);
    if (!ch)
    {
        return;
    }

    /* period must be a multiple of the 8-byte alignment */
    uassert_int_equal(hal_dma_chan_prep_cyclic(ch, 0x40000000, 64, 24,
                                              DMA_MEM_TO_DEV),
                      -EINVAL);
    /* buffer length must be a whole number of periods */
    uassert_int_equal(hal_dma_chan_prep_cyclic(ch, 0x40000000, 64, 48,
                                              DMA_MEM_TO_DEV),
                      -EINVAL);
    /* 4096 / 8 = 512 periods, more than the descriptor pool */
    uassert_int_equal(hal_dma_chan_prep_cyclic(ch, 0x40000000, 4096, 8,
                                              DMA_MEM_TO_DEV),
                      -EINVAL);
    uassert_null(ch->desc);

    hal_release_dma_chan(ch);
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_dmac_memcpy_small);
    UTEST_UNIT_RUN(tc_dmac_memcpy_large);
    UTEST_UNIT_RUN(tc_dmac_reject_short);
    UTEST_UNIT_RUN(tc_dmac_repeated);
    UTEST_UNIT_RUN(tc_dmac_channel_exhaustion);
    UTEST_UNIT_RUN(tc_dmac_channel_reuse);
    UTEST_UNIT_RUN(tc_dmac_cyclic_checks);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.dmac", RT_NULL, RT_NULL, 60);

#endif /* BSP_USING_DMAC */
