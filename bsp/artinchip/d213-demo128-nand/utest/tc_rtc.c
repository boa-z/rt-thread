/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <time.h>

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

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_rtc_advance);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.rtc", RT_NULL, RT_NULL, 10);

#endif /* BSP_USING_RTC */
