/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * GPIO registration and pin-name decoding.  This case is intentionally
 * non-destructive: it does not change pinmux or drive a board pin.  Use
 * the gpio_toggle / pin MSH commands for an oscilloscope or LED test.
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "utest.h"

static void tc_gpio_presence(void)
{
    rt_device_t dev;

    dev = rt_device_find("gpio");
    uassert_not_null(dev);

    uassert_true(rt_pin_get("PA.0") >= 0);
    uassert_true(rt_pin_get("PG.31") >= 0);
    uassert_true(rt_pin_get("PZ.0") < 0);
    uassert_true(rt_pin_get("PA.32") < 0);
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_gpio_presence);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.gpio", RT_NULL, RT_NULL, 10);
