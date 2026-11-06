/*
 * Copyright (c) 2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Read-only SPI NAND regression tests.
 *
 * The tests never program, erase or mark a NAND block.  They do verify
 * that those operations are rejected by the driver and that the read
 * path is repeatable and bounds checked.
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "utest.h"
#include "drv_spinand.h"

#ifdef BSP_USING_SPINAND_MTD_NAND

#define TC_SPINAND_REPEAT_COUNT 100

static void tc_spinand_info(void)
{
    struct aic_spinand_ro_info info;

    if (aic_spinand_ro_ready() != RT_EOK)
    {
        rt_kprintf("tc_spinand: device not initialised, skipping\n");
        return;
    }

    uassert_int_equal(aic_spinand_ro_get_info(&info), RT_EOK);
    uassert_int_not_equal(info.id[0], 0x00);
    uassert_int_not_equal(info.id[0], 0xFF);
    uassert_true(info.page_size >= 512);
    uassert_true(info.oob_size > 0);
    uassert_true(info.pages_per_block > 0);
    uassert_true(info.block_count > 0);
    rt_kprintf("tc_spinand: %s id=%02X %02X %02X, %u+%u, %u x %u\n",
               info.model, info.id[0], info.id[1], info.id[2],
               info.page_size, info.oob_size,
               info.pages_per_block, info.block_count);
}

static void tc_spinand_repeat_read(void)
{
    struct rt_mtd_nand_device *dev;
    rt_uint8_t *first;
    rt_uint8_t *current;
    rt_uint32_t i;
    rt_err_t ret;

    dev = (struct rt_mtd_nand_device *)rt_device_find("os");
    if (dev == RT_NULL || dev->parent.type != RT_Device_Class_MTD)
    {
        rt_kprintf("tc_spinand: 'os' MTD device missing, skipping\n");
        return;
    }

    first = (rt_uint8_t *)rt_malloc(dev->page_size);
    current = (rt_uint8_t *)rt_malloc(dev->page_size);
    uassert_not_null(first);
    uassert_not_null(current);
    if (first == RT_NULL || current == RT_NULL)
    {
        rt_free(first);
        rt_free(current);
        return;
    }

    ret = rt_mtd_nand_read(dev, 0, first, dev->page_size, RT_NULL, 0);
    uassert_true(ret == RT_MTD_EOK || ret == RT_MTD_EECC_CORRECT);
    if (ret != RT_MTD_EOK && ret != RT_MTD_EECC_CORRECT)
    {
        rt_free(first);
        rt_free(current);
        return;
    }

    for (i = 0; i < TC_SPINAND_REPEAT_COUNT; i++)
    {
        ret = rt_mtd_nand_read(dev, 0, current, dev->page_size, RT_NULL, 0);
        if (ret != RT_MTD_EOK && ret != RT_MTD_EECC_CORRECT)
        {
            rt_kprintf("tc_spinand: repeat read %u failed (%d)\n", i, ret);
            uassert_true(0);
            break;
        }
        if (rt_memcmp(first, current, dev->page_size) != 0)
        {
            rt_kprintf("tc_spinand: page 0 changed on read %u\n", i);
            uassert_true(0);
            break;
        }
    }
    uassert_int_equal(i, TC_SPINAND_REPEAT_COUNT);

    rt_free(first);
    rt_free(current);
}

static void tc_spinand_bounds(void)
{
    struct rt_mtd_nand_device *dev;
    rt_uint8_t *buf;
    rt_uint32_t out_page;
    rt_err_t ret;
    int result = SPINAND_RO_EIO;

    dev = (struct rt_mtd_nand_device *)rt_device_find("os");
    if (dev == RT_NULL || dev->parent.type != RT_Device_Class_MTD)
    {
        rt_kprintf("tc_spinand: 'os' MTD device missing, skipping\n");
        return;
    }

    buf = (rt_uint8_t *)rt_malloc(dev->page_size);
    uassert_not_null(buf);
    if (buf == RT_NULL)
        return;

    out_page = (rt_uint32_t)dev->block_total * dev->pages_per_block;
    ret = rt_mtd_nand_read(dev, out_page, buf, dev->page_size, RT_NULL, 0);
    uassert_true(ret != RT_MTD_EOK);
    uassert_int_not_equal(rt_mtd_nand_check_block(dev, dev->block_total),
                          RT_MTD_EOK);

    rt_free(buf);

    /* Raw helper must also reject out-of-range physical pages. */
    uassert_int_equal(aic_spinand_ro_read_page(0xFFFFFFFFU, RT_NULL, 0,
                                               RT_NULL, 0, &result),
                      SPINAND_RO_EINVAL);
}

static void tc_spinand_write_rejected(void)
{
    struct rt_mtd_nand_device *dev;
    rt_uint8_t pattern[8] = {0};

    dev = (struct rt_mtd_nand_device *)rt_device_find("os");
    if (dev == RT_NULL || dev->parent.type != RT_Device_Class_MTD)
    {
        rt_kprintf("tc_spinand: 'os' MTD device missing, skipping\n");
        return;
    }

    /* None of these calls may reach a NAND program/erase command. */
    uassert_int_equal(rt_mtd_nand_write(dev, 0, pattern, sizeof(pattern),
                                       RT_NULL, 0), -RT_EPERM);
    uassert_int_equal(rt_mtd_nand_erase_block(dev, 0), -RT_EPERM);
    uassert_int_equal(rt_mtd_nand_mark_badblock(dev, 0), -RT_EPERM);
    uassert_int_equal(rt_mtd_nand_move_page(dev, 0, 1), -RT_EPERM);

    /* The generic device entry points must not fake success either. */
    uassert_int_equal(rt_device_open(&dev->parent, RT_DEVICE_FLAG_RDONLY),
                      RT_EOK);
    uassert_int_equal(rt_device_read(&dev->parent, 0, pattern,
                                     sizeof(pattern)), -RT_ENOSYS);
    uassert_int_equal(rt_device_write(&dev->parent, 0, pattern,
                                      sizeof(pattern)), -RT_EPERM);
    uassert_int_equal(rt_device_close(&dev->parent), RT_EOK);
}

static void tc_spinand_check_block(void)
{
    struct aic_spinand_ro_info info;
    struct rt_mtd_nand_device *dev;
    rt_bool_t bad = RT_FALSE;
    rt_err_t ret;

    if (aic_spinand_ro_get_info(&info) != RT_EOK)
    {
        rt_kprintf("tc_spinand: device not initialised, skipping\n");
        return;
    }

    ret = aic_spinand_ro_check_block(0, &bad);
    uassert_int_equal(ret, RT_EOK);
    if (ret == RT_EOK)
        rt_kprintf("tc_spinand: physical block 0 is %s\n",
                   bad ? "factory bad" : "good");

    dev = (struct rt_mtd_nand_device *)rt_device_find("os");
    if (dev == RT_NULL || dev->parent.type != RT_Device_Class_MTD)
        return;

    ret = rt_mtd_nand_check_block(dev, 0);
    uassert_true(ret == RT_EOK || ret == -RT_ERROR);
    uassert_int_not_equal(ret, RT_MTD_EIO);
}

static void testcase(void)
{
    UTEST_UNIT_RUN(tc_spinand_info);
    UTEST_UNIT_RUN(tc_spinand_check_block);
    UTEST_UNIT_RUN(tc_spinand_repeat_read);
    UTEST_UNIT_RUN(tc_spinand_bounds);
    UTEST_UNIT_RUN(tc_spinand_write_rejected);
}
UTEST_TC_EXPORT(testcase, "bsp.d213.spinand.ro", RT_NULL, RT_NULL, 120);

#endif /* BSP_USING_SPINAND_MTD_NAND */
