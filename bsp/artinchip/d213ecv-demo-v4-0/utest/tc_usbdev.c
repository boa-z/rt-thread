/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * D213 USB0 device-mode regression.
 *
 * The d213-usbdev-utest image enables BSP_USING_USB0_DEVICE, so
 * board/usb_device.c initialises the AIC UDC and registers one CDC
 * ACM function as the character device "usb-acm0".  No host cable is
 * required: the controller and the chardev must exist right after
 * boot.  Enumeration itself is checked through "usb_dev_status" on the
 * bench.
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "utest.h"

#if defined(BSP_USING_USB0_DEVICE)

#include "aic_hal_clk.h"
#include "aic_hal_reset.h"
#include "aic_clk_id.h"
#include "aic_reset_id.h"
#include "usbd_core.h"

static void tc_usbdev_chardev(void)
{
    rt_device_t dev = rt_device_find("usb-acm0");

    uassert_not_null(dev);
    if (dev == RT_NULL)
        return;

    uassert_int_equal(dev->type, RT_Device_Class_Char);
    rt_kprintf("tc_usbdev: usb-acm0 registered, configured=%d\n",
               usb_device_is_configured(0));
}

static void tc_usbdev_clock(void)
{
    uassert_int_equal(g_usbdev_bus[0].reg_base, USB_DEV_BASE);
    uassert_true(hal_clk_is_enabled(CLK_USBD));
    uassert_true(hal_clk_is_enabled(CLK_USB_PHY0));
    uassert_true(hal_reset_status(RESET_USBD));
    uassert_true(hal_reset_status(RESET_USBPHY0));
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_usbdev_chardev);
    UTEST_UNIT_RUN(tc_usbdev_clock);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.usbdev", RT_NULL, RT_NULL, 20);

#endif /* BSP_USING_USB0_DEVICE */
