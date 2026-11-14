/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SDMC1 / SD card read-only regression test.
 *
 * The test never writes to the card.  A missing card is reported as a
 * SKIP line and the case returns; the MMCSD detection thread may need a
 * moment after boot, so the test waits for the "sd0" block device.
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "utest.h"

#ifdef BSP_USING_SDMC1

#define TC_SDCARD_WAIT_MS      3000
#define TC_SDCARD_SECTOR_SIZE  512
/*
 * 64 sectors = 32 KiB.  With 4 KiB IDMA descriptors this is one request
 * spanning eight descriptors, so the test also covers the multi-
 * descriptor DMA path rather than just a single small block.
 */
#define TC_SDCARD_TEST_SECTORS 64

static rt_device_t sdcard_dev;
static struct rt_device_blk_geometry sdcard_geometry;

static rt_bool_t sdcard_wait_ready(void)
{
    int waited = 0;

    while (waited < TC_SDCARD_WAIT_MS)
    {
        sdcard_dev = rt_device_find("sd0");
        if (sdcard_dev != RT_NULL)
        {
            return RT_TRUE;
        }

        rt_thread_mdelay(100);
        waited += 100;
    }

    return RT_FALSE;
}

static void tc_sdcard_probe(void)
{
    if (!sdcard_wait_ready())
    {
        rt_kprintf("tc_sdcard: no SD card detected (SKIP, not a pass of "
                   "the card path)\n");
        return;
    }

    uassert_int_equal(rt_device_open(sdcard_dev, RT_DEVICE_OFLAG_RDONLY),
                      RT_EOK);
    uassert_int_equal(rt_device_control(sdcard_dev,
                                        RT_DEVICE_CTRL_BLK_GETGEOME,
                                        &sdcard_geometry),
                      RT_EOK);

    rt_kprintf("tc_sdcard: block size %u, sector count %u\n",
               sdcard_geometry.block_size, sdcard_geometry.sector_count);

    uassert_int_equal(sdcard_geometry.bytes_per_sector,
                      TC_SDCARD_SECTOR_SIZE);
    uassert_true(sdcard_geometry.sector_count > 0);
    uassert_true(sdcard_geometry.sector_count >= TC_SDCARD_TEST_SECTORS);
}

static void tc_sdcard_reads_are_repeatable(void)
{
    /*
     * Keep the buffers static: two 32 KiB arrays would exceed the utest
     * thread stack (RT_UTEST_THR_STACK_SIZE, 8 KiB by default).
     */
    static rt_uint8_t first[TC_SDCARD_TEST_SECTORS * TC_SDCARD_SECTOR_SIZE];
    static rt_uint8_t second[TC_SDCARD_TEST_SECTORS * TC_SDCARD_SECTOR_SIZE];

    if (sdcard_dev == RT_NULL)
    {
        rt_kprintf("tc_sdcard: no card, read test skipped\n");
        return;
    }

    uassert_int_equal(rt_device_read(sdcard_dev, 0, first,
                                     TC_SDCARD_TEST_SECTORS),
                      TC_SDCARD_TEST_SECTORS);
    /*
     * A repeated read would also compare equal if both buffers stayed
     * zero (a DMA that never ran).  Sector 0 must carry the 0x55AA boot
     * signature, which proves the data really came from the card.
     */
    uassert_int_equal(first[510], 0x55);
    uassert_int_equal(first[511], 0xAA);

    uassert_int_equal(rt_device_read(sdcard_dev, 0, second,
                                     TC_SDCARD_TEST_SECTORS),
                      TC_SDCARD_TEST_SECTORS);
    uassert_int_equal(rt_memcmp(first, second, sizeof(first)), 0);
}

static void tc_sdcard_cleanup(void)
{
    if (sdcard_dev != RT_NULL)
    {
        rt_device_close(sdcard_dev);
        sdcard_dev = RT_NULL;
    }
}

static rt_err_t tc_sdcard_init(void)
{
    sdcard_dev = RT_NULL;
    rt_memset(&sdcard_geometry, 0, sizeof(sdcard_geometry));
    return RT_EOK;
}

static void tc_sdcard_case(void)
{
    tc_sdcard_probe();
    tc_sdcard_reads_are_repeatable();
}

UTEST_TC_EXPORT(tc_sdcard_case, "bsp.d213.sdcard", tc_sdcard_init,
                tc_sdcard_cleanup, 20);

#endif /* BSP_USING_SDMC1 */
