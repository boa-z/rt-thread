/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/sensor.h>

#include "utest.h"

#ifdef BSP_USING_TSEN

#define TC_TSEN_READS          3
#define TC_TSEN_DELAY_MS       300
/* The sensor reports 0.1 degC units; accept the full industrial range. */
#define TC_TSEN_MIN_DC         (-400)
#define TC_TSEN_MAX_DC         (1250)

static void tc_tsen_read(void)
{
    struct rt_sensor_info info;
    struct rt_sensor_data data;
    rt_device_t dev;
    rt_ssize_t n;
    rt_err_t ret;
    int i;

    dev = rt_device_find("temp_tsen_cpu");
    uassert_not_null(dev);
    if (dev == RT_NULL)
        return;

    ret = rt_device_open(dev, RT_DEVICE_FLAG_RDONLY);
    uassert_int_equal(ret, RT_EOK);
    if (ret != RT_EOK)
        return;

    ret = rt_device_control(dev, RT_SENSOR_CTRL_GET_INFO, &info);
    uassert_int_equal(ret, RT_EOK);
    uassert_int_equal(info.type, RT_SENSOR_CLASS_TEMP);
    uassert_int_equal(info.unit, RT_SENSOR_UNIT_DCELSIUS);

    for (i = 0; i < TC_TSEN_READS; i++)
    {
        rt_memset(&data, 0, sizeof(data));
        n = rt_device_read(dev, 0, &data, 1);
        uassert_int_equal(n, 1);
        if (n != 1)
            break;

        uassert_int_equal(data.type, RT_SENSOR_CLASS_TEMP);
        uassert_true(data.data.temp >= TC_TSEN_MIN_DC);
        uassert_true(data.data.temp <= TC_TSEN_MAX_DC);
        rt_kprintf("tsen: %s = %d.%d C\n", dev->parent.name,
                   data.data.temp / 10, data.data.temp % 10);

        rt_thread_mdelay(TC_TSEN_DELAY_MS);
    }

    uassert_int_equal(rt_device_close(dev), RT_EOK);
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_tsen_read);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.tsen", RT_NULL, RT_NULL, 10);

#endif /* BSP_USING_TSEN */
