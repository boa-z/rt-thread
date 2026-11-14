/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CAN controller configuration smoke test.  This uses the standard
 * RT-Thread CAN device API and does not transmit on the bus, so it does
 * not require a transceiver, termination or a peer node.
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "utest.h"

#ifdef BSP_USING_CAN0

#define TC_CAN_BAUD 500000

static void tc_can_controller_config(void)
{
    rt_device_t dev;
    struct rt_can_status status;
    rt_err_t ret;

    dev = rt_device_find("can0");
    uassert_not_null(dev);
    if (dev == RT_NULL)
    {
        return;
    }

    ret = rt_device_open(dev, RT_DEVICE_FLAG_INT_RX | RT_DEVICE_FLAG_INT_TX);
    uassert_int_equal(ret, RT_EOK);
    if (ret != RT_EOK)
    {
        return;
    }

    uassert_int_equal(rt_device_control(dev, RT_CAN_CMD_SET_BAUD,
                                        (void *)(rt_ubase_t)TC_CAN_BAUD),
                      RT_EOK);
    uassert_int_equal(rt_device_control(dev, RT_CAN_CMD_SET_MODE,
                                        (void *)(rt_ubase_t)RT_CAN_MODE_NORMAL),
                      RT_EOK);

    rt_memset(&status, 0, sizeof(status));
    uassert_int_equal(rt_device_control(dev, RT_CAN_CMD_GET_STATUS, &status),
                      RT_EOK);

    uassert_int_equal(rt_device_close(dev), RT_EOK);
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_can_controller_config);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.can0.config", RT_NULL, RT_NULL, 10);

#endif /* BSP_USING_CAN0 */
