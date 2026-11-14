/*
 * Copyright (c) 2022-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Curated copy of the Luban-Lite external DMA-controller HAL interface
 * (bsp/artinchip/include/hal/hal_dma.h).  Only the D21x variant is kept:
 * the vendor file also carries V11/V12/3x register layouts behind
 * AIC_DMA_DRV_* macros, which this BSP does not use.
 */

#ifndef _ARTINCHIP_HAL_DMA_H_
#define _ARTINCHIP_HAL_DMA_H_

#include "aic_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* D21x DMA v1.0: eight channels, 8-byte descriptor alignment. */
#define AIC_DMA_CH_NUM      8
#define AIC_DMA_ALIGN_SIZE  8

enum dma_status
{
    DMA_COMPLETE,
    DMA_IN_PROGRESS,
    DMA_PAUSED,
    DMA_ERROR,
    DMA_OUT_OF_ORDER,
};

enum dma_transfer_direction
{
    DMA_MEM_TO_MEM,
    DMA_MEM_TO_DEV,
    DMA_DEV_TO_MEM,
    DMA_DEV_TO_DEV,
    DMA_TRANS_NONE,
};

enum dma_transfer_type
{
    TYPE_IO_SINGLE      = 0,
    TYPE_BURST          = 1,
    TYPE_MEMORY         = 2,
    TYPE_MEMORYSET      = 3,
    TYPE_IO_FAST        = 4,
    TYPE_IO_AUTO        = 10,
};

enum dma_slave_buswidth
{
    DMA_SLAVE_BUSWIDTH_UNDEFINED = 0,
    DMA_SLAVE_BUSWIDTH_1_BYTE = 1,
    DMA_SLAVE_BUSWIDTH_2_BYTES = 2,
    DMA_SLAVE_BUSWIDTH_3_BYTES = 3,
    DMA_SLAVE_BUSWIDTH_4_BYTES = 4,
    DMA_SLAVE_BUSWIDTH_8_BYTES = 8,
    DMA_SLAVE_BUSWIDTH_16_BYTES = 16,
    DMA_SLAVE_BUSWIDTH_32_BYTES = 32,
    DMA_SLAVE_BUSWIDTH_64_BYTES = 64,
};

typedef void (*dma_async_callback)(void *dma_async_param);

struct dma_slave_config
{
    enum dma_transfer_direction direction;
    unsigned long src_addr;
    unsigned long dst_addr;
    enum dma_slave_buswidth src_addr_width;
    enum dma_slave_buswidth dst_addr_width;
    u32 src_maxburst;
    u32 dst_maxburst;
    u32 slave_id;
};

struct dma_slave_table
{
    u32 id;
    u32 burst_num;
    u32 width_num;
    const u32 *burst;
    const u32 *width;
};

/*
 * D21x uses DMA v1.0: a task list is walked by the controller itself, so
 * the descriptor must stay cache-line aligned (64 bytes on C906).
 */
struct aic_dma_task
{
    u32 cfg;            /* transfer configuration */
    u32 src;            /* source address of this transfer package */
    u32 dst;            /* destination address of this transfer package */
    u32 len;            /* data length of this transfer package */
    u32 delay;          /* delay for period transfer */
    u32 p_next;         /* next package address seen by the controller */
    u32 mode;           /* negotiation mode */
    u32 pad[7];
    /* CPU-only list link, not used by the controller */
    struct aic_dma_task *v_next;
};

struct aic_dma_chan
{
    u8 ch_nr;           /* DRQ port number of the allocated channel */
    u8 used;
    u8 irq_type;
    bool cyclic;
    bool memset;
    unsigned long base;
    struct dma_slave_config cfg;
    volatile int lock;
    dma_async_callback callback;
    void *callback_param;
    struct aic_dma_task *desc;
};

int hal_dma_chan_prep_memset(struct aic_dma_chan *chan, u32 p_dest,
                             u32 value, u32 len);
int hal_dma_chan_prep_memcpy(struct aic_dma_chan *chan, u32 p_dest,
                             u32 p_src, u32 len);
int hal_dma_chan_prep_device(struct aic_dma_chan *chan, u32 p_dest,
                             u32 p_src, u32 len,
                             enum dma_transfer_direction dir);
int hal_dma_chan_prep_cyclic(struct aic_dma_chan *chan, u32 p_buf_addr,
                             u32 buf_len, u32 period_len,
                             enum dma_transfer_direction dir);
enum dma_status hal_dma_chan_tx_status(struct aic_dma_chan *chan,
                                       u32 *left_size);
int hal_dma_chan_start(struct aic_dma_chan *chan);
int hal_dma_chan_stop(struct aic_dma_chan *chan);
int hal_dma_chan_pause(struct aic_dma_chan *chan);
int hal_dma_chan_resume(struct aic_dma_chan *chan);
int hal_dma_chan_terminate_all(struct aic_dma_chan *chan);
int hal_dma_chan_register_cb(struct aic_dma_chan *chan,
                             dma_async_callback callback,
                             void *callback_param);
int hal_dma_chan_config(struct aic_dma_chan *chan,
                        struct dma_slave_config *config);
int hal_release_dma_chan(struct aic_dma_chan *chan);
struct aic_dma_chan *hal_request_dma_chan(void);
int hal_dma_init(void);
int hal_dma_deinit(void);
int hal_dma_chan_dump(int ch_nr);
int hal_wait_dma_chans_finish(void);

irqreturn_t hal_dma_irq(int irq, void *arg);

#ifdef __cplusplus
}
#endif

#endif /* _ARTINCHIP_HAL_DMA_H_ */
