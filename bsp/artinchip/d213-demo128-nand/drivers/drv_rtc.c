/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x RTC driver.
 *
 * The vendor HAL (vendor/hal/hal_rtc.c, RTC V1.0) is reused.  The D21x
 * RTC counts seconds from an arbitrary epoch; this BSP exposes the same
 * 2020-05-20 epoch used by Luban-Lite so that images are interchangeable.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <time.h>

#include "hal_rtc.h"

#define AIC_RTC_EPOCH       1589932800ULL /* 2020-05-20 00:00:00 UTC */

static rt_rtc_dev_t aic_rtc;

static rt_err_t aic_rtc_init(void)
{
    if (hal_rtc_init() < 0)
        return -RT_ERROR;

    hal_rtc_cali(AIC_RTC_CLK_RATE);

    return RT_EOK;
}

static rt_err_t aic_rtc_get_secs(time_t *sec)
{
    u32 value = 0;

    hal_rtc_read_time(&value);
    *sec = (time_t)((u64)value + AIC_RTC_EPOCH);

    return RT_EOK;
}

static rt_err_t aic_rtc_set_secs(time_t *sec)
{
    if (*sec < (time_t)AIC_RTC_EPOCH)
        return -RT_EINVAL;

    hal_rtc_set_time((u32)(*sec - AIC_RTC_EPOCH));

    return RT_EOK;
}

static const struct rt_rtc_ops aic_rtc_ops =
{
    .init = aic_rtc_init,
    .get_secs = aic_rtc_get_secs,
    .set_secs = aic_rtc_set_secs,
    .get_alarm = RT_NULL,
    .set_alarm = RT_NULL,
    .get_timeval = RT_NULL,
    .set_timeval = RT_NULL,
};

int rt_hw_rtc_init(void)
{
    aic_rtc.ops = &aic_rtc_ops;
    return rt_hw_rtc_register(&aic_rtc, "rtc", RT_DEVICE_FLAG_RDWR, RT_NULL);
}
INIT_DEVICE_EXPORT(rt_hw_rtc_init);
