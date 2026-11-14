/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * D21x general-purpose ADC (GPAI) adapter for the RT-Thread ADC
 * framework.
 *
 * The GPAI HAL (libraries/soc/d21x/hal/hal_gpai.c) is taken from
 * Luban-Lite and is unchanged.  The vendor driver bsp/artinchip/drv/gpai
 * extends rt_adc_ops with DMA, FIFO and alarm commands that upstream
 * RT-Thread does not have; this adapter only implements the four
 * upstream operations (enabled/convert/get_resolution/get_vref) and uses
 * the HAL's single-shot CPU-interrupt path.
 *
 * The channel table is BSP configuration, not a copy of the vendor
 * gpai_info.c: it lists only the channels this BSP builds and fixes
 * single-shot sampling with CPU interrupt completion.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/adc.h>

#include "aic_core.h"
#include "hal_adcim.h"
#include "hal_gpai.h"

#define AIC_GPAI_NAME       "gpai"

/*
 * The GPAI analog front-end is shared with ADCIM: the common block has
 * to be clocked and its offset calibrated before a channel is enabled.
 */
#define AIC_LDO30_CFG       (SYSCFG_BASE + 0x020)
#define AIC_LDO30_VAL_MASK  0x7U

struct aic_gpai_ch aic_gpai_chs[] =
{
#ifdef BSP_USING_GPAI6
    {
        .id = 6,
        .available = 1,
        .adc_acq = BSP_GPAI6_ADC_ACQ,
        .obtain_data_mode = AIC_GPAI_OBTAIN_DATA_BY_CPU,
        .mode = AIC_GPAI_MODE_SINGLE,
        .fifo_depth = 32,
    },
#endif
};

/*
 * rt_adc_voltage() enables and disables the channel around its own
 * conversion.  The board key thread keeps channel 6 enabled, so a plain
 * boolean would let the framework tear the channel down underneath it.
 * Count references instead.
 */
static rt_uint8_t aic_gpai_refcnt[AIC_GPAI_CH_NUM];

static rt_err_t aic_gpai_enabled(struct rt_adc_device *dev,
                                 rt_int8_t ch, rt_bool_t enabled)
{
    struct aic_gpai_ch *chan = hal_gpai_ch_is_valid(ch);

    if (!chan || chan->id >= AIC_GPAI_CH_NUM)
        return -RT_EINVAL;

    if (enabled)
    {
        if (aic_gpai_refcnt[chan->id]++ > 0)
            return RT_EOK;

        hal_gpai_clk_get(chan);
        chan->irq_count = 0;
        chan->complete = aicos_sem_create(0);
        if (!chan->complete)
        {
            aic_gpai_refcnt[chan->id] = 0;
            return -RT_ENOMEM;
        }

        if (aich_gpai_ch_init(chan, chan->pclk_rate) < 0)
        {
            aicos_sem_delete(chan->complete);
            chan->complete = RT_NULL;
            aic_gpai_refcnt[chan->id] = 0;
            return -RT_ERROR;
        }
    }
    else
    {
        if (aic_gpai_refcnt[chan->id] == 0)
            return RT_EOK;
        if (--aic_gpai_refcnt[chan->id] > 0)
            return RT_EOK;

        aich_gpai_ch_deinit(chan);
        if (chan->complete)
        {
            aicos_sem_delete(chan->complete);
            chan->complete = RT_NULL;
        }
    }

    return RT_EOK;
}

static rt_err_t aic_gpai_convert(struct rt_adc_device *dev,
                                 rt_int8_t ch, rt_uint32_t *value)
{
    struct aic_gpai_ch *chan = hal_gpai_ch_is_valid(ch);
    u16 data = 0;
    int ret;

    if (!chan || !value)
        return -RT_EINVAL;

    if (!chan->enabled)
        return -RT_EBUSY;

    ret = hal_gpai_get_data(chan, &data, AIC_GPAI_TIMEOUT);
    if (ret == -ENODATA)
        return -RT_EIO;
    if (ret < 0)
        return -RT_ETIMEOUT;

    *value = data;

    return RT_EOK;
}

static rt_uint8_t aic_gpai_get_resolution(struct rt_adc_device *dev)
{
    return AIC_GPAI_NUM_BITS;
}

/*
 * GPAI is ratiometric to the LDO30 analog supply that the bootloader
 * programmed (2.8 V .. 3.15 V in 50 mV steps).  Return it in mV so that
 * rt_adc_voltage() is meaningful; the raw key ladder is decoded against
 * the same value in the board key driver.
 */
static rt_int16_t aic_gpai_get_vref(struct rt_adc_device *dev)
{
    u32 value = readl(AIC_LDO30_CFG) & AIC_LDO30_VAL_MASK;

    return (rt_int16_t)(2800U + value * 50U);
}

static const struct rt_adc_ops aic_gpai_ops =
{
    .enabled = aic_gpai_enabled,
    .convert = aic_gpai_convert,
    .get_resolution = aic_gpai_get_resolution,
    .get_vref = aic_gpai_get_vref,
};

static int drv_gpai_init(void)
{
    static struct rt_adc_device dev;
    rt_err_t ret;

    if (hal_adcim_probe() < 0)
    {
        rt_kprintf("gpai: ADCIM probe failed\n");
        return -RT_ERROR;
    }

    if (hal_gpai_init() < 0)
    {
        rt_kprintf("gpai: clock/reset init failed\n");
        return -RT_ERROR;
    }

    aicos_request_irq(GPAI_IRQn, aich_gpai_isr, 0, "gpai", RT_NULL);
    aich_gpai_enable(1);
    hal_gpai_set_ch_num(ARRAY_SIZE(aic_gpai_chs));

    ret = rt_hw_adc_register(&dev, AIC_GPAI_NAME, &aic_gpai_ops, RT_NULL);
    if (ret != RT_EOK)
    {
        rt_kprintf("gpai: register failed (%d)\n", ret);
        return ret;
    }

    return RT_EOK;
}
INIT_DEVICE_EXPORT(drv_gpai_init);
