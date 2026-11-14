/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ArtInChip D21x RTC driver.
 *
 * The vendor HAL (libraries/soc/d21x/hal/hal_rtc.c, RTC V1.0) is reused.  The D21x
 * RTC counts seconds from an arbitrary epoch; this BSP exposes the same
 * 2020-05-20 epoch used by Luban-Lite so that images are interchangeable.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <time.h>

#ifdef RT_USING_ALARM
#include <drivers/dev_alarm.h>
#endif

#include "hal_rtc.h"

#define AIC_RTC_EPOCH       1589932800ULL /* 2020-05-20 00:00:00 UTC */

static rt_rtc_dev_t aic_rtc;
static rt_bool_t aic_rtc_inited;

#ifdef RT_USING_ALARM
static int aic_rtc_alarm_event(void)
{
    rt_alarm_update(RT_NULL, 1);
    return 0;
}
#endif

static rt_err_t aic_rtc_init(void)
{
    if (aic_rtc_inited)
        return RT_EOK;

    if (hal_rtc_init() < 0)
        return -RT_ERROR;

    hal_rtc_cali(AIC_RTC_CLK_RATE);

#ifdef RT_USING_ALARM
    hal_rtc_register_callback(aic_rtc_alarm_event);
    aicos_request_irq(RTC_IRQn, hal_rtc_irq, 0, "rtc", RT_NULL);
#endif

    aic_rtc_inited = RT_TRUE;

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

static rt_err_t aic_rtc_get_alarm(struct rt_rtc_wkalarm *alarm)
{
    struct tm tm = {0};
    time_t sec;
    u32 value = 0;

    alarm->enable = hal_rtc_read_alarm(&value) ? RT_TRUE : RT_FALSE;

    sec = (time_t)((u64)value + AIC_RTC_EPOCH);
    gmtime_r(&sec, &tm);

    alarm->tm_sec  = tm.tm_sec;
    alarm->tm_min  = tm.tm_min;
    alarm->tm_hour = tm.tm_hour;
    alarm->tm_mday = tm.tm_mday;
    alarm->tm_mon  = tm.tm_mon;
    alarm->tm_year = tm.tm_year;

    return RT_EOK;
}

static rt_err_t aic_rtc_set_alarm(struct rt_rtc_wkalarm *alarm)
{
    struct tm tm_now;
    struct tm tm_alarm;
    time_t now;
    time_t sec;

    if (!alarm->enable)
    {
        hal_rtc_alarm_disable();
        return RT_EOK;
    }

    aic_rtc_get_secs(&now);
    gmtime_r(&now, &tm_now);
    tm_alarm = tm_now;

    /*
     * The RT-Thread alarm framework supplies a full date for one-shot
     * alarms and only a time of day for the repeating kinds (the date
     * fields are then zero or RT_ALARM_TM_NOW).  Use the supplied date
     * when it is valid, otherwise arm the next occurrence of the given
     * time of day.
     */
    if (alarm->tm_year >= 0 && alarm->tm_mon >= 0 && alarm->tm_mon <= 11 &&
        alarm->tm_mday >= 1 && alarm->tm_mday <= 31)
    {
        tm_alarm.tm_year = alarm->tm_year;
        tm_alarm.tm_mon  = alarm->tm_mon;
        tm_alarm.tm_mday = alarm->tm_mday;
        tm_alarm.tm_hour = alarm->tm_hour;
        tm_alarm.tm_min  = alarm->tm_min;
        tm_alarm.tm_sec  = alarm->tm_sec;
        sec = timegm(&tm_alarm);
    }
    else
    {
        tm_alarm.tm_hour = alarm->tm_hour;
        tm_alarm.tm_min  = alarm->tm_min;
        tm_alarm.tm_sec  = alarm->tm_sec;
        sec = timegm(&tm_alarm);
        if (sec <= now)
            sec += 24 * 3600;
    }

    if (sec < (time_t)AIC_RTC_EPOCH)
        return -RT_EINVAL;

    hal_rtc_set_alarm((u32)(sec - AIC_RTC_EPOCH));

    return RT_EOK;
}

static const struct rt_rtc_ops aic_rtc_ops =
{
    .init = aic_rtc_init,
    .get_secs = aic_rtc_get_secs,
    .set_secs = aic_rtc_set_secs,
    .get_alarm = aic_rtc_get_alarm,
    .set_alarm = aic_rtc_set_alarm,
    .get_timeval = RT_NULL,
    .set_timeval = RT_NULL,
};

int rt_hw_rtc_init(void)
{
    aic_rtc.ops = &aic_rtc_ops;

    /*
     * Initialize the hardware here rather than waiting for the first
     * rt_device_open(): the alarm framework programs the comparator
     * through rt_device_control() without opening the device.
     */
    if (aic_rtc_init() != RT_EOK)
        return -RT_ERROR;

    return rt_hw_rtc_register(&aic_rtc, "rtc", RT_DEVICE_FLAG_RDWR, RT_NULL);
}
INIT_DEVICE_EXPORT(rt_hw_rtc_init);
