/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x watchdog driver.
 *
 * The vendor HAL (libraries/soc/d21x/hal/hal_wdt.c) implements the register-protected
 * watchdog.  This adapter registers the standard RT-Thread watchdog device.
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "hal_wdt.h"
#include "aic_hal_clk.h"

#define AIC_WDT_DEFAULT_TIMEOUT 10
#define AIC_WDT_DEFAULT_CLR_SEC 3

struct aic_wdt_dev
{
    rt_watchdog_t wdt;
    struct aic_wdt chan;
    rt_bool_t running;
};

static struct aic_wdt_dev aic_wdt;

static rt_err_t aic_wdt_init(rt_watchdog_t *wdt)
{
    struct aic_wdt_dev *dev = (struct aic_wdt_dev *)wdt;

    if (hal_clk_enable_deassertrst(CLK_WDT) < 0)
        return -RT_ERROR;

    /* Make sure the write protection is released before programming. */
    hal_wdt_reg_protect(0);

    dev->chan.timeout = AIC_WDT_DEFAULT_TIMEOUT;
    dev->chan.rst_thd = AIC_WDT_DEFAULT_TIMEOUT;
    dev->chan.irq_thd = 0;
    dev->chan.clr_thd = AIC_WDT_DEFAULT_CLR_SEC;

    hal_wdt_switch_chan(0);
    hal_wdt_clr_thd_set(0, &dev->chan);
    hal_wdt_irq_thd_set(0, &dev->chan);
    hal_wdt_rst_thd_set(0, &dev->chan);
    hal_wdt_op_clr(0);

    return RT_EOK;
}

static rt_err_t aic_wdt_control(rt_watchdog_t *wdt, int cmd, void *arg)
{
    struct aic_wdt_dev *dev = (struct aic_wdt_dev *)wdt;
    rt_uint32_t value;

    switch (cmd)
    {
    case RT_DEVICE_CTRL_WDT_GET_TIMEOUT:
        *(rt_uint32_t *)arg = dev->chan.timeout;
        break;

    case RT_DEVICE_CTRL_WDT_SET_TIMEOUT:
        value = *(rt_uint32_t *)arg;
        if (value < WDT_MIN_TIMEOUT || value > WDT_MAX_TIMEOUT)
            return -RT_EINVAL;

        dev->chan.timeout = value;
        dev->chan.rst_thd = value;
        /*
         * The hardware only accepts a counter clear while the counter is
         * between clr_thd and rst_thd (the vendor clear window).  The
         * vendor driver keeps clr_thd inside [0, rst_thd - 2]; leaving
         * only rst_thd - 1 would narrow the usable feed window to one
         * second and cause early feeds to be silently ignored.
         */
        if (value < 3)
            dev->chan.clr_thd = 0;
        else if (dev->chan.clr_thd > value - 2)
            dev->chan.clr_thd = value - 2;

        hal_wdt_rst_thd_set(0, &dev->chan);
        hal_wdt_clr_thd_set(0, &dev->chan);
        hal_wdt_op_clr(0);
        break;

    case RT_DEVICE_CTRL_WDT_GET_TIMELEFT:
        *(rt_uint32_t *)arg = hal_wdt_remain(&dev->chan);
        break;

    case RT_DEVICE_CTRL_WDT_KEEPALIVE:
        hal_wdt_op_clr(0);
        break;

    case RT_DEVICE_CTRL_WDT_START:
        if (!dev->running)
        {
            hal_wdt_enable(1, 0);
            dev->running = RT_TRUE;
        }
        break;

    case RT_DEVICE_CTRL_WDT_STOP:
        hal_wdt_irq_enable(0);
        hal_wdt_enable(0, 0);
        dev->running = RT_FALSE;
        break;

    default:
        return -RT_EINVAL;
    }

    return RT_EOK;
}

static const struct rt_watchdog_ops aic_wdt_ops =
{
    .init = aic_wdt_init,
    .control = aic_wdt_control,
};

int rt_hw_wdt_init(void)
{
    aic_wdt.wdt.ops = &aic_wdt_ops;
    return rt_hw_watchdog_register(&aic_wdt.wdt, "wdt",
                                   RT_DEVICE_FLAG_RDWR, &aic_wdt);
}
INIT_DEVICE_EXPORT(rt_hw_wdt_init);
