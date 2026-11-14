/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * D21x internal thermal sensor (TSEN) adapter for the RT-Thread sensor
 * framework.
 *
 * The channel table, the single-sample flow and the SID eFuse
 * calibration are taken from Luban-Lite bsp/artinchip/drv/tsen/drv_tsen.c
 * (Copyright (c) 2022-2026, ArtInChip Technology Co., Ltd, Apache-2.0).
 * The PM hooks and the vendor-only GET_CH_INFO command are not ported.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/sensor.h>

#include "aic_core.h"
#include "hal_adcim.h"
#include "hal_tsen.h"

struct aic_tsen_dev
{
    struct rt_sensor_device dev;
    struct aic_tsen_ch *ch;
};

/*
 * Default slope/offset are the hardware fallback values used by the
 * vendor driver when the eFuse has no calibration data (CP version 0).
 * They are overridden by hal_tsen_curve_fitting() when the OTP is
 * programmed.
 */
struct aic_tsen_ch aic_tsen_chs[] =
{
#ifdef AIC_USING_TSEN_CPU
    {
        .id = 0,
        .available = 1,
        .name = "tsen_cpu",
        .mode = AIC_TSEN_MODE_SINGLE,
        .soft_mode = AIC_TSEN_SOFT_MODE_SINGLE,
        .slope  = -1132,
        .offset = 2439001,
    },
#endif
#ifdef AIC_USING_TSEN_GPAI
    {
        .id = 1,
        .available = 1,
        .name = "tsen_gpai",
        .mode = AIC_TSEN_MODE_SINGLE,
        .soft_mode = AIC_TSEN_SOFT_MODE_SINGLE,
        .slope  = -1159,
        .offset = 2450566,
    },
#endif
};

static struct aic_tsen_dev g_aic_tsen[ARRAY_SIZE(aic_tsen_chs)];

static rt_ssize_t aic_tsen_fetch(struct rt_sensor_device *sensor,
                                 void *buf, rt_size_t len)
{
    struct aic_tsen_ch *chan;
    struct rt_sensor_data *data = (struct rt_sensor_data *)buf;
    s32 temp = 0;

    if (buf == RT_NULL || len == 0)
        return 0;

    if (sensor->parent.device_id >= ARRAY_SIZE(aic_tsen_chs))
        return -RT_EINVAL;

    chan = &aic_tsen_chs[sensor->parent.device_id];

    if (!chan->available)
        return -RT_EIO;

    if (chan->mode == AIC_TSEN_MODE_SINGLE)
    {
        if (hal_tsen_get_temp(chan, &temp) < 0)
            return -RT_ETIMEOUT;
    }
    else
    {
        temp = hal_tsen_data2temp(chan);
    }

    data->type = RT_SENSOR_CLASS_TEMP;
    data->timestamp = rt_sensor_get_ts();
    data->data.temp = temp;

    return 1;
}

static rt_err_t aic_tsen_control(struct rt_sensor_device *sensor,
                                 int cmd, void *args)
{
    struct aic_tsen_ch *chan = &aic_tsen_chs[sensor->parent.device_id];
    unsigned long power;

    switch (cmd)
    {
    case RT_SENSOR_CTRL_SET_POWER:
        power = (unsigned long)args;
        if (power == RT_SENSOR_POWER_NORMAL)
        {
            if (hal_tsen_ch_init(chan, chan->pclk_rate) < 0)
                return -RT_ERROR;
            return RT_EOK;
        }
        if (power == RT_SENSOR_POWER_DOWN)
        {
            hal_tsen_ch_enable(chan->id, 0);
            return RT_EOK;
        }
        return -RT_EINVAL;
    default:
        return -RT_ERROR;
    }
}

static const struct rt_sensor_ops aic_sensor_ops =
{
    .fetch_data = aic_tsen_fetch,
    .control = aic_tsen_control,
};

static int drv_tsen_init(void)
{
    struct rt_sensor_info *info;
    struct aic_tsen_ch *chan;
    rt_size_t i;
    s32 ret;

    /*
     * The TSEN analog front-end shares the ADCIM common block.  The
     * vendor driver relies on the ADCIM driver probe having run; here it
     * is the only consumer, so probe it before enabling TSEN.
     */
    if (hal_adcim_probe() < 0)
    {
        rt_kprintf("tsen: ADCIM probe failed\n");
        return -RT_ERROR;
    }

    ret = hal_tsen_clk_init();
    if (ret < 0)
    {
        rt_kprintf("tsen: clock/reset init failed\n");
        return -RT_ERROR;
    }

    aicos_request_irq(TSEN_IRQn, hal_tsen_irq_handle, 0, "tsen", RT_NULL);
    hal_tsen_enable(1);
    hal_tsen_set_ch_num(ARRAY_SIZE(aic_tsen_chs));

    for (i = 0; i < ARRAY_SIZE(aic_tsen_chs); i++)
    {
        chan = &aic_tsen_chs[i];

        hal_tsen_pclk_get(chan);
        chan->complete = aicos_sem_create(0);
        if (chan->complete == RT_NULL)
        {
            rt_kprintf("tsen: no memory for %s semaphore\n", chan->name);
            return -RT_ENOMEM;
        }

#ifdef AIC_SID_DRV
        /* Read the per-chip calibration once; a blank eFuse leaves the
         * default slope/offset in place. */
        hal_tsen_curve_fitting(chan);
#endif
        rt_kprintf("tsen: %s pclk %u Hz, slope %d, offset %d\n",
                   chan->name, chan->pclk_rate, chan->slope, chan->offset);

        g_aic_tsen[i].ch = chan;
        info = &g_aic_tsen[i].dev.info;
        info->type = RT_SENSOR_CLASS_TEMP;
        info->vendor = RT_SENSOR_VENDOR_UNKNOWN;
        info->model = "aic-tsen";
        info->unit = RT_SENSOR_UNIT_DCELSIUS;
        info->intf_type = 0;
        info->range_max = 125;
        info->range_min = 0;
        info->period_min = 1000;
        info->fifo_max = 0;
        g_aic_tsen[i].dev.parent.device_id = i;
        g_aic_tsen[i].dev.ops = &aic_sensor_ops;

        ret = rt_hw_sensor_register(&g_aic_tsen[i].dev, chan->name,
                                    RT_DEVICE_FLAG_RDWR, RT_NULL);
        if (ret != RT_EOK)
        {
            rt_kprintf("tsen: register %s failed (%d)\n", chan->name, ret);
            return -RT_ERROR;
        }
    }

    return RT_EOK;
}
INIT_DEVICE_EXPORT(drv_tsen_init);

#ifdef RT_USING_FINSH
#include <finsh.h>

static void tsen_status(int argc, char **argv)
{
    rt_size_t i;

    (void)argc;
    (void)argv;

    for (i = 0; i < ARRAY_SIZE(aic_tsen_chs); i++)
    {
        hal_tsen_status_show(&aic_tsen_chs[i]);
        rt_kprintf("   slope %d, offset %d, pclk %u Hz\n",
                   aic_tsen_chs[i].slope, aic_tsen_chs[i].offset,
                   aic_tsen_chs[i].pclk_rate);
    }
}
MSH_CMD_EXPORT(tsen_status, show internal temperature sensor status);
#endif
