/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x SDMC host driver.
 *
 * The controller programming sequence, command encoding and clock divider
 * selection are ported from Luban-Lite (Apache-2.0):
 *   bsp/artinchip/drv/sdmc/drv_sdmc.c
 *   bsp/artinchip/hal/sdmc/hal_sdmc.c
 *
 * Transfer modes:
 *   - internal DMA (IDMA) for bulk and for non-word-sized SDIO transfers;
 *   - CPU FIFO for small transfers and as the fallback when the DMA
 *     buffer cannot be allocated.
 * Command completion and data completion are polled.  The only interrupt
 * used is the SDIO "card wants attention" source (SDMC1 IRQ 47,
 * SDMC2 IRQ 48).
 *
 * Two instances are supported:
 * SDMC1/2 instances use the SoC register and clock tables. Board slot
 * descriptors supply bus width, maximum frequency, block name and optional
 * module power sequencing. Filesystem mounting belongs to the board.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/dev_mmcsd_core.h>
#include <drivers/dev_sdio.h>

#include "aic_core.h"
#include <interrupt.h>
#include "aic_soc.h"
#include "hal_sdmc.h"
#include "drv_dma.h"
#include "drv_sdmc.h"
#include "aic_board.h"

#define MSEC_PER_SEC     1000U
#define SDMC_CMD_RETRY   2000000U

/*
 * SD_IO_RW_DIRECT (CMD52) answer, "R5": the flags and the card's I/O
 * state occupy bits [15:8], the byte written (or read) bits [7:0].
 * The SDIO core in this tree defines no R5 field names, so decode the
 * word here.  The state field is the SDIO I/O state: DIS, CMD, TRANS,
 * DATA, RCV, PRG for 0..5.
 */
#define SDMC_R5_COM_CRC_ERROR   (1U << 15)
#define SDMC_R5_ILLEGAL_COMMAND (1U << 14)
#define SDMC_R5_STATE_SHIFT     12
#define SDMC_R5_STATE_MASK      (3U << SDMC_R5_STATE_SHIFT)
#define SDMC_R5_STATE(x)        (((x) & SDMC_R5_STATE_MASK) >> SDMC_R5_STATE_SHIFT)
#define SDMC_R5_ERROR           (1U << 11)
#define SDMC_R5_FUNCTION_NUMBER (1U << 9)
#define SDMC_R5_OUT_OF_RANGE    (1U << 8)

#define SDMC_INSTANCE_MAX 2

/*
 * One descriptor covers up to 4 KiB.  With max_blk_count = 512 and
 * max_blk_size = 512 the block layer never asks for more than 256 KiB,
 * which is exactly 64 descriptors.
 */
#define SDMC_IDMA_DESC_MAX  64
#define SDMC_IDMA_MAX_BYTES (SDMC_IDMA_DESC_MAX * 4096)
#define SDMC_IDMA_MIN_BYTES 512

struct aic_sdmc_cfg
{
    u32 base;
    int irq;
    u32 clk_id;
    unsigned int index;
    const char *name;
};

struct aic_sdmc
{
    struct rt_mmcsd_host *rthost;
    struct aic_sdmc_host host;
    const struct aic_sdmc_cfg *cfg;
    const struct aic_sdmc_slot *slot;
    struct aic_sdmc_idma_desc *idma_desc;
    unsigned int index;
    unsigned int clock;
    unsigned int sclk_rate;
    unsigned int buswidth;
    unsigned int ddr_mode;
    int fifo_mode;
    u8 idma_enable;
    u8 is_enable;
    u8 dma_errors;
    u32 dma_requests;
    u32 fifo_requests;
};

/*
 * Only one request is in flight per host (the MMCSD core serialises them
 * with bus_lock), so one aligned descriptor array per instance is enough.
 * They are static because the stack of the MMCSD detect/data threads is
 * small.  Row 0 is unused so that rows can be indexed by instance number.
 */
static struct aic_sdmc_idma_desc
    g_sdmc_idma_desc[SDMC_INSTANCE_MAX + 1][SDMC_IDMA_DESC_MAX]
    __attribute__((aligned(AIC_DMA_CACHE_LINE)));

static const struct aic_sdmc_cfg g_sdmc_cfgs[] =
{
#ifdef BSP_USING_SDMC1
    {
        .base = 0x10450000UL,
        .irq = SDMC1_IRQn,
        .clk_id = CLK_SDMC1,
        .index = 1,
        .name = "sdmc1",
    },
#endif
#ifdef BSP_USING_SDMC2
    {
        .base = 0x10460000UL,
        .irq = SDMC2_IRQn,
        .clk_id = CLK_SDMC2,
        .index = 2,
        .name = "sdmc2",
    },
#endif
};

static struct aic_sdmc *g_sdmc[SDMC_INSTANCE_MAX + 1];
static rt_bool_t rescan_pending;
static rt_tick_t rescan_tick;

static u32 aic_sdmc_buswidth(u32 type)
{
    switch (type)
    {
    case SDMC_CTYPE_8BIT:
        return 8;
    case SDMC_CTYPE_4BIT:
        return 4;
    case SDMC_CTYPE_1BIT:
    default:
        return 1;
    }
}

static inline int resp_crc_type(struct rt_mmcsd_cmd *cmd)
{
    u32 type = resp_type(cmd);

    return (type == RESP_R1) || (type == RESP_R1B) || (type == RESP_R2) ||
           (type == RESP_R5) || (type == RESP_R6) || (type == RESP_R7);
}

static u32 aic_sdmc_get_timeout(struct aic_sdmc *host, const u32 size)
{
    unsigned int timeout;

    timeout = size * 8;
    timeout /= aic_sdmc_buswidth(host->buswidth);
    timeout *= 10; /* wait 10 times as long */
    timeout /= (host->clock / MSEC_PER_SEC);
    timeout /= host->ddr_mode ? 2 : 1;
    timeout = (timeout < MSEC_PER_SEC) ? MSEC_PER_SEC : timeout;

    return timeout;
}

static int aic_sdmc_fifo_transfer(struct aic_sdmc *host,
                                  struct rt_mmcsd_data *data,
                                  u32 *buf, u32 words)
{
    int ret = 0;
    u32 mask, left;
    rt_tick_t timeout, end;
    rt_tick_t start = rt_tick_get_millisecond();

    timeout = aic_sdmc_get_timeout(host, words * 4);
    left = words;

    for (;;)
    {
        mask = hal_sdmc_int_stat(&host->host);
        if (mask & (SDMC_DATA_ERR | SDMC_DATA_TOUT))
        {
            pr_err("sdmc: data error %u/%u words, %s, status 0x%x\n",
                   left, words,
                   (data->flags & DATA_DIR_READ) ? "read" : "write", mask);
            data->err = -RT_EIO;
            ret = -RT_EIO;
            break;
        }

        if (left)
        {
            if ((data->flags & DATA_DIR_READ) &&
                (mask & (SDMC_INT_RXDR | SDMC_INT_DAT_DONE)))
            {
                left = hal_sdmc_data_rx(&host->host, buf, left);
            }
            else if ((data->flags & DATA_DIR_WRITE) &&
                     (mask & SDMC_INT_TXDR))
            {
                left = hal_sdmc_data_tx(&host->host, buf, left);
            }
        }

        if ((mask & SDMC_INT_DAT_DONE) && !left)
        {
            break;
        }

        end = rt_tick_get_millisecond();
        if (end - start > timeout)
        {
            pr_err("sdmc: data timeout %u > %u, %u/%u words, status 0x%x\n",
                   (u32)(end - start), timeout, left, words, mask);
            data->err = -RT_ETIMEOUT;
            ret = -RT_ETIMEOUT;
            break;
        }
    }

    hal_sdmc_int_clr(&host->host, mask & ~SDMC_INT_FROM_SDIO);
    return ret;
}

/*
 * DAT_DONE only says that the card has finished the data phase.  The
 * internal DMAC can still be draining its FIFO into memory at that
 * moment, and the IDMAC status register is not updated in polled mode.
 * The last descriptor's OWN bit is cleared by the DMAC when it has
 * consumed the final buffer, so use that as the real completion gate.
 */
static rt_bool_t aic_sdmc_idma_wait_done(struct aic_sdmc *host, u32 ndesc,
                                         rt_tick_t timeout)
{
    rt_tick_t start = rt_tick_get_millisecond();
    u32 last = ndesc - 1;

    for (;;)
    {
        /*
         * The descriptor was written back before the DMAC started; the
         * DMAC clears OWN in memory, so drop the CPU's clean copy
         * before sampling it.
         */
        aic_dma_cache_invalidate(&host->idma_desc[last],
                                 sizeof(host->idma_desc[last]));

        if (!(host->idma_desc[last].flags & SDMC_IDMAC_OWN))
        {
            return RT_TRUE;
        }

        if ((rt_tick_get_millisecond() - start) > timeout)
        {
            return RT_FALSE;
        }
    }
}

static int aic_sdmc_idma_transfer(struct aic_sdmc *host,
                                  struct rt_mmcsd_data *data,
                                  void *buf, rt_size_t total, u32 ndesc)
{
    int ret = 0;
    u32 mask = 0, dma_mask = 0;
    rt_tick_t timeout, start;

    start = rt_tick_get_millisecond();
    timeout = aic_sdmc_get_timeout(host, total);

    for (;;)
    {
        mask = hal_sdmc_int_stat(&host->host);
        dma_mask = hal_sdmc_idma_int_stat(&host->host);

        if ((mask & (SDMC_DATA_ERR | SDMC_DATA_TOUT)) ||
            (dma_mask & (SDMC_IDMAC_INT_FBE | SDMC_IDMAC_INT_DU |
                         SDMC_IDMAC_INT_CES)))
        {
            pr_err("sdmc: dma error, %s, status 0x%x/0x%x, rsp 0x%08x\n",
                   (data->flags & DATA_DIR_READ) ? "read" : "write",
                   mask, dma_mask,
                   data->mrq ? data->mrq->cmd->resp[0] : 0);
            hal_sdmc_idma_dump(&host->host, host->idma_desc);
            data->err = -RT_EIO;
            ret = -RT_EIO;
            break;
        }

        /*
         * DAT_DONE is raised after the card has sent/received the last
         * data bit, and the internal DMAC has already drained the FIFO
         * by then (CTRST.FIFO_EMPTY).  The IDMAC status is not updated
         * in polled mode, so it cannot be used as a completion gate.
         */
        if (mask & SDMC_INT_DAT_DONE)
        {
            break;
        }

        if ((rt_tick_get_millisecond() - start) > timeout)
        {
            pr_err("sdmc: dma timeout %u, %s, status 0x%x/0x%x\n",
                   (u32)(rt_tick_get_millisecond() - start),
                   (data->flags & DATA_DIR_READ) ? "read" : "write",
                   mask, dma_mask);
            hal_sdmc_idma_dump(&host->host, host->idma_desc);
            data->err = -RT_ETIMEOUT;
            ret = -RT_ETIMEOUT;
            break;
        }
    }

    if (!ret && !aic_sdmc_idma_wait_done(host, ndesc, timeout))
    {
        pr_err("sdmc: dma stalled after DAT_DONE\n");
        data->err = -RT_ETIMEOUT;
        ret = -RT_ETIMEOUT;
    }

    if (!ret && (data->flags & DATA_DIR_READ))
    {
        /* The DMA engine, not the CPU, is the last writer. */
        aic_dma_cache_invalidate(buf, total);
    }

    hal_sdmc_idma_int_clr(&host->host, dma_mask);
    hal_sdmc_int_clr(&host->host, mask & ~SDMC_INT_FROM_SDIO);
    hal_sdmc_idma_finish(&host->host);

    if (ret)
    {
        hal_sdmc_idma_disable(&host->host);
    }

    return ret;
}

static rt_bool_t aic_sdmc_use_idma(struct aic_sdmc *host, rt_size_t total)
{
#ifdef BSP_SDMC_USE_IDMA
    /*
     * SDIO data transfers use the internal DMA engine when it is
     * available, matching the vendor driver's AIC_SDMC_IRQ_MODE.  The
     * CPU FIFO moves whole 32-bit words only, so keep short or unaligned
     * SDIO requests on the DMA path as well; the caller still owns the
     * aligned bounce buffer.  idma_enable is honoured here so the
     * "disable DMA after repeated errors" fallback can actually take
     * effect for an SDIO slot.
     */
    if (host->host.is_sdio)
    {
        return host->idma_enable && total <= SDMC_IDMA_MAX_BYTES;
    }

    if (!host->idma_enable || total > SDMC_IDMA_MAX_BYTES)
    {
        return RT_FALSE;
    }

    /*
     * The FIFO path moves whole 32-bit words, so a short, unaligned
     * transfer (notably SDIO CMD53 byte mode) must go through IDMA.
     */
    return (total >= SDMC_IDMA_MIN_BYTES) || (total & 3);
#else
    (void)host;
    (void)total;
    return RT_FALSE;
#endif
}

static int aic_sdmc_set_transfer_mode(struct aic_sdmc *host,
                                      struct rt_mmcsd_data *data)
{
    unsigned long mode = SDMC_CMD_DAT_EXP;

    if (data->flags & DATA_DIR_WRITE)
    {
        mode |= SDMC_CMD_DAT_WR;
    }

    return mode;
}

static int aic_sdmc_send_stop(struct aic_sdmc *host, struct rt_mmcsd_cmd *stop)
{
    u32 retry = SDMC_CMD_RETRY;
    u32 mask = 0;
    u32 flags = SDMC_CMD_STOP | SDMC_CMD_START | SDMC_CMD_USE_HOLD_REG;

    if (resp_type(stop) != RESP_NONE)
    {
        flags |= SDMC_CMD_RESP_EXP;
    }
    if (resp_crc_type(stop))
    {
        flags |= SDMC_CMD_RESP_CRC;
    }

    /*
     * A multi-block read is open-ended: CMD12 must follow DAT_DONE or the
     * card stays in data mode and CTRST.BUSY never clears.
     */
    hal_sdmc_int_clr(&host->host, SDMC_INT_ALL & ~SDMC_INT_FROM_SDIO);
    hal_sdmc_set_arg(&host->host, stop->arg);
    hal_sdmc_set_cmd(&host->host, flags | stop->cmd_code);

    while (retry-- > 0)
    {
        mask = hal_sdmc_int_stat(&host->host);
        if (mask & SDMC_INT_CMD_DONE)
        {
            break;
        }
    }

    if (!(mask & SDMC_INT_CMD_DONE))
    {
        stop->err = -RT_ETIMEOUT;
    }
    else if (mask & SDMC_INT_RTO)
    {
        stop->err = -RT_ETIMEOUT;
    }
    else if (mask & SDMC_INT_RESP_ERR)
    {
        stop->err = -RT_EIO;
    }
    else if (resp_crc_type(stop) && (mask & SDMC_INT_RCRC))
    {
        stop->err = -RT_EINVAL;
    }
    else if (resp_type(stop) != RESP_NONE)
    {
        hal_sdmc_get_rsp(&host->host, (u32 *)stop->resp, 0);
    }

    hal_sdmc_int_clr(&host->host, mask & ~SDMC_INT_FROM_SDIO);

    return stop->err;
}

/*
 * Ask the card to abort the data phase of the function whose CMD53 just
 * failed.  Function 0 has no abort select value, so only functions 1..7
 * can be aborted.  Like CMD12, the abort travels with the controller's
 * STOP flag: it must pre-empt the data state machine, not queue behind
 * a transfer that will never finish.
 */
static int aic_sdmc_card_abort(struct aic_sdmc *host, u32 fn, u32 *rsp_val)
{
    u32 mask = 0, retry = SDMC_CMD_RETRY;
    u32 rsp = 0;
    int err = 0;

    if (fn == 0 || fn > 7)
    {
        return -RT_EINVAL;
    }

    hal_sdmc_int_clr(&host->host, SDMC_INT_ALL & ~SDMC_INT_FROM_SDIO);
    hal_sdmc_set_arg(&host->host,
                     SDIO_ARG_CMD52_WRITE |
                     ((SDIO_REG_CCCR_IO_ABORT & SDIO_ARG_CMD52_REG_MASK) <<
                      SDIO_ARG_CMD52_REG_SHIFT) |
                     (fn & SDIO_ARG_CMD52_DATA_MASK));
    hal_sdmc_set_cmd(&host->host,
                     SD_IO_RW_DIRECT | SDMC_CMD_START |
                     SDMC_CMD_USE_HOLD_REG | SDMC_CMD_STOP |
                     SDMC_CMD_RESP_EXP | SDMC_CMD_RESP_CRC);

    while (retry-- > 0)
    {
        mask = hal_sdmc_int_stat(&host->host);
        if (mask & SDMC_INT_CMD_DONE)
        {
            break;
        }
    }

    if (!(mask & SDMC_INT_CMD_DONE))
    {
        err = -RT_ETIMEOUT;
    }
    else if (mask & (SDMC_INT_RTO | SDMC_INT_RESP_ERR))
    {
        err = -RT_EIO;
    }
    else
    {
        hal_sdmc_get_rsp(&host->host, &rsp, 0);
    }

    hal_sdmc_int_clr(&host->host, mask & ~SDMC_INT_FROM_SDIO);

    if (rsp_val)
    {
        *rsp_val = rsp;
    }

    return err;
}

/*
 * Bring the controller back to a known state after a stuck command or a
 * data error.  Without this the CTRST.BUSY bit can stay set and every
 * later request fails with -RT_EBUSY.
 */
static void aic_sdmc_recover(struct aic_sdmc *host)
{
    pr_warn("sdmc: recovering controller after error\n");

    hal_sdmc_soft_reset(&host->host);
    hal_sdmc_set_buswidth(&host->host, host->buswidth);
    hal_sdmc_set_ddrmode(&host->host, host->ddr_mode);
    hal_sdmc_set_phase(&host->host, 3, 0);
    hal_sdmc_fifo_init(&host->host, &host->host.fifoth_val);
    hal_sdmc_clk_enable(&host->host);
}

static void aic_sdmc_request(struct rt_mmcsd_host *rthost,
                             struct rt_mmcsd_req *req)
{
    struct aic_sdmc *host;
    struct rt_mmcsd_cmd *cmd;
    struct rt_mmcsd_data *data;
    s32 ret = 0, flags = 0;
    u32 retry = SDMC_CMD_RETRY;
    u32 mask = 0;
    rt_tick_t timeout = 500;
    rt_tick_t start;
    void *buf = RT_NULL;
    void *bounce = RT_NULL;
    rt_size_t total = 0;
    u32 words = 0, ndesc = 0;
    int use_dma = 0, bounce_dma = 0;
    int data_done = 0, need_recover = 0;
    u32 abort_fn = 0;

    RT_ASSERT(rthost != RT_NULL);
    RT_ASSERT(req != RT_NULL);
    RT_ASSERT(req->cmd != RT_NULL);

    host = (struct aic_sdmc *)rthost->private_data;
    RT_ASSERT(host != RT_NULL);

    cmd = req->cmd;
    data = req->data;

    if (data)
    {
        total = (rt_size_t)data->blksize * data->blks;
        words = (total + 3) / 4;
        if (!data->buf || total == 0)
        {
            cmd->err = -RT_EINVAL;
            goto out;
        }

        /*
         * IDMA needs a cache-line aligned buffer: always transfer from
         * an aligned bounce buffer, then copy back to the caller.  The
         * CPU FIFO path only needs word alignment and whole words.
         */
        use_dma = aic_sdmc_use_idma(host, total);
        if (use_dma)
        {
            bounce = aic_dma_alloc(total);
            if (bounce)
            {
                bounce_dma = 1;
            }
            else
            {
                use_dma = 0;
            }
        }

        if (!use_dma && ((((rt_ubase_t)data->buf & 3) != 0) || (total & 3)))
        {
            bounce = rt_malloc(words * 4);
            if (!bounce)
            {
                cmd->err = -RT_ENOMEM;
                goto out;
            }
        }

        if (bounce && (data->flags & DATA_DIR_WRITE))
        {
            if (!bounce_dma)
            {
                /* Keep the padding bytes of a partial last word defined. */
                rt_memset(bounce, 0, words * 4);
            }
            rt_memcpy(bounce, data->buf, total);
        }
        buf = bounce ? bounce : data->buf;
    }

    start = rt_tick_get_millisecond();
    if (cmd->cmd_code != STOP_TRANSMISSION)
    {
        while (hal_sdmc_is_busy(&host->host))
        {
            if (rt_tick_get_millisecond() - start > timeout)
            {
                pr_warn("sdmc: controller busy before CMD%u\n", cmd->cmd_code);
                cmd->err = -RT_EBUSY;
                goto out;
            }
            /* Called in thread context; do not spin away a whole tick. */
            rt_thread_mdelay(1);
        }
    }

    /* Keep a pending SDIO interrupt source; it is not ours to clear. */
    hal_sdmc_int_clr(&host->host, SDMC_INT_ALL & ~SDMC_INT_FROM_SDIO);

    if (data)
    {
        hal_sdmc_set_blk(&host->host, data->blksize, data->blks);
        hal_sdmc_reset(&host->host, SDMC_HCTRL1_FIFO_RESET);

        if (use_dma)
        {
            host->fifo_mode = 0;
            ndesc = hal_sdmc_idma_prepare(&host->host, data->blksize,
                                          data->blks, host->idma_desc, buf);
            if (!ndesc || ndesc > SDMC_IDMA_DESC_MAX)
            {
                pr_err("sdmc: bad descriptor count %u\n", ndesc);
                cmd->err = -RT_EINVAL;
                need_recover = 1;
                goto out;
            }

            /* The device reads the descriptors, so write them back. */
            aic_dma_cache_clean(host->idma_desc,
                                ndesc * sizeof(host->idma_desc[0]));

            if (data->flags & DATA_DIR_READ)
            {
                /* Drop stale/dirty CPU lines before the device writes. */
                aic_dma_cache_invalidate(buf, total);
            }
            else
            {
                aic_dma_cache_clean(buf, total);
            }

            hal_sdmc_idma_start(&host->host, host->idma_desc);
        }
        else
        {
            host->fifo_mode = 1;
            if (hal_sdmc_get_idma_status(&host->host))
            {
                hal_sdmc_idma_disable(&host->host);
            }
        }
    }

    hal_sdmc_set_arg(&host->host, cmd->arg);
    if (data)
    {
        flags = aic_sdmc_set_transfer_mode(host, data);
    }

    if (cmd->cmd_code == GO_IDLE_STATE)
    {
        flags |= SDMC_CMD_INIT;
    }

    if (cmd->cmd_code == STOP_TRANSMISSION)
    {
        flags |= SDMC_CMD_STOP;
    }
    else
    {
        flags |= SDMC_CMD_PRV_DAT_WAIT;
    }

    if (resp_type(cmd) != RESP_NONE)
    {
        flags |= SDMC_CMD_RESP_EXP;
        if (resp_type(cmd) == RESP_R2)
        {
            flags |= SDMC_CMD_RESP_LEN;
        }
    }

    if (resp_crc_type(cmd))
    {
        flags |= SDMC_CMD_RESP_CRC;
    }

    flags |= (cmd->cmd_code | SDMC_CMD_START | SDMC_CMD_USE_HOLD_REG);
    hal_sdmc_set_cmd(&host->host, flags);

    while (retry-- > 0)
    {
        mask = hal_sdmc_int_stat(&host->host);
        if (mask & SDMC_INT_CMD_DONE)
        {
            if (!data)
            {
                hal_sdmc_int_clr(&host->host, mask);
            }
            break;
        }
    }

    if (!(mask & SDMC_INT_CMD_DONE))
    {
        pr_warn("sdmc: CMD%u done timeout\n", cmd->cmd_code);
        cmd->err = -RT_ETIMEOUT;
        need_recover = 1;
        goto out;
    }

    if (mask & SDMC_INT_RTO)
    {
        /*
         * A card that does not answer a probe command (for example
         * CMD5 on an SD card) is normal.  The controller is idle, so
         * do not reset it here; the core will try the next probe.
         */
        hal_sdmc_int_clr(&host->host, mask & ~SDMC_INT_FROM_SDIO);
        cmd->err = -RT_ETIMEOUT;
        goto out;
    }
    else if (mask & SDMC_INT_RESP_ERR)
    {
        pr_err("sdmc: CMD%u response error\n", cmd->cmd_code);
        hal_sdmc_int_clr(&host->host, mask & ~SDMC_INT_FROM_SDIO);
        cmd->err = -RT_EIO;
        goto out;
    }
    else if (resp_crc_type(cmd) && (mask & SDMC_INT_RCRC))
    {
        pr_err("sdmc: CMD%u response CRC error\n", cmd->cmd_code);
        hal_sdmc_int_clr(&host->host, mask & ~SDMC_INT_FROM_SDIO);
        cmd->err = -RT_EINVAL;
        goto out;
    }

    if (resp_type(cmd) != RESP_NONE)
    {
        hal_sdmc_get_rsp(&host->host, (u32 *)cmd->resp,
                         resp_type(cmd) == RESP_R2);
    }

#ifdef BSP_SDMC_TRACE
    if ((cmd->cmd_code == SD_IO_RW_DIRECT) ||
        (cmd->cmd_code == SD_IO_RW_EXTENDED))
    {
        pr_info("sdmc%u: CMD%u arg %08x rsp %08x clk %u\n", host->index,
                cmd->cmd_code, cmd->arg, cmd->resp[0], host->clock);
    }
#endif

    /*
     * CMD5 answers only with an SDIO card.  Remember it so the clock
     * control keeps the card clock running for asynchronous interrupts.
     */
    if ((cmd->cmd_code == SD_IO_SEND_OP_COND) && !cmd->err)
    {
        host->host.is_sdio = 1;
    }

    if (data)
    {
        if (use_dma)
        {
            host->dma_requests++;
            ret = aic_sdmc_idma_transfer(host, data, buf, total, ndesc);
        }
        else
        {
            host->fifo_requests++;
            ret = aic_sdmc_fifo_transfer(host, data, (u32 *)buf, words);
        }

        if (ret)
        {
            cmd->err = ret;
            need_recover = 1;

            /*
             * A failed CMD53 leaves the card waiting for a data phase
             * that will never complete; byte-mode and single-block
             * transfers have no CMD12 to end it, so abort the function
             * through CCCR before the controller is reset.
             */
            if ((cmd->cmd_code == SD_IO_RW_EXTENDED) && host->host.is_sdio)
            {
                abort_fn = (cmd->arg >> SDIO_ARG_CMD53_FUNC_SHIFT) &
                           SDIO_ARG_CMD53_FUNC_MASK;
            }

            /*
             * Best effort: get the card out of its data phase before
             * resetting the controller.  Ignore the result; the
             * controller reset below is the fallback.
             */
            if (req->stop)
            {
                aic_sdmc_send_stop(host, req->stop);
            }

            /*
             * Do not keep hammering a DMA path that fails repeatedly;
             * the FIFO path can still serve the card.
             */
            if (use_dma && ++host->dma_errors >= 3)
            {
                host->idma_enable = 0;
                host->dma_errors = 0;
                pr_warn("sdmc: DMA disabled after repeated errors, "
                        "using FIFO\n");
            }
        }
        else
        {
            if (use_dma)
            {
                host->dma_errors = 0;
            }

            data_done = 1;
            if (req->stop && aic_sdmc_send_stop(host, req->stop))
            {
                need_recover = 1;
            }
        }
    }

out:
    if (need_recover)
    {
        int abort_ret = -RT_EINVAL;
        u32 abort_rsp = 0;

        /*
         * Try the abort while the controller still owns the data state
         * machine, then once more after the soft reset in case the
         * first attempt could not be issued.
         */
        if (abort_fn)
        {
            abort_ret = aic_sdmc_card_abort(host, abort_fn, &abort_rsp);
        }

        aic_sdmc_recover(host);

        if (abort_fn)
        {
            if (abort_ret)
            {
                abort_ret = aic_sdmc_card_abort(host, abort_fn, &abort_rsp);
            }

            /*
             * The R5 word shows whether the card accepted the abort:
             * ILLEGAL_COMMAND or OUT_OF_RANGE means the function does
             * not implement CCCR 0x06, ERROR means the abort itself
             * failed, and the I/O state says whether the function left
             * its data phase.
             */
            pr_warn("sdmc%u: CMD52 abort function %u ret %d rsp 0x%08x "
                    "(state %u%s%s%s%s%s)\n",
                    host->index, abort_fn, abort_ret, abort_rsp,
                    SDMC_R5_STATE(abort_rsp),
                    (abort_rsp & SDMC_R5_COM_CRC_ERROR) ? " crc-error" : "",
                    (abort_rsp & SDMC_R5_ILLEGAL_COMMAND) ? " illegal" : "",
                    (abort_rsp & SDMC_R5_ERROR) ? " error" : "",
                    (abort_rsp & SDMC_R5_FUNCTION_NUMBER) ? " func-error" : "",
                    (abort_rsp & SDMC_R5_OUT_OF_RANGE) ? " out-of-range" : "");
        }
    }

    if (bounce)
    {
        if (data_done && (data->flags & DATA_DIR_READ))
        {
            rt_memcpy(data->buf, bounce, total);
        }
        if (bounce_dma)
        {
            aic_dma_free(bounce);
        }
        else
        {
            rt_free(bounce);
        }
    }
    mmcsd_req_complete(rthost);
}

static u32 aic_sdmc_get_best_div(u32 sclk, u32 target_freq)
{
    u32 down, up, f_down, f_up;

    down = sclk / target_freq;
    up = DIV_ROUND_UP(sclk, target_freq);

    f_down = down == 0 ? sclk : sclk / down;
    f_up = up == 0 ? sclk : sclk / up;

    if ((f_down - target_freq) < (target_freq - f_up))
    {
        return down;
    }
    return up;
}

static int aic_sdmc_setup_bus(struct aic_sdmc *host, u32 freq)
{
    u32 mux, div, sclk = host->sclk_rate;

    if ((freq == host->clock) || (freq == 0))
    {
        return 0;
    }

    if (sclk == freq)
    {
        mux = 1;
        div = 0;
    }
    else
    {
        div = aic_sdmc_get_best_div(sclk, freq);
        if (div <= 4)
        {
            mux = DIV_ROUND_UP(div, 2);
        }
        else if (div % 8)
        {
            mux = 2;
        }
        else
        {
            mux = 4;
        }

        div /= mux * 2;
        if (div > SDMC_CLKCTRL_DIV_MAX)
        {
            div = SDMC_CLKCTRL_DIV_MAX;
        }
    }

    aic_sdmc_set_ext_clk_mux(&host->host, mux);
    hal_sdmc_set_div(&host->host, div);
    hal_sdmc_set_cmd(&host->host,
                     SDMC_CMD_PRV_DAT_WAIT | SDMC_CMD_UPD_CLK | SDMC_CMD_START);

    if (hal_sdmc_wait_cmd_started(&host->host))
    {
        return -RT_ETIMEOUT;
    }

    hal_sdmc_clk_enable(&host->host);
    hal_sdmc_set_cmd(&host->host,
                     SDMC_CMD_PRV_DAT_WAIT | SDMC_CMD_UPD_CLK | SDMC_CMD_START);

    if (hal_sdmc_wait_cmd_started(&host->host))
    {
        return -RT_ETIMEOUT;
    }

    host->clock = freq;
    return 0;
}

static int aic_sdmc_init(struct aic_sdmc *host)
{
    if (hal_sdmc_reset(&host->host, SDMC_HCTRL1_RESET_ALL))
    {
        pr_err("sdmc: controller reset timed out\n");
        return -RT_EIO;
    }

    aic_sdmc_setup_bus(host, host->rthost->freq_min);
    hal_sdmc_init(&host->host);
    hal_sdmc_fifo_init(&host->host, &host->host.fifoth_val);
    hal_sdmc_set_phase(&host->host, host->slot->drive_phase,
                       host->slot->sample_phase);
    hal_sdmc_clk_enable(&host->host);

    return 0;
}

static void aic_sdmc_set_iocfg(struct rt_mmcsd_host *rthost,
                               struct rt_mmcsd_io_cfg *io_cfg)
{
    struct aic_sdmc *host;

    RT_ASSERT(rthost != RT_NULL);
    RT_ASSERT(rthost->private_data != RT_NULL);
    RT_ASSERT(io_cfg != RT_NULL);

    host = (struct aic_sdmc *)rthost->private_data;

    switch (io_cfg->bus_width)
    {
    case MMCSD_BUS_WIDTH_8:
        host->buswidth = SDMC_CTYPE_8BIT;
        break;
    case MMCSD_BUS_WIDTH_4:
        host->buswidth = SDMC_CTYPE_4BIT;
        break;
    default:
        host->buswidth = SDMC_CTYPE_1BIT;
        break;
    }

    hal_sdmc_set_buswidth(&host->host, host->buswidth);
    hal_sdmc_set_ddrmode(&host->host, host->ddr_mode);

    switch (io_cfg->power_mode)
    {
    case MMCSD_POWER_ON:
        if (!host->is_enable)
        {
            host->is_enable = 1;
            hal_sdmc_reset(&host->host, SDMC_HCTRL1_RESET_ALL);
            host->clock = 0;
        }
        aic_sdmc_setup_bus(host, io_cfg->clock);
        break;
    case MMCSD_POWER_OFF:
        host->is_enable = 0;
        hal_sdmc_clk_disable(&host->host);
        hal_sdmc_set_cmd(&host->host,
                         SDMC_CMD_PRV_DAT_WAIT | SDMC_CMD_UPD_CLK |
                             SDMC_CMD_START);
        break;
    case MMCSD_POWER_UP:
    default:
        break;
    }
}

/*
 * Only the SDIO interrupt source is enabled in SDMC_INTEN; command and
 * data completion stay polled.  The status register still latches those
 * other bits (and the polled request path clears them), so this handler
 * must touch nothing but the SDIO bit: clearing a CMD_DONE/DAT_DONE that
 * a concurrent polled command is waiting for would turn into a spurious
 * command timeout.  A non-SDIO bit cannot raise the PLIC line here
 * because it is not enabled, so leaving it in the status register is
 * harmless.
 */
static void aic_sdmc_isr(int irq, void *param)
{
    struct aic_sdmc *host = (struct aic_sdmc *)param;
    u32 mask;

    (void)irq;

    if (!host)
    {
        return;
    }

    mask = hal_sdmc_int_stat(&host->host);

    if (mask & SDMC_INT_FROM_SDIO)
    {
        hal_sdmc_int_clr(&host->host, SDMC_INT_FROM_SDIO);
        sdio_irq_wakeup(host->rthost);
    }
}

static void aic_sdmc_enable_sdio_irq(struct rt_mmcsd_host *rthost,
                                     rt_int32_t en)
{
    struct aic_sdmc *host = (struct aic_sdmc *)rthost->private_data;

    if (en)
    {
        /* SDIO cards need a free-running card clock for async IRQs. */
        host->host.is_sdio = 1;
        hal_sdmc_clk_enable(&host->host);
    }

    hal_sdmc_sdio_irq_enable(&host->host, en);
}

static const struct rt_mmcsd_host_ops aic_sdmc_ops = {
    .request = aic_sdmc_request,
    .set_iocfg = aic_sdmc_set_iocfg,
    .enable_sdio_irq = aic_sdmc_enable_sdio_irq,
};

static void aic_sdmc_setup_cfg(struct rt_mmcsd_host *rthost)
{
    struct aic_sdmc *host = (struct aic_sdmc *)rthost->private_data;

    rthost->ops = &aic_sdmc_ops;
    rthost->freq_min = SDMC_CLOCK_MIN;
    rthost->freq_max = host->slot->max_hz;
    rthost->valid_ocr = VDD_32_33 | VDD_33_34;
    rthost->flags = MMCSD_MUTBLKWRITE | MMCSD_SUP_HIGHSPEED |
                    MMCSD_SUP_SDIO_IRQ;

    if (host->buswidth == SDMC_CTYPE_4BIT)
    {
        rthost->flags |= MMCSD_BUSWIDTH_4;
    }
    else if (host->buswidth == SDMC_CTYPE_8BIT)
    {
        rthost->flags |= MMCSD_BUSWIDTH_8;
    }

    /*
     * 64 KiB per request: large enough to amortise command setup and
     * to exercise multi-descriptor IDMA, small enough that the FIFO
     * fallback bounce buffer stays reasonable.
     */
    rthost->max_seg_size = 64 * 1024;
    rthost->max_dma_segs = 1;
    rthost->max_blk_size = 512;
    rthost->max_blk_count = 512;
}

static s32 aic_sdmc_clk_init(struct aic_sdmc *host)
{
    s32 ret;

    hal_clk_disable_assertrst(host->cfg->clk_id);
    hal_clk_disable(host->cfg->clk_id);

    hal_clk_set_freq(host->cfg->clk_id, SDMC_CLOCK_MAX);
    host->sclk_rate = hal_clk_get_freq(host->cfg->clk_id) / 2;
    pr_info("%s: sclk %u kHz, parent %lu kHz\n",
            host->cfg->name, host->sclk_rate / 1000,
            (unsigned long)hal_clk_get_freq(CLK_PLL_FRA0) / 1000);

    ret = hal_clk_enable(host->cfg->clk_id);
    if (ret < 0)
    {
        pr_err("%s: clock enable failed\n", host->cfg->name);
        return -RT_EIO;
    }

    ret = hal_clk_enable_deassertrst(host->cfg->clk_id);
    if (ret < 0)
    {
        pr_err("%s: reset deassert failed\n", host->cfg->name);
        return -RT_EIO;
    }

    return 0;
}

static int aic_sdmc_probe(const struct aic_sdmc_cfg *cfg, unsigned int index)
{
    struct rt_mmcsd_host *rthost;
    struct aic_sdmc *host;
    const struct aic_sdmc_slot *slot = aic_board_sdmc_slot(index);

    if (!slot || !slot->block_name || !slot->block_name[0] ||
        slot->drive_phase > 3 || slot->sample_phase > 3 ||
        rt_strlen(slot->block_name) >= RT_NAME_MAX ||
        (slot->bus_width != 1 && slot->bus_width != 4 && slot->bus_width != 8) ||
        slot->max_hz < SDMC_CLOCK_MIN || slot->max_hz > SDMC_CLOCK_MAX)
        return -RT_EINVAL;
    rthost = mmcsd_alloc_host();
    if (!rthost)
    {
        return -RT_ENOMEM;
    }

    host = rt_malloc(sizeof(*host));
    if (!host)
    {
        mmcsd_free_host(rthost);
        return -RT_ENOMEM;
    }

    rt_memset(host, 0, sizeof(*host));
    host->index = index;
    host->cfg = cfg;
    host->slot = slot;
    host->host.base = (volatile void *)(rt_ubase_t)cfg->base;
    host->idma_desc = g_sdmc_idma_desc[index];
    host->rthost = rthost;
    host->buswidth = slot->bus_width == 8 ? SDMC_CTYPE_8BIT :
                      slot->bus_width == 4 ? SDMC_CTYPE_4BIT : SDMC_CTYPE_1BIT;
    host->fifo_mode = 1;
#ifdef BSP_SDMC_USE_IDMA
    host->idma_enable = 1;
#else
    host->idma_enable = 0;
#endif

    if (slot->power_on && slot->power_on() != RT_EOK)
    {
        rt_free(host);
        mmcsd_free_host(rthost);
        return -RT_EIO;
    }

    if (aic_sdmc_clk_init(host) < 0)
    {
        rt_free(host);
        mmcsd_free_host(rthost);
        return -RT_EIO;
    }

    host->host.fifoth_val = MSIZE(2) | RX_WMARK(7) | TX_WMARK(8);
    host->host.is_sdio = !!slot->is_sdio;
    rthost->private_data = host;
    /* Keep the vendor block-device name so mount scripts are portable. */
    rt_strncpy(rthost->name, slot->block_name, RT_NAME_MAX);
    aic_sdmc_setup_cfg(rthost);

    if (aic_sdmc_init(host) < 0)
    {
        rt_free(host);
        mmcsd_free_host(rthost);
        return -RT_EIO;
    }

    /*
     * Only the SDIO "card wants attention" source is routed to the
     * PLIC; it stays masked in the controller until a function driver
     * calls enable_sdio_irq().
     */
    rt_hw_interrupt_install(IRQ_PLIC(cfg->irq), aic_sdmc_isr, host,
                            cfg->name);
    rt_hw_interrupt_umask(IRQ_PLIC(cfg->irq));

    g_sdmc[index] = host;
    pr_info("%s: driver loaded (%u-bit, %s, SDIO IRQ armable)\n",
            cfg->name, aic_sdmc_buswidth(host->buswidth),
            host->idma_enable ? "IDMA polling" : "FIFO polling");

    mmcsd_change(rthost);
    return 0;
}

static int drv_sdmc_init(void)
{
    unsigned int i;
    int probed = 0;

    for (i = 0; i < sizeof(g_sdmc_cfgs) / sizeof(g_sdmc_cfgs[0]); i++)
    {
        if (aic_sdmc_probe(&g_sdmc_cfgs[i], g_sdmc_cfgs[i].index) == 0)
        {
            probed++;
        }
        else
        {
            /* Keep a working instance even when the other slot is empty
             * or fails to initialise. */
            pr_err("%s: probe failed\n", g_sdmc_cfgs[i].name);
        }
    }

    return probed ? 0 : -RT_EIO;
}
INIT_DEVICE_EXPORT(drv_sdmc_init);

struct rt_mmcsd_host *aic_sdmc_host(unsigned int index)
{
    if (index > SDMC_INSTANCE_MAX || !g_sdmc[index])
    {
        return RT_NULL;
    }

    return g_sdmc[index]->rthost;
}

#ifdef RT_USING_FINSH
#include <finsh.h>

#ifdef BSP_USING_SDMC1
static void sd_rescan(void)
{
    if (!g_sdmc[1])
    {
        rt_kprintf("sdmc1 not initialized\n");
        return;
    }

    /*
     * mmcsd_change() means "re-check the slot" to the core: it probes
     * when host->card is NULL and *removes* the card otherwise.  The
     * removal path frees the card and the block device, and re-inserting
     * is not supported by this polled, card-detect-less port.  Refuse
     * rather than tearing down a live card.
     */
    if (g_sdmc[1]->rthost->card != RT_NULL ||
        rt_device_find(g_sdmc[1]->slot->block_name) != RT_NULL)
    {
        rescan_pending = RT_FALSE;
        rt_kprintf("sdmc1: card already detected; hot-swap needs a reboot "
                   "(no card-detect pin)\n");
        return;
    }

    /*
     * A second mmcsd_change() queued while the first probe is still
     * running would be interpreted as "card removed" once the first
     * probe succeeds, so refuse overlapping rescans.
     */
    if (rescan_pending &&
        (rt_tick_get() - rescan_tick) < rt_tick_from_millisecond(5000))
    {
        rt_kprintf("sdmc1: rescan already in progress\n");
        return;
    }

    rescan_pending = RT_TRUE;
    rescan_tick = rt_tick_get();
    mmcsd_change(g_sdmc[1]->rthost);
    rt_kprintf("sdmc1: rescan requested\n");
}
MSH_CMD_EXPORT(sd_rescan, rescan an empty SDMC1 slot);

static void sd_dma(int argc, char **argv)
{
    if (!g_sdmc[1])
    {
        rt_kprintf("sdmc1 not initialized\n");
        return;
    }

    if (argc < 2)
    {
        rt_kprintf("sdmc1: DMA %s, requests dma=%u fifo=%u "
                   "(use sd_dma 0|1 to change)\n",
                   g_sdmc[1]->idma_enable ? "enabled" : "disabled",
                   g_sdmc[1]->dma_requests, g_sdmc[1]->fifo_requests);
        return;
    }

    g_sdmc[1]->idma_enable = (argv[1][0] == '1') ? 1 : 0;
    rt_kprintf("sdmc1: DMA %s\n",
               g_sdmc[1]->idma_enable ? "enabled" : "disabled");
}
MSH_CMD_EXPORT(sd_dma, enable or disable SDMC1 internal DMA);

static void sd_perf(int argc, char **argv)
{
    rt_device_t dev;
    rt_uint8_t *buf;
    rt_uint32_t blocks = 1024, i;
    rt_size_t got;
    rt_tick_t start, elapsed;

    if (argc >= 2)
    {
        blocks = 0;
        for (i = 0; argv[1][i] >= '0' && argv[1][i] <= '9'; i++)
        {
            blocks = blocks * 10 + (rt_uint32_t)(argv[1][i] - '0');
        }
    }

    if (blocks == 0 || blocks > 4096)
    {
        rt_kprintf("usage: sd_perf [blocks] (1..4096)\n");
        return;
    }

    dev = g_sdmc[1] ? rt_device_find(g_sdmc[1]->slot->block_name) : RT_NULL;
    if (!dev)
    {
        rt_kprintf("sd0 not present\n");
        return;
    }

    buf = aic_dma_alloc((rt_size_t)blocks * 512);
    if (!buf)
    {
        rt_kprintf("sd_perf: out of memory\n");
        return;
    }

    if (rt_device_open(dev, RT_DEVICE_OFLAG_RDONLY) != RT_EOK)
    {
        rt_kprintf("sd_perf: cannot open sd0\n");
        aic_dma_free(buf);
        return;
    }

    start = rt_tick_get_millisecond();
    got = rt_device_read(dev, 0, buf, blocks);
    elapsed = rt_tick_get_millisecond() - start;

    rt_device_close(dev);
    aic_dma_free(buf);

    if (got != blocks)
    {
        rt_kprintf("sd_perf: read %u/%u blocks in %u ms\n",
                   (unsigned)got, blocks, (unsigned)elapsed);
        return;
    }

    rt_kprintf("sd_perf: %u blocks in %u ms = %u KB/s (%s)\n",
               blocks, (unsigned)elapsed,
               elapsed ? (unsigned)(((rt_uint64_t)blocks * 512) / elapsed)
                       : 0,
               g_sdmc[1]->idma_enable ? "IDMA" : "FIFO");
}
MSH_CMD_EXPORT(sd_perf, measure SD card sequential read throughput);
#endif /* BSP_USING_SDMC1 */

#ifdef BSP_USING_SDMC2
/*
 * Bring-up aid for the SDIO slot: the module has no block device, so
 * there is no "list device" entry to check.  This prints what the MMCSD
 * core found and reads back a few CCCR registers with CMD52.
 */
static u32 aic_sdmc_reg(struct aic_sdmc *host, u32 off)
{
    return *(volatile u32 *)((ulong)host->cfg->base + off);
}

/*
 * Dump the host side of a slot: clock tree, transfer-engine selection and
 * the controller registers that matter for a stuck transfer.  Called by
 * the "sdmc_dump" command and by utest cases so a failing transfer lands
 * in the test log together with the register state it happened in.
 */
void aic_sdmc_dump(unsigned int index)
{
    struct aic_sdmc *host;

    if (index > SDMC_INSTANCE_MAX || !g_sdmc[index])
    {
        rt_kprintf("sdmc%u: not probed\n", index);
        return;
    }

    host = g_sdmc[index];

    rt_kprintf("%s: clk %u Hz, sclk %u Hz, clk source %u Hz, %u-bit, "
               "idma %u\n", host->cfg->name, host->clock, host->sclk_rate,
               (unsigned)hal_clk_get_freq(host->cfg->clk_id),
               aic_sdmc_buswidth(host->buswidth), host->idma_enable);
    rt_kprintf("%s: clkctrl %08x dlyctrl %08x hctrl1 %08x hctrl2 %08x\n",
               host->cfg->name, aic_sdmc_reg(host, SDMC_CLKCTRL),
               aic_sdmc_reg(host, SDMC_DLYCTRL),
               aic_sdmc_reg(host, SDMC_HCTRL1),
               aic_sdmc_reg(host, SDMC_HCTRL2));
    rt_kprintf("%s: pbuscfg %08x inten %08x oinst %08x idmacap %08x\n",
               host->cfg->name, aic_sdmc_reg(host, SDMC_PBUSCFG),
               aic_sdmc_reg(host, SDMC_INTEN),
               aic_sdmc_reg(host, SDMC_OINTST),
               aic_sdmc_reg(host, SDMC_IDMARCAP));
    rt_kprintf("%s: ctc %08x fifocfg %08x ttmc %08x ctrst %08x\n",
               host->cfg->name, aic_sdmc_reg(host, SDMC_CTC),
               aic_sdmc_reg(host, SDMC_FIFOCFG),
               aic_sdmc_reg(host, SDMC_TTMC),
               aic_sdmc_reg(host, SDMC_CTRST));
    rt_kprintf("%s: idmast %08x idmasaddr %08x hinfo %08x verid %08x\n",
               host->cfg->name, aic_sdmc_reg(host, SDMC_IDMAST),
               aic_sdmc_reg(host, SDMC_IDMASADDR),
               aic_sdmc_reg(host, SDMC_HINFO),
               aic_sdmc_reg(host, SDMC_VERID));
}

static void sdmc_dump(int argc, char **argv)
{
    unsigned int index = 2;

    if (argc >= 2)
    {
        index = (unsigned int)(argv[1][0] - '0');
    }

    aic_sdmc_dump(index);
}
MSH_CMD_EXPORT(sdmc_dump, dump SDMC host registers);

static void sdio_info(int argc, char **argv)
{
    struct rt_mmcsd_host *rthost = g_sdmc[2] ? g_sdmc[2]->rthost : RT_NULL;
    struct rt_mmcsd_card *card;
    struct rt_sdio_function *func0;
    rt_int32_t err = 0;
    rt_uint8_t rev, bus, io_en, io_rdy;
    int i;

    (void)argc;
    (void)argv;

    if (!rthost)
    {
        rt_kprintf("sdmc2 not initialized\n");
        return;
    }

    card = rthost->card;
    if (!card)
    {
        rt_kprintf("sdmc2: no card detected\n");
        return;
    }

    rt_kprintf("sdmc2: card type %s, %u function(s), max %u Hz\n",
               card->card_type == CARD_TYPE_SDIO_COMBO ? "SDIO combo" :
               card->card_type == CARD_TYPE_SDIO ? "SDIO" : "other",
               card->sdio_function_num, card->cis.max_tran_speed);
    rt_kprintf("sdmc2: manf 0x%04x product 0x%04x, %s speed, %u-bit\n",
               card->cis.manufacturer, card->cis.product,
               (card->flags & CARD_FLAG_HIGHSPEED) ? "high" : "default",
               card->cccr.bus_width ? 4 : 1);

    func0 = card->sdio_function[0];
    if (!func0)
    {
        rt_kprintf("sdmc2: function 0 missing\n");
        return;
    }

    rev = sdio_io_readb(func0, SDIO_REG_CCCR_CCCR_REV, &err);
    if (!err)
        bus = sdio_io_readb(func0, SDIO_REG_CCCR_BUS_IF, &err);
    if (!err)
        io_en = sdio_io_readb(func0, SDIO_REG_CCCR_IO_EN, &err);
    if (!err)
        io_rdy = sdio_io_readb(func0, SDIO_REG_CCCR_IO_RDY, &err);

    if (err)
    {
        rt_kprintf("sdmc2: CCCR read failed (%d)\n", err);
        return;
    }

    rt_kprintf("sdmc2: CCCR rev %u, bus 0x%02x, IO_EN 0x%02x, "
               "IO_RDY 0x%02x\n", rev, bus, io_en, io_rdy);

    for (i = 1; i <= card->sdio_function_num; i++)
    {
        struct rt_sdio_function *func = card->sdio_function[i];

        if (!func)
            continue;
        rt_kprintf("sdmc2: func%d code 0x%02x manf 0x%04x product 0x%04x, "
                   "max block %u\n", i, func->func_code, func->manufacturer,
                   func->product, func->max_blk_size);
    }
}
MSH_CMD_EXPORT(sdio_info, print SDMC2 SDIO card information);

static void sdio_dma(int argc, char **argv)
{
    if (!g_sdmc[2])
    {
        rt_kprintf("sdmc2 not initialized\n");
        return;
    }

    if (argc < 2)
    {
        rt_kprintf("sdmc2: DMA %s, requests dma=%u fifo=%u "
                   "(use sdio_dma 0|1 to change)\n",
                   g_sdmc[2]->idma_enable ? "enabled" : "disabled",
                   g_sdmc[2]->dma_requests, g_sdmc[2]->fifo_requests);
        return;
    }

    g_sdmc[2]->idma_enable = (argv[1][0] == '1') ? 1 : 0;
    g_sdmc[2]->dma_errors = 0;
    rt_kprintf("sdmc2: DMA %s\n",
               g_sdmc[2]->idma_enable ? "enabled" : "disabled");
}
MSH_CMD_EXPORT(sdio_dma, enable or disable SDMC2 internal DMA);
#endif /* BSP_USING_SDMC2 */


#endif /* RT_USING_FINSH */
