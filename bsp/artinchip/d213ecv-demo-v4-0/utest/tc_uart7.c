/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * UART7 loopback test.  UART7 is the BT HCI port of the Wi-Fi/BT combo
 * module on the carplay board (PF.6..PF.9).  The module's BT stack is not
 * part of this BSP, so the port is checked with the controller's internal
 * MCR loopback: TXD is routed back to RXD and no external wiring is
 * needed.  That covers the clock, pad function, baud divisor, polled RX
 * path and the device API.  RTS/CTS operation itself can only be checked
 * against the module.
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "utest.h"
#include "board.h"

#ifdef BSP_USING_UART7

#define TC_UART7_MCR           0x10U
#define TC_UART7_MCR_LOOPBACK  0x10U
#define TC_UART7_PATTERN_LEN   32

static void tc_uart7_loopback(void)
{
    static rt_uint8_t pattern[TC_UART7_PATTERN_LEN];
    rt_uint8_t rx[TC_UART7_PATTERN_LEN];
    volatile rt_uint32_t *mcr = (volatile rt_uint32_t *)(uintptr_t)
        (AIC_UART_BASE(7) + TC_UART7_MCR);
    rt_uint32_t saved_mcr;
    rt_device_t dev;
    rt_tick_t deadline;
    rt_size_t got = 0;
    unsigned int i;

    dev = rt_device_find("uart7");
    uassert_not_null(dev);
    if (dev == RT_NULL)
    {
        return;
    }

    uassert_int_equal(rt_device_open(dev, RT_DEVICE_OFLAG_RDWR |
                                          RT_DEVICE_FLAG_INT_RX),
                      RT_EOK);

    for (i = 0; i < sizeof(pattern); i++)
    {
        pattern[i] = (rt_uint8_t)(0x20 + i);
    }

    /*
     * Modem flow control is masked off for the duration: CTS is not
     * driven by anything in this setup and would gate the transmitter.
     * The previous MCR value is restored afterwards.
     */
    saved_mcr = *mcr;
    *mcr = TC_UART7_MCR_LOOPBACK;
    rt_thread_mdelay(2);
    while (rt_device_read(dev, 0, rx, sizeof(rx)) > 0)
    {
    }

    uassert_int_equal((int)rt_device_write(dev, 0, pattern, sizeof(pattern)),
                      (int)sizeof(pattern));

    deadline = rt_tick_get() + rt_tick_from_millisecond(1000);
    while (got < sizeof(pattern) && rt_tick_get() < deadline)
    {
        rt_size_t n = rt_device_read(dev, 0, rx + got, sizeof(pattern) - got);

        if (n > 0)
        {
            got += n;
        }
        else
        {
            rt_thread_mdelay(1);
        }
    }

    *mcr = saved_mcr;

    uassert_int_equal((int)got, (int)sizeof(pattern));
    if (got == sizeof(pattern))
    {
        uassert_int_equal(rt_memcmp(rx, pattern, sizeof(pattern)), 0);
    }

    rt_device_close(dev);
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_uart7_loopback);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.uart7", RT_NULL, RT_NULL, 10);

#endif /* BSP_USING_UART7 */
