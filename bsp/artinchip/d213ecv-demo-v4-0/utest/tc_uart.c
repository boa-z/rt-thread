/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * UART0 console availability.  This deliberately does not change the
 * console configuration or switch the RX path back to interrupt mode.
 * Receiving the utest output on the host proves the TX path; the host
 * script proves RX by delivering the commands themselves.
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "utest.h"

static void tc_uart0_console(void)
{
    static const char marker[] = "\r\n[utest] uart0 console tx\r\n";
    rt_device_t dev;

    dev = rt_device_find(RT_CONSOLE_DEVICE_NAME);
    uassert_not_null(dev);
    if (dev == RT_NULL)
    {
        return;
    }

    uassert_true((dev->open_flag & RT_DEVICE_OFLAG_OPEN) != 0);
    uassert_int_equal((int)rt_device_write(dev, 0, marker,
                                           sizeof(marker) - 1),
                      (int)(sizeof(marker) - 1));
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_uart0_console);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.uart0", RT_NULL, RT_NULL, 10);
