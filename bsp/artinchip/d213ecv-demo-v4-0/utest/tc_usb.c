/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * USB host / mass-storage regression.
 *
 * The d213-usb-utest image enables RT_CHERRYUSB_HOST with the AIC EHCI
 * port and the MSC class.  platform/rtthread/rt_usbh_msc.c registers the
 * attached U disk as /dev/sdN and mounts its FAT volume, so this case
 * only has to prove the block path: device appears, geometry is sane,
 * two reads of the same sectors match, and sector 0 carries a boot
 * signature (MBR or FAT BPB).  It deliberately does not write to the
 * disk.
 *
 * Needs a U disk on the enabled host port (USB0 or USB1).  If
 * enumeration never completes, the case fails rather than silently
 * skipping.
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "utest.h"

#if defined(BSP_USING_USB0_HOST) || defined(BSP_USING_USB1_HOST)

#define TC_USB_DEV_NAME       "/dev/sda"
#define TC_USB_SECTOR_SIZE    512
#define TC_USB_TEST_SECTORS   8
#define TC_USB_WAIT_MS        8000

static rt_uint8_t tc_usb_first[TC_USB_TEST_SECTORS * TC_USB_SECTOR_SIZE]
    __attribute__((aligned(64)));
static rt_uint8_t tc_usb_second[TC_USB_TEST_SECTORS * TC_USB_SECTOR_SIZE]
    __attribute__((aligned(64)));

static void tc_usb_udisk(void)
{
    struct rt_device_blk_geometry geometry;
    rt_device_t dev = RT_NULL;
    rt_tick_t deadline;
    rt_size_t got;
    rt_err_t ret;
    rt_bool_t shared = RT_FALSE;

    deadline = rt_tick_get() + rt_tick_from_millisecond(TC_USB_WAIT_MS);
    while (rt_tick_get() < deadline)
    {
        dev = rt_device_find(TC_USB_DEV_NAME);
        if (dev != RT_NULL)
            break;
        rt_thread_mdelay(100);
    }

    uassert_not_null(dev);
    if (dev == RT_NULL)
        return;

    /*
     * platform/rtthread/rt_usbh_msc.c mounts the U disk as soon as it is
     * enumerated and keeps the standalone device open.  Then open()
     * returns -RT_EBUSY here; the device is usable through the existing
     * handle, so run the block checks anyway and do not close it.
     */
    rt_thread_mdelay(200);
    ret = rt_device_open(dev, RT_DEVICE_OFLAG_RDONLY);
    if (ret == -RT_EBUSY)
        shared = RT_TRUE;
    else
        uassert_int_equal(ret, RT_EOK);

    rt_memset(&geometry, 0, sizeof(geometry));
    uassert_int_equal(rt_device_control(dev, RT_DEVICE_CTRL_BLK_GETGEOME,
                                        &geometry),
                      RT_EOK);
    uassert_int_equal(geometry.bytes_per_sector, TC_USB_SECTOR_SIZE);
    uassert_true(geometry.sector_count >= TC_USB_TEST_SECTORS);

    got = rt_device_read(dev, 0, tc_usb_first, TC_USB_TEST_SECTORS);
    uassert_int_equal(got, TC_USB_TEST_SECTORS);
    got = rt_device_read(dev, 0, tc_usb_second, TC_USB_TEST_SECTORS);
    uassert_int_equal(got, TC_USB_TEST_SECTORS);
    uassert_int_equal(rt_memcmp(tc_usb_first, tc_usb_second,
                                sizeof(tc_usb_first)),
                      0);

    /*
     * MBR disks carry 0x55AA at offset 510 of the first sector; a
     * superfloppy FAT volume has the same signature in its boot sector.
     * All-zero data means the read never reached the device.
     */
    uassert_int_equal(tc_usb_first[510], 0x55);
    uassert_int_equal(tc_usb_first[511], 0xAA);

    if (!shared)
        rt_device_close(dev);
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_usb_udisk);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.usb", RT_NULL, RT_NULL, 20);

#endif /* BSP_USING_USB0_HOST || BSP_USING_USB1_HOST */
