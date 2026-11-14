/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x external DMA controller (DMAC) driver.
 *
 * The controller programming lives in libraries/soc/d21x/hal/hal_dma.c (curated from
 * Luban-Lite, Apache-2.0).  This file only enables the clock/reset,
 * routes the controller IRQ, and provides a memory-to-memory self-test
 * plus a register dump.  Peripheral users (UART, SPI, I2S) request a
 * channel through hal_request_dma_chan() and drive it with the HAL; UART
 * TX is the first user (BSP_USING_UART_DMA).
 *
 * The controller is off by default (BSP_USING_DMAC=n).
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "aic_core.h"
#include <interrupt.h>
#include "aic_soc.h"
#include "hal_dma.h"
#include "drv_dma.h"
#include "drv_dmac.h"

#define DMAC_TEST_MIN_BYTES 64
#define DMAC_TEST_MAX_BYTES (1024 * 1024)
#define DMAC_TEST_TIMEOUT_MS 1000

static rt_sem_t dmac_done_sem;
static volatile rt_bool_t dmac_ready;

static void dmac_done_callback(void *param)
{
    rt_sem_t sem = (rt_sem_t)param;

    if (sem)
    {
        rt_sem_release(sem);
    }
}

static void dmac_isr(int irq, void *param)
{
    (void)param;

    hal_dma_irq(irq, RT_NULL);
}

static int drv_dmac_init(void)
{
    if (hal_clk_enable_deassertrst(CLK_DMA) < 0)
    {
        pr_err("dmac: clock enable / reset deassert failed\n");
        return -RT_EIO;
    }

    hal_dma_init();

    dmac_done_sem = rt_sem_create("dmac", 0, RT_IPC_FLAG_FIFO);
    if (!dmac_done_sem)
    {
        pr_err("dmac: cannot create completion semaphore\n");
        hal_clk_disable(CLK_DMA);
        return -RT_ENOMEM;
    }

    rt_hw_interrupt_install(IRQ_PLIC(DMA_IRQn), dmac_isr, RT_NULL, "dmac");
    rt_hw_interrupt_umask(IRQ_PLIC(DMA_IRQn));

    dmac_ready = RT_TRUE;
    pr_info("dmac: driver loaded (%d channels, IRQ %d)\n",
            AIC_DMA_CH_NUM, IRQ_PLIC(DMA_IRQn));

    return 0;
}
INIT_BOARD_EXPORT(drv_dmac_init);

rt_bool_t aic_dmac_ready(void)
{
    return dmac_ready;
}

rt_err_t aic_dmac_memcpy_test(rt_uint32_t len, rt_uint32_t *kbps, int *ch)
{
    rt_uint8_t *src = RT_NULL;
    rt_uint8_t *dst = RT_NULL;
    struct aic_dma_chan *chan;
    rt_err_t ret;
    rt_tick_t start, elapsed;
    rt_uint32_t i;
    rt_bool_t same = RT_TRUE;

    if (!dmac_ready)
    {
        return -RT_EBUSY;
    }

    if (len < DMAC_TEST_MIN_BYTES || len > DMAC_TEST_MAX_BYTES)
    {
        return -RT_EINVAL;
    }
    len &= ~7U;

    src = aic_dma_alloc(len);
    dst = aic_dma_alloc(len);
    if (!src || !dst)
    {
        ret = -RT_ENOMEM;
        goto out;
    }

    for (i = 0; i < len; i++)
    {
        src[i] = (rt_uint8_t)(i * 7 + 0x5a);
    }
    rt_memset(dst, 0, len);

    chan = hal_request_dma_chan();
    if (!chan)
    {
        ret = -RT_ENOMEM;
        goto out;
    }

    rt_sem_control(dmac_done_sem, RT_IPC_CMD_RESET, RT_NULL);
    hal_dma_chan_register_cb(chan, dmac_done_callback, dmac_done_sem);

    ret = hal_dma_chan_prep_memcpy(chan, (u32)(rt_ubase_t)dst,
                                   (u32)(rt_ubase_t)src, len);
    if (ret)
    {
        hal_release_dma_chan(chan);
        goto out;
    }

    start = rt_tick_get_millisecond();
    ret = hal_dma_chan_start(chan);
    if (ret)
    {
        hal_release_dma_chan(chan);
        goto out;
    }

    if (rt_sem_take(dmac_done_sem,
                    rt_tick_from_millisecond(DMAC_TEST_TIMEOUT_MS)) !=
        RT_EOK)
    {
        u32 left = 0;

        pr_err("dma_test: completion timeout, status %d, left %u\n",
               hal_dma_chan_tx_status(chan, &left), left);
        hal_dma_chan_dump(chan->ch_nr);
        hal_dma_chan_stop(chan);
        hal_release_dma_chan(chan);
        ret = -RT_ETIMEOUT;
        goto out;
    }
    elapsed = rt_tick_get_millisecond() - start;

    /* The DMA engine was the last writer: drop stale CPU lines. */
    aic_dma_cache_invalidate(dst, len);

    for (i = 0; i < len; i++)
    {
        if (src[i] != dst[i])
        {
            pr_err("dma_test: mismatch at %u (src 0x%02x dst 0x%02x)\n",
                   i, src[i], dst[i]);
            same = RT_FALSE;
            break;
        }
    }

    if (kbps)
    {
        *kbps = elapsed ? (rt_uint32_t)(((rt_uint64_t)len * 1000) /
                                        (elapsed * 1024))
                        : 0;
    }
    if (ch)
    {
        *ch = chan->ch_nr;
    }

    hal_dma_chan_stop(chan);
    hal_release_dma_chan(chan);

    ret = same ? RT_EOK : -RT_EIO;

out:
    if (src)
    {
        aic_dma_free(src);
    }
    if (dst)
    {
        aic_dma_free(dst);
    }

    return ret;
}

#ifdef RT_USING_FINSH
#include <finsh.h>

static void dma_dump(int argc, char **argv)
{
    int ch = 0;

    if (!dmac_ready)
    {
        rt_kprintf("dmac not initialized\n");
        return;
    }

    if (argc >= 2 && argv[1][0] >= '0' && argv[1][0] <= '9')
    {
        ch = argv[1][0] - '0';
        if (argv[1][1] >= '0' && argv[1][1] <= '9')
        {
            ch = ch * 10 + (argv[1][1] - '0');
        }
    }

    if (ch >= AIC_DMA_CH_NUM)
    {
        rt_kprintf("usage: dma_dump [0..%d]\n", AIC_DMA_CH_NUM - 1);
        return;
    }

    hal_dma_chan_dump(ch);
}
MSH_CMD_EXPORT(dma_dump, dump DMAC channel registers);

static void dma_test(int argc, char **argv)
{
    rt_uint32_t len = 4096;
    rt_uint32_t kbps = 0;
    int ch = -1;
    rt_err_t ret;

    if (!dmac_ready)
    {
        rt_kprintf("dmac not initialized\n");
        return;
    }

    if (argc >= 2)
    {
        len = 0;
        for (int i = 0; argv[1][i] >= '0' && argv[1][i] <= '9'; i++)
        {
            len = len * 10 + (rt_uint32_t)(argv[1][i] - '0');
        }
    }

    if (len < DMAC_TEST_MIN_BYTES || len > DMAC_TEST_MAX_BYTES)
    {
        rt_kprintf("usage: dma_test [bytes] (%d..%d, multiples of 8)\n",
                   DMAC_TEST_MIN_BYTES, DMAC_TEST_MAX_BYTES);
        return;
    }

    ret = aic_dmac_memcpy_test(len, &kbps, &ch);
    if (ret == RT_EOK && kbps == 0)
    {
        /* Shorter than the 1 ms tick: the copy itself is sub-millisecond. */
        rt_kprintf("dma_test: ch%d %u bytes PASS (<1 ms)\n", ch, len & ~7U);
    }
    else if (ret == RT_EOK)
    {
        rt_kprintf("dma_test: ch%d %u bytes = %u KB/s PASS\n",
                   ch, len & ~7U, kbps);
    }
    else
    {
        rt_kprintf("dma_test: FAIL (%d)\n", ret);
    }
}
MSH_CMD_EXPORT(dma_test, run a DMAC memory-to-memory copy test);
#endif /* RT_USING_FINSH */
