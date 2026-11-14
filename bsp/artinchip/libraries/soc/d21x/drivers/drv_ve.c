/*
 * Copyright (c) 2026 RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 * D213 VE v1.0 resource/IRQ adapter. Codec register programming stays in
 * clients. IRQ/status and clock sequence follow luban-lite-jc V1.3.2.
 */
#include <rtthread.h>
#include <rthw.h>
#include <rtdevice.h>
#include <interrupt.h>
#include "aic_soc.h"
#include "aic_core.h"
#include "aic_hal_reset.h"
#include "aic_drv_ve.h"

#define VE_BASE             0x18c00000UL
#define VE_IRQ              61
#define VE_CLOCK            200000000UL
#define VE_TIMEOUT_MS       2000

struct aic_ve_client
{
    struct rt_device dev;
    struct rt_mutex engine;
    struct rt_semaphore done;
    volatile rt_uint32_t status;
    rt_thread_t owner;
    rt_bool_t opened;
    rt_bool_t initialized;
    rt_bool_t completed;
};
static struct aic_ve_client ve;

static void ve_isr(int vector, void *arg)
{
    struct aic_ve_client *v = arg;
    rt_uint32_t status = 0;
    rt_uint32_t s;
    (void)vector;

    if (readl(VE_BASE + 0x10))
    {
        s = readl(VE_BASE + 0x128);
        status |= s;
        writel(s | 0x70000, VE_BASE + 0x128);
    }
    if (readl(VE_BASE + 0x14))
    {
        s = readl(VE_BASE + 0x2004);
        status |= s;
        writel(s | 0xf, VE_BASE + 0x2004);
    }
    if (readl(VE_BASE + 0x18))
    {
        s = readl(VE_BASE + 0xc04);
        status |= s;
        writel(s | 0xf, VE_BASE + 0xc04);
    }
    writel(0, VE_BASE + 0xc);
    /* A second level-triggered PLIC delivery or an idle IRQ is not a
     * completion for the next transaction. */
    if (status && v->owner)
    {
        v->status |= status;
        rt_sem_release(&v->done);
    }
}

/* Called with engine held. Mask the source while clearing completion state.
 * Reset must stop DMA before WAIT reports a timeout to the caller. */
static int ve_reset_hw(void)
{
    int ret;
    rt_hw_interrupt_mask(IRQ_PLIC(VE_IRQ));
    writel(0, VE_BASE + 0xc);
    ret = hal_reset_assert(RESET_VE);
    if (!ret) ret = hal_reset_deassert(RESET_VE);
    rt_sem_control(&ve.done, RT_IPC_CMD_RESET, RT_NULL);
    ve.status = 0;
    ve.completed = RT_TRUE;
    aicos_dma_sync();
    if (!ret && ve.opened) rt_hw_interrupt_umask(IRQ_PLIC(VE_IRQ));
    return ret;
}

struct aic_ve_client *drv_ve_open(void)
{
    int ret;
    if (!ve.initialized || !rt_thread_self() || rt_interrupt_get_nest()) return RT_NULL;
    ret = rt_mutex_take(&ve.engine, rt_tick_from_millisecond(VE_TIMEOUT_MS));
    if (ret) return RT_NULL;
    if (ve.opened) { rt_mutex_release(&ve.engine); return RT_NULL; }
    ret = hal_clk_set_freq(CLK_VE, VE_CLOCK);
    if (!ret) ret = hal_clk_enable(CLK_VE);
    if (!ret) ret = ve_reset_hw();
    if (ret)
    {
        hal_reset_assert(RESET_VE);
        hal_clk_disable(CLK_VE);
        rt_mutex_release(&ve.engine);
        return RT_NULL;
    }
    ve.opened = RT_TRUE;
    rt_hw_interrupt_umask(IRQ_PLIC(VE_IRQ));
    rt_mutex_release(&ve.engine);
    return &ve;
}

int drv_ve_close(struct aic_ve_client *client)
{
    int ret;
    if (client != &ve || !ve.initialized || rt_interrupt_get_nest()) return -RT_EINVAL;
    if (ve.owner == rt_thread_self()) return -RT_EBUSY;
    ret = rt_mutex_take(&ve.engine, rt_tick_from_millisecond(VE_TIMEOUT_MS));
    if (ret) return ret;
    if (!ve.opened) { rt_mutex_release(&ve.engine); return -RT_EINVAL; }
    ve.opened = RT_FALSE;
    ret = ve_reset_hw();
    hal_reset_assert(RESET_VE);
    hal_clk_disable(CLK_VE);
    rt_mutex_release(&ve.engine);
    return ret;
}

int drv_ve_control(struct aic_ve_client *client, int cmd, void *arg)
{
    int ret;
    rt_base_t level;
    if (client != &ve || !ve.initialized || !rt_thread_self() || rt_interrupt_get_nest()) return -RT_EINVAL;
    if (cmd == IOC_VE_GET_INFO)
    {
        struct aic_ve_info *info = arg;
        if (!info) return -RT_EINVAL;
        info->reg_base = VE_BASE;
        info->clock_hz = VE_CLOCK;
        info->timeout_ms = VE_TIMEOUT_MS;
        return RT_EOK;
    }
    if (cmd == IOC_VE_GET_CLIENT)
    {
        if (ve.owner == rt_thread_self()) return -RT_EBUSY;
        ret = rt_mutex_take(&ve.engine, rt_tick_from_millisecond(VE_TIMEOUT_MS));
        if (ret) return ret;
        /* An opener/closer may have run while this caller waited. */
        if (!ve.opened) { rt_mutex_release(&ve.engine); return -RT_EIO; }
        level = rt_hw_interrupt_disable();
        ve.status = 0;
        ve.completed = RT_FALSE;
        rt_sem_control(&ve.done, RT_IPC_CMD_RESET, RT_NULL);
        ve.owner = rt_thread_self();
        rt_hw_interrupt_enable(level);
        return RT_EOK;
    }
    if (!ve.opened || ve.owner != rt_thread_self()) return -RT_EPERM;
    switch (cmd)
    {
    case IOC_VE_WAIT:
    {
        struct wait_info *info = arg;
        if (!info || info->wait_time < 0 || info->wait_time > VE_TIMEOUT_MS)
            return -RT_EINVAL;
        info->reg_status = 0;
        ret = rt_sem_take(&ve.done, rt_tick_from_millisecond(info->wait_time));
        if (ret)
        {
            int reset_ret = ve_reset_hw();
            return reset_ret ? reset_ret : ret;
        }
        aicos_dma_sync();
        level = rt_hw_interrupt_disable();
        ve.completed = RT_TRUE;
        info->reg_status = ve.status;
        ve.status = 0;
        rt_hw_interrupt_enable(level);
        return RT_EOK;
    }
    case IOC_VE_RESET:
        return ve_reset_hw();
    case IOC_VE_PUT_CLIENT:
        /* Preserve completed codec state between frames. Without a
         * successful WAIT/RESET, PUT is an abort and must stop DMA first. */
        ret = ve.completed ? RT_EOK : ve_reset_hw();
        level = rt_hw_interrupt_disable();
        ve.owner = RT_NULL;
        rt_hw_interrupt_enable(level);
        rt_mutex_release(&ve.engine);
        return ret;
    default:
        return -RT_ENOSYS;
    }
}

static rt_err_t ve_open(rt_device_t dev, rt_uint16_t flags)
{
    (void)dev; (void)flags;
    return drv_ve_open() ? RT_EOK : -RT_EBUSY;
}
static rt_err_t ve_close(rt_device_t dev)
{
    int ret = drv_ve_close(&ve);
    /* rt_device_close decrements ref_count BEFORE invoking us. Preserve
     * the standalone open on failure, so PUT and a later close can retry. */
    if (ret) dev->ref_count++;
    return ret;
}
static rt_err_t ve_control(rt_device_t dev, int cmd, void *arg)
{
    (void)dev;
    return drv_ve_control(&ve, cmd, arg);
}
static const struct rt_device_ops ve_ops =
{
    .open = ve_open,
    .close = ve_close,
    .control = ve_control,
};
static int rt_hw_ve_init(void)
{
    int ret = rt_mutex_init(&ve.engine, "ve", RT_IPC_FLAG_PRIO);
    if (ret) return ret;
    ret = rt_sem_init(&ve.done, "veirq", 0, RT_IPC_FLAG_FIFO);
    if (ret) { rt_mutex_detach(&ve.engine); return ret; }
    rt_hw_interrupt_install(IRQ_PLIC(VE_IRQ), ve_isr, &ve, "ve");
    rt_hw_interrupt_mask(IRQ_PLIC(VE_IRQ));
    ve.dev.type = RT_Device_Class_Miscellaneous;
    ve.dev.ops = &ve_ops;
    ret = rt_device_register(&ve.dev, "ve", RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_STANDALONE);
    if (ret)
    {
        rt_sem_detach(&ve.done);
        rt_mutex_detach(&ve.engine);
        return ret;
    }
    ve.initialized = RT_TRUE;
    return RT_EOK;
}
INIT_DEVICE_EXPORT(rt_hw_ve_init);
