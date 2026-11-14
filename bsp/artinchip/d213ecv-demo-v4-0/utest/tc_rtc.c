/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <time.h>

#ifdef RT_USING_ALARM
#include <drivers/dev_alarm.h>
#endif

#include "utest.h"

#ifdef BSP_USING_RTC

static void tc_rtc_advance(void)
{
    rt_device_t dev;
    time_t first = 0;
    time_t second = 0;
    rt_err_t ret;

    dev = rt_device_find("rtc");
    uassert_not_null(dev);
    if (dev == RT_NULL)
    {
        return;
    }

    ret = rt_device_open(dev, RT_DEVICE_FLAG_RDWR);
    uassert_int_equal(ret, RT_EOK);
    if (ret != RT_EOK)
    {
        return;
    }

    ret = rt_device_control(dev, RT_DEVICE_CTRL_RTC_GET_TIME, &first);
    uassert_int_equal(ret, RT_EOK);
    if (ret == RT_EOK)
    {
        rt_thread_mdelay(1100);
        ret = rt_device_control(dev, RT_DEVICE_CTRL_RTC_GET_TIME, &second);
        uassert_int_equal(ret, RT_EOK);
        if (ret == RT_EOK)
        {
            uassert_true(second > first);
            uassert_true((second - first) <= 5);
        }
    }

    uassert_int_equal(rt_device_close(dev), RT_EOK);
}

#ifdef RT_USING_ALARM
#define TC_RTC_ALARM_DELAY_SEC    3
#define TC_RTC_ALARM_TIMEOUT_MS   10000

static struct rt_semaphore rtc_alarm_sem;
static volatile rt_uint32_t rtc_alarm_count;

static void rtc_alarm_cb(rt_alarm_t alarm, time_t timestamp)
{
    (void)alarm;
    (void)timestamp;

    rtc_alarm_count++;
    rt_sem_release(&rtc_alarm_sem);
}

static void tc_rtc_alarm(void)
{
    struct rt_alarm_setup setup;
    struct rt_rtc_wkalarm wkalarm;
    rt_device_t dev;
    rt_alarm_t alarm;
    struct tm tm;
    time_t now = 0;
    time_t target;
    rt_err_t ret;

    dev = rt_device_find("rtc");
    uassert_not_null(dev);
    if (dev == RT_NULL)
    {
        return;
    }

    ret = rt_device_open(dev, RT_DEVICE_FLAG_RDWR);
    uassert_int_equal(ret, RT_EOK);
    if (ret != RT_EOK)
    {
        return;
    }

    ret = rt_device_control(dev, RT_DEVICE_CTRL_RTC_GET_TIME, &now);
    uassert_int_equal(ret, RT_EOK);
    if (ret != RT_EOK)
    {
        rt_device_close(dev);
        return;
    }

    target = now + TC_RTC_ALARM_DELAY_SEC;

    rt_memset(&setup, 0, sizeof(setup));
    setup.flag = RT_ALARM_ONESHOT;
    gmtime_r(&target, &tm);
    setup.wktime = tm;

    rt_sem_init(&rtc_alarm_sem, "rtcalm", 0, RT_IPC_FLAG_FIFO);
    rtc_alarm_count = 0;

    alarm = rt_alarm_create(rtc_alarm_cb, &setup);
    uassert_not_null(alarm);
    if (alarm != RT_NULL)
    {
        ret = rt_alarm_start(alarm);
        uassert_int_equal(ret, RT_EOK);
        if (ret == RT_EOK)
        {
            ret = rt_sem_take(&rtc_alarm_sem,
                              rt_tick_from_millisecond(TC_RTC_ALARM_TIMEOUT_MS));
            uassert_int_equal(ret, RT_EOK);
            uassert_int_equal(rtc_alarm_count, 1);

            /* A one-shot alarm must disarm the hardware comparator. */
            ret = rt_device_control(dev, RT_DEVICE_CTRL_RTC_GET_ALARM, &wkalarm);
            uassert_int_equal(ret, RT_EOK);
            uassert_false(wkalarm.enable);
        }

        rt_alarm_delete(alarm);
    }

    rt_sem_detach(&rtc_alarm_sem);
    uassert_int_equal(rt_device_close(dev), RT_EOK);
}
#endif /* RT_USING_ALARM */

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_rtc_advance);
#ifdef RT_USING_ALARM
    UTEST_UNIT_RUN(tc_rtc_alarm);
#endif
}
UTEST_TC_EXPORT(testcase, "bsp.d213.rtc", RT_NULL, RT_NULL, 25);

#endif /* BSP_USING_RTC */
